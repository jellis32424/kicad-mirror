/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2015-2016 Mario Luzeiro <mrluzeiro@ua.pt>
 * Copyright (C) 1992-2022 KiCad Developers, see AUTHORS.txt for contributors.
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

/**
 * @file  create_layer_items.cpp
 * @brief This file implements the creation of the pcb board.
 *
 * It is based on the function found in the files:
 *  board_items_to_polygon_shape_transform.cpp
 *  board_items_to_polygon_shape_transform.cpp
 */

#include "board_adapter.h"
#include "../3d_rendering/raytracing/shapes2D/filled_circle_2d.h"
#include <board_design_settings.h>
#include <footprint.h>
#include <pad.h>
#include <pcb_text.h>
#include <pcb_textbox.h>
#include <fp_shape.h>
#include <zone.h>
#include <convert_basic_shapes_to_polygon.h>
#include <trigo.h>
#include <vector>
#include <thread>
#include <core/arraydim.h>
#include <algorithm>
#include <atomic>
#include <wx/log.h>

#ifdef PRINT_STATISTICS_3D_VIEWER
#include <profile.h>
#endif


void BOARD_ADAPTER::destroyLayers()
{
    if( !m_layers_poly.empty() )
    {
        for( std::pair<const PCB_LAYER_ID, SHAPE_POLY_SET*>& poly : m_layers_poly )
            delete poly.second;

        m_layers_poly.clear();
    }

    delete m_frontPlatedPadPolys;
    m_frontPlatedPadPolys = nullptr;

    delete m_backPlatedPadPolys;
    m_backPlatedPadPolys = nullptr;

    if( !m_layerHoleIdPolys.empty() )
    {
        for( std::pair<const PCB_LAYER_ID, SHAPE_POLY_SET*>& poly : m_layerHoleIdPolys )
            delete poly.second;

        m_layerHoleIdPolys.clear();
    }

    if( !m_layerHoleOdPolys.empty() )
    {
        for( std::pair<const PCB_LAYER_ID, SHAPE_POLY_SET*>& poly : m_layerHoleOdPolys )
            delete poly.second;

        m_layerHoleOdPolys.clear();
    }

    if( !m_layerMap.empty() )
    {
        for( std::pair<const PCB_LAYER_ID, BVH_CONTAINER_2D*>& poly : m_layerMap )
            delete poly.second;

        m_layerMap.clear();
    }

    delete m_platedPadsFront;
    m_platedPadsFront = nullptr;

    delete m_platedPadsBack;
    m_platedPadsBack = nullptr;

    if( !m_layerHoleMap.empty() )
    {
        for( std::pair<const PCB_LAYER_ID, BVH_CONTAINER_2D*>& poly : m_layerHoleMap )
            delete poly.second;

        m_layerHoleMap.clear();
    }

    m_throughHoleIds.Clear();
    m_throughHoleOds.Clear();
    m_throughHoleAnnularRings.Clear();
    m_throughHoleViaOds.Clear();
    m_throughHoleViaIds.Clear();
    m_nonPlatedThroughHoleOdPolys.RemoveAllContours();
    m_throughHoleOdPolys.RemoveAllContours();

    m_throughHoleViaOdPolys.RemoveAllContours();
    m_throughHoleAnnularRingPolys.RemoveAllContours();
}


