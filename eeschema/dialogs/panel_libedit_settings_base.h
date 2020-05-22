///////////////////////////////////////////////////////////////////////////
// C++ code generated with wxFormBuilder (version Oct 26 2018)
// http://www.wxformbuilder.org/
//
// PLEASE DO *NOT* EDIT THIS FILE!
///////////////////////////////////////////////////////////////////////////

#pragma once

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/intl.h>
#include <wx/string.h>
#include <wx/stattext.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/textctrl.h>
#include <wx/sizer.h>
#include <wx/checkbox.h>
#include <wx/statbox.h>
#include <wx/choice.h>
#include <wx/spinctrl.h>
#include <wx/panel.h>

///////////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////////////
/// Class PANEL_LIBEDIT_SETTINGS_BASE
///////////////////////////////////////////////////////////////////////////////
class PANEL_LIBEDIT_SETTINGS_BASE : public wxPanel
{
	private:

	protected:
		wxStaticText* m_lineWidthLabel;
		wxTextCtrl* m_lineWidthCtrl;
		wxStaticText* m_lineWidthUnits;
		wxStaticText* m_textSizeLabel;
		wxTextCtrl* m_textSizeCtrl;
		wxStaticText* m_textSizeUnits;
		wxStaticText* m_pinLengthLabel;
		wxTextCtrl* m_pinLengthCtrl;
		wxStaticText* m_pinLengthUnits;
		wxStaticText* m_pinNumSizeLabel;
		wxTextCtrl* m_pinNumSizeCtrl;
		wxStaticText* m_pinNumSizeUnits;
		wxStaticText* m_pinNameSizeLabel;
		wxTextCtrl* m_pinNameSizeCtrl;
		wxStaticText* m_pinNameSizeUnits;
		wxCheckBox* m_cbShowPinElectricalType;
		wxStaticText* m_hPitchLabel;
		wxTextCtrl* m_hPitchCtrl;
		wxStaticText* m_hPitchUnits;
		wxStaticText* m_vPitchLabel;
		wxTextCtrl* m_vPitchCtrl;
		wxStaticText* m_vPitchUnits;
		wxStaticText* m_pinPitchLabel;
		wxChoice* m_choicePinDisplacement;
		wxStaticText* m_pinPitchUnits;
		wxStaticText* m_labelIncrementLabel1;
		wxSpinCtrl* m_spinRepeatLabel;

	public:

		PANEL_LIBEDIT_SETTINGS_BASE( wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize( -1,-1 ), long style = wxTAB_TRAVERSAL, const wxString& name = wxEmptyString );
		~PANEL_LIBEDIT_SETTINGS_BASE();

};

