/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2004-2022 KiCad Developers.
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

#include <common.h>
#include <math_for_graphics.h>
#include <board_design_settings.h>
#include <footprint.h>
#include <pcb_shape.h>
#include <pad.h>
#include <pcb_track.h>
#include <thread_pool.h>
#include <zone.h>

#include <geometry/seg.h>
#include <geometry/shape_poly_set.h>
#include <geometry/shape_segment.h>

#include <drc/drc_engine.h>
#include <drc/drc_rtree.h>
#include <drc/drc_item.h>
#include <drc/drc_rule.h>
#include <drc/drc_test_provider_clearance_base.h>
#include <pcb_dimension.h>

#include <future>

/*
    Copper clearance test. Checks all copper items (pads, vias, tracks, drawings, zones) for their
    electrical clearance.

    Errors generated:
    - DRCE_CLEARANCE
    - DRCE_HOLE_CLEARANCE
    - DRCE_TRACKS_CROSSING
    - DRCE_ZONES_INTERSECT
    - DRCE_SHORTING_ITEMS
*/

class DRC_TEST_PROVIDER_COPPER_CLEARANCE : public DRC_TEST_PROVIDER_CLEARANCE_BASE
{
public:
    DRC_TEST_PROVIDER_COPPER_CLEARANCE () :
            DRC_TEST_PROVIDER_CLEARANCE_BASE(),
            m_drcEpsilon( 0 )
    {
    }

    virtual ~DRC_TEST_PROVIDER_COPPER_CLEARANCE()
    {
    }

    virtual bool Run() override;

    virtual const wxString GetName() const override
    {
        return wxT( "clearance" );
    };

    virtual const wxString GetDescription() const override
    {
        return wxT( "Tests copper item clearance" );
    }

private:
    /**
     * Checks for track/via/hole <-> clearance
     * @param track Track to text
     * @param trackShape Primitive track shape
     * @param layer Which layer to test (in case of vias this can be multiple
     * @param other item against which to test the track item
     * @return false if there is a clearance violation reported, true if there is none
     */
    bool testTrackAgainstItem( PCB_TRACK* track, SHAPE* trackShape, PCB_LAYER_ID layer,
                               BOARD_ITEM* other );

    void testTrackClearances();

    bool testPadAgainstItem( PAD* pad, SHAPE* padShape, PCB_LAYER_ID layer, BOARD_ITEM* other );

    void testPadClearances();

    void testGraphicClearances();

    void testZonesToZones();

    void testItemAgainstZone( BOARD_ITEM* aItem, ZONE* aZone, PCB_LAYER_ID aLayer );

    void testKnockoutTextAgainstZone( BOARD_ITEM* aText, NETINFO_ITEM** aInheritedNet, ZONE* aZone );

    typedef struct checked
    {
        checked()
            : layers(), has_error( false ) {}

        checked( PCB_LAYER_ID aLayer )
            : layers( aLayer ), has_error( false ) {}

        LSET layers;
        bool has_error;
    } layers_checked;

private:
    int m_drcEpsilon;
};


bool DRC_TEST_PROVIDER_COPPER_CLEARANCE::Run()
{
    m_board = m_drcEngine->GetBoard();

    if( m_board->m_DRCMaxClearance <= 0 )
    {
        reportAux( wxT( "No Clearance constraints found. Tests not run." ) );
        return true;   // continue with other tests
    }

    m_drcEpsilon = m_board->GetDesignSettings().GetDRCEpsilon();

    if( !m_drcEngine->IsErrorLimitExceeded( DRCE_CLEARANCE ) )
    {
        if( !reportPhase( _( "Checking track & via clearances..." ) ) )
            return false;   // DRC cancelled

        testTrackClearances();
    }
    else if( !m_drcEngine->IsErrorLimitExceeded( DRCE_HOLE_CLEARANCE ) )
    {
        if( !reportPhase( _( "Checking hole clearances..." ) ) )
            return false;   // DRC cancelled

        testTrackClearances();
    }

    if( !m_drcEngine->IsErrorLimitExceeded( DRCE_CLEARANCE ) )
    {
        if( !reportPhase( _( "Checking pad clearances..." ) ) )
            return false;   // DRC cancelled

        testPadClearances();
    }
    else if( !m_drcEngine->IsErrorLimitExceeded( DRCE_SHORTING_ITEMS )
            || !m_drcEngine->IsErrorLimitExceeded( DRCE_HOLE_CLEARANCE ) )
    {
        if( !reportPhase( _( "Checking pads..." ) ) )
            return false;   // DRC cancelled

        testPadClearances();
    }

    if( !m_drcEngine->IsErrorLimitExceeded( DRCE_CLEARANCE ) )
    {
        if( !reportPhase( _( "Checking copper graphic clearances..." ) ) )
            return false;   // DRC cancelled

        testGraphicClearances();
    }

    if( !m_drcEngine->IsErrorLimitExceeded( DRCE_CLEARANCE ) )
    {
        if( !reportPhase( _( "Checking copper zone clearances..." ) ) )
            return false;   // DRC cancelled

        testZonesToZones();
    }
    else if( !m_drcEngine->IsErrorLimitExceeded( DRCE_ZONES_INTERSECT ) )
    {
        if( !reportPhase( _( "Checking zones..." ) ) )
            return false;   // DRC cancelled

        testZonesToZones();
    }

    reportRuleStatistics();

    return !m_drcEngine->IsCancelled();
}


