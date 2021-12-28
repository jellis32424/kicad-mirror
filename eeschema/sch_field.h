/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2015 Jean-Pierre Charras, jp.charras at wanadoo.fr
 * Copyright (C) 2004-2021 KiCad Developers, see AUTHORS.txt for contributors.
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

#ifndef CLASS_SCH_FIELD_H
#define CLASS_SCH_FIELD_H


#include <eda_text.h>
#include <sch_item.h>
#include <template_fieldnames.h>
#include <general.h>

class SCH_EDIT_FRAME;
class LIB_FIELD;


/**
 * Instances are attached to a symbol or sheet and provide a place for the symbol's value,
 * reference designator, footprint, , a sheet's name, filename, and user definable name-value
 * pairs of arbitrary purpose.
 *
 *  - Field 0 is reserved for the symbol reference.
 *  - Field 1 is reserved for the symbol value.
 *  - Field 2 is reserved for the symbol footprint.
 *  - Field 3 is reserved for the symbol data sheet file.
 *  - Field 4 and higher are user definable.
 */
class SCH_FIELD : public SCH_ITEM, public EDA_TEXT
{
public:
    SCH_FIELD( const wxPoint& aPos, int aFieldId, SCH_ITEM* aParent,
               const wxString& aName = wxEmptyString );

    // Do not create a copy constructor.  The one generated by the compiler is adequate.

    ~SCH_FIELD();

    static inline bool ClassOf( const EDA_ITEM* aItem )
    {
        return aItem && SCH_FIELD_T == aItem->Type();
    }

    wxString GetClass() const override
    {
        return wxT( "SCH_FIELD" );
    }

    bool IsType( const KICAD_T aScanTypes[] ) const override
    {
        if( SCH_ITEM::IsType( aScanTypes ) )
            return true;

        for( const KICAD_T* p = aScanTypes; *p != EOT; ++p )
        {
            if( *p == SCH_FIELD_LOCATE_REFERENCE_T && m_id == REFERENCE_FIELD )
                return true;
            else if ( *p == SCH_FIELD_LOCATE_VALUE_T && m_id == VALUE_FIELD )
                return true;
            else if ( *p == SCH_FIELD_LOCATE_FOOTPRINT_T && m_id == FOOTPRINT_FIELD )
                return true;
            else if ( *p == SCH_FIELD_LOCATE_DATASHEET_T && m_id == DATASHEET_FIELD )
                return true;
        }

        return false;
    }

    bool IsHypertext() const override
    {
        return m_id == 0 && m_parent && m_parent->Type() == SCH_GLOBAL_LABEL_T;
    }

    void DoHypertextMenu( EDA_DRAW_FRAME* aFrame ) override;

    /**
     * Return the field name.
     *
     * @param aUseDefaultName When true return the default field name if the field name is
     *                        empty.  Otherwise the default field name is returned.
     * @return the name of the field.
     */
    wxString GetName( bool aUseDefaultName = true ) const;

    /**
     * Get a non-language-specific name for a field which can be used for storage, variable
     * look-up, etc.
     */
    wxString GetCanonicalName() const;

    void SetName( const wxString& aName ) { m_name = aName; }

    int GetId() const { return m_id; }

    void SetId( int aId );

    wxString GetShownText( int aDepth = 0 ) const override;

    /**
     * Adjusters to allow EDA_TEXT to draw/print/etc. text in absolute coords.
     */
    EDA_ANGLE GetDrawRotation() const override;
    wxPoint GetDrawPos() const override;
    GR_TEXT_H_ALIGN_T GetDrawHorizJustify() const override;
    GR_TEXT_V_ALIGN_T GetDrawVertJustify() const override;

    const EDA_RECT GetBoundingBox() const override;

    /**
     * Return whether the field will be rendered with the horizontal justification
     * inverted due to rotation or mirroring of the parent.
     */
    bool IsHorizJustifyFlipped() const;
    bool IsVertJustifyFlipped() const;

    GR_TEXT_H_ALIGN_T GetEffectiveHorizJustify() const;
    GR_TEXT_V_ALIGN_T GetEffectiveVertJustify() const;

    /**
     * @return true if the field is either empty or holds "~".
     */
    bool IsVoid() const;

    void SwapData( SCH_ITEM* aItem ) override;

    /**
     * Copy parameters from a LIB_FIELD source.
     *
     * Pointers and specific values (position) are not copied.
     *
     * @param aSource is the LIB_FIELD to read.
     */
    void ImportValues( const LIB_FIELD& aSource );

    int GetPenWidth() const override;

    void Print( const RENDER_SETTINGS* aSettings, const wxPoint& aOffset ) override;

    void Move( const wxPoint& aMoveVector ) override
    {
        Offset( aMoveVector );
    }

    void Rotate( const wxPoint& aCenter ) override;

    /**
     * @copydoc SCH_ITEM::MirrorVertically()
     *
     * This overload does nothing.  Fields are never mirrored alone.  They are moved
     * when the parent symbol is mirrored.  This function is only needed by the
     * pure function of the master class.
     */
    void MirrorVertically( int aCenter ) override
    {
    }

    /**
     * @copydoc SCH_ITEM::MirrorHorizontally()
     *
     * This overload does nothing.  Fields are never mirrored alone.  They are moved
     * when the parent symbol is mirrored.  This function is only needed by the
     * pure function of the master class.
     */
    void MirrorHorizontally( int aCenter ) override
    {
    }

    bool Matches( const wxFindReplaceData& aSearchData, void* aAuxData ) const override;

    bool Replace( const wxFindReplaceData& aSearchData, void* aAuxData = nullptr ) override;

    wxString GetSelectMenuText( EDA_UNITS aUnits ) const override;
    void GetMsgPanelInfo( EDA_DRAW_FRAME* aFrame, std::vector<MSG_PANEL_ITEM>& aList ) override;

    BITMAPS GetMenuImage() const override;

    bool IsReplaceable() const override;

    wxPoint GetLibPosition() const { return EDA_TEXT::GetTextPos(); }

    wxPoint GetPosition() const override;
    void SetPosition( const wxPoint& aPosition ) override;

    wxPoint GetParentPosition() const;

    bool HitTest( const wxPoint& aPosition, int aAccuracy = 0 ) const override;
    bool HitTest( const EDA_RECT& aRect, bool aContained, int aAccuracy = 0 ) const override;

    void Plot( PLOTTER* aPlotter ) const override;

    EDA_ITEM* Clone() const override;

    bool operator <( const SCH_ITEM& aItem ) const override;

#if defined(DEBUG)
    void Show( int nestLevel, std::ostream& os ) const override { ShowDummy( os ); }
#endif

private:
    int      m_id;         ///< Field index, @see enum MANDATORY_FIELD_T

    wxString m_name;
};


#endif /* CLASS_SCH_FIELD_H */
