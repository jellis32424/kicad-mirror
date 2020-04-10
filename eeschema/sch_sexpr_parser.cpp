/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2020 CERN
 *
 * @author Wayne Stambaugh <stambaughw@gmail.com>
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

/**
 * @file sch_sexpr_parser.cpp
 * @brief Schematic and symbol library s-expression file format parser implementations.
 */

// For some reason wxWidgets is built with wxUSE_BASE64 unset so expose the wxWidgets
// base64 code.
#define wxUSE_BASE64 1
#include <wx/base64.h>
#include <wx/mstream.h>
#include <wx/tokenzr.h>

#include <common.h>
#include <lib_id.h>
#include <plotter.h>

#include <class_libentry.h>
#include <lib_arc.h>
#include <lib_bezier.h>
#include <lib_circle.h>
#include <lib_pin.h>
#include <lib_polyline.h>
#include <lib_rectangle.h>
#include <lib_text.h>
#include <sch_bitmap.h>
#include <sch_bus_entry.h>
#include <sch_component.h>
#include <sch_edit_frame.h>          // CMP_ORIENT_XXX
#include <sch_field.h>
#include <sch_file_versions.h>
#include <sch_line.h>
#include <sch_junction.h>
#include <sch_no_connect.h>
#include <sch_screen.h>
#include <sch_sexpr_parser.h>
#include <template_fieldnames.h>


using namespace TSCHEMATIC_T;


SCH_SEXPR_PARSER::SCH_SEXPR_PARSER( LINE_READER* aLineReader ) :
    SCHEMATIC_LEXER( aLineReader ),
    m_requiredVersion( 0 ),
    m_fieldId( 0 ),
    m_unit( 1 ),
    m_convert( 1 )
{
}


bool SCH_SEXPR_PARSER::IsTooRecent() const
{
    return m_requiredVersion && m_requiredVersion > SEXPR_SYMBOL_LIB_FILE_VERSION;
}


void SCH_SEXPR_PARSER::ParseLib( LIB_PART_MAP& aSymbolLibMap )
{
    T token;

    NeedLEFT();
    NextTok();
    parseHeader( T_kicad_symbol_lib, SEXPR_SYMBOL_LIB_FILE_VERSION );

    for( token = NextTok();  token != T_RIGHT;  token = NextTok() )
    {
        if( token != T_LEFT )
            Expecting( T_LEFT );

        token = NextTok();

        if( token == T_symbol )
        {
            m_unit = 1;
            m_convert = 1;
            LIB_PART* symbol = ParseSymbol( aSymbolLibMap );
            aSymbolLibMap[symbol->GetName()] = symbol;
        }
        else
        {
            Expecting( "symbol" );
        }
    }
}


LIB_PART* SCH_SEXPR_PARSER::ParseSymbol( LIB_PART_MAP& aSymbolLibMap )
{
    wxCHECK_MSG( CurTok() == T_symbol, nullptr,
                 wxT( "Cannot parse " ) + GetTokenString( CurTok() ) + wxT( " as a symbol." ) );

    T token;
    long tmp;
    wxString error;
    wxString name;
    LIB_ITEM* item;
    std::unique_ptr<LIB_PART> symbol( new LIB_PART( wxEmptyString ) );

    symbol->SetUnitCount( 1 );

    m_fieldId = MANDATORY_FIELDS;

    token = NextTok();

    if( !IsSymbol( token ) )
    {
        error.Printf( _( "Invalid symbol name in\nfile: \"%s\"\nline: %d\noffset: %d" ),
                      CurSource().c_str(), CurLineNumber(), CurOffset() );
        THROW_IO_ERROR( error );
    }

    name = FromUTF8();

    if( name.IsEmpty() )
    {
        error.Printf( _( "Empty symbol name in\nfile: \"%s\"\nline: %d\noffset: %d" ),
                      CurSource().c_str(), CurLineNumber(), CurOffset() );
        THROW_IO_ERROR( error );
    }

    m_symbolName = name;
    symbol->SetName( name );

    for( token = NextTok();  token != T_RIGHT;  token = NextTok() )
    {
        if( token != T_LEFT )
            Expecting( T_LEFT );

        token = NextTok();

        switch( token )
        {
        case T_pin_names:
            parsePinNames( symbol );
            break;

        case T_pin_numbers:
            token = NextTok();

            if( token != T_hide )
                Expecting( "hide" );

            symbol->SetShowPinNumbers( false );
            NeedRIGHT();
            break;

        case T_property:
            parseProperty( symbol );
            break;

        case T_extends:
        {
            token = NextTok();

            if( !IsSymbol( token ) )
            {
                error.Printf(
                    _( "Invalid symbol extends name in\nfile: \"%s\"\nline: %d\noffset: %d" ),
                    CurSource().c_str(), CurLineNumber(), CurOffset() );
                THROW_IO_ERROR( error );
            }

            name = FromUTF8();
            auto it = aSymbolLibMap.find( name );

            if( it == aSymbolLibMap.end() )
            {
                error.Printf(
                    _( "No parent for extended symbol %s in\nfile: \"%s\"\nline: %d\noffset: %d" ),
                    name.c_str(), CurSource().c_str(), CurLineNumber(), CurOffset() );
                THROW_IO_ERROR( error );
            }

            symbol->SetParent( it->second );
            NeedRIGHT();
            break;
        }

        case T_symbol:
        {
            token = NextTok();

            if( !IsSymbol( token ) )
            {
                error.Printf(
                    _( "Invalid symbol unit name in\nfile: \"%s\"\nline: %d\noffset: %d" ),
                    CurSource().c_str(), CurLineNumber(), CurOffset() );
                THROW_IO_ERROR( error );
            }

            name = FromUTF8();

            if( !name.StartsWith( m_symbolName ) )
            {
                error.Printf(
                    _( "Invalid symbol unit name prefix %s in\nfile: \"%s\"\nline: %d\noffset: %d" ),
                    name.c_str(), CurSource().c_str(), CurLineNumber(), CurOffset() );
                THROW_IO_ERROR( error );
            }

            name = name.Right( name.Length() - m_symbolName.Length() - 1 );

            wxStringTokenizer tokenizer( name, "_" );

            if( tokenizer.CountTokens() != 2 )
            {
                error.Printf(
                    _( "Invalid symbol unit name suffix %s in\nfile: \"%s\"\nline: %d\noffset: %d" ),
                    name.c_str(), CurSource().c_str(), CurLineNumber(), CurOffset() );
                THROW_IO_ERROR( error );
            }

            if( !tokenizer.GetNextToken().ToLong( &tmp ) )
            {
                error.Printf(
                    _( "Invalid symbol unit number %s in\nfile: \"%s\"\nline: %d\noffset: %d" ),
                    name.c_str(), CurSource().c_str(), CurLineNumber(), CurOffset() );
                THROW_IO_ERROR( error );
            }

            m_unit = static_cast<int>( tmp );

            if( !tokenizer.GetNextToken().ToLong( &tmp ) )
            {
                error.Printf(
                    _( "Invalid symbol convert number %s in\nfile: \"%s\"\nline: %d\noffset: %d" ),
                    name.c_str(), CurSource().c_str(), CurLineNumber(), CurOffset() );
                THROW_IO_ERROR( error );
            }

            m_convert = static_cast<int>( tmp );

            if( m_convert > 1 )
                symbol->SetConversion( true, false );

            if( m_unit > symbol->GetUnitCount() )
                symbol->SetUnitCount( m_unit, false );

            for( token = NextTok();  token != T_RIGHT;  token = NextTok() )
            {
                if( token != T_LEFT )
                    Expecting( T_LEFT );

                token = NextTok();

                switch( token )
                {
                case T_arc:
                case T_bezier:
                case T_circle:
                case T_pin:
                case T_polyline:
                case T_rectangle:
                case T_text:
                    item = ParseDrawItem();

                    wxCHECK_MSG( item, nullptr, "Invalid draw item pointer." );

                    item->SetParent( symbol.get() );
                    symbol->AddDrawItem( item );
                    break;

                default:
                    Expecting( "arc, bezier, circle, pin, polyline, rectangle, or text" );
                };
            }

            m_unit = 1;
            m_convert = 1;
            break;
        }

        case T_arc:
        case T_bezier:
        case T_circle:
        case T_pin:
        case T_polyline:
        case T_rectangle:
        case T_text:
            item = ParseDrawItem();

            wxCHECK_MSG( item, nullptr, "Invalid draw item pointer." );

            item->SetParent( symbol.get() );
            symbol->AddDrawItem( item );
            break;

        default:
            Expecting( "pin_names, pin_numbers, arc, bezier, circle, pin, polyline, "
                       "rectangle, or text" );
        }
    }

    m_symbolName.clear();

    return symbol.release();
}