bool DRC_TEST_PROVIDER_COPPER_CLEARANCE::testTrackAgainstItem( PCB_TRACK* track, SHAPE* trackShape,
                                                               PCB_LAYER_ID layer,
                                                               BOARD_ITEM* other )
{
    bool           testClearance = !m_drcEngine->IsErrorLimitExceeded( DRCE_CLEARANCE );
    bool           testHoles = !m_drcEngine->IsErrorLimitExceeded( DRCE_HOLE_CLEARANCE );
    DRC_CONSTRAINT constraint;
    int            clearance = -1;
    int            actual;
    VECTOR2I       pos;
    bool           has_error = false;

    std::shared_ptr<SHAPE> otherShape = other->GetEffectiveShape( layer );

    if( other->Type() == PCB_PAD_T )
    {
        PAD* pad = static_cast<PAD*>( other );

        if( pad->GetAttribute() == PAD_ATTRIB::NPTH && !pad->FlashLayer( layer ) )
            testClearance = false;
    }

    if( testClearance )
    {
        constraint = m_drcEngine->EvalRules( CLEARANCE_CONSTRAINT, track, other, layer );
        clearance = constraint.GetValue().Min();
    }

    if( constraint.GetSeverity() != RPT_SEVERITY_IGNORE && clearance > 0 )
    {
        // Special processing for track:track intersections
        if( track->Type() == PCB_TRACE_T && other->Type() == PCB_TRACE_T )
        {
            SEG trackSeg( track->GetStart(), track->GetEnd() );
            SEG otherSeg( track->GetStart(), track->GetEnd() );

            if( OPT_VECTOR2I intersection = trackSeg.Intersect( otherSeg ) )
            {
                std::shared_ptr<DRC_ITEM> drcItem = DRC_ITEM::Create( DRCE_TRACKS_CROSSING );
                drcItem->SetItems( track, other );
                drcItem->SetViolatingRule( constraint.GetParentRule() );

                reportViolation( drcItem, *intersection, layer );

                return false;
            }
        }

        if( trackShape->Collide( otherShape.get(), clearance - m_drcEpsilon, &actual, &pos ) )
        {
            if( m_drcEngine->IsNetTieExclusion( track->GetNetCode(), layer, pos, other ) )
            {
                // Collision occurred as track was entering a pad marked as a net-tie.  We
                // allow these.
            }
            else
            {
                std::shared_ptr<DRC_ITEM> drce = DRC_ITEM::Create( DRCE_CLEARANCE );
                wxString msg = formatMsg( _( "(%s clearance %s; actual %s)" ),
                                          constraint.GetName(),
                                          clearance,
                                          actual );

                drce->SetErrorMessage( drce->GetErrorText() + wxS( " " ) + msg );
                drce->SetItems( track, other );
                drce->SetViolatingRule( constraint.GetParentRule() );

                reportViolation( drce, pos, layer );
                has_error = true;

                if( !m_drcEngine->GetReportAllTrackErrors() )
                    return false;
            }
        }
    }

    if( testHoles && ( track->HasHole() || other->HasHole() ) )
    {
        std::array<BOARD_ITEM*, 2> a{ track, other };
        std::array<BOARD_ITEM*, 2> b{ other, track };
        std::array<SHAPE*, 2>      a_shape{ trackShape, otherShape.get() };

        for( size_t ii = 0; ii < 2; ++ii )
        {
            std::shared_ptr<SHAPE_SEGMENT> holeShape;

            // We only test a track item here against an item with a hole.
            // If either case is not valid, simply move on
            if( !( dynamic_cast<PCB_TRACK*>( a[ii] ) ) || !b[ii]->HasHole() )
            {
                continue;
            }
            if( b[ii]->Type() == PCB_VIA_T )
            {
                if( b[ii]->GetLayerSet().Contains( layer ) )
                    holeShape = b[ii]->GetEffectiveHoleShape();
            }
            else
            {
                holeShape = b[ii]->GetEffectiveHoleShape();
            }

            constraint = m_drcEngine->EvalRules( HOLE_CLEARANCE_CONSTRAINT, b[ii], a[ii], layer );
            clearance = constraint.GetValue().Min();

            // Test for hole to item clearance even if clearance is 0, because the item cannot be
            // inside (or intersect) the hole.
            if( constraint.GetSeverity() != RPT_SEVERITY_IGNORE )
            {
                if( a_shape[ii]->Collide( holeShape.get(), std::max( 0, clearance - m_drcEpsilon ),
                                          &actual, &pos ) )
                {
                    std::shared_ptr<DRC_ITEM> drce = DRC_ITEM::Create( DRCE_HOLE_CLEARANCE );
                    wxString msg = formatMsg( clearance ? _( "(%s clearance %s; actual %s)" )
                                                        : _( "(%s clearance %s; actual < 0)" ),
                                              constraint.GetName(),
                                              clearance,
                                              actual );

                    drce->SetErrorMessage( drce->GetErrorText() + wxS( " " ) + msg );
                    drce->SetItems( a[ii], b[ii] );
                    drce->SetViolatingRule( constraint.GetParentRule() );

                    reportViolation( drce, pos, layer );
                    return false;
                }
            }
        }
    }

    return !has_error;
}