void BOARD_ADAPTER::createLayers( REPORTER* aStatusReporter )
{
    destroyLayers();

    // Build Copper layers
    // Based on:
    //    https://github.com/KiCad/kicad-source-mirror/blob/master/3d-viewer/3d_draw.cpp#L692

#ifdef PRINT_STATISTICS_3D_VIEWER
    unsigned stats_startCopperLayersTime = GetRunningMicroSecs();

    unsigned start_Time = stats_startCopperLayersTime;
#endif

    PCB_LAYER_ID cu_seq[MAX_CU_LAYERS];
    LSET         cu_set = LSET::AllCuMask( m_copperLayersCount );

    m_trackCount               = 0;
    m_averageTrackWidth        = 0;
    m_viaCount                 = 0;
    m_averageViaHoleDiameter   = 0;
    m_holeCount                = 0;
    m_averageHoleDiameter      = 0;

    if( !m_board )
        return;

    // Prepare track list, convert in a vector. Calc statistic for the holes
    std::vector<const PCB_TRACK*> trackList;
    trackList.clear();
    trackList.reserve( m_board->Tracks().size() );

    int maxError = m_board->GetDesignSettings().m_MaxError;

    for( PCB_TRACK* track : m_board->Tracks() )
    {
        if( !Is3dLayerEnabled( track->GetLayer() ) ) // Skip non enabled layers
            continue;

        // Note: a PCB_TRACK holds normal segment tracks and also vias circles (that have also
        // drill values)
        trackList.push_back( track );

        if( track->Type() == PCB_VIA_T )
        {
            const PCB_VIA *via = static_cast< const PCB_VIA*>( track );
            m_viaCount++;
            m_averageViaHoleDiameter += via->GetDrillValue() * m_biuTo3Dunits;
        }
        else
        {
            m_trackCount++;
        }

        m_averageTrackWidth += track->GetWidth() * m_biuTo3Dunits;
    }

    if( m_trackCount )
        m_averageTrackWidth /= (float)m_trackCount;

    if( m_viaCount )
        m_averageViaHoleDiameter /= (float)m_viaCount;

    // Prepare copper layers index and containers
    std::vector<PCB_LAYER_ID> layer_ids;
    layer_ids.clear();
    layer_ids.reserve( m_copperLayersCount );

    for( unsigned i = 0; i < arrayDim( cu_seq ); ++i )
        cu_seq[i] = ToLAYER_ID( B_Cu - i );

    for( LSEQ cu = cu_set.Seq( cu_seq, arrayDim( cu_seq ) ); cu; ++cu )
    {
        const PCB_LAYER_ID layer = *cu;

        if( !Is3dLayerEnabled( layer ) ) // Skip non enabled layers
            continue;

        layer_ids.push_back( layer );

        BVH_CONTAINER_2D *layerContainer = new BVH_CONTAINER_2D;
        m_layerMap[layer] = layerContainer;

        if( m_Cfg->m_Render.opengl_copper_thickness
                && m_Cfg->m_Render.engine == RENDER_ENGINE::OPENGL )
        {
            SHAPE_POLY_SET* layerPoly    = new SHAPE_POLY_SET;
            m_layers_poly[layer] = layerPoly;
        }
    }

    if( m_Cfg->m_Render.renderPlatedPadsAsPlated && m_Cfg->m_Render.realistic )
    {
        m_frontPlatedPadPolys = new SHAPE_POLY_SET;
        m_backPlatedPadPolys = new SHAPE_POLY_SET;

        m_platedPadsFront = new BVH_CONTAINER_2D;
        m_platedPadsBack = new BVH_CONTAINER_2D;

    }

    if( aStatusReporter )
        aStatusReporter->Report( _( "Create tracks and vias" ) );

    // Create tracks as objects and add it to container
    for( PCB_LAYER_ID layer : layer_ids )
    {
        wxASSERT( m_layerMap.find( layer ) != m_layerMap.end() );

        BVH_CONTAINER_2D *layerContainer = m_layerMap[layer];

        for( const PCB_TRACK* track : trackList )
        {
            // NOTE: Vias can be on multiple layers
            if( !track->IsOnLayer( layer ) )
                continue;

            // Skip vias annulus when not connected on this layer (if removing is enabled)
            const PCB_VIA *via = dyn_cast< const PCB_VIA*>( track );

            if( via && IsCopperLayer( layer ) && !via->FlashLayer( layer )  )
                continue;

            // Add object item to layer container
            createTrack( track, layerContainer );
        }
    }

    // Create VIAS and THTs objects and add it to holes containers
    for( PCB_LAYER_ID layer : layer_ids )
    {
        // ADD TRACKS
        unsigned int nTracks = trackList.size();

        for( unsigned int trackIdx = 0; trackIdx < nTracks; ++trackIdx )
        {
            const PCB_TRACK *track = trackList[trackIdx];

            if( !track->IsOnLayer( layer ) )
                continue;

            // ADD VIAS and THT
            if( track->Type() == PCB_VIA_T )
            {
                const PCB_VIA* via               = static_cast<const PCB_VIA*>( track );
                const VIATYPE  viatype           = via->GetViaType();
                const float    holediameter      = via->GetDrillValue() * BiuTo3dUnits();

                // holes and layer copper extend half info cylinder wall to hide transition
                const float    thickness         = GetHolePlatingThickness() * BiuTo3dUnits() / 2.0f;
                const float    hole_inner_radius = holediameter / 2.0f;
                const float    ring_radius       = via->GetWidth() * BiuTo3dUnits() / 2.0f;

                const SFVEC2F via_center( via->GetStart().x * m_biuTo3Dunits,
                                          -via->GetStart().y * m_biuTo3Dunits );

                if( viatype != VIATYPE::THROUGH )
                {
                    // Add hole objects
                    BVH_CONTAINER_2D *layerHoleContainer = nullptr;

                    // Check if the layer is already created
                    if( m_layerHoleMap.find( layer ) == m_layerHoleMap.end() )
                    {
                        // not found, create a new container
                        layerHoleContainer = new BVH_CONTAINER_2D;
                        m_layerHoleMap[layer] = layerHoleContainer;
                    }
                    else
                    {
                        // found
                        layerHoleContainer = m_layerHoleMap[layer];
                    }

                    // Add a hole for this layer
                    layerHoleContainer->Add( new FILLED_CIRCLE_2D( via_center,
                                                                   hole_inner_radius + thickness,
                                                                   *track ) );
                }
                else if( layer == layer_ids[0] ) // it only adds once the THT holes
                {
                    // Add through hole object
                    m_throughHoleOds.Add( new FILLED_CIRCLE_2D( via_center,
                                                                hole_inner_radius + thickness,
                                                                *track ) );
                    m_throughHoleViaOds.Add( new FILLED_CIRCLE_2D( via_center,
                                                                   hole_inner_radius + thickness,
                                                                   *track ) );

                    if( m_Cfg->m_Render.clip_silk_on_via_annulus
                            && m_Cfg->m_Render.realistic
                            && ring_radius > 0.0 )
                    {
                        m_throughHoleAnnularRings.Add( new FILLED_CIRCLE_2D( via_center,
                                                                             ring_radius,
                                                                             *track ) );
                    }

                    if( hole_inner_radius > 0.0 )
                    {
                        m_throughHoleIds.Add( new FILLED_CIRCLE_2D( via_center,
                                                                    hole_inner_radius,
                                                                    *track ) );
                    }
                }
            }
        }
    }

    // Create VIAS and THTs objects and add it to holes containers
    for( PCB_LAYER_ID layer : layer_ids )
    {
        // ADD TRACKS
        const unsigned int nTracks = trackList.size();

        for( unsigned int trackIdx = 0; trackIdx < nTracks; ++trackIdx )
        {
            const PCB_TRACK *track = trackList[trackIdx];

            if( !track->IsOnLayer( layer ) )
                continue;

            // ADD VIAS and THT
            if( track->Type() == PCB_VIA_T )
            {
                const PCB_VIA* via = static_cast<const PCB_VIA*>( track );
                const VIATYPE  viatype = via->GetViaType();

                if( viatype != VIATYPE::THROUGH )
                {
                    // Add PCB_VIA hole contours

                    // Add outer holes of VIAs
                    SHAPE_POLY_SET *layerOuterHolesPoly = nullptr;
                    SHAPE_POLY_SET *layerInnerHolesPoly = nullptr;

                    // Check if the layer is already created
                    if( m_layerHoleOdPolys.find( layer ) == m_layerHoleOdPolys.end() )
                    {
                        // not found, create a new container
                        layerOuterHolesPoly = new SHAPE_POLY_SET;
                        m_layerHoleOdPolys[layer] = layerOuterHolesPoly;

                        wxASSERT( m_layerHoleIdPolys.find( layer ) == m_layerHoleIdPolys.end() );

                        layerInnerHolesPoly = new SHAPE_POLY_SET;
                        m_layerHoleIdPolys[layer] = layerInnerHolesPoly;
                    }
                    else
                    {
                        // found
                        layerOuterHolesPoly = m_layerHoleOdPolys[layer];

                        wxASSERT( m_layerHoleIdPolys.find( layer ) != m_layerHoleIdPolys.end() );

                        layerInnerHolesPoly = m_layerHoleIdPolys[layer];
                    }

                    const int holediameter = via->GetDrillValue();
                    const int hole_outer_radius = (holediameter / 2) + GetHolePlatingThickness();

                    TransformCircleToPolygon( *layerOuterHolesPoly, via->GetStart(),
                                              hole_outer_radius, maxError, ERROR_INSIDE );

                    TransformCircleToPolygon( *layerInnerHolesPoly, via->GetStart(),
                                              holediameter / 2, maxError, ERROR_INSIDE );
                }
                else if( layer == layer_ids[0] ) // it only adds once the THT holes
                {
                    const int holediameter = via->GetDrillValue();
                    const int hole_outer_radius = (holediameter / 2) + GetHolePlatingThickness();
                    const int hole_outer_ring_radius = via->GetWidth() / 2.0f;

                    // Add through hole contours
                    TransformCircleToPolygon( m_throughHoleOdPolys, via->GetStart(),
                                              hole_outer_radius, maxError, ERROR_INSIDE );

                    // Add same thing for vias only
                    TransformCircleToPolygon( m_throughHoleViaOdPolys, via->GetStart(),
                                              hole_outer_radius, maxError, ERROR_INSIDE );

                    if( m_Cfg->m_Render.clip_silk_on_via_annulus && m_Cfg->m_Render.realistic )
                    {
                        TransformCircleToPolygon( m_throughHoleAnnularRingPolys, via->GetStart(),
                                                  hole_outer_ring_radius, maxError, ERROR_INSIDE );
                    }
                }
            }
        }
    }

    // Creates vertical outline contours of the tracks and add it to the poly of the layer
    if( m_Cfg->m_Render.opengl_copper_thickness && m_Cfg->m_Render.engine == RENDER_ENGINE::OPENGL )
    {
        for( PCB_LAYER_ID layer : layer_ids )
        {
            wxASSERT( m_layers_poly.find( layer ) != m_layers_poly.end() );

            SHAPE_POLY_SET *layerPoly = m_layers_poly[layer];

            // ADD TRACKS
            unsigned int nTracks = trackList.size();

            for( unsigned int trackIdx = 0; trackIdx < nTracks; ++trackIdx )
            {
                const PCB_TRACK *track = trackList[trackIdx];

                if( !track->IsOnLayer( layer ) )
                    continue;

                // Skip vias annulus when not connected on this layer (if removing is enabled)
                const PCB_VIA *via = dyn_cast<const PCB_VIA*>( track );

                if( via && !via->FlashLayer( layer ) && IsCopperLayer( layer ) )
                    continue;

                // Add the track/via contour
                track->TransformShapeToPolygon( *layerPoly, layer, 0, maxError, ERROR_INSIDE );
            }
        }
    }

    // Add holes of footprints
    for( FOOTPRINT* footprint : m_board->Footprints() )
    {
        for( PAD* pad : footprint->Pads() )
        {
            const VECTOR2I padHole = pad->GetDrillSize();

            if( !padHole.x )    // Not drilled pad like SMD pad
                continue;

            // The hole in the body is inflated by copper thickness, if not plated, no copper
            const int inflate = ( pad->GetAttribute () != PAD_ATTRIB::NPTH ) ?
                                GetHolePlatingThickness() / 2 : 0;

            m_holeCount++;
            m_averageHoleDiameter += ( ( pad->GetDrillSize().x +
                                             pad->GetDrillSize().y ) / 2.0f ) * m_biuTo3Dunits;

            m_throughHoleOds.Add( createPadWithDrill( pad, inflate ) );

            if( m_Cfg->m_Render.clip_silk_on_via_annulus && m_Cfg->m_Render.realistic )
                m_throughHoleAnnularRings.Add( createPadWithDrill( pad, inflate ) );

            m_throughHoleIds.Add( createPadWithDrill( pad, 0 ) );
        }
    }

    if( m_holeCount )
        m_averageHoleDiameter /= (float)m_holeCount;

    // Add contours of the pad holes (pads can be Circle or Segment holes)
    for( FOOTPRINT* footprint : m_board->Footprints() )
    {
        for( PAD* pad : footprint->Pads() )
        {
            const VECTOR2I padHole = pad->GetDrillSize();

            if( !padHole.x ) // Not drilled pad like SMD pad
                continue;

            // The hole in the body is inflated by copper thickness.
            const int inflate = GetHolePlatingThickness();

            if( pad->GetAttribute () != PAD_ATTRIB::NPTH )
            {
                if( m_Cfg->m_Render.clip_silk_on_via_annulus && m_Cfg->m_Render.realistic )
                {
                    pad->TransformHoleToPolygon( m_throughHoleAnnularRingPolys, inflate, maxError,
                                                 ERROR_INSIDE );
                }

                pad->TransformHoleToPolygon( m_throughHoleOdPolys, inflate, maxError,
                                             ERROR_INSIDE );
            }
            else
            {
                // If not plated, no copper.
                if( m_Cfg->m_Render.clip_silk_on_via_annulus && m_Cfg->m_Render.realistic )
                {
                    pad->TransformHoleToPolygon( m_throughHoleAnnularRingPolys, 0, maxError,
                                                 ERROR_INSIDE );
                }

                pad->TransformHoleToPolygon( m_nonPlatedThroughHoleOdPolys, 0, maxError,
                                             ERROR_INSIDE );
            }
        }
    }

    const bool renderPlatedPadsAsPlated = m_Cfg->m_Render.renderPlatedPadsAsPlated
                                                && m_Cfg->m_Render.realistic;

    // Add footprints PADs objects to containers
    for( PCB_LAYER_ID layer : layer_ids )
    {
        wxASSERT( m_layerMap.find( layer ) != m_layerMap.end() );

        BVH_CONTAINER_2D *layerContainer = m_layerMap[layer];

        // ADD PADS
        for( FOOTPRINT* footprint : m_board->Footprints() )
        {
            addPads( footprint, layerContainer, layer, renderPlatedPadsAsPlated, false );

            // Micro-wave footprints may have items on copper layers
            addFootprintShapes( footprint, layerContainer, layer );
        }
    }

    if( renderPlatedPadsAsPlated )
    {
        // ADD PLATED PADS
        for( FOOTPRINT* footprint : m_board->Footprints() )
        {
            addPads( footprint, m_platedPadsFront, F_Cu, false, true );
            addPads( footprint, m_platedPadsBack, B_Cu, false, true );
        }

        m_platedPadsFront->BuildBVH();
        m_platedPadsBack->BuildBVH();
    }

    // Add footprints PADs poly contours (vertical outlines)
    if( m_Cfg->m_Render.opengl_copper_thickness && m_Cfg->m_Render.engine == RENDER_ENGINE::OPENGL )
    {
        for( PCB_LAYER_ID layer : layer_ids )
        {
            wxASSERT( m_layers_poly.find( layer ) != m_layers_poly.end() );

            SHAPE_POLY_SET *layerPoly = m_layers_poly[layer];

            // Add pads to polygon list
            for( FOOTPRINT* footprint : m_board->Footprints() )
            {
                // Note: NPTH pads are not drawn on copper layers when the pad has same shape as
                // its hole
                footprint->TransformPadsToPolySet( *layerPoly, layer, 0, maxError, ERROR_INSIDE,
                                                   true, renderPlatedPadsAsPlated, false );

                transformFPShapesToPolySet( footprint, layer, *layerPoly );
            }
        }

        if( renderPlatedPadsAsPlated )
        {
            // ADD PLATED PADS contours
            for( FOOTPRINT* footprint : m_board->Footprints() )
            {
                footprint->TransformPadsToPolySet( *m_frontPlatedPadPolys, F_Cu, 0, maxError,
                                                   ERROR_INSIDE, true, false, true );

                footprint->TransformPadsToPolySet( *m_backPlatedPadPolys, B_Cu, 0, maxError,
                                                   ERROR_INSIDE, true, false, true );
            }
        }
    }

    // Add graphic item on copper layers to object containers
    for( PCB_LAYER_ID layer : layer_ids )
    {
        wxASSERT( m_layerMap.find( layer ) != m_layerMap.end() );

        BVH_CONTAINER_2D *layerContainer = m_layerMap[layer];

        // Add graphic items on copper layers (texts and other graphics)
        for( BOARD_ITEM* item : m_board->Drawings() )
        {
            if( !item->IsOnLayer( layer ) )
                continue;

            switch( item->Type() )
            {
            case PCB_SHAPE_T:
                addShape( static_cast<PCB_SHAPE*>( item ), layerContainer, item );
                break;

            case PCB_TEXT_T:
                addText( static_cast<PCB_TEXT*>( item ), layerContainer, item );
                break;

            case PCB_TEXTBOX_T:
                addShape( static_cast<PCB_TEXTBOX*>( item ), layerContainer, item );
                break;

            case PCB_DIM_ALIGNED_T:
            case PCB_DIM_CENTER_T:
            case PCB_DIM_RADIAL_T:
            case PCB_DIM_ORTHOGONAL_T:
            case PCB_DIM_LEADER_T:
                addShape( static_cast<PCB_DIMENSION_BASE*>( item ), layerContainer, item );
                break;

            default:
                wxLogTrace( m_logTrace, wxT( "createLayers: item type: %d not implemented" ),
                            item->Type() );
                break;
            }
        }
    }

    // Add graphic item on copper layers to poly contours (vertical outlines)
    if( m_Cfg->m_Render.opengl_copper_thickness && m_Cfg->m_Render.engine == RENDER_ENGINE::OPENGL )
    {
        for( PCB_LAYER_ID layer : layer_ids )
        {
            wxASSERT( m_layers_poly.find( layer ) != m_layers_poly.end() );

            SHAPE_POLY_SET *layerPoly = m_layers_poly[layer];

            // Add graphic items on copper layers (texts and other )
            for( BOARD_ITEM* item : m_board->Drawings() )
            {
                if( !item->IsOnLayer( layer ) )
                    continue;

                switch( item->Type() )
                {
                case PCB_SHAPE_T:
                    item->TransformShapeToPolygon( *layerPoly, layer, 0, maxError, ERROR_INSIDE );
                    break;

                case PCB_TEXT_T:
                {
                    PCB_TEXT* text = static_cast<PCB_TEXT*>( item );

                    text->TransformTextToPolySet( *layerPoly, 0, maxError, ERROR_INSIDE );
                    break;
                }

                case PCB_TEXTBOX_T:
                {
                    PCB_TEXTBOX* textbox = static_cast<PCB_TEXTBOX*>( item );

                    textbox->TransformTextToPolySet( *layerPoly, 0, maxError, ERROR_INSIDE );
                    break;
                }

                default:
                    wxLogTrace( m_logTrace, wxT( "createLayers: item type: %d not implemented" ),
                                item->Type() );
                    break;
                }
            }
        }
    }

    if( m_Cfg->m_Render.show_zones )
    {
        if( aStatusReporter )
            aStatusReporter->Report( _( "Create zones" ) );

        std::vector<std::pair<ZONE*, PCB_LAYER_ID>> zones;
        std::unordered_map<PCB_LAYER_ID, std::unique_ptr<std::mutex>> layer_lock;

        for( ZONE* zone : m_board->Zones() )
        {
            for( PCB_LAYER_ID layer : zone->GetLayerSet().Seq() )
            {
                zones.emplace_back( std::make_pair( zone, layer ) );
                layer_lock.emplace( layer, std::make_unique<std::mutex>() );
            }
        }

        // Add zones objects
        std::atomic<size_t> nextZone( 0 );
        std::atomic<size_t> threadsFinished( 0 );

        size_t parallelThreadCount = std::min<size_t>( zones.size(),
                std::max<size_t>( std::thread::hardware_concurrency(), 2 ) );

        for( size_t ii = 0; ii < parallelThreadCount; ++ii )
        {
            std::thread t = std::thread( [&]()
            {
                for( size_t areaId = nextZone.fetch_add( 1 );
                            areaId < zones.size();
                            areaId = nextZone.fetch_add( 1 ) )
                {
                    ZONE* zone = zones[areaId].first;

                    if( zone == nullptr )
                        break;

                    PCB_LAYER_ID layer = zones[areaId].second;

                    auto layerContainer = m_layerMap.find( layer );
                    auto layerPolyContainer = m_layers_poly.find( layer );

                    if( layerContainer != m_layerMap.end() )
                        addSolidAreasShapes( zone, layerContainer->second, layer );

                    if( m_Cfg->m_Render.opengl_copper_thickness
                          && m_Cfg->m_Render.engine == RENDER_ENGINE::OPENGL
                          && layerPolyContainer != m_layers_poly.end() )
                    {
                        auto mut_it = layer_lock.find( layer );

                        std::lock_guard< std::mutex > lock( *( mut_it->second ) );
                        zone->TransformSolidAreasShapesToPolygon( layer, *layerPolyContainer->second );
                    }
                }

                threadsFinished++;
            } );

            t.detach();
        }

        while( threadsFinished < parallelThreadCount )
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );

    }

    // Simplify layer polygons

    if( aStatusReporter )
        aStatusReporter->Report( _( "Simplifying copper layers polygons" ) );

    if( m_Cfg->m_Render.opengl_copper_thickness && m_Cfg->m_Render.engine == RENDER_ENGINE::OPENGL )
    {
        if( renderPlatedPadsAsPlated )
        {
            if( m_frontPlatedPadPolys && ( m_layers_poly.find( F_Cu ) != m_layers_poly.end() ) )
            {
                if( aStatusReporter )
                    aStatusReporter->Report( _( "Simplifying polygons on F_Cu" ) );

                SHAPE_POLY_SET *layerPoly_F_Cu = m_layers_poly[F_Cu];
                layerPoly_F_Cu->BooleanSubtract( *m_frontPlatedPadPolys, SHAPE_POLY_SET::PM_FAST );

                 m_frontPlatedPadPolys->Simplify( SHAPE_POLY_SET::PM_FAST );
            }

            if( m_backPlatedPadPolys && ( m_layers_poly.find( B_Cu ) != m_layers_poly.end() ) )
            {
                if( aStatusReporter )
                    aStatusReporter->Report( _( "Simplifying polygons on B_Cu" ) );

                SHAPE_POLY_SET *layerPoly_B_Cu = m_layers_poly[B_Cu];
                layerPoly_B_Cu->BooleanSubtract( *m_backPlatedPadPolys, SHAPE_POLY_SET::PM_FAST );

                m_backPlatedPadPolys->Simplify( SHAPE_POLY_SET::PM_FAST );
            }
        }

        std::vector<PCB_LAYER_ID> &selected_layer_id = layer_ids;
        std::vector<PCB_LAYER_ID> layer_id_without_F_and_B;

        if( renderPlatedPadsAsPlated )
        {
            layer_id_without_F_and_B.clear();
            layer_id_without_F_and_B.reserve( layer_ids.size() );

            for( PCB_LAYER_ID layer: layer_ids )
            {
                if( layer != F_Cu && layer != B_Cu )
                    layer_id_without_F_and_B.push_back( layer );
            }

            selected_layer_id = layer_id_without_F_and_B;
        }

        if( selected_layer_id.size() > 0 )
        {
            if( aStatusReporter )
            {
                aStatusReporter->Report( wxString::Format( _( "Simplifying %d copper layers" ),
                                                           (int) selected_layer_id.size() ) );
            }

            std::atomic<size_t> nextItem( 0 );
            std::atomic<size_t> threadsFinished( 0 );

            size_t parallelThreadCount = std::min<size_t>(
                    std::max<size_t>( std::thread::hardware_concurrency(), 2 ),
                    selected_layer_id.size() );

            for( size_t ii = 0; ii < parallelThreadCount; ++ii )
            {
                std::thread t = std::thread(
                        [&nextItem, &threadsFinished, &selected_layer_id, this]()
                        {
                            for( size_t i = nextItem.fetch_add( 1 );
                                        i < selected_layer_id.size();
                                        i = nextItem.fetch_add( 1 ) )
                            {
                                auto layerPoly = m_layers_poly.find( selected_layer_id[i] );

                                if( layerPoly != m_layers_poly.end() )
                                    // This will make a union of all added contours
                                    layerPoly->second->Simplify( SHAPE_POLY_SET::PM_FAST );
                            }

                            threadsFinished++;
                        } );

                t.detach();
            }

            while( threadsFinished < parallelThreadCount )
                std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        }
    }

    // Simplify holes polygon contours
    if( aStatusReporter )
        aStatusReporter->Report( _( "Simplify holes contours" ) );

    for( PCB_LAYER_ID layer : layer_ids )
    {
        if( m_layerHoleOdPolys.find( layer ) != m_layerHoleOdPolys.end() )
        {
            // found
            SHAPE_POLY_SET *polyLayer = m_layerHoleOdPolys[layer];
            polyLayer->Simplify( SHAPE_POLY_SET::PM_FAST );

            wxASSERT( m_layerHoleIdPolys.find( layer ) != m_layerHoleIdPolys.end() );

            polyLayer = m_layerHoleIdPolys[layer];
            polyLayer->Simplify( SHAPE_POLY_SET::PM_FAST );
        }
    }

    // End Build Copper layers

    // This will make a union of all added contours
    m_throughHoleOdPolys.Simplify( SHAPE_POLY_SET::PM_FAST );
    m_nonPlatedThroughHoleOdPolys.Simplify( SHAPE_POLY_SET::PM_FAST );
    m_throughHoleViaOdPolys.Simplify( SHAPE_POLY_SET::PM_FAST );
    m_throughHoleAnnularRingPolys.Simplify( SHAPE_POLY_SET::PM_FAST );

    // Build Tech layers
    // Based on:
    //    https://github.com/KiCad/kicad-source-mirror/blob/master/3d-viewer/3d_draw.cpp#L1059
    if( aStatusReporter )
        aStatusReporter->Report( _( "Build Tech layers" ) );

    // draw graphic items, on technical layers

    // Vertical walls (layer thickness) around shapes is really time consumming
    // They are built on request
    bool buildVerticalWallsForTechLayers = m_Cfg->m_Render.opengl_copper_thickness
                                              && m_Cfg->m_Render.engine == RENDER_ENGINE::OPENGL;

    static const PCB_LAYER_ID techLayerList[] = {
            B_Adhes,
            F_Adhes,
            B_Paste,
            F_Paste,
            B_SilkS,
            F_SilkS,
            B_Mask,
            F_Mask,

            // Aux Layers
            Dwgs_User,
            Cmts_User,
            Eco1_User,
            Eco2_User,
            Edge_Cuts,
            Margin
        };

    // User layers are not drawn here, only technical layers
    for( LSEQ seq = LSET::AllNonCuMask().Seq( techLayerList, arrayDim( techLayerList ) );
         seq;
         ++seq )
    {
        const PCB_LAYER_ID layer = *seq;

        if( !Is3dLayerEnabled( layer ) )
            continue;

        if( aStatusReporter )
            aStatusReporter->Report( wxString::Format( _( "Build Tech layer %d" ), (int) layer ) );

        BVH_CONTAINER_2D *layerContainer = new BVH_CONTAINER_2D;
        m_layerMap[layer] = layerContainer;

        SHAPE_POLY_SET *layerPoly = new SHAPE_POLY_SET;
        m_layers_poly[layer] = layerPoly;

        // Add drawing objects
        for( BOARD_ITEM* item : m_board->Drawings() )
        {
            if( !item->IsOnLayer( layer ) )
                continue;

            switch( item->Type() )
            {
            case PCB_SHAPE_T:
                addShape( static_cast<PCB_SHAPE*>( item ), layerContainer, item );
                break;

            case PCB_TEXT_T:
                addText( static_cast<PCB_TEXT*>( item ), layerContainer, item );
                break;

            case PCB_TEXTBOX_T:
                addShape( static_cast<PCB_TEXTBOX*>( item ), layerContainer, item );
                break;

            case PCB_DIM_ALIGNED_T:
            case PCB_DIM_CENTER_T:
            case PCB_DIM_RADIAL_T:
            case PCB_DIM_ORTHOGONAL_T:
            case PCB_DIM_LEADER_T:
                addShape( static_cast<PCB_DIMENSION_BASE*>( item ), layerContainer, item );
                break;

            default:
                break;
            }
        }

        // Add drawing contours (vertical walls)
        if( buildVerticalWallsForTechLayers )
        {
            for( BOARD_ITEM* item : m_board->Drawings() )
            {
                if( !item->IsOnLayer( layer ) )
                    continue;

                switch( item->Type() )
                {
                case PCB_SHAPE_T:
                    item->TransformShapeToPolygon( *layerPoly, layer, 0, maxError, ERROR_INSIDE );
                    break;

                case PCB_TEXT_T:
                {
                    PCB_TEXT* text = static_cast<PCB_TEXT*>( item );

                    text->TransformTextToPolySet( *layerPoly, 0, maxError, ERROR_INSIDE );
                    break;
                }

                case PCB_TEXTBOX_T:
                {
                    PCB_TEXTBOX* textbox = static_cast<PCB_TEXTBOX*>( item );

                    textbox->TransformTextToPolySet( *layerPoly, 0, maxError, ERROR_INSIDE );
                    break;
                }

                default:
                    break;
                }
            }
        }

        // Add footprints tech layers - objects
        for( FOOTPRINT* footprint : m_board->Footprints() )
        {
            if( layer == F_SilkS || layer == B_SilkS )
            {
                int linewidth = m_board->GetDesignSettings().m_LineThickness[ LAYER_CLASS_SILK ];

                for( PAD* pad : footprint->Pads() )
                {
                    if( !pad->IsOnLayer( layer ) )
                        continue;

                    buildPadOutlineAsSegments( pad, layerContainer, linewidth );
                }
            }
            else
            {
                addPads( footprint, layerContainer, layer, false, false );
            }

            addFootprintShapes( footprint, layerContainer, layer );
        }


        // Add footprints tech layers - contours (vertical walls)
        if( buildVerticalWallsForTechLayers )
        {
            for( FOOTPRINT* footprint : m_board->Footprints() )
            {
                if( layer == F_SilkS || layer == B_SilkS )
                {
                    int linewidth = m_board->GetDesignSettings().m_LineThickness[ LAYER_CLASS_SILK ];

                    for( PAD* pad : footprint->Pads() )
                    {
                        if( !pad->IsOnLayer( layer ) )
                            continue;

                        buildPadOutlineAsPolygon( pad, *layerPoly, linewidth );
                    }
                }
                else
                {
                    footprint->TransformPadsToPolySet( *layerPoly, layer, 0, maxError, ERROR_INSIDE );
                }

                // On tech layers, use a poor circle approximation, only for texts (stroke font)
                footprint->TransformFPTextToPolySet( *layerPoly, layer, 0, maxError, ERROR_INSIDE );

                // Add the remaining things with dynamic seg count for circles
                transformFPShapesToPolySet( footprint, layer, *layerPoly );
            }
        }

        // Draw non copper zones
        if( m_Cfg->m_Render.show_zones )
        {
            for( ZONE* zone : m_board->Zones() )
            {
                if( zone->IsOnLayer( layer ) )
                    addSolidAreasShapes( zone, layerContainer, layer );
            }

            if( buildVerticalWallsForTechLayers )
            {
                for( ZONE* zone : m_board->Zones() )
                {

                    if( zone->IsOnLayer( layer ) )
                        zone->TransformSolidAreasShapesToPolygon( layer, *layerPoly );
                }
            }
        }

        // This will make a union of all added contours
        layerPoly->Simplify( SHAPE_POLY_SET::PM_FAST );
    }
    // End Build Tech layers

    // Build BVH (Bounding volume hierarchy) for holes and vias

    if( aStatusReporter )
        aStatusReporter->Report( _( "Build BVH for holes and vias" ) );

    m_throughHoleIds.BuildBVH();
    m_throughHoleOds.BuildBVH();
    m_throughHoleAnnularRings.BuildBVH();

    if( !m_layerHoleMap.empty() )
    {
        for( std::pair<const PCB_LAYER_ID, BVH_CONTAINER_2D*>& hole : m_layerHoleMap )
            hole.second->BuildBVH();
    }

    // We only need the Solder mask to initialize the BVH
    // because..?
    if( m_layerMap[B_Mask] )
        m_layerMap[B_Mask]->BuildBVH();

    if( m_layerMap[F_Mask] )
        m_layerMap[F_Mask]->BuildBVH();
}
