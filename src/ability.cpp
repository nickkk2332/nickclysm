#include "ability.h"

#include "debug.h"
#include "effect_on_condition.h"
#include "flexbuffer_json.h"
#include "generic_factory.h"
#include "magic.h"

namespace
{
generic_factory<ability_definition> factory( "ability" );

int bounded( const JsonObject &jo, const std::string &key, int fallback, int low, int high )
{
    const int value = jo.get_int( key, fallback );
    if( value < low || value > high ) {
        jo.throw_error_at( key, "integer outside supported bounds" );
    }
    return value;
}

void read_eocs( const JsonObject &jo, const std::string &key, const std::string &source,
                std::vector<effect_on_condition_id> &out )
{
    if( !jo.has_member( key ) ) {
        return;
    }
    out.clear();
    for( JsonValue entry : jo.get_array( key ) ) {
        if( out.size() == 32 ) {
            jo.throw_error_at( key, "at most 32 EOCs per hook" );
        }
        out.push_back( effect_on_conditions::load_inline_eoc( entry, source ) );
    }
}
} // namespace

template<>
const ability_definition &ability_id::obj() const
{
    return factory.obj( *this );
}

template<>
bool ability_id::is_valid() const
{
    return factory.is_valid( *this );
}

void ability_definition::load( const JsonObject &jo, std::string_view source )
{
    mandatory( jo, was_loaded, "name", name );
    revision = bounded( jo, "revision", revision, 1, 1000000 );
    if( jo.has_array( "windup_ticks" ) ) {
        const JsonArray values = jo.get_array( "windup_ticks" );
        if( values.size() != 2 ) {
            jo.throw_error_at( "windup_ticks", "expected two inclusive bounds" );
        }
        windup_min = values.get_int( 0 );
        windup_max = values.get_int( 1 );
    } else if( jo.has_member( "windup_ticks" ) ) {
        windup_min = windup_max = jo.get_int( "windup_ticks" );
    }
    if( windup_min < 0 || windup_max < windup_min || windup_max > 600 ) {
        jo.throw_error_at( "windup_ticks", "expected 0 <= minimum <= maximum <= 600" );
    }
    range = bounded( jo, "range", range, 0, 60 );
    target = jo.get_string( "target", target );
    if( target != "creature" && target != "point" && target != "self" ) {
        jo.throw_error_at( "target", "expected creature, point or self" );
    }
    line_of_sight = jo.get_bool( "line_of_sight", line_of_sight );
    interrupt_damage = jo.get_bool( "interrupt_damage", interrupt_damage );
    interrupt_displacement = jo.get_bool( "interrupt_displacement", interrupt_displacement );
    allow_effect_only = jo.get_bool( "allow_effect_only", allow_effect_only );
    move_cost = bounded( jo, "move_cost", move_cost, 0, 1000000 );
    if( jo.has_object( "energy_cost" ) ) {
        const JsonObject cost = jo.get_object( "energy_cost" );
        energy = cost.get_string( "resource" );
        energy_cost = bounded( cost, "amount", 0, 0, 1000000 );
    }
    if( energy != "none" && energy != "mana" && energy != "stamina" ) {
        jo.throw_error_at( "energy_cost", "supported resources: none, mana, stamina" );
    }
    if( energy == "none" && energy_cost != 0 ) {
        jo.throw_error_at( "energy_cost", "none cannot carry a nonzero cost" );
    }
    if( jo.has_object( "spell" ) ) {
        const JsonObject effect = jo.get_object( "spell" );
        spell = spell_id( effect.get_string( "id" ) );
        spell_level = bounded( effect, "level", 0, 0, 100 );
        eocs.clear();
    }
    if( jo.has_member( "eocs" ) ) {
        if( jo.has_member( "spell" ) ) {
            jo.throw_error( "choose one release adapter: spell or eocs" );
        }
        spell = spell_id();
        read_eocs( jo, "eocs", std::string( source ), eocs );
    }
    if( spell.is_empty() && eocs.empty() ) {
        jo.throw_error( "an ability needs a spell or nonempty eocs release adapter" );
    }
    read_eocs( jo, "on_start", std::string( source ), on_start );
    read_eocs( jo, "before_release", std::string( source ), before_release );
    read_eocs( jo, "on_complete", std::string( source ), on_complete );
    read_eocs( jo, "on_interrupt", std::string( source ), on_interrupt );
    read_eocs( jo, "on_end", std::string( source ), on_end );
    jo.read( "locals", locals );
    if( locals.size() > 128 ) {
        jo.throw_error_at( "locals", "at most 128 instance locals" );
    }
}

void ability_definition::check() const
{
    if( !spell.is_empty() && !spell.is_valid() ) {
        debugmsg( "Ability %s has invalid spell %s", id.str(), spell.str() );
    }
    for( const auto *list : {
             &eocs, &on_start, &before_release, &on_complete, &on_interrupt, &on_end
         } ) {
        for( const effect_on_condition_id &eoc : *list ) {
            if( !eoc.is_valid() || eoc->type != ACTIVATION ) {
                debugmsg( "Ability %s needs an ACTIVATION EOC: %s", id.str(), eoc.str() );
            }
        }
    }
}

void abilities::load( const JsonObject &jo, const std::string &source )
{
    factory.load( jo, source );
}

void abilities::reset()
{
    factory.reset();
}

void abilities::finalize()
{
    factory.finalize();
}

void abilities::check()
{
    factory.check();
}