void DRC_TEST_PROVIDER_COPPER_CLEARANCE::testItemAgainstZone( BOARD_ITEM* aItem, ZONE* aZone,
                                                              PCB_LAYER_ID aLayer )
{
    if( !aZone->GetLayerSet().test( aLayer ) )
        return;

    if( aZone->GetNetCode() && aItem->IsConnected() )
    {
        if( aZone->GetNetCode() == static_cast<BOARD_CONNECTED_ITEM*>( aItem )->GetNetCode() )
            return;
    }

    BOX2I itemBBox = aItem->GetBoundingBox();
    BOX2I worstCaseBBox = itemBBox;

    worstCaseBBox.Inflate( m_board->m_DRCMaxClearance );

    if( !worstCaseBBox.Intersects( aZone->GetBoundingBox() ) )
        return;

    bool testClearance = !m_drcEngine->IsErrorLimitExceeded( DRCE_CLEARANCE );
    bool testHoles = !m_drcEngine->IsErrorLimitExceeded( DRCE_HOLE_CLEARANCE );

    if( !testClearance && !testHoles )
        return;

    DRC_RTREE*  zoneTree = m_board->m_CopperZoneRTreeCache[ aZone ].get();

    if( !zoneTree )
        return;

    DRC_CONSTRAINT constraint;
    int            clearance = -1;
    int            actual;
    VECTOR2I       pos;

    if( aItem->Type() == PCB_PAD_T )
    {
        PAD* pad = static_cast<PAD*>( aItem );
        bool flashedPad = pad->FlashLayer( aLayer );
        bool platedHole = pad->HasHole() && pad->GetAttribute() == PAD_ATTRIB::PTH;

        if( !flashedPad && !platedHole )
            testClearance = false;
    }

    if( testClearance )
    {
        constraint = m_drcEngine->EvalRules( CLEARANCE_CONSTRAINT, aItem, aZone, aLayer );
        clearance = constraint.GetValue().Min();
    }

    if( constraint.GetSeverity() != RPT_SEVERITY_IGNORE && clearance > 0 )
    {
        std::shared_ptr<SHAPE> itemShape = aItem->GetEffectiveShape( aLayer, FLASHING::DEFAULT );

        if( zoneTree->QueryColliding( itemBBox, itemShape.get(), aLayer,
                                      std::max( 0, clearance - m_drcEpsilon ), &actual, &pos ) )
        {
            std::shared_ptr<DRC_ITEM> drce = DRC_ITEM::Create( DRCE_CLEARANCE );
            wxString msg = formatMsg( _( "(%s clearance %s; actual %s)" ),
                                      constraint.GetName(),
                                      clearance,
                                      actual );

            drce->SetErrorMessage( drce->GetErrorText() + wxS( " " ) + msg );
            drce->SetItems( aItem, aZone );
            drce->SetViolatingRule( constraint.GetParentRule() );

            reportViolation( drce, pos, aLayer );
        }
    }

    if( testHoles && aItem->HasHole() )
    {
        std::shared_ptr<SHAPE_SEGMENT> holeShape;

        if( aItem->Type() == PCB_VIA_T )
        {
            if( aItem->GetLayerSet().Contains( aLayer ) )
                holeShape = aItem->GetEffectiveHoleShape();
        }
        else
        {
            holeShape = aItem->GetEffectiveHoleShape();
        }

        if( holeShape )
        {
            constraint = m_drcEngine->EvalRules( HOLE_CLEARANCE_CONSTRAINT, aItem, aZone, aLayer );
            clearance = constraint.GetValue().Min();

            if( constraint.GetSeverity() != RPT_SEVERITY_IGNORE && clearance > 0 )
            {
                if( zoneTree->QueryColliding( itemBBox, holeShape.get(), aLayer,
                                              std::max( 0, clearance - m_drcEpsilon ),
                                              &actual, &pos ) )
                {
                    std::shared_ptr<DRC_ITEM> drce = DRC_ITEM::Create( DRCE_HOLE_CLEARANCE );
                    wxString msg = formatMsg( _( "(%s clearance %s; actual %s)" ),
                                              constraint.GetName(),
                                              clearance,
                                              actual );

                    drce->SetErrorMessage( drce->GetErrorText() + wxS( " " ) + msg );
                    drce->SetItems( aItem, aZone );
                    drce->SetViolatingRule( constraint.GetParentRule() );

                    reportViolation( drce, pos, aLayer );
                }
            }
        }
    }
}


/*
 * We have to special-case knockout text as it's most often knocked-out of a zone, so it's
 * presumed to collide with one.  However, if it collides with more than one, and they have
 * different nets, then we have a short.
 */
