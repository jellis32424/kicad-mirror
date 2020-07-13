/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 1992-2017 jean-pierre Charras jp.charras at wanadoo.fr
 * Copyright (C) 1992-2020 Kicad Developers, see AUTHORS.txt for contributors.
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


#include <fctsys.h>
#include <sch_edit_frame.h>
#include <bitmaps.h>
#include <confirm.h>
#include <dialog_annotate_base.h>
#include <eeschema_settings.h>
#include <kiface_i.h>
#include <wx_html_report_panel.h>

// A window name for the annotate dialog to retrieve is if not destroyed
#define DLG_WINDOW_NAME "DialogAnnotateWindowName"


/**
 * DIALOG_ANNOTATE: a dialog to set/clear reference designators,
 * of a schematic hierarchy, with different options
 */
class DIALOG_ANNOTATE: public DIALOG_ANNOTATE_BASE
{
public:
    DIALOG_ANNOTATE( SCH_EDIT_FRAME* parent, const wxString& message );
    ~DIALOG_ANNOTATE();

private:
    SCH_EDIT_FRAME* m_Parent;

    /// Initialises member variables
    void InitValues();
    void OnClearAnnotationClick( wxCommandEvent& event ) override;
    void OnCloseClick( wxCommandEvent& event ) override;
    void OnClose( wxCloseEvent& event ) override;
    void OnApplyClick( wxCommandEvent& event ) override;

    // User functions:
    bool GetLevel();
    bool GetResetItems();
    bool GetLockUnits();

    /**
     * Function GetSortOrder
     * @return 0 if annotation by X position,
     *         1 if annotation by Y position,
     */
    int GetSortOrder();

    /**
     * Function GetAnnotateAlgo
     * @return 0 if annotation using first free Id value
     *         1 for first free Id value inside sheet num * 100 to sheet num * 100 + 99
     *         2 for first free Id value inside sheet num * 1000 to sheet num * 1000 + 999
     */
    int GetAnnotateAlgo();

    int GetStartNumber();
};


DIALOG_ANNOTATE::DIALOG_ANNOTATE( SCH_EDIT_FRAME* parent, const wxString& message )
    : DIALOG_ANNOTATE_BASE( parent )
{
    SetName( DLG_WINDOW_NAME );
    m_Parent = parent;

    if( !message.IsEmpty() )
    {
        m_userMessage->SetLabelText( message );
        m_userMessage->Show( true );

        m_rbScope->Enable( false );
    }

    m_MessageWindow->SetLabel( _( "Annotation Messages:" ) );

    // We use a sdbSizer to get platform-dependent ordering of the action buttons, but
    // that requires us to correct the button labels here.
    m_sdbSizer1OK->SetLabel( _( "Annotate" ) );
    m_sdbSizer1Cancel->SetLabel( _( "Close" ) );
    m_sdbSizer1->Layout();

    m_sdbSizer1OK->SetDefault();

    InitValues();
    Layout();

    // When all widgets have the size fixed, call FinishDialogSettings
    FinishDialogSettings();
}


DIALOG_ANNOTATE::~DIALOG_ANNOTATE()
{
    auto cfg = static_cast<EESCHEMA_SETTINGS*>( Kiface().KifaceSettings() );

    cfg->m_AnnotatePanel.sort_order = GetSortOrder();
    cfg->m_AnnotatePanel.method = GetAnnotateAlgo();
    cfg->m_AnnotatePanel.messages_filter = m_MessageWindow->GetVisibleSeverities();
}