LIB_ITEM* SCH_SEXPR_PARSER::ParseDrawItem()
{
    switch( CurTok() )
    {
    case T_arc:
        return static_cast<LIB_ITEM*>( parseArc() );
        break;

    case T_bezier:
        return static_cast<LIB_ITEM*>( parseBezier() );
        break;

    case T_circle:
        return static_cast<LIB_ITEM*>( parseCircle() );
        break;

    case T_pin:
        return static_cast<LIB_ITEM*>( parsePin() );
        break;

    case T_polyline:
        return static_cast<LIB_ITEM*>( parsePolyLine() );
        break;

    case T_rectangle:
        return static_cast<LIB_ITEM*>( parseRectangle() );
        break;

    case T_text:
        return static_cast<LIB_TEXT*>( parseText() );
        break;

    default:
        Expecting( "arc, bezier, circle, pin, polyline, rectangle, or text" );
    }

    return nullptr;
}


double SCH_SEXPR_PARSER::parseDouble()
{
    char* tmp;

    errno = 0;

    double fval = strtod( CurText(), &tmp );

    if( errno )
    {
        wxString error;
        error.Printf( _( "Invalid floating point number in\nfile: \"%s\"\nline: %d\noffset: %d" ),
                      CurSource().c_str(), CurLineNumber(), CurOffset() );

        THROW_IO_ERROR( error );
    }

    if( CurText() == tmp )
    {
        wxString error;
        error.Printf( _( "Missing floating point number in\nfile: \"%s\"\nline: %d\noffset: %d" ),
                      CurSource().c_str(), CurLineNumber(), CurOffset() );

        THROW_IO_ERROR( error );
    }

    return fval;
}


void SCH_SEXPR_PARSER::parseStroke( STROKE_PARAMS& aStroke )
{
    wxCHECK_RET( CurTok() == T_stroke,
                 wxT( "Cannot parse " ) + GetTokenString( CurTok() ) + wxT( " as a stroke." ) );

    aStroke.m_Width = Mils2iu( DEFAULT_LINE_WIDTH );
    aStroke.m_Type = PLOT_DASH_TYPE::DEFAULT;
    aStroke.m_Color = COLOR4D::UNSPECIFIED;

    T token;

    for( token = NextTok();  token != T_RIGHT;  token = NextTok() )
    {
        if( token != T_LEFT )
            Expecting( T_LEFT );

        token = NextTok();

        switch( token )
        {
        case T_width:
            aStroke.m_Width = parseInternalUnits( "stroke width" );
            NeedRIGHT();
            break;

        case T_type:
        {
            token = NextTok();

            switch( token )
            {
            case T_dash:      aStroke.m_Type = PLOT_DASH_TYPE::DASH;      break;
            case T_dot:       aStroke.m_Type = PLOT_DASH_TYPE::DOT;       break;
            case T_dash_dot:  aStroke.m_Type = PLOT_DASH_TYPE::DASHDOT;   break;
            case T_solid:     aStroke.m_Type = PLOT_DASH_TYPE::SOLID;     break;
            default:
                Expecting( "solid, dash, dash_dot, or dot" );
            }

            NeedRIGHT();
            break;
        }

        case T_color:
            aStroke.m_Color =
                    COLOR4D( parseInt( "red" ) / 255.0,
                             parseInt( "green" ) / 255.0,
                             parseInt( "blue" ) / 255.0,
                             parseDouble( "alpha" ) );
            NeedRIGHT();
            break;

        default:
            Expecting( "width, type, or color" );
        }

    }
}


void SCH_SEXPR_PARSER::parseFill( FILL_PARAMS& aFill )
{
    wxCHECK_RET( CurTok() == T_fill,
                 wxT( "Cannot parse " ) + GetTokenString( CurTok() ) + wxT( " as fill." ) );

    aFill.m_FillType = NO_FILL;
    aFill.m_Color = COLOR4D::UNSPECIFIED;

    T token;

    for( token = NextTok();  token != T_RIGHT;  token = NextTok() )
    {
        if( token != T_LEFT )
            Expecting( T_LEFT );

        token = NextTok();

        switch( token )
        {
        case T_type:
        {
            token = NextTok();

            switch( token )
            {
            case T_none:       aFill.m_FillType = NO_FILL;                  break;
            case T_outline:    aFill.m_FillType = FILLED_SHAPE;             break;
            case T_background: aFill.m_FillType = FILLED_WITH_BG_BODYCOLOR; break;
            default:
                Expecting( "none, outline, or background" );
            }

            NeedRIGHT();
            break;
        }

        case T_color:
            aFill.m_Color =
                    COLOR4D( parseInt( "red" ) / 255.0,
                             parseInt( "green" ) / 255.0,
                             parseInt( "blue" ) / 255.0,
                             parseDouble( "alpha" ) );

            NeedRIGHT();
            break;

        default:
            Expecting( "type or color" );
        }
    }
}


void SCH_SEXPR_PARSER::parseEDA_TEXT( EDA_TEXT* aText )
{
    wxCHECK_RET( aText && CurTok() == T_effects,
                 wxT( "Cannot parse " ) + GetTokenString( CurTok() ) + wxT( " as EDA_TEXT." ) );

    T token;

    for( token = NextTok();  token != T_RIGHT;  token = NextTok() )
    {
        if( token == T_LEFT )
            token = NextTok();

        switch( token )
        {
        case T_font:
            for( token = NextTok();  token != T_RIGHT;  token = NextTok() )
            {
                if( token == T_LEFT )
                    token = NextTok();

                switch( token )
                {
                case T_size:
                {
                    wxSize sz;
                    sz.SetHeight( parseInternalUnits( "text height" ) );
                    sz.SetWidth( parseInternalUnits( "text width" ) );
                    aText->SetTextSize( sz );
                    NeedRIGHT();
                    break;
                }

                case T_thickness:
                    aText->SetThickness( parseInternalUnits( "text thickness" ) );
                    NeedRIGHT();
                    break;

                case T_bold:
                    aText->SetBold( true );
                    break;

                case T_italic:
                    aText->SetItalic( true );
                    break;

                default:
                    Expecting( "size, bold, or italic" );
                }
            }

            break;

        case T_justify:
            for( token = NextTok();  token != T_RIGHT;  token = NextTok() )
            {
                switch( token )
                {
                case T_left:
                    aText->SetHorizJustify( GR_TEXT_HJUSTIFY_LEFT );
                    break;

                case T_right:
                    aText->SetHorizJustify( GR_TEXT_HJUSTIFY_RIGHT );
                    break;

                case T_top:
                    aText->SetVertJustify( GR_TEXT_VJUSTIFY_TOP );
                    break;

                case T_bottom:
                    aText->SetVertJustify( GR_TEXT_VJUSTIFY_BOTTOM );
                    break;

                case T_mirror:
                    aText->SetMirrored( true );
                    break;

                default:
                    Expecting( "left, right, top, bottom, or mirror" );
                }
            }

            break;

        case T_hide:
            aText->SetVisible( false );
            break;

        default:
            Expecting( "font, justify, or hide" );
        }
    }
}


void SCH_SEXPR_PARSER::parseHeader( TSCHEMATIC_T::T aHeaderType, int aFileVersion )
{
    wxCHECK_RET( CurTok() == aHeaderType,
                 wxT( "Cannot parse " ) + GetTokenString( CurTok() ) + wxT( " as a header." ) );

    NeedLEFT();

    T tok = NextTok();

    if( tok == T_version )
    {
        m_requiredVersion = parseInt( FromUTF8().mb_str( wxConvUTF8 ) );
        NeedRIGHT();

        // Skip the host name and host build version information.
        NeedLEFT();
        NeedSYMBOL();
        NeedSYMBOL();
        NeedSYMBOL();
        NeedRIGHT();
    }
    else
    {
        m_requiredVersion = aFileVersion;

        // Skip the host name and host build version information.
        NeedSYMBOL();
        NeedSYMBOL();
        NeedRIGHT();
    }
}


