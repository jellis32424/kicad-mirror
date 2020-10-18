/**
 * @file select_layers_to_pcb.cpp
 * @brief Dialog to choose equivalence between gerber layers and pcb layers
 */

/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 1992-2020 KiCad Developers, see AUTHORS.txt for contributors.
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

#include <X2_gerber_attributes.h>
#include <gerber_file_image.h>
#include <gerber_file_image_list.h>
#include <gerbview.h>
#include <gerbview_frame.h>
#include <gerbview_id.h>
#include <gerbview_settings.h>
#include <kiface_i.h>
#include <layers_id_colors_and_visibility.h>

#include <dialogs/dialog_layers_select_to_pcb.h>

#include <wx/msgdlg.h>

// Imported function
extern const wxString GetPCBDefaultLayerName( LAYER_NUM aLayerNumber );

enum swap_layer_id {
    ID_LAYERS_MAP_DIALOG = ID_GERBER_END_LIST,
    ID_BUTTON_0,
    ID_TEXT_0 = ID_BUTTON_0 + GERBER_DRAWLAYERS_COUNT
};


/*
 * This dialog shows the gerber files loaded, and allows user to choose:
 *   what gerber file and what board layer are used
 *   the number of copper layers
 */

int LAYERS_MAP_DIALOG::m_exportBoardCopperLayersCount = 2;


BEGIN_EVENT_TABLE( LAYERS_MAP_DIALOG, LAYERS_MAP_DIALOG_BASE )
    EVT_COMMAND_RANGE( ID_BUTTON_0, ID_BUTTON_0 + GERBER_DRAWLAYERS_COUNT-1,
                       wxEVT_COMMAND_BUTTON_CLICKED,
                       LAYERS_MAP_DIALOG::OnSelectLayer )
END_EVENT_TABLE()


LAYERS_MAP_DIALOG::LAYERS_MAP_DIALOG( GERBVIEW_FRAME* parent ) :
    LAYERS_MAP_DIALOG_BASE( parent )
{
    m_Parent = parent;
    initDialog();

    // Resize the dialog
    Layout();
    GetSizer()->SetSizeHints( this );
    Centre();
}


