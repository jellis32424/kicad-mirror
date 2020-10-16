/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2004 Jean-Pierre Charras, jp.charras at wanadoo.fr
 * Copyright (C) 2004-2020 KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <sch_draw_panel.h>
#include <bitmaps.h>
#include <schematic.h>
#include <sch_bus_entry.h>
#include <sch_edit_frame.h>
#include <sch_line.h>
#include <sch_text.h>
#include <settings/color_settings.h>
#include <netclass.h>
#include <trigo.h>
#include "sch_painter.h"


SCH_BUS_ENTRY_BASE::SCH_BUS_ENTRY_BASE( KICAD_T aType, const wxPoint& pos, bool aFlipY ) :
    SCH_ITEM( NULL, aType )
{
    m_pos    = pos;
    m_size.x = Mils2iu( DEFAULT_SCH_ENTRY_SIZE );
    m_size.y = Mils2iu( DEFAULT_SCH_ENTRY_SIZE );

    if( aFlipY )
        m_size.y *= -1;

    m_isDanglingStart = m_isDanglingEnd = true;
}


SCH_BUS_WIRE_ENTRY::SCH_BUS_WIRE_ENTRY( const wxPoint& pos, bool aFlipY ) :
    SCH_BUS_ENTRY_BASE( SCH_BUS_WIRE_ENTRY_T, pos, aFlipY )
{
    m_Layer  = LAYER_WIRE;
    m_connected_bus_item = nullptr;
}


SCH_BUS_BUS_ENTRY::SCH_BUS_BUS_ENTRY( const wxPoint& pos, bool aFlipY ) :
    SCH_BUS_ENTRY_BASE( SCH_BUS_BUS_ENTRY_T, pos, aFlipY )
{
    m_Layer = LAYER_BUS;
    m_connected_bus_items[0] = nullptr;
    m_connected_bus_items[1] = nullptr;
}


EDA_ITEM* SCH_BUS_WIRE_ENTRY::Clone() const
{
    return new SCH_BUS_WIRE_ENTRY( *this );
}


EDA_ITEM* SCH_BUS_BUS_ENTRY::Clone() const
{
    return new SCH_BUS_BUS_ENTRY( *this );
}


bool SCH_BUS_ENTRY_BASE::doIsConnected( const wxPoint& aPosition ) const
{
    return ( m_pos == aPosition || GetEnd() == aPosition );
}


wxPoint SCH_BUS_ENTRY_BASE::GetEnd() const
{
    return wxPoint( m_pos.x + m_size.x, m_pos.y + m_size.y );
}


void SCH_BUS_ENTRY_BASE::SwapData( SCH_ITEM* aItem )
{
    SCH_BUS_ENTRY_BASE* item = dynamic_cast<SCH_BUS_ENTRY_BASE*>( aItem );
    wxCHECK_RET( item, wxT( "Cannot swap bus entry data with invalid item." ) );

    std::swap( m_pos, item->m_pos );
    std::swap( m_size, item->m_size );
    std::swap( m_stroke, item->m_stroke );
}


void SCH_BUS_ENTRY_BASE::ViewGetLayers( int aLayers[], int& aCount ) const
{
    aCount     = 2;
    aLayers[0] = Type() == SCH_BUS_BUS_ENTRY_T ? LAYER_BUS : LAYER_WIRE;
    aLayers[1] = LAYER_SELECTION_SHADOWS;
}


const EDA_RECT SCH_BUS_ENTRY_BASE::GetBoundingBox() const
{
    EDA_RECT box;

    box.SetOrigin( m_pos );
    box.SetEnd( GetEnd() );

    box.Normalize();
    box.Inflate( ( GetPenWidth() / 2 ) + 1 );

    return box;
}


COLOR4D SCH_BUS_ENTRY_BASE::GetStrokeColor() const
{
    NETCLASSPTR netclass = NetClass();

    if( netclass && netclass->GetSchematicColor() != COLOR4D::UNSPECIFIED )
        return netclass->GetSchematicColor();

    return m_stroke.GetColor();
}


