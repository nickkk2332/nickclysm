#include "power_eoc.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "ability.h"
#include "ability_ai.h"
#include "ability_runtime.h"
#include "condition.h"
#include "coordinates.h"
#include "creature.h"
#include "creature_ref.h"
#include "creature_tracker.h"
#include "dialogue.h"
#include "dialogue_helpers.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "generic_factory.h"
#include "math_parser_diag_value.h"
#include "point.h"
#include "power.h"
#include "power_actor.h"
#include "power_motion.h"
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
    Creature *who = role != "any" && d.has_actor( role == "npc" ) ?
                    d.actor( role == "npc" )->get_creature() : nullptr;
    return who && g && !who->is_fake() && !who->is_hallucination() &&
           g->shared_from( *who ).get() == who ? who : nullptr;
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
        const power_application_admission result = get_powers().apply( request );
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
        const creature_ref ref = creature_ref::bind( *who );
        const std::string resource = id.evaluate( d );
        const double value = amount.evaluate( d );
        if( !integer( value, -1000000, 1000000 ) ) {
            d.set_value( "reason", "invalid_amount" );
            return;
        }
        const power_balance balance = get_powers().balance( *who, resource );
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
        const creature_ref ref = creature_ref::bind( *who );
        const power_cooldown_id group( id.evaluate( d ) );
        const double value = amount.evaluate( d );
        if( !integer( value, 0, 1000 ) ) {
            d.set_value( "reason", "invalid_amount" );
            return;
        }
        const power_charge_view status = get_powers().charges( *who, group );
        if( ref.resolve() != who || !status.reason.empty() ) {
            d.set_value( "reason", status.reason.empty() ? "participant_unavailable" : status.reason );
            return;
        }
        const int recovered = get_powers().recover_charge( *who, group, static_cast<int>( value ) );
        d.set_value( "power_charges_recovered", recovered );
        d.set_value( "reason", "applied" );
    };
}

std::function<void( dialogue & )> power_eoc::fling( const JsonObject &jo, std::string_view member,
        std::string_view )
{
    const JsonObject args = jo.get_object( member );
    const std::string source_role = role( args, "source", "u" );
    const std::string target_role = role( args, "target", "npc" );
    const dbl_or_var velocity = get_dbl_or_var( args, "velocity", true );
    const bool controlled = args.get_bool( "controlled", false );
    const bool hit_characters = args.get_bool( "hit_characters", true );
    return [source_role, target_role, velocity, controlled, hit_characters]( dialogue & d ) {
        Creature *source = actor( d, source_role );
        Creature *target = actor( d, target_role );
        const creature_ref source_ref = source ? creature_ref::bind( *source ) : creature_ref();
        const creature_ref target_ref = target ? creature_ref::bind( *target ) : creature_ref();
        const double speed = velocity.evaluate( d );
        d.set_value( "power_motion_position", target ? target->pos_abs() : tripoint_abs_ms() );
        d.set_value( "power_motion_distance", 0 );
        d.set_value( "power_motion_hp_loss", 0 );
        d.set_value( "power_motion_impacts", 0 );
        d.set_value( "power_motion_fell", 0 );
        if( !integer( speed, 10, 300 ) ) {
            d.set_value( "reason", "invalid_velocity" );
            return;
        }
        const std::string cause = d.get_context().count( "ability_instance" ) ?
                                  "ability:" + d.get_value( "ability_instance" ).str() :
                                  d.get_context().count( "power_application" ) ?
                                  "application:" + d.get_value( "power_application" ).str() : "eoc";
        const power_motion_result out = power_motion::fling( source_ref, target_ref,
                                        static_cast<int>( speed ),
                                        controlled, hit_characters, cause );
        d.set_value( "reason", out.reason );
        d.set_value( "power_motion_distance", rl_dist( out.origin, out.destination ) );
        d.set_value( "power_motion_hp_loss", out.hp_loss );
        d.set_value( "power_motion_impacts", out.impacts.size() );
        d.set_value( "power_motion_fell", out.fell ? 1 : 0 );
        d.set_value( "power_motion_position", out.destination );
    };
}

std::function<void( dialogue & )> power_eoc::summon( const JsonObject &jo, std::string_view member,
        std::string_view )
{
    const JsonObject args = jo.get_object( member );
    const str_or_var id = get_str_or_var( args.get_member( "id" ), "id" );
    const std::string owner_role = role( args, "owner", "u" );
    std::optional<var_info> loc;
    optional( args, false, "loc", loc );
    return [id, owner_role, loc]( dialogue & d ) {
        Creature *owner = actor( d, owner_role );
        const creature_ref owner_ref = owner ? creature_ref::bind( *owner ) : creature_ref();
        const power_summon_id definition( id.evaluate( d ) );
        const auto point = loc ? std::make_optional( read_var_value( *loc, d ).tripoint() ) : std::nullopt;
        const power_summon_admission result = get_power_actors().summon( definition, owner_ref, point );
        d.set_value( "power_bond", std::to_string( result.id ) );
        d.set_value( "reason", result.reason );
    };
}