void LAYERS_MAP_DIALOG::initDialog()
{
    wxStaticText* label;
    wxStaticText* text;
    int           item_ID;
    wxString      msg;
    wxSize        goodSize;
    GERBVIEW_SETTINGS* config = static_cast<GERBVIEW_SETTINGS*>( Kiface().KifaceSettings() );

    for( int ii = 0; ii < GERBER_DRAWLAYERS_COUNT; ++ii )
    {
        // Specify the default value for each member of these arrays.
        m_buttonTable[ii]       = -1;
        m_layersLookUpTable[ii] = UNSELECTED_LAYER;
    }

    // Ensure we have:
    //    At least 2 copper layers and less than max pcb copper layers count
    //    Even number of layers because a board *must* have even layers count
    normalizeBrdLayersCount();

    int idx = ( m_exportBoardCopperLayersCount / 2 ) - 1;
    m_comboCopperLayersCount->SetSelection( idx );

    m_gerberActiveLayersCount      = 0;
    GERBER_FILE_IMAGE_LIST* images = m_Parent->GetGerberLayout()->GetImagesList();

    for( unsigned ii = 0; ii < GERBER_DRAWLAYERS_COUNT; ++ii )
    {
        if( images->GetGbrImage( ii ) == NULL )
            break;

        m_buttonTable[m_gerberActiveLayersCount] = ii;
        m_gerberActiveLayersCount++;
    }

    if( m_gerberActiveLayersCount <= GERBER_DRAWLAYERS_COUNT / 2 ) // Only one list is enough
        m_staticlineSep->Hide();

    wxFlexGridSizer* flexColumnBoxSizer = m_flexLeftColumnBoxSizer;

    for( int ii = 0; ii < m_gerberActiveLayersCount; ii++ )
    {
        // Each Gerber layer has an associated static text string (to
        // identify that layer), a button (for invoking a child dialog
        // box to change which Pcbnew layer that the Gerber layer is
        // mapped to), and a second static text string (to depict which
        // Pcbnew layer that the Gerber layer has been mapped to). Each
        // of those items are placed into the left hand column, middle
        // column, and right hand column (respectively) of the Flexgrid
        // sizer, and the color of the second text string is set to
        // fuchsia or blue (to respectively indicate whether the Gerber
        // layer has been mapped to a Pcbnew layer or is not being
        // exported at all).  (Experimentation has shown that if a text
        // control is used to depict which Pcbnew layer that each Gerber
        // layer is mapped to (instead of a static text string), then
        // those controls do not behave in a fully satisfactory manner
        // in the Linux version. Even when the read-only attribute is
        // specified for all of those controls, they can still be selected
        // when the arrow keys or Tab key is used to step through all of
        // the controls within the dialog box, and directives to set the
        // foreground color of the text of each such control to blue (to
        // indicate that the text is of a read-only nature) are disregarded.
        // Specify a FlexGrid sizer with an appropriate number of rows
        // and three columns.  If nb_items < 16, then the number of rows
        // is nb_items; otherwise, the number of rows is 16 (with two
        // separate columns of controls being used if nb_items > 16).

        if( ii == GERBER_DRAWLAYERS_COUNT / 2 )
            flexColumnBoxSizer = m_flexRightColumnBoxSizer;

        // Provide a text string to identify the Gerber layer
        msg.Printf( _( "Layer %d" ), m_buttonTable[ii] + 1 );

        label = new wxStaticText( this, wxID_STATIC, msg );
        flexColumnBoxSizer->Add( label, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5 );

        /* Add file name and extension without path.  */
        wxFileName fn( images->GetGbrImage( ii )->m_FileName );
        label = new wxStaticText( this, wxID_STATIC, fn.GetFullName() );
        flexColumnBoxSizer->Add( label, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5 );

        // Provide a button for this layer (which will invoke a child dialog box)
        item_ID          = ID_BUTTON_0 + ii;
        wxButton * Button = new wxButton( this, item_ID, wxT( "..." ), wxDefaultPosition,
                                          wxDefaultSize, wxBU_EXACTFIT );

        flexColumnBoxSizer->Add( Button, 0, wxALIGN_CENTER_VERTICAL | wxALL );

        // Provide another text string to specify which Pcbnew layer that this
        // Gerber layer is mapped to.  All layers initially default to
        // "Do NotExport" (which corresponds to UNSELECTED_LAYER).  Whenever
        // a layer is set to "Do Not Export" it's displayed in blue.  When a
        // user selects a specific KiCad layer to map to, it's displayed in
        // magenta which indicates it will be exported.
        item_ID = ID_TEXT_0 + ii;

        // All layers default to "Do Not Export" displayed in blue
        msg  = _( "Do not export" );
        text = new wxStaticText( this, item_ID, msg );
        text->SetForegroundColour( *wxBLUE );

        // When the first of these text strings is being added, determine what
        // size is necessary to to be able to display any possible string
        // without it being truncated. Then specify that size as the minimum
        // size for all of these text strings. (If this minimum size is not
        // determined in this fashion, then it is possible for the display of
        // one or more of these strings to be truncated after different Pcbnew
        // layers are selected.)

        if( ii == 0 )
        {
            goodSize = text->GetSize();

            for( LAYER_NUM jj = 0; jj < GERBER_DRAWLAYERS_COUNT; ++jj )
            {
                text->SetLabel( GetPCBDefaultLayerName( jj ) );

                if( goodSize.x < text->GetSize().x )
                    goodSize.x = text->GetSize().x;
            }
            text->SetLabel( msg ); // Reset label to default text
        }

        text->SetMinSize( goodSize );
        flexColumnBoxSizer->Add( text, 1, wxALIGN_CENTER_VERTICAL | wxALL, 5 );

        m_layersList[ii] = text;
    }

    // If the user has never stored any Gerber to Kicad layer mapping,
    // then disable the button to retrieve it
    if( config->m_GerberToPcbLayerMapping.size() == 0 )
        m_buttonRetrieve->Enable( false );


    std::vector<int> gerber2KicadMapping;

    // See how many of the loaded Gerbers can be mapped to KiCad layers automatically
    int numMappedGerbers = findKnownGerbersLoaded( gerber2KicadMapping );

    if( numMappedGerbers > 0 )
    {
        // See if the user wants to map the Altium Gerbers to known KiCad PCB layers
        int returnVal = wxMessageBox(
                _( "Gerbers with known layers: " + wxString::Format( wxT( "%i" ), numMappedGerbers )
                        + "\n\nAssign to matching KiCad PCB layers?" ),
                _( "Automatic Layer Assignment" ), wxOK | wxCANCEL | wxOK_DEFAULT );

        if( returnVal == wxOK )
        {
            for( int ii = 0; ii < m_gerberActiveLayersCount; ii++ )
            {
                int currLayer = gerber2KicadMapping[ii];

                // Default to "Do Not Export" for unselected or undefined layer
                if( ( currLayer == UNSELECTED_LAYER ) || ( currLayer == UNDEFINED_LAYER ) )
                {
                    m_layersList[ii]->SetLabel( _( "Do not export" ) );
                    m_layersList[ii]->SetForegroundColour( *wxBLUE );

                    // Set the layer internally to unselected
                    m_layersLookUpTable[ii] = UNSELECTED_LAYER;
                }
                else
                {
                    m_layersList[ii]->SetLabel( GetPCBDefaultLayerName( currLayer ) );
                    m_layersList[ii]->SetForegroundColour( wxColour( 255, 0, 128 ) );

                    // Set the layer internally to the matching KiCad layer
                    m_layersLookUpTable[ii] = currLayer;
                }
            }
        }
    }
}