void DRC_TEST_PROVIDER_COPPER_CLEARANCE::testKnockoutTextAgainstZone( BOARD_ITEM* aText,
                                                                      NETINFO_ITEM** aInheritedNet,
                                                                      ZONE* aZone )
{
    bool testClearance = !m_drcEngine->IsErrorLimitExceeded( DRCE_CLEARANCE );
    bool testShorts = !m_drcEngine->IsErrorLimitExceeded( DRCE_SHORTING_ITEMS );

    if( !testClearance && !testShorts )
        return;

    PCB_LAYER_ID layer = aText->GetLayer();

    if( !aZone->GetLayerSet().test( layer ) )
        return;

    BOX2I itemBBox = aText->GetBoundingBox();
    BOX2I worstCaseBBox = itemBBox;

    worstCaseBBox.Inflate( m_board->m_DRCMaxClearance );

    if( !worstCaseBBox.Intersects( aZone->GetBoundingBox() ) )
        return;

    DRC_RTREE*  zoneTree = m_board->m_CopperZoneRTreeCache[ aZone ].get();

    if( !zoneTree )
        return;

    std::shared_ptr<SHAPE> itemShape = aText->GetEffectiveShape( layer, FLASHING::DEFAULT );

    if( *aInheritedNet == nullptr )
    {
        if( zoneTree->QueryColliding( itemBBox, itemShape.get(), layer ) )
            *aInheritedNet = aZone->GetNet();
    }

    if( *aInheritedNet == aZone->GetNet() )
        return;

    DRC_CONSTRAINT constraint = m_drcEngine->EvalRules( CLEARANCE_CONSTRAINT, aText, aZone, layer );
    int            clearance = constraint.GetValue().Min();
    int            actual;
    VECTOR2I       pos;

    if( constraint.GetSeverity() != RPT_SEVERITY_IGNORE && clearance >= 0 )
    {
        if( zoneTree->QueryColliding( itemBBox, itemShape.get(), layer,
                                      std::max( 0, clearance - m_drcEpsilon ), &actual, &pos ) )
        {
            std::shared_ptr<DRC_ITEM> drce;
            wxString                  msg;

            if( testShorts && actual == 0 && *aInheritedNet )
            {
                drce = DRC_ITEM::Create( DRCE_SHORTING_ITEMS );
                msg.Printf( _( "(nets %s and %s)" ),
                              ( *aInheritedNet )->GetNetname(),
                              aZone->GetNetname() );
            }
            else
            {
                drce = DRC_ITEM::Create( DRCE_CLEARANCE );
                msg = formatMsg( _( "(%s clearance %s; actual %s)" ),
                                 constraint.GetName(),
                                 clearance,
                                 actual );
            }

            drce->SetErrorMessage( drce->GetErrorText() + wxS( " " ) + msg );
            drce->SetItems( aText, aZone );
            drce->SetViolatingRule( constraint.GetParentRule() );

            reportViolation( drce, pos, layer );
        }
    }
}


void DRC_TEST_PROVIDER_COPPER_CLEARANCE::testTrackClearances()
{
    // This is the number of tests between 2 calls to the progress bar
    const int progressDelta = 100;
    int       ii = 0;

    reportAux( wxT( "Testing %d tracks & vias..." ), m_board->Tracks().size() );

    std::map<BOARD_ITEM*, int>                            freePadsUsageMap;
    std::unordered_map<PTR_PTR_CACHE_KEY, layers_checked> checkedPairs;

    LSET boardCopperLayers = LSET::AllCuMask( m_board->GetCopperLayerCount() );

    for( PCB_TRACK* track : m_board->Tracks() )
    {
        if( !reportProgress( ii++, m_board->Tracks().size(), progressDelta ) )
            break;

        for( PCB_LAYER_ID layer : LSET( track->GetLayerSet() & boardCopperLayers ).Seq() )
        {
            std::shared_ptr<SHAPE> trackShape = track->GetEffectiveShape( layer );

            m_board->m_CopperItemRTreeCache->QueryColliding( track, layer, layer,
                    // Filter:
                    [&]( BOARD_ITEM* other ) -> bool
                    {
                        auto otherCItem = dynamic_cast<BOARD_CONNECTED_ITEM*>( other );

                        if( otherCItem && otherCItem->GetNetCode() == track->GetNetCode() )
                            return false;

                        BOARD_ITEM* a = track;
                        BOARD_ITEM* b = other;

                        // store canonical order so we don't collide in both directions
                        // (a:b and b:a)
                        if( static_cast<void*>( a ) > static_cast<void*>( b ) )
                            std::swap( a, b );

                        auto it = checkedPairs.find( { a, b } );

                        if( it != checkedPairs.end() && ( it->second.layers.test( layer )
                                || ( it->second.has_error && !m_drcEngine->GetReportAllTrackErrors() ) ) )
                        {
                            return false;
                        }
                        else
                        {
                            checkedPairs[ { a, b } ].layers.set( layer );
                            return true;
                        }
                    },
                    // Visitor:
                    [&]( BOARD_ITEM* other ) -> bool
                    {
                        if( m_drcEngine->IsCancelled() )
                            return false;

                        if( other->Type() == PCB_PAD_T && static_cast<PAD*>( other )->IsFreePad() )
                        {
                            if( other->GetEffectiveShape( layer )->Collide( trackShape.get() ) )
                            {
                                auto it = freePadsUsageMap.find( other );

                                if( it == freePadsUsageMap.end() )
                                {
                                    freePadsUsageMap[ other ] = track->GetNetCode();
                                    return true;    // Continue colliding tests
                                }
                                else if( it->second == track->GetNetCode() )
                                {
                                    return true;    // Continue colliding tests
                                }
                            }
                        }

                        BOARD_ITEM* a = track;
                        BOARD_ITEM* b = other;

                        // store canonical order so we don't collide in both directions
                        // (a:b and b:a)
                        if( static_cast<void*>( a ) > static_cast<void*>( b ) )
                            std::swap( a, b );

                        auto it = checkedPairs.find( { a, b } );

                        // If we get an error, mark the pair as having a clearance error already
                        // Only continue if we are reporting all track errors
                        if( !testTrackAgainstItem( track, trackShape.get(), layer, other ) )
                        {
                            if( it != checkedPairs.end() )
                                it->second.has_error = true;

                            if( !m_drcEngine->GetReportAllTrackErrors() )
                                return false;   // We're done with this track
                        }

                        return !m_drcEngine->IsCancelled();
                    },
                    m_board->m_DRCMaxClearance );

            for( ZONE* zone : m_board->m_DRCCopperZones )
            {
                testItemAgainstZone( track, zone, layer );

                if( m_drcEngine->IsCancelled() )
                    break;
            }
        }
    }
}