PLOT_DASH_TYPE SCH_BUS_ENTRY_BASE::GetStrokeStyle() const
{
    NETCLASSPTR netclass = NetClass();

    if( netclass )
        return (PLOT_DASH_TYPE) netclass->GetLineStyle();

    return m_stroke.GetPlotStyle();
}


int SCH_BUS_WIRE_ENTRY::GetPenWidth() const
{
    NETCLASSPTR netclass = NetClass();

    if( netclass )
        return netclass->GetWireWidth();

    if( m_stroke.GetWidth() == 0 && Schematic() )
        return std::max( Schematic()->Settings().m_DefaultWireThickness, 1 );

    return ( m_stroke.GetWidth() == 0 ) ? 1 : m_stroke.GetWidth();
}


int SCH_BUS_BUS_ENTRY::GetPenWidth() const
{
    NETCLASSPTR netclass = NetClass();

    if( netclass )
        return netclass->GetBusWidth();

    if( m_stroke.GetWidth() == 0 && Schematic() )
        return std::max( Schematic()->Settings().m_DefaultBusThickness, 1 );

    return ( m_stroke.GetWidth() == 0 ) ? 1 : m_stroke.GetWidth();
}


void SCH_BUS_WIRE_ENTRY::GetEndPoints( std::vector< DANGLING_END_ITEM >& aItemList )
{
    DANGLING_END_ITEM item( WIRE_ENTRY_END, this, m_pos );
    aItemList.push_back( item );

    DANGLING_END_ITEM item1( WIRE_ENTRY_END, this, GetEnd() );
    aItemList.push_back( item1 );
}


void SCH_BUS_BUS_ENTRY::GetEndPoints( std::vector< DANGLING_END_ITEM >& aItemList )
{
    DANGLING_END_ITEM item( BUS_ENTRY_END, this, m_pos );
    aItemList.push_back( item );

    DANGLING_END_ITEM item1( BUS_ENTRY_END, this, GetEnd() );
    aItemList.push_back( item1 );
}


void SCH_BUS_ENTRY_BASE::Print( RENDER_SETTINGS* aSettings, const wxPoint& aOffset )
{
    wxDC*   DC = aSettings->GetPrintDC();
    COLOR4D color = ( GetStrokeColor()  == COLOR4D::UNSPECIFIED ) ?
            aSettings->GetLayerColor( m_Layer ) : GetStrokeColor();
    int     penWidth = ( GetPenWidth() == 0 ) ? aSettings->GetDefaultPenWidth() : GetPenWidth();

    GRLine( nullptr, DC, m_pos.x + aOffset.x, m_pos.y + aOffset.y, GetEnd().x + aOffset.x,
            GetEnd().y + aOffset.y, penWidth, color,
            GetwxPenStyle( (PLOT_DASH_TYPE) GetStrokeStyle() ) );
}


void SCH_BUS_ENTRY_BASE::MirrorX( int aXaxis_position )
{
    MIRROR( m_pos.y, aXaxis_position );
    m_size.y = -m_size.y;
}


void SCH_BUS_ENTRY_BASE::MirrorY( int aYaxis_position )
{
    MIRROR( m_pos.x, aYaxis_position );
    m_size.x = -m_size.x;
}


void SCH_BUS_ENTRY_BASE::Rotate( wxPoint aPosition )
{
    RotatePoint( &m_pos, aPosition, 900 );
    RotatePoint( &m_size.x, &m_size.y, 900 );
}