/* Ensure m_exportBoardCopperLayersCount = 2 to BOARD_COPPER_LAYERS_MAX_COUNT
 * and it is an even value because Boards have always an even layer count
 */
void LAYERS_MAP_DIALOG::normalizeBrdLayersCount()
{
    if( ( m_exportBoardCopperLayersCount & 1 ) )
        m_exportBoardCopperLayersCount++;

    if( m_exportBoardCopperLayersCount > GERBER_DRAWLAYERS_COUNT )
        m_exportBoardCopperLayersCount = GERBER_DRAWLAYERS_COUNT;

    if( m_exportBoardCopperLayersCount < 2 )
        m_exportBoardCopperLayersCount = 2;

}

/*
 * Called when user change the current board copper layers count
 */
void LAYERS_MAP_DIALOG::OnBrdLayersCountSelection( wxCommandEvent& event )
{
    int id = event.GetSelection();
    m_exportBoardCopperLayersCount = (id+1) * 2;
}

/*
 * reset pcb layers selection to the default value
 */
void LAYERS_MAP_DIALOG::OnResetClick( wxCommandEvent& event )
{
    wxString  msg;
    int       ii;
    LAYER_NUM layer;
    for( ii = 0, layer = 0; ii < m_gerberActiveLayersCount; ii++, ++layer )
    {
        m_layersLookUpTable[ii] = UNSELECTED_LAYER;
        m_layersList[ii]->SetLabel( _( "Do not export" ) );
        m_layersList[ii]->SetForegroundColour( *wxBLUE );
        m_buttonTable[ii] = ii;
    }
}


/* Stores the current layers selection in config
 */
void LAYERS_MAP_DIALOG::OnStoreSetup( wxCommandEvent& event )
{
    auto config = static_cast<GERBVIEW_SETTINGS*>( Kiface().KifaceSettings() );
    config->m_BoardLayersCount = m_exportBoardCopperLayersCount;

    config->m_GerberToPcbLayerMapping.clear();

    for( int ii = 0; ii < GERBER_DRAWLAYERS_COUNT; ++ii )
    {
        config->m_GerberToPcbLayerMapping.push_back( m_layersLookUpTable[ii] );
    }

    // Enable the "Get Stored Choice" button in case it was disabled in "initDialog()"
    // due to no previously stored choices.
    m_buttonRetrieve->Enable( true );
}

