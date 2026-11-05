#include "power_math.h"

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
    return d.has_actor( beta ) ? d.const_actor( beta )->get_const_creature() : nullptr;
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
