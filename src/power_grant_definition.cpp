#include "power.h"

#include <set>

#include "bodypart.h"
#include "debug.h"
#include "effect.h"
#include "flexbuffer_json.h"
#include "itype.h"

bool power_grant_definition::empty() const
{
    return effects.empty() && equipment.empty() && !form;
}

void power_grant_definition::load( const JsonObject &jo )
{
    if( jo.has_member( "effects" ) ) {
        effects.clear();
        std::set<std::pair<efftype_id, bodypart_str_id>> seen;
        for( const JsonObject entry : jo.get_array( "effects" ) ) {
            power_effect_grant grant;
            entry.read( "id", grant.effect, true );
            entry.read( "bodypart", grant.bodypart );
            grant.intensity = entry.get_int( "intensity", 1 );
            if( grant.intensity < 1 || grant.intensity > 1000000 || effects.size() >= 16 ||
                !seen.emplace( grant.effect, grant.bodypart ).second ) {
                entry.throw_error( "Invalid or duplicate effect grant; at most 16" );
            }
            effects.push_back( grant );
        }
    }
    if( jo.has_member( "equipment" ) ) {
        equipment.clear();
        bool wielded = false;
        for( const JsonObject entry : jo.get_array( "equipment" ) ) {
            power_equipment_grant grant;
            entry.read( "item", grant.item, true );
            grant.slot = entry.get_string( "slot", "worn" );
            if( equipment.size() >= 8 || ( grant.slot != "worn" && grant.slot != "wielded" ) ||
                ( grant.slot == "wielded" && wielded ) ) {
                entry.throw_error( "At most eight equipment grants and one wielded item" );
            }
            wielded = wielded || grant.slot == "wielded";
            equipment.push_back( grant );
        }
    }
    if( jo.has_object( "form" ) ) {
        const auto options = jo.get_object( "form" );
        const bool inherited = form;
        stow_worn = options.get_bool( "stow_worn", inherited ? stow_worn : true );
        stow_weapon = options.get_bool( "stow_weapon", inherited ? stow_weapon : true );
        form = true;
    } else if( jo.has_member( "form" ) ) {
        if( !jo.has_bool( "form" ) || jo.get_bool( "form" ) ) {
            jo.throw_error_at( "form", "expected a form object or false" );
        }
        form = stow_worn = stow_weapon = false;
    }
}

void power_grant_definition::check( const std::string &id ) const
{
    for( const auto &grant : effects ) {
        if( !grant.effect.is_valid() || grant.effect.str() == "knocked_out" ||
            ( grant.bodypart != bodypart_str_id::NULL_ID() && !grant.bodypart.is_valid() ) ||
            ( grant.effect.is_valid() && grant.intensity > effect( &*grant.effect ).get_max_intensity() ) ) {
            debugmsg( "Application %s has invalid effect grant %s", id, grant.effect.str() );
        }
    }
    for( const auto &grant : equipment ) {
        if( !grant.item.is_valid() || ( grant.slot == "worn" && !grant.item->armor ) ||
            ( grant.item.is_valid() && ( grant.item->count_by_charges() || grant.item->comestible ||
                                         grant.item->gun || grant.item->tool ) ) ) {
            debugmsg( "Application %s requires nonconsumable armor or melee equipment: %s", id,
                      grant.item.str() );
        }
    }
}