void LAYERS_MAP_DIALOG::OnGetSetup( wxCommandEvent& event )
{
    GERBVIEW_SETTINGS* config = static_cast<GERBVIEW_SETTINGS*>( Kiface().KifaceSettings() );

    m_exportBoardCopperLayersCount = config->m_BoardLayersCount;
    normalizeBrdLayersCount();

    int idx = ( m_exportBoardCopperLayersCount / 2 ) - 1;
    m_comboCopperLayersCount->SetSelection( idx );

    for( int ii = 0; ii < GERBER_DRAWLAYERS_COUNT; ++ii )
    {
        // Ensure the layer mapping in config exists for this layer, and store it
        if( (size_t)ii >= config->m_GerberToPcbLayerMapping.size() )
            break;

        m_layersLookUpTable[ii] = config->m_GerberToPcbLayerMapping[ ii ];
    }

    for( int ii = 0; ii < m_gerberActiveLayersCount; ii++ )
    {
        LAYER_NUM layer = m_layersLookUpTable[ii];
        if( layer == UNSELECTED_LAYER )
        {
            m_layersList[ii]->SetLabel( _( "Do not export" ) );
            m_layersList[ii]->SetForegroundColour( *wxBLUE );
        }
        else if( layer == UNDEFINED_LAYER )
        {
            m_layersList[ii]->SetLabel( _( "Hole data" ) );
            m_layersList[ii]->SetForegroundColour( wxColour( 255, 0, 128 ) );
        }
        else
        {
            m_layersList[ii]->SetLabel( GetPCBDefaultLayerName( layer ) );
            m_layersList[ii]->SetForegroundColour( wxColour( 255, 0, 128 ) );
        }
    }
}

void LAYERS_MAP_DIALOG::OnSelectLayer( wxCommandEvent& event )
{
    int ii;

    ii = event.GetId() - ID_BUTTON_0;

    if( (ii < 0) || (ii >= GERBER_DRAWLAYERS_COUNT) )
    {
        wxFAIL_MSG( wxT("Bad layer id") );
        return;
    }

    LAYER_NUM jj = m_layersLookUpTable[m_buttonTable[ii]];

    if( jj != UNSELECTED_LAYER && jj != UNDEFINED_LAYER && !IsValidLayer( jj ) )
        jj = B_Cu;  // (Defaults to "Copper" layer.)

    // Get file name of Gerber loaded on this layer
    wxFileName fn( m_Parent->GetGerberLayout()->GetImagesList()->GetGbrImage( ii )->m_FileName );
    // Surround it with quotes to make it stand out on the dialog title bar
    wxString layerName = "\"" + fn.GetFullName() + "\"";

    // Display dialog to let user select a layer for the Gerber
    jj = m_Parent->SelectPCBLayer( jj, m_exportBoardCopperLayersCount, layerName );

    if( jj != UNSELECTED_LAYER && jj != UNDEFINED_LAYER && !IsValidLayer( jj ) )
        return;

    if( jj != m_layersLookUpTable[m_buttonTable[ii]] )
    {
        m_layersLookUpTable[m_buttonTable[ii]] = jj;

        if( jj == UNSELECTED_LAYER )
        {
            m_layersList[ii]->SetLabel( _( "Do not export" ) );

            // Change the text color to blue (to highlight
            // that this layer is *not* being exported)
            m_layersList[ii]->SetForegroundColour( *wxBLUE );
        }
        else if( jj == UNDEFINED_LAYER )
        {
            m_layersList[ii]->SetLabel( _( "Hole data" ) );

            // Change the text color to fuchsia (to highlight
            // that this layer *is* being exported)
            m_layersList[ii]->SetForegroundColour( wxColour( 255, 0, 128 ) );
        }
        else
        {
            m_layersList[ii]->SetLabel( GetPCBDefaultLayerName( jj ) );

            // Change the text color to fuchsia (to highlight
            // that this layer *is* being exported)
            m_layersList[ii]->SetForegroundColour( wxColour( 255, 0, 128 ) );
        }
    }
}


