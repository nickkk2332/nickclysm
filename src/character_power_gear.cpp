#include "character.h"

#include <utility>

#include "character_attire.h"
#include "crafting.h"
#include "event.h"
#include "event_bus.h"
#include "item.h"
#include "item_location.h"
#include "ranged.h"

std::string Character::check_power_stow( bool clothing, bool held ) const
{
    if( clothing ) {
        if( worn.worn.size() > 64 ) {
            return "wardrobe_budget";
        }
        for( const item &value : worn.worn ) {
            if( !const_cast<Character *>( this )->can_takeoff( value, &worn.worn ).success() ) {
                return "clothing_locked";
            }
            if( !value.items_with( []( const item & nested ) {
            return nested.has_var( "power_grant_application" );
            } ).empty() || value.has_var( "power_grant_application" ) ) {
                return "borrowed_equipment";
            }
        }
    }
    if( held && !weapon.is_null() ) {
        if( !can_unwield( weapon ).success() ) {
            return "weapon_locked";
        }
        if( weapon.has_var( "power_grant_application" ) ||
        !weapon.items_with( []( const item & nested ) {
        return nested.has_var( "power_grant_application" );
        } ).empty() ) {
            return "borrowed_equipment";
        }
    }
    return {};
}

void Character::stow_power_gear( bool clothing, bool held, std::list<item> &garments,
                                 std::optional<item> &held_item )
{
    if( clothing ) {
        // Commit the transfer before native item callbacks; no copy changes UIDs.
        garments.splice( garments.end(), worn.worn );
        for( item &value : garments ) {
            const side original_side = value.get_side();
            value.on_takeoff( *this );
            if( value.is_sided() ) {
                value.set_side( original_side );
            }
        }
    }
    if( held && !weapon.is_null() ) {
        held_item.emplace( std::move( weapon ) );
        weapon = item();
    }
    cached_info.erase( "weapon_value" );
    recalculate_enchantment_cache();
    invalidate_inventory_validity_cache();
    invalidate_weight_carried_cache();
    invalidate_leak_level_cache();
    recalc_sight_limits();
    calc_encumbrance();
    calc_discomfort();
    worn.recalc_ablative_blocking( this );
}

bool Character::wear_power_item( item &value )
{
    if( !can_wear( value ).success() ) {
        return false;
    }
    invalidate_inventory_validity_cache();
    invalidate_leak_level_cache();
    worn.wear_power_item( *this, value );
    return true;
}

bool Character::wield_power_item( item &value )
{
    if( !weapon.is_null() || !can_wield( value ).success() ) {
        return false;
    }
    invalidate_inventory_validity_cache();
    invalidate_weight_carried_cache();
    invalidate_leak_level_cache();
    weapon = std::move( value );
    cached_info.erase( "weapon_value" );
    recoil = MAX_RECOIL;
    item_location loc = get_wielded_item();
    if( loc ) {
        craft_relocated( loc );
        loc->on_wield( *this, false );
    }
    get_event_bus().send<event_type::character_wields_item>( getID(), weapon.typeId() );
    return true;
}