void SCH_SEXPR_PARSER::parsePinNames( std::unique_ptr<LIB_PART>& aSymbol )
{
    wxCHECK_RET( CurTok() == T_pin_names,
                 wxT( "Cannot parse " ) + GetTokenString( CurTok() ) +
                 wxT( " as a pin_name token." ) );

    wxString error;

    T token = NextTok();

    if( token == T_LEFT )
    {
        token = NextTok();

        if( token != T_offset )
            Expecting( "offset" );

        aSymbol->SetPinNameOffset( parseInternalUnits( "pin name offset" ) );
        NeedRIGHT();
        token = NextTok();  // Either ) or hide
    }

    if( token == T_hide )
    {
        aSymbol->SetShowPinNames( false );
        NeedRIGHT();
    }
    else if( token != T_RIGHT )
    {
        error.Printf(
            _( "Invalid symbol names definition in\nfile: \"%s\"\nline: %d\noffset: %d" ),
            CurSource().c_str(), CurLineNumber(), CurOffset() );
        THROW_IO_ERROR( error );
    }
}


void SCH_SEXPR_PARSER::parseProperty( std::unique_ptr<LIB_PART>& aSymbol )
{
    wxCHECK_RET( CurTok() == T_property,
                 wxT( "Cannot parse " ) + GetTokenString( CurTok() ) +
                 wxT( " as a property token." ) );

    wxString error;
    wxString name;
    wxString value;
    std::unique_ptr<LIB_FIELD> field( new LIB_FIELD( MANDATORY_FIELDS ) );

    T token = NextTok();

    if( !IsSymbol( token ) )
    {
        error.Printf( _( "Invalid property name in\nfile: \"%s\"\nline: %d\noffset: %d" ),
                      CurSource().c_str(), CurLineNumber(), CurOffset() );
        THROW_IO_ERROR( error );
    }

    name = FromUTF8();

    if( name.IsEmpty() )
    {
        error.Printf( _( "Empty property name in\nfile: \"%s\"\nline: %d\noffset: %d" ),
                      CurSource().c_str(), CurLineNumber(), CurOffset() );
        THROW_IO_ERROR( error );
    }

    field->SetName( name );
    token = NextTok();

    if( !IsSymbol( token ) )
    {
        error.Printf( _( "Invalid property value in\nfile: \"%s\"\nline: %d\noffset: %d" ),
                      CurSource().c_str(), CurLineNumber(), CurOffset() );
        THROW_IO_ERROR( error );
    }

    // Empty property values are valid.
    value = FromUTF8();

    field->SetText( value );

    for( token = NextTok();  token != T_RIGHT;  token = NextTok() )
    {
        if( token != T_LEFT )
            Expecting( T_LEFT );

        token = NextTok();

        switch( token )
        {
        case T_id:
            field->SetId( parseInt( "field ID" ) );
            NeedRIGHT();
            break;

        case T_at:
            field->SetPosition( parseXY() );
            field->SetTextAngle( static_cast<int>( parseDouble( "text angle" ) * 10.0 ) );
            NeedRIGHT();
            break;

        case T_effects:
            parseEDA_TEXT( static_cast<EDA_TEXT*>( field.get() ) );
            break;

        default:
            Expecting( "id, at or effects" );
        }
    }

    LIB_FIELD* existingField;

    if( field->GetId() < MANDATORY_FIELDS )
    {
        existingField = aSymbol->GetField( field->GetId() );

        /// @todo Remove this once the legacy file format is deprecated.
        if( field->GetId() == DATASHEET )
        {
            aSymbol->SetDocFileName( value );
            field->SetText( wxEmptyString );
        }

        *existingField = *field;
    }
    else if( name == "ki_keywords" )
    {
        // Not a LIB_FIELD object yet.
        aSymbol->SetKeyWords( value );
    }
    else if( name == "ki_description" )
    {
        // Not a LIB_FIELD object yet.
        aSymbol->SetDescription( value );
    }
    else if( name == "ki_fp_filters" )
    {
        // Not a LIB_FIELD object yet.
        wxArrayString filters;
        wxStringTokenizer tokenizer( value );

        while( tokenizer.HasMoreTokens() )
            filters.Add( tokenizer.GetNextToken() );

        aSymbol->SetFootprintFilters( filters );
    }
    else if( name == "ki_locked" )
    {
        // This is a temporary LIB_FIELD object until interchangeable units are determined on
        // the fly.
        aSymbol->LockUnits( true );
    }
    else
    {
        existingField = aSymbol->GetField( field->GetId() );

        if( !existingField )
        {
            aSymbol->AddDrawItem( field.release() );
        }
        else
        {
            *existingField = *field;
        }
    }
}


LIB_ARC* SCH_SEXPR_PARSER::parseArc()
{
    wxCHECK_MSG( CurTok() == T_arc, nullptr,
                 wxT( "Cannot parse " ) + GetTokenString( CurTok() ) + wxT( " as an arc token." ) );

    T token;
    wxPoint startPoint;
    wxPoint midPoint;
    wxPoint endPoint;
    wxPoint pos;
    FILL_PARAMS fill;
    bool hasMidPoint = false;
    std::unique_ptr<LIB_ARC> arc( new LIB_ARC( nullptr ) );

    arc->SetUnit( m_unit );
    arc->SetConvert( m_convert );

    for( token = NextTok();  token != T_RIGHT;  token = NextTok() )
    {
        if( token != T_LEFT )
            Expecting( T_LEFT );

        token = NextTok();

        switch( token )
        {
        case T_start:
            startPoint = parseXY();
            NeedRIGHT();
            break;

        case T_mid:
            midPoint = parseXY();
            NeedRIGHT();
            hasMidPoint = true;
            break;

        case T_end:
            endPoint = parseXY();
            NeedRIGHT();
            break;

        case T_radius:
            for( token = NextTok();  token != T_RIGHT;  token = NextTok() )
            {
                if( token != T_LEFT )
                    Expecting( T_LEFT );

                token = NextTok();

                switch( token )
                {
                case T_at:
                    pos = parseXY();
                    NeedRIGHT();
                    break;

                case T_length:
                    arc->SetRadius( parseInternalUnits( "radius length" ) );
                    NeedRIGHT();
                    break;

                case T_angles:
                {
                    int angle1 = KiROUND( parseDouble( "start radius angle" ) * 10.0 );
                    int angle2 = KiROUND( parseDouble( "end radius angle" ) * 10.0 );

                    NORMALIZE_ANGLE_POS( angle1 );
                    NORMALIZE_ANGLE_POS( angle2 );
                    arc->SetFirstRadiusAngle( angle1 );
                    arc->SetSecondRadiusAngle( angle2 );
                    NeedRIGHT();
                    break;
                }

                default:
                    Expecting( "at, length, or angle" );
                }
            }

            break;

        case T_stroke:
            NeedLEFT();
            token = NextTok();

            if( token != T_width )
                Expecting( "width" );

            arc->SetWidth( parseInternalUnits( "stroke width" ) );
            NeedRIGHT();   // Closes width token;
            NeedRIGHT();   // Closes stroke token;
            break;

        case T_fill:
            parseFill( fill );
            arc->SetFillMode( fill.m_FillType );
            break;

        default:
            Expecting( "start, end, radius, stroke, or fill" );
        }
    }

    arc->SetPosition( pos );
    arc->SetStart( startPoint );
    arc->SetEnd( endPoint );

    if( hasMidPoint )
    {
        VECTOR2I center = GetArcCenter( arc->GetStart(), midPoint, arc->GetEnd() );

        arc->SetPosition( wxPoint( center.x, center.y ) );

        // @todo Calculate the radius.

        arc->CalcRadiusAngles();
    }

    return arc.release();
}