void LAYERS_MAP_DIALOG::OnOkClick( wxCommandEvent& event )
{
    /* Make some test about copper layers:
     * Board must have enough copper layers to handle selected internal layers
     */
    normalizeBrdLayersCount();

    int inner_layer_max = 0;
    for( int ii = 0; ii < GERBER_DRAWLAYERS_COUNT; ++ii )
    {
            if( m_layersLookUpTable[ii] < F_Cu )
            {
                if( m_layersLookUpTable[ii ] > inner_layer_max )
                    inner_layer_max = m_layersLookUpTable[ii];
            }
    }

    // inner_layer_max must be less than  (or equal to) the number of
    // internal copper layers
    // internal copper layers = m_exportBoardCopperLayersCount-2
    if( inner_layer_max > m_exportBoardCopperLayersCount-2 )
    {
        wxMessageBox(
        _("Exported board does not have enough copper layers to handle selected inner layers") );
        return;
    }

    EndModal( wxID_OK );
}


int LAYERS_MAP_DIALOG::findKnownGerbersLoaded( std::vector<int>& aGerber2KicadMapping )
{
    int numKnownGerbers = 0;

    // We can automatically map Gerbers using different techniques.  The first thing we
    // try is to see if any of the loaded Gerbers were created by or use the
    // Altium/Protel file extensions
    numKnownGerbers += findNumAltiumGerbersLoaded( aGerber2KicadMapping );

    // Next we check if any of the loaded Gerbers are X2 Gerbers and if they contain

    // layer information in "File Functions". For info about X2 Gerbers see
    // http://www.ucamco.com/files/downloads/file/81/the_gerber_file_format_specification.pdf
    numKnownGerbers += findNumX2GerbersLoaded( aGerber2KicadMapping );

    // Finally, check if any of the loaded Gerbers use the KiCad naming conventions
    numKnownGerbers += findNumKiCadGerbersLoaded( aGerber2KicadMapping );

    return numKnownGerbers;
}


int LAYERS_MAP_DIALOG::findNumAltiumGerbersLoaded( std::vector<int>& aGerber2KicadMapping )
{
    // The next comment preserves initializer formatting below it
    // clang-format off
    // This map contains the known Altium file extensions for Gerbers that we care about,
    // along with their corresponding KiCad layer
    std::map<wxString, PCB_LAYER_ID> altiumExt{
        { "GTL", F_Cu },      // Top copper
        { "G1", In1_Cu },     // Inner layers 1 - 30
        { "G2", In2_Cu },
        { "G3", In3_Cu },
        { "G4", In4_Cu },
        { "G5", In5_Cu },
        { "G6", In6_Cu },
        { "G7", In7_Cu },
        { "G8", In8_Cu },
        { "G9", In9_Cu },
        { "G10", In10_Cu },
        { "G11", In11_Cu },
        { "G12", In12_Cu },
        { "G13", In13_Cu },
        { "G14", In14_Cu },
        { "G15", In15_Cu },
        { "G16", In16_Cu },
        { "G17", In17_Cu },
        { "G18", In18_Cu },
        { "G19", In19_Cu },
        { "G20", In20_Cu },
        { "G21", In21_Cu },
        { "G22", In22_Cu },
        { "G23", In23_Cu },
        { "G24", In24_Cu },
        { "G25", In25_Cu },
        { "G26", In26_Cu },
        { "G27", In27_Cu },
        { "G28", In28_Cu },
        { "G29", In29_Cu },
        { "G30", In30_Cu },
        { "GBL", B_Cu },      // Bottom copper
        { "GTP", F_Paste },   // Paste top
        { "GBP", B_Paste },   // Paste bottom
        { "GTO", F_SilkS },   // Silkscreen top
        { "GBO", B_SilkS },   // Silkscreen bottom
        { "GTS", F_Mask },    // Soldermask top
        { "GBS", B_Mask },    // Soldermask bottom
        { "GM1", Eco1_User }, // Altium mechanical layer 1
        { "GM2", Eco2_User }, // Altium mechanical layer 2
        { "GKO", Edge_Cuts }  // PCB Outline
    };
    // clang-format on

    std::map<wxString, PCB_LAYER_ID>::iterator it;

    int numAltiumMatches = 0; // Assume we won't find Altium Gerbers

    GERBER_FILE_IMAGE_LIST* images = m_Parent->GetGerberLayout()->GetImagesList();

    // If the passed vector isn't empty but is too small to hold the loaded
    // Gerbers, then bail because something isn't right.

    if( ( aGerber2KicadMapping.size() != 0 )
            && ( aGerber2KicadMapping.size() != (size_t) m_gerberActiveLayersCount ) )
        return numAltiumMatches;

    // If the passed vector is empty, set it to the same number of elements as there
    // are loaded Gerbers, and set each to "UNSELECTED_LAYER"

    if( aGerber2KicadMapping.size() == 0 )
        aGerber2KicadMapping.assign( m_gerberActiveLayersCount, UNSELECTED_LAYER );

    // Loop through all loaded Gerbers looking for any with Altium specific extensions
    for( int ii = 0; ii < m_gerberActiveLayersCount; ii++ )
    {
        if( images->GetGbrImage( ii ) )
        {
            // Get file name of Gerber loaded on this layer.
            wxFileName fn( images->GetGbrImage( ii )->m_FileName );

            // Get uppercase version of file extension
            wxString FileExt = fn.GetExt();
            FileExt.MakeUpper();

            // Check for matching Altium Gerber file extension we'll handle
            it = altiumExt.find( FileExt );

            if( it != altiumExt.end() )
            {
                // We got a match, so store the KiCad layer number.  We verify it's set to
                // "UNSELECTED_LAYER" in case the passed vector already had entries
                // matched to other known Gerber files.   This will preserve them.

                if( aGerber2KicadMapping[ii] == UNSELECTED_LAYER )
                {
                    aGerber2KicadMapping[ii] = it->second;
                    numAltiumMatches++;
                }
            }
        }
    }

    // Return number of Altium Gerbers we found.  Each index in the passed vector corresponds to
    // a loaded Gerber layer, and the entry will contain the index to the matching
    // KiCad layer for Altium Gerbers, or "UNSELECTED_LAYER" for the rest.
    return numAltiumMatches;
}

