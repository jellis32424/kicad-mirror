/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2018 Jean-Pierre Charras, jp.charras at wandadoo.fr
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

#ifndef BOARD_ITEM_STRUCT_H
#define BOARD_ITEM_STRUCT_H


#include <eda_item.h>
#include <eda_units.h>
#include <gr_basic.h>
#include <layer_ids.h>
#include <geometry/geometry_utils.h>
#include <stroke_params.h>
#include <geometry/eda_angle.h>

class BOARD;
class BOARD_ITEM_CONTAINER;
class SHAPE_POLY_SET;
class SHAPE_SEGMENT;
class PCB_BASE_FRAME;
class SHAPE;
class PCB_GROUP;


/**
 * A base class for any item which can be embedded within the #BOARD container class, and
 * therefore instances of derived classes should only be found in Pcbnew or other programs
 * that use class #BOARD and its contents.
 */
class BOARD_ITEM : public EDA_ITEM
{
public:
    BOARD_ITEM( BOARD_ITEM* aParent, KICAD_T idtype, PCB_LAYER_ID aLayer = F_Cu ) :
            EDA_ITEM( aParent, idtype ),
            m_layer( aLayer ),
            m_isKnockout( false ),
            m_isLocked( false ),
            m_group( nullptr )
    {
    }

    void SetParentGroup( PCB_GROUP* aGroup ) { m_group = aGroup; }
    PCB_GROUP* GetParentGroup() const { return m_group; }

    // Do not create a copy constructor & operator=.
    // The ones generated by the compiler are adequate.
    int GetX() const
    {
        VECTOR2I p = GetPosition();
        return p.x;
    }

    int GetY() const
    {
        VECTOR2I p = GetPosition();
        return p.y;
    }

    /**
     * This defaults to the center of the bounding box if not overridden.
     *
     * @return center point of the item
     */
    virtual VECTOR2I GetCenter() const
    {
        return GetBoundingBox().GetCenter();
    }

    void SetX( int aX )
    {
        VECTOR2I p( aX, GetY() );
        SetPosition( p );
    }

    void SetY( int aY )
    {
        VECTOR2I p( GetX(), aY );
        SetPosition( p );
    }

    /**
     * Returns information if the object is derived from BOARD_CONNECTED_ITEM.
     *
     * @return True if the object is of BOARD_CONNECTED_ITEM type, false otherwise.
     */
    virtual bool IsConnected() const
    {
        return false;
    }

    /**
     * @return true if the object is on any copper layer, false otherwise.
     */
    virtual bool IsOnCopperLayer() const
    {
        return IsCopperLayer( GetLayer() );
    }

    virtual bool HasHole() const
    {
        return false;
    }

    virtual bool IsTented() const
    {
        return false;
    }

    /**
     * A value of wxPoint(0,0) which can be passed to the Draw() functions.
     */
    static VECTOR2I ZeroOffset;

    /**
     * Some pad shapes can be complex (rounded/chamfered rectangle), even without considering
     * custom shapes.  This routine returns a COMPOUND shape (set of simple shapes which make
     * up the pad for use with routing, collision determination, etc).
     *
     * @note This list can contain a SHAPE_SIMPLE (a simple single-outline non-intersecting
     * polygon), but should never contain a SHAPE_POLY_SET (a complex polygon consisting of
     * multiple outlines and/or holes).
     *
     * @param aLayer in case of items spanning multiple layers, only the shapes belonging to aLayer
     *               will be returned. Pass UNDEFINED_LAYER to return shapes for all layers.
     */
    virtual std::shared_ptr<SHAPE> GetEffectiveShape( PCB_LAYER_ID aLayer = UNDEFINED_LAYER,
                                                      FLASHING aFlash = FLASHING::DEFAULT ) const;

    virtual std::shared_ptr<SHAPE_SEGMENT> GetEffectiveHoleShape() const;

    BOARD_ITEM_CONTAINER* GetParent() const { return (BOARD_ITEM_CONTAINER*) m_parent; }

    BOARD_ITEM_CONTAINER* GetParentFootprint() const;

    /**
     * Check if this item has line stoke properties.
     *
     * @see #STROKE_PARAMS
     */
    virtual bool HasLineStroke() const { return false; }

    virtual STROKE_PARAMS GetStroke() const;
    virtual void SetStroke( const STROKE_PARAMS& aStroke );

    /**
     * Return the primary layer this item is on.
     */
    virtual PCB_LAYER_ID GetLayer() const { return m_layer; }

    /**
     * Return a std::bitset of all layers on which the item physically resides.
     */
    virtual LSET GetLayerSet() const
    {
        if( m_layer == UNDEFINED_LAYER )
            return LSET();
        else
            return LSET( m_layer );
    }

    virtual void SetLayerSet( LSET aLayers )
    {
        if( aLayers.count() == 1 )
        {
            SetLayer( aLayers.Seq()[0] );
            return;
        }

        wxFAIL_MSG( wxT( "Attempted to SetLayerSet() on a single-layer object." ) );

        // Derived classes which support multiple layers must implement this
    }