LIB_BEZIER* SCH_SEXPR_PARSER::parseBezier()
{
    wxCHECK_MSG( CurTok() == T_bezier, nullptr,
                 wxT( "Cannot parse " ) + GetTokenString( CurTok() ) + wxT( " as a bezier." ) );

    T token;
    FILL_PARAMS fill;
    std::unique_ptr<LIB_BEZIER> bezier( new LIB_BEZIER( nullptr ) );

    bezier->SetUnit( m_unit );
    bezier->SetConvert( m_convert );

    for( token = NextTok();  token != T_RIGHT;  token = NextTok() )
    {
        if( token != T_LEFT )
            Expecting( T_LEFT );

        token = NextTok();

        switch( token )
        {
        case T_pts:
            for( token = NextTok();  token != T_RIGHT;  token = NextTok() )
            {
                if( token != T_LEFT )
                    Expecting( T_LEFT );

                token = NextTok();

                if( token != T_xy )
                    Expecting( "xy" );

                bezier->AddPoint( parseXY() );

                NeedRIGHT();
            }

            break;

        case T_stroke:
            NeedLEFT();
            token = NextTok();

            if( token != T_width )
                Expecting( "width" );

            bezier->SetWidth( parseInternalUnits( "stroke width" ) );
            NeedRIGHT();   // Closes width token;
            NeedRIGHT();   // Closes stroke token;
            break;

        case T_fill:
            parseFill( fill );
            bezier->SetFillMode( fill.m_FillType );
            break;

        default:
            Expecting( "pts, stroke, or fill" );
        }
    }

    return bezier.release();
}


LIB_CIRCLE* SCH_SEXPR_PARSER::parseCircle()
{
    wxCHECK_MSG( CurTok() == T_circle, nullptr,
                 wxT( "Cannot parse " ) + GetTokenString( CurTok() ) + wxT( " as a circle token." ) );

    T token;
    FILL_PARAMS fill;
    std::unique_ptr<LIB_CIRCLE> circle( new LIB_CIRCLE( nullptr ) );

    circle->SetUnit( m_unit );
    circle->SetConvert( m_convert );

    for( token = NextTok();  token != T_RIGHT;  token = NextTok() )
    {
        if( token != T_LEFT )
            Expecting( T_LEFT );

        token = NextTok();

        switch( token )
        {
        case T_center:
            circle->SetPosition( parseXY() );
            NeedRIGHT();
            break;

        case T_radius:
            circle->SetRadius( parseInternalUnits( "radius length" ) );
            NeedRIGHT();
            break;

        case T_stroke:
            NeedLEFT();
            token = NextTok();

            if( token != T_width )
                Expecting( "width" );

            circle->SetWidth( parseInternalUnits( "stroke width" ) );
            NeedRIGHT();   // Closes width token;
            NeedRIGHT();   // Closes stroke token;
            break;

        case T_fill:
            parseFill( fill );
            circle->SetFillMode( fill.m_FillType );
            break;

        default:
            Expecting( "start, end, radius, stroke, or fill" );
        }
    }

    return circle.release();
}


LIB_PIN* SCH_SEXPR_PARSER::parsePin()
{
    wxCHECK_MSG( CurTok() == T_pin, nullptr,
                 wxT( "Cannot parse " ) + GetTokenString( CurTok() ) + wxT( " as a pin token." ) );

    T token;
    wxString tmp;
    wxString error;
    std::unique_ptr<LIB_PIN> pin( new LIB_PIN( nullptr ) );

    pin->SetUnit( m_unit );
    pin->SetConvert( m_convert );

    // Pin electrical type.
    token = NextTok();

    switch( token )
    {
    case T_input:
        pin->SetType( ELECTRICAL_PINTYPE::PT_INPUT );
        break;

    case T_output:
        pin->SetType( ELECTRICAL_PINTYPE::PT_OUTPUT );
        break;

    case T_bidirectional:
        pin->SetType( ELECTRICAL_PINTYPE::PT_BIDI );
        break;

    case T_tri_state:
        pin->SetType( ELECTRICAL_PINTYPE::PT_TRISTATE );
        break;

    case T_passive:
        pin->SetType( ELECTRICAL_PINTYPE::PT_PASSIVE );
        break;

    case T_unspecified:
        pin->SetType( ELECTRICAL_PINTYPE::PT_UNSPECIFIED );
        break;

    case T_power_in:
        pin->SetType( ELECTRICAL_PINTYPE::PT_POWER_IN );
        break;

    case T_power_out:
        pin->SetType( ELECTRICAL_PINTYPE::PT_POWER_OUT );
        break;

    case T_open_collector:
        pin->SetType( ELECTRICAL_PINTYPE::PT_OPENCOLLECTOR );
        break;

    case T_open_emitter:
        pin->SetType( ELECTRICAL_PINTYPE::PT_OPENEMITTER );
        break;

    case T_unconnected:
        pin->SetType( ELECTRICAL_PINTYPE::PT_NC );
        break;

    default:
        Expecting( "input, output, bidirectional, tri_state, passive, unspecified, "
                   "power_in, power_out, open_collector, open_emitter, or unconnected" );
    }

    // Pin shape.
    token = NextTok();

    switch( token )
    {
    case T_line:
        pin->SetShape( GRAPHIC_PINSHAPE::LINE );
        break;

    case T_inverted:
        pin->SetShape( GRAPHIC_PINSHAPE::INVERTED );
        break;

    case T_clock:
        pin->SetShape( GRAPHIC_PINSHAPE::CLOCK );
        break;

    case T_inverted_clock:
        pin->SetShape( GRAPHIC_PINSHAPE::INVERTED_CLOCK );
        break;

    case T_input_low:
        pin->SetShape( GRAPHIC_PINSHAPE::INPUT_LOW );
        break;

    case T_clock_low:
        pin->SetShape( GRAPHIC_PINSHAPE::CLOCK_LOW );
        break;

    case T_output_low:
        pin->SetShape( GRAPHIC_PINSHAPE::OUTPUT_LOW );
        break;

    case T_edge_clock_high:
        pin->SetShape( GRAPHIC_PINSHAPE::FALLING_EDGE_CLOCK );
        break;

    case T_non_logic:
        pin->SetShape( GRAPHIC_PINSHAPE::NONLOGIC );
        break;

    default:
        Expecting( "line, inverted, clock, inverted_clock, input_low, clock_low, "
                   "output_low, edge_clock_high, non_logic" );
    }

    for( token = NextTok();  token != T_RIGHT;  token = NextTok() )
    {
        if( token == T_hide )
        {
            pin->SetVisible( false );
            continue;
        }

        if( token != T_LEFT )
            Expecting( T_LEFT );

        token = NextTok();

        switch( token )
        {
        case T_at:
            pin->SetPosition( parseXY() );

            switch( parseInt( "pin orientation" ) )
            {
            case 0:
                pin->SetOrientation( PIN_RIGHT );
                break;

            case 90:
                pin->SetOrientation( PIN_UP );
                break;

            case 180:
                pin->SetOrientation( PIN_LEFT );
                break;

            case 270:
                pin->SetOrientation( PIN_DOWN );
                break;

            default:
                Expecting( "0, 90, 180, or 270" );
            }

            NeedRIGHT();
            break;

        case T_length:
            pin->SetLength( parseInternalUnits( "pin length" ) );
            NeedRIGHT();
            break;

        case T_name:
            token = NextTok();

            if( !IsSymbol( token ) )
            {
                error.Printf( _( "Invalid pin name in\nfile: \"%s\"\nline: %d\noffset: %d" ),
                              CurSource().c_str(), CurLineNumber(), CurOffset() );
                THROW_IO_ERROR( error );
            }

            pin->SetName( FromUTF8() );
            token = NextTok();

            if( token != T_RIGHT )
            {
                token = NextTok();

                if( token == T_effects )
                {
                    // The EDA_TEXT font effects formatting is used so use and EDA_TEXT object
                    // so duplicate parsing is not required.
                    EDA_TEXT text;

                    parseEDA_TEXT( &text );
                    pin->SetNameTextSize( text.GetTextHeight() );
                    NeedRIGHT();
                }
                else
                {
                    Expecting( "effects" );
                }
            }

            break;

        case T_number:
            token = NextTok();

            if( !IsSymbol( token ) )
            {
                error.Printf( _( "Invalid pin number in\nfile: \"%s\"\nline: %d\noffset: %d" ),
                              CurSource().c_str(), CurLineNumber(), CurOffset() );
                THROW_IO_ERROR( error );
            }

            pin->SetNumber( FromUTF8() );
            token = NextTok();

            if( token != T_RIGHT )
            {
                token = NextTok();

                if( token == T_effects )
                {
                    // The EDA_TEXT font effects formatting is used so use and EDA_TEXT object
                    // so duplicate parsing is not required.
                    EDA_TEXT text;

                    parseEDA_TEXT( &text );
                    pin->SetNumberTextSize( text.GetTextHeight(), false );
                    NeedRIGHT();
                }
                else
                {
                    Expecting( "effects" );
                }
            }

            break;

        default:
            Expecting( "at, name, number, or length" );
        }
    }

    return pin.release();
}


