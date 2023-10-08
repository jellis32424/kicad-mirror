/*
 * KiRouter - a push-and-(sometimes-)shove PCB router
 *
 * Copyright (C) 2013-2017 CERN
 * Copyright (C) 2016-2023 KiCad Developers, see AUTHORS.txt for contributors.
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

#include <class_draw_panel_gal.h>
#include <dialogs/dialog_unit_entry.h>
#include <kiplatform/ui.h>
#include <tool/tool_manager.h>
#include <tools/pcb_actions.h>
#include <tools/zone_filler_tool.h>
#include <tools/tool_event_utils.h>
#include <board_design_settings.h>
#include "pns_router.h"
#include "pns_meander_placer.h" // fixme: move settings to separate header
#include "pns_meander_skew_placer.h"
#include "pns_tune_status_popup.h"

#include "length_tuner_tool.h"
#include <bitmaps.h>

using namespace KIGFX;

// Actions, being statically-defined, require specialized I18N handling.  We continue to
// use the _() macro so that string harvesting by the I18N framework doesn't have to be
// specialized, but we don't translate on initialization and instead do it in the getters.

#undef _
#define _(s) s

static TOOL_ACTION ACT_StartTuning( "pcbnew.LengthTuner.StartTuning",
        AS_CONTEXT,
        'X', LEGACY_HK_NAME( "Add New Track" ),
        _( "New Track" ), _( "Starts laying a new track." ) );

static TOOL_ACTION ACT_EndTuning( "pcbnew.LengthTuner.EndTuning",
        AS_CONTEXT,
        WXK_END, LEGACY_HK_NAME( "Stop laying the current track." ),
        _( "End Track" ), _( "Stops laying the current meander." ) );

static TOOL_ACTION ACT_SpacingIncrease( "pcbnew.LengthTuner.SpacingIncrease",
        AS_CONTEXT,
        '1', LEGACY_HK_NAME( "Increase meander spacing by one step." ),
        _( "Increase Spacing" ), _( "Increase meander spacing by one step." ),
        BITMAPS::router_len_tuner_dist_incr );

static TOOL_ACTION ACT_SpacingDecrease( "pcbnew.LengthTuner.SpacingDecrease",
        AS_CONTEXT,
        '2', LEGACY_HK_NAME( "Decrease meander spacing by one step." ),
        _( "Decrease Spacing" ), _( "Decrease meander spacing by one step." ),
        BITMAPS::router_len_tuner_dist_decr );

static TOOL_ACTION ACT_AmplIncrease( "pcbnew.LengthTuner.AmplIncrease",
        AS_CONTEXT,
        '3', LEGACY_HK_NAME( "Increase meander amplitude by one step." ),
        _( "Increase Amplitude" ), _( "Increase meander amplitude by one step." ),
        BITMAPS::router_len_tuner_amplitude_incr );

static TOOL_ACTION ACT_AmplDecrease( "pcbnew.LengthTuner.AmplDecrease",
        AS_CONTEXT,
        '4', LEGACY_HK_NAME( "Decrease meander amplitude by one step." ),
        _( "Decrease Amplitude" ), _( "Decrease meander amplitude by one step." ),
        BITMAPS::router_len_tuner_amplitude_decr );

#undef _
#define _(s) wxGetTranslation((s))


LENGTH_TUNER_TOOL::LENGTH_TUNER_TOOL() :
    TOOL_BASE( "pcbnew.LengthTuner" ),
    m_inLengthTuner( false )
{
    // set the initial tune mode for the settings dialog,
    // in case the dialog is opened before the tool is activated the first time
    m_lastTuneMode = PNS::ROUTER_MODE::PNS_MODE_TUNE_SINGLE;
    m_inLengthTuner = false;
}


LENGTH_TUNER_TOOL::~LENGTH_TUNER_TOOL()
{
}


bool LENGTH_TUNER_TOOL::Init()
{
    m_inLengthTuner = false;

    auto tuning =
            [&]( const SELECTION& )
            {
                return m_router->RoutingInProgress();
            };

    auto& menu = m_menu.GetMenu();

    menu.SetTitle( _( "Length Tuner" ) );
    menu.SetIcon( BITMAPS::router_len_tuner );
    menu.DisplayTitle( true );

    menu.AddItem( ACTIONS::cancelInteractive,             SELECTION_CONDITIONS::ShowAlways );

    menu.AddSeparator();

    menu.AddItem( ACT_SpacingIncrease,                    tuning );
    menu.AddItem( ACT_SpacingDecrease,                    tuning );
    menu.AddItem( ACT_AmplIncrease,                       tuning );
    menu.AddItem( ACT_AmplDecrease,                       tuning );

    return true;
}


void LENGTH_TUNER_TOOL::Reset( RESET_REASON aReason )
{
    if( aReason == RUN )
        TOOL_BASE::Reset( aReason );
}


void LENGTH_TUNER_TOOL::updateStatusPopup( PNS_TUNE_STATUS_POPUP& aPopup )
{
    // fixme: wx code not allowed inside tools!
    wxPoint p = KIPLATFORM::UI::GetMousePosition();

    p.x += 20;
    p.y += 20;

    aPopup.UpdateStatus( m_router );
    aPopup.Move( p );
}


void LENGTH_TUNER_TOOL::performTuning()
{
    if( m_startItem )
    {
        frame()->SetActiveLayer( ToLAYER_ID ( m_startItem->Layers().Start() ) );

        if( m_startItem->Net() )
            highlightNets( true, { m_startItem->Net() } );
    }

    controls()->ForceCursorPosition( false );
    controls()->SetAutoPan( true );

    int layer = m_startItem ? m_startItem->Layer() : static_cast<int>( frame()->GetActiveLayer() );

    if( !m_router->StartRouting( m_startSnapPoint, m_startItem, layer ) )
    {
        frame()->ShowInfoBarMsg( m_router->FailureReason() );
        highlightNets( false );
        return;
    }

    BOARD_DESIGN_SETTINGS&    bds = board()->GetDesignSettings();
    PNS::MEANDER_PLACER_BASE* placer = static_cast<PNS::MEANDER_PLACER_BASE*>( m_router->Placer() );
    PNS::MEANDER_SETTINGS*    settings = nullptr;

    switch( m_lastTuneMode )
    {
    case PNS::PNS_MODE_TUNE_SINGLE:         settings = &bds.m_singleTrackMeanderSettings; break;
    case PNS::PNS_MODE_TUNE_DIFF_PAIR:      settings = &bds.m_diffPairMeanderSettings;    break;
    case PNS::PNS_MODE_TUNE_DIFF_PAIR_SKEW: settings = &bds.m_skewMeanderSettings;        break;
    default:
        wxFAIL_MSG( wxT( "Unsupported tuning mode." ) );
        m_router->StopRouting();
        highlightNets( false );
        return;
    }

    if( m_lastTuneMode == PNS::PNS_MODE_TUNE_DIFF_PAIR_SKEW )
    {
        PNS::MEANDER_SKEW_PLACER* skewPlacer = static_cast<PNS::MEANDER_SKEW_PLACER*>( placer );
        WX_UNIT_ENTRY_DIALOG      dlg( frame(), _( "Skew Tuning" ), _( "Target skew:" ),
                                       skewPlacer->CurrentSkew() );

        if( dlg.ShowModal() != wxID_OK )
        {
            m_router->StopRouting();
            highlightNets( false );
            return;
        }

        settings->m_targetLength = dlg.GetValue();
    }
    else
    {
        std::shared_ptr<DRC_ENGINE>& drcEngine = bds.m_DRCEngine;
        DRC_CONSTRAINT               constraint;

        constraint = drcEngine->EvalRules( LENGTH_CONSTRAINT, m_startItem->Parent(), nullptr,
                                           ToLAYER_ID( layer ) );

        if( constraint.IsNull() )
        {
            WX_UNIT_ENTRY_DIALOG dlg( frame(), _( "Length Tuning" ), _( "Target length:" ),
                                      100 * PCB_IU_PER_MM );

            if( dlg.ShowModal() != wxID_OK )
            {
                m_router->StopRouting();
                highlightNets( false );
                return;
            }

            settings->m_targetLength = dlg.GetValue();
        }
        else
        {
            settings->m_targetLength = constraint.GetValue().Opt();
        }
    }

    placer->UpdateSettings( *settings );

    frame()->UndoRedoBlock( true );

    VECTOR2I end = getViewControls()->GetMousePosition();

    // Create an instance of PNS_TUNE_STATUS_POPUP.
    PNS_TUNE_STATUS_POPUP statusPopup( frame() );
    statusPopup.Popup();
    canvas()->SetStatusPopup( statusPopup.GetPanel() );

    m_router->Move( end, nullptr );
    updateStatusPopup( statusPopup );

    auto setCursor =
            [&]()
            {
                frame()->GetCanvas()->SetCurrentCursor( KICURSOR::ARROW );
            };

    // Set initial cursor
    setCursor();

    while( TOOL_EVENT* evt = Wait() )
    {
        setCursor();

        if( evt->IsCancelInteractive() || evt->IsActivate() )
        {
            break;
        }
        else if( evt->IsMotion() )
        {
            end = evt->Position();
            m_router->Move( end, nullptr );
            updateStatusPopup( statusPopup );
        }
        else if( evt->IsClick( BUT_LEFT ) )
        {
            if( m_router->FixRoute( evt->Position(), nullptr ) )
                break;
        }
        else if( evt->IsClick( BUT_RIGHT ) )
        {
            m_menu.ShowContextMenu( selection() );
        }
        else if( evt->IsAction( &ACT_EndTuning ) )
        {
            if( m_router->FixRoute( end, nullptr ) )
                break;
        }
        else if( evt->IsAction( &ACT_AmplDecrease ) )
        {
            placer->AmplitudeStep( -1 );
            m_router->Move( end, nullptr );
            updateStatusPopup( statusPopup );
        }
        else if( evt->IsAction( &ACT_AmplIncrease ) )
        {
            placer->AmplitudeStep( 1 );
            m_router->Move( end, nullptr );
            updateStatusPopup( statusPopup );
        }
        else if(evt->IsAction( &ACT_SpacingDecrease ) )
        {
            placer->SpacingStep( -1 );
            m_router->Move( end, nullptr );
            updateStatusPopup( statusPopup );
        }
        else if( evt->IsAction( &ACT_SpacingIncrease ) )
        {
            placer->SpacingStep( 1 );
            m_router->Move( end, nullptr );
            updateStatusPopup( statusPopup );
        }
        // TODO: It'd be nice to be able to say "don't allow any non-trivial editing actions",
        // but we don't at present have that, so we just knock out some of the egregious ones.
        else if( ZONE_FILLER_TOOL::IsZoneFillAction( evt ) )
        {
            wxBell();
        }
        else
        {
            evt->SetPassEvent();
        }
    }

    m_router->StopRouting();
    frame()->UndoRedoBlock( false );

    canvas()->SetStatusPopup( nullptr );
    controls()->SetAutoPan( false );
    controls()->ForceCursorPosition( false );
    frame()->GetCanvas()->SetCurrentCursor( KICURSOR::ARROW );
    highlightNets( false );
}


void LENGTH_TUNER_TOOL::setTransitions()
{
    Go( &LENGTH_TUNER_TOOL::MainLoop,       PCB_ACTIONS::routerTuneSingleTrace.MakeEvent() );
    Go( &LENGTH_TUNER_TOOL::MainLoop,       PCB_ACTIONS::routerTuneDiffPair.MakeEvent() );
    Go( &LENGTH_TUNER_TOOL::MainLoop,       PCB_ACTIONS::routerTuneDiffPairSkew.MakeEvent() );
}


int LENGTH_TUNER_TOOL::MainLoop( const TOOL_EVENT& aEvent )
{
    if( m_inLengthTuner )
        return 0;

    REENTRANCY_GUARD guard( &m_inLengthTuner );

    // Deselect all items
    m_toolMgr->RunAction( PCB_ACTIONS::selectionClear );

    frame()->PushTool( aEvent );

    auto setCursor =
            [&]()
            {
                frame()->GetCanvas()->SetCurrentCursor( KICURSOR::ARROW );
            };

    Activate();
    // Must be done after Activate() so that it gets set into the correct context
    controls()->ShowCursor( true );
    // Set initial cursor
    setCursor();

    // Router mode must be set after Activate()
    m_lastTuneMode = aEvent.Parameter<PNS::ROUTER_MODE>();
    m_router->SetMode( m_lastTuneMode );

    // Main loop: keep receiving events
    while( TOOL_EVENT* evt = Wait() )
    {
        setCursor();

        if( evt->IsCancelInteractive() || evt->IsActivate() )
        {
            break; // Finish
        }
        else if( evt->Action() == TA_UNDO_REDO_PRE )
        {
            m_router->ClearWorld();
        }
        else if( evt->Action() == TA_UNDO_REDO_POST || evt->Action() == TA_MODEL_CHANGE )
        {
            m_router->SyncWorld();
        }
        else if( evt->IsMotion() )
        {
            updateStartItem( *evt );
        }
        else if( evt->IsClick( BUT_LEFT ) || evt->IsAction( &ACT_StartTuning ) )
        {
            updateStartItem( *evt );
            performTuning();
        }
        else if( evt->IsClick( BUT_RIGHT ) )
        {
            m_menu.ShowContextMenu( selection() );
        }
        else
        {
            evt->SetPassEvent();
        }
    }

    // Store routing settings till the next invocation
    m_savedSizes = m_router->Sizes();

    frame()->GetCanvas()->SetCurrentCursor( KICURSOR::ARROW );
    frame()->PopTool( aEvent );
    return 0;
}
