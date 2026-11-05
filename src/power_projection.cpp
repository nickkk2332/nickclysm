#include "power_projection.h"

#include <cmath>

#include "bodypart.h"
#include "debug.h"
#include "effect_on_condition.h"
#include "flexbuffer_json.h"
#include "generic_factory.h"

namespace
{
generic_factory<power_projection> definitions( "power projection" );
int bounded( const JsonObject &jo, const std::string &key, int fallback, int low, int high )
{
    const int value = jo.get_int( key, fallback );
    if( value < low || value > high ) {
        jo.throw_error_at( key, "integer outside supported bounds" );
    }
    return value;
}
void hooks( const JsonObject &jo, const std::string &key, const std::string &source,
            std::vector<effect_on_condition_id> &out )
{
    if( !jo.has_member( key ) ) {
        return;
    }
    out.clear();
    for( const JsonValue &value : jo.get_array( key ) ) {
        if( out.size() >= 16 ) {
            jo.throw_error_at( key, "at most 16 hooks" );
        }
        out.push_back( effect_on_conditions::load_inline_eoc( value, source ) );
    }
}
}
template<> const power_projection &power_projection_id::obj() const
{
    return definitions.obj( *this );
}
template<> bool power_projection_id::is_valid() const
{
    return definitions.is_valid( *this );
}

void power_projection::load( const JsonObject &jo, std::string_view source )
{
    mandatory( jo, was_loaded, "name", name );
    kind = jo.get_string( "kind", kind );
    if( kind != "projectile" && kind != "beam" && kind != "area" ) {
        jo.throw_error_at( "kind", "expected projectile, beam or area" );
    }
    revision = bounded( jo, "revision", revision, 1, 1000000 );
    range = bounded( jo, "range", range, 1, 60 );
    radius = bounded( jo, "radius", radius, 0, 10 );
    speed = bounded( jo, "speed", speed, 1, 10 );
    strength = bounded( jo, "strength", strength, 1, 1000000 );
    duration_ticks = bounded( jo, "duration_ticks", duration_ticks, 1, 1000000 );
    charge_ticks = bounded( jo, "charge_ticks", charge_ticks, 0, 600 );
    if( charge_ticks >= duration_ticks ) {
        jo.throw_error( "charge must finish before expiry" );
    }
    interval_ticks = bounded( jo, "interval_ticks", interval_ticks, 1, 3600 );
    max_per_owner = bounded( jo, "max_per_owner", max_per_owner, 1, 128 );
    track_target = jo.get_bool( "track_target", track_target );
    follow_owner = jo.get_bool( "follow_owner", follow_owner );
    intercepts = jo.get_bool( "intercepts", intercepts );
    contest = jo.get_bool( "contest", contest );
    line_of_sight = jo.get_bool( "line_of_sight", line_of_sight );
    interrupt_damage = jo.get_bool( "interrupt_damage", interrupt_damage );
    interrupt_displacement = jo.get_bool( "interrupt_displacement", interrupt_displacement );
    interrupt_incapacitated = jo.get_bool( "interrupt_incapacitated", interrupt_incapacitated );
    if( ( kind == "projectile" && follow_owner ) || ( kind != "area" && intercepts ) ||
        ( kind == "area" && track_target ) ) {
        jo.throw_error( "incompatible geometry policy" );
    }
    affects = jo.get_string( "affects", affects );
    if( affects != "hostile" && affects != "friendly" && affects != "all" ) {
        jo.throw_error_at( "affects", "expected hostile, friendly or all" );
    }
    jo.read( "bodypart", bodypart );
    jo.read( "damage", damage );
    if( damage.damage_units.size() > 16 ) {
        jo.throw_error_at( "damage", "at most 16 damage components" );
    }
    double total = 0;
    for( const auto &unit : damage ) {
        for( const double value : {
                 unit.amount, unit.res_pen, unit.res_mult, unit.damage_multiplier,
                 unit.unconditional_res_mult, unit.unconditional_damage_mult
             } ) {
            if( !std::isfinite( value ) || value < 0 || value > 1000000 ) {
                jo.throw_error_at( "damage", "damage values must be finite and between 0 and 1000000" );
            }
        }
        total += unit.amount * unit.damage_multiplier * unit.unconditional_damage_mult;
        if( !unit.barrels.empty() ) {
            jo.throw_error_at( "damage", "barrel scaling is not supported" );
        }
    }
    if( total > 1000000 ) {
        jo.throw_error_at( "damage", "packet exceeds 1000000" );
    }
    if( jo.has_object( "start_cost" ) ) {
        start_cost.load( jo.get_object( "start_cost" ) );
    }
    if( jo.has_object( "upkeep" ) ) {
        upkeep.load( jo.get_object( "upkeep" ) );
    }
    jo.read( "tags", tags );
    if( tags.size() > 32 ) {
        jo.throw_error_at( "tags", "at most 32 tags" );
    }
    hooks( jo, "on_start", std::string( source ), on_start );
    hooks( jo, "on_release", std::string( source ), on_release );
    hooks( jo, "on_enter", std::string( source ), on_enter );
    hooks( jo, "on_stay", std::string( source ), on_stay );
    hooks( jo, "on_exit", std::string( source ), on_exit );
    hooks( jo, "on_hit", std::string( source ), on_hit );
    hooks( jo, "on_clash", std::string( source ), on_clash );
    hooks( jo, "on_end", std::string( source ), on_end );
    jo.read( "locals", locals );
    if( locals.size() > 64 ) {
        jo.throw_error_at( "locals", "at most 64 locals" );
    }
    for( const auto &entry : locals ) {
        if( ( !entry.second.is_dbl() && !entry.second.is_str() && !entry.second.is_tripoint() ) ||
            ( entry.second.is_dbl() && !std::isfinite( entry.second.dbl() ) ) ||
            entry.first.rfind( "projection_", 0 ) == 0 || entry.first == "power_projection" ) {
            jo.throw_error_at( "locals",
                               "locals must be finite numbers, strings or positions with non-reserved names" );
        }
    }
}
void power_projection::check() const
{
    if( !bodypart.is_valid() ) {
        debugmsg( "Projection %s has invalid bodypart", id.str() );
    }
    for( const auto &unit : damage ) {
        if( !unit.type.is_valid() ) {
            debugmsg( "Projection %s has invalid damage type", id.str() );
        }
    }
    for( const auto *cost : {
             &start_cost, &upkeep
         } ) {
        if( !powers::valid_resource( cost->resource ) ) {
            debugmsg( "Projection %s has invalid resource", id.str() );
        }
    }
    for( const auto *list : {
             &on_start, &on_release, &on_enter, &on_stay, &on_exit, &on_hit, &on_clash, &on_end
         } ) {
        for( const auto &eoc : *list ) {
            if( !eoc.is_valid() || eoc->type != ACTIVATION ) {
                debugmsg( "Projection %s needs ACTIVATION hooks", id.str() );
            }
        }
    }
}
void power_projections::load( const JsonObject &jo, const std::string &source )
{
    definitions.load( jo, source );
}
void power_projections::reset()
{
    definitions.reset();
}
void power_projections::finalize()
{
    definitions.finalize();
}
void power_projections::check()
{
    definitions.check();
}