LIB_POLYLINE* SCH_SEXPR_PARSER::parsePolyLine()
{
    wxCHECK_MSG( CurTok() == T_polyline, nullptr,
                 wxT( "Cannot parse " ) + GetTokenString( CurTok() ) + wxT( " as a polyline." ) );

    T token;
    FILL_PARAMS fill;
    std::unique_ptr<LIB_POLYLINE> polyLine( new LIB_POLYLINE( nullptr ) );

    polyLine->SetUnit( m_unit );
    polyLine->SetConvert( m_convert );

    for( token = NextTok();  token != T_RIGHT;  token = NextTok() )
    {
        if( token != T_LEFT )
            Expecting( T_LEFT );

        token = NextTok();

        switch( token )
        {
        case T_pts:
            for( token = NextTok();  token != T_RIGHT;  token = NextTok() )
            {
                if( token != T_LEFT )
                    Expecting( T_LEFT );

                token = NextTok();

                if( token != T_xy )
                    Expecting( "xy" );

                polyLine->AddPoint( parseXY() );

                NeedRIGHT();
            }

            break;

        case T_stroke:
            NeedLEFT();
            token = NextTok();

            if( token != T_width )
                Expecting( "width" );

            polyLine->SetWidth( parseInternalUnits( "stroke width" ) );
            NeedRIGHT();   // Closes width token;
            NeedRIGHT();   // Closes stroke token;
            break;

        case T_fill:
            parseFill( fill );
            polyLine->SetFillMode( fill.m_FillType );
            break;

        default:
            Expecting( "pts, stroke, or fill" );
        }
    }

    return polyLine.release();
}


LIB_RECTANGLE* SCH_SEXPR_PARSER::parseRectangle()
{
    wxCHECK_MSG( CurTok() == T_rectangle, nullptr,
                 wxT( "Cannot parse " ) + GetTokenString( CurTok() ) + wxT( " as a rectangle token." ) );

    T token;
    FILL_PARAMS fill;
    std::unique_ptr<LIB_RECTANGLE> rectangle( new LIB_RECTANGLE( nullptr ) );

    rectangle->SetUnit( m_unit );
    rectangle->SetConvert( m_convert );

    for( token = NextTok();  token != T_RIGHT;  token = NextTok() )
    {
        if( token != T_LEFT )
            Expecting( T_LEFT );

        token = NextTok();

        switch( token )
        {
        case T_start:
            rectangle->SetPosition( parseXY() );
            NeedRIGHT();
            break;

        case T_end:
            rectangle->SetEnd( parseXY() );
            NeedRIGHT();
            break;

        case T_stroke:
            NeedLEFT();
            token = NextTok();

            if( token != T_width )
                Expecting( "width" );

            rectangle->SetWidth( parseInternalUnits( "stroke width" ) );
            NeedRIGHT();   // Closes width token;
            NeedRIGHT();   // Closes stroke token;
            break;

        case T_fill:
            parseFill( fill );
            rectangle->SetFillMode( fill.m_FillType );
            break;

        default:
            Expecting( "start, end, stroke, or fill" );
        }
    }

    return rectangle.release();
}


LIB_TEXT* SCH_SEXPR_PARSER::parseText()
{
    wxCHECK_MSG( CurTok() == T_text, nullptr,
                 wxT( "Cannot parse " ) + GetTokenString( CurTok() ) + wxT( " as a text token." ) );

    T token;
    wxString tmp;
    wxString error;
    std::unique_ptr<LIB_TEXT> text( new LIB_TEXT( nullptr ) );

    text->SetUnit( m_unit );
    text->SetConvert( m_convert );
    token = NextTok();

    if( !IsSymbol( token ) )
    {
        error.Printf( _( "Invalid text string in\nfile: \"%s\"\nline: %d\noffset: %d" ),
                      CurSource().c_str(), CurLineNumber(), CurOffset() );
        THROW_IO_ERROR( error );
    }

    text->SetText( FromUTF8() );

    for( token = NextTok();  token != T_RIGHT;  token = NextTok() )
    {
        if( token != T_LEFT )
            Expecting( T_LEFT );

        token = NextTok();

        switch( token )
        {
        case T_at:
            text->SetPosition( parseXY() );
            text->SetTextAngle( parseDouble( "text angle" ) );
            NeedRIGHT();
            break;

        case T_effects:
            parseEDA_TEXT( static_cast<EDA_TEXT*>( text.get() ) );
            break;

        default:
            Expecting( "at or effects" );
        }
    }

    return text.release();
}


void SCH_SEXPR_PARSER::parsePAGE_INFO( PAGE_INFO& aPageInfo )
{
    wxCHECK_RET( CurTok() == T_page,
                 wxT( "Cannot parse " ) + GetTokenString( CurTok() ) + wxT( " as a PAGE_INFO." ) );

    T token;

    NeedSYMBOL();

    wxString pageType = FromUTF8();

    if( !aPageInfo.SetType( pageType ) )
    {
        wxString err;
        err.Printf( _( "Page type \"%s\" is not valid " ), GetChars( FromUTF8() ) );
        THROW_PARSE_ERROR( err, CurSource(), CurLine(), CurLineNumber(), CurOffset() );
    }

    if( pageType == PAGE_INFO::Custom )
    {
        double width = parseDouble( "width" );      // width in mm

        // Perform some controls to avoid crashes if the size is edited by hands
        if( width < 100.0 )
            width = 100.0;
        else if( width > 1200.0 )
            width = 1200.0;

        double height = parseDouble( "height" );    // height in mm

        if( height < 100.0 )
            height = 100.0;
        else if( height > 1200.0 )
            height = 1200.0;

        aPageInfo.SetWidthMils( Mm2mils( width ) );
        aPageInfo.SetHeightMils( Mm2mils( height ) );
    }

    token = NextTok();

    if( token == T_portrait )
    {
        aPageInfo.SetPortrait( true );
        NeedRIGHT();
    }
    else if( token != T_RIGHT )
    {
        Expecting( "portrait" );
    }
}


void SCH_SEXPR_PARSER::parseTITLE_BLOCK( TITLE_BLOCK& aTitleBlock )
{
    wxCHECK_RET( CurTok() == T_title_block,
                 wxT( "Cannot parse " ) + GetTokenString( CurTok() ) +
                 wxT( " as TITLE_BLOCK." ) );

    T token;

    for( token = NextTok();  token != T_RIGHT;  token = NextTok() )
    {
        if( token != T_LEFT )
            Expecting( T_LEFT );

        token = NextTok();

        switch( token )
        {
        case T_title:
            NextTok();
            aTitleBlock.SetTitle( FromUTF8() );
            break;

        case T_date:
            NextTok();
            aTitleBlock.SetDate( FromUTF8() );
            break;

        case T_rev:
            NextTok();
            aTitleBlock.SetRevision( FromUTF8() );
            break;

        case T_company:
            NextTok();
            aTitleBlock.SetCompany( FromUTF8() );
            break;

        case T_comment:
        {
            int commentNumber = parseInt( "comment" );

            switch( commentNumber )
            {
            case 1:
                NextTok();
                aTitleBlock.SetComment( 0, FromUTF8() );
                break;

            case 2:
                NextTok();
                aTitleBlock.SetComment( 1, FromUTF8() );
                break;

            case 3:
                NextTok();
                aTitleBlock.SetComment( 2, FromUTF8() );
                break;

            case 4:
                NextTok();
                aTitleBlock.SetComment( 3, FromUTF8() );
                break;

            case 5:
                NextTok();
                aTitleBlock.SetComment( 4, FromUTF8() );
                break;

            case 6:
                NextTok();
                aTitleBlock.SetComment( 5, FromUTF8() );
                break;

            case 7:
                NextTok();
                aTitleBlock.SetComment( 6, FromUTF8() );
                break;

            case 8:
                NextTok();
                aTitleBlock.SetComment( 7, FromUTF8() );
                break;

            case 9:
                NextTok();
                aTitleBlock.SetComment( 8, FromUTF8() );
                break;

            default:
                wxString err;
                err.Printf( wxT( "%d is not a valid title block comment number" ), commentNumber );
                THROW_PARSE_ERROR( err, CurSource(), CurLine(), CurLineNumber(), CurOffset() );
            }

            break;
        }

        default:
            Expecting( "title, date, rev, company, or comment" );
        }

        NeedRIGHT();
    }
}