int LAYERS_MAP_DIALOG::findNumKiCadGerbersLoaded( std::vector<int>& aGerber2KicadMapping )
{
    // The next comment preserves initializer formatting below it
    // clang-format off
    // This map contains the known KiCad suffixes used for Gerbers that we care about,
    // along with their corresponding KiCad layer
    std::map<wxString, PCB_LAYER_ID> kicadLayers
    {
        { "-F_Cu",      F_Cu },
        { "-In1_Cu",    In1_Cu },
        { "-In2_Cu",    In2_Cu },
        { "-In3_Cu",    In3_Cu },
        { "-In4_Cu",    In4_Cu },
        { "-In5_Cu",    In5_Cu },
        { "-In6_Cu",    In6_Cu },
        { "-In7_Cu",    In7_Cu },
        { "-In8_Cu",    In8_Cu },
        { "-In9_Cu",    In9_Cu },
        { "-In10_Cu",   In10_Cu },
        { "-In11_Cu",   In11_Cu },
        { "-In12_Cu",   In12_Cu },
        { "-In13_Cu",   In13_Cu },
        { "-In14_Cu",   In14_Cu },
        { "-In15_Cu",   In15_Cu },
        { "-In16_Cu",   In16_Cu },
        { "-In17_Cu",   In17_Cu },
        { "-In18_Cu",   In18_Cu },
        { "-In19_Cu",   In19_Cu },
        { "-In20_Cu",   In20_Cu },
        { "-In21_Cu",   In21_Cu },
        { "-In22_Cu",   In22_Cu },
        { "-In23_Cu",   In23_Cu },
        { "-In24_Cu",   In24_Cu },
        { "-In25_Cu",   In25_Cu },
        { "-In26_Cu",   In26_Cu },
        { "-In27_Cu",   In27_Cu },
        { "-In28_Cu",   In28_Cu },
        { "-In29_Cu",   In29_Cu },
        { "-In30_Cu",   In30_Cu },
        { "-B_Cu",      B_Cu },
        { "-B_Adhes",   B_Adhes },
        { "-F_Adhes",   F_Adhes },
        { "-B_Paste",   B_Paste },
        { "-F_Paste",   F_Paste },
        { "-B_SilkS",   B_SilkS },
        { "-F_SilkS",   F_SilkS },
        { "-B_Mask",    B_Mask },
        { "-F_Mask",    F_Mask },
        { "-Dwgs_User", Dwgs_User },
        { "-Cmts_User", Cmts_User },
        { "-Eco1_User", Eco1_User },
        { "-Eco2_User", Eco2_User },
        { "-Edge_Cuts", Edge_Cuts }
    };
    // clang-format on

    std::map<wxString, PCB_LAYER_ID>::iterator it;

    int numKicadMatches = 0; // Assume we won't find KiCad Gerbers

    GERBER_FILE_IMAGE_LIST* images = m_Parent->GetGerberLayout()->GetImagesList();

    // If the passed vector isn't empty but is too small to hold the loaded
    // Gerbers, then bail because something isn't right.

    if( ( aGerber2KicadMapping.size() != 0 )
            && ( aGerber2KicadMapping.size() < (size_t) m_gerberActiveLayersCount ) )
        return numKicadMatches;

    // If the passed vector is empty, set it to the same number of elements as there
    // are loaded Gerbers, and set each to "UNSELECTED_LAYER"

    if( aGerber2KicadMapping.size() == 0 )
        aGerber2KicadMapping.assign( m_gerberActiveLayersCount, UNSELECTED_LAYER );

    // Loop through all loaded Gerbers looking for any with KiCad specific layer names
    for( int ii = 0; ii < m_gerberActiveLayersCount; ii++ )
    {
        if( images->GetGbrImage( ii ) )
        {
            // Get file name of Gerber loaded on this layer.
            wxFileName fn( images->GetGbrImage( ii )->m_FileName );

            wxString layerName = fn.GetName();

            // To create Gerber file names, KiCad appends a suffix consisting of a "-" and the
            // name of the layer to the project name.  We need to isolate the suffix if present
            // and see if it's a known KiCad layer name.  Start by looking for the last "-" in
            // the file name.
            int dashPos = layerName.Find( '-', true );

            // If one was found, isolate the suffix from the "-" to the end of the file name
            wxString suffix;

            if( dashPos != wxNOT_FOUND )
                suffix = layerName.Right( layerName.length() - dashPos );

            // Check if the string we've isolated matches any known KiCad layer names
            it = kicadLayers.find( suffix );

            if( it != kicadLayers.end() )
            {
                // We got a match, so store the KiCad layer number.  We verify it's set to
                // "UNSELECTED_LAYER" in case the passed vector already had entries
                // matched to other known Gerber files.  This will preserve them.

                if( aGerber2KicadMapping[ii] == UNSELECTED_LAYER )
                {
                    aGerber2KicadMapping[ii] = it->second;
                    numKicadMatches++;
                }
            }
        }
    }

    // Return number of KiCad Gerbers we found.  Each index in the passed vector corresponds to
    // a loaded Gerber layer, and the entry will contain the index to the matching
    // KiCad layer for KiCad Gerbers, or "UNSELECTED_LAYER" for the rest.
    return numKicadMatches;
}

