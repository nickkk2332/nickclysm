#include "power.h"

#include <cmath>
#include <initializer_list>

#include "creature.h"
#include "debug.h"
#include "effect_on_condition.h"
#include "flexbuffer_json.h"
#include "generic_factory.h"
#include "power_actor.h"
#include "power_projection.h"

namespace
{
generic_factory<power_resource> resources( "power resource" );
generic_factory<power_cooldown> cooldowns( "power cooldown" );
generic_factory<power_application> applications( "power application" );

int bounded( const JsonObject &jo, const std::string_view key, int fallback, int low = 0,
             int high = 1000000 )
{
    const int value = jo.get_int( key, fallback );
    if( value < low || value > high ) {
        jo.throw_error_at( key, "integer outside supported bounds" );
    }
    return value;
}

void read_eocs( const JsonObject &jo, const std::string_view key, const std::string_view source,
                std::vector<effect_on_condition_id> &out )
{
    if( !jo.has_member( key ) ) {
        return;
    }
    out.clear();
    for( JsonValue value : jo.get_array( key ) ) {
        if( out.size() == 16 ) {
            jo.throw_error_at( key, "at most 16 EOCs per application hook" );
        }
        out.push_back( effect_on_conditions::load_inline_eoc( value, source ) );
    }
}
} // namespace

template<> const power_resource &power_resource_id::obj() const
{
    return resources.obj( *this );
}
template<> bool power_resource_id::is_valid() const
{
    return resources.is_valid( *this );
}
template<> const power_cooldown &power_cooldown_id::obj() const
{
    return cooldowns.obj( *this );
}
template<> bool power_cooldown_id::is_valid() const
{
    return cooldowns.is_valid( *this );
}
template<> const power_application &power_application_id::obj() const
{
    return applications.obj( *this );
}
template<> bool power_application_id::is_valid() const
{
    return applications.is_valid( *this );
}

void power_cost::load( const JsonObject &jo )
{
    resource = jo.get_string( "resource" );
    amount = bounded( jo, "amount", 0 );
    if( resource.empty() || ( resource == "none" && amount != 0 ) ) {
        jo.throw_error( "cost requires a resource; none cannot carry a nonzero cost" );
    }
}

void power_resource::load( const JsonObject &jo, std::string_view )
{
    mandatory( jo, was_loaded, "name", name );
    if( id.str() == "none" || id.str() == "mana" || id.str() == "stamina" ) {
        jo.throw_error( "native resource names are reserved" );
    }
    capacity = bounded( jo, "capacity", capacity, 1 );
    initial = bounded( jo, "initial", was_loaded ? initial : capacity, 0, capacity );
    regen_amount = bounded( jo, "regen_amount", regen_amount );
    regen_ticks = bounded( jo, "regen_ticks", regen_ticks, 1 );
    jo.read( "actor_kinds", actor_kinds );
    for( const std::string &kind : actor_kinds ) {
        if( kind != "avatar" && kind != "npc" && kind != "monster" ) {
            jo.throw_error_at( "actor_kinds", "expected avatar, npc or monster" );
        }
    }
}

void power_resource::check() const {}

void power_cooldown::load( const JsonObject &jo, std::string_view )
{
    mandatory( jo, was_loaded, "name", name );
    max_charges = bounded( jo, "max_charges", max_charges, 1, 1000 );
    initial_charges = bounded( jo, "initial_charges", was_loaded ? initial_charges : max_charges,
                               0, max_charges );
    recharge_ticks = bounded( jo, "recharge_ticks", recharge_ticks, 1 );
    cooldown_ticks = bounded( jo, "cooldown_ticks", cooldown_ticks );
}

void power_cooldown::check() const {}

void power_application::load( const JsonObject &jo, std::string_view source )
{
    mandatory( jo, was_loaded, "name", name );
    revision = bounded( jo, "revision", revision, 1 );
    duration_ticks = bounded( jo, "duration_ticks", duration_ticks );
    interval_ticks = bounded( jo, "interval_ticks", interval_ticks, 1, 3600 );
    max_per_owner = bounded( jo, "max_per_owner", max_per_owner, 1, 128 );
    magnitude = jo.get_float( "magnitude", magnitude );
    if( !std::isfinite( magnitude ) || magnitude < 0 || magnitude > 1000000 ) {
        jo.throw_error_at( "magnitude", "expected a finite magnitude between 0 and 1000000" );
    }
    jo.read( "tags", tags );
    if( tags.size() > 32 ) {
        jo.throw_error_at( "tags", "at most 32 application tags" );
    }
    interrupt_damage = jo.get_bool( "interrupt_damage", interrupt_damage );
    interrupt_incapacitated = jo.get_bool( "interrupt_incapacitated", interrupt_incapacitated );
    interrupt_displacement = jo.get_bool( "interrupt_displacement", interrupt_displacement );
    if( jo.has_object( "start_cost" ) ) {
        start_cost.load( jo.get_object( "start_cost" ) );
    }
    if( jo.has_object( "upkeep" ) ) {
        upkeep.load( jo.get_object( "upkeep" ) );
    }
    if( jo.has_object( "grants" ) ) {
        grants.load( jo.get_object( "grants" ) );
    }
    read_eocs( jo, "on_start", std::string( source ), on_start );
    read_eocs( jo, "on_tick", std::string( source ), on_tick );
    read_eocs( jo, "on_end", std::string( source ), on_end );
    jo.read( "locals", locals );
    if( locals.size() > 64 ) {
        jo.throw_error_at( "locals", "at most 64 application locals" );
    }
}

void power_application::check() const
{
    grants.check( id.str() );
    for( const auto *cost : {
             &start_cost, &upkeep
         } ) {
        if( !powers::valid_resource( cost->resource ) ) {
            debugmsg( "Application %s has invalid resource %s", id.str(), cost->resource );
        }
    }
    for( const auto *list : {
             &on_start, &on_tick, &on_end
         } ) {
        for( const auto &eoc : *list ) {
            if( !eoc.is_valid() || eoc->type != ACTIVATION ) {
                debugmsg( "Application %s needs ACTIVATION EOC %s", id.str(), eoc.str() );
            }
        }
    }
}

void powers::load_resource( const JsonObject &jo, const std::string &source )
{
    resources.load( jo, source );
}
void powers::load_cooldown( const JsonObject &jo, const std::string &source )
{
    cooldowns.load( jo, source );
}
void powers::load_application( const JsonObject &jo, const std::string &source )
{
    applications.load( jo, source );
}
void powers::reset()
{
    power_summons::reset();
    power_projections::reset();
    applications.reset();
    cooldowns.reset();
    resources.reset();
}
void powers::finalize()
{
    resources.finalize();
    cooldowns.finalize();
    applications.finalize();
    power_summons::finalize();
    power_projections::finalize();
}
void powers::check()
{
    resources.check();
    cooldowns.check();
    applications.check();
    power_summons::check();
    power_projections::check();
}
bool powers::valid_resource( const std::string &resource )
{
    return resource == "none" || resource == "mana" || resource == "stamina" ||
           power_resource_id( resource ).is_valid();
}
std::string powers::actor_kind( const Creature &who )
{
    return who.is_avatar() ? "avatar" : who.is_npc() ? "npc" : who.is_monster() ? "monster" :
           "untracked";
}