bool DRC_TEST_PROVIDER_COPPER_CLEARANCE::testPadAgainstItem( PAD* pad, SHAPE* padShape,
                                                             PCB_LAYER_ID aLayer,
                                                             BOARD_ITEM* other )
{
    bool testClearance = !m_drcEngine->IsErrorLimitExceeded( DRCE_CLEARANCE );
    bool testShorting = !m_drcEngine->IsErrorLimitExceeded( DRCE_SHORTING_ITEMS );
    bool testHoles = !m_drcEngine->IsErrorLimitExceeded( DRCE_HOLE_CLEARANCE );

    // Disable some tests for net-tie objects in a footprint
    if( other->GetParent() == pad->GetParent() )
    {
        FOOTPRINT*              fp = static_cast<FOOTPRINT*>( pad->GetParent() );
        std::map<wxString, int> padToNetTieGroupMap = fp->MapPadNumbersToNetTieGroups();
        int                     padGroupIdx = padToNetTieGroupMap[ pad->GetNumber() ];

        if( other->Type() == PCB_PAD_T )
        {
            PAD* otherPad = static_cast<PAD*>( other );

            if( padGroupIdx >= 0 && padGroupIdx == padToNetTieGroupMap[ otherPad->GetNumber() ] )
                testClearance = false;

            if( pad->SameLogicalPadAs( otherPad ) )
                testHoles = false;
        }

        if( other->Type() == PCB_FP_SHAPE_T && padGroupIdx >= 0 )
            testClearance = false;
    }

    PAD*     otherPad = nullptr;
    PCB_VIA* otherVia = nullptr;

    if( other->Type() == PCB_PAD_T )
        otherPad = static_cast<PAD*>( other );

    if( other->Type() == PCB_VIA_T )
        otherVia = static_cast<PCB_VIA*>( other );

    if( !IsCopperLayer( aLayer ) )
        testClearance = false;

    // A NPTH has no cylinder, but it may still have pads on some layers
    if( pad->GetAttribute() == PAD_ATTRIB::NPTH && !pad->FlashLayer( aLayer ) )
        testClearance = false;

    if( otherPad && otherPad->GetAttribute() == PAD_ATTRIB::NPTH && !otherPad->FlashLayer( aLayer ) )
        testClearance = false;

    // Track clearances are tested in testTrackClearances()
    if( dynamic_cast<PCB_TRACK*>( other) )
        testClearance = false;

    int padNet = pad->GetNetCode();
    int otherPadNet = otherPad ? otherPad->GetNetCode() : 0;
    int otherViaNet = otherVia ? otherVia->GetNetCode() : 0;

    // Pads and vias of the same (defined) net get a waiver on clearance and hole tests
    if( ( otherPadNet && otherPadNet == padNet ) || ( otherViaNet && otherViaNet == padNet ) )
    {
        testClearance = false;
        testHoles = false;
    }

    if( !( pad->GetDrillSize().x > 0 )
            && !( otherPad && otherPad->GetDrillSize().x > 0 )
            && !( otherVia && otherVia->GetDrill() > 0 ) )
    {
        testHoles = false;
    }

    if( !testClearance && !testShorting && !testHoles )
        return false;

    std::shared_ptr<SHAPE> otherShape = other->GetEffectiveShape( aLayer );
    DRC_CONSTRAINT         constraint;
    int                    clearance;
    int                    actual;
    VECTOR2I               pos;

    if( otherPad && pad->SameLogicalPadAs( otherPad ) )
    {
        // If pads are equivalent (ie: from the same footprint with the same pad number)...
        // ... and have nets...
        // then they must be the same net
        if( pad->GetNetCode() && otherPad->GetNetCode()
                && pad->GetNetCode() != otherPad->GetNetCode()
                && testShorting )
        {
            std::shared_ptr<DRC_ITEM> drce = DRC_ITEM::Create( DRCE_SHORTING_ITEMS );
            wxString msg;

            msg.Printf( _( "(nets %s and %s)" ),
                          pad->GetNetname(),
                          otherPad->GetNetname() );

            drce->SetErrorMessage( drce->GetErrorText() + wxS( " " ) + msg );
            drce->SetItems( pad, otherPad );

            reportViolation( drce, otherPad->GetPosition(), aLayer );
        }

        return !m_drcEngine->IsCancelled();
    }

    if( testClearance )
    {
        constraint = m_drcEngine->EvalRules( CLEARANCE_CONSTRAINT, pad, other, aLayer );
        clearance = constraint.GetValue().Min();

        if( constraint.GetSeverity() != RPT_SEVERITY_IGNORE && clearance > 0 )
        {
            if( padShape->Collide( otherShape.get(), std::max( 0, clearance - m_drcEpsilon ),
                                   &actual, &pos ) )
            {
                if( m_drcEngine->IsNetTieExclusion( pad->GetNetCode(), aLayer, pos, other ) )
                {
                    // Pads connected to pads of a net-tie footprint are allowed to collide
                    // with the net-tie footprint's graphics.
                }
                else
                {
                    std::shared_ptr<DRC_ITEM> drce = DRC_ITEM::Create( DRCE_CLEARANCE );
                    wxString msg = formatMsg( _( "(%s clearance %s; actual %s)" ),
                                              constraint.GetName(),
                                              clearance,
                                              actual );

                    drce->SetErrorMessage( drce->GetErrorText() + wxS( " " ) + msg );
                    drce->SetItems( pad, other );
                    drce->SetViolatingRule( constraint.GetParentRule() );

                    reportViolation( drce, pos, aLayer );
                    testHoles = false;  // No need for multiple violations
                }
            }
        }
    }

    if( testHoles )
    {
        constraint = m_drcEngine->EvalRules( HOLE_CLEARANCE_CONSTRAINT, pad, other, aLayer );
        clearance = constraint.GetValue().Min();

        if( constraint.GetSeverity() == RPT_SEVERITY_IGNORE )
            testHoles = false;
    }

    if( testHoles && otherPad && pad->FlashLayer( aLayer ) && otherPad->HasHole() )
    {
        if( clearance > 0 && padShape->Collide( otherPad->GetEffectiveHoleShape().get(),
                                                std::max( 0, clearance - m_drcEpsilon ),
                                                &actual, &pos ) )
        {
            std::shared_ptr<DRC_ITEM> drce = DRC_ITEM::Create( DRCE_HOLE_CLEARANCE );
            wxString msg = formatMsg( _( "(%s clearance %s; actual %s)" ),
                                      constraint.GetName(),
                                      clearance,
                                      actual );

            drce->SetErrorMessage( drce->GetErrorText() + wxS( " " ) + msg );
            drce->SetItems( pad, other );
            drce->SetViolatingRule( constraint.GetParentRule() );

            reportViolation( drce, pos, aLayer );
            testHoles = false;  // No need for multiple violations
        }
    }

    if( testHoles && otherPad && otherPad->FlashLayer( aLayer ) && pad->HasHole() )
    {
        if( clearance > 0 && otherShape->Collide( pad->GetEffectiveHoleShape().get(),
                                                  std::max( 0, clearance - m_drcEpsilon ),
                                                  &actual, &pos ) )
        {
            std::shared_ptr<DRC_ITEM> drce = DRC_ITEM::Create( DRCE_HOLE_CLEARANCE );
            wxString msg = formatMsg( _( "(%s clearance %s; actual %s)" ),
                                      constraint.GetName(),
                                      clearance,
                                      actual );

            drce->SetErrorMessage( drce->GetErrorText() + wxS( " " ) + msg );
            drce->SetItems( pad, other );
            drce->SetViolatingRule( constraint.GetParentRule() );

            reportViolation( drce, pos, aLayer );
            testHoles = false;  // No need for multiple violations
        }
    }

    if( testHoles && otherVia && otherVia->IsOnLayer( aLayer ) )
    {
        if( clearance > 0 && padShape->Collide( otherVia->GetEffectiveHoleShape().get(),
                                                std::max( 0, clearance - m_drcEpsilon ),
                                                &actual, &pos ) )
        {
            std::shared_ptr<DRC_ITEM> drce = DRC_ITEM::Create( DRCE_HOLE_CLEARANCE );
            wxString msg = formatMsg( _( "(%s clearance %s; actual %s)" ),
                                      constraint.GetName(),
                                      clearance,
                                      actual );

            drce->SetErrorMessage( drce->GetErrorText() + wxS( " " ) + msg );
            drce->SetItems( pad, otherVia );
            drce->SetViolatingRule( constraint.GetParentRule() );

            reportViolation( drce, pos, aLayer );
        }
    }

    return !m_drcEngine->IsCancelled();
}