bool SCH_BUS_WIRE_ENTRY::UpdateDanglingState( std::vector<DANGLING_END_ITEM>& aItemList,
                                              const SCH_SHEET_PATH* aPath )
{
    bool previousStateStart = m_isDanglingStart;
    bool previousStateEnd = m_isDanglingEnd;

    m_isDanglingStart = m_isDanglingEnd = true;

    // Wires and buses are stored in the list as a pair, start and end. This
    // variable holds the start position from one iteration so it can be used
    // when the end position is found.
    wxPoint seg_start;

    // Store the connection type and state for the start (0) and end (1)
    bool has_wire[2] = { false };
    bool has_bus[2] = { false };

    for( DANGLING_END_ITEM& each_item : aItemList )
    {
        if( each_item.GetItem() == this )
            continue;

        switch( each_item.GetType() )
        {
        case WIRE_START_END:
        case WIRE_END_END:
            if( m_pos == each_item.GetPosition() )
                has_wire[0] = true;
            else if( GetEnd() == each_item.GetPosition() )
                has_wire[1] = true;

            break;

        case BUS_START_END:
            seg_start = each_item.GetPosition();
            break;

        case BUS_END_END:
            if( IsPointOnSegment( seg_start, each_item.GetPosition(), m_pos ) )
                has_bus[0] = true;
            else if( IsPointOnSegment( seg_start, each_item.GetPosition(), GetEnd() ) )
                has_bus[1] = true;

            break;

        default:
            break;
        }
    }

    // A bus-wire entry is connected at both ends if it has a bus and a wire on its
    // ends.  Otherwise, we connect only one end (in the case of a wire-wire or bus-bus)
    if( ( has_wire[0] && has_bus[1] ) || ( has_wire[1] && has_bus[0] ) )
        m_isDanglingEnd = m_isDanglingStart = false;
    else if( has_wire[0] || has_bus[0] )
        m_isDanglingStart = false;
    else if( has_wire[1] || has_bus[1] )
        m_isDanglingEnd = false;

    return (previousStateStart != m_isDanglingStart) || (previousStateEnd != m_isDanglingEnd);
}


bool SCH_BUS_BUS_ENTRY::UpdateDanglingState( std::vector<DANGLING_END_ITEM>& aItemList,
                                             const SCH_SHEET_PATH* aPath )
{
    bool previousStateStart = m_isDanglingStart;
    bool previousStateEnd = m_isDanglingEnd;

    m_isDanglingStart = m_isDanglingEnd = true;

    // Wires and buses are stored in the list as a pair, start and end. This
    // variable holds the start position from one iteration so it can be used
    // when the end position is found.
    wxPoint seg_start;

    for( DANGLING_END_ITEM& each_item : aItemList )
    {
        if( each_item.GetItem() == this )
            continue;

        switch( each_item.GetType() )
        {
        case BUS_START_END:
            seg_start = each_item.GetPosition();
            break;
        case BUS_END_END:
            if( IsPointOnSegment( seg_start, each_item.GetPosition(), m_pos ) )
                m_isDanglingStart = false;
            if( IsPointOnSegment( seg_start, each_item.GetPosition(), GetEnd() ) )
                m_isDanglingEnd = false;
            break;
        default:
            break;
        }
    }

    return (previousStateStart != m_isDanglingStart) || (previousStateEnd != m_isDanglingEnd);
}


bool SCH_BUS_ENTRY_BASE::IsDangling() const
{
    return m_isDanglingStart || m_isDanglingEnd;
}


std::vector<wxPoint> SCH_BUS_ENTRY_BASE::GetConnectionPoints() const
{
    return { m_pos, GetEnd() };
}


wxString SCH_BUS_WIRE_ENTRY::GetSelectMenuText( EDA_UNITS aUnits ) const
{
    return wxString( _( "Bus to Wire Entry" ) );
}


wxString SCH_BUS_BUS_ENTRY::GetSelectMenuText( EDA_UNITS aUnits ) const
{
    return wxString( _( "Bus to Bus Entry" ) );
}


BITMAP_DEF SCH_BUS_WIRE_ENTRY::GetMenuImage() const
{
    return add_line2bus_xpm;
}


BITMAP_DEF SCH_BUS_BUS_ENTRY::GetMenuImage() const
{
    return add_bus2bus_xpm;
}