int LAYERS_MAP_DIALOG::findNumX2GerbersLoaded( std::vector<int>& aGerber2KicadMapping )
{
    // The next comment preserves initializer formatting below it
    // clang-format off
    // This map contains the known KiCad X2 "File Function" values used for Gerbers that we
    // care about, along with their corresponding KiCad layer
    std::map<wxString, PCB_LAYER_ID> kicadLayers
    {
        { "Top",   F_Cu },
        { "L2",    In1_Cu },
        { "L2",    In1_Cu },
        { "L2",    In2_Cu },
        { "L3",    In3_Cu },
        { "L4",    In4_Cu },
        { "L5",    In5_Cu },
        { "L6",    In6_Cu },
        { "L7",    In7_Cu },
        { "L8",    In8_Cu },
        { "L9",    In9_Cu },
        { "L10",   In10_Cu },
        { "L11",   In11_Cu },
        { "L12",   In12_Cu },
        { "L13",   In13_Cu },
        { "L14",   In14_Cu },
        { "L15",   In15_Cu },
        { "L16",   In16_Cu },
        { "L17",   In17_Cu },
        { "L18",   In18_Cu },
        { "L19",   In19_Cu },
        { "L20",   In20_Cu },
        { "L21",   In21_Cu },
        { "L22",   In22_Cu },
        { "L23",   In23_Cu },
        { "L24",   In24_Cu },
        { "L25",   In25_Cu },
        { "L26",   In26_Cu },
        { "L27",   In27_Cu },
        { "L28",   In28_Cu },
        { "L29",   In29_Cu },
        { "L30",   In30_Cu },
        { "Bot",         B_Cu },
        { "BotGlue",     B_Adhes },
        { "TopGlue",     F_Adhes },
        { "BotPaste",    B_Paste },
        { "TopPaste",    F_Paste },
        { "BotLegend",   B_SilkS },
        { "TopLegend",   F_SilkS },
        { "BotSoldermask",      B_Mask },
        { "TopSoldermask",      F_Mask },
        { "FabricationDrawing", Dwgs_User },
        { "OtherDrawing",       Cmts_User },
        { "TopAssemblyDrawing", Eco1_User },
        { "BotAssemblyDrawing", Eco2_User },
        { "PProfile",           Edge_Cuts }, // Plated PCB outline
        { "NPProfile",          Edge_Cuts }  // Non-plated PCB outline
    };
    // clang-format on

    std::map<wxString, PCB_LAYER_ID>::iterator it;

    int numKicadMatches = 0; // Assume we won't find KiCad Gerbers

    wxString mapThis;

    GERBER_FILE_IMAGE_LIST* images = m_Parent->GetGerberLayout()->GetImagesList();

    // If the passed vector isn't empty but is too small to hold the loaded
    // Gerbers, then bail because something isn't right.

    if( ( aGerber2KicadMapping.size() != 0 )
            && ( aGerber2KicadMapping.size() < (size_t) m_gerberActiveLayersCount ) )
        return numKicadMatches;

    // If the passed vector is empty, set it to the same number of elements as there
    // are loaded Gerbers, and set each to "UNSELECTED_LAYER"

    if( aGerber2KicadMapping.size() == 0 )
        aGerber2KicadMapping.assign( m_gerberActiveLayersCount, UNSELECTED_LAYER );

    // Loop through all loaded Gerbers looking for any with X2 File Functions
    for( int ii = 0; ii < m_gerberActiveLayersCount; ii++ )
    {
        if( images->GetGbrImage( ii ) )
        {
            X2_ATTRIBUTE_FILEFUNCTION* x2 = images->GetGbrImage( ii )->m_FileFunction;

            mapThis = "";

            if( images->GetGbrImage( ii )->m_IsX2_file )
            {
                if( x2->IsCopper() )
                {
                    // This is a copper layer, so figure out which one
                    mapThis = x2->GetBrdLayerSide(); // Returns "Top", "Bot" or "Inr"

                    // To map inner layers properly, we need the layer number
                    if( mapThis.IsSameAs( wxT( "Inr" ), false ) )
                        mapThis = x2->GetBrdLayerId(); // Returns "L2", "L5", etc
                }
                else
                {
                    // Create strings like "TopSolderMask" or "BotPaste" for non-copper layers
                    mapThis << x2->GetBrdLayerId() << x2->GetFileType();
                }


                // Check if the string we've isolated matches any known X2 layer names
                it = kicadLayers.find( mapThis );

                if( it != kicadLayers.end() )
                {
                    // We got a match, so store the KiCad layer number.  We verify it's set to
                    // "UNSELECTED_LAYER" in case the passed vector already had entries
                    // matched to other known Gerber files.   This will preserve them.

                    if( aGerber2KicadMapping[ii] == UNSELECTED_LAYER )
                    {
                        aGerber2KicadMapping[ii] = it->second;
                        numKicadMatches++;
                    }
                }
            }
        }
    }

    return numKicadMatches;
}