std::function<void( dialogue & )> power_eoc::summon_command( const JsonObject &jo,
        std::string_view member, std::string_view )
{
    const JsonObject args = jo.get_object( member );
    const str_or_var handle = get_str_or_var( args.get_member( "bond" ), "bond" );
    const str_or_var command = get_str_or_var( args.get_member( "command" ), "command" );
    const std::string owner_role = role( args, "owner", "u" );
    const std::string target_role = role( args, "target", "npc" );
    std::optional<var_info> loc;
    optional( args, false, "loc", loc );
    const bool targeted = args.get_bool( "targeted", false );
    return [handle, command, owner_role, target_role, loc, targeted]( dialogue & d ) {
        Creature *owner = actor( d, owner_role );
        Creature *target = actor( d, target_role );
        const creature_ref owner_ref = owner ? creature_ref::bind( *owner ) : creature_ref();
        creature_ref target_ref = target ? creature_ref::bind( *target ) : creature_ref();
        int64_t id = 0;
        try {
            const auto text = handle.evaluate( d );
            size_t used = 0;
            id = std::stoll( text, &used );
            if( used != text.size() ) {
                id = 0;
            }
        } catch( const std::invalid_argument & ) {
        } catch( const std::out_of_range & ) {
        }
        const auto order = command.evaluate( d );
        if( targeted && order == "attack" ) {
            if( !owner || !owner->is_avatar() ) {
                d.set_value( "reason", "interactive_target_requires_avatar" );
                return;
            }
            const auto chosen = g->look_around();
            if( !chosen ) {
                d.set_value( "reason", "target_cancelled" );
                return;
            }
            Creature *selected = get_creature_tracker().creature_at( *chosen );
            target_ref = selected ? creature_ref::bind( *selected ) : creature_ref();
        }
        if( order == "dismiss" ) {
            bool ended = false;
            for( power_summon_view const &view : get_power_actors().inspect() ) {
                if( view.id == id && view.owner == owner_ref ) {
                    ended = get_power_actors().end( id, "dismissed" );
                }
            }
            d.set_value( "reason", ended ? "dismissed" : "not_found" );
        } else {
            const auto point = loc ? std::make_optional( read_var_value( *loc, d ).tripoint() ) : std::nullopt;
            d.set_value( "reason", get_power_actors().command( id, owner_ref, order, target_ref, point ) );
        }
    };
}

std::function<void( dialogue & )> power_eoc::summon_dismiss( const JsonObject &jo,
        std::string_view member, std::string_view )
{
    const JsonObject args = jo.get_object( member );
    str_or_var id;
    optional( args, false, "id", id, "" );
    const std::string owner_role = role( args, "owner", "u" );
    return [id, owner_role]( dialogue & d ) {
        Creature *owner = actor( d, owner_role );
        const creature_ref ref = owner ? creature_ref::bind( *owner ) : creature_ref();
        const int count = owner ? get_power_actors().dismiss( ref,
                          power_summon_id( id.evaluate( d ) ) ) : 0;
        d.set_value( "power_summons_dismissed", count );
        d.set_value( "reason", count ? "dismissed" : "not_found" );
    };
}

std::function<void( dialogue & )> power_eoc::choose_ability( const JsonObject &jo,
        std::string_view member, std::string_view )
{
    const JsonObject args = jo.get_object( member );
    std::vector<ability_id> ids;
    args.read( "abilities", ids, true );
    if( ids.empty() || ids.size() > 16 ) {
        args.throw_error_at( "abilities", "expected 1 to 16 abilities" );
    }
    return [ids]( dialogue & d ) {
        Creature *source = actor( d, "u" );
        Creature *target = actor( d, "npc" );
        const auto request = ability_ai::choose( ids,
                             source ? creature_ref::bind( *source ) : creature_ref(),
                             target ? creature_ref::bind( *target ) : creature_ref() );
        ability_admission result;
        if( request ) {
            ability_request with_context = *request;
            with_context.context = d.get_context();
            result = g->abilities().request( with_context );
        } else {
            result.reason = "no_eligible_ability";
        }
        d.set_value( "ability_instance", std::to_string( result.id ) );
        d.set_value( "reason", result.reason );
    };
}