SCH_FIELD* SCH_SEXPR_PARSER::parseSchField()
{
    wxCHECK_MSG( CurTok() == T_property, nullptr,
                 wxT( "Cannot parse " ) + GetTokenString( CurTok() ) +
                 wxT( " as a property token." ) );

    wxString error;
    wxString name;
    wxString value;

    T token = NextTok();

    if( !IsSymbol( token ) )
    {
        error.Printf( _( "Invalid property name in\nfile: \"%s\"\nline: %d\noffset: %d" ),
                      CurSource().c_str(), CurLineNumber(), CurOffset() );
        THROW_IO_ERROR( error );
    }

    name = FromUTF8();

    if( name.IsEmpty() )
    {
        error.Printf( _( "Empty property name in\nfile: \"%s\"\nline: %d\noffset: %d" ),
                      CurSource().c_str(), CurLineNumber(), CurOffset() );
        THROW_IO_ERROR( error );
    }

    token = NextTok();

    if( !IsSymbol( token ) )
    {
        error.Printf( _( "Invalid property value in\nfile: \"%s\"\nline: %d\noffset: %d" ),
                      CurSource().c_str(), CurLineNumber(), CurOffset() );
        THROW_IO_ERROR( error );
    }

    // Empty property values are valid.
    value = FromUTF8();

    std::unique_ptr<SCH_FIELD> field( new SCH_FIELD( wxDefaultPosition, MANDATORY_FIELDS,
                                                     nullptr, name ) );

    field->SetText( value );
    field->SetVisible( true );

    for( token = NextTok();  token != T_RIGHT;  token = NextTok() )
    {
        if( token != T_LEFT )
            Expecting( T_LEFT );

        token = NextTok();

        switch( token )
        {
        case T_id:
            field->SetId( parseInt( "field ID" ) );
            NeedRIGHT();
            break;

        case T_at:
            field->SetPosition( parseXY() );
            field->SetTextAngle( static_cast<int>( parseDouble( "text angle" ) * 10.0 ) );
            NeedRIGHT();
            break;

        case T_effects:
            parseEDA_TEXT( static_cast<EDA_TEXT*>( field.get() ) );
            break;

        default:
            Expecting( "at or effects" );
        }
    }

    return field.release();
}


SCH_SHEET_PIN* SCH_SEXPR_PARSER::parseSchSheetPin( SCH_SHEET* aSheet )
{
    wxCHECK_MSG( aSheet != nullptr, nullptr, "" );
    wxCHECK_MSG( CurTok() == T_pin, nullptr,
                 wxT( "Cannot parse " ) + GetTokenString( CurTok() ) +
                 wxT( " as a sheet pin token." ) );

    wxString error;
    wxString name;
    wxString shape;

    T token = NextTok();

    if( !IsSymbol( token ) )
    {
        error.Printf( _( "Invalid sheet pin name in\nfile: \"%s\"\nline: %d\noffset: %d" ),
                      CurSource().c_str(), CurLineNumber(), CurOffset() );
        THROW_IO_ERROR( error );
    }

    name = FromUTF8();

    if( name.IsEmpty() )
    {
        error.Printf( _( "Empty sheet pin name in\nfile: \"%s\"\nline: %d\noffset: %d" ),
                      CurSource().c_str(), CurLineNumber(), CurOffset() );
        THROW_IO_ERROR( error );
    }

    std::unique_ptr<SCH_SHEET_PIN> sheetPin( new SCH_SHEET_PIN( aSheet, wxPoint( 0, 0 ), name ) );

    token = NextTok();

    switch( token )
    {
    case T_input:          sheetPin->SetShape( PINSHEETLABEL_SHAPE::PS_INPUT );        break;
    case T_output:         sheetPin->SetShape( PINSHEETLABEL_SHAPE::PS_OUTPUT );       break;
    case T_bidirectional:  sheetPin->SetShape( PINSHEETLABEL_SHAPE::PS_BIDI );         break;
    case T_tri_state:      sheetPin->SetShape( PINSHEETLABEL_SHAPE::PS_TRISTATE );     break;
    case T_passive:        sheetPin->SetShape( PINSHEETLABEL_SHAPE::PS_UNSPECIFIED );  break;
    default:
        Expecting( "input, output, bidirectional, tri_state, or passive" );
    }

    for( token = NextTok();  token != T_RIGHT;  token = NextTok() )
    {
        if( token != T_LEFT )
            Expecting( T_LEFT );

        token = NextTok();

        switch( token )
        {
        case T_at:
        {
            sheetPin->SetPosition( parseXY() );

            double angle = parseDouble( "sheet pin angle (side)" );

            if( angle == 0.0 )
                sheetPin->SetEdge( SHEET_RIGHT_SIDE );
            else if( angle == 90.0 )
                sheetPin->SetEdge( SHEET_TOP_SIDE );
            else if( angle == 180.0 )
                sheetPin->SetEdge( SHEET_LEFT_SIDE );
            else if( angle == 270.0 )
                sheetPin->SetEdge( SHEET_BOTTOM_SIDE );
            else
                Expecting( "0, 90, 180, or 270" );

            NeedRIGHT();
            break;
        }

        case T_effects:
            parseEDA_TEXT( static_cast<EDA_TEXT*>( sheetPin.get() ) );
            break;

        default:
            Expecting( "at or effects" );
        }
    }

    return sheetPin.release();
}


void SCH_SEXPR_PARSER::parseSchSymbolInstances( std::unique_ptr<SCH_COMPONENT>& aSymbol )
{
    wxCHECK_RET( CurTok() == T_instances,
                 wxT( "Cannot parse " ) + GetTokenString( CurTok() ) +
                 wxT( " as a instances token." ) );

    T token;

    for( token = NextTok();  token != T_RIGHT;  token = NextTok() )
    {
        if( token != T_LEFT )
            Expecting( T_LEFT );

        token = NextTok();

        switch( token )
        {
        case T_path:
        {
            NeedSYMBOL();

            int unit = 1;
            wxString reference;
            KIID_PATH path( FromUTF8() );

            for( token = NextTok();  token != T_RIGHT;  token = NextTok() )
            {
                if( token != T_LEFT )
                    Expecting( T_LEFT );

                token = NextTok();

                switch( token )
                {
                case T_reference:
                    NeedSYMBOL();
                    reference = FromUTF8();
                    NeedRIGHT();
                    break;

                case T_unit:
                    unit = parseInt( "symbol unit" );
                    NeedRIGHT();
                    break;

                default:
                    Expecting( "path or unit" );
                }
            }

            aSymbol->AddHierarchicalReference( path, reference, unit );
            aSymbol->GetField( REFERENCE )->SetText( reference );
            break;
        }

        default:
            Expecting( "path" );
        }
    }
}