void DIALOG_ANNOTATE::InitValues()
{
    EESCHEMA_SETTINGS* cfg = static_cast<EESCHEMA_SETTINGS*>( Kiface().KifaceSettings() );
    int option;

    // These are always reset to attempt to keep the user out of trouble...
    m_rbScope->SetSelection( 0 );
    m_rbOptions->SetSelection( 0 );

    option = cfg->m_AnnotatePanel.sort_order;

    switch( option )
    {
    default:
    case 0: m_rbSortBy_X_Position->SetValue( true ); break;
    case 1: m_rbSortBy_Y_Position->SetValue( true ); break;
    }

    option = cfg->m_AnnotatePanel.method;

    switch( option )
    {
    default:
    case 0: m_rbFirstFree->SetValue( true );  break;
    case 1: m_rbSheetX100->SetValue( true );  break;
    case 2: m_rbSheetX1000->SetValue( true ); break;
    }

    m_textNumberAfter->SetValue( wxT( "0" ) );

    annotate_down_right_bitmap->SetBitmap( KiBitmap( annotate_down_right_xpm ) );
    annotate_right_down_bitmap->SetBitmap( KiBitmap( annotate_right_down_xpm ) );

    m_MessageWindow->SetVisibleSeverities( cfg->m_AnnotatePanel.messages_filter );

    m_MessageWindow->MsgPanelSetMinSize( wxSize( -1, 160 ) );
}


// This is a modeless dialog so we have to handle these ourselves.
void DIALOG_ANNOTATE::OnCloseClick( wxCommandEvent& event )
{
    Close();
}


void DIALOG_ANNOTATE::OnClose( wxCloseEvent& event )
{
    Destroy();
}


void DIALOG_ANNOTATE::OnApplyClick( wxCommandEvent& event )
{
    m_MessageWindow->Clear();
    REPORTER& reporter = m_MessageWindow->Reporter();
    m_MessageWindow->SetLazyUpdate( true );     // Don't update after each message

    m_Parent->AnnotateComponents( GetLevel(), (ANNOTATE_ORDER_T) GetSortOrder(),
                                  (ANNOTATE_OPTION_T) GetAnnotateAlgo(), GetStartNumber(),
                                  GetResetItems() , true, GetLockUnits(), reporter );

    m_MessageWindow->Flush( true );             // Now update to show all messages

    m_Parent->GetCanvas()->Refresh();

    m_btnClear->Enable();

    // Don't close dialog if there are things the user needs to address
    if( reporter.HasMessage() )
        return;

    if( m_userMessage->IsShown() )
    {
        // Close the dialog by calling the default handler for a wxID_OK event
        event.SetId( wxID_OK );
        event.Skip();
    }
}


void DIALOG_ANNOTATE::OnClearAnnotationClick( wxCommandEvent& event )
{
    bool appendUndo = false;

    m_Parent->DeleteAnnotation( !GetLevel(), &appendUndo );
    m_btnClear->Enable( false );
}


bool DIALOG_ANNOTATE::GetLevel()
{
    return m_rbScope->GetSelection() == 0;
}


bool DIALOG_ANNOTATE::GetResetItems()
{
    return m_rbOptions->GetSelection() >= 1;
}


bool DIALOG_ANNOTATE::GetLockUnits()
{
    return m_rbOptions->GetSelection() == 2;
}


int DIALOG_ANNOTATE::GetSortOrder()
{
    if( m_rbSortBy_Y_Position->GetValue() )
        return 1;
    else
        return 0;
}


int DIALOG_ANNOTATE::GetAnnotateAlgo()
{
    if( m_rbSheetX100->GetValue() )
        return 1;
    else if( m_rbSheetX1000->GetValue() )
        return 2;
    else
        return 0;
}


int DIALOG_ANNOTATE::GetStartNumber()
{
    return ValueFromString( EDA_UNITS::UNSCALED, m_textNumberAfter->GetValue() );
}


void SCH_EDIT_FRAME::OnAnnotate( wxCommandEvent& event )
{
    DIALOG_ANNOTATE* dlg = static_cast<DIALOG_ANNOTATE*> ( wxWindow::FindWindowByName( DLG_WINDOW_NAME ) );

    if( !dlg )
    {
        dlg = new DIALOG_ANNOTATE( this, wxEmptyString );
        dlg->Show( true );
    }
    else    // The dialog is already opened, perhaps not visible
    {
        dlg->Show( true );
    }
}


int SCH_EDIT_FRAME::ModalAnnotate( const wxString& aMessage )
{
    DIALOG_ANNOTATE dlg( this, aMessage );

    return dlg.ShowModal();
}