void DRC_TEST_PROVIDER_COPPER_CLEARANCE::testPadClearances( )
{
    const int progressDelta = 100;
    size_t    count = 0;
    int       ii = 0;

    for( FOOTPRINT* footprint : m_board->Footprints() )
        count += footprint->Pads().size();

    reportAux( wxT( "Testing %d pads..." ), count );

    std::unordered_map<PTR_PTR_CACHE_KEY, int> checkedPairs;

    LSET boardCopperLayers = LSET::AllCuMask( m_board->GetCopperLayerCount() );

    for( FOOTPRINT* footprint : m_board->Footprints() )
    {
        for( PAD* pad : footprint->Pads() )
        {
            for( PCB_LAYER_ID layer : LSET( pad->GetLayerSet() & boardCopperLayers ).Seq() )
            {
                std::shared_ptr<SHAPE> padShape = pad->GetEffectiveShape( layer );

                m_board->m_CopperItemRTreeCache->QueryColliding( pad, layer, layer,
                        // Filter:
                        [&]( BOARD_ITEM* other ) -> bool
                        {
                            BOARD_ITEM* a = pad;
                            BOARD_ITEM* b = other;

                            // store canonical order so we don't collide in both directions
                            // (a:b and b:a)
                            if( static_cast<void*>( a ) > static_cast<void*>( b ) )
                                std::swap( a, b );

                            if( checkedPairs.find( { a, b } ) != checkedPairs.end() )
                            {
                                return false;
                            }
                            else
                            {
                                checkedPairs[ { a, b } ] = 1;
                                return true;
                            }
                        },
                        // Visitor
                        [&]( BOARD_ITEM* other ) -> bool
                        {
                            return testPadAgainstItem( pad, padShape.get(), layer, other );
                        },
                        m_board->m_DRCMaxClearance );

                for( ZONE* zone : m_board->m_DRCCopperZones )
                {
                    testItemAgainstZone( pad, zone, layer );

                    if( m_drcEngine->IsCancelled() )
                        return;
                }
            }

            if( !reportProgress( ii++, (int) count, progressDelta ) )
                return;
        }

        if( m_drcEngine->IsCancelled() )
            return;
    }
}