void SCH_SEXPR_PARSER::ParseSchematic( SCH_SCREEN* aScreen )
{
    wxCHECK_RET( aScreen != nullptr, "" );

    T token;

    NeedLEFT();
    NextTok();

    if( CurTok() != T_kicad_sch )
        Expecting( "kicad_sch" );

    parseHeader( T_kicad_sch, SEXPR_SCHEMATIC_FILE_VERSION );

    for( token = NextTok();  token != T_RIGHT;  token = NextTok() )
    {
        if( token != T_LEFT )
            Expecting( T_LEFT );

        token = NextTok();

        switch( token )
        {
        case T_page:
        {
            PAGE_INFO pageInfo;
            parsePAGE_INFO( pageInfo );
            aScreen->SetPageSettings( pageInfo );
            break;
        }

        case T_title_block:
        {
            TITLE_BLOCK tb;
            parseTITLE_BLOCK( tb );
            aScreen->SetTitleBlock( tb );
            break;
        }

        case T_symbol:
            aScreen->Append( static_cast<SCH_ITEM*>( parseSchematicSymbol() ) );
            break;

        case T_image:
            aScreen->Append( static_cast<SCH_ITEM*>( parseImage() ) );
            break;

        case T_sheet:
            aScreen->Append( static_cast<SCH_ITEM*>( parseSheet() ) );
            break;

        case T_junction:
            aScreen->Append( static_cast<SCH_ITEM*>( parseJunction() ) );
            break;

        case T_no_connect:
            aScreen->Append( static_cast<SCH_ITEM*>( parseNoConnect() ) );
            break;

        case T_bus_entry:
            aScreen->Append( static_cast<SCH_ITEM*>( parseBusEntry() ) );
            break;

        case T_polyline:
        case T_bus:
        case T_wire:
            aScreen->Append( static_cast<SCH_ITEM*>( parseLine() ) );
            break;

        case T_text:
        case T_label:
        case T_global_label:
        case T_hierarchical_label:
            aScreen->Append( static_cast<SCH_ITEM*>( parseSchText() ) );
            break;

        default:
            Expecting( "symbol, bitmap, sheet, junction, no_connect, bus_entry, line"
                       "bus, text, label, global_label, or hierarchical_label" );
        }
    }
}


SCH_COMPONENT* SCH_SEXPR_PARSER::parseSchematicSymbol()
{
    wxCHECK_MSG( CurTok() == T_symbol, nullptr,
                 wxT( "Cannot parse " ) + GetTokenString( CurTok() ) + wxT( " as a symbol." ) );

    T token;
    int orientation = CMP_ORIENT_0;
    wxString tmp;
    SCH_FIELD* field;
    std::unique_ptr<SCH_COMPONENT> symbol( new SCH_COMPONENT() );

    m_fieldId = MANDATORY_FIELDS;

    for( token = NextTok();  token != T_RIGHT;  token = NextTok() )
    {
        if( token != T_LEFT )
            Expecting( T_LEFT );

        token = NextTok();

        switch( token )
        {
        case T_lib_id:
        {
            token = NextTok();

            if( !IsSymbol( token ) && token != T_NUMBER )
                Expecting( "symbol|number" );

            LIB_ID id;
            wxString text = FromUTF8();

            if( !text.IsEmpty() && id.Parse( text, LIB_ID::ID_SCH, true ) >= 0 )
            {
                tmp.Printf( _( "Invalid symbol lbirary ID in\nfile: \"%s\"\nline: %d\n"
                               "offset: %d" ),
                            GetChars( CurSource() ), CurLineNumber(), CurOffset() );
                THROW_IO_ERROR( tmp );
            }

            symbol->SetLibId( id );
            NeedRIGHT();
            break;
        }

        case T_at:
            symbol->SetPosition( parseXY() );

            switch( static_cast<int>( parseDouble( "symbol orientation" ) ) )
            {
            case 0:    orientation = CMP_ORIENT_0;    break;
            case 90:   orientation = CMP_ORIENT_90;   break;
            case 180:  orientation = CMP_ORIENT_180;  break;
            case 270:  orientation = CMP_ORIENT_270;  break;
            default:   Expecting( "0, 90, 180, or 270" );
            }

            NeedRIGHT();
            break;

        case T_mirror:
            token = NextTok();

            if( token == T_x )
                orientation |= CMP_MIRROR_X;
            else if( token == T_y )
                orientation |= CMP_MIRROR_Y;
            else
                Expecting( "x or y" );

            NeedRIGHT();
            break;

        case T_unit:
            symbol->SetUnit( parseInt( "symbol unit" ) );
            NeedRIGHT();
            break;

        case T_uuid:
            NeedSYMBOL();
            const_cast<KIID&>( symbol->m_Uuid ) = KIID( FromUTF8() );
            NeedRIGHT();
            break;

        case T_property:
        {
            field = parseSchField();
            field->SetParent( symbol.get() );

            if( field->GetId() == REFERENCE )
            {
                field->SetLayer( LAYER_REFERENCEPART );
            }
            else if( field->GetId() == VALUE )
            {
                field->SetLayer( LAYER_VALUEPART );
            }
            else if( field->GetId() >= MANDATORY_FIELDS )
            {
                symbol->AddField( *field );
            }

            *symbol->GetField( field->GetId() ) = *field;
            delete field;
            break;
        }

        case T_instances:
            parseSchSymbolInstances( symbol );
            break;

        default:
            Expecting( "lib_id, at, mirror, uuid, property, or instances" );
        }
    }

    symbol->SetOrientation( orientation );

    return symbol.release();
}


SCH_BITMAP* SCH_SEXPR_PARSER::parseImage()
{
    wxCHECK_MSG( CurTok() == T_image, nullptr,
                 wxT( "Cannot parse " ) + GetTokenString( CurTok() ) + wxT( " as an image." ) );

    T token;
    std::unique_ptr<SCH_BITMAP> bitmap( new SCH_BITMAP() );

    for( token = NextTok();  token != T_RIGHT;  token = NextTok() )
    {
        if( token != T_LEFT )
            Expecting( T_LEFT );

        token = NextTok();

        switch( token )
        {
        case T_at:
            bitmap->SetPosition( parseXY() );
            NeedRIGHT();
            break;

        case T_scale:
            bitmap->GetImage()->SetScale( parseDouble( "image scale factor" ) );

            if( !std::isnormal( bitmap->GetImage()->GetScale() ) )
                bitmap->GetImage()->SetScale( 1.0 );

            NeedRIGHT();
            break;

        case T_data:
        {
            token = NextTok();

            wxString data;

            while( token != T_RIGHT )
            {
                if( !IsSymbol( token ) )
                    Expecting( "base64 image data" );

                data += FromUTF8();
            }

            wxMemoryBuffer buffer = wxBase64Decode( data );
            wxMemoryOutputStream stream( buffer.GetData(), buffer.GetBufSize() );
            wxImage* image = new wxImage();
            wxMemoryInputStream istream( stream );
            image->LoadFile( istream, wxBITMAP_TYPE_PNG );
            bitmap->GetImage()->SetImage( image );
            bitmap->GetImage()->SetBitmap( new wxBitmap( *image ) );
            break;
        }

        default:
            Expecting( "at, scale, or data" );
        }
    }

    return bitmap.release();
}


SCH_SHEET* SCH_SEXPR_PARSER::parseSheet()
{
    wxCHECK_MSG( CurTok() == T_sheet, nullptr,
                 wxT( "Cannot parse " ) + GetTokenString( CurTok() ) + wxT( " as a sheet." ) );

    T token;
    STROKE_PARAMS stroke;
    FILL_PARAMS fill;
    SCH_FIELD* field;
    std::vector<SCH_FIELD> fields;
    std::unique_ptr<SCH_SHEET> sheet( new SCH_SHEET() );

    for( token = NextTok();  token != T_RIGHT;  token = NextTok() )
    {
        if( token != T_LEFT )
            Expecting( T_LEFT );

        token = NextTok();

        switch( token )
        {
        case T_at:
            sheet->SetPosition( parseXY() );
            NeedRIGHT();
            break;

        case T_size:
        {
            wxSize size;
            size.SetWidth( parseInternalUnits( "sheet width" ) );
            size.SetHeight( parseInternalUnits( "sheet height" ) );
            sheet->SetSize( size );
            NeedRIGHT();
            break;
        }

        case T_stroke:
            parseStroke( stroke );
            sheet->SetBorderWidth( stroke.m_Width );
            sheet->SetBorderColor( stroke.m_Color );
            break;

        case T_fill:
            parseFill( fill );
            sheet->SetBackgroundColor( fill.m_Color );
            break;

        case T_uuid:
            NeedSYMBOL();
            const_cast<KIID&>( sheet->m_Uuid ) = KIID( FromUTF8() );
            NeedRIGHT();
            break;

        case T_property:
            field = parseSchField();

            if( field->GetName() == "ki_sheet_name" )
            {
                field->SetId( SHEETNAME );
                field->SetName( SCH_SHEET::GetDefaultFieldName( SHEETNAME ) );
            }
            else if( field->GetName() == "ki_sheet_file" )
            {
                field->SetId( SHEETFILENAME );
                field->SetName( SCH_SHEET::GetDefaultFieldName( SHEETFILENAME ) );
            }
            else
            {
                field->SetId( m_fieldId );
                m_fieldId += 1;
            }

            field->SetParent( sheet.get() );
            fields.emplace_back( *field );
            delete field;
            break;

        case T_pin:
            sheet->AddPin( parseSchSheetPin( sheet.get() ) );
            break;

        default:
            Expecting( "at, size, stroke, background, uuid, property, or pin" );
        }
    }

    sheet->SetFields( fields );

    return sheet.release();
}