    /**
     * Set the layer this item is on.
     *
     * This method is virtual because some items (in fact: class DIMENSION)
     * have a slightly different initialization.
     *
     * @param aLayer The layer number.
     */
    virtual void SetLayer( PCB_LAYER_ID aLayer )
    {
        m_layer = aLayer;
    }

    /**
     * Create a copy of this #BOARD_ITEM.
     */
    virtual BOARD_ITEM* Duplicate() const;

    /**
     * Swap data between \a aItem and \a aImage.
     *
     * \a aItem and \a aImage should have the same type.
     *
     * Used in undo and redo commands to swap values between an item and its copy.
     * Only values like layer, size .. which are modified by editing are swapped.
     *
     * @param aImage the item image which contains data to swap.
     */
    virtual void SwapData( BOARD_ITEM* aImage );

    /**
     * Test to see if this object is on the given layer.
     *
     * Virtual so objects like #PAD, which reside on multiple layers can do their own form
     * of testing.
     *
     * @param aLayer The layer to test for.
     * @return true if on given layer, else false.
     */
    virtual bool IsOnLayer( PCB_LAYER_ID aLayer ) const
    {
        return m_layer == aLayer;
    }

    virtual bool IsKnockout() const { return m_isKnockout; }
    virtual void SetIsKnockout( bool aKnockout ) { m_isKnockout = aKnockout; }

    virtual bool IsLocked() const;
    virtual void SetLocked( bool aLocked ) { m_isLocked = aLocked; }

    /**
     * Delete this object after removing from its parent if it has one.
     */
    void DeleteStructure();

    /**
     * Move this object.
     *
     * @param aMoveVector the move vector for this object.
     */
    virtual void Move( const VECTOR2I& aMoveVector )
    {
        wxFAIL_MSG( wxT( "virtual BOARD_ITEM::Move called for " ) + GetClass() );
    }

    /**
     * Rotate this object.
     *
     * @param aRotCentre the rotation center point.
     */
    virtual void Rotate( const VECTOR2I& aRotCentre, const EDA_ANGLE& aAngle );

    /**
     * Flip this object, i.e. change the board side for this object.
     *
     * @param aCentre the rotation point.
     * @param aFlipLeftRight mirror across Y axis instead of X (the default).
     */
    virtual void Flip( const VECTOR2I& aCentre, bool aFlipLeftRight );

    /**
     * Return the #BOARD in which this #BOARD_ITEM resides, or NULL if none.
     */
    virtual const BOARD* GetBoard() const;
    virtual BOARD* GetBoard();

    /**
     * Return the name of the PCB layer on which the item resides.
     *
     * @return the layer name associated with this item.
     */
    wxString GetLayerName() const;

    virtual void ViewGetLayers( int aLayers[], int& aCount ) const override;

    /**
     * Convert the item shape to a closed polygon.
     *
     * Used in filling zones calculations.  Circles and arcs are approximated by segments.
     *
     * @param aCornerBuffer a buffer to store the polygon.
     * @param aClearanceValue the clearance around the pad.
     * @param aError the maximum deviation from true circle.
     * @param aErrorLoc should the approximation error be placed outside or inside the polygon?
     * @param ignoreLineWidth used for edge cut items where the line width is only
     *                        for visualization.
     */
    virtual void TransformShapeWithClearanceToPolygon( SHAPE_POLY_SET& aCornerBuffer,
                                                       PCB_LAYER_ID aLayer, int aClearanceValue,
                                                       int aError, ERROR_LOC aErrorLoc,
                                                       bool ignoreLineWidth = false ) const;

    struct ptr_cmp
    {
        bool operator() ( const BOARD_ITEM* a, const BOARD_ITEM* b ) const;
    };

protected:
    /**
     * Return a string (to be shown to the user) describing a layer mask.
     */
    virtual wxString layerMaskDescribe() const;

protected:
    PCB_LAYER_ID    m_layer;
    bool            m_isKnockout;

    bool            m_isLocked;

    PCB_GROUP*      m_group;
};

#ifndef SWIG
DECLARE_ENUM_TO_WXANY( PCB_LAYER_ID );
#endif


/**
 * A singleton item of this class is returned for a weak reference that no longer exists.
 *
 * Its sole purpose is to flag the item as having been deleted.
 */
class DELETED_BOARD_ITEM : public BOARD_ITEM
{
public:
    DELETED_BOARD_ITEM() :
        BOARD_ITEM( nullptr, NOT_USED )
    {}

    wxString GetSelectMenuText( EDA_UNITS aUnits ) const override
    {
        return _( "(Deleted Item)" );
    }

    wxString GetClass() const override
    {
        return wxT( "DELETED_BOARD_ITEM" );
    }

    // pure virtuals:
    void     SetPosition( const VECTOR2I& ) override {}
    VECTOR2I GetPosition() const override { return VECTOR2I( 0, 0 ); }

    static DELETED_BOARD_ITEM* GetInstance()
    {
        static DELETED_BOARD_ITEM* item = nullptr;

        if( !item )
            item = new DELETED_BOARD_ITEM();

        return item;
    }

#if defined(DEBUG)
    void Show( int , std::ostream& ) const override {}
#endif
};


#endif /* BOARD_ITEM_STRUCT_H */