void DRC_TEST_PROVIDER_COPPER_CLEARANCE::testGraphicClearances( )
{
    const int progressDelta = 100;
    size_t    count = m_board->Drawings().size();
    int       ii = 0;

    for( FOOTPRINT* footprint : m_board->Footprints() )
        count += footprint->GraphicalItems().size();

    reportAux( wxT( "Testing %d graphics..." ), count );

    auto isKnockoutText =
            []( BOARD_ITEM* item )
            {
                if( item->Type() == PCB_TEXT_T )
                    return static_cast<PCB_TEXT*>( item )->IsKnockout();
                else if( item->Type() == PCB_FP_TEXT_T )
                    return static_cast<FP_TEXT*>( item )->IsKnockout();
                else
                    return false;
            };

    auto testGraphicAgainstZone =
            [&]( BOARD_ITEM* item )
            {
                if( item->Type() == PCB_BITMAP_T )
                    return;

                if( !IsCopperLayer( item->GetLayer() ) )
                    return;

                // Knockout text is most often knocked-out of a zone, so it's presumed to
                // collide with one.  However, if it collides with more than one, and they
                // have different nets, then we have a short.
                NETINFO_ITEM* inheritedNet = nullptr;

                for( ZONE* zone : m_board->m_DRCCopperZones )
                {
                    if( isKnockoutText( item ) )
                        testKnockoutTextAgainstZone( item, &inheritedNet, zone );
                    else
                        testItemAgainstZone( item, zone, item->GetLayer() );

                    if( m_drcEngine->IsCancelled() )
                        return;
                }
            };

    for( BOARD_ITEM* item : m_board->Drawings() )
    {
        testGraphicAgainstZone( item );

        if( !reportProgress( ii++, (int) count, progressDelta ) )
            return;
    }

    for( FOOTPRINT* footprint : m_board->Footprints() )
    {
        for( BOARD_ITEM* item : footprint->GraphicalItems() )
        {
            testGraphicAgainstZone( item );

            if( !reportProgress( ii++, (int) count, progressDelta ) )
                return;
        }
    }
}


