/*
 * KiRouter - a push-and-(sometimes-)shove PCB router
 *
 * Copyright (C) 2013  CERN
 * Copyright (C) 2016-2021 KiCad Developers, see AUTHORS.txt for contributors.
 * Author: Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include <functional>
using namespace std::placeholders;

#include <pcb_painter.h>
#include <pcbnew_settings.h>

#include <tools/pcb_grid_helper.h>
#include <wx/log.h>

#include "pns_kicad_iface.h"
#include "pns_tool_base.h"
#include "pns_arc.h"
#include "pns_solid.h"
#include "pns_dragger.h"


using namespace KIGFX;

namespace PNS {


TOOL_BASE::TOOL_BASE( const std::string& aToolName ) :
    PCB_TOOL_BASE( aToolName )
{
    m_gridHelper = nullptr;
    m_iface = nullptr;
    m_router = nullptr;
    m_cancelled = false;

    m_startItem = nullptr;

    m_endItem = nullptr;
    m_gridHelper = nullptr;

    m_cancelled = false;
}


TOOL_BASE::~TOOL_BASE()
{
    delete m_gridHelper;
    delete m_router;
    delete m_iface; // Delete after m_router because PNS::NODE dtor needs m_ruleResolver
}


void TOOL_BASE::Reset( RESET_REASON aReason )
{
    delete m_gridHelper;
    delete m_router;
    delete m_iface; // Delete after m_router because PNS::NODE dtor needs m_ruleResolver

    m_iface = new PNS_KICAD_IFACE;
    m_iface->SetBoard( board() );
    m_iface->SetView( getView() );
    m_iface->SetHostTool( this );

    m_router = new ROUTER;
    m_router->SetInterface( m_iface );
    m_router->ClearWorld();
    m_router->SyncWorld();

    m_router->UpdateSizes( m_savedSizes );

    PCBNEW_SETTINGS* settings = frame()->GetPcbNewSettings();

    if( !settings->m_PnsSettings )
        settings->m_PnsSettings = std::make_unique<ROUTING_SETTINGS>( settings, "tools.pns" );

    m_router->LoadSettings( settings->m_PnsSettings.get() );

    m_gridHelper = new PCB_GRID_HELPER( m_toolMgr, frame()->GetMagneticItemsSettings() );
}


ITEM* TOOL_BASE::pickSingleItem( const VECTOR2I& aWhere, int aNet, int aLayer, bool aIgnorePads,
								 const std::vector<ITEM*> aAvoidItems )
{
    int tl = aLayer > 0 ? aLayer : getView()->GetTopLayer();

    static const int candidateCount = 5;
    ITEM* prioritized[candidateCount];
    SEG::ecoord dist[candidateCount];

    for( int i = 0; i < candidateCount; i++ )
    {
        prioritized[i] = nullptr;
        dist[i] = VECTOR2I::ECOORD_MAX;
    }

    auto haveCandidates =
            [&]()
            {
                for( ITEM* item : prioritized )
                {
                    if( item )
                        return true;
                }

                return false;
            };

    for( bool useClearance : { false, true } )
    {
        ITEM_SET candidates = m_router->QueryHoverItems( aWhere, useClearance );

        for( ITEM* item : candidates.Items() )
        {
            if( !item->IsRoutable() )
                continue;

            if( !IsCopperLayer( item->Layers().Start() ) )
                continue;

            if( !m_iface->IsAnyLayerVisible( item->Layers() ) )
                continue;

            if( alg::contains( aAvoidItems, item ) )
                continue;

            // fixme: this causes flicker with live loop removal...
            //if( item->Parent() && !item->Parent()->ViewIsVisible() )
            //    continue;

            if( item->OfKind( ITEM::SOLID_T ) && aIgnorePads )
            {
                continue;
            }
            else if( aNet <= 0 || item->Net() == aNet )
            {
                if( item->OfKind( ITEM::VIA_T | ITEM::SOLID_T ) )
                {
                    SEG::ecoord d = ( item->Shape()->Centre() - aWhere ).SquaredEuclideanNorm();

                    if( d < dist[2] )
                    {
                        prioritized[2] = item;
                        dist[2] = d;
                    }

                    if( item->Layers().Overlaps( tl ) && d < dist[0] )
                    {
                        prioritized[0] = item;
                        dist[0] = d;
                    }
                }
                else    // ITEM::SEGMENT_T | ITEM::ARC_T
                {
                    LINKED_ITEM* li = static_cast<LINKED_ITEM*>( item );
                    SEG::ecoord  d = std::min( ( li->Anchor( 0 ) - aWhere ).SquaredEuclideanNorm(),
                                               ( li->Anchor( 1 ) - aWhere ).SquaredEuclideanNorm() );

                    if( d < dist[3] )
                    {
                        prioritized[3] = item;
                        dist[3] = d;
                    }

                    if( item->Layers().Overlaps( tl ) && d < dist[1] )
                    {
                        prioritized[1] = item;
                        dist[1] = d;
                    }
                }
            }
            else if( item->OfKind( ITEM::SOLID_T ) && item->IsFreePad() )
            {
                // Allow free pads only when already inside pad
                if( item->Shape()->Collide( aWhere ) )
                {
                    prioritized[0] = item;
                    dist[0] = 0;
                }
            }
            else if ( item->Net() == 0 && m_router->Settings().Mode() == RM_MarkObstacles )
            {
                // Allow unconnected items as last resort in RM_MarkObstacles mode
                if( item->Layers().Overlaps( tl ) )
                    prioritized[4] = item;
            }
        }

        if( haveCandidates() )
            break;
    }

    ITEM* rv = nullptr;

    bool highContrast = ( frame()->GetDisplayOptions().m_ContrastModeDisplay != HIGH_CONTRAST_MODE::NORMAL );

    for( ITEM* item : prioritized )
    {
        if( highContrast && item && !item->Layers().Overlaps( tl ) )
            item = nullptr;

        if( item && ( aLayer < 0 || item->Layers().Overlaps( aLayer ) ) )
        {
            rv = item;
            break;
        }
    }

    if( rv )
    {
        wxLogTrace( wxT( "PNS" ), wxT( "%s, layer : %d, tl: %d" ),
                    rv->KindStr().c_str(),
                    rv->Layers().Start(),
                    tl );
    }

    return rv;
}


void TOOL_BASE::highlightNets( bool aEnabled, std::set<int> aNetcodes )
{
    RENDER_SETTINGS* rs = getView()->GetPainter()->GetSettings();

    if( aNetcodes.size() > 0 && aEnabled )
    {
        // If the user has previously set some of the routed nets to be highlighted,
        // we assume they want to keep them highlighted after routing

        const std::set<int>& currentNetCodes = rs->GetHighlightNetCodes();
        bool                 keep = false;

        for( const int& netcode : aNetcodes )
        {
            if( currentNetCodes.find( netcode ) != currentNetCodes.end() )
            {
                keep = true;
                break;
            }
        }

        if( rs->IsHighlightEnabled() && keep )
            m_startHighlightNetcodes = currentNetCodes;
        else
            m_startHighlightNetcodes.clear();

        rs->SetHighlight( aNetcodes, true );
    }
    else
    {
        rs->SetHighlight( m_startHighlightNetcodes, m_startHighlightNetcodes.size() > 0 );
    }

    // Do not remove this call.  This is required to update the layers when we highlight a net.
    // In this case, highlighting a net dims all other elements, so the colors need to update
    getView()->UpdateAllLayersColor();
}


bool TOOL_BASE::checkSnap( ITEM *aItem )
{
    // Sync PNS engine settings with the general PCB editor options.
    auto& pnss = m_router->Settings();

    // If we're dragging a track segment, don't try to snap to items that are part of the original line.
    if( m_startItem && aItem && m_router->GetState() == ROUTER::DRAG_SEGMENT
        && m_router->GetDragger() )
    {
        DRAGGER*     dragger = dynamic_cast<DRAGGER*>( m_router->GetDragger() );
        LINKED_ITEM* liItem = dynamic_cast<LINKED_ITEM*>( aItem );

        if( dragger && liItem && dragger->GetOriginalLine().ContainsLink( liItem ) )
        {
            return false;
        }
    }

    pnss.SetSnapToPads(
            frame()->GetMagneticItemsSettings()->pads == MAGNETIC_OPTIONS::CAPTURE_CURSOR_IN_TRACK_TOOL ||
            frame()->GetMagneticItemsSettings()->pads == MAGNETIC_OPTIONS::CAPTURE_ALWAYS );

    pnss.SetSnapToTracks(
            frame()->GetMagneticItemsSettings()->tracks == MAGNETIC_OPTIONS::CAPTURE_CURSOR_IN_TRACK_TOOL
            || frame()->GetMagneticItemsSettings()->tracks == MAGNETIC_OPTIONS::CAPTURE_ALWAYS );

    if( aItem )
    {
        if( aItem->OfKind( ITEM::VIA_T | ITEM::SEGMENT_T | ITEM::ARC_T )  )
            return pnss.GetSnapToTracks();
        else if( aItem->OfKind( ITEM::SOLID_T ) )
            return pnss.GetSnapToPads();
    }

    return false;
}


void TOOL_BASE::updateStartItem( const TOOL_EVENT& aEvent, bool aIgnorePads )
{
    int      tl = getView()->GetTopLayer();
    VECTOR2I cp = aEvent.IsPrime() ? aEvent.Position()
                                   : controls()->GetCursorPosition( !aEvent.Modifier( MD_SHIFT ) );
    VECTOR2I p;
    GAL*     gal = m_toolMgr->GetView()->GetGAL();

    controls()->ForceCursorPosition( false );
    m_gridHelper->SetUseGrid( gal->GetGridSnapping() && !aEvent.DisableGridSnapping()  );
    m_gridHelper->SetSnap( !aEvent.Modifier( MD_SHIFT ) );

    if( aEvent.IsMotion() || aEvent.IsClick() )
        p = aEvent.Position();
    else
        p = cp;

    m_startItem = pickSingleItem( aEvent.IsClick() ? cp : p, -1, -1, aIgnorePads );

    if( !m_gridHelper->GetUseGrid() && m_startItem && !m_startItem->Layers().Overlaps( tl ) )
        m_startItem = nullptr;

    m_startSnapPoint = snapToItem( m_startItem, p );
    controls()->ForceCursorPosition( true, m_startSnapPoint );
}


void TOOL_BASE::updateEndItem( const TOOL_EVENT& aEvent )
{
    int  layer;
    GAL* gal = m_toolMgr->GetView()->GetGAL();

    m_gridHelper->SetUseGrid( gal->GetGridSnapping() && !aEvent.DisableGridSnapping()  );
    m_gridHelper->SetSnap( !aEvent.Modifier( MD_SHIFT ) );

    controls()->ForceCursorPosition( false );

    VECTOR2I mousePos = controls()->GetMousePosition();

    if( m_router->GetState() == ROUTER::ROUTE_TRACK && aEvent.IsDrag() )
    {
        // If the user is moving the mouse quickly while routing then clicks will come in as
        // short drags.  In this case we want to use the drag origin rather than the current
        // mouse position.
        mousePos = aEvent.DragOrigin();
    }

    if( m_router->Settings().Mode() != RM_MarkObstacles &&
        ( m_router->GetCurrentNets().empty() || m_router->GetCurrentNets().front() < 0 ) )
    {
        m_endSnapPoint = snapToItem( nullptr, mousePos );
        controls()->ForceCursorPosition( true, m_endSnapPoint );
        m_endItem = nullptr;

        return;
    }

    if( m_router->IsPlacingVia() )
        layer = -1;
    else
        layer = m_router->GetCurrentLayer();

    ITEM* endItem = nullptr;

    std::vector<int> nets = m_router->GetCurrentNets();

    for( int net : nets )
    {
        endItem = pickSingleItem( mousePos, net, layer, false, { m_startItem } );

        if( endItem )
            break;
    }

    if( m_gridHelper->GetSnap() && checkSnap( endItem ) )
    {
        m_endItem = endItem;
        m_endSnapPoint = snapToItem( endItem, mousePos );
    }
    else
    {
        m_endItem = nullptr;
        m_endSnapPoint = m_gridHelper->Align( mousePos );
    }

    controls()->ForceCursorPosition( true, m_endSnapPoint );

    if( m_endItem )
    {
        wxLogTrace( wxT( "PNS" ), wxT( "%s, layer : %d" ),
                    m_endItem->KindStr().c_str(),
                    m_endItem->Layers().Start() );
    }
}


ROUTER *TOOL_BASE::Router() const
{
    return m_router;
}


const VECTOR2I TOOL_BASE::snapToItem( ITEM* aItem, const VECTOR2I& aP )
{
    if( !aItem || !m_iface->IsItemVisible( aItem ) )
    {
        return m_gridHelper->Align( aP );
    }

    switch( aItem->Kind() )
    {
    case ITEM::SOLID_T:
        return static_cast<SOLID*>( aItem )->Pos();

    case ITEM::VIA_T:
        return static_cast<VIA*>( aItem )->Pos();

    case ITEM::SEGMENT_T:
    case ITEM::ARC_T:
    {
        LINKED_ITEM* li = static_cast<LINKED_ITEM*>( aItem );
        VECTOR2I     A = li->Anchor( 0 );
        VECTOR2I     B = li->Anchor( 1 );
        SEG::ecoord  w_sq = SEG::Square( li->Width() / 2 );
        SEG::ecoord  distA_sq = ( aP - A ).SquaredEuclideanNorm();
        SEG::ecoord  distB_sq = ( aP - B ).SquaredEuclideanNorm();

        if( distA_sq < w_sq || distB_sq < w_sq )
        {
            return ( distA_sq < distB_sq ) ? A : B;
        }
        else if( aItem->Kind() == ITEM::SEGMENT_T )
        {
            // TODO(snh): Clean this up
            SEGMENT* seg = static_cast<SEGMENT*>( li );
            return m_gridHelper->AlignToSegment( aP, seg->Seg() );
        }
        else if( aItem->Kind() == ITEM::ARC_T )
        {
            ARC* arc = static_cast<ARC*>( li );
            return m_gridHelper->AlignToArc( aP, *static_cast<const SHAPE_ARC*>( arc->Shape() ) );
        }

        break;
    }

    default:
        break;
    }

    return m_gridHelper->Align( aP );
}

}