bool SCH_BUS_ENTRY_BASE::HitTest( const wxPoint& aPosition, int aAccuracy ) const
{
    // Insure minimum accuracy
    if( aAccuracy == 0 )
        aAccuracy = ( GetPenWidth() / 2 ) + 4;

    return TestSegmentHit( aPosition, m_pos, GetEnd(), aAccuracy );
}


bool SCH_BUS_ENTRY_BASE::HitTest( const EDA_RECT& aRect, bool aContained, int aAccuracy ) const
{
    EDA_RECT rect = aRect;

    rect.Inflate( aAccuracy );

    if( aContained )
        return rect.Contains( GetBoundingBox() );

    return rect.Intersects( GetBoundingBox() );
}


void SCH_BUS_ENTRY_BASE::Plot( PLOTTER* aPlotter )
{
    auto* settings = static_cast<KIGFX::SCH_RENDER_SETTINGS*>( aPlotter->RenderSettings() );

    COLOR4D color = ( GetStrokeColor() == COLOR4D::UNSPECIFIED ) ?
            settings->GetLayerColor( m_Layer ) : GetStrokeColor();
    int     penWidth = ( GetPenWidth() == 0 ) ? settings->GetDefaultPenWidth() : GetPenWidth();

    penWidth = std::max( penWidth, settings->GetMinPenWidth() );

    aPlotter->SetCurrentLineWidth( penWidth );
    aPlotter->SetColor( color );
    aPlotter->SetDash( GetStrokeStyle() );
    aPlotter->MoveTo( m_pos );
    aPlotter->FinishTo( GetEnd() );
}


void SCH_BUS_ENTRY_BASE::GetMsgPanelInfo( EDA_DRAW_FRAME* aFrame, MSG_PANEL_ITEMS& aList )
{
    wxString msg;

    switch( GetLayer() )
    {
    default:
    case LAYER_WIRE: msg = _( "Wire" ); break;
    case LAYER_BUS:  msg = _( "Bus" );  break;
    }

    aList.push_back( MSG_PANEL_ITEM( _( "Bus Entry Type" ), msg, DARKCYAN ) );

    SCH_EDIT_FRAME* frame = dynamic_cast<SCH_EDIT_FRAME*>( aFrame );

    if( !frame )
        return;

    if( SCH_CONNECTION* conn = Connection() )
        conn->AppendInfoToMsgPanel( aList );
}


bool SCH_BUS_ENTRY_BASE::operator <( const SCH_ITEM& aItem ) const
{
    if( Type() != aItem.Type() )
        return Type() < aItem.Type();

    auto component = static_cast<const SCH_BUS_ENTRY_BASE*>( &aItem );

    if( GetLayer() != component->GetLayer() )
        return GetLayer() < component->GetLayer();

    if( GetPosition().x != component->GetPosition().x )
        return GetPosition().x < component->GetPosition().x;

    if( GetPosition().y != component->GetPosition().y )
        return GetPosition().y < component->GetPosition().y;

    if( GetEnd().x != component->GetEnd().x )
        return GetEnd().x < component->GetEnd().x;

    return GetEnd().y < component->GetEnd().y;
}


bool SCH_BUS_WIRE_ENTRY::ConnectionPropagatesTo( const EDA_ITEM* aItem ) const
{
    // Don't generate connections between bus entries and buses, since there is
    // a connectivity change at that point (e.g. A[7..0] to A7)
    if( ( aItem->Type() == SCH_LINE_T ) &&
        ( static_cast<const SCH_LINE*>( aItem )->GetLayer() == LAYER_BUS ) )
    {
        return false;
    }

    // Don't generate connections between bus entries and bus labels that happen
    // to land at the same point on the bus wire as this bus entry
    if( ( aItem->Type() == SCH_LABEL_T ) &&
        SCH_CONNECTION::IsBusLabel( static_cast<const SCH_LABEL*>( aItem )->GetText() ) )
    {
        return false;
    }

    // Don't generate connections between two bus-wire entries
    if( aItem->Type() == SCH_BUS_WIRE_ENTRY_T )
        return false;

    return true;
}