void DRC_TEST_PROVIDER_COPPER_CLEARANCE::testZonesToZones()
{
    const int progressDelta = 50;

    bool      testClearance = !m_drcEngine->IsErrorLimitExceeded( DRCE_CLEARANCE );
    bool      testIntersects = !m_drcEngine->IsErrorLimitExceeded( DRCE_ZONES_INTERSECT );

    DRC_CONSTRAINT  constraint;
    bool            cancelled = false;
    std::vector<std::map<PCB_LAYER_ID, std::vector<SEG>>> poly_segments;

    poly_segments.resize( m_board->m_DRCCopperZones.size() );

    // Contains the index for zoneA, zoneB, the conflict point, the actual clearance, the required clearance, and the layer
    using report_data = std::tuple<int, int, VECTOR2I, int, int, PCB_LAYER_ID>;
    const int invalid_zone = -1;

    std::vector<std::future<report_data>> futures;
    thread_pool& tp = GetKiCadThreadPool();

    auto checkZones = [testClearance, testIntersects, &poly_segments, &cancelled, invalid_zone]
                        ( int zoneA, int zoneB,
                          int zone2zoneClearance, PCB_LAYER_ID layer ) -> report_data
    {
        // Iterate through all the segments of refSmoothedPoly
        std::map<VECTOR2I, int> conflictPoints;

        std::vector<SEG>& refSegments = poly_segments[zoneA][layer];
        std::vector<SEG>& testSegments = poly_segments[zoneB][layer];
        bool reported = false;
        auto invalid_result = std::make_tuple( invalid_zone, invalid_zone, VECTOR2I(), 0, 0, F_Cu );

        for( SEG& refSegment : refSegments )
        {
            int ax1 = refSegment.A.x;
            int ay1 = refSegment.A.y;
            int ax2 = refSegment.B.x;
            int ay2 = refSegment.B.y;

            // Iterate through all the segments in smoothed_polys[ia2]
            for( SEG& testSegment : testSegments )
            {
                // Build test segment
                VECTOR2I pt;

                int bx1 = testSegment.A.x;
                int by1 = testSegment.A.y;
                int bx2 = testSegment.B.x;
                int by2 = testSegment.B.y;

                // We have ensured that the A segment starts before the B segment, so if the
                // A segment ends before the B segment starts, we can skip to the next A
                if( ax2 < bx1 )
                    break;

                int d = GetClearanceBetweenSegments( bx1, by1, bx2, by2, 0,
                                                     ax1, ay1, ax2, ay2, 0,
                                                     zone2zoneClearance, &pt.x, &pt.y );

                if( d < zone2zoneClearance )
                {
                    if( d == 0 && testIntersects )
                        reported = true;
                    else if( testClearance )
                        reported = true;

                    if( reported )
                        return std::make_tuple( zoneA, zoneB, pt, d, zone2zoneClearance, layer );
                }

                if( cancelled )
                    return invalid_result;
            }
        }

        return invalid_result;
    };

    for( int layer_id = F_Cu; layer_id <= B_Cu; ++layer_id )
    {
        PCB_LAYER_ID layer = static_cast<PCB_LAYER_ID>( layer_id );
        int          zone2zoneClearance;

        // Skip over layers not used on the current board
        if( !m_board->IsLayerEnabled( layer ) )
            continue;

        for( size_t ii = 0; ii < m_board->m_DRCCopperZones.size(); ii++ )
        {
            if( m_board->m_DRCCopperZones[ii]->IsOnLayer( layer ) )
            {
                SHAPE_POLY_SET poly =
                        *m_board->m_DRCCopperZones[ii]->GetFilledPolysList( layer );
                std::vector<SEG>& poly_segs = poly_segments[ii][layer];

                poly.Fracture( SHAPE_POLY_SET::PM_FAST );
                poly.BuildBBoxCaches();
                poly_segs.reserve( poly.FullPointCount() );

                for( auto it = poly.IterateSegmentsWithHoles(); it; it++ )
                {
                    SEG seg = *it;

                    if( seg.A.x > seg.B.x )
                        seg.Reverse();

                    poly_segs.push_back( seg );
                }

                std::sort( poly_segs.begin(), poly_segs.end() );
            }
        }

        std::vector<std::pair<int, int>> zonePairs;

        for( size_t ia = 0; ia < m_board->m_DRCCopperZones.size(); ia++ )
        {
            if( !reportProgress( layer_id * m_board->m_DRCCopperZones.size() + ia,
                                 B_Cu * m_board->m_DRCCopperZones.size(), progressDelta ) )
            {
                return;     // DRC cancelled
            }

            ZONE* zoneA = m_board->m_DRCCopperZones[ia];

            if( !zoneA->IsOnLayer( layer ) )
                continue;

            for( size_t ia2 = ia + 1; ia2 < m_board->m_DRCCopperZones.size(); ia2++ )
            {
                ZONE* zoneB = m_board->m_DRCCopperZones[ia2];

                // test for same layer
                if( !zoneB->IsOnLayer( layer ) )
                    continue;

                // Test for same net
                if( zoneA->GetNetCode() == zoneB->GetNetCode() && zoneA->GetNetCode() >= 0 )
                    continue;

                // rule areas may overlap at will
                if( zoneA->GetIsRuleArea() || zoneB->GetIsRuleArea() )
                    continue;

                // Examine a candidate zone: compare zoneB to zoneA
                SHAPE_POLY_SET* polyA = m_board->m_DRCCopperZones[ia]->GetFill( layer );
                SHAPE_POLY_SET* polyB = m_board->m_DRCCopperZones[ia2]->GetFill( layer );

                if( !polyA->BBoxFromCaches().Intersects( polyB->BBoxFromCaches() ) )
                    continue;

                // Get clearance used in zone to zone test.
                constraint = m_drcEngine->EvalRules( CLEARANCE_CONSTRAINT, zoneA, zoneB, layer );
                zone2zoneClearance = constraint.GetValue().Min();

                if( constraint.GetSeverity() == RPT_SEVERITY_IGNORE || zone2zoneClearance <= 0 )
                    continue;

                futures.push_back( tp.submit( checkZones, ia, ia2, zone2zoneClearance, layer ) );
            }
        }
    }

    for( auto& task : futures )
    {
        if( !task.valid() )
            continue;

        std::future_status result;

        while( true )
        {
            result = task.wait_for( std::chrono::milliseconds( 200 ) );

            if( m_drcEngine->IsCancelled() )
            {
                cancelled = true;
                break;
            }

            if( result == std::future_status::ready )
            {
                report_data data = task.get();
                int zoneA_id = std::get<0>( data );
                int zoneB_id = std::get<1>( data );
                VECTOR2I pt = std::get<2>( data );
                int actual = std::get<3>( data );
                int required = std::get<4>( data );
                PCB_LAYER_ID layer = std::get<5>( data );

                if( zoneA_id != invalid_zone )
                {
                    ZONE* zoneA = m_board->m_DRCCopperZones[zoneA_id];
                    ZONE* zoneB = m_board->m_DRCCopperZones[zoneB_id];

                    constraint = m_drcEngine->EvalRules( CLEARANCE_CONSTRAINT, zoneA, zoneB, layer );
                    std::shared_ptr<DRC_ITEM> drce;

                    if( actual <= 0 && testIntersects )
                    {
                        drce = DRC_ITEM::Create( DRCE_ZONES_INTERSECT );
                    }
                    else if( testClearance )
                    {
                        drce = DRC_ITEM::Create( DRCE_CLEARANCE );
                        wxString msg = formatMsg( _( "(%s clearance %s; actual %s)" ),
                                                  constraint.GetName(),
                                                  required,
                                                  std::max( actual, 0 ) );

                        drce->SetErrorMessage( drce->GetErrorText() + wxS( " " ) + msg );
                    }

                    if( drce )
                    {
                        drce->SetItems( zoneA, zoneB );
                        drce->SetViolatingRule( constraint.GetParentRule() );

                        reportViolation( drce, pt, layer );
                    }
                }

                break;
            }

        }
    }
}


namespace detail
{
    static DRC_REGISTER_TEST_PROVIDER<DRC_TEST_PROVIDER_COPPER_CLEARANCE> dummy;
}
