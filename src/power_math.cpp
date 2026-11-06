#include "power_math.h"
#include "ability_ai.h"
#include "power_actor.h"
#include "game.h"

#include <optional>
#include <string>

#include "creature.h"
#include "dialogue.h"
#include "math_parser_type.h"
#include "power_runtime.h"
#include "talker.h"

namespace
{
const Creature *actor( const const_dialogue &d, char scope )
{
    const bool beta = scope == 'n';
    const Creature *who = d.has_actor( beta ) ? d.const_actor( beta )->get_const_creature() : nullptr;
    return who && g && !who->is_fake() && !who->is_hallucination() &&
           g->shared_from( *who ).get() == who ? who : nullptr;
}
} // namespace

double power_math::resource( const const_dialogue &d, char scope,
                             const std::vector<diag_value> &params, const diag_kwargs &kwargs )
{
    const Creature *who = actor( d, scope );
    if( !who ) {
        return 0;
    }
    const auto status = get_powers().balance( *who, params[0].str( d ) );
    const std::string field = kwargs.kwarg_or( "field", "available" ).str( d );
    if( field == "supported" ) {
        return status.reason.empty() ? 1 : 0;
    }
    if( field != "available" && field != "total" && field != "reserved" && field != "capacity" ) {
        throw math::runtime_error( "Unknown power_resource field %s", field );
    }
    return !status.reason.empty() ? 0 : field == "total" ? status.total : field == "reserved" ?
           status.reserved : field == "capacity" ? status.capacity : status.available();
}

double power_math::cooldown( const const_dialogue &d, char scope,
                             const std::vector<diag_value> &params, const diag_kwargs &kwargs )
{
    const Creature *who = actor( d, scope );
    if( !who ) {
        return 0;
    }
    const auto status = get_powers().charges( *who, power_cooldown_id( params[0].str( d ) ) );
    const std::string field = kwargs.kwarg_or( "field", "charges" ).str( d );
    if( field == "supported" ) {
        return status.reason.empty() ? 1 : 0;
    }
    if( field != "charges" && field != "capacity" && field != "remaining" && field != "recharge" ) {
        throw math::runtime_error( "Unknown power_cooldown field %s", field );
    }
    return !status.reason.empty() ? 0 : field == "capacity" ? status.capacity : field == "remaining" ?
           status.cooldown_remaining : field == "recharge" ? status.recharge_remaining : status.charges;
}

double power_math::application( const const_dialogue &d, char scope,
                                const std::vector<diag_value> &params, const diag_kwargs &kwargs )
{
    const Creature *who = actor( d, scope );
    if( !who ) {
        return 0;
    }
    const std::string field = kwargs.kwarg_or( "field", "count" ).str( d );
    const std::string owner_role = kwargs.kwarg_or( "owner", "any" ).str( d );
    const std::string tag = kwargs.kwarg_or( "tag", "" ).str( d );
    if( field != "count" && field != "magnitude" ) {
        throw math::runtime_error( "Unknown power_application field %s", field );
    }
    std::optional<creature_ref> owner;
    if( owner_role != "any" ) {
        if( owner_role != "u" && owner_role != "npc" ) {
            throw math::runtime_error( "Unknown power_application owner role %s", owner_role );
        }
        const Creature *source = actor( d, owner_role == "npc" ? 'n' : 'u' );
        if( !source ) {
            return 0;
        }
        owner = creature_ref::bind( *source );
    }
    return get_powers().application_value( *who, power_application_id( params[0].str( d ) ),
                                           field == "magnitude", owner, tag );
}


double power_math::ability( const const_dialogue &d, char scope,
                            const std::vector<diag_value> &params, const diag_kwargs &kwargs )
{
    const Creature *caster = actor( d, scope );
    const Creature *target = actor( d, scope == 'u' ? 'n' : 'u' );
    ability_request request;
    request.ability = ability_id( params[0].str( d ) );
    if( caster ) {
        request.caster = creature_ref::bind( *caster );
    }
    if( target ) {
        request.target = creature_ref::bind( *target );
    }
    const auto result = ability_ai::query( request );
    const std::string field = kwargs.kwarg_or( "field", "eligible" ).str( d );
    if( field == "eligible" ) {
        return result.eligible() ? 1 : 0;
    }
    if( field == "score" ) {
        return result.eligible() ? result.score : 0;
    }
    if( field == "magnitude" ) {
        return result.magnitude;
    }
    if( field == "energy_cost" ) {
        return result.energy_cost;
    }
    if( field == "move_cost" ) {
        return result.move_cost;
    }
    if( field == "windup_min" ) {
        return result.windup_min;
    }
    if( field == "windup_max" ) {
        return result.windup_max;
    }
    throw math::runtime_error( "Unknown ability_query field %s", field );
}

double power_math::summon( const const_dialogue &d, char scope,
                           const std::vector<diag_value> &params, const diag_kwargs & )
{
    const Creature *owner = actor( d, scope );
    if( !owner ) {
        return 0;
    }
    const auto ref = creature_ref::bind( *owner );
    const power_summon_id filter( params[0].str( d ) );
    int count = 0;
    for( const auto &v : get_power_actors().inspect() ) {
        if( v.state == "active" && v.owner == ref && v.owner.resolve() == owner && v.summon.resolve() &&
            calendar::turn < v.expires && ( filter.is_empty() || filter == v.definition ) ) {
            ++count;
        }
    }
    return count;
}