SCH_JUNCTION* SCH_SEXPR_PARSER::parseJunction()
{
    wxCHECK_MSG( CurTok() == T_junction, nullptr,
                 wxT( "Cannot parse " ) + GetTokenString( CurTok() ) + wxT( " as a junction." ) );

    T token;
    std::unique_ptr<SCH_JUNCTION> junction( new SCH_JUNCTION() );

    for( token = NextTok();  token != T_RIGHT;  token = NextTok() )
    {
        if( token != T_LEFT )
            Expecting( T_LEFT );

        token = NextTok();

        switch( token )
        {
        case T_at:
            junction->SetPosition( parseXY() );
            NeedRIGHT();
            break;

        default:
            Expecting( "at" );
        }
    }

    return junction.release();
}


SCH_NO_CONNECT* SCH_SEXPR_PARSER::parseNoConnect()
{
    wxCHECK_MSG( CurTok() == T_no_connect, nullptr,
                 wxT( "Cannot parse " ) + GetTokenString( CurTok() ) + wxT( " as a no connect." ) );

    T token;
    std::unique_ptr<SCH_NO_CONNECT> no_connect( new SCH_NO_CONNECT() );

    for( token = NextTok();  token != T_RIGHT;  token = NextTok() )
    {
        if( token != T_LEFT )
            Expecting( T_LEFT );

        token = NextTok();

        switch( token )
        {
        case T_at:
            no_connect->SetPosition( parseXY() );
            NeedRIGHT();
            break;

        default:
            Expecting( "at" );
        }
    }

    return no_connect.release();
}


SCH_BUS_WIRE_ENTRY* SCH_SEXPR_PARSER::parseBusEntry()
{
    wxCHECK_MSG( CurTok() == T_bus_entry, nullptr,
                 wxT( "Cannot parse " ) + GetTokenString( CurTok() ) + wxT( " as a bus entry." ) );

    T token;
    std::unique_ptr<SCH_BUS_WIRE_ENTRY> busEntry( new SCH_BUS_WIRE_ENTRY() );

    for( token = NextTok();  token != T_RIGHT;  token = NextTok() )
    {
        if( token != T_LEFT )
            Expecting( T_LEFT );

        token = NextTok();

        switch( token )
        {
        case T_at:
            busEntry->SetPosition( parseXY() );
            NeedRIGHT();
            break;

        case T_size:
        {
            wxSize size;

            size.SetWidth( parseInternalUnits( "bus entry height" ) );
            size.SetHeight( parseInternalUnits( "bus entry width" ) );
            busEntry->SetSize( size );

            if( size.y < 0 )
                busEntry->SetBusEntryShape( '/' );

            NeedRIGHT();
            break;
        }

        default:
            Expecting( "at or size" );
        }
    }

    return busEntry.release();
}


SCH_LINE* SCH_SEXPR_PARSER::parseLine()
{
    T token;
    STROKE_PARAMS stroke;
    std::unique_ptr<SCH_LINE> line( new SCH_LINE() );

    switch( CurTok() )
    {
    case T_polyline:   line->SetLayer( LAYER_NOTES );   break;
    case T_wire:       line->SetLayer( LAYER_WIRE );    break;
    case T_bus:        line->SetLayer( LAYER_BUS );     break;
    default:
        wxCHECK_MSG( false, nullptr,
                     wxT( "Cannot parse " ) + GetTokenString( CurTok() ) + wxT( " as a line." ) );
    }

    for( token = NextTok();  token != T_RIGHT;  token = NextTok() )
    {
        if( token != T_LEFT )
            Expecting( T_LEFT );

        token = NextTok();

        switch( token )
        {
        case T_pts:
            NeedLEFT();
            token = NextTok();

            if( token != T_xy )
                Expecting( "xy" );

            line->SetStartPoint( parseXY() );
            NeedRIGHT();
            NeedLEFT();
            token = NextTok();

            if( token != T_xy )
                Expecting( "xy" );

            line->SetEndPoint( parseXY() );
            NeedRIGHT();
            NeedRIGHT();
            break;

        case T_stroke:
            parseStroke( stroke );
            line->SetLineWidth( stroke.m_Width );
            line->SetLineStyle( stroke.m_Type );
            line->SetLineColor( stroke.m_Color );
            break;

        default:
            Expecting( "at or stroke" );
        }
    }

    return line.release();
}


SCH_TEXT* SCH_SEXPR_PARSER::parseSchText()
{
    T token;
    std::unique_ptr<SCH_TEXT> text;

    switch( CurTok() )
    {
    case T_text:                text.reset( new SCH_TEXT );          break;
    case T_label:               text.reset( new SCH_LABEL );         break;
    case T_global_label:        text.reset( new SCH_GLOBALLABEL );   break;
    case T_hierarchical_label:  text.reset( new SCH_HIERLABEL );     break;
    default:
        wxCHECK_MSG( false, nullptr,
                     wxT( "Cannot parse " ) + GetTokenString( CurTok() ) + wxT( " as text." ) );
    }

    NeedSYMBOL();

    text->SetText( FromUTF8() );

    for( token = NextTok();  token != T_RIGHT;  token = NextTok() )
    {
        if( token != T_LEFT )
            Expecting( T_LEFT );

        token = NextTok();

        switch( token )
        {
        case T_at:
        {
            text->SetPosition( parseXY() );

            switch( static_cast<int>( parseDouble( "text angle" ) ) )
            {
            case 0:    text->SetLabelSpinStyle( LABEL_SPIN_STYLE::RIGHT );    break;
            case 90:   text->SetLabelSpinStyle( LABEL_SPIN_STYLE::UP );       break;
            case 180:  text->SetLabelSpinStyle( LABEL_SPIN_STYLE::LEFT );     break;
            case 270:  text->SetLabelSpinStyle( LABEL_SPIN_STYLE::BOTTOM );   break;
            default:
                wxFAIL;
                text->SetLabelSpinStyle( LABEL_SPIN_STYLE::RIGHT );
                break;
            }

            NeedRIGHT();
            break;
        }

        case T_shape:
        {
            if( text->Type() == SCH_TEXT_T || text->Type() == SCH_LABEL_T )
                Unexpected( T_shape );

            token = NextTok();

            switch( token )
            {
            case T_input:          text->SetShape( PINSHEETLABEL_SHAPE::PS_INPUT );        break;
            case T_output:         text->SetShape( PINSHEETLABEL_SHAPE::PS_OUTPUT );       break;
            case T_bidirectional:  text->SetShape( PINSHEETLABEL_SHAPE::PS_BIDI );         break;
            case T_tri_state:      text->SetShape( PINSHEETLABEL_SHAPE::PS_TRISTATE );     break;
            case T_passive:        text->SetShape( PINSHEETLABEL_SHAPE::PS_UNSPECIFIED );  break;
            default:
                Expecting( "input, output, bidirectional, tri_state, or passive" );
            }

            NeedRIGHT();
            break;
        }

        case T_effects:
            parseEDA_TEXT( static_cast<EDA_TEXT*>( text.get() ) );
            break;

        default:
            Expecting( "at, shape, or effects" );
        }
    }

    return text.release();
}
