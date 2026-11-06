#include "power_eoc.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "creature.h"
#include "condition.h"
#include "generic_factory.h"
#include "dialogue.h"
#include "dialogue_helpers.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "power_runtime.h"
#include "talker.h"

namespace
{
std::string role( const JsonObject &jo, const std::string &key, const std::string &fallback,
                  bool any = false )
{
    const std::string out = jo.get_string( key, fallback );
    if( out != "u" && out != "npc" && !( any && out == "any" ) ) {
        jo.throw_error_at( key, "expected u or npc (any is allowed for an owner filter)" );
    }
    return out;
}

Creature *actor( dialogue &d, const std::string &role )
{
    return role != "any" && d.has_actor( role == "npc" ) ?
           d.actor( role == "npc" )->get_creature() : nullptr;
}

bool integer( double value, int low, int high )
{
    return std::isfinite( value ) && value >= low && value <= high && std::floor( value ) == value;
}
} // namespace

std::function<void( dialogue & )> power_eoc::apply( const JsonObject &jo, std::string_view member,
        std::string_view )
{
    const JsonObject args = jo.get_object( member );
    const str_or_var id = get_str_or_var( args.get_member( "id" ), "id" );
    const std::string owner_role = role( args, "owner", "u" );
    const std::string target_role = role( args, "target", "u" );
    std::optional<dbl_or_var> magnitude;
    if( args.has_member( "magnitude" ) ) {
        magnitude = get_dbl_or_var( args, "magnitude", true );
    }
    return [id, owner_role, target_role, magnitude]( dialogue & d ) {
        d.set_value( "power_application", "0" );
        Creature *owner = actor( d, owner_role );
        Creature *target = actor( d, target_role );
        const auto owner_hold = owner ? g->shared_from( *owner ) : nullptr;
        const auto target_hold = target ? g->shared_from( *target ) : nullptr;
        if( !owner_hold || !target_hold ) {
            d.set_value( "reason", "participant_unavailable" );
            return;
        }
        power_application_request request;
        request.owner = creature_ref::bind( *owner );
        request.target = creature_ref::bind( *target );
        request.application = power_application_id( id.evaluate( d ) );
        if( magnitude ) {
            request.magnitude = magnitude->evaluate( d );
        }
        request.context = d.get_context();
        const auto result = get_powers().apply( request );
        d.set_value( "power_application", std::to_string( result.id ) );
        d.set_value( "reason", result.reason );
    };
}

std::function<void( dialogue & )> power_eoc::end( const JsonObject &jo, std::string_view member,
        std::string_view )
{
    const str_or_var id = get_str_or_var( jo.get_member( member ), member );
    return [id]( dialogue & d ) {
        bool ended = false;
        try {
            const std::string text = id.evaluate( d );
            size_t consumed = 0;
            const int64_t handle = std::stoll( text, &consumed );
            if( consumed == text.size() ) {
                ended = get_powers().end( handle );
            }
        } catch( const std::invalid_argument & ) {
        } catch( const std::out_of_range & ) {
        }
        d.set_value( "reason", ended ? "dispelled" : "not_found" );
    };
}

std::function<void( dialogue & )> power_eoc::dispel( const JsonObject &jo, std::string_view member,
        std::string_view )
{
    const JsonObject args = jo.get_object( member );
    str_or_var id;
    str_or_var tag;
    optional( args, false, "id", id, "" );
    optional( args, false, "tag", tag, "" );
    const std::string target_role = role( args, "target", "u" );
    const std::string owner_role = role( args, "owner", "u", true );
    return [id, tag, target_role, owner_role]( dialogue & d ) {
        Creature *target = actor( d, target_role );
        Creature *owner = actor( d, owner_role );
        const auto target_hold = target ? g->shared_from( *target ) : nullptr;
        const auto owner_hold = owner ? g->shared_from( *owner ) : nullptr;
        d.set_value( "power_dispelled", 0.0 );
        if( !target_hold || ( owner_role != "any" && !owner_hold ) ) {
            d.set_value( "reason", "participant_unavailable" );
            return;
        }
        const creature_ref target_ref = creature_ref::bind( *target );
        const std::optional<creature_ref> owner_ref = owner ?
                std::make_optional( creature_ref::bind( *owner ) ) : std::nullopt;
        const power_application_id filter( id.evaluate( d ) );
        const std::string tag_filter = tag.evaluate( d );
        if( target_ref.resolve_loaded() != target ||
            ( owner_ref && owner_ref->resolve_loaded() != owner ) ) {
            d.set_value( "reason", "participant_unavailable" );
            return;
        }
        const int count = get_powers().dispel( target_ref, filter, owner_ref, tag_filter );
        d.set_value( "power_dispelled", count );
        d.set_value( "reason", count ? "dispelled" : "not_found" );
    };
}

std::function<void( dialogue & )> power_eoc::resource( const JsonObject &jo,
        std::string_view member,
        std::string_view )
{
    const JsonObject args = jo.get_object( member );
    const str_or_var id = get_str_or_var( args.get_member( "id" ), "id" );
    const std::string target_role = role( args, "actor", "u" );
    const dbl_or_var amount = get_dbl_or_var( args, "amount", true );
    return [id, target_role, amount]( dialogue & d ) {
        Creature *who = actor( d, target_role );
        const auto hold = who ? g->shared_from( *who ) : nullptr;
        d.set_value( "power_resource_changed", 0.0 );
        if( !hold ) {
            d.set_value( "reason", "participant_unavailable" );
            return;
        }
        const auto ref = creature_ref::bind( *who );
        const std::string resource = id.evaluate( d );
        const double value = amount.evaluate( d );
        if( !integer( value, -1000000, 1000000 ) ) {
            d.set_value( "reason", "invalid_amount" );
            return;
        }
        const auto balance = get_powers().balance( *who, resource );
        if( ref.resolve() != who || !balance.reason.empty() || resource == "none" ) {
            d.set_value( "reason", balance.reason.empty() ? "participant_unavailable" : balance.reason );
            return;
        }
        const int changed = get_powers().modify( *who, resource, static_cast<int>( value ), "eoc" );
        d.set_value( "power_resource_changed", changed );
        d.set_value( "reason", "applied" );
    };
}

std::function<void( dialogue & )> power_eoc::charge( const JsonObject &jo, std::string_view member,
        std::string_view )
{
    const JsonObject args = jo.get_object( member );
    const str_or_var id = get_str_or_var( args.get_member( "id" ), "id" );
    const std::string target_role = role( args, "actor", "u" );
    const dbl_or_var amount = get_dbl_or_var( args, "amount", false, 1 );
    return [id, target_role, amount]( dialogue & d ) {
        Creature *who = actor( d, target_role );
        const auto hold = who ? g->shared_from( *who ) : nullptr;
        d.set_value( "power_charges_recovered", 0.0 );
        if( !hold ) {
            d.set_value( "reason", "participant_unavailable" );
            return;
        }
        const auto ref = creature_ref::bind( *who );
        const power_cooldown_id group( id.evaluate( d ) );
        const double value = amount.evaluate( d );
        if( !integer( value, 0, 1000 ) ) {
            d.set_value( "reason", "invalid_amount" );
            return;
        }
        const auto status = get_powers().charges( *who, group );
        if( ref.resolve() != who || !status.reason.empty() ) {
            d.set_value( "reason", status.reason.empty() ? "participant_unavailable" : status.reason );
            return;
        }
        const int recovered = get_powers().recover_charge( *who, group, static_cast<int>( value ) );
        d.set_value( "power_charges_recovered", recovered );
        d.set_value( "reason", "applied" );
    };
}
