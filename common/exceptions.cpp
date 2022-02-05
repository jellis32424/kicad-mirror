/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2007-2016 SoftPLC Corporation, Dick Hollenbeck <dick@softplc.com>
 * Copyright (C) 2017 KiCad Developers, see AUTHORS.txt for contributors.
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

#include <wx/string.h>
#include <wx/translation.h>
#include <ki_exception.h>


const wxString IO_ERROR::What() const
{
#ifdef DEBUG
     return wxString( wxT( "IO_ERROR: " ) ) + Problem() + wxT( "\n\n" ) + Where();
#else
     return Problem();
#endif
}


const wxString IO_ERROR::Where() const
{
    return where;
}


const wxString IO_ERROR::Problem() const
{
    return problem;
}



void IO_ERROR::init( const wxString& aProblem, const char* aThrowersFile,
                     const char* aThrowersFunction, int aThrowersLineNumber )
{
    problem = aProblem;

    // The throwers filename is a full filename, depending on Kicad source location.
    // a short filename will be printed (it is better for user, the full filename has no meaning).
    wxString srcname = aThrowersFile;

    where.Printf( _( "from %s : %s() line %d" ),
                  srcname.AfterLast( '/' ),
                  wxString( aThrowersFunction ),
                  aThrowersLineNumber );
}


void PARSE_ERROR::init( const wxString& aProblem, const char* aThrowersFile,
                        const char* aThrowersFunction, int aThrowersLineNumber,
                        const wxString& aSource, const char* aInputLine, int aLineNumber,
                        int aByteIndex )
{
    parseProblem = aProblem;

    problem.Printf( _( "%s in '%s', line %d, offset %d." ),
                    aProblem,
                    aSource,
                    aLineNumber,
                    aByteIndex );

    inputLine  = aInputLine;
    lineNumber = aLineNumber;
    byteIndex  = aByteIndex;

    // The throwers filename is a full filename, depending on Kicad source location.
    // a short filename will be printed (it is better for user, the full filename has no meaning).
    wxString srcname = aThrowersFile;

    where.Printf( _( "from %s : %s() line:%d" ),
                  srcname.AfterLast( '/' ),
                  wxString( aThrowersFunction ),
                  aThrowersLineNumber );
}


void FUTURE_FORMAT_ERROR::init( const wxString& aRequiredVersion )
{
    requiredVersion = aRequiredVersion;

    problem.Printf( _( "KiCad was unable to open this file because it was created with a more "
                       "recent version than the one you are running.\n\n"
                       "To open it you will need to upgrade KiCad to a version dated %s or "
                       "later." ),
                    aRequiredVersion );
}


FUTURE_FORMAT_ERROR::FUTURE_FORMAT_ERROR( const wxString& aRequiredVersion ) :
        PARSE_ERROR()
{
    init( aRequiredVersion );

    lineNumber = 0;
    byteIndex = 0;
}


FUTURE_FORMAT_ERROR::FUTURE_FORMAT_ERROR( const PARSE_ERROR& aParseError,
                                          const wxString& aRequiredVersion ) :
        PARSE_ERROR()
{
    init( aRequiredVersion );

    if( !aParseError.Problem().IsEmpty() )
        problem += wxS( "\n\n" ) + _( "Full error text:" ) + wxS( "\n" ) + aParseError.Problem();

    lineNumber = aParseError.lineNumber;
    byteIndex = aParseError.byteIndex;
    inputLine = aParseError.inputLine;
}
