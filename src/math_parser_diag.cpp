#include "math_parser_diag.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "achievement.h"
#include "basecamp.h"
#include "behavior_oracle.h"
#include "bionics.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_compiler_support.h"
#include "cata_utility.h"
#include "cata_variant.h"
#include "character.h"
#include "character_attire.h"
#include "character_id.h"
#include "character_oracle.h"
#include "city.h"
#include "condition.h"
#include "coordinates.h"
#include "creature.h"
#include "damage.h"
#include "debug.h"
#include "dialogue.h"
#include "dispersion.h"
#include "effect.h"
#include "enum_conversions.h"
#include "enums.h"
#include "faction.h"
#include "field.h"
#include "game.h"
#include "game_constants.h"
#include "input_popup.h"
#include "item.h"
#include "item_location.h"
#include "item_pocket.h"
#include "itype.h"
#include "magic.h"
#include "magic_enchantment.h"
#include "map.h"
#include "map_iterator.h"
#include "melee.h"
#include "math_parser_diag_value.h"
#include "math_parser_type.h"
#include "mod_manager.h"
#include "mod_setting.h"
#include "mongroup.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "npc_json_goal.h"
#include "npctalk.h"
#include "options.h"
#include "overmapbuffer.h"
#include "past_achievements_info.h"
#include "pimpl.h"
#include "player_activity.h"
#include "point.h"
#include "power_math.h"
#include "proficiency.h"
#include "recipe.h"
#include "relic.h"
#include "scent_map.h"
#include "skill.h"
#include "stats_tracker.h"
#include "stomach.h"
#include "talker.h"
#include "translations.h"
#include "type_id.h"
#include "units.h"
#include "value_ptr.h"
#include "veh_type.h"
#include "vehicle.h"
#include "weather.h"
#include "weather_gen.h"
#include "weather_type.h"
#include "worldfactory.h"
#include "wound.h"

class event_statistic;

/*
General guidelines for writing dialogue functions

- Don't validate the number of arguments (params). The math parser already does that
- Only use variadic functions if all arguments are treated the same way,
  regardless of how many there are (including zero)
- Use kwargs for optional arguments
- Prefer splitting functions instead of using mandatory kwargs
  ex: school_level() split from spell_level() instead of spell_level('school':blorg)
- throw math::runtime_error for all errors
*/

static const itype_id itype_battery( "battery" );

static const json_character_flag json_flag_MUTATION_THRESHOLD( "MUTATION_THRESHOLD" );

namespace
{
bool is_beta( char scope )
{
    switch( scope ) {
        case 'n':
            return true;
        case 'u':
        default:
            return false;
    }
}

constexpr bool is_true( double dbl )
{
    return dbl >= 1 || float_equals( dbl, 1 );
}

template <typename T>
constexpr std::string_view _str_type_of()
{
    if constexpr( std::is_same_v<T, units::energy> ) {
        return "energy";
    } else if constexpr( std::is_same_v<T, time_duration> ) {
        return "time";
    }
    return "cookies";
}

template <typename T, size_t N>
T _read_from_string( std::string_view s,
                     const std::array<std::pair<std::string_view, T>, N> &units )
{
    // TODO: LAMBDA_NORETURN_CLANG21x1 can be replaced with [[noreturn]] once we switch to C++23 on all compilers
    auto const error = [s]( char const * suffix, size_t /* offset */ ) LAMBDA_NORETURN_CLANG21x1 {
        throw math::runtime_error( R"(Failed to convert "%s" to a %s value: %s)", s,
                                   _str_type_of<T>(), suffix );
    };
    return detail::read_from_json_string_common<T>( s, units, error );
}

double u_val( const_dialogue const &d, char scope, std::vector<diag_value> const &params,
              diag_kwargs const & /* kwargs */ )
{
    return conditional_t::get_legacy_dbl( d, params[0].str( d ), scope );
}

void u_val_ass( double val, dialogue &d, char scope, std::vector<diag_value> const &params,
                diag_kwargs const & /* kwargs */ )
{
    conditional_t::set_legacy_dbl( d, val, params[0].str( d ), scope );
}

double option_eval( const_dialogue const &d, char /* scope */,
                    std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    return get_option<float>( params[0].str( d ), true );
}

// A literal id must name a loaded mod setting, one math can read as a number
const mod_setting *literal_mod_setting( std::vector<diag_value> const &params )
{
    if( !params[0].is_str() ) {
        return nullptr;
    }
    const mod_setting_id id( params[0].str() );
    if( !id.is_valid() ) {
        throw math::syntax_error( R"(mod_setting(): no mod defines the setting "%s")", id.str() );
    }
    if( id->kind == mod_setting::kind_t::text ) {
        throw math::syntax_error(
            R"(mod_setting(): "%s" is text, which isn't a number.  Read it with mod_setting_is or the mod_setting string mutator)",
            id.str() );
    }
    return &*id;
}

void mod_setting_check( std::vector<diag_value> const &params )
{
    literal_mod_setting( params );
}

void mod_setting_assign_check( std::vector<diag_value> const &params )
{
    const mod_setting *setting = literal_mod_setting( params );
    if( setting != nullptr && !setting->eoc_writable ) {
        throw math::syntax_error(
            R"(mod_setting(): EOCs can't change "%s": its mod_setting doesn't set "eoc_writable")",
            setting->id.str() );
    }
}

const mod_setting &runtime_mod_setting( const_dialogue const &d,
                                        std::vector<diag_value> const &params )
{
    const mod_setting_id id( params[0].str( d ) );
    if( !id.is_valid() ) {
        throw math::runtime_error( R"(mod_setting(): no mod defines the setting "%s")", id.str() );
    }
    return *id;
}

// mod_setting('id'): a number; a switch is 0 or 1, a choice its index (doc/JSON/MOD_SETTINGS.md)
double mod_setting_eval( const_dialogue const &d, char /* scope */,
                         std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    const mod_setting &setting = runtime_mod_setting( d, params );
    const std::optional<double> value = setting.as_number( mod_settings::get( setting ) );
    if( !value ) {
        throw math::runtime_error( R"(mod_setting(): "%s" is text, which isn't a number)",
                                   setting.id.str() );
    }
    return *value;
}

// mod_setting('id') = v: clamped and snapped to the step; a choice by index
void mod_setting_ass( double val, dialogue &d, char /* scope */,
                      std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    const mod_setting &setting = runtime_mod_setting( d, params );
    if( !setting.eoc_writable ) {
        throw math::runtime_error(
            R"(mod_setting(): EOCs can't change "%s": its mod_setting doesn't set "eoc_writable")",
            setting.id.str() );
    }
    const std::string error = mod_settings::set( setting, val );
    if( !error.empty() ) {
        throw math::runtime_error( "mod_setting(): %s", error );
    }
}

double event_statistic_eval( const_dialogue const &d, char /* scope */,
                             std::vector<diag_value> const &params,
                             diag_kwargs const & /* kwargs */ )
{
    string_id<event_statistic> stat_id( params[0].str( d ) );
    if( !stat_id.is_valid() ) {
        throw math::runtime_error( R"(Unknown event_statistic "%s")", stat_id.str() );
    }
    cata_variant val = get_stats().value_of( stat_id );
    return diag_value( val ).dbl( d );
}

// EVT-J05: -1 not tracked in this game, 0 pending, 1 completed, 2 failed
double achievement_state_eval( const_dialogue const &d, char /* scope */,
                               std::vector<diag_value> const &params,
                               diag_kwargs const & /* kwargs */ )
{
    const achievement_id id( params[0].str( d ) );
    if( !id.is_valid() ) {
        throw math::runtime_error( R"(Unknown achievement or conduct "%s")", id.str() );
    }
    const achievements_tracker &achievements = get_achievements();
    if( !achievements.is_tracked( id ) ) {
        return -1;
    }
    switch( achievements.is_completed( id ) ) {
        case achievement_completion::completed:
            return 1;
        case achievement_completion::failed:
            return 2;
        case achievement_completion::pending:
        case achievement_completion::last:
            break;
    }
    return 0;
}

// EVT-J05: 1 when a past game (any world) completed the achievement, as unlocks read it
double past_achievement_eval( const_dialogue const &d, char /* scope */,
                              std::vector<diag_value> const &params,
                              diag_kwargs const & /* kwargs */ )
{
    const achievement_id id( params[0].str( d ) );
    if( !id.is_valid() ) {
        throw math::runtime_error( R"(Unknown achievement or conduct "%s")", id.str() );
    }
    return get_past_achievements().is_completed( id ) ? 1 : 0;
}

double addiction_intensity_eval( const_dialogue const &d, char scope,
                                 std::vector<diag_value> const &params,
                                 diag_kwargs const & /* kwargs */ )
{
    return d.const_actor( is_beta( scope ) )
           ->get_addiction_intensity( addiction_id( params[0].str( d ) ) );
}

void addiction_intensity_ass( double val, dialogue &d, char scope,
                              std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    d.actor( is_beta( scope ) )->set_addiction_intensity( addiction_id( params[0].str( d ) ),
            static_cast<int>( std::lround( val ) ) );
}

double addiction_turns_eval( const_dialogue const &d, char scope,
                             std::vector<diag_value> const &params,
                             diag_kwargs const & /* kwargs */ )
{
    return d.const_actor( is_beta( scope ) )
           ->get_addiction_turns( addiction_id( params[0].str( d ) ) );
}

void addiction_turns_ass( double val, dialogue &d, char scope,
                          std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    d.actor( is_beta( scope ) )->set_addiction_turns( addiction_id( params[0].str( d ) ), val );
}

double health_eval( const_dialogue const &d, char scope,
                    std::vector<diag_value> const & /* params */, diag_kwargs const & /* kwargs */ )
{
    return d.const_actor( is_beta( scope ) )->get_health();
}

void health_ass( double val, dialogue &d, char scope, std::vector<diag_value> const & /* params */,
                 diag_kwargs const & /* kwargs */ )
{
    const int current_health = d.actor( is_beta( scope ) )->get_health();
    d.actor( is_beta( scope ) )->mod_livestyle( val - current_health );
}

double daily_health_eval( const_dialogue const &d, char scope,
                          std::vector<diag_value> const & /* params */, diag_kwargs const & /* kwargs */ )
{
    return d.const_actor( is_beta( scope ) )->get_daily_health();
}

void daily_health_ass( double val, dialogue &d, char scope,
                       std::vector<diag_value> const & /* params */, diag_kwargs const &kwargs )
{
    talker *actor = d.actor( is_beta( scope ) );
    const int delta = val - actor->get_daily_health();
    // Match normal daily-health modifiers, with a directional cap that works
    // even when the optional keyword is omitted.
    const int cap = kwargs.kwarg_or( "cap", delta < 0 ? -200 : 200 ).dbl( d );
    actor->mod_daily_health( delta, cap );
}

double hunger_eval( const_dialogue const &d, char scope,
                    std::vector<diag_value> const & /* params */, diag_kwargs const & /* kwargs */ )
{
    return d.const_actor( is_beta( scope ) )->get_hunger();
}

void hunger_ass( double val, dialogue &d, char scope,
                 std::vector<diag_value> const & /* params */, diag_kwargs const & /* kwargs */ )
{
    d.actor( is_beta( scope ) )->set_hunger( val );
}

double armor_eval( const_dialogue const &d, char scope, std::vector<diag_value> const &params,
                   diag_kwargs const &kwargs )
{
    damage_type_id dt( params[0].str( d ) );
    bodypart_id bp( params[1].str( d ) );
    const_talker const *actor = d.const_actor( is_beta( scope ) );
    // An item: the protection it gives its wearer on that part.  The roll is the coverage roll
    // the materials are checked against: 0 counts every material of the part, 99 only those
    // covering all of it.
    if( item_location const *it = actor->get_const_item(); it != nullptr && *it ) {
        if( !( *it )->covers( bp ) ) {
            return 0.0;
        }
        const int roll = clamp( static_cast<int>( kwargs.kwarg_or( "roll", 0 ).dbl( d ) ), 0, 99 );
        return ( *it )->resist( dt, false, bp, roll );
    }
    // A character: everything that protects them in combat (worn armor, armor bonuses, bionics,
    // mutations, the body part itself), or only what they wear
    const std::string source = kwargs.kwarg_or( "source", "all" ).str( d );
    if( source == "worn" ) {
        Character const *guy = actor->get_const_character();
        return guy != nullptr ? guy->worn.damage_resist( dt, bp ) : 0.0;
    }
    if( source != "all" ) {
        throw math::runtime_error( R"(armor(): source must be "all" or "worn", not "%s")", source );
    }
    return actor->armor_at( dt, bp );
}

double arm_strength_eval( const_dialogue const &d, char scope,
                          std::vector<diag_value> const & /* params */,
                          diag_kwargs const & /* kwargs */ )
{
    const_talker const *actor = d.const_actor( is_beta( scope ) );
    if( Character const *const guy = actor->get_const_character() ) {
        return guy->get_arm_str();
    }
    return actor->str_cur();
}

double charge_count_eval( const_dialogue const &d, char scope,
                          std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    return d.const_actor( is_beta( scope ) )->charges_of( itype_id( params[0].str( d ) ) );
}

double coverage_eval( const_dialogue const &d, char scope, std::vector<diag_value> const &params,
                      diag_kwargs const & /* kwargs */ )
{
    bodypart_id bp( params[0].str( d ) );
    return d.const_actor( is_beta( scope ) )->coverage_at( bp );
}

itype_id resolve_eats_like( const itype_id &id )
{
    if( id->comestible && !id->comestible->eats_like.is_empty() ) {
        return id->comestible->eats_like;
    }
    return id;
}

double consumption_count_eval( const_dialogue const &d, char scope,
                               std::vector<diag_value> const &params,
                               diag_kwargs const &kwargs )
{
    const Character *ch = d.const_actor( is_beta( scope ) )->get_const_character();
    if( !ch ) {
        throw math::runtime_error( "consumption_count() requires a character" );
    }
    const itype_id target_id( params[0].str( d ) );
    const itype_id canonical = resolve_eats_like( target_id );
    diag_value hours_val = kwargs.kwarg_or( "hours" );
    const time_duration window = hours_val.is_empty()
                                 ? 48_hours
                                 : time_duration::from_hours( hours_val.dbl( d ) );
    int count = 0;
    for( const consumption_event &event : ch->consumption_history ) {
        if( event.time > calendar::turn - window &&
            resolve_eats_like( event.type_id ) == canonical ) {
            ++count;
        }
    }
    return count;
}

double consumption_count_total_eval( const_dialogue const &d, char scope,
                                     std::vector<diag_value> const & /* params */,
                                     diag_kwargs const &kwargs )
{
    const Character *ch = d.const_actor( is_beta( scope ) )->get_const_character();
    if( !ch ) {
        throw math::runtime_error( "consumption_count_total() requires a character" );
    }
    diag_value hours_val = kwargs.kwarg_or( "hours" );
    const time_duration window = hours_val.is_empty()
                                 ? 48_hours
                                 : time_duration::from_hours( hours_val.dbl( d ) );
    int count = 0;
    for( const consumption_event &event : ch->consumption_history ) {
        if( event.time > calendar::turn - window ) {
            ++count;
        }
    }
    return count;
}

// HLTH-J03: how the food tastes to the one eating it: the other talker, when it is a character
double food_fun_eval( const_dialogue const &d, char scope,
                      std::vector<diag_value> const & /* params */,
                      diag_kwargs const & /* kwargs */ )
{
    const bool beta = is_beta( scope );
    item_location const *loc = d.const_actor( beta )->get_const_item();
    if( loc == nullptr || !*loc || !( *loc )->is_comestible() ) {
        return 0;
    }
    const Character *eater = d.has_actor( !beta ) ?
                             d.const_actor( !beta )->get_const_character() : nullptr;
    if( eater == nullptr ) {
        return ( *loc )->get_comestible_fun();
    }
    return eater->fun_for( **loc ).first;
}

double light_level_eval( const_dialogue const &d, char /* scope */,
                         std::vector<diag_value> const &params, diag_kwargs const &/* kwargs */ )
{
    const map &here = get_map();
    tripoint_abs_ms loc_val = params[0].tripoint( d );

    return static_cast<double>( here.light_at( here.get_bub( loc_val ) ) );
}

// SPAT-J05: the noise meter, the loudest sound heard this turn on or next to the character
double noise_level_eval( const_dialogue const &d, char scope,
                         std::vector<diag_value> const & /* params */, diag_kwargs const & /* kwargs */ )
{
    const Character *guy = d.const_actor( is_beta( scope ) )->get_const_character();
    return guy != nullptr ? guy->volume : 0;
}

// SPAT-J07: the scent left at a location, 0 outside the reality bubble. Setting it keeps the one
// scent type the bubble has.
constexpr int max_eoc_scent = 10000;

double scent_eval( const_dialogue const &d, char /* scope */,
                   std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    return get_scent().get( get_map().get_bub( params[0].tripoint( d ) ) );
}

void scent_ass( double val, dialogue &d, char /* scope */, std::vector<diag_value> const &params,
                diag_kwargs const & /* kwargs */ )
{
    const int value = static_cast<int>( std::lround( std::clamp( val, 0.0,
                                        static_cast<double>( max_eoc_scent ) ) ) );
    get_scent().set( get_map().get_bub( params[0].tripoint( d ) ), value );
}

double distance_eval( const_dialogue const &d, char /* scope */,
                      std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    const auto get_pos = [&d]( diag_value const & dv ) {
        if( dv.is_str() ) {
            std::string const &str = dv.str( d );
            if( str == "u" ) {
                return d.const_actor( false )->pos_abs();
            } else if( str == "npc" ) {
                return d.const_actor( true )->pos_abs();
            }
            throw math::runtime_error( R"(Invalid distance actor "%s")", str );
        }
        return dv.tripoint( d );
    };
    return rl_dist( get_pos( params[0] ), get_pos( params[1] ) );
}

// OMAP-J01: overmap functions. A location argument is a location variable, or "u" / "npc" for
// that talker's position, as in distance().
tripoint_abs_omt _omt_of( const_dialogue const &d, diag_value const &dv )
{
    if( dv.is_str() ) {
        std::string const &str = dv.str( d );
        if( str == "u" ) {
            return d.const_actor( false )->pos_abs_omt();
        } else if( str == "npc" ) {
            return d.const_actor( true )->pos_abs_omt();
        }
        throw math::runtime_error( R"(Invalid location "%s")", str );
    }
    return project_to<coords::omt>( dv.tripoint( d ) );
}

double omt_distance_eval( const_dialogue const &d, char /* scope */,
                          std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    return rl_dist( _omt_of( d, params[0] ), _omt_of( d, params[1] ) );
}

double horde_size_eval( const_dialogue const &d, char /* scope */,
                        std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    const tripoint_abs_omt center = _omt_of( d, params[0] );
    const int radius = std::max( 0, static_cast<int>( kwargs.kwarg_or( "radius", 0 ).dbl( d ) ) );
    int total = 0;
    for( const tripoint_abs_omt &p : points_in_radius( center, radius ) ) {
        total += overmap_buffer.get_horde_size( p );
    }
    return total;
}

// The closest city matching the kwargs of city_distance() and city_size()
city_reference _closest_city( const_dialogue const &d, std::vector<diag_value> const &params,
                              diag_kwargs const &kwargs )
{
    city_filter filter;
    filter.known_only = is_true( kwargs.kwarg_or( "known", 1 ).dbl( d ) );
    filter.min_size = static_cast<int>( kwargs.kwarg_or( "min_size", 0 ).dbl( d ) );
    filter.max_size = static_cast<int>( kwargs.kwarg_or( "max_size", -1 ).dbl( d ) );
    filter.max_distance = static_cast<int>( kwargs.kwarg_or( "max_distance", -1 ).dbl( d ) );
    diag_value const name = kwargs.kwarg_or( "name" );
    if( !name.is_empty() ) {
        filter.name = name.str( d );
    }
    return overmap_buffer.closest_city( project_to<coords::sm>( _omt_of( d, params[0] ) ), filter );
}

double city_distance_eval( const_dialogue const &d, char /* scope */,
                           std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    const city_reference city = _closest_city( d, params, kwargs );
    if( !city ) {
        return -1;
    }
    return rl_dist( project_to<coords::omt>( city.abs_sm_pos ).xy(), _omt_of( d, params[0] ).xy() );
}

double city_size_eval( const_dialogue const &d, char /* scope */,
                       std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    const city_reference city = _closest_city( d, params, kwargs );
    return city ? city.city->size : 0;
}

double artifact_resonance_eval( const_dialogue const &d, char scope,
                                std::vector<diag_value> const & /* params */,
                                diag_kwargs const & /* kwargs */ )
{
    return d.const_actor( is_beta( scope ) )->get_artifact_resonance();
}

double damage_level_eval( const_dialogue const &d, char scope,
                          std::vector<diag_value> const & /* params */,
                          diag_kwargs const & /* kwargs */ )
{
    item_location const *it = d.const_actor( is_beta( scope ) )->get_const_item();
    // an item that is gone (used up earlier in the same EOC) is no item
    if( it == nullptr || !*it ) {
        throw math::runtime_error( "subject of damage_level() must be an item" );
    }
    return ( *it )->damage_level();
}

// MATH-J07: the item a function reads, or an error naming the function for another talker
item const &_talker_item( const_dialogue const &d, char scope, std::string_view func )
{
    item_location const *it = d.const_actor( is_beta( scope ) )->get_const_item();
    if( it == nullptr || !*it ) {
        throw math::runtime_error( "subject of %s() must be an item", func );
    }
    return *it->get_item();
}

item &_talker_item( dialogue &d, char scope, std::string_view func )
{
    item_location *it = d.actor( is_beta( scope ) )->get_item();
    if( it == nullptr || !*it ) {
        throw math::runtime_error( "subject of %s() must be an item", func );
    }
    return *it->get_item();
}

// item_temp(): in kelvin, as weather('temperature'); 0 for an item whose temperature isn't kept
double item_temp_eval( const_dialogue const &d, char scope,
                       std::vector<diag_value> const & /* params */,
                       diag_kwargs const & /* kwargs */ )
{
    item const &it = _talker_item( d, scope, "item_temp" );
    return it.has_temperature() ? units::to_kelvin( it.temperature ) : 0;
}

void item_temp_ass( double val, dialogue &d, char scope,
                    std::vector<diag_value> const & /* params */, diag_kwargs const & /* kwargs */ )
{
    item &it = _talker_item( d, scope, "item_temp" );
    if( it.has_temperature() ) {
        it.set_item_temperature( units::from_kelvin( std::max( 0.0, val ) ) );
    }
}

// rot(): in seconds; 'format': 'relative' is the fraction of the shelf life (1 is rotten).
// 0 for an item that doesn't spoil, which ignores assignment
double rot_eval( const_dialogue const &d, char scope, std::vector<diag_value> const & /* params */,
                 diag_kwargs const &kwargs )
{
    item const &it = _talker_item( d, scope, "rot" );
    if( !it.goes_bad() ) {
        return 0;
    }
    std::string const format = kwargs.kwarg_or( "format" ).str( d );
    if( format == "relative" ) {
        return it.get_relative_rot();
    }
    if( !format.empty() ) {
        throw math::runtime_error( R"(rot(): unknown format "%s")", format );
    }
    return to_turns<double>( it.get_rot() );
}

void rot_ass( double val, dialogue &d, char scope, std::vector<diag_value> const & /* params */,
              diag_kwargs const &kwargs )
{
    item &it = _talker_item( d, scope, "rot" );
    if( !it.goes_bad() ) {
        return;
    }
    std::string const format = kwargs.kwarg_or( "format" ).str( d );
    if( format == "relative" ) {
        it.set_relative_rot( val );
    } else if( format.empty() ) {
        it.set_rot( time_duration::from_turns( static_cast<int>( std::lround( val ) ) ) );
    } else {
        throw math::runtime_error( R"(rot(): unknown format "%s")", format );
    }
}

// ammo_count(): the ammo loaded in the item, of any type (a gun's rounds, a tool's charges)
double ammo_count_eval( const_dialogue const &d, char scope,
                        std::vector<diag_value> const & /* params */,
                        diag_kwargs const & /* kwargs */ )
{
    return _talker_item( d, scope, "ammo_count" ).ammo_remaining();
}

// add_effect keeps a main_parts_only effect on the main part, so read and write it there,
// or setting it on a hand would add a second effect instead of changing the first
bodypart_id effect_bodypart( const efftype_id &id, const std::string &bp_str )
{
    if( bp_str.empty() ) {
        return bodypart_str_id::NULL_ID();
    }
    const bodypart_id bp( bp_str );
    if( !bp.is_valid() ) {
        return bp;
    }
    return id.is_valid() && id->get_main_parts() ? bodypart_id( bp->main_part ) : bp;
}

double effect_intensity_eval( const_dialogue const &d, char scope,
                              std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    const efftype_id id( params[0].str( d ) );
    bodypart_id const bp = effect_bodypart( id, kwargs.kwarg_or( "bodypart" ).str( d ) );
    effect target = d.const_actor( is_beta( scope ) )->get_effect( id, bp );
    return target.is_null() ? -1 : target.get_intensity();
}

double limb_score_eval( const_dialogue const &d, char scope,
                        std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    const limb_score_id ls( params[0].str( d ) );
    // 'type' is optional: when omitted, sum the score over all body part types
    std::string const type_str = kwargs.kwarg_or( "type" ).str( d );
    bp_type bp_t = bp_type::num_types;
    if( !type_str.empty() ) {
        std::optional<bp_type> const parsed = io::string_to_enum_optional<bp_type>( type_str );
        if( !parsed ) {
            throw math::runtime_error( "Unknown body part type '%s' in limb_score()", type_str );
        }
        bp_t = *parsed;
    }

    return d.const_actor( is_beta( scope ) )->get_limb_score( ls, bp_t );
}

double encumbrance_eval( const_dialogue const &d, char scope, std::vector<diag_value> const &params,
                         diag_kwargs const & /* kwargs */ )
{
    bodypart_id bp( params[0].str( d ) );
    return d.const_actor( is_beta( scope ) )->encumbrance_at( bp );
}

// The faction a faction_*() function names. An unknown id is a math error, not a crash.
faction *_get_faction( const_dialogue const &d, diag_value const &id_val )
{
    std::string const &id = id_val.str( d );
    faction *fac = g->faction_manager_ptr->get( faction_id( id ), false );
    if( fac == nullptr ) {
        throw math::runtime_error( R"(Unknown faction "%s")", id );
    }
    return fac;
}

double faction_like_eval( const_dialogue const &d, char /* scope */,
                          std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    faction *fac = _get_faction( d, params[0] );
    return fac->likes_u;
}

void faction_like_ass( double val, dialogue &d, char /* scope */,
                       std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    faction *fac = _get_faction( d, params[0] );
    fac->likes_u = val;
}

double faction_respect_eval( const_dialogue const &d, char /* scope */,
                             std::vector<diag_value> const &params,
                             diag_kwargs const & /* kwargs */ )
{
    faction *fac = _get_faction( d, params[0] );
    return fac->respects_u;
}

void faction_respect_ass( double val, dialogue &d, char /* scope */,
                          std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    faction *fac = _get_faction( d, params[0] );
    fac->respects_u = val;
}

double faction_trust_eval( const_dialogue const &d, char /* scope */,
                           std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    faction *fac = _get_faction( d, params[0] );
    return fac->trusts_u;
}

void faction_trust_ass( double val, dialogue &d, char /* scope */,
                        std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    faction *fac = _get_faction( d, params[0] );
    fac->trusts_u = val;
}

double faction_food_supply_eval( const_dialogue const &d, char /* scope */,
                                 std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    diag_value vit_val = kwargs.kwarg_or( "vitamin" );
    faction *fac = _get_faction( d, params[0] );
    if( !vit_val.is_empty() ) {
        return static_cast<double>(
                   fac->food_supply().get_vitamin( vitamin_id( vit_val.str( d ) ) ) );
    }
    return static_cast<double>( fac->food_supply().calories );
}

// faction_food_supply('your_followers') += 50000 puts that many calories (1/1000 kcal) in the
// faction's larder and -= takes them out ('vitamin': the units of that vitamin instead); what
// is added never goes bad, or goes bad after 'expires_in' (CAMP-J08)
void faction_food_supply_ass( double val, dialogue &d, char /* scope */,
                              std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    faction *fac = _get_faction( d, params[0] );
    const diag_value vit_val = kwargs.kwarg_or( "vitamin" );
    const nutrients current = fac->food_supply();
    const int64_t wanted = static_cast<int64_t>( std::round( val ) );
    nutrients change;
    int64_t delta = 0;
    if( !vit_val.is_empty() ) {
        const vitamin_id vit( vit_val.str( d ) );
        delta = wanted - current.get_vitamin( vit );
        change.set_vitamin( vit, static_cast<int>( std::min<int64_t>( std::abs( delta ), INT_MAX ) ) );
    } else {
        delta = wanted - current.calories;
        change.calories = std::abs( delta );
    }
    if( delta > 0 ) {
        time_point expires = calendar::turn_zero;
        const diag_value expires_val = kwargs.kwarg_or( "expires_in" );
        if( !expires_val.is_empty() ) {
            expires = calendar::turn + time_duration::from_seconds( expires_val.dbl( d ) );
        }
        fac->add_to_food_supply( { { expires, change } } );
    } else if( delta < 0 && ( current.calories > 0 || !current.vitamins().empty() ) ) {
        fac->consume_food_supply( change );
    }
}

// The camp a camp_ function asks about: the one at the 'location' kwarg, or else the NPC
// actor's assigned camp, or else the camp at (or within 3 overmap tiles of) the actor;
// nullptr if there is none
basecamp *camp_function_camp( const_dialogue const &d, char scope, diag_kwargs const &kwargs,
                              std::string_view name )
{
    tripoint_abs_omt where;
    const diag_value loc_val = kwargs.kwarg_or( "location" );
    if( !loc_val.is_empty() ) {
        where = project_to<coords::omt>( loc_val.tripoint( d ) );
    } else if( scope == 'g' ) {
        throw math::syntax_error( R"("%s" needs either an actor scope (u/n) or a 'location' kwarg)",
                                  std::string( name ) );
    } else {
        const_talker const *actor = d.const_actor( is_beta( scope ) );
        const npc *guy = actor->get_const_npc();
        where = guy != nullptr && guy->assigned_camp ? *guy->assigned_camp :
                project_to<coords::omt>( actor->pos_abs() );
    }
    std::optional<basecamp *> camp = overmap_buffer.find_camp( where.xy() );
    return camp ? *camp : nullptr;
}

double camp_provides_eval( const_dialogue const &d, char scope,
                           std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    const basecamp *camp = camp_function_camp( d, scope, kwargs, "camp_provides" );
    return camp == nullptr ? 0 : camp->provides_amount( params[0].str( d ) );
}

double camp_expansion_count_eval( const_dialogue const &d, char scope,
                                  std::vector<diag_value> const & /* params */, diag_kwargs const &kwargs )
{
    const basecamp *camp = camp_function_camp( d, scope, kwargs, "camp_expansion_count" );
    if( camp == nullptr ) {
        return 0;
    }
    return static_cast<double>( std::count_if( camp->directions.begin(), camp->directions.end(),
    []( const point_rel_omt & dir ) {
        return dir != base_camps::base_dir;
    } ) );
}

double camp_assigned_npcs_eval( const_dialogue const &d, char scope,
                                std::vector<diag_value> const & /* params */, diag_kwargs const &kwargs )
{
    basecamp *camp = camp_function_camp( d, scope, kwargs, "camp_assigned_npcs" );
    return camp == nullptr ? 0 : static_cast<double>( camp->get_npcs_assigned().size() );
}

// companions out on camp missions from the camp: on the faction_mission named, or on any for ''
double camp_workers_eval( const_dialogue const &d, char scope,
                          std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    const basecamp *camp = camp_function_camp( d, scope, kwargs, "camp_workers" );
    if( camp == nullptr ) {
        return 0;
    }
    const std::string mission = params[0].str( d );
    int count = 0;
    for( const shared_ptr_fast<npc> &guy : overmap_buffer.get_companion_mission_npcs() ) {
        const npc_companion_mission c_mission = guy->get_companion_mission();
        if( c_mission.position == camp->camp_omt_pos() && c_mission.role_id == "FACTION_CAMP" &&
            ( mission.empty() || c_mission.miss_id.type.str() == mission ) ) {
            count++;
        }
    }
    return count;
}

double faction_wealth_eval( const_dialogue const &d, char /* scope */,
                            std::vector<diag_value> const &params,
                            diag_kwargs const & /* kwargs */ )
{
    faction *fac = _get_faction( d, params[0] );
    return fac->wealth;
}

void faction_wealth_ass( double val, dialogue &d, char /* scope */,
                         std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    faction *fac = _get_faction( d, params[0] );
    fac->wealth = val;
}

double faction_power_eval( const_dialogue const &d, char /* scope */,
                           std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    faction *fac = _get_faction( d, params[0] );
    return fac->power;
}

void faction_power_ass( double val, dialogue &d, char /* scope */,
                        std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    faction *fac = _get_faction( d, params[0] );
    fac->power = val;
}

double faction_size_eval( const_dialogue const &d, char /* scope */,
                          std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    faction *fac = _get_faction( d, params[0] );
    return fac->size;
}

void faction_size_ass( double val, dialogue &d, char /* scope */,
                       std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    faction *fac = _get_faction( d, params[0] );
    fac->size = val;
}

// The tile a map function reads or writes: its 'location' kwarg, else the actor's position
tripoint_abs_ms map_function_location( const_dialogue const &d, char scope,
                                       diag_kwargs const &kwargs, std::string_view name )
{
    diag_value loc_val = kwargs.kwarg_or( "location" );
    if( !loc_val.is_empty() ) {
        return loc_val.tripoint( d );
    }
    if( scope == 'g' ) {
        throw math::syntax_error( R"("%s" needs either an actor scope (u/n) or a 'location' kwarg)",
                                  std::string( name ) );
    }
    return d.const_actor( is_beta( scope ) )->pos_abs();
}

// The field of that type at a tile of the reality bubble, if there is a live one
const field_entry *live_field_at( const map &here, const tripoint_abs_ms &loc,
                                  const field_type_id &ft )
{
    if( !here.inbounds( loc ) ) {
        return nullptr;
    }
    const field_entry *fp = here.field_at( here.get_bub( loc ) ).find_field( ft );
    return fp != nullptr && fp->is_field_alive() ? fp : nullptr;
}

double field_strength_eval( const_dialogue const &d, char scope,
                            std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    const tripoint_abs_ms loc = map_function_location( d, scope, kwargs, "field_strength" );
    const field_entry *fp = live_field_at( get_map(), loc, field_type_id( params[0].str( d ) ) );
    return fp ? fp->get_field_intensity() : 0;
}

// field_strength('fd_fire', 'location': loc) = 2 sets the intensity exactly (capped at the
// field's highest), adding the field if it isn't there; 0 or less removes it (MAP-J06)
void field_strength_ass( double val, dialogue &d, char scope, std::vector<diag_value> const &params,
                         diag_kwargs const &kwargs )
{
    const tripoint_abs_ms loc = map_function_location( d, scope, kwargs, "field_strength" );
    map &here = get_map();
    const field_type_str_id ft( params[0].str( d ) );
    if( !here.inbounds( loc ) || !ft.is_valid() || ft.is_null() ) {
        return;
    }
    const tripoint_bub_ms p = here.get_bub( loc );
    const int intensity = static_cast<int>( std::round( val ) );
    if( intensity <= 0 ) {
        here.remove_field( p, ft );
    } else if( live_field_at( here, loc, ft ) != nullptr ) {
        here.set_field_intensity( p, ft, intensity );
    } else {
        // a dead entry would keep its age: start afresh (without hitting whoever stands there)
        here.remove_field( p, ft );
        here.add_field( p, ft, intensity, 0_turns, false );
    }
}

// field_age('fd_smoke', 'location': loc): how old the field is, in turns (-1 with no such field).
// An older field is closer to losing intensity.
double field_age_eval( const_dialogue const &d, char scope, std::vector<diag_value> const &params,
                       diag_kwargs const &kwargs )
{
    const tripoint_abs_ms loc = map_function_location( d, scope, kwargs, "field_age" );
    const field_entry *fp = live_field_at( get_map(), loc, field_type_id( params[0].str( d ) ) );
    return fp ? to_turns<double>( fp->get_field_age() ) : -1.0;
}

void field_age_ass( double val, dialogue &d, char scope, std::vector<diag_value> const &params,
                    diag_kwargs const &kwargs )
{
    const tripoint_abs_ms loc = map_function_location( d, scope, kwargs, "field_age" );
    map &here = get_map();
    const field_type_id ft( params[0].str( d ) );
    if( live_field_at( here, loc, ft ) != nullptr ) {
        here.set_field_age( here.get_bub( loc ), ft,
                            time_duration::from_turns( std::max( 0.0, std::round( val ) ) ) );
    }
}

// map_radiation('location': loc): the radiation of a tile of the reality bubble (0 outside it)
double map_radiation_eval( const_dialogue const &d, char scope,
                           std::vector<diag_value> const & /* params */, diag_kwargs const &kwargs )
{
    const tripoint_abs_ms loc = map_function_location( d, scope, kwargs, "map_radiation" );
    const map &here = get_map();
    return here.inbounds( loc ) ? here.get_radiation( here.get_bub( loc ) ) : 0;
}

void map_radiation_ass( double val, dialogue &d, char scope,
                        std::vector<diag_value> const & /* params */, diag_kwargs const &kwargs )
{
    const tripoint_abs_ms loc = map_function_location( d, scope, kwargs, "map_radiation" );
    map &here = get_map();
    if( here.inbounds( loc ) ) {
        here.set_radiation( here.get_bub( loc ), std::max( 0, static_cast<int>( std::round( val ) ) ) );
    }
}

double gun_damage_eval( const_dialogue const &d, char scope, std::vector<diag_value> const &params,
                        diag_kwargs const & /* kwargs */ )
{

    item_location const *it = d.const_actor( is_beta( scope ) )->get_const_item();
    if( it == nullptr || !*it ) {
        throw math::runtime_error( "subject of gun_damage() must be an item" );
    }
    std::string const &dt_str = params[0].str( d );
    if( dt_str == "ALL" ) {
        return ( *it )->gun_damage( true ).total_damage();
    }
    return ( *it )->gun_damage( true ).type_damage( damage_type_id( dt_str ) );
}

// The item a gun or ammo function is about
item const *gun_talker( const_dialogue const &d, char scope, std::string_view func )
{
    item_location const *it = d.const_actor( is_beta( scope ) )->get_const_item();
    if( it == nullptr || !*it ) {
        throw math::runtime_error( "For %s(), talker must be an item", func );
    }
    return it->get_item();
}

item *gun_talker( dialogue &d, char scope, std::string_view func )
{
    item_location *it = d.actor( is_beta( scope ) )->get_item();
    if( it == nullptr || !*it ) {
        throw math::runtime_error( "For %s(), talker must be an item", func );
    }
    return it->get_item();
}

double ammo_remaining_eval( const_dialogue const &d, char scope,
                            std::vector<diag_value> const & /* params */,
                            diag_kwargs const & /* kwargs */ )
{
    return gun_talker( d, scope, "ammo_remaining" )->ammo_remaining();
}

void ammo_remaining_ass( double val, dialogue &d, char scope,
                         std::vector<diag_value> const & /* params */, diag_kwargs const & /* kwargs */ )
{
    item *it = gun_talker( d, scope, "ammo_remaining" );
    // Refill or empty what it has: its integral magazine or the magazine in it, with the ammo
    // it is loaded with (or its default ammo).  Nothing is added where it has no magazine.
    if( !it->magazine_integral() && it->magazine_current() == nullptr ) {
        return;
    }
    itype_id ammo = it->ammo_current();
    if( ammo.is_null() ) {
        ammo = it->ammo_default();
    }
    if( ammo.is_null() || !ammo.is_valid() || !ammo->ammo ) {
        return;
    }
    const int capacity = it->ammo_capacity( ammo->ammo->type );
    it->ammo_set( ammo, clamp( static_cast<int>( std::lround( val ) ), 0, capacity ) );
}

double ammo_capacity_eval( const_dialogue const &d, char scope,
                           std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    item const *it = gun_talker( d, scope, "ammo_capacity" );
    const std::string &at_str = params[0].str( d );
    ammotype at( at_str );
    if( at_str.empty() ) {
        // The ammo it is loaded with, or its default ammo
        itype_id ammo = it->ammo_current();
        if( ammo.is_null() ) {
            ammo = it->ammo_default();
        }
        if( ammo.is_null() || !ammo.is_valid() || !ammo->ammo ) {
            return 0.0;
        }
        at = ammo->ammo->type;
    } else if( !at.is_valid() ) {
        throw math::runtime_error( "ammo_capacity(): unknown ammo type \"%s\"", at_str );
    }
    return it->ammo_capacity( at );
}

double has_gunmod_eval( const_dialogue const &d, char scope,
                        std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    item const *it = gun_talker( d, scope, "has_gunmod" );
    return it->gunmod_find( itype_id( params[0].str( d ) ) ) != nullptr ? 1.0 : 0.0;
}

double gun_heat_eval( const_dialogue const &d, char scope,
                      std::vector<diag_value> const & /* params */, diag_kwargs const & /* kwargs */ )
{
    return gun_talker( d, scope, "gun_heat" )->get_var( "gun_heat", 0.0 );
}

void gun_heat_ass( double val, dialogue &d, char scope,
                   std::vector<diag_value> const & /* params */, diag_kwargs const & /* kwargs */ )
{
    item *it = gun_talker( d, scope, "gun_heat" );
    if( it->is_gun() ) {
        it->set_var( "gun_heat", std::max( 0.0, val ) );
    }
}

double has_trait_eval( const_dialogue const &d, char scope, std::vector<diag_value> const &params,
                       diag_kwargs const & /* kwargs */ )
{
    return d.const_actor( is_beta( scope ) )->has_trait( trait_id( params[0].str( d ) ) );
}

double sum_traits_of_category_eval( const_dialogue const &d, char scope,
                                    std::vector<diag_value> const &params,
                                    diag_kwargs const &kwargs )
{

    diag_value type = kwargs.kwarg_or( "type", "ALL" );

    mutation_category_id cat = mutation_category_id( params[0].str( d ) );
    std::string const &thing = type.str( d );
    mut_count_type count_type;

    if( thing == "POSITIVE" ) {
        count_type = mut_count_type::POSITIVE;
    } else if( thing == "NEGATIVE" ) {
        count_type = mut_count_type::NEGATIVE;
    } else if( thing == "ALL" ) {
        count_type = mut_count_type::ALL;
    } else {
        throw math::runtime_error( "Incorrect type '%s' in sum_traits_of_category", type.str( d ) );
    }

    return d.const_actor( is_beta( scope ) )->get_total_in_category( cat, count_type );
}

double sum_traits_of_category_char_has_eval( const_dialogue const &d, char scope,
        std::vector<diag_value> const &params,
        diag_kwargs const &kwargs )
{

    diag_value type = kwargs.kwarg_or( "type", "ALL" );

    mutation_category_id cat = mutation_category_id( params[0].str( d ) );
    std::string const &thing = type.str( d );
    mut_count_type count_type;

    if( thing == "POSITIVE" ) {
        count_type = mut_count_type::POSITIVE;
    } else if( thing == "NEGATIVE" ) {
        count_type = mut_count_type::NEGATIVE;
    } else if( thing == "ALL" ) {
        count_type = mut_count_type::ALL;
    } else {
        throw math::runtime_error( "Incorrect type '%s' in sum_traits_of_category", type.str( d ) );
    }

    return d.const_actor( is_beta( scope ) )->get_total_in_category_char_has( cat, count_type );
}

mutation_category_id mutation_category_param( const_dialogue const &d,
        std::vector<diag_value> const &params, std::string_view func )
{
    const mutation_category_id cat( params[0].str( d ) );
    if( !cat.is_valid() ) {
        throw math::runtime_error( R"(%s(): unknown mutation category "%s")", func, cat.str() );
    }
    return cat;
}

trait_id trait_param( const_dialogue const &d, std::vector<diag_value> const &params,
                      std::string_view func )
{
    const trait_id trait( params[0].str( d ) );
    if( !trait.is_valid() ) {
        throw math::runtime_error( R"(%s(): unknown trait "%s")", func, trait.str() );
    }
    return trait;
}

// mutation_instability('ALPHA'): the instability mutating towards the category would weigh;
// 0 for a talker that isn't a character
double mutation_instability_eval( const_dialogue const &d, char scope,
                                  std::vector<diag_value> const &params,
                                  diag_kwargs const & /* kwargs */ )
{
    const mutation_category_id cat = mutation_category_param( d, params, "mutation_instability" );
    const Character *guy = d.const_actor( is_beta( scope ) )->get_const_character();
    return guy == nullptr ? 0.0 : guy->get_instability_per_category( cat );
}

// mutation_category_level('ALPHA'): the strength of the category that gates its threshold and
// dreams; 0 for a talker that isn't a character
double mutation_category_level_eval( const_dialogue const &d, char scope,
                                     std::vector<diag_value> const &params,
                                     diag_kwargs const & /* kwargs */ )
{
    const mutation_category_id cat = mutation_category_param( d, params,
                                     "mutation_category_level" );
    const Character *guy = d.const_actor( is_beta( scope ) )->get_const_character();
    if( guy == nullptr ) {
        return 0.0;
    }
    const auto level = guy->mutation_category_level.find( cat );
    return level == guy->mutation_category_level.end() ? 0.0 : level->second;
}

// trait_cooldown('SHELL2'): turns until the active trait next pays its cost; 0 for a trait the
// character doesn't have, or a talker that isn't a character
double trait_cooldown_eval( const_dialogue const &d, char scope,
                            std::vector<diag_value> const &params,
                            diag_kwargs const & /* kwargs */ )
{
    const trait_id trait = trait_param( d, params, "trait_cooldown" );
    const Character *guy = d.const_actor( is_beta( scope ) )->get_const_character();
    if( guy == nullptr || !guy->has_trait( trait ) ) {
        return 0.0;
    }
    return to_turns<double>( guy->get_cost_timer( trait ) );
}

// a trait the character doesn't have is left alone
void trait_cooldown_ass( double val, dialogue &d, char scope,
                         std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    const trait_id trait = trait_param( d, params, "trait_cooldown" );
    Character *guy = d.actor( is_beta( scope ) )->get_character();
    if( guy == nullptr ) {
        throw math::runtime_error( "trait_cooldown(): talker must be a character" );
    }
    if( guy->has_trait( trait ) ) {
        const double turns = std::clamp( std::round( val ), 0.0,
                                         to_turns<double>( calendar::INDEFINITELY_LONG_DURATION ) );
        guy->set_cost_timer( trait, time_duration::from_turns<double>( turns ) );
    }
}

// UIINV-J02: can_craft('recipe', 'batch': n): 1 if the character could start crafting the recipe
// now (Character::can_craft_now); 0 for a talker that isn't a character
double can_craft_eval( const_dialogue const &d, char scope, std::vector<diag_value> const &params,
                       diag_kwargs const &kwargs )
{
    const recipe_id rec( params[0].str( d ) );
    if( !rec.is_valid() ) {
        throw math::runtime_error( "can_craft(): unknown recipe \"%s\"", rec.str() );
    }
    const Character *guy = d.const_actor( is_beta( scope ) )->get_const_character();
    if( guy == nullptr ) {
        return 0.0;
    }
    const int batch = static_cast<int>( kwargs.kwarg_or( "batch", 1 ).dbl( d ) );
    return guy->can_craft_now( rec.obj(), std::max( batch, 1 ) ) ? 1.0 : 0.0;
}

// has_active_trait('SHELL2'): 1 if the character has the trait and it is switched on
double has_active_trait_eval( const_dialogue const &d, char scope,
                              std::vector<diag_value> const &params,
                              diag_kwargs const & /* kwargs */ )
{
    const trait_id trait = trait_param( d, params, "has_active_trait" );
    const Character *guy = d.const_actor( is_beta( scope ) )->get_const_character();
    return guy != nullptr && guy->has_active_mutation( trait ) ? 1.0 : 0.0;
}

// MUT-J03: bionic state for EOC math. A talker that isn't a character reads 0.
bionic_id bionic_param( const_dialogue const &d, std::vector<diag_value> const &params,
                        std::string_view func )
{
    const bionic_id bio( params[0].str( d ) );
    if( !bio.is_valid() ) {
        throw math::runtime_error( R"(%s(): unknown bionic "%s")", func, bio.str() );
    }
    return bio;
}

// The character's first installed copy of the bionic, or null
bionic *installed_bionic( const Character &guy, const bionic_id &bio )
{
    const std::optional<bionic *> found = guy.find_bionic_by_type( bio );
    return found ? *found : nullptr;
}

Character &bionic_character( dialogue &d, char scope, std::string_view func )
{
    Character *guy = d.actor( is_beta( scope ) )->get_character();
    if( guy == nullptr ) {
        throw math::runtime_error( "%s(): talker must be a character", func );
    }
    return *guy;
}

// bionic_count('bio_ears'): how many of the bionic are installed; 'ANY' counts every bionic
double bionic_count_eval( const_dialogue const &d, char scope,
                          std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    const Character *guy = d.const_actor( is_beta( scope ) )->get_const_character();
    if( guy == nullptr ) {
        return 0.0;
    }
    if( params[0].str( d ) == "ANY" ) {
        return guy->num_bionics();
    }
    const bionic_id bio = bionic_param( d, params, "bionic_count" );
    int count = 0;
    for( const bionic_id &installed : guy->get_bionics() ) {
        count += installed == bio ? 1 : 0;
    }
    return count;
}

// has_active_bionic('bio_night_vision'): 1 if it is installed and working (powered, not shorted
// out, not stopped by its fuel threshold)
double has_active_bionic_eval( const_dialogue const &d, char scope,
                               std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    const bionic_id bio = bionic_param( d, params, "has_active_bionic" );
    const Character *guy = d.const_actor( is_beta( scope ) )->get_const_character();
    return guy != nullptr && guy->has_active_bionic( bio ) ? 1.0 : 0.0;
}

// bionic_charge('bio_ads'): turns left on its charge timer (a bionic with a "time" that is
// running); 0 when it isn't installed
double bionic_charge_eval( const_dialogue const &d, char scope,
                           std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    const bionic_id bio = bionic_param( d, params, "bionic_charge" );
    const Character *guy = d.const_actor( is_beta( scope ) )->get_const_character();
    const bionic *installed = guy == nullptr ? nullptr : installed_bionic( *guy, bio );
    return installed == nullptr ? 0.0 : to_turns<double>( installed->charge_timer );
}

void bionic_charge_ass( double val, dialogue &d, char scope,
                        std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    const bionic_id bio = bionic_param( d, params, "bionic_charge" );
    if( bionic *installed = installed_bionic( bionic_character( d, scope, "bionic_charge" ), bio ) ) {
        installed->charge_timer = time_duration::from_turns( std::max( 0.0, std::round( val ) ) );
    }
}

// bionic_incapacitated('bio_flashlight'): turns until it stops shorting out (an EMP, say)
double bionic_incapacitated_eval( const_dialogue const &d, char scope,
                                  std::vector<diag_value> const &params,
                                  diag_kwargs const & /* kwargs */ )
{
    const bionic_id bio = bionic_param( d, params, "bionic_incapacitated" );
    const Character *guy = d.const_actor( is_beta( scope ) )->get_const_character();
    const bionic *installed = guy == nullptr ? nullptr : installed_bionic( *guy, bio );
    return installed == nullptr ? 0.0 : to_turns<double>( installed->incapacitated_time );
}

void bionic_incapacitated_ass( double val, dialogue &d, char scope,
                               std::vector<diag_value> const &params,
                               diag_kwargs const & /* kwargs */ )
{
    const bionic_id bio = bionic_param( d, params, "bionic_incapacitated" );
    Character &guy = bionic_character( d, scope, "bionic_incapacitated" );
    if( bionic *installed = installed_bionic( guy, bio ) ) {
        installed->incapacitated_time = time_duration::from_turns( std::max( 0.0, std::round( val ) ) );
        // a shorted-out bionic is off, as an EMP leaves it
        if( installed->incapacitated_time > 0_turns && installed->powered ) {
            guy.deactivate_bionic( *installed, false, true );
        }
    }
}

// bionic_safe_fuel('bio_fuel_wood'): the fraction of power below which the power source burns
// fuel; -1 when it burns regardless
double bionic_safe_fuel_eval( const_dialogue const &d, char scope,
                              std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    const bionic_id bio = bionic_param( d, params, "bionic_safe_fuel" );
    const Character *guy = d.const_actor( is_beta( scope ) )->get_const_character();
    const bionic *installed = guy == nullptr ? nullptr : installed_bionic( *guy, bio );
    return installed == nullptr ? 0.0 : installed->get_safe_fuel_thresh();
}

void bionic_safe_fuel_ass( double val, dialogue &d, char scope,
                           std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    const bionic_id bio = bionic_param( d, params, "bionic_safe_fuel" );
    if( bionic *installed = installed_bionic( bionic_character( d, scope, "bionic_safe_fuel" ),
                            bio ) ) {
        installed->set_safe_fuel_thresh( static_cast<float>( std::clamp( val, -1.0, 1.0 ) ) );
    }
}

// bionic_cost('bio_night_vision', 'type': 'activate'): a cost from the bionic's definition, in
// millijoules: 'activate' (the default), 'deactivate', 'react' (each time it recharges) or
// 'trigger'
double bionic_cost_eval( const_dialogue const &d, char /* scope */,
                         std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    const bionic_id bio = bionic_param( d, params, "bionic_cost" );
    const std::string type = kwargs.kwarg_or( "type", "activate" ).str( d );
    if( type == "activate" ) {
        return units::to_millijoule( bio->power_activate );
    } else if( type == "deactivate" ) {
        return units::to_millijoule( bio->power_deactivate );
    } else if( type == "react" ) {
        return units::to_millijoule( bio->power_over_time );
    } else if( type == "trigger" ) {
        return units::to_millijoule( bio->power_trigger );
    }
    throw math::runtime_error( R"(bionic_cost(): unknown type "%s"; use 'activate', 'deactivate', )"
                               R"('react' or 'trigger')", type );
}

// bionic_power(): stored bionic power in millijoules, assignable within 0 and the maximum
double bionic_power_eval( const_dialogue const &d, char scope,
                          std::vector<diag_value> const & /* params */, diag_kwargs const & /* kwargs */ )
{
    const Character *guy = d.const_actor( is_beta( scope ) )->get_const_character();
    return guy == nullptr ? 0.0 : units::to_millijoule( guy->get_power_level() );
}

void bionic_power_ass( double val, dialogue &d, char scope,
                       std::vector<diag_value> const & /* params */, diag_kwargs const & /* kwargs */ )
{
    Character &guy = bionic_character( d, scope, "bionic_power" );
    const double max = units::to_millijoule( guy.get_max_power_level() );
    guy.set_power_level( units::from_millijoule( static_cast<std::int64_t>( std::clamp( std::round(
                             val ), 0.0, max ) ) ) );
}

double bionic_power_max_eval( const_dialogue const &d, char scope,
                              std::vector<diag_value> const & /* params */, diag_kwargs const & /* kwargs */ )
{
    const Character *guy = d.const_actor( is_beta( scope ) )->get_const_character();
    return guy == nullptr ? 0.0 : units::to_millijoule( guy->get_max_power_level() );
}

// activity_moves_left(): the moves (100 a turn) the current activity has left; 0 without one,
// or for a talker that isn't a character
double activity_moves_left_eval( const_dialogue const &d, char scope,
                                 std::vector<diag_value> const & /* params */,
                                 diag_kwargs const & /* kwargs */ )
{
    const Character *guy = d.const_actor( is_beta( scope ) )->get_const_character();
    return guy == nullptr || !guy->activity ? 0.0 : guy->activity.moves_left;
}

// 0 ends the activity at its next turn, as if its time had run out; without an activity it does
// nothing
void activity_moves_left_ass( double val, dialogue &d, char scope,
                              std::vector<diag_value> const & /* params */,
                              diag_kwargs const & /* kwargs */ )
{
    Character *guy = d.actor( is_beta( scope ) )->get_character();
    if( guy == nullptr ) {
        throw math::runtime_error( "activity_moves_left(): talker must be a character" );
    }
    if( guy->activity ) {
        player_activity &act = guy->activity;
        act.moves_left = static_cast<int>( std::clamp( std::round( val ), 0.0,
                                           static_cast<double>( calendar::INDEFINITELY_LONG ) ) );
        // keeps the progress shown between 0 and 100%
        act.moves_total = std::max( act.moves_total, act.moves_left );
    }
}

double has_flag_eval( const_dialogue const &d, char scope, std::vector<diag_value> const &params,
                      diag_kwargs const & /* kwargs */ )
{
    const_talker const *actor = d.const_actor( is_beta( scope ) );
    json_character_flag jcf( params[0].str( d ) );
    if( jcf == json_flag_MUTATION_THRESHOLD ) {
        return actor->crossed_threshold();
    }
    return actor->has_flag( jcf );
}

double has_var_eval( const_dialogue const &/* d */, char /* scope */,
                     std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    return static_cast<double>( !params[0].is_empty() );
}

double knows_proficiency_eval( const_dialogue const &d, char scope,
                               std::vector<diag_value> const &params,
                               diag_kwargs const & /* kwargs */ )
{
    return d.const_actor( is_beta( scope ) )
           ->knows_proficiency( proficiency_id( params[0].str( d ) ) );
}

double has_wielded_with_flag_eval( const_dialogue const &d, char scope,
                                   std::vector<diag_value> const &params,
                                   diag_kwargs const & /* kwargs */ )
{
    return d.const_actor( is_beta( scope ) )
           ->wielded_with_flag( flag_id( params[0].str( d ) ) );
}

double morale_eval( const_dialogue const &d, char scope, std::vector<diag_value> const &,
                    diag_kwargs const &kwargs )
{
    diag_value raw_val = kwargs.kwarg_or( "raw" );


    const bool raw = is_true( raw_val.dbl( d ) );
    return d.const_actor( is_beta( scope ) )->morale_cur( raw );
}

void morale_ass( double val, dialogue &d, char scope, std::vector<diag_value> const &,
                 diag_kwargs const & /* kwargs */ )
{
    d.actor( is_beta( scope ) )->set_morale( val );
}

// HLTH-J08, HLTH-J10: what one morale type adds to the character's morale now (0 without it)
double morale_value_eval( const_dialogue const &d, char scope,
                          std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    const Character *ch = d.const_actor( is_beta( scope ) )->get_const_character();
    return ch == nullptr ? 0 : ch->has_morale( morale_type( params[0].str( d ) ) );
}

// HLTH-J10: the stomach or guts: 'volume' and 'water' in ml, 'capacity' in ml, 'kcal'
double stomach_eval( const_dialogue const &d, char scope, std::vector<diag_value> const &params,
                     diag_kwargs const &kwargs )
{
    const std::string what = params[0].str( d );
    const std::string organ = kwargs.kwarg_or( "organ", "stomach" ).str( d );
    if( organ != "stomach" && organ != "guts" ) {
        throw math::runtime_error( R"(Unknown organ "%s" for stomach(): "stomach" or "guts")", organ );
    }
    if( what != "volume" && what != "capacity" && what != "kcal" && what != "water" ) {
        throw math::runtime_error(
            R"(Unknown value "%s" for stomach(): "volume", "capacity", "kcal" or "water")", what );
    }
    const Character *ch = d.const_actor( is_beta( scope ) )->get_const_character();
    if( ch == nullptr ) {
        return 0;
    }
    const stomach_contents &part = organ == "guts" ? ch->guts : ch->stomach;
    if( what == "volume" ) {
        return units::to_milliliter( part.contains() );
    } else if( what == "capacity" ) {
        return units::to_milliliter( part.capacity( *ch ) );
    } else if( what == "kcal" ) {
        return part.get_calories();
    }
    return units::to_milliliter( part.get_water() );
}

// HLTH-J10: where the first wound of a type is (its body part and its place in that part's
// wounds), on the body part when one is named; nothing if there is none
std::optional<std::pair<bodypart_str_id, size_t>> find_wound( const Character &ch,
        const wound_type_id &type, const std::string &bp )
{
    for( const std::pair<const bodypart_str_id, bodypart> &part : ch.get_body() ) {
        if( !bp.empty() && part.first.str() != bp ) {
            continue;
        }
        const std::vector<wound> &wounds = part.second.get_wounds();
        for( size_t i = 0; i < wounds.size(); ++i ) {
            if( wounds[i].type == type ) {
                return std::make_pair( part.first, i );
            }
        }
    }
    return std::nullopt;
}

const wound *find_wound( const_dialogue const &d, char scope,
                         std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    const Character *ch = d.const_actor( is_beta( scope ) )->get_const_character();
    if( ch == nullptr ) {
        return nullptr;
    }
    const std::optional<std::pair<bodypart_str_id, size_t>> at = find_wound( *ch,
            wound_type_id( params[0].str( d ) ), kwargs.kwarg_or( "bodypart" ).str( d ) );
    return at ? &ch->get_body().at( at->first ).get_wounds()[at->second] : nullptr;
}

double wound_healing_eval( const_dialogue const &d, char scope,
                           std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    const wound *wd = find_wound( d, scope, params, kwargs );
    return wd == nullptr ? -1 : 100.0 * wd->healing_percentage();
}

void wound_healing_ass( double val, dialogue &d, char scope, std::vector<diag_value> const &params,
                        diag_kwargs const &kwargs )
{
    Character *ch = d.actor( is_beta( scope ) )->get_character();
    if( ch == nullptr ) {
        return;
    }
    const std::optional<std::pair<bodypart_str_id, size_t>> at = find_wound( *ch,
            wound_type_id( params[0].str( d ) ), kwargs.kwarg_or( "bodypart" ).str( d ) );
    if( at ) {
        ch->get_part( at->first )->get_wounds()[at->second].set_healing_percentage(
            std::clamp( val, 0.0, 100.0 ) / 100.0 );
    }
}

double wound_pain_eval( const_dialogue const &d, char scope, std::vector<diag_value> const &params,
                        diag_kwargs const &kwargs )
{
    const wound *wd = find_wound( d, scope, params, kwargs );
    return wd == nullptr ? -1 : wd->get_pain();
}

double hp_eval( const_dialogue const &d, char scope, std::vector<diag_value> const &params,
                diag_kwargs const & /* kwargs */ )
{
    std::string const &bp_str = params[0].str( d );
    bool const major = bp_str == "ALL_MAJOR";
    bool const minor = bp_str == "ALL_MINOR";
    if( major || minor ) {
        get_body_part_flags const parts =
            major ? get_body_part_flags::only_main : get_body_part_flags::only_minor;
        int ret{};
        for( bodypart_id const &part :
             d.const_actor( is_beta( scope ) )->get_all_body_parts( parts ) ) {
            ret += d.const_actor( is_beta( scope ) )->get_cur_hp( part );
        }
        return ret;
    }
    bodypart_id const bp = bp_str == "ALL" ? bodypart_str_id::NULL_ID() : bodypart_id( bp_str );
    return d.const_actor( is_beta( scope ) )->get_cur_hp( bp );
}

void hp_ass( double val, dialogue &d, char scope, std::vector<diag_value> const &params,
             diag_kwargs const & /* kwargs */ )
{
    std::string const &bp_str = params[0].str( d );
    bool const major = bp_str == "ALL_MAJOR";
    bool const minor = bp_str == "ALL_MINOR";
    if( bp_str == "ALL" ) {
        d.actor( is_beta( scope ) )->set_all_parts_hp_cur( val );
    } else if( major || minor ) {
        get_body_part_flags const parts =
            major ? get_body_part_flags::only_main : get_body_part_flags::only_minor;
        for( bodypart_id const &part : d.actor( is_beta( scope ) )->get_all_body_parts( parts ) ) {
            d.actor( is_beta( scope ) )->set_part_hp_cur( part, val );
        }
    } else {
        d.actor( is_beta( scope ) )->set_part_hp_cur( bodypart_id( bp_str ), val );
    }
}

double degradation_eval( const_dialogue const &d, char scope,
                         std::vector<diag_value> const & /* params */, diag_kwargs const & )
{
    return d.const_actor( is_beta( scope ) )->get_degradation();
}
void degradation_ass( double val, dialogue &d, char scope,
                      std::vector<diag_value> const & /* params */,
                      diag_kwargs const & /* kwargs */ )
{
    d.actor( is_beta( scope ) )->set_degradation( val );
}

// The item an item_* function reads: the talker must be an item that still exists (an item used up
// earlier in the same EOC is no item)
const item &talker_item_for( const_dialogue const &d, char scope, const char *func )
{
    item_location const *it = d.const_actor( is_beta( scope ) )->get_const_item();
    if( it == nullptr || !*it ) {
        throw math::runtime_error( "subject of %s() must be an item", func );
    }
    return **it;
}

item_location &talker_item_loc_for( dialogue &d, char scope, const char *func )
{
    item_location *it = d.actor( is_beta( scope ) )->get_item();
    if( it == nullptr || !*it ) {
        throw math::runtime_error( "subject of %s() must be an item", func );
    }
    return *it;
}

int rounded_count( double val )
{
    return static_cast<int>( std::clamp( std::round( val ), 0.0, static_cast<double>( INT_MAX ) ) );
}

// the stack size of an item counted by charges, else the ammo (or battery charge) loaded in it
double item_charges_eval( const_dialogue const &d, char scope,
                          std::vector<diag_value> const & /* params */, diag_kwargs const & )
{
    const item &it = talker_item_for( d, scope, "item_charges" );
    return it.count_by_charges() ? it.charges : it.ammo_remaining();
}

void item_charges_ass( double val, dialogue &d, char scope,
                       std::vector<diag_value> const & /* params */, diag_kwargs const & )
{
    item &it = *talker_item_loc_for( d, scope, "item_charges" );
    const int qty = rounded_count( val );
    if( it.count_by_charges() ) {
        // a stack can't be empty: removing the item is the die effect's job
        it.charges = std::max( qty, 1 );
        return;
    }
    itype_id ammo = it.ammo_current();
    if( ammo.is_null() ) {
        ammo = it.ammo_default();
    }
    if( !ammo.is_null() && ammo->ammo ) {
        it.ammo_set( ammo, std::min( qty, it.ammo_capacity( ammo->ammo->type ) ) );
        return;
    }
    if( it.ammo_types().empty() && it.type->can_have_charges() ) {
        it.charges = qty;
        return;
    }
    throw math::runtime_error( "item_charges(): %s holds no charges or ammo", it.typeId().str() );
}

// Kelvin, as weather('temperature'); 0 for an item whose temperature isn't tracked
double item_temperature_eval( const_dialogue const &d, char scope,
                              std::vector<diag_value> const & /* params */, diag_kwargs const & )
{
    const item &it = talker_item_for( d, scope, "item_temperature" );
    return it.has_temperature() ? units::to_kelvin( it.temperature ) : 0.0;
}

void item_temperature_ass( double val, dialogue &d, char scope,
                           std::vector<diag_value> const & /* params */, diag_kwargs const & )
{
    item &it = *talker_item_loc_for( d, scope, "item_temperature" );
    if( !it.has_temperature() ) {
        throw math::runtime_error( "item_temperature(): %s doesn't track its temperature",
                                   it.typeId().str() );
    }
    it.set_item_temperature( units::from_kelvin( static_cast<float>( std::max( val, 0.0 ) ) ) );
}

// 0 when fresh, 1 when it turns rotten; always 0 for an item that doesn't spoil
double item_freshness_eval( const_dialogue const &d, char scope,
                            std::vector<diag_value> const & /* params */, diag_kwargs const & )
{
    return talker_item_for( d, scope, "item_freshness" ).get_relative_rot();
}

void item_freshness_ass( double val, dialogue &d, char scope,
                         std::vector<diag_value> const & /* params */, diag_kwargs const & )
{
    item &it = *talker_item_loc_for( d, scope, "item_freshness" );
    if( !it.goes_bad() ) {
        throw math::runtime_error( "item_freshness(): %s doesn't spoil", it.typeId().str() );
    }
    it.set_relative_rot( std::max( val, 0.0 ) );
}

// turns until the item is dry
double item_wetness_eval( const_dialogue const &d, char scope,
                          std::vector<diag_value> const & /* params */, diag_kwargs const & )
{
    return talker_item_for( d, scope, "item_wetness" ).wetness;
}

void item_wetness_ass( double val, dialogue &d, char scope,
                       std::vector<diag_value> const & /* params */, diag_kwargs const & )
{
    talker_item_loc_for( d, scope, "item_wetness" )->wetness = rounded_count( val );
}

// 0 for an item that isn't a relic
double item_relic_charges_eval( const_dialogue const &d, char scope,
                                std::vector<diag_value> const & /* params */, diag_kwargs const & )
{
    const item &it = talker_item_for( d, scope, "item_relic_charges" );
    return it.is_relic() ? it.relic_data->charges() : 0;
}

void item_relic_charges_ass( double val, dialogue &d, char scope,
                             std::vector<diag_value> const & /* params */, diag_kwargs const & )
{
    item &it = *talker_item_loc_for( d, scope, "item_relic_charges" );
    if( !it.is_relic() ) {
        throw math::runtime_error( "item_relic_charges(): %s isn't a relic", it.typeId().str() );
    }
    it.relic_data->set_charges( rounded_count( val ) );
}

// seconds until the item's countdown fires, -1 when it has none
double item_countdown_eval( const_dialogue const &d, char scope,
                            std::vector<diag_value> const & /* params */, diag_kwargs const & )
{
    const item &it = talker_item_for( d, scope, "item_countdown" );
    if( it.countdown_point == calendar::turn_max ) {
        return -1;
    }
    return std::max( to_seconds<double>( it.countdown_point - calendar::turn ), 0.0 );
}

void item_countdown_ass( double val, dialogue &d, char scope,
                         std::vector<diag_value> const & /* params */, diag_kwargs const & )
{
    item_location &loc = talker_item_loc_for( d, scope, "item_countdown" );
    if( val < 0 ) {
        loc->countdown_point = calendar::turn_max;
        return;
    }
    loc->countdown_point = calendar::turn + time_duration::from_seconds( rounded_count( val ) );
    // a countdown is only checked while the item is active
    loc->active = true;
    loc.make_active();
}

void spellcasting_adjustment_ass( double val, dialogue &d, char scope,
                                  std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    enum spell_scope { scope_all, scope_mod, scope_school, scope_spell };
    diag_value filter;
    spell_scope spellsearch_scope;
    if( filter = kwargs.kwarg_or( "mod" ); !filter.is_empty() ) {
        spellsearch_scope = scope_mod;
    } else if( filter = kwargs.kwarg_or( "school" ); !filter.is_empty() ) {
        spellsearch_scope = scope_school;
    } else if( filter = kwargs.kwarg_or( "spell" ); !filter.is_empty() ) {
        spellsearch_scope = scope_spell;
    } else {
        spellsearch_scope = scope_all;
    }

    diag_value whitelist = kwargs.kwarg_or( "flag_whitelist" );
    diag_value blacklist = kwargs.kwarg_or( "flag_blacklist" );

    // Only characters have a spellbook; a monster, item, furniture or vehicle u has none
    Character *const caster = d.actor( is_beta( scope ) )->get_character();
    if( caster == nullptr ) {
        throw math::runtime_error( "spellcasting_adjustment needs a character talker" );
    }
    diag_value const &spellcasting_property = params[0];
    std::string const &filter_str = filter.str( d );
    switch( spellsearch_scope ) {
        case scope_spell:
            caster->magic->get_spell( spell_id( filter_str ) )
            .set_temp_adjustment( spellcasting_property.str( d ), val );
            break;
        case scope_school: {
            const trait_id school_id( filter_str );
            for( spell *spellIt : caster->magic->get_spells() ) {
                if( spellIt->spell_class() == school_id &&
                    ( whitelist.str( d ).empty() || spellIt->has_flag( whitelist.str( d ) ) ) &&
                    ( blacklist.str( d ).empty() || !spellIt->has_flag( blacklist.str( d ) ) ) ) {
                    spellIt->set_temp_adjustment( spellcasting_property.str( d ), val );
                }
            }
            break;
        }
        case scope_mod: {
            const mod_id target_mod_id( filter_str );
            for( spell *spellIt : caster->magic->get_spells() ) {
                if( get_mod_base_id_from_src( spellIt->get_src() ) == target_mod_id &&
                    ( whitelist.str( d ).empty() || spellIt->has_flag( whitelist.str( d ) ) ) &&
                    ( blacklist.str( d ).empty() || !spellIt->has_flag( blacklist.str( d ) ) ) ) {
                    spellIt->set_temp_adjustment( spellcasting_property.str( d ), val );
                }
            }
            break;
        }
        case scope_all:
            for( spell *spellIt : caster->magic->get_spells() ) {
                if( ( whitelist.str( d ).empty() || spellIt->has_flag( whitelist.str( d ) ) ) &&
                    ( blacklist.str( d ).empty() || !spellIt->has_flag( blacklist.str( d ) ) ) ) {
                    spellIt->set_temp_adjustment( spellcasting_property.str( d ), val );
                }
            }
            break;
    }
}

double hp_max_eval( const_dialogue const &d, char scope, std::vector<diag_value> const &params,
                    diag_kwargs const & /* kwargs */ )
{
    bodypart_id bp( params[0].str( d ) );
    return d.const_actor( is_beta( scope ) )->get_hp_max( bp );
}

double item_count_eval( const_dialogue const &d, char scope, std::vector<diag_value> const &params,
                        diag_kwargs const & /* kwargs */ )
{
    return d.const_actor( is_beta( scope ) )->get_amount( itype_id( params[0].str( d ) ) );
}

double item_rad_eval( const_dialogue const &d, char scope, std::vector<diag_value> const &params,
                      diag_kwargs const &kwargs )
{
    diag_value agg_val = kwargs.kwarg_or( "aggregate", "min" );

    std::optional<aggregate_type> const agg =
        io::string_to_enum_optional<aggregate_type>( agg_val.str( d ) );
    return d.const_actor( is_beta( scope ) )
           ->item_rads( flag_id( params[0].str( d ) ), agg.value_or( aggregate_type::MIN ) );
}

double num_input_eval( const_dialogue const &d, char /*scope*/,
                       std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    double dv = params[1].dbl( d );
    int popup_val = dv;
    number_input_popup<int> popup( 55, popup_val );
    popup.set_label( _( "Input a value:" ) );
    popup.set_description( params[0].str( d ) );
    popup_val = popup.query();
    if( popup.cancelled() ) {
        return dv;
    }
    return static_cast<double>( popup_val );
}

double attack_speed_eval( const_dialogue const &d, char scope,
                          std::vector<diag_value> const & /* params */,
                          diag_kwargs const & /* kwargs */ )
{
    return d.const_actor( is_beta( scope ) )->attack_speed();
}

double move_speed_eval( const_dialogue const &d, char scope,
                        std::vector<diag_value> const & /* params */,
                        diag_kwargs const & /* kwargs */ )
{
    return d.const_actor( is_beta( scope ) )->get_speed();
}

double melee_damage_eval( const_dialogue const &d, char scope,
                          std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{

    item_location const *it = d.const_actor( is_beta( scope ) )->get_const_item();
    if( it == nullptr || !*it ) {
        throw math::runtime_error( "subject of melee_damage() must be an item" );
    }
    std::string const &dt_str = params[0].str( d );
    if( dt_str == "ALL" ) {
        std::vector<damage_type> const &dts = damage_type::get_all();
        return std::accumulate( dts.cbegin(), dts.cend(), 0, [&it]( int a, damage_type const & dt ) {
            return a + ( *it )->damage_melee( dt.id );
        } );
    }
    return ( *it )->damage_melee( damage_type_id( dt_str ) );
}

// CMBT-J10: combat state for EOCs. These read creatures; any other talker is an error.
Creature const &combat_creature( const_dialogue const &d, char scope,
                                 std::string_view func )
{
    Creature const *critter = d.const_actor( is_beta( scope ) )->get_const_creature();
    if( critter == nullptr ) {
        throw math::runtime_error( "%s(): talker must be a creature", func );
    }
    return *critter;
}

Creature &combat_creature( dialogue &d, char scope, std::string_view func )
{
    Creature *critter = d.actor( is_beta( scope ) )->get_creature();
    if( critter == nullptr ) {
        throw math::runtime_error( "%s(): talker must be a creature", func );
    }
    return *critter;
}

// The center of the melee hit roll: what a defender's dodge roll is compared against
double melee_to_hit_eval( const_dialogue const &d, char scope,
                          std::vector<diag_value> const & /* params */,
                          diag_kwargs const & /* kwargs */ )
{
    Creature const &critter = combat_creature( d, scope, "melee_to_hit" );
    if( Character const *guy = critter.as_character() ) {
        return guy->get_melee_hit_base();
    }
    return critter.get_hit();
}

double dodge_eval( const_dialogue const &d, char scope,
                   std::vector<diag_value> const & /* params */,
                   diag_kwargs const & /* kwargs */ )
{
    return combat_creature( d, scope, "dodge" ).get_dodge();
}

// Chance (0 to 1) of a melee critical with the wielded weapon against a target whose dodge is
// "dodge" (default 0), with a hit roll of "hit" (default the attacker's melee_to_hit()).
// Only characters score melee criticals; other creatures give 0.
double crit_chance_eval( const_dialogue const &d, char scope,
                         std::vector<diag_value> const & /* params */, diag_kwargs const &kwargs )
{
    Character const *guy = combat_creature( d, scope, "crit_chance" ).as_character();
    if( guy == nullptr ) {
        return 0.0;
    }
    const diag_value hit_val = kwargs.kwarg_or( "hit" );
    const float hit = hit_val.is_empty() ? guy->get_melee_hit_base() :
                      static_cast<float>( hit_val.dbl( d ) );
    const float target_dodge = static_cast<float>( kwargs.kwarg_or( "dodge", 0 ).dbl( d ) );
    const item_location weapon = guy->used_weapon();
    // crit_chance() counts its calls in the melee debug statistics; asking isn't attacking
    const melee_statistic_data saved_stats = melee::melee_stats;
    const double chance = guy->crit_chance( hit, target_dodge,
                                            weapon ? *weapon : null_item_reference() );
    melee::melee_stats = saved_stats;
    return std::clamp( chance, 0.0, 1.0 );
}

double blocks_left_eval( const_dialogue const &d, char scope,
                         std::vector<diag_value> const & /* params */,
                         diag_kwargs const & /* kwargs */ )
{
    Creature const &critter = combat_creature( d, scope, "blocks_left" );
    if( Character const *guy = critter.as_character() ) {
        return guy->blocks_left;
    }
    if( monster const *mon = critter.as_monster() ) {
        return mon->blocks_left;
    }
    return 0.0;
}

void blocks_left_ass( double val, dialogue &d, char scope,
                      std::vector<diag_value> const & /* params */,
                      diag_kwargs const & /* kwargs */ )
{
    Creature &critter = combat_creature( d, scope, "blocks_left" );
    const int blocks = std::max( 0, static_cast<int>( std::lround( val ) ) );
    if( Character *guy = critter.as_character() ) {
        guy->blocks_left = blocks;
    } else if( monster *mon = critter.as_monster() ) {
        mon->blocks_left = blocks;
    }
}

double dodges_left_eval( const_dialogue const &d, char scope,
                         std::vector<diag_value> const & /* params */,
                         diag_kwargs const & /* kwargs */ )
{
    Character const *guy = combat_creature( d, scope, "dodges_left" ).as_character();
    return guy == nullptr ? 0.0 : guy->get_dodges_left();
}

void dodges_left_ass( double val, dialogue &d, char scope,
                      std::vector<diag_value> const & /* params */,
                      diag_kwargs const & /* kwargs */ )
{
    Character *guy = combat_creature( d, scope, "dodges_left" ).as_character();
    if( guy == nullptr ) {
        throw math::runtime_error( "dodges_left(): only characters count their dodges" );
    }
    guy->set_dodges_left( std::max( 0, static_cast<int>( std::lround( val ) ) ) );
}

// Aim penalty of the next shot, in MOA; aiming lowers it and shooting raises it.
// Non-characters have none.
double recoil_eval( const_dialogue const &d, char scope,
                    std::vector<diag_value> const & /* params */,
                    diag_kwargs const & /* kwargs */ )
{
    Character const *guy = combat_creature( d, scope, "recoil" ).as_character();
    return guy == nullptr ? 0.0 : guy->recoil;
}

void recoil_ass( double val, dialogue &d, char scope,
                 std::vector<diag_value> const & /* params */,
                 diag_kwargs const & /* kwargs */ )
{
    Character *guy = combat_creature( d, scope, "recoil" ).as_character();
    if( guy == nullptr ) {
        throw math::runtime_error( "recoil(): talker must be a character" );
    }
    guy->recoil = std::clamp( val, 0.0, MAX_RECOIL );
}

// Average dispersion of the wielded gun in the character's hands, before recoil; 0 without a gun
double weapon_dispersion_eval( const_dialogue const &d, char scope,
                               std::vector<diag_value> const & /* params */,
                               diag_kwargs const & /* kwargs */ )
{
    Character const *guy = combat_creature( d, scope, "weapon_dispersion" ).as_character();
    if( guy == nullptr ) {
        return 0.0;
    }
    const item_location weapon = guy->get_wielded_item();
    if( !weapon || !weapon->is_gun() ) {
        return 0.0;
    }
    return guy->get_weapon_dispersion( *weapon ).avg();
}

double mod_order_eval( const_dialogue const &d, char /* scope */,
                       std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    int count = 0;
    mod_id our_mod_id( params[0].str( d ) );
    for( const mod_id &mod : world_generator->active_world->active_mod_order ) {
        if( our_mod_id == mod ) {
            return count;
        }
        count++;
    }
    return -1;
}

enum class character_filter : int {
    allies = 0,
    not_allies,
    hostile,
    any,
};

bool _friend_match_filter_character( Character const &beta, Character const &guy,
                                     character_filter filter )
{
    switch( filter ) {
        case character_filter::allies:
            return guy.is_ally( beta );
        case character_filter::not_allies:
            return !guy.is_ally( beta );
        case character_filter::hostile:
            return guy.attitude_to( beta ) == Creature::Attitude::HOSTILE ||
                   ( beta.is_avatar() && guy.is_npc() && guy.as_npc()->guaranteed_hostile() );
        case character_filter::any:
            return true;
    }
    return false;
}

bool _filter_character( Character const *beta, Character const &guy, int radius,
                        tripoint_abs_ms const &loc, character_filter filter,
                        bool allow_hallucinations )
{
    if( ( !guy.is_hallucination() || allow_hallucinations ) &&
        ( beta == nullptr || beta->getID() != guy.getID() ) ) {
        // Without a character there is no attitude to match (the caller allows only "any")
        return ( beta == nullptr || _friend_match_filter_character( *beta, guy, filter ) ) &&
               radius >= rl_dist( guy.pos_abs(), loc );
    }
    return false;
}

// TODO: some form of notation or sentinel value for referencing
// the reality bubble size (since it might change from 60, and
// hardcoding that is unfortunate)
double _characters_nearby_eval( const_dialogue const &d, char scope,
                                std::vector<diag_value> const & /* params */,
                                diag_kwargs const &kwargs )
{
    diag_value radius_val = kwargs.kwarg_or( "radius", 1000 );
    diag_value filter_val = kwargs.kwarg_or( "attitude", "any" );
    diag_value allow_hallucinations_val = kwargs.kwarg_or( "allow_hallucinations" );
    diag_value loc_val = kwargs.kwarg_or( "location" );
    bool const beta = is_beta( scope );
    tripoint_abs_ms loc;

    if( !loc_val.is_empty() ) {
        loc = loc_val.tripoint( d );
    } else if( scope == 'g' ) {
        throw math::syntax_error(
            R"("characters_nearby" needs either an actor scope (u/n) or a 'location' kwarg)" );
    } else {
        loc = d.const_actor( beta )->pos_abs();
    }

    int const radius = static_cast<int>( radius_val.dbl( d ) );
    std::string const &filter_str = filter_val.str( d );
    character_filter filter = character_filter::any;
    if( filter_str == "allies" ) {
        filter = character_filter::allies;
    } else if( filter_str == "not_allies" ) {
        filter = character_filter::not_allies;
    } else if( filter_str == "hostile" ) {
        filter = character_filter::hostile;
    } else if( filter_str != "any" ) {
        throw math::runtime_error( R"(Unknown attitude filter "%s" for characters_nearby())",
                                   filter_str );
    }
    bool allow_hallucinations = false;
    int const hallucinations_int = static_cast<int>( allow_hallucinations_val.dbl( d ) );
    if( hallucinations_int != 0 ) {
        allow_hallucinations = true;
    }
    if( filter != character_filter::any &&
        d.const_actor( beta )->get_const_character() == nullptr ) {
        throw math::runtime_error(
            R"(characters_nearby() attitude "%s" needs a character to compare with)", filter_str );
    }

    std::vector<Character *> const targets = g->get_characters_if(
    [beta, &d, radius, &loc, filter, allow_hallucinations]( const Character & guy ) {
        const_talker const *const tk = d.const_actor( beta );
        return _filter_character( tk->get_const_character(), guy, radius, loc, filter,
                                  allow_hallucinations );
    } );
    return static_cast<double>( targets.size() );
}

double characters_nearby_eval( const_dialogue const &d, char scope,
                               std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    return _characters_nearby_eval( d, scope, params, kwargs );
}

template <class ID>
using f_monster_match = bool ( * )( Creature const &critter, ID const &id );

bool mon_check_id( Creature const &critter, mtype_id const &id )
{
    return id == critter.as_monster()->type->id;
}

bool mon_check_species( Creature const &critter, species_id const &id )
{
    return critter.as_monster()->in_species( id );
}

bool mon_check_group( Creature const &critter, mongroup_id const &id )
{
    return MonsterGroupManager::IsMonsterInGroup( id, critter.as_monster()->type->id );
}

enum class mon_filter : int {
    enemies = 0,
    friends,
    both,
    // MATH-J06: by the monster's attitude to the talker, so wildlife that won't attack is left out
    hostile_to,
    neutral_to,
};

bool _matches_attitude_filter( Creature const &critter, mon_filter filter, Creature const *to )
{
    monster const *mon = critter.as_monster();
    switch( filter ) {
        case mon_filter::enemies:
            return mon->friendly == 0;
        case mon_filter::friends:
            return mon->friendly != 0;
        case mon_filter::both:
            return true;
        case mon_filter::hostile_to:
            return to != nullptr && mon->attitude_to( *to ) == Creature::Attitude::HOSTILE;
        case mon_filter::neutral_to:
            return to != nullptr && mon->attitude_to( *to ) == Creature::Attitude::NEUTRAL;
    }
    return false;
}

template <class ID>
bool _filter_monster( Creature const &critter, std::vector<ID> const &ids, int radius,
                      tripoint_abs_ms const &loc, f_monster_match<ID> f, mon_filter filter,
                      bool allow_hallucinations, Creature const *to )
{
    if( critter.is_monster() && ( allow_hallucinations || !critter.is_hallucination() ) ) {
        bool const id_filter =
        ids.empty() || std::any_of( ids.begin(), ids.end(), [&critter, f]( ID const & id ) {
            return ( *f )( critter, id );
        } );

        return id_filter && radius >= rl_dist( critter.pos_abs(), loc ) &&
               _matches_attitude_filter( critter, filter, to );
    }
    return false;
}

// TODO: some form of notation or sentinel value for referencing
// the reality bubble size (since it might change from 60, and
// hardcoding that is unfortunate)
template <class ID>
double _monsters_nearby_eval( const_dialogue const &d, char scope,
                              std::vector<diag_value> const &params, diag_kwargs const &kwargs,
                              f_monster_match<ID> f )
{
    diag_value radius_val = kwargs.kwarg_or( "radius", 1000 );
    diag_value filter_val = kwargs.kwarg_or( "attitude", "hostile" );
    diag_value loc_val = kwargs.kwarg_or( "location" );
    tripoint_abs_ms loc;

    if( !loc_val.is_empty() ) {
        loc = loc_val.tripoint( d );
    } else if( scope == 'g' ) {
        throw math::syntax_error(
            R"("monsters_nearby" needs either an actor scope (u/n) or a 'location' kwarg)" );
    } else {
        loc = d.const_actor( is_beta( scope ) )->pos_abs();
    }

    int const radius = static_cast<int>( radius_val.dbl( d ) );
    std::vector<ID> mids( params.size() );
    std::transform( params.begin(), params.end(), mids.begin(), [&d]( diag_value const & val ) {
        return ID( val.str( d ) );
    } );

    std::string const &filter_str = filter_val.str( d );
    mon_filter filter = mon_filter::enemies;
    if( filter_str == "both" ) {
        filter = mon_filter::both;
    } else if( filter_str == "friendly" ) {
        filter = mon_filter::friends;
    } else if( filter_str == "enemy" ) {
        filter = mon_filter::hostile_to;
    } else if( filter_str == "neutral" ) {
        filter = mon_filter::neutral_to;
    } else if( filter_str != "hostile" ) {
        throw math::runtime_error( R"(Unknown attitude filter "%s" for monsters_nearby())",
                                   filter_str );
    }
    // MATH-J06: hallucinations count only when asked for, as for characters_nearby()
    bool const allow_hallucinations =
        is_true( kwargs.kwarg_or( "allow_hallucinations", 0 ).dbl( d ) );
    // "enemy" and "neutral" are the attitude to the talker, or to the avatar without one
    Creature const *to = nullptr;
    if( filter == mon_filter::hostile_to || filter == mon_filter::neutral_to ) {
        to = scope != 'g' ? d.const_actor( is_beta( scope ) )->get_const_creature() : nullptr;
        if( to == nullptr ) {
            to = &get_player_character();
        }
    }

    std::vector<Creature *> const targets =
        g->get_creatures_if( [&mids, &radius, &loc, f, filter, allow_hallucinations,
           to]( const Creature & critter ) {
        return _filter_monster( critter, mids, radius, loc, f, filter, allow_hallucinations, to );
    } );
    return static_cast<double>( targets.size() );
}

// MATH-J06: is_hallucination(): 1 for a hallucinated monster or NPC, 0 for anything else
double is_hallucination_eval( const_dialogue const &d, char scope,
                              std::vector<diag_value> const & /* params */,
                              diag_kwargs const & /* kwargs */ )
{
    Creature const *critter = d.const_actor( is_beta( scope ) )->get_const_creature();
    return critter != nullptr && critter->is_hallucination() ? 1 : 0;
}

double monsters_nearby_eval( const_dialogue const &d, char scope,
                             std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    return _monsters_nearby_eval( d, scope, params, kwargs, mon_check_id );
}

double monster_species_nearby_eval( const_dialogue const &d, char scope,
                                    std::vector<diag_value> const &params,
                                    diag_kwargs const &kwargs )
{
    return _monsters_nearby_eval( d, scope, params, kwargs, mon_check_species );
}

double monster_groups_nearby_eval( const_dialogue const &d, char scope,
                                   std::vector<diag_value> const &params,
                                   diag_kwargs const &kwargs )
{
    return _monsters_nearby_eval( d, scope, params, kwargs, mon_check_group );
}

double moon_phase_eval( const_dialogue const & /* d */, char /* scope */,
                        std::vector<diag_value> const & /* params */,
                        diag_kwargs const & /* kwargs */ )
{
    return static_cast<int>( get_moon_phase( calendar::turn ) );
}

// oracle('name'): a literal name must be a named score.
void oracle_check( std::vector<diag_value> const &params )
{
    if( params[0].is_str() && behavior::score_predicate_map.count( params[0].str() ) == 0 ) {
        throw math::syntax_error( R"(oracle(): "%s" is not a named score)", params[0].str() );
    }
}

// A named score of the NPC behavior tree (score_predicate_map), for u.
double oracle_eval( const_dialogue const &d, char /* scope */,
                    std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    const std::string name( params[0].str( d ) );
    const auto score = behavior::score_predicate_map.find( name );
    if( score == behavior::score_predicate_map.end() ) {
        throw math::runtime_error( R"(oracle(): "%s" is not a named score)", name );
    }
    const Character *who = d.has_actor( false ) ? d.const_actor( false )->get_const_character() :
                           nullptr;
    if( who == nullptr ) {
        throw math::runtime_error( "oracle() needs u to be a character" );
    }
    const behavior::character_oracle_t oracle( who );
    return score->second( &oracle, "" );
}

double oxygen_eval( const_dialogue const &d, char scope, std::vector<diag_value> const &/*params*/,
                    diag_kwargs const & /* kwargs */ )
{
    return d.const_actor( is_beta( scope ) )->get_oxygen();
}

void oxygen_ass( double val, dialogue &d, char scope, std::vector<diag_value> const &/*params*/,
                 diag_kwargs const & /* kwargs */ )
{
    d.actor( is_beta( scope ) )->set_oxygen( val );
}

double oxygen_max_eval( const_dialogue const &d, char scope,
                        std::vector<diag_value> const &/*params*/,
                        diag_kwargs const & /* kwargs */ )
{
    return d.const_actor( is_beta( scope ) )->get_oxygen_max();
}

double pain_eval( const_dialogue const &d, char scope, std::vector<diag_value> const & /* params */,
                  diag_kwargs const &kwargs )
{
    diag_value format_value = kwargs.kwarg_or( "type", "raw" );

    std::string const &format = format_value.str( d );
    if( format == "perceived" ) {
        return d.const_actor( is_beta( scope ) )->perceived_pain_cur();
    } else if( format == "raw" ) {
        return d.const_actor( is_beta( scope ) )->pain_cur();
    } else {
        throw math::runtime_error( R"(Unknown type "%s" for pain())", format );
    }
}

double price_eval( const_dialogue const &d, char scope,
                   std::vector<diag_value> const & /* params */,
                   diag_kwargs const & /* kwargs */ )
{
    return d.const_actor( is_beta( scope ) )->get_price();
}

double price_postapoc_eval( const_dialogue const &d, char scope,
                            std::vector<diag_value> const & /* params */,
                            diag_kwargs const & /* kwargs */ )
{
    return d.const_actor( is_beta( scope ) )->get_price_postapoc();
}

void pain_ass( double val, dialogue &d, char scope, std::vector<diag_value> const & /* params */,
               diag_kwargs const &kwargs )
{
    diag_value format_value = kwargs.kwarg_or( "type", "raw" );

    std::string const &format = format_value.str( d );
    if( format == "perceived" ) {
        // val is the target value; apply the difference through mod_pain so that pain
        // sensitivity and immunity still apply to the change.
        talker *const actor = d.actor( is_beta( scope ) );
        actor->mod_pain( static_cast<int>( val ) - actor->perceived_pain_cur() );
    } else if( format == "raw" ) {
        d.actor( is_beta( scope ) )->set_pain( val );
    } else {
        throw math::runtime_error( R"(Unknown type "%s" for pain())", format );
    }
}

double energy_eval( const_dialogue const &d, char /* scope */,
                    std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    return static_cast<double>( units::to_millijoule(
                                    _read_from_string<units::energy>( params[0].str( d ), units::energy_units ) ) );
}

double school_level_eval( const_dialogue const &d, char scope,
                          std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    return d.const_actor( is_beta( scope ) )->get_spell_level( trait_id( params[0].str( d ) ) );
}

double school_level_adjustment_eval( const_dialogue const &d, char scope,
                                     std::vector<diag_value> const &params,
                                     diag_kwargs const & /* kwargs */ )
{
    const Character *ch = d.const_actor( is_beta( scope ) )->get_const_character();
    if( ch ) {
        const trait_id school( params[0].str( d ) );
        auto it = ch->magic->caster_level_adjustment_by_school.find( school );
        if( it != ch->magic->caster_level_adjustment_by_school.end() ) {
            return it->second;
        } else {
            return 0.0;
        }
    }
    return 0.0;
}

void school_level_adjustment_ass( double val, dialogue &d, char scope,
                                  std::vector<diag_value> const &params,
                                  diag_kwargs const & /* kwargs */ )
{
    const Character *ch = d.actor( is_beta( scope ) )->get_character();
    if( ch ) {
        const trait_id school( params[0].str( d ) );
        auto it = ch->magic->caster_level_adjustment_by_school.find( school );
        if( it != ch->magic->caster_level_adjustment_by_school.end() ) {
            it->second = val;
        } else {
            ch->magic->caster_level_adjustment_by_school.insert( { school, val } );
        }
    }
}

double get_daily_calories( const_dialogue const &d, char scope,
                           std::vector<diag_value> const & /* params */, diag_kwargs const &kwargs )
{
    diag_value type_val = kwargs.kwarg_or( "type", "total" );
    diag_value day_val = kwargs.kwarg_or( "day" );

    std::string const &type = type_val.str( d );
    int const day = day_val.dbl( d );
    if( day < 0 ) {
        throw math::runtime_error(
            "get_daily_calories(): cannot access calorie diary from the future (day < 0)" );
    }

    return d.const_actor( is_beta( scope ) )->get_daily_calories( day, type );
}

double skill_eval( const_dialogue const &d, char scope, std::vector<diag_value> const &params,
                   diag_kwargs const & /* kwargs */ )
{
    return d.const_actor( is_beta( scope ) )->get_skill_level( skill_id( params[0].str( d ) ) );
}

void skill_ass( double val, dialogue &d, char scope, std::vector<diag_value> const &params,
                diag_kwargs const & /* kwargs */ )
{
    d.actor( is_beta( scope ) )->set_skill_level( skill_id( params[0].str( d ) ), val );
}

double skill_exp_eval( const_dialogue const &d, char scope, std::vector<diag_value> const &params,
                       diag_kwargs const &kwargs )
{
    diag_value format_value = kwargs.kwarg_or( "format", "percentage" );

    skill_id skill( params[0].str( d ) );
    std::string const &format = format_value.str( d );
    if( format != "raw" && format != "percentage" ) {
        throw math::runtime_error( R"(Unknown format type "%s" for skill_exp")", format );
    }
    bool raw = format == "raw";
    return d.const_actor( is_beta( scope ) )->get_skill_exp( skill, raw );
}

void skill_exp_ass( double val, dialogue &d, char scope, std::vector<diag_value> const &params,
                    diag_kwargs const &kwargs )
{
    diag_value format_value = kwargs.kwarg_or( "format", "percentage" );
    diag_value practice_value = kwargs.kwarg_or( "practice" );

    skill_id skill( params[0].str( d ) );
    std::string const &format = format_value.str( d );
    if( format != "raw" && format != "percentage" ) {
        throw math::runtime_error( R"(Unknown format type "%s" for skill_exp)", format );
    }
    bool raw = format == "raw";
    if( is_true( practice_value.dbl( d ) ) ) {
        // Practice for the difference, as an activity does: focus, catch-up, the skill's own
        // rules, the level-up messages and events all apply (PROG-J06)
        Character *guy = d.actor( is_beta( scope ) )->get_character();
        if( guy == nullptr ) {
            return;
        }
        const SkillLevel &level = guy->get_skill_level_object( skill );
        const double gain = val - level.exercise( raw );
        const double raw_gain = raw ? gain : gain * 100.0 * ( level.level() + 1 ) * ( level.level() + 1 );
        // Character::practice takes hundredths of raw experience, at most 1000 at a time
        int amount = static_cast<int>( std::lround( raw_gain / 100.0 ) );
        while( amount > 0 ) {
            const int chunk = std::min( amount, 1000 );
            guy->practice( skill, chunk, MAX_SKILL, true, true );
            amount -= chunk;
        }
        return;
    }
    d.actor( is_beta( scope ) )->set_skill_exp( skill, val, raw );
}

// Defined below, with time()
double _turns_per_unit( std::string_view unit );
double _time_in_unit( double time, std::string_view unit );

// Theoretical skill, rust and the time since practice (PROG-J06)
bool skill_format_is_raw( diag_kwargs const &kwargs, const_dialogue const &d, const char *func )
{
    diag_value format_value = kwargs.kwarg_or( "format", "percentage" );
    std::string const &format = format_value.str( d );
    if( format != "raw" && format != "percentage" ) {
        throw math::runtime_error( R"(Unknown format type "%s" for %s)", format, func );
    }
    return format == "raw";
}

double skill_knowledge_eval( const_dialogue const &d, char scope,
                             std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    const Character *guy = d.const_actor( is_beta( scope ) )->get_const_character();
    return guy == nullptr ? 0 : guy->get_knowledge_level( skill_id( params[0].str( d ) ) );
}

void skill_knowledge_ass( double val, dialogue &d, char scope,
                          std::vector<diag_value> const &params,
                          diag_kwargs const & /* kwargs */ )
{
    Character *guy = d.actor( is_beta( scope ) )->get_character();
    if( guy == nullptr ) {
        return;
    }
    const skill_id skill( params[0].str( d ) );
    // Theory is never below the practical level
    const int practical = guy->get_skill_level_object( skill ).level();
    guy->set_knowledge_level( skill, std::clamp( static_cast<int>( val ), practical, MAX_SKILL ) );
}

double skill_knowledge_exp_eval( const_dialogue const &d, char scope,
                                 std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    const bool raw = skill_format_is_raw( kwargs, d, "skill_knowledge_exp" );
    const Character *guy = d.const_actor( is_beta( scope ) )->get_const_character();
    return guy == nullptr ? 0 : guy->get_skill_level_object( skill_id( params[0].str(
                d ) ) ).knowledgeExperience( raw );
}

void skill_knowledge_exp_ass( double val, dialogue &d, char scope,
                              std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    const bool raw = skill_format_is_raw( kwargs, d, "skill_knowledge_exp" );
    Character *guy = d.actor( is_beta( scope ) )->get_character();
    if( guy != nullptr ) {
        guy->get_skill_level_object( skill_id( params[0].str( d ) ) ).set_knowledge_exercise(
            static_cast<int>( val ), raw );
    }
}

double skill_rust_eval( const_dialogue const &d, char scope, std::vector<diag_value> const &params,
                        diag_kwargs const &kwargs )
{
    const bool raw = skill_format_is_raw( kwargs, d, "skill_rust" );
    const Character *guy = d.const_actor( is_beta( scope ) )->get_const_character();
    if( guy == nullptr ) {
        return 0;
    }
    const SkillLevel &level = guy->get_skill_level_object( skill_id( params[0].str( d ) ) );
    const double rust = level.rustAccumulator();
    // A percentage of the current level's experience, as skill_exp reads it
    return raw ? rust : rust / ( 100.0 * ( level.level() + 1 ) * ( level.level() + 1 ) );
}

double skill_rusty_eval( const_dialogue const &d, char scope, std::vector<diag_value> const &params,
                         diag_kwargs const & /* kwargs */ )
{
    const Character *guy = d.const_actor( is_beta( scope ) )->get_const_character();
    return guy != nullptr &&
           guy->get_skill_level_object( skill_id( params[0].str( d ) ) ).isRusty() ? 1 : 0;
}

double skill_time_since_practice_eval( const_dialogue const &d, char scope,
                                       std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    diag_value unit_val = kwargs.kwarg_or( "unit" );
    const Character *guy = d.const_actor( is_beta( scope ) )->get_const_character();
    if( guy == nullptr ) {
        return 0;
    }
    const time_point last = guy->get_skill_level_object( skill_id( params[0].str(
                                d ) ) ).lastPracticed();
    return _time_in_unit( to_turns<double>( calendar::turn - last ), unit_val.str( d ) );
}

void skill_time_since_practice_ass( double val, dialogue &d, char scope,
                                    std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    diag_value unit_val = kwargs.kwarg_or( "unit" );
    Character *guy = d.actor( is_beta( scope ) )->get_character();
    if( guy == nullptr ) {
        return;
    }
    const double turns = std::max( val, 0.0 ) * _turns_per_unit( unit_val.str( d ) );
    guy->get_skill_level_object( skill_id( params[0].str( d ) ) ).set_last_practiced(
        calendar::turn - time_duration::from_turns( static_cast<int>( std::lround( turns ) ) ) );
}

double spell_count_eval( const_dialogue const &d, char scope,
                         std::vector<diag_value> const & /* params */, diag_kwargs const &kwargs )
{
    diag_value school_value = kwargs.kwarg_or( "school" );

    std::string const &school_str = school_value.str( d );
    const trait_id scid = school_str.empty() ? trait_id::NULL_ID() : trait_id( school_str );
    return d.const_actor( is_beta( scope ) )->get_spell_count( scid );
}

double spell_sum_eval( const_dialogue const &d, char scope,
                       std::vector<diag_value> const & /* params */, diag_kwargs const &kwargs )
{
    diag_value school_value = kwargs.kwarg_or( "school" );
    diag_value min_level = kwargs.kwarg_or( "level" );

    std::string const &school_str = school_value.str( d );
    int const min_spell_level = min_level.dbl( d );
    const trait_id scid = school_str.empty() ? trait_id::NULL_ID() : trait_id( school_str );
    return d.const_actor( is_beta( scope ) )->get_spell_sum( scid, min_spell_level );
}

double spell_difficulty_eval( const_dialogue const &d, char scope,
                              std::vector<diag_value> const &params,
                              diag_kwargs const &kwargs )
{
    bool ignore_modifiers = is_true( kwargs.kwarg_or( "baseline" ).dbl( d ) );
    std::string const &sid_str = params[0].str( d );
    spell_id spell( sid_str );
    if( spell.is_valid() ) {
        return d.const_actor( is_beta( scope ) )->get_spell_difficulty( spell, ignore_modifiers );
    }
    throw math::runtime_error( R"(Unknown spell id "%s" for spell_difficulty_eval)", sid_str );
}

// MAGIC-J02: what a cast of the spell would do now. The character's own spell, or a level 0 one
// if it doesn't know the spell; with 'baseline', its level without temporary adjustments.
spell spell_for_math( const Character &guy, const spell_id &id, bool baseline )
{
    if( guy.magic->knows_spell( id ) ) {
        const spell &known = guy.magic->get_spell( id );
        return baseline ? spell( id, known.xp() ) : known;
    }
    return spell( id );
}

spell_id spell_param( const_dialogue const &d, std::vector<diag_value> const &params,
                      std::string_view func )
{
    const spell_id id( params[0].str( d ) );
    if( !id.is_valid() ) {
        throw math::runtime_error( R"(%s(): unknown spell "%s")", func, id.str() );
    }
    return id;
}

// Runs @p read on the spell for the talker; 0 for a talker that isn't a character
template<typename Read>
double spell_value_for_math( const_dialogue const &d, char scope,
                             std::vector<diag_value> const &params, diag_kwargs const &kwargs,
                             std::string_view func, Read read )
{
    const spell_id id = spell_param( d, params, func );
    const Character *guy = d.const_actor( is_beta( scope ) )->get_const_character();
    if( guy == nullptr ) {
        return 0.0;
    }
    const bool baseline = is_true( kwargs.kwarg_or( "baseline" ).dbl( d ) );
    return read( spell_for_math( *guy, id, baseline ), *guy );
}

double spell_fail_eval( const_dialogue const &d, char scope, std::vector<diag_value> const &params,
                        diag_kwargs const &kwargs )
{
    return spell_value_for_math( d, scope, params, kwargs, "spell_fail",
    []( const spell & sp, const Character & guy ) {
        return static_cast<double>( sp.spell_fail( guy ) );
    } );
}

double spell_casting_exp_eval( const_dialogue const &d, char scope,
                               std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    return spell_value_for_math( d, scope, params, kwargs, "spell_casting_exp",
    []( const spell & sp, const Character & guy ) {
        return static_cast<double>( sp.casting_exp( guy ) );
    } );
}

double spell_sound_eval( const_dialogue const &d, char scope, std::vector<diag_value> const &params,
                         diag_kwargs const &kwargs )
{
    return spell_value_for_math( d, scope, params, kwargs, "spell_sound",
    []( const spell & sp, const Character & guy ) {
        return static_cast<double>( sp.sound_volume( guy ) );
    } );
}

double spell_energy_cost_eval( const_dialogue const &d, char scope,
                               std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    return spell_value_for_math( d, scope, params, kwargs, "spell_energy_cost",
    []( const spell & sp, const Character & guy ) {
        return static_cast<double>( sp.energy_cost( guy ) );
    } );
}

double spell_cast_time_eval( const_dialogue const &d, char scope,
                             std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    return spell_value_for_math( d, scope, params, kwargs, "spell_cast_time",
    []( const spell & sp, const Character & guy ) {
        return static_cast<double>( sp.casting_time( guy ) );
    } );
}

// MAGIC-J10: the rest of what a cast of the spell would do now, as spell_fail() and the others
double spell_damage_eval( const_dialogue const &d, char scope,
                          std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    return spell_value_for_math( d, scope, params, kwargs, "spell_damage",
    []( const spell & sp, const Character & guy ) {
        return static_cast<double>( sp.damage( guy ) );
    } );
}

double spell_range_eval( const_dialogue const &d, char scope,
                         std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    return spell_value_for_math( d, scope, params, kwargs, "spell_range",
    []( const spell & sp, const Character & guy ) {
        return static_cast<double>( sp.range( guy ) );
    } );
}

double spell_aoe_eval( const_dialogue const &d, char scope,
                       std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    return spell_value_for_math( d, scope, params, kwargs, "spell_aoe",
    []( const spell & sp, const Character & guy ) {
        return static_cast<double>( sp.aoe( guy ) );
    } );
}

// in turns
double spell_duration_eval( const_dialogue const &d, char scope,
                            std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    return spell_value_for_math( d, scope, params, kwargs, "spell_duration",
    []( const spell & sp, const Character & guy ) {
        return to_turns<double>( sp.duration_turns( guy ) );
    } );
}

double spell_max_level_eval( const_dialogue const &d, char scope,
                             std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    return spell_value_for_math( d, scope, params, kwargs, "spell_max_level",
    []( const spell & sp, const Character & guy ) {
        return static_cast<double>( sp.get_max_level( guy ) );
    } );
}

// MAGIC-J10: the sum of the talker's enchantments' "add" (or, with 'type': 'multiply', their
// "multiply") for an enchantment value, as the game last cached them; 0 for a talker that isn't
// a creature
double enchantment_value_eval( const_dialogue const &d, char scope,
                               std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    const std::string &name = params[0].str( d );
    const std::optional<enchant_vals::mod> value =
        io::string_to_enum_optional<enchant_vals::mod>( name );
    if( !value.has_value() ) {
        throw math::runtime_error( R"(enchantment_value(): unknown enchantment value "%s")", name );
    }
    const std::string type = kwargs.kwarg_or( "type", "add" ).str( d );
    if( type != "add" && type != "multiply" ) {
        throw math::runtime_error(
            R"(enchantment_value(): 'type' is "add" or "multiply", not "%s")", type );
    }
    const Creature *critter = d.const_actor( is_beta( scope ) )->get_const_creature();
    if( critter == nullptr ) {
        return 0.0;
    }
    return type == "add" ? critter->enchantment_cache->get_value_add( *value ) :
           critter->enchantment_cache->get_value_multiply( *value );
}

// XP still needed for the next level of a spell the character knows; -1 otherwise, as
// spell_exp() reads
double spell_exp_to_next_level_eval( const_dialogue const &d, char scope,
                                     std::vector<diag_value> const &params,
                                     diag_kwargs const & /* kwargs */ )
{
    const spell_id id = spell_param( d, params, "spell_exp_to_next_level" );
    const Character *guy = d.const_actor( is_beta( scope ) )->get_const_character();
    if( guy == nullptr || !guy->magic->knows_spell( id ) ) {
        return -1.0;
    }
    return guy->magic->get_spell( id ).exp_to_next_level();
}

double spell_exp_eval( const_dialogue const &d, char scope, std::vector<diag_value> const &params,
                       diag_kwargs const & /* kwargs */ )
{
    return d.const_actor( is_beta( scope ) )->get_spell_exp( spell_id( params[0].str( d ) ) );
}

double spell_exp_for_level_eval( const_dialogue const &d, char /* scope */,
                                 std::vector<diag_value> const &params,
                                 diag_kwargs const & /* kwargs */ )
{
    std::string const &sid_str = params[0].str( d );
    spell_id spell( sid_str );
    if( spell.is_valid() ) {
        return spell->exp_for_level( params[1].dbl( d ) );
    }

    throw math::runtime_error( R"(Unknown spell id "%s" for spell_exp_for_level)", sid_str );
}

void spell_exp_ass( double val, dialogue &d, char scope, std::vector<diag_value> const &params,
                    diag_kwargs const & /* kwargs */ )
{
    d.actor( is_beta( scope ) )->set_spell_exp( spell_id( params[0].str( d ) ), val );
}

double spell_level_eval( const_dialogue const &d, char scope, std::vector<diag_value> const &params,
                         diag_kwargs const & /* kwargs */ )
{
    const spell_id spell( params[0].str( d ) );
    if( spell == spell_id::NULL_ID() ) {
        return d.const_actor( is_beta( scope ) )->get_highest_spell_level();
    } else {
        return d.const_actor( is_beta( scope ) )->get_spell_level( spell );
    }
}

void spell_level_ass( double val, dialogue &d, char scope, std::vector<diag_value> const &params,
                      diag_kwargs const & /* kwargs */ )
{
    std::string const &spell_str = params[0].str( d );
    const spell_id spell( spell_str );
    if( spell == spell_id::NULL_ID() ) {
        throw math::runtime_error( R"("%s" is not a valid spell)", spell_str );
    } else {
        d.actor( is_beta( scope ) )->set_spell_level( spell, val );
    }
}

double spell_level_adjustment_eval( const_dialogue const &d, char scope,
                                    std::vector<diag_value> const &params,
                                    diag_kwargs const & /* kwargs */ )
{
    const Character *ch = d.const_actor( is_beta( scope ) )->get_const_character();
    if( ch ) {
        const spell_id spell( params[0].str( d ) );
        if( spell == spell_id::NULL_ID() ) {
            return ch->magic->caster_level_adjustment;
        } else {
            std::map<spell_id, double>::iterator it =
                ch->magic->caster_level_adjustment_by_spell.find( spell );
            if( it != ch->magic->caster_level_adjustment_by_spell.end() ) {
                return it->second;
            }
        }
    }
    return 0.0;
}

void spell_level_adjustment_ass( double val, dialogue &d, char scope,
                                 std::vector<diag_value> const &params,
                                 diag_kwargs const & /* kwargs */ )
{
    const Character *ch = d.actor( is_beta( scope ) )->get_character();
    if( ch ) {
        const spell_id spell( params[0].str( d ) );
        if( spell == spell_id::NULL_ID() ) {
            ch->magic->caster_level_adjustment = val;
        } else {
            std::map<spell_id, double>::iterator it =
                ch->magic->caster_level_adjustment_by_spell.find( spell );
            if( it != ch->magic->caster_level_adjustment_by_spell.end() ) {
                it->second = val;
            } else {
                ch->magic->caster_level_adjustment_by_spell.insert( { spell, val } );
            }
        }
    }
}

// How many turns one 'unit' is; no unit is turns
double _turns_per_unit( std::string_view unit )
{
    if( !unit.empty() ) {
        decltype( time_duration::units )::const_iterator iter = std::find_if( time_duration::units.cbegin(),
                time_duration::units.cend(),
        [&unit]( std::pair<std::string_view, time_duration> const & u ) {
            return u.first == unit;
        } );

        if( iter == time_duration::units.end() ) {
            throw math::runtime_error( R"(Unknown time unit "%s")", unit );
        } else {
            return to_turns<double>( iter->second );
        }
    }

    return 1.0;
}

double _time_in_unit( double time, std::string_view unit )
{
    return unit.empty() ? time : time / _turns_per_unit( unit );
}

double time_eval( const_dialogue const &d, char /* scope */, std::vector<diag_value> const &params,
                  diag_kwargs const &kwargs )
{
    diag_value unit_val = kwargs.kwarg_or( "unit" );

    std::string const &val_str = params[0].str( d );
    double ret{};
    if( val_str == "now" ) {
        ret = to_turns<double>( calendar::turn - calendar::turn_zero );
    } else if( val_str == "cataclysm" ) {
        ret = to_turns<double>( calendar::start_of_cataclysm - calendar::turn_zero );
    } else {
        ret = to_turns<double>( _read_from_string<time_duration>( val_str, time_duration::units ) );
    }

    return _time_in_unit( ret, unit_val.str( d ) );
}

void time_ass( double val, dialogue &d, char /* scope */,
               std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    // intentionally duplicate check for str to avoid the `Expected str, got ...` error and get the
    // nicer one below
    if( params[0].is_str() && params[0].str() == "now" ) {
        // val is in the 'unit' time('now') reads in, as for time('now', 'unit':'hours') += 1.
        // Rounded: the trip through the unit can land a hair below a whole turn.
        double const turns = val * _turns_per_unit( kwargs.kwarg_or( "unit" ).str( d ) );
        calendar::turn = calendar::turn_zero + time_duration::from_turns<double>( std::round( turns ) );
    } else {
        throw math::syntax_error( "Only time('now') is a valid time() assignment target" );
    }
}

double time_since_eval( const_dialogue const &d, char /* scope */,
                        std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    diag_value unit_val = kwargs.kwarg_or( "unit" );

    double ret{};
    diag_value const &val = params[0];
    if( val.is_empty() ) {
        return -1.0;
    }

    if( val.is_str() ) {
        std::string const &val_str = val.str( d );
        if( val_str == "cataclysm" ) {
            ret = to_turns<double>( calendar::turn - calendar::start_of_cataclysm );
        } else if( val_str == "midnight" ) {
            ret = to_turns<double>( time_past_midnight( calendar::turn ) );
        } else if( val_str == "noon" ) {
            // since the last 12:00, so 0 to 24 hours
            ret = to_turns<double>( calendar::turn - noon( calendar::turn ) );
            if( ret < 0 ) {
                ret += to_turns<double>( 1_days );
            }
        } else {
            throw math::runtime_error( R"(Unknown time point "%s")", val_str );
        }
    } else {
        ret = to_turn<double>( calendar::turn ) - val.dbl( d );
    }
    return _time_in_unit( ret, unit_val.str( d ) );
}

double time_until_eval( const_dialogue const &d, char /* scope */,
                        std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    diag_value unit_val = kwargs.kwarg_or( "unit" );

    double ret{};
    diag_value const &val = params[0];
    if( val.is_empty() ) {
        return -1.0;
    }
    if( val.is_str() ) {
        std::string const &val_str = val.str( d );
        if( val_str == "night_time" ) {
            ret = to_turns<double>( night_time( calendar::turn ) - calendar::turn );
        } else if( val_str == "daylight_time" ) {
            ret = to_turns<double>( daylight_time( calendar::turn ) - calendar::turn );
        } else if( val_str == "sunset" ) {
            ret = to_turns<double>( sunset( calendar::turn ) - calendar::turn );
        } else if( val_str == "sunrise" ) {
            ret = to_turns<double>( sunrise( calendar::turn ) - calendar::turn );
        } else if( val_str == "noon" ) {
            ret = to_turns<double>( noon( calendar::turn ) - calendar::turn );
        } else {
            throw math::runtime_error( R"(Unknown time point "%s")", val_str );
        }

        if( ret < 0 ) {
            ret += to_turns<double>( 1_days );
        }
    } else {
        ret = val.dbl( d ) - to_turn<double>( calendar::turn );
    }

    return _time_in_unit( ret, unit_val.str( d ) );
}

// Time until the EOC next runs, or -1 if it isn't queued. Global EOCs are queued in the game,
// the others on the character they run for (u_ and n_ scopes).  With u_ and n_, it also finds
// those queued in the game for that talker with run_eocs' keep_talkers, whatever it is.
double time_until_eoc_eval( const_dialogue const &d, char scope,
                            std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    diag_value unit_val = kwargs.kwarg_or( "unit" );

    effect_on_condition_id eoc_id( params[0].str( d ) );
    std::optional<time_point> soonest;
    const auto find_in = [&eoc_id, &soonest]( queued_eocs const & queue,
    std::string_view for_handle ) {
        for( queued_eoc const &eoc : queue.list ) {
            if( eoc.eoc == eoc_id && !eoc.cancelled && ( !soonest || eoc.time < *soonest ) &&
                ( for_handle.empty() || ( !eoc.alpha.empty() &&
                                          same_talker_handle( eoc.alpha, for_handle ) ) ) ) {
                soonest = eoc.time;
            }
        }
    };
    if( scope == 'g' ) {
        find_in( g->queued_global_effect_on_conditions, {} );
    } else {
        const_talker const &who = *d.const_actor( is_beta( scope ) );
        if( Character const *chr = who.get_const_character() ) {
            find_in( chr->queued_effect_on_conditions, {} );
        }
        const std::string handle = talker_handle( who );
        if( !handle.empty() ) {
            find_in( g->queued_global_effect_on_conditions, handle );
        }
    }
    if( !soonest ) {
        return -1;
    }
    return _time_in_unit( std::max( 0.0, to_turns<double>( *soonest - calendar::turn ) ),
                          unit_val.str( d ) );
}

double effect_duration_eval( const_dialogue const &d, char scope,
                             std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    diag_value bp_val = kwargs.kwarg_or( "bodypart" );
    diag_value unit_val = kwargs.kwarg_or( "unit" );

    const efftype_id id( params[0].str( d ) );
    bodypart_id const bp = effect_bodypart( id, bp_val.str( d ) );
    effect target = d.const_actor( is_beta( scope ) )->get_effect( id, bp );
    return target.is_null()
           ? -1
           : _time_in_unit( to_seconds<double>( target.get_duration() ), unit_val.str( d ) );
}

// The creature, effect and body part effect_duration() and effect_intensity() set
struct effect_to_set {
    Creature &who;
    efftype_id id;
    bodypart_id bp;
};

effect_to_set get_effect_to_set( dialogue &d, char scope, std::vector<diag_value> const &params,
                                 diag_kwargs const &kwargs, std::string_view func )
{
    Creature *who = d.actor( is_beta( scope ) )->get_creature();
    if( who == nullptr ) {
        throw math::runtime_error( "%s(): only a creature's effects can be set", func );
    }
    const efftype_id id( params[0].str( d ) );
    if( !id.is_valid() ) {
        throw math::runtime_error( R"(%s(): unknown effect "%s")", func, id.str() );
    }
    const std::string bp_str = kwargs.kwarg_or( "bodypart" ).str( d );
    if( !bp_str.empty() && !bodypart_str_id( bp_str ).is_valid() ) {
        throw math::runtime_error( R"(%s(): unknown body part "%s")", func, bp_str );
    }
    return { *who, id, effect_bodypart( id, bp_str ) };
}

// In the unit effect_duration() reads in. A missing effect is added; 0 or less removes it, as
// u_lose_effect does.
void effect_duration_ass( double val, dialogue &d, char scope,
                          std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    const effect_to_set eff = get_effect_to_set( d, scope, params, kwargs, "effect_duration" );
    const std::string unit = kwargs.kwarg_or( "unit" ).str( d );
    const double turns = std::min( std::round( val * _turns_per_unit( unit ) ),
                                   to_turns<double>( calendar::INDEFINITELY_LONG_DURATION ) );
    effect &target = eff.who.get_effect( eff.id, eff.bp );
    if( turns < 1 ) {
        if( !target.is_null() ) {
            eff.who.remove_effect( eff.id, eff.bp );
        }
    } else if( target.is_null() ) {
        eff.who.add_effect( eff.id, time_duration::from_turns<double>( turns ), eff.bp );
    } else {
        target.set_duration( time_duration::from_turns<double>( turns ) );
    }
}

// Clamped to what the effect allows; below 1 removes the effect. A missing effect is left
// alone, as there is no duration to give it.
void effect_intensity_ass( double val, dialogue &d, char scope,
                           std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    const effect_to_set eff = get_effect_to_set( d, scope, params, kwargs, "effect_intensity" );
    const effect &target = eff.who.get_effect( eff.id, eff.bp );
    if( target.is_null() ) {
        return;
    }
    if( std::round( val ) < 1 ) {
        eff.who.remove_effect( eff.id, eff.bp );
        return;
    }
    const int intensity = static_cast<int>( std::min( std::round( val ),
                                            static_cast<double>( target.get_max_intensity() ) ) );
    if( eff.id->intensity_duration() > 0_turns ) {
        // An effect whose intensity follows its duration ignores an intensity it is given;
        // set the duration that gives this intensity instead
        effect &changed = eff.who.get_effect( eff.id, eff.bp );
        changed.set_duration( eff.id->intensity_duration() * ( intensity - 1 ) + 1_turns );
        return;
    }
    // add_effect changes the intensity as the game does, with its messages and morale
    eff.who.add_effect( eff.id, 0_turns, eff.bp, false, intensity, true );
}

double proficiency_eval( const_dialogue const &d, char scope, std::vector<diag_value> const &params,
                         diag_kwargs const &kwargs )
{
    diag_value fmt_val = kwargs.kwarg_or( "format", "time_spent" );

    proficiency_id prof( params[0].str( d ) );
    time_duration raw = d.const_actor( is_beta( scope ) )->proficiency_practiced_time( prof );
    std::string const &format = fmt_val.str( d );
    if( format == "percent" ) {
        return raw * 100.0 / prof->time_to_learn();
    } else if( format == "permille" ) {
        return static_cast<double>( raw * 1000 / prof->time_to_learn() );
    } else if( format == "total_time_required" ) {
        return to_turns<double>( prof->time_to_learn() );
    } else if( format == "time_left" ) {
        return to_turns<double>( prof->time_to_learn() - raw );
    } else {
        if( format != "time_spent" ) {
            throw math::runtime_error( R"(Unknown format type "%s" for proficiency)", format );
        }
        return to_turns<double>( raw );
    }
}

void proficiency_ass( double val, dialogue &d, char scope, std::vector<diag_value> const &params,
                      diag_kwargs const &kwargs )
{
    diag_value fmt_val = kwargs.kwarg_or( "format", "time_spent" );
    diag_value direct_val = kwargs.kwarg_or( "direct" );

    proficiency_id prof( params[0].str( d ) );
    std::string const &format = fmt_val.str( d );
    bool const direct = is_true( direct_val.dbl( d ) );
    int to_write = 0;
    if( format == "percent" ) {
        to_write = to_turns<int>( prof->time_to_learn() * val ) / 100;
    } else if( format == "permille" ) {
        to_write = to_turns<int>( prof->time_to_learn() * val ) / 1000;
    } else if( format == "time_left" ) {
        to_write = to_turns<int>( prof->time_to_learn() ) - val;
    } else {
        if( format != "time_spent" ) {
            throw math::runtime_error( R"(Unknown format type "%s" for proficiency)", format );
        }
        to_write = val;
    }
    int before = to_turns<int>( d.actor( is_beta( scope ) )->proficiency_practiced_time( prof ) );
    int learned = to_write - before;
    // Due to rounding errors, -1 can occur in normal situations. When that happens, ignore it
    if( !direct && learned < 1 ) {
        if( learned < -1 ) {
            throw math::runtime_error(
                "For proficiency %s in dialogue, trying to learn negative without direct",
                prof.str() );
        }
        return;
    }
    if( !direct ) {
        d.actor( is_beta( scope ) )->train_proficiency_for( prof, learned );
    } else {
        d.actor( is_beta( scope ) )->set_proficiency_practiced_time( prof, to_write );
    }
}

double _test_add( diag_value const &v, const_dialogue const &d )
{
    double ret{};
    if( v.is_array() ) {
        for( diag_value const &w : v.array( d ) ) {
            ret += _test_add( w, d );
        }
    } else {
        ret += v.dbl( d );
    }
    return ret;
}
double _test_len( diag_value const &v, const_dialogue const &d )
{
    double ret{};
    for( diag_value const &w : v.array( d ) ) {
        ret += w.str( d ).length();
    }
    return ret;
}
double _test_func( const_dialogue const &d, std::vector<diag_value> const &params,
                   diag_kwargs const &kwargs,
                   double ( *f )( diag_value const &v, const_dialogue const &d ) )
{
    std::vector<diag_value> all_params( params );
    for( diag_kwargs::impl_t::value_type const &v : kwargs.kwargs ) {
        all_params.emplace_back( v.second );
    }
    double ret = 0;
    for( diag_value const &v : all_params ) {
        ret += f( v, d );
    }
    return ret;
}

double test_diag( const_dialogue const &d, char /* scope */, std::vector<diag_value> const &params,
                  diag_kwargs const &kwargs )
{
    return _test_func( d, params, kwargs, _test_add );
}

double test_str_len( const_dialogue const &d, char /* scope */,
                     std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    return _test_func( d, params, kwargs, _test_len );
}

double ugliness_eval( const_dialogue const &d, char scope,
                      std::vector<diag_value> const & /* params */,
                      diag_kwargs const & /* kwargs */ )
{
    return d.const_actor( is_beta( scope ) )->get_ugliness();
}

double value_or_eval( const_dialogue const &d, char /* scope */,
                      std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    diag_value const &has = params[0];
    if( !has.is_empty() ) {
        return has.dbl( d );
    }
    return params[1].dbl( d );
}

double vision_range_eval( const_dialogue const &d, char scope,
                          std::vector<diag_value> const & /* params */,
                          diag_kwargs const & /* kwargs */ )
{
    const_talker const *const actor = d.const_actor( is_beta( scope ) );
    if( Character const *const chr = actor->get_const_character(); chr != nullptr ) {
        return chr->unimpaired_range();
    } else if( monster const *const mon = actor->get_const_monster(); mon != nullptr ) {
        map &here = get_map();
        return mon->sight_range( here.ambient_light_at( mon->pos_bub() ) );
    }
    throw math::runtime_error( "Tried to access vision range of a non-Character talker" );
}

double npc_anger_eval( const_dialogue const &d, char scope,
                       std::vector<diag_value> const & /* params */,
                       diag_kwargs const & /* kwargs */ )
{
    if( d.const_actor( is_beta( scope ) ) ) {
        return d.const_actor( is_beta( scope ) )->get_npc_anger();
    } else {
        return 0;
    }
}

double npc_fear_eval( const_dialogue const &d, char scope,
                      std::vector<diag_value> const & /* params */,
                      diag_kwargs const & /* kwargs */ )
{
    if( d.const_actor( is_beta( scope ) ) ) {
        return d.const_actor( is_beta( scope ) )->get_npc_fear();
    } else {
        return 0;
    }
}

double npc_value_eval( const_dialogue const &d, char scope,
                       std::vector<diag_value> const & /* params */,
                       diag_kwargs const & /* kwargs */ )
{
    if( d.const_actor( is_beta( scope ) ) ) {
        return d.const_actor( is_beta( scope ) )->get_npc_value();
    } else {
        return 0;
    }
}

// turns since the NPC's current goal started; 0 when it has none, or isn't an NPC
double npc_goal_turns_eval( const_dialogue const &d, char scope,
                            std::vector<diag_value> const & /* params */,
                            diag_kwargs const & /* kwargs */ )
{
    const const_talker *who = d.const_actor( is_beta( scope ) );
    const npc *guy = who == nullptr ? nullptr : who->get_const_npc();
    return guy == nullptr ? 0.0 : npc_goal_runtime::turns_on_goal( *guy );
}

// the NPC u or n is; nullptr when that talker isn't an NPC, or there is none
const npc *scoped_npc( const_dialogue const &d, char scope )
{
    const bool beta = is_beta( scope );
    return d.has_actor( beta ) ? d.const_actor( beta )->get_const_npc() : nullptr;
}

npc *scoped_npc( dialogue &d, char scope )
{
    const bool beta = is_beta( scope );
    return d.has_actor( beta ) ? d.actor( beta )->get_npc() : nullptr;
}

// a personality trait npc_personality() names, nullptr for another name
int8_t *personality_trait( npc_personality &p, std::string_view trait )
{
    if( trait == "aggression" ) {
        return &p.aggression;
    } else if( trait == "bravery" ) {
        return &p.bravery;
    } else if( trait == "collector" ) {
        return &p.collector;
    } else if( trait == "altruism" ) {
        return &p.altruism;
    }
    return nullptr;
}

constexpr const char *personality_traits = "aggression, bravery, collector or altruism";

void npc_personality_check( std::vector<diag_value> const &params )
{
    npc_personality p;
    if( params[0].is_str() && personality_trait( p, params[0].str() ) == nullptr ) {
        throw math::syntax_error( R"(npc_personality(): "%s" isn't %s)", params[0].str(),
                                  personality_traits );
    }
}

// npc_personality('aggression'): the trait, -10 to 10; 0 for a talker that isn't an NPC
double npc_personality_eval( const_dialogue const &d, char scope,
                             std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    const std::string trait( params[0].str( d ) );
    npc_personality p;
    if( personality_trait( p, trait ) == nullptr ) {
        throw math::runtime_error( R"(npc_personality(): "%s" isn't %s)", trait, personality_traits );
    }
    const npc *guy = scoped_npc( d, scope );
    if( guy == nullptr ) {
        return 0.0;
    }
    p = guy->personality;
    return *personality_trait( p, trait );
}

// clamped to the personality range, as the npc_class bonuses are
void npc_personality_ass( double val, dialogue &d, char scope,
                          std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    const std::string trait( params[0].str( d ) );
    npc *guy = scoped_npc( d, scope );
    npc_personality none;
    int8_t *value = personality_trait( guy == nullptr ? none : guy->personality, trait );
    if( value == nullptr ) {
        throw math::runtime_error( R"(npc_personality(): "%s" isn't %s)", trait, personality_traits );
    }
    if( guy != nullptr ) {
        *value = static_cast<int8_t>( std::clamp( val, static_cast<double>( NPC_PERSONALITY_MIN ),
                                      static_cast<double>( NPC_PERSONALITY_MAX ) ) );
    }
}

// npc_danger(): the danger the NPC's AI rates around it, as npc_in_danger reads it
double npc_danger_eval( const_dialogue const &d, char scope,
                        std::vector<diag_value> const & /* params */, diag_kwargs const & /* kwargs */ )
{
    const npc *guy = scoped_npc( d, scope );
    return guy == nullptr ? 0.0 : guy->get_ai_danger();
}

// npc_panic(): how panicked the NPC is in a fight; it runs, and stops using cover, as it grows
double npc_panic_eval( const_dialogue const &d, char scope,
                       std::vector<diag_value> const & /* params */, diag_kwargs const & /* kwargs */ )
{
    const npc *guy = scoped_npc( d, scope );
    return guy == nullptr ? 0.0 : guy->mem_combat.panic;
}

constexpr const char *need_goals =
    "drink_water, eat_food, go_to_sleep, regulate_temperature or an npc_need";

void npc_need_urgency_check( std::vector<diag_value> const &params )
{
    // a custom need may load after the math that names it: then it is checked once data loaded
    if( params[0].is_str() && !npc_custom_needs::may_name_need( params[0].str() ) ) {
        throw math::syntax_error( R"(npc_need_urgency(): "%s" isn't %s)", params[0].str(), need_goals );
    }
}

// npc_need_urgency('drink_water'): the need's urgency, 0 to 1, as the arbitration and the
// built-in scores have it; a custom need's (npc_need) as its own urgency gives it
double npc_need_urgency_eval( const_dialogue const &d, char scope,
                              std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    const std::string need( params[0].str( d ) );
    const std::optional<npc::need_goal_id> id = npc::goal_id_for( need );
    const npc_custom_need *custom = id ? nullptr : npc_custom_needs::find( need );
    if( !id && custom == nullptr ) {
        throw math::runtime_error( R"(npc_need_urgency(): "%s" isn't %s)", need, need_goals );
    }
    const npc *guy = scoped_npc( d, scope );
    if( guy == nullptr ) {
        return 0.0;
    }
    return id ? guy->current_need_urgency( *id ) : custom->urgency_for( *guy );
}

double npc_trust_eval( const_dialogue const &d, char scope,
                       std::vector<diag_value> const & /* params */,
                       diag_kwargs const & /* kwargs */ )
{
    if( d.const_actor( is_beta( scope ) ) ) {
        return d.const_actor( is_beta( scope ) )->get_npc_trust();
    } else {
        return 0;
    }
}

void npc_anger_ass( double val, dialogue &d, char scope,
                    std::vector<diag_value> const & /* params */, diag_kwargs const & /* kwargs */ )
{
    d.actor( is_beta( scope ) )->set_npc_anger( val );
}

void npc_fear_ass( double val, dialogue &d, char scope,
                   std::vector<diag_value> const & /* params */, diag_kwargs const & /* kwargs */ )
{
    d.actor( is_beta( scope ) )->set_npc_fear( val );
}

void npc_value_ass( double val, dialogue &d, char scope,
                    std::vector<diag_value> const & /* params */, diag_kwargs const & /* kwargs */ )
{
    d.actor( is_beta( scope ) )->set_npc_value( val );
}

void npc_trust_ass( double val, dialogue &d, char scope,
                    std::vector<diag_value> const & /* params */, diag_kwargs const & /* kwargs */ )
{
    d.actor( is_beta( scope ) )->set_npc_trust( val );
}

double trauma_eval( const_dialogue const &d, char scope,
                    std::vector<diag_value> const & /* params */,
                    diag_kwargs const & /* kwargs */ )
{
    return d.const_actor( is_beta( scope ) )->get_trauma();
}

void trauma_ass( double val, dialogue &d, char scope,
                 std::vector<diag_value> const & /* params */, diag_kwargs const & /* kwargs */ )
{
    d.actor( is_beta( scope ) )->set_trauma( val );
}

double gender_eval( const_dialogue const &d, char scope,
                    std::vector<diag_value> const & /* params */,
                    diag_kwargs const & /* kwargs */ )
{
    // This has no talker overload because it's only relevant to characters.
    const Character *maybe_guy = d.const_actor( is_beta( scope ) )->get_const_character();
    if( maybe_guy ) {
        return maybe_guy->male;
    } else {
        return 0;
    }
}

void gender_ass( double val, dialogue &d, char scope,
                 std::vector<diag_value> const & /* params */, diag_kwargs const & /* kwargs */ )
{
    // This has no talker overload because it's only relevant to characters.
    Character *maybe_guy_maybe_girl_soon = d.actor( is_beta( scope ) )->get_character();
    if( maybe_guy_maybe_girl_soon ) {
        maybe_guy_maybe_girl_soon->male = val;
    }
}

double calories_eval( const_dialogue const &d, char scope,
                      std::vector<diag_value> const & /* params */, diag_kwargs const &kwargs )
{
    diag_value format_value = kwargs.kwarg_or( "format", "raw" );

    std::string const &format = format_value.str( d );
    if( format != "raw" && format != "percent" ) {
        throw math::runtime_error( R"(Unknown format type "%s" for calories)", format );
    }

    if( format == "percent" ) {
        if( d.const_actor( is_beta( scope ) )->get_const_character() ) {
            double divisor = d.const_actor( is_beta( scope ) )->get_healthy_kcal() / 100.0;
            // if no data, default to default height of 175cm
            if( divisor == 0 ) {
                debugmsg( "Can't get healthy amount of calories, return raw calories instead" );
                return d.const_actor( is_beta( scope ) )->get_stored_kcal();
            }
            return d.const_actor( is_beta( scope ) )->get_stored_kcal() / divisor;
        } else {
            throw math::runtime_error( "Percent can be used only with character" );
        }
    } else if( format == "raw" ) {
        if( d.const_actor( is_beta( scope ) )->get_const_character() ) {
            return d.const_actor( is_beta( scope ) )->get_stored_kcal();
        }
        item_location const *it = d.const_actor( is_beta( scope ) )->get_const_item();
        if( it && *it ) {
            return default_character_compute_effective_nutrients( *it->get_item() ).kcal();
        }
    }
    throw math::runtime_error( "For calories(), talker is not character nor item" );
}

void calories_ass( double val, dialogue &d, char scope,
                   std::vector<diag_value> const & /* params */, diag_kwargs const &kwargs )
{
    diag_value ignore_weariness_val = kwargs.kwarg_or( "dont_affect_weariness" );
    diag_value format_value = kwargs.kwarg_or( "format", "raw" );

    // val is in the format calories() reads in: with 'percent', percent of the healthy amount
    std::string const &format = format_value.str( d );
    if( format != "raw" && format != "percent" ) {
        throw math::runtime_error( R"(Unknown format type "%s" for calories)", format );
    }
    if( format == "percent" ) {
        if( !d.actor( is_beta( scope ) )->get_character() ) {
            throw math::runtime_error( "Percent can be used only with character" );
        }
        double divisor = d.actor( is_beta( scope ) )->get_healthy_kcal() / 100.0;
        if( divisor == 0 ) {
            debugmsg( "Can't get healthy amount of calories, set raw calories instead" );
        } else {
            val = std::round( val * divisor );
        }
    }

    const bool ignore_weariness = is_true( ignore_weariness_val.dbl( d ) );
    int current_kcal = d.actor( is_beta( scope ) )->get_stored_kcal();
    int difference = val - current_kcal;
    d.actor( is_beta( scope ) )->mod_stored_kcal( difference, ignore_weariness );
}

double weight_eval( const_dialogue const &d, char scope,
                    std::vector<diag_value> const & /* params */, diag_kwargs const & /* kwargs */ )
{
    const_talker const *actor = d.const_actor( is_beta( scope ) );
    // in milligrams, as a double: a vehicle (cargo and passengers included) or a big monster
    // is heavier than the 2147 kg an int holds
    if( actor->get_const_character() || actor->get_const_monster() ||
        actor->get_const_vehicle() ) {
        return static_cast<double>( to_milligram( actor->get_weight() ) );
    }
    item_location const *it = actor->get_const_item();
    if( it && *it ) {
        return static_cast<double>( to_milligram( it->get_item()->weight() ) );
    }
    throw math::runtime_error( "For weight(), talker is not a creature, item or vehicle" );
}

double quality_eval( const_dialogue const &d, char scope,
                     std::vector<diag_value> const &params /* params */,
                     diag_kwargs const &kwargs /* kwargs */ )
{
    diag_value strict_val = kwargs.kwarg_or( "strict" );

    std::string const &quality = params[0].str( d );
    bool strict = is_true( strict_val.dbl( d ) );

    return d.const_actor( is_beta( scope ) )->get_quality( quality, strict );
}

double volume_eval( const_dialogue const &d, char scope,
                    std::vector<diag_value> const & /* params */, diag_kwargs const & /* kwargs */ )
{
    if( d.const_actor( is_beta( scope ) )->get_const_character() ||
        d.const_actor( is_beta( scope ) )->get_const_monster() ) {
        return d.const_actor( is_beta( scope ) )->get_volume();
    }
    item_location const *it = d.const_actor( is_beta( scope ) )->get_const_item();
    if( it && *it ) {
        return to_milliliter( it->get_item()->volume() );
    }
    throw math::runtime_error( "For volume(), talker is not character nor item" );
}

Character const *carrier_talker( const_dialogue const &d, char scope, std::string_view func )
{
    Character const *const chr = d.const_actor( is_beta( scope ) )->get_const_character();
    if( chr == nullptr ) {
        throw math::runtime_error( "For %s(), talker must be a character", func );
    }
    return chr;
}

item const *container_talker( const_dialogue const &d, char scope, std::string_view func )
{
    item_location const *const it = d.const_actor( is_beta( scope ) )->get_const_item();
    if( it == nullptr || !*it ) {
        throw math::runtime_error( "For %s(), talker must be an item", func );
    }
    return it->get_item();
}

// Totals over an item's container pockets (the pockets that hold contents, not magazines,
// mods or software). Per-pocket free space is clamped at zero, so an overfilled pocket does
// not hide the room left in its neighbours.
struct container_totals {
    units::volume volume_capacity = 0_ml;
    units::volume contents_volume = 0_ml;
    units::volume remaining_volume = 0_ml;
    units::mass weight_capacity = 0_gram;
    units::mass contents_weight = 0_gram;
    units::mass remaining_weight = 0_gram;
    int contents_count = 0;
};

container_totals get_container_totals( item const &it )
{
    container_totals ret;
    for( item_pocket const *pocket : it.get_pockets( item_pocket::ok_default_containers ) ) {
        ret.volume_capacity += pocket->volume_capacity();
        ret.contents_volume += pocket->contents_volume();
        ret.remaining_volume += std::max( pocket->remaining_volume(), 0_ml );
        ret.weight_capacity += pocket->weight_capacity();
        ret.contents_weight += pocket->contains_weight();
        ret.remaining_weight += std::max( pocket->remaining_weight(), 0_gram );
        for( item const *content : pocket->all_items_top() ) {
            ret.contents_count += content->count();
        }
    }
    return ret;
}

double weight_carried_eval( const_dialogue const &d, char scope,
                            std::vector<diag_value> const & /* params */,
                            diag_kwargs const & /* kwargs */ )
{
    return units::to_milligram( carrier_talker( d, scope, "weight_carried" )->weight_carried() );
}

double weight_capacity_eval( const_dialogue const &d, char scope,
                             std::vector<diag_value> const & /* params */,
                             diag_kwargs const & /* kwargs */ )
{
    if( Character const *chr = d.const_actor( is_beta( scope ) )->get_const_character() ) {
        return units::to_milligram( chr->weight_capacity() );
    }
    item_location const *it = d.const_actor( is_beta( scope ) )->get_const_item();
    if( it && *it ) {
        return units::to_milligram( get_container_totals( *it->get_item() ).weight_capacity );
    }
    throw math::runtime_error( "For weight_capacity(), talker is not character nor item" );
}

double volume_carried_eval( const_dialogue const &d, char scope,
                            std::vector<diag_value> const & /* params */,
                            diag_kwargs const & /* kwargs */ )
{
    return units::to_milliliter( carrier_talker( d, scope, "volume_carried" )->volume_carried() );
}

double volume_capacity_eval( const_dialogue const &d, char scope,
                             std::vector<diag_value> const & /* params */,
                             diag_kwargs const & /* kwargs */ )
{
    if( Character const *chr = d.const_actor( is_beta( scope ) )->get_const_character() ) {
        return units::to_milliliter( chr->volume_capacity() );
    }
    item_location const *it = d.const_actor( is_beta( scope ) )->get_const_item();
    if( it && *it ) {
        return units::to_milliliter( get_container_totals( *it->get_item() ).volume_capacity );
    }
    throw math::runtime_error( "For volume_capacity(), talker is not character nor item" );
}

double free_volume_eval( const_dialogue const &d, char scope,
                         std::vector<diag_value> const & /* params */,
                         diag_kwargs const & /* kwargs */ )
{
    return units::to_milliliter( carrier_talker( d, scope, "free_volume" )->free_space() );
}

double contents_count_eval( const_dialogue const &d, char scope,
                            std::vector<diag_value> const & /* params */,
                            diag_kwargs const & /* kwargs */ )
{
    return get_container_totals( *container_talker( d, scope, "contents_count" ) ).contents_count;
}

double contents_volume_eval( const_dialogue const &d, char scope,
                             std::vector<diag_value> const & /* params */,
                             diag_kwargs const & /* kwargs */ )
{
    return units::to_milliliter( get_container_totals( *container_talker( d, scope,
                                 "contents_volume" ) ).contents_volume );
}

double contents_weight_eval( const_dialogue const &d, char scope,
                             std::vector<diag_value> const & /* params */,
                             diag_kwargs const & /* kwargs */ )
{
    return units::to_milligram( get_container_totals( *container_talker( d, scope,
                                "contents_weight" ) ).contents_weight );
}

double remaining_volume_eval( const_dialogue const &d, char scope,
                              std::vector<diag_value> const & /* params */,
                              diag_kwargs const & /* kwargs */ )
{
    return units::to_milliliter( get_container_totals( *container_talker( d, scope,
                                 "remaining_volume" ) ).remaining_volume );
}

double remaining_weight_eval( const_dialogue const &d, char scope,
                              std::vector<diag_value> const & /* params */,
                              diag_kwargs const & /* kwargs */ )
{
    return units::to_milligram( get_container_totals( *container_talker( d, scope,
                                "remaining_weight" ) ).remaining_weight );
}

// USE-J10: the state of an item talker: active, countdown, item_counter, ammo and charges.
item_location &mutable_item_talker( dialogue &d, char scope, std::string_view func )
{
    item_location *const it = d.actor( is_beta( scope ) )->get_item();
    if( it == nullptr || !*it ) {
        throw math::runtime_error( "For %s(), talker must be an item", func );
    }
    return *it;
}

// A value written to an item's state, as an int kept within 0 and INT_MAX.
int item_state_amount( double val )
{
    return static_cast<int>( std::llround( std::clamp( val, 0.0, static_cast<double>( INT_MAX ) ) ) );
}

// Its weight or contents changed: the carrier's and container's caches must know.
void item_state_changed( item_location &loc )
{
    loc->on_contents_changed();
    if( loc.has_parent() ) {
        loc.parent_item()->on_contents_changed();
    }
    if( Character *const carrier = loc.carrier() ) {
        carrier->invalidate_weight_carried_cache();
    }
}

double item_active_eval( const_dialogue const &d, char scope,
                         std::vector<diag_value> const & /* params */,
                         diag_kwargs const & /* kwargs */ )
{
    return container_talker( d, scope, "item_active" )->active ? 1 : 0;
}

void item_active_ass( double val, dialogue &d, char scope,
                      std::vector<diag_value> const & /* params */,
                      diag_kwargs const & /* kwargs */ )
{
    item_location &loc = mutable_item_talker( d, scope, "item_active" );
    if( is_true( val ) ) {
        loc->active = true;
        // an item on the ground or in a vehicle is processed once it is in the active item list
        loc.make_active();
    } else {
        // as transform_item: an item with a temperature stays active to keep it up to date
        loc->active = loc->has_temperature();
    }
}

double item_counter_eval( const_dialogue const &d, char scope,
                          std::vector<diag_value> const & /* params */,
                          diag_kwargs const & /* kwargs */ )
{
    return container_talker( d, scope, "item_counter" )->item_counter;
}

void item_counter_ass( double val, dialogue &d, char scope,
                       std::vector<diag_value> const & /* params */,
                       diag_kwargs const & /* kwargs */ )
{
    mutable_item_talker( d, scope, "item_counter" )->item_counter = item_state_amount( val );
}

itype_id item_ammo_param( const_dialogue const &d, diag_value const &param )
{
    const itype_id ammo( param.str( d ) );
    if( !ammo.is_valid() || !ammo->ammo ) {
        throw math::runtime_error( R"(For item_ammo(), "%s" is not an ammo)", ammo.str() );
    }
    return ammo;
}

double item_ammo_eval( const_dialogue const &d, char scope, std::vector<diag_value> const &params,
                       diag_kwargs const & /* kwargs */ )
{
    item const *const it = container_talker( d, scope, "item_ammo" );
    return it->ammo_remaining_of( item_ammo_param( d, params[0] ) );
}

void item_ammo_ass( double val, dialogue &d, char scope, std::vector<diag_value> const &params,
                    diag_kwargs const & /* kwargs */ )
{
    item_location &loc = mutable_item_talker( d, scope, "item_ammo" );
    const itype_id ammo = item_ammo_param( d, params[0] );
    const ammotype &type = ammo->ammo->type;
    // the magazine that takes this ammo, or the item's own magazine pocket
    item *target = loc.get_item();
    for( item *mag : loc->magazines_current() ) {
        if( mag->ammo_capacity( type ) > 0 ) {
            target = mag;
            break;
        }
    }
    const int capacity = target->ammo_capacity( type );
    if( capacity <= 0 ) {
        // nothing takes it now: no magazine, or a pocket for another ammo
        return;
    }
    const int amount = std::min( item_state_amount( val ), capacity );
    if( amount == target->ammo_remaining_of( ammo ) ) {
        return;
    }
    // what the magazine held is replaced: 0 empties it
    target->ammo_set( ammo, amount );
    item_state_changed( loc );
}

vehicle const *vehicle_talker( const_dialogue const &d, char scope, std::string_view func )
{
    vehicle const *const veh = d.const_actor( is_beta( scope ) )->get_const_vehicle();
    if( veh == nullptr ) {
        throw math::runtime_error( "For %s(), talker must be a vehicle", func );
    }
    return veh;
}

vehicle *mutable_vehicle_talker( dialogue &d, char scope, std::string_view func )
{
    vehicle *const veh = d.actor( is_beta( scope ) )->get_vehicle();
    if( veh == nullptr ) {
        throw math::runtime_error( "For %s(), talker must be a vehicle", func );
    }
    return veh;
}

itype_id vehicle_fuel_param( const_dialogue const &d, diag_value const &param )
{
    const itype_id fuel( param.str( d ) );
    if( !fuel.is_valid() ) {
        throw math::runtime_error( R"(Unknown fuel "%s")", fuel.str() );
    }
    return fuel;
}

std::string vehicle_part_flag_param( const_dialogue const &d, diag_value const &param )
{
    std::string const &flag = param.str( d );
    if( !flag_id( flag ).is_valid() ) {
        throw math::runtime_error( R"(Unknown vehicle part flag "%s")", flag );
    }
    return flag;
}

// An amount of fuel or charge as the int the vehicle functions take.
int64_t vehicle_amount( double val )
{
    return std::llround( std::clamp( val, 0.0, static_cast<double>( INT_MAX ) ) );
}

// Battery charge is shared by every vehicle and appliance connected by cables; reading and
// writing it ignore the cable loss so a written value reads back unchanged.
void set_vehicle_battery( vehicle &veh, map &here, double val )
{
    const int64_t current = veh.battery_left( here, false );
    const int64_t target = vehicle_amount( val );
    const int change = static_cast<int>( std::min<int64_t>( std::abs( target - current ),
                                         INT_MAX ) );
    if( target > current ) {
        veh.charge_battery( here, change, false );
    } else if( target < current ) {
        veh.discharge_battery( here, change, false );
    }
}

double vehicle_battery_eval( const_dialogue const &d, char scope,
                             std::vector<diag_value> const & /* params */,
                             diag_kwargs const & /* kwargs */ )
{
    return vehicle_talker( d, scope, "vehicle_battery" )->battery_left( get_map(), false );
}

void vehicle_battery_ass( double val, dialogue &d, char scope,
                          std::vector<diag_value> const & /* params */,
                          diag_kwargs const & /* kwargs */ )
{
    set_vehicle_battery( *mutable_vehicle_talker( d, scope, "vehicle_battery" ), get_map(), val );
}

double vehicle_battery_max_eval( const_dialogue const &d, char scope,
                                 std::vector<diag_value> const & /* params */,
                                 diag_kwargs const & /* kwargs */ )
{
    return vehicle_talker( d, scope, "vehicle_battery_max" )->fuel_capacity( get_map(),
            itype_battery );
}

double vehicle_fuel_eval( const_dialogue const &d, char scope,
                          std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    vehicle const *const veh = vehicle_talker( d, scope, "vehicle_fuel" );
    const itype_id fuel = vehicle_fuel_param( d, params[0] );
    if( fuel == itype_battery ) {
        return veh->battery_left( get_map(), false );
    }
    return veh->fuel_left( get_map(), fuel );
}

void vehicle_fuel_ass( double val, dialogue &d, char scope, std::vector<diag_value> const &params,
                       diag_kwargs const & /* kwargs */ )
{
    vehicle *const veh = mutable_vehicle_talker( d, scope, "vehicle_fuel" );
    const itype_id fuel = vehicle_fuel_param( d, params[0] );
    map &here = get_map();
    if( fuel == itype_battery ) {
        set_vehicle_battery( *veh, here, val );
        return;
    }
    const int64_t current = veh->fuel_left( here, fuel );
    const int64_t target = vehicle_amount( val );
    if( target < current ) {
        const int excess = static_cast<int>( std::min<int64_t>( current - target, INT_MAX ) );
        // take it only from the tanks fuel_left() counts: not racked vehicles' or frozen ones
        veh->drain( here, fuel, excess, []( vehicle_part & vp ) {
            return !vp.has_flag( vp_flag::carried_flag ) &&
                   !( vp.is_tank() && !vp.get_base().empty() &&
                      vp.get_base().legacy_front().made_of( phase_id::SOLID ) );
        } );
        return;
    }
    int64_t to_add = target - current;
    const item fuel_item( fuel );
    const auto fill = [veh, &fuel, &fuel_item, &to_add]( bool empty_stores ) {
        for( const int idx : veh->fuel_containers ) {
            vehicle_part &vp = veh->part( idx );
            if( to_add <= 0 || vp.is_broken() || vp.has_flag( vp_flag::carried_flag ) ||
                vp.is_battery() ) {
                continue;
            }
            if( empty_stores ? !vp.ammo_current().is_null() || !vp.can_reload( fuel_item ) :
                vp.ammo_current() != fuel ) {
                continue;
            }
            const int held = empty_stores ? 0 : vp.ammo_remaining();
            const int room = vp.item_capacity( fuel ) - held;
            if( room <= 0 ) {
                continue;
            }
            const int added = static_cast<int>( std::min<int64_t>( room, to_add ) );
            if( vp.ammo_set( fuel, held + added ) > held ) {
                to_add -= added;
            }
        }
    };
    // top up the stores that already hold this fuel, then fill empty ones that accept it
    fill( false );
    fill( true );
    veh->invalidate_mass();
}

double vehicle_fuel_capacity_eval( const_dialogue const &d, char scope,
                                   std::vector<diag_value> const &params,
                                   diag_kwargs const & /* kwargs */ )
{
    vehicle const *const veh = vehicle_talker( d, scope, "vehicle_fuel_capacity" );
    return veh->fuel_capacity( get_map(), vehicle_fuel_param( d, params[0] ) );
}

double vehicle_engine_on_eval( const_dialogue const &d, char scope,
                               std::vector<diag_value> const & /* params */,
                               diag_kwargs const & /* kwargs */ )
{
    return vehicle_talker( d, scope, "vehicle_engine_on" )->engine_on ? 1 : 0;
}

void vehicle_engine_on_ass( double val, dialogue &d, char scope,
                            std::vector<diag_value> const & /* params */,
                            diag_kwargs const & /* kwargs */ )
{
    vehicle *const veh = mutable_vehicle_talker( d, scope, "vehicle_engine_on" );
    map &here = get_map();
    if( !is_true( val ) ) {
        if( veh->engine_on ) {
            veh->stop_engines( here );
        }
        return;
    }
    if( veh->engine_on ) {
        return;
    }
    // like vehicle::start_engines(): with no engine switched on, switch on every working one
    if( std::none_of( veh->engines.begin(), veh->engines.end(), [veh]( int idx ) {
    return veh->part( idx ).enabled && !veh->part( idx ).is_broken();
    } ) ) {
        for( const int idx : veh->engines ) {
            veh->part( idx ).enabled = !veh->part( idx ).is_broken();
        }
    }
    // like vehicle::start_engine(), switch an engine to another fuel it burns when its own is out
    for( const int idx : veh->engines ) {
        if( veh->part( idx ).enabled ) {
            veh->auto_select_fuel( here, veh->part( idx ) );
        }
    }
    // engines without fuel would stall on the next turn anyway (vehicle::idle)
    veh->engine_on = veh->total_power( here ) > 0_W;
}

// Speeds are stored in hundredths of a mile per hour.
double vehicle_velocity_eval( const_dialogue const &d, char scope,
                              std::vector<diag_value> const & /* params */,
                              diag_kwargs const & /* kwargs */ )
{
    return vehicle_talker( d, scope, "vehicle_velocity" )->velocity / 100.0;
}

void vehicle_velocity_ass( double val, dialogue &d, char scope,
                           std::vector<diag_value> const & /* params */,
                           diag_kwargs const & /* kwargs */ )
{
    vehicle *const veh = mutable_vehicle_talker( d, scope, "vehicle_velocity" );
    map &here = get_map();
    // what the engines could reach with fuel, so an empty tank does not pin the vehicle
    const double low = veh->max_reverse_velocity( here, false );
    const double high = veh->max_velocity( here, false );
    veh->velocity = static_cast<int>( std::lround( std::clamp( val * 100.0, low, high ) ) );
}

double vehicle_cruise_eval( const_dialogue const &d, char scope,
                            std::vector<diag_value> const & /* params */,
                            diag_kwargs const & /* kwargs */ )
{
    return vehicle_talker( d, scope, "vehicle_cruise" )->cruise_velocity / 100.0;
}

void vehicle_cruise_ass( double val, dialogue &d, char scope,
                         std::vector<diag_value> const & /* params */,
                         diag_kwargs const & /* kwargs */ )
{
    vehicle *const veh = mutable_vehicle_talker( d, scope, "vehicle_cruise" );
    map &here = get_map();
    // the same limits as vehicle::cruise_thrust()
    const double low = veh->max_reverse_velocity( here );
    const double high = veh->max_velocity( here );
    veh->cruise_velocity = static_cast<int>( std::lround( std::clamp( val * 100.0, low, high ) ) );
}

double vehicle_max_speed_eval( const_dialogue const &d, char scope,
                               std::vector<diag_value> const & /* params */,
                               diag_kwargs const & /* kwargs */ )
{
    return vehicle_talker( d, scope, "vehicle_max_speed" )->max_velocity( get_map() ) / 100.0;
}

double vehicle_power_eval( const_dialogue const &d, char scope,
                           std::vector<diag_value> const & /* params */,
                           diag_kwargs const & /* kwargs */ )
{
    return units::to_watt( vehicle_talker( d, scope, "vehicle_power" )->total_power( get_map() ) );
}

double vehicle_locked_eval( const_dialogue const &d, char scope,
                            std::vector<diag_value> const & /* params */,
                            diag_kwargs const & /* kwargs */ )
{
    return vehicle_talker( d, scope, "vehicle_locked" )->is_locked ? 1 : 0;
}

void vehicle_locked_ass( double val, dialogue &d, char scope,
                         std::vector<diag_value> const & /* params */,
                         diag_kwargs const & /* kwargs */ )
{
    mutable_vehicle_talker( d, scope, "vehicle_locked" )->is_locked = is_true( val );
}

// The parts vehicle_part_count(), vehicle_part_broken_count() and vehicle_part_hp() look at:
// those with the flag given as the argument and of the vehicle part id given as the "part"
// kwarg (VINT-J01); with neither, every part. Parts of a racked vehicle never count.
struct vehicle_part_query {
    std::string flag;
    std::optional<vpart_id> part;

    bool matches( const vehicle_part &vp ) const {
        return !vp.removed && !vp.is_fake && !vp.has_flag( vp_flag::carried_flag ) &&
               ( flag.empty() || vp.info().has_flag( flag ) ) && ( !part || vp.info().id == *part );
    }
};

vehicle_part_query vehicle_part_query_params( const_dialogue const &d, std::string_view func,
        std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    if( params.size() > 1 ) {
        throw math::runtime_error( "%s() takes at most one argument, a vehicle part flag", func );
    }
    vehicle_part_query query;
    if( !params.empty() ) {
        query.flag = vehicle_part_flag_param( d, params[0] );
    }
    if( kwargs.kwargs.count( "part" ) != 0 ) {
        diag_value const part_val = kwargs.kwarg_or( "part" );
        const vpart_id part( part_val.str( d ) );
        if( !part.is_valid() ) {
            throw math::runtime_error( R"(Unknown vehicle part "%s" for %s())", part.str(), func );
        }
        query.part = part;
    }
    return query;
}

double vehicle_part_count_eval( const_dialogue const &d, char scope,
                                std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    vehicle const *const veh = vehicle_talker( d, scope, "vehicle_part_count" );
    const vehicle_part_query query = vehicle_part_query_params( d, "vehicle_part_count", params,
                                     kwargs );
    int count = 0;
    for( int idx = 0; idx < veh->part_count(); idx++ ) {
        const vehicle_part &vp = veh->part( idx );
        count += query.matches( vp ) && !vp.is_broken() ? 1 : 0;
    }
    return count;
}

double vehicle_part_broken_count_eval( const_dialogue const &d, char scope,
                                       std::vector<diag_value> const &params,
                                       diag_kwargs const &kwargs )
{
    vehicle const *const veh = vehicle_talker( d, scope, "vehicle_part_broken_count" );
    const vehicle_part_query query = vehicle_part_query_params( d, "vehicle_part_broken_count",
                                     params, kwargs );
    int broken = 0;
    for( int idx = 0; idx < veh->part_count(); idx++ ) {
        const vehicle_part &vp = veh->part( idx );
        broken += query.matches( vp ) && vp.is_broken() ? 1 : 0;
    }
    return broken;
}

double vehicle_part_hp_eval( const_dialogue const &d, char scope,
                             std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    vehicle const *const veh = vehicle_talker( d, scope, "vehicle_part_hp" );
    const vehicle_part_query query = vehicle_part_query_params( d, "vehicle_part_hp", params,
                                     kwargs );
    diag_value const agg_val = kwargs.kwarg_or( "aggregate", "min" );
    std::string const &agg = agg_val.str( d );
    if( agg != "min" && agg != "max" && agg != "average" ) {
        throw math::runtime_error( R"(Unknown aggregate "%s" for vehicle_part_hp())", agg );
    }
    // VINT-J01: "hp" gives the hit points themselves rather than the share of the durability
    diag_value const format_val = kwargs.kwarg_or( "format", "percent" );
    std::string const &format = format_val.str( d );
    if( format != "percent" && format != "hp" ) {
        throw math::runtime_error( R"(Unknown format "%s" for vehicle_part_hp())", format );
    }
    std::vector<double> health;
    for( int idx = 0; idx < veh->part_count(); idx++ ) {
        const vehicle_part &vp = veh->part( idx );
        if( query.matches( vp ) ) {
            health.push_back( format == "hp" ? vp.hp() : vp.health_percent() * 100.0 );
        }
    }
    if( health.empty() ) {
        return 0;
    }
    if( agg == "max" ) {
        return *std::max_element( health.begin(), health.end() );
    }
    if( agg == "average" ) {
        return std::accumulate( health.begin(), health.end(), 0.0 ) / health.size();
    }
    return *std::min_element( health.begin(), health.end() );
}

double vitamin_eval( const_dialogue const &d, char scope, std::vector<diag_value> const &params,
                     diag_kwargs const & /* kwargs */ )
{
    std::string const &id_str = params[0].str( d );
    const_talker const *const actor = d.const_actor( is_beta( scope ) );
    if( Character const *const chr = actor->get_const_character(); chr != nullptr ) {
        return chr->vitamin_get( vitamin_id( id_str ) );
    }
    if( item_location const *const itm = actor->get_const_item(); itm != nullptr && *itm ) {
        const std::map<vitamin_id, int> &vitamin_data =
            default_character_compute_effective_nutrients( *itm->get_item() ).vitamins();
        const auto &v = vitamin_data.find( vitamin_id( id_str ) );
        return v != vitamin_data.end() ? v->second : 0;
    }
    throw math::runtime_error( "Tried to access vitamins of a non-Character/non-item talker" );
}

void vitamin_ass( double val, dialogue &d, char scope, std::vector<diag_value> const &params,
                  diag_kwargs const & /* kwargs */ )
{
    if( d.actor( is_beta( scope ) )->get_character() ) {
        d.actor( is_beta( scope ) )
        ->get_character()
        ->vitamin_set( vitamin_id( params[0].str( d ) ), val );
    }
}

double warmth_eval( const_dialogue const &d, char scope, std::vector<diag_value> const &params,
                    diag_kwargs const & /* kwargs */ )
{
    bodypart_id bp( params[0].str( d ) );
    return units::to_legacy_bodypart_temp(
               d.const_actor( is_beta( scope ) )->get_cur_part_temp( bp ) );
}

// CHAR-J10: per-part body state.  A talker that isn't a character, or lacks the part, reads 0
// and ignores writes.
Character const *_body_part_owner( const_dialogue const &d, char scope,
                                   std::vector<diag_value> const &params, bodypart_id &bp )
{
    Character const *const chr = d.const_actor( is_beta( scope ) )->get_const_character();
    if( chr == nullptr ) {
        return nullptr;
    }
    bp = bodypart_id( params[0].str( d ) );
    return chr->has_part( bp ) ? chr : nullptr;
}

Character *_body_part_owner( dialogue &d, char scope, std::vector<diag_value> const &params,
                             bodypart_id &bp )
{
    Character *const chr = d.actor( is_beta( scope ) )->get_character();
    if( chr == nullptr ) {
        return nullptr;
    }
    bp = bodypart_id( params[0].str( d ) );
    return chr->has_part( bp ) ? chr : nullptr;
}

// MATH-J05: the character's own body part a function names, or nullopt for another talker or
// a part it doesn't have
std::optional<bodypart_id> _character_part( const Character *guy, std::string const &bp_str,
        std::string_view func )
{
    if( !bodypart_str_id( bp_str ).is_valid() ) {
        throw math::runtime_error( R"(%s(): unknown body part "%s")", func, bp_str );
    }
    const bodypart_id bp( bp_str );
    if( guy == nullptr || !guy->has_part( bp ) ) {
        return std::nullopt;
    }
    return bp;
}

// Sets the part's current temperature, which then moves toward what clothing and weather make it
void warmth_ass( double val, dialogue &d, char scope, std::vector<diag_value> const &params,
                 diag_kwargs const & /* kwargs */ )
{
    Character *guy = d.actor( is_beta( scope ) )->get_character();
    if( std::optional<bodypart_id> bp = _character_part( guy, params[0].str( d ), "warmth" ) ) {
        guy->set_part_temp_cur( *bp, units::from_legacy_bodypart_temp( static_cast<int>(
                                    std::lround( val ) ) ) );
    }
}

// wetness('torso'): how wet the part is, in percent of what it can hold (u_add_wet's unit)
double wetness_eval( const_dialogue const &d, char scope, std::vector<diag_value> const &params,
                     diag_kwargs const & /* kwargs */ )
{
    const Character *guy = d.const_actor( is_beta( scope ) )->get_const_character();
    std::optional<bodypart_id> bp = _character_part( guy, params[0].str( d ), "wetness" );
    if( !bp ) {
        return 0;
    }
    return 100.0 * guy->get_part_wetness_percentage( *bp );
}

void wetness_ass( double val, dialogue &d, char scope, std::vector<diag_value> const &params,
                  diag_kwargs const & /* kwargs */ )
{
    Character *guy = d.actor( is_beta( scope ) )->get_character();
    std::optional<bodypart_id> bp = _character_part( guy, params[0].str( d ), "wetness" );
    if( !bp ) {
        return;
    }
    const int capacity = guy->get_part_drench_capacity( *bp );
    guy->set_part_wetness( *bp, clamp( static_cast<int>( std::lround( val * capacity / 100.0 ) ), 0,
                                       capacity ) );
}

// body_temp('bp'): the part's current temperature in Celsius; a write is a momentary change,
// the part then drifts back towards what its surroundings make it
double body_temp_eval( const_dialogue const &d, char scope, std::vector<diag_value> const &params,
                       diag_kwargs const & /* kwargs */ )
{
    bodypart_id bp;
    Character const *const chr = _body_part_owner( d, scope, params, bp );
    return chr == nullptr ? 0.0 : units::to_celsius( chr->get_part_temp_cur( bp ) );
}

void body_temp_ass( double val, dialogue &d, char scope, std::vector<diag_value> const &params,
                    diag_kwargs const & /* kwargs */ )
{
    bodypart_id bp;
    Character *const chr = _body_part_owner( d, scope, params, bp );
    if( chr != nullptr ) {
        chr->set_part_temp_cur( bp, units::from_celsius( val ) );
    }
}

// frostbite('bp'): the part's frostbite timer; frostnip and frostbite come from it
double frostbite_eval( const_dialogue const &d, char scope, std::vector<diag_value> const &params,
                       diag_kwargs const & /* kwargs */ )
{
    bodypart_id bp;
    Character const *const chr = _body_part_owner( d, scope, params, bp );
    return chr == nullptr ? 0.0 : chr->get_part_frostbite_timer( bp );
}

void frostbite_ass( double val, dialogue &d, char scope, std::vector<diag_value> const &params,
                    diag_kwargs const & /* kwargs */ )
{
    bodypart_id bp;
    Character *const chr = _body_part_owner( d, scope, params, bp );
    if( chr != nullptr ) {
        chr->set_part_frostbite_timer( bp, std::max( 0, static_cast<int>( val ) ) );
    }
}

// CHAR-J09: the cardiovascular readings, updated once a minute
double heart_rate_eval( const_dialogue const &d, char scope,
                        std::vector<diag_value> const & /* params */, diag_kwargs const & /* kwargs */ )
{
    Character const *const chr = d.const_actor( is_beta( scope ) )->get_const_character();
    return chr == nullptr ? 0.0 : chr->heartrate_bpm_average();
}

double blood_pressure_eval( const_dialogue const &d, char scope,
                            std::vector<diag_value> const & /* params */, diag_kwargs const & /* kwargs */ )
{
    Character const *const chr = d.const_actor( is_beta( scope ) )->get_const_character();
    return chr == nullptr ? 0.0 : chr->get_circulation();
}

double respiration_rate_eval( const_dialogue const &d, char scope,
                              std::vector<diag_value> const & /* params */, diag_kwargs const & /* kwargs */ )
{
    Character const *const chr = d.const_actor( is_beta( scope ) )->get_const_character();
    return chr == nullptr ? 0.0 : chr->get_respiration_rate();
}

// The OMT weather('snow_depth') is about: the 'location' kwarg (a location, or "u" / "npc" as in
// omt_distance()), else the avatar's. Other aspects are the same everywhere and take no 'location'.
tripoint_abs_omt _weather_omt( const_dialogue const &d, std::string const &aspect,
                               diag_kwargs const &kwargs )
{
    diag_value const loc = kwargs.kwarg_or( "location" );
    if( aspect != "snow_depth" ) {
        if( !loc.is_empty() ) {
            throw math::runtime_error( "weather('%s') can't take a 'location'", aspect );
        }
        return tripoint_abs_omt::invalid;
    }
    return loc.is_empty() ? get_player_character().pos_abs_omt() : _omt_of( d, loc );
}

double weather_eval( const_dialogue const &d, char /* scope */,
                     std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    weather_manager const &weather = get_weather_const();
    std::string const &aspect = params[0].str( d );
    tripoint_abs_omt const omt = _weather_omt( d, aspect, kwargs );
    if( aspect == "snow_depth" ) {
        return weather.get_snow_depth_mm( omt );
    }
    if( aspect == "snow_rate" ) {
        return weather.snow_rate_mm_per_hour();
    }
    if( aspect == "temperature" ) {
        return units::to_kelvin( weather.weather_precise->temperature );
    }
    if( aspect == "temperature_offset" ) {
        return units::to_kelvin_delta( weather.eoc_temperature_offset.value.value_or( 0_K_delta ) );
    }
    if( aspect == "windpower" ) {
        return weather.weather_precise->windpower;
    }
    if( aspect == "wind_direction" ) {
        return weather.weather_precise->winddirection;
    }
    if( aspect == "humidity" ) {
        return weather.weather_precise->humidity;
    }
    if( aspect == "pressure" ) {
        return weather.weather_precise->pressure;
    }
    if( aspect == "precipitation" ) {
        return weather.weather_id->get_precip_rate();
    }
    if( aspect == "duration_left" ) {
        const double left = std::max( 0.0,
                                      to_turns<double>( weather.nextweather - calendar::turn ) );
        return _time_in_unit( left, kwargs.kwarg_or( "unit" ).str( d ) );
    }
    throw math::syntax_error( "Unknown weather aspect %s", aspect );
}

// When a durable override set by weather() should end: nullopt without a 'duration',
// calendar::turn_max for 'PERMANENT'. A number is in turns (seconds).
std::optional<time_point> _weather_override_end( const_dialogue const &d,
        diag_kwargs const &kwargs )
{
    diag_value const duration = kwargs.kwarg_or( "duration" );
    if( duration.is_empty() ) {
        return std::nullopt;
    }
    double turns;
    if( duration.is_dbl() ) {
        turns = duration.dbl();
    } else {
        std::string const &dur_str = duration.str( d );
        if( dur_str == "PERMANENT" ) {
            return calendar::turn_max;
        }
        turns = to_turns<double>( _read_from_string<time_duration>( dur_str,
                                  time_duration::units ) );
    }
    if( turns >= to_turns<double>( calendar::turn_max - calendar::turn ) ) {
        return calendar::turn_max;
    }
    return calendar::turn + time_duration::from_turns<double>( std::max( turns, 0.0 ) );
}

// Keeps value until end (a 'duration' of 0 or less removes the override); no end: no change
template<typename T>
void _hold_weather_override( timed_weather_override<T> &override,
                             std::optional<time_point> const &end, T const &value )
{
    if( !end ) {
        return;
    }
    if( *end <= calendar::turn ) {
        override.reset();
    } else {
        override.set( value, *end );
    }
}

// Without a 'duration' kwarg the value applies to the current weather only, as before, and
// is regenerated at the next weather change. With one it is also held across weather changes.
void weather_ass( double val, dialogue &d, char /* scope */, std::vector<diag_value> const &params,
                  diag_kwargs const &kwargs )
{
    weather_manager &weather = get_weather();
    std::string const &aspect = params[0].str( d );
    std::optional<time_point> const end = _weather_override_end( d, kwargs );
    tripoint_abs_omt const omt = _weather_omt( d, aspect, kwargs );
    if( aspect == "snow_depth" ) {
        // Snow on the ground, not weather: it stays and melts or grows from there
        if( end ) {
            throw math::runtime_error( "weather('snow_depth') can't take a 'duration'" );
        }
        weather.set_snow_depth_mm( omt, val );
        return;
    }
    if( aspect == "temperature" ) {
        units::temperature const temp = units::from_kelvin( val );
        weather.weather_precise->temperature = temp;
        weather.temperature = temp;
        _hold_weather_override( weather.eoc_temperature, end, temp );
    } else if( aspect == "temperature_offset" ) {
        // Not part of the generated weather, so it stays until it runs out or is cleared
        units::temperature_delta const offset = units::from_kelvin_delta( val );
        const std::optional<time_point> offset_end = end.value_or( calendar::turn_max );
        // One that ends at once is removed (see _hold_weather_override): the temperature
        // loses the old offset and gains none.
        units::temperature_delta const held = *offset_end <= calendar::turn ? 0_K_delta : offset;
        units::temperature_delta const change =
            held - weather.eoc_temperature_offset.value.value_or( 0_K_delta );
        if( !weather.eoc_temperature.value ) {
            weather.weather_precise->temperature += change;
            weather.temperature += change;
        }
        _hold_weather_override( weather.eoc_temperature_offset, offset_end, offset );
    } else if( aspect == "windpower" ) {
        weather.weather_precise->windpower = val;
        if( !weather.windspeed_override ) {
            weather.windspeed = static_cast<int>( std::lround( val ) );
        }
        _hold_weather_override( weather.eoc_windspeed, end, val );
    } else if( aspect == "wind_direction" ) {
        int const direction = ( static_cast<int>( std::lround( val ) ) % 360 + 360 ) % 360;
        weather.weather_precise->winddirection = direction;
        if( !weather.wind_direction_override ) {
            weather.winddirection = direction;
        }
        _hold_weather_override( weather.eoc_wind_direction, end, direction );
    } else if( aspect == "humidity" || aspect == "pressure" ) {
        if( end ) {
            throw math::runtime_error( "weather('%s') can't take a 'duration'", aspect );
        }
        if( aspect == "humidity" ) {
            weather.weather_precise->humidity = val;
        } else {
            weather.weather_precise->pressure = val;
        }
    } else {
        throw math::syntax_error( "Unknown or read-only weather aspect %s", aspect );
    }

    weather.clear_temp_cache();
}

// Turns until a monster's special attack is ready (0: ready); -1 if the talker isn't a monster
// or the monster doesn't have that special
double special_cooldown_eval( const_dialogue const &d, char scope,
                              std::vector<diag_value> const &params,
                              diag_kwargs const & /* kwargs */ )
{
    monster const *mon = d.const_actor( is_beta( scope ) )->get_const_monster();
    if( mon == nullptr ) {
        return -1;
    }
    return mon->special_cooldown( params[0].str( d ) ).value_or( -1 );
}

void special_cooldown_ass( double val, dialogue &d, char scope,
                           std::vector<diag_value> const &params, diag_kwargs const & /* kwargs */ )
{
    monster *mon = d.actor( is_beta( scope ) )->get_monster();
    std::string const &special = params[0].str( d );
    if( mon == nullptr || !mon->special_cooldown( special ) ) {
        return;
    }
    mon->set_special( special, std::max( 0, static_cast<int>( std::lround( val ) ) ) );
}

double climate_control_str_heat_eval( const_dialogue const &d, char scope,
                                      std::vector<diag_value> const & /* params */,
                                      diag_kwargs const & /* kwargs */ )
{
    return d.const_actor( is_beta( scope ) )->climate_control_str_heat();
}

double climate_control_str_chill_eval( const_dialogue const &d, char scope,
                                       std::vector<diag_value> const & /* params */,
                                       diag_kwargs const & /* kwargs */ )
{
    return d.const_actor( is_beta( scope ) )->climate_control_str_chill();
}

// { "name", { "scopes", num_args, eval function, assign function, { allowed kwargs, ... } } }
// kwargs are not included in num_args
std::map<std::string_view, dialogue_func> const dialogue_funcs{
    { "_test_diag_", { "g", -1, test_diag, {}, { "1", "2", "3", "blorg" } } },
    { "_test_str_len_", { "g", -1, test_str_len, {}, { "1", "2", "3" } } },
    { "addiction_intensity", { "un", 1, addiction_intensity_eval, addiction_intensity_ass } },
    { "addiction_turns", { "un", 1, addiction_turns_eval, addiction_turns_ass } },
    { "activity_moves_left", { "un", 0, activity_moves_left_eval, activity_moves_left_ass } },
    { "armor", { "un", 2, armor_eval, {}, { "source", "roll" } } },
    { "arm_strength", { "un", 0, arm_strength_eval } },
    { "ammo_count", { "un", 0, ammo_count_eval } },
    { "artifact_resonance", { "un", 0, artifact_resonance_eval } },
    { "attack_speed", { "un", 0, attack_speed_eval } },
    { "speed", { "un", 0, move_speed_eval } },
    { "characters_nearby", { "ung", 0, characters_nearby_eval, {}, { "radius", "attitude", "location", "allow_hallucinations" } } },
    { "charge_count", { "un", 1, charge_count_eval } },
    { "consumption_count", { "un", 1, consumption_count_eval, {}, { "hours" } } },
    { "consumption_count_total", { "un", 0, consumption_count_total_eval, {}, { "hours" } } },
    { "food_fun", { "un", 0, food_fun_eval } },
    { "coverage", { "un", 1, coverage_eval } },
    { "daily_health", { "un", 0, daily_health_eval, daily_health_ass, { "cap" } } },
    { "damage_level", { "un", 0, damage_level_eval } },
    { "degradation", { "un", 0, degradation_eval, degradation_ass } },
    { "distance", { "g", 2, distance_eval } },
    { "light_level", { "g", 1, light_level_eval } },
    { "noise_level", { "un", 0, noise_level_eval } },
    { "scent", { "g", 1, scent_eval, scent_ass } },
    { "effect_intensity", { "un", 1, effect_intensity_eval, effect_intensity_ass, { "bodypart" } } },
    { "effect_duration", { "un", 1, effect_duration_eval, effect_duration_ass, { "bodypart", "unit" } } },
    { "limb_score", { "un", 1, limb_score_eval, {}, { "type" } } },
    { "health", { "un", 0, health_eval, health_ass } },
    { "hunger", { "un", 0, hunger_eval, hunger_ass } },
    { "encumbrance", { "un", 1, encumbrance_eval } },
    { "enchantment_value", { "un", 1, enchantment_value_eval, {}, { "type" } } },
    { "energy", { "g", 1, energy_eval } },
    { "event_statistic", { "g", 1, event_statistic_eval } },
    { "achievement_state", { "g", 1, achievement_state_eval } },
    { "past_achievement", { "g", 1, past_achievement_eval } },
    { "faction_like", { "g", 1, faction_like_eval, faction_like_ass } },
    { "faction_respect", { "g", 1, faction_respect_eval, faction_respect_ass } },
    { "faction_trust", { "g", 1, faction_trust_eval, faction_trust_ass } },
    { "faction_food_supply", { "g", 1, faction_food_supply_eval, faction_food_supply_ass, { "vitamin", "expires_in" } } },
    { "camp_provides", { "ung", 1, camp_provides_eval, {}, { "location" } } },
    { "camp_expansion_count", { "ung", 0, camp_expansion_count_eval, {}, { "location" } } },
    { "camp_assigned_npcs", { "ung", 0, camp_assigned_npcs_eval, {}, { "location" } } },
    { "camp_workers", { "ung", 1, camp_workers_eval, {}, { "location" } } },
    { "faction_wealth", { "g", 1, faction_wealth_eval, faction_wealth_ass } },
    { "faction_power", { "g", 1, faction_power_eval, faction_power_ass } },
    { "faction_size", { "g", 1, faction_size_eval, faction_size_ass } },
    { "field_strength", { "ung", 1, field_strength_eval, field_strength_ass, { "location" } } },
    { "field_age", { "ung", 1, field_age_eval, field_age_ass, { "location" } } },
    { "map_radiation", { "ung", 0, map_radiation_eval, map_radiation_ass, { "location" } } },
    { "gender", { "un", 0, gender_eval, gender_ass } },
    { "gun_damage", { "un", 1, gun_damage_eval } },
    { "gun_heat", { "un", 0, gun_heat_eval, gun_heat_ass } },
    { "ammo_remaining", { "un", 0, ammo_remaining_eval, ammo_remaining_ass } },
    { "ammo_capacity", { "un", 1, ammo_capacity_eval } },
    { "has_gunmod", { "un", 1, has_gunmod_eval } },
    { "game_option", { "g", 1, option_eval } },
    { "has_flag", { "un", 1, has_flag_eval } },
    { "has_trait", { "un", 1, has_trait_eval } },
    { "sum_traits_of_category", { "un", 1, sum_traits_of_category_eval, {}, { "type" } } },
    { "sum_traits_of_category_char_has", { "un", 1, sum_traits_of_category_char_has_eval, {}, { "type" } } },
    { "has_active_trait", { "un", 1, has_active_trait_eval } },
    { "can_craft", { "un", 1, can_craft_eval, {}, { "batch" } } },
    { "bionic_count", { "un", 1, bionic_count_eval } },
    { "has_active_bionic", { "un", 1, has_active_bionic_eval } },
    { "bionic_charge", { "un", 1, bionic_charge_eval, bionic_charge_ass } },
    { "bionic_incapacitated", { "un", 1, bionic_incapacitated_eval, bionic_incapacitated_ass } },
    { "bionic_safe_fuel", { "un", 1, bionic_safe_fuel_eval, bionic_safe_fuel_ass } },
    { "bionic_cost", { "g", 1, bionic_cost_eval, {}, { "type" } } },
    { "bionic_power", { "un", 0, bionic_power_eval, bionic_power_ass } },
    { "bionic_power_max", { "un", 0, bionic_power_max_eval } },
    { "mutation_category_level", { "un", 1, mutation_category_level_eval } },
    { "mutation_instability", { "un", 1, mutation_instability_eval } },
    { "trait_cooldown", { "un", 1, trait_cooldown_eval, trait_cooldown_ass } },
    { "has_proficiency", { "un", 1, knows_proficiency_eval } },
    { "has_wielded_with_flag", { "un", 1, has_wielded_with_flag_eval } },
    { "has_var", { "g", 1, has_var_eval } },
    { "hp", { "un", 1, hp_eval, hp_ass } },
    { "hp_max", { "un", 1, hp_max_eval } },
    { "is_hallucination", { "un", 0, is_hallucination_eval } },
    { "item_active", { "un", 0, item_active_eval, item_active_ass } },
    { "item_ammo", { "un", 1, item_ammo_eval, item_ammo_ass } },
    { "item_count", { "un", 1, item_count_eval } },
    { "item_counter", { "un", 0, item_counter_eval, item_counter_ass } },
    { "item_temp", { "un", 0, item_temp_eval, item_temp_ass } },
    { "item_rad", { "un", 1, item_rad_eval, {}, { "aggregate" } } },
    { "item_charges", { "un", 0, item_charges_eval, item_charges_ass } },
    { "item_countdown", { "un", 0, item_countdown_eval, item_countdown_ass } },
    { "item_freshness", { "un", 0, item_freshness_eval, item_freshness_ass } },
    { "item_relic_charges", { "un", 0, item_relic_charges_eval, item_relic_charges_ass } },
    { "item_temperature", { "un", 0, item_temperature_eval, item_temperature_ass } },
    { "item_wetness", { "un", 0, item_wetness_eval, item_wetness_ass } },
    { "melee_damage", { "un", 1, melee_damage_eval } },
    // CMBT-J10: combat state
    { "melee_to_hit", { "un", 0, melee_to_hit_eval } },
    { "dodge", { "un", 0, dodge_eval } },
    { "crit_chance", { "un", 0, crit_chance_eval, {}, { "hit", "dodge" } } },
    { "blocks_left", { "un", 0, blocks_left_eval, blocks_left_ass } },
    { "dodges_left", { "un", 0, dodges_left_eval, dodges_left_ass } },
    { "recoil", { "un", 0, recoil_eval, recoil_ass } },
    { "weapon_dispersion", { "un", 0, weapon_dispersion_eval } },
    { "mod_load_order", { "g", 1, mod_order_eval } },
    { "mod_setting", { "g", 1, mod_setting_eval, mod_setting_ass, {}, mod_setting_check, mod_setting_assign_check } },
    { "monsters_nearby", { "ung", -1, monsters_nearby_eval, {}, { "radius", "attitude", "location", "allow_hallucinations" } } },
    { "mon_species_nearby", { "ung", -1, monster_species_nearby_eval, {}, { "radius", "attitude", "location", "allow_hallucinations" } } },
    { "mon_groups_nearby", { "ung", -1, monster_groups_nearby_eval, {}, { "radius", "attitude", "location", "allow_hallucinations" } } },
    // OMAP-J01: overmap functions
    { "omt_distance", { "g", 2, omt_distance_eval } },
    { "horde_size", { "g", 1, horde_size_eval, {}, { "radius" } } },
    { "city_distance", { "g", 1, city_distance_eval, {}, { "known", "min_size", "max_size", "max_distance", "name" } } },
    { "city_size", { "g", 1, city_size_eval, {}, { "known", "min_size", "max_size", "max_distance", "name" } } },
    { "moon_phase", { "g", 0, moon_phase_eval } },
    { "morale", { "un", 0, morale_eval, morale_ass, { "raw" } } },
    { "morale_value", { "un", 1, morale_value_eval } },
    { "stomach", { "un", 1, stomach_eval, {}, { "organ" } } },
    { "wound_healing", { "un", 1, wound_healing_eval, wound_healing_ass, { "bodypart" } } },
    { "wound_pain", { "un", 1, wound_pain_eval, {}, { "bodypart" } } },
    { "num_input", { "g", 2, num_input_eval } },
    { "oracle", { "g", 1, oracle_eval, {}, {}, oracle_check } },
    { "oxygen", { "un", 0, oxygen_eval, oxygen_ass } },
    { "oxygen_max", { "un", 0, oxygen_max_eval } },
    { "pain", { "un", 0, pain_eval, pain_ass, { "type" } } },
    { "price", { "un", 0, price_eval } },
    { "price_postapoc", { "un", 0, price_postapoc_eval } },
    { "school_level", { "un", 1, school_level_eval } },
    { "school_level_adjustment", { "un", 1, school_level_adjustment_eval, school_level_adjustment_ass } },
    { "spellcasting_adjustment", { "u", 1, {}, spellcasting_adjustment_ass, { "mod", "school", "spell", "flag_whitelist", "flag_blacklist" } } },
    { "get_calories_daily", { "g", 0, get_daily_calories, {}, { "type", "day" } } },
    { "quality", { "un", 1, quality_eval, {}, { "strict" } } },
    { "skill", { "un", 1, skill_eval, skill_ass } },
    { "skill_exp", { "un", 1, skill_exp_eval, skill_exp_ass, { "format", "practice" } } },
    { "skill_knowledge", { "un", 1, skill_knowledge_eval, skill_knowledge_ass } },
    { "skill_knowledge_exp", { "un", 1, skill_knowledge_exp_eval, skill_knowledge_exp_ass, { "format" } } },
    { "skill_rust", { "un", 1, skill_rust_eval, {}, { "format" } } },
    { "skill_rusty", { "un", 1, skill_rusty_eval } },
    { "skill_time_since_practice", { "un", 1, skill_time_since_practice_eval, skill_time_since_practice_ass, { "unit" } } },
    { "ability_query", { "un", 1, power_math::ability, {}, { "field" } } },
    { "power_summons", { "un", 1, power_math::summon } },
    { "power_projection", { "un", 1, power_math::projection, {}, { "field" } } },
    { "power_area", { "un", 1, power_math::area, {}, { "field", "owner" } } },
    { "power_resource", { "un", 1, power_math::resource, {}, { "field" } } },
    { "power_cooldown", { "un", 1, power_math::cooldown, {}, { "field" } } },
    { "power_application", { "un", 1, power_math::application, {}, { "field", "owner", "tag" } } },
    { "special_cooldown", { "un", 1, special_cooldown_eval, special_cooldown_ass } },
    { "spell_count", { "un", 0, spell_count_eval, {}, { "school" } } },
    { "spell_difficulty", { "un", 1, spell_difficulty_eval, {}, { "baseline" } } },
    { "spell_exp", { "un", 1, spell_exp_eval, spell_exp_ass } },
    { "spell_exp_to_next_level", { "un", 1, spell_exp_to_next_level_eval } },
    { "spell_fail", { "un", 1, spell_fail_eval, {}, { "baseline" } } },
    { "spell_casting_exp", { "un", 1, spell_casting_exp_eval, {}, { "baseline" } } },
    { "spell_sound", { "un", 1, spell_sound_eval, {}, { "baseline" } } },
    { "spell_energy_cost", { "un", 1, spell_energy_cost_eval, {}, { "baseline" } } },
    { "spell_cast_time", { "un", 1, spell_cast_time_eval, {}, { "baseline" } } },
    { "spell_damage", { "un", 1, spell_damage_eval, {}, { "baseline" } } },
    { "spell_range", { "un", 1, spell_range_eval, {}, { "baseline" } } },
    { "spell_aoe", { "un", 1, spell_aoe_eval, {}, { "baseline" } } },
    { "spell_duration", { "un", 1, spell_duration_eval, {}, { "baseline" } } },
    { "spell_max_level", { "un", 1, spell_max_level_eval, {}, { "baseline" } } },
    { "spell_exp_for_level", { "g", 2, spell_exp_for_level_eval } },
    { "spell_level", { "un", 1, spell_level_eval, spell_level_ass } },
    { "spell_level_adjustment", { "un", 1, spell_level_adjustment_eval, spell_level_adjustment_ass } },
    { "spell_level_sum", { "un", 0, spell_sum_eval, {}, { "school", "level" } } },
    { "time", { "g", 1, time_eval, time_ass, { "unit" } } },
    { "time_since", { "g", 1, time_since_eval, {}, { "unit" } } },
    { "time_until", { "g", 1, time_until_eval, {}, { "unit" } } },
    { "time_until_eoc", { "ung", 1, time_until_eoc_eval, {}, { "unit" } } },
    { "trauma", { "un", 0, trauma_eval, trauma_ass } },
    { "proficiency", { "un", 1, proficiency_eval, proficiency_ass, { "format", "direct" } } },
    { "val", { "un", 1, u_val, u_val_ass } },
    { "npc_anger", { "un", 0, npc_anger_eval, npc_anger_ass } },
    { "npc_fear", { "un", 0, npc_fear_eval, npc_fear_ass } },
    { "npc_danger", { "ung", 0, npc_danger_eval } },
    { "npc_goal_turns", { "ung", 0, npc_goal_turns_eval } },
    { "npc_need_urgency", { "ung", 1, npc_need_urgency_eval, {}, {}, npc_need_urgency_check } },
    { "npc_panic", { "ung", 0, npc_panic_eval } },
    { "npc_personality", { "ung", 1, npc_personality_eval, npc_personality_ass, {}, npc_personality_check } },
    { "npc_value", { "un", 0, npc_value_eval, npc_value_ass } },
    { "npc_trust", { "un", 0, npc_trust_eval, npc_trust_ass } },
    { "ugliness", { "un", 0, ugliness_eval } },
    { "value_or", { "g", 2, value_or_eval } },
    { "vision_range", { "un", 0, vision_range_eval } },
    { "vitamin", { "un", 1, vitamin_eval, vitamin_ass } },
    { "calories", { "un", 0, calories_eval, calories_ass, { "format", "dont_affect_weariness" } } },
    { "weight", { "un", 0, weight_eval } },
    { "weight_carried", { "un", 0, weight_carried_eval } },
    { "weight_capacity", { "un", 0, weight_capacity_eval } },
    { "volume", { "un", 0, volume_eval } },
    { "volume_carried", { "un", 0, volume_carried_eval } },
    { "volume_capacity", { "un", 0, volume_capacity_eval } },
    { "free_volume", { "un", 0, free_volume_eval } },
    { "contents_count", { "un", 0, contents_count_eval } },
    { "contents_volume", { "un", 0, contents_volume_eval } },
    { "contents_weight", { "un", 0, contents_weight_eval } },
    { "remaining_volume", { "un", 0, remaining_volume_eval } },
    { "remaining_weight", { "un", 0, remaining_weight_eval } },
    { "rot", { "un", 0, rot_eval, rot_ass, { "format" } } },
    { "warmth", { "un", 1, warmth_eval, warmth_ass } },
    { "wetness", { "un", 1, wetness_eval, wetness_ass } },
    { "body_temp", { "un", 1, body_temp_eval, body_temp_ass } },
    { "frostbite", { "un", 1, frostbite_eval, frostbite_ass } },
    { "heart_rate", { "un", 0, heart_rate_eval } },
    { "blood_pressure", { "un", 0, blood_pressure_eval } },
    { "respiration_rate", { "un", 0, respiration_rate_eval } },
    { "weather", { "g", 1, weather_eval, weather_ass, { "duration", "unit", "location" } } },
    { "climate_control_str_heat", { "un", 0, climate_control_str_heat_eval } },
    { "climate_control_str_chill", { "un", 0, climate_control_str_chill_eval } },
    { "vehicle_battery", { "un", 0, vehicle_battery_eval, vehicle_battery_ass } },
    { "vehicle_battery_max", { "un", 0, vehicle_battery_max_eval } },
    { "vehicle_cruise", { "un", 0, vehicle_cruise_eval, vehicle_cruise_ass } },
    { "vehicle_engine_on", { "un", 0, vehicle_engine_on_eval, vehicle_engine_on_ass } },
    { "vehicle_fuel", { "un", 1, vehicle_fuel_eval, vehicle_fuel_ass } },
    { "vehicle_fuel_capacity", { "un", 1, vehicle_fuel_capacity_eval } },
    { "vehicle_locked", { "un", 0, vehicle_locked_eval, vehicle_locked_ass } },
    { "vehicle_max_speed", { "un", 0, vehicle_max_speed_eval } },
    { "vehicle_part_broken_count", { "un", -1, vehicle_part_broken_count_eval, {}, { "part" } } },
    { "vehicle_part_count", { "un", -1, vehicle_part_count_eval, {}, { "part" } } },
    { "vehicle_part_hp", { "un", -1, vehicle_part_hp_eval, {}, { "aggregate", "format", "part" } } },
    { "vehicle_power", { "un", 0, vehicle_power_eval } },
    { "vehicle_velocity", { "un", 0, vehicle_velocity_eval, vehicle_velocity_ass } },
};

// MATH-J04: the ids dialogue functions take. A literal one is checked once all data has loaded;
// an id in a variable can only be checked when the function runs.
template<typename Id>
bool is_known_id( std::string const &id )
{
    return id.empty() || Id( id ).is_valid();
}

bool is_known_bodypart_or_all( std::string const &id )
{
    return id == "ALL" || id == "ALL_MAJOR" || id == "ALL_MINOR" ||
           is_known_id<bodypart_str_id>( id );
}

bool is_known_damage_type_or_all( std::string const &id )
{
    return id == "ALL" || is_known_id<damage_type_id>( id );
}

bool is_known_bionic_or_any( std::string const &id )
{
    return id == "ANY" || is_known_id<bionic_id>( id );
}

// the null spell is the highest level spell for spell_level()
bool is_known_spell( std::string const &id )
{
    return is_known_id<spell_id>( id ) || spell_id( id ).is_null();
}

// a magic school is a trait; spells without one are in "NONE"
bool is_known_school( std::string const &id )
{
    return id == "NONE" || is_known_id<trait_id>( id );
}

// an enchantment "values" name, such as MAX_MANA
bool is_known_enchant_value( std::string const &id )
{
    return id.empty() || io::string_to_enum_optional<enchant_vals::mod>( id ).has_value();
}

struct literal_id_kind {
    std::string_view name;
    bool ( *is_known )( std::string const & );
};

const literal_id_kind id_addiction{ "addiction", &is_known_id<addiction_id> };
const literal_id_kind id_bionic{ "bionic", &is_known_id<bionic_id> };
const literal_id_kind id_bionic_or_any{ "bionic", &is_known_bionic_or_any };
const literal_id_kind id_bodypart{ "body part", &is_known_id<bodypart_str_id> };
const literal_id_kind id_bodypart_or_all{ "body part", &is_known_bodypart_or_all };
const literal_id_kind id_damage_type{ "damage type", &is_known_id<damage_type_id> };
const literal_id_kind id_damage_type_or_all{ "damage type", &is_known_damage_type_or_all };
const literal_id_kind id_effect{ "effect", &is_known_id<efftype_id> };
const literal_id_kind id_enchant_value{ "enchantment value", &is_known_enchant_value };
const literal_id_kind id_eoc{ "effect_on_condition", &is_known_id<effect_on_condition_id> };
const literal_id_kind id_event_statistic{ "event_statistic", &is_known_id<string_id<event_statistic>> };
const literal_id_kind id_field_type{ "field type", &is_known_id<field_type_str_id> };
const literal_id_kind id_item_flag{ "item flag", &is_known_id<flag_id> };
const literal_id_kind id_itype{ "item", &is_known_id<itype_id> };
const literal_id_kind id_limb_score{ "limb score", &is_known_id<limb_score_id> };
const literal_id_kind id_mongroup{ "monster group", &is_known_id<mongroup_id> };
const literal_id_kind id_morale_type{ "morale type", &is_known_id<morale_type> };
const literal_id_kind id_mtype{ "monster", &is_known_id<mtype_id> };
const literal_id_kind id_mutation_category{ "mutation category", &is_known_id<mutation_category_id> };
const literal_id_kind id_proficiency{ "proficiency", &is_known_id<proficiency_id> };
const literal_id_kind id_quality{ "quality", &is_known_id<quality_id> };
const literal_id_kind id_recipe{ "recipe", &is_known_id<recipe_id> };
const literal_id_kind id_school{ "magic school", &is_known_school };
const literal_id_kind id_skill{ "skill", &is_known_id<skill_id> };
const literal_id_kind id_species{ "species", &is_known_id<species_id> };
const literal_id_kind id_spell{ "spell", &is_known_spell };
const literal_id_kind id_trait{ "trait", &is_known_id<trait_id> };
const literal_id_kind id_vitamin{ "vitamin", &is_known_id<vitamin_id> };
const literal_id_kind id_vpart{ "vehicle part", &is_known_id<vpart_id> };
const literal_id_kind id_wound{ "wound", &is_known_id<wound_type_id> };

// An argument that names an id: a positional one (every_arg for all of them), or a kwarg
constexpr int every_arg = -1;
constexpr int kwarg_arg = -2;
struct literal_id_arg {
    int pos;
    std::string_view kwarg;
    literal_id_kind kind;
};

literal_id_arg id_arg( int pos, literal_id_kind const &kind )
{
    return { pos, {}, kind };
}

literal_id_arg id_kwarg( std::string_view kwarg, literal_id_kind const &kind )
{
    return { kwarg_arg, kwarg, kind };
}

// Functions whose arguments can also be something other than an id (a variable name, a flag
// that needn't be defined, a faction made in play) are left out.
std::map<std::string_view, std::vector<literal_id_arg>> const literal_id_args{
    { "addiction_intensity", { id_arg( 0, id_addiction ) } },
    { "addiction_turns", { id_arg( 0, id_addiction ) } },
    { "armor", { id_arg( 0, id_damage_type ), id_arg( 1, id_bodypart ) } },
    { "bionic_charge", { id_arg( 0, id_bionic ) } },
    { "bionic_cost", { id_arg( 0, id_bionic ) } },
    { "bionic_count", { id_arg( 0, id_bionic_or_any ) } },
    { "bionic_incapacitated", { id_arg( 0, id_bionic ) } },
    { "bionic_safe_fuel", { id_arg( 0, id_bionic ) } },
    { "can_craft", { id_arg( 0, id_recipe ) } },
    { "charge_count", { id_arg( 0, id_itype ) } },
    { "consumption_count", { id_arg( 0, id_itype ) } },
    { "coverage", { id_arg( 0, id_bodypart ) } },
    { "effect_duration", { id_arg( 0, id_effect ), id_kwarg( "bodypart", id_bodypart ) } },
    { "effect_intensity", { id_arg( 0, id_effect ), id_kwarg( "bodypart", id_bodypart ) } },
    { "enchantment_value", { id_arg( 0, id_enchant_value ) } },
    { "encumbrance", { id_arg( 0, id_bodypart ) } },
    { "event_statistic", { id_arg( 0, id_event_statistic ) } },
    { "faction_food_supply", { id_kwarg( "vitamin", id_vitamin ) } },
    { "field_strength", { id_arg( 0, id_field_type ) } },
    { "gun_damage", { id_arg( 0, id_damage_type_or_all ) } },
    { "has_active_bionic", { id_arg( 0, id_bionic ) } },
    { "has_active_trait", { id_arg( 0, id_trait ) } },
    { "has_proficiency", { id_arg( 0, id_proficiency ) } },
    { "has_trait", { id_arg( 0, id_trait ) } },
    { "has_wielded_with_flag", { id_arg( 0, id_item_flag ) } },
    { "hp", { id_arg( 0, id_bodypart_or_all ) } },
    { "hp_max", { id_arg( 0, id_bodypart ) } },
    { "item_count", { id_arg( 0, id_itype ) } },
    { "item_rad", { id_arg( 0, id_item_flag ) } },
    { "limb_score", { id_arg( 0, id_limb_score ) } },
    { "melee_damage", { id_arg( 0, id_damage_type_or_all ) } },
    { "mon_groups_nearby", { id_arg( every_arg, id_mongroup ) } },
    { "mon_species_nearby", { id_arg( every_arg, id_species ) } },
    { "monsters_nearby", { id_arg( every_arg, id_mtype ) } },
    { "morale_value", { id_arg( 0, id_morale_type ) } },
    { "mutation_category_level", { id_arg( 0, id_mutation_category ) } },
    { "mutation_instability", { id_arg( 0, id_mutation_category ) } },
    { "proficiency", { id_arg( 0, id_proficiency ) } },
    { "quality", { id_arg( 0, id_quality ) } },
    { "school_level", { id_arg( 0, id_school ) } },
    { "school_level_adjustment", { id_arg( 0, id_school ) } },
    { "skill", { id_arg( 0, id_skill ) } },
    { "skill_exp", { id_arg( 0, id_skill ) } },
    { "skill_knowledge", { id_arg( 0, id_skill ) } },
    { "skill_knowledge_exp", { id_arg( 0, id_skill ) } },
    { "skill_rust", { id_arg( 0, id_skill ) } },
    { "skill_rusty", { id_arg( 0, id_skill ) } },
    { "skill_time_since_practice", { id_arg( 0, id_skill ) } },
    { "spell_aoe", { id_arg( 0, id_spell ) } },
    { "spell_cast_time", { id_arg( 0, id_spell ) } },
    { "spell_casting_exp", { id_arg( 0, id_spell ) } },
    { "spell_count", { id_kwarg( "school", id_school ) } },
    { "spell_damage", { id_arg( 0, id_spell ) } },
    { "spell_difficulty", { id_arg( 0, id_spell ) } },
    { "spell_duration", { id_arg( 0, id_spell ) } },
    { "spell_energy_cost", { id_arg( 0, id_spell ) } },
    { "spell_exp", { id_arg( 0, id_spell ) } },
    { "spell_exp_for_level", { id_arg( 0, id_spell ) } },
    { "spell_exp_to_next_level", { id_arg( 0, id_spell ) } },
    { "spell_fail", { id_arg( 0, id_spell ) } },
    { "spell_level", { id_arg( 0, id_spell ) } },
    { "spell_level_adjustment", { id_arg( 0, id_spell ) } },
    { "spell_level_sum", { id_kwarg( "school", id_school ) } },
    { "spell_max_level", { id_arg( 0, id_spell ) } },
    { "spell_range", { id_arg( 0, id_spell ) } },
    { "spell_sound", { id_arg( 0, id_spell ) } },
    { "spellcasting_adjustment", { id_kwarg( "school", id_school ), id_kwarg( "spell", id_spell ) } },
    { "sum_traits_of_category", { id_arg( 0, id_mutation_category ) } },
    { "sum_traits_of_category_char_has", { id_arg( 0, id_mutation_category ) } },
    { "time_until_eoc", { id_arg( 0, id_eoc ) } },
    { "trait_cooldown", { id_arg( 0, id_trait ) } },
    { "vehicle_fuel", { id_arg( 0, id_itype ) } },
    { "vehicle_fuel_capacity", { id_arg( 0, id_itype ) } },
    { "vehicle_part_broken_count", { id_kwarg( "part", id_vpart ) } },
    { "vehicle_part_count", { id_kwarg( "part", id_vpart ) } },
    { "vehicle_part_hp", { id_kwarg( "part", id_vpart ) } },
    { "vitamin", { id_arg( 0, id_vitamin ) } },
    { "warmth", { id_arg( 0, id_bodypart ) } },
    { "wound_healing", { id_arg( 0, id_wound ), id_kwarg( "bodypart", id_bodypart ) } },
    { "wound_pain", { id_arg( 0, id_wound ), id_kwarg( "bodypart", id_bodypart ) } },
};

void add_literal_id( std::string_view func, diag_value const &arg, literal_id_kind const &kind,
                     std::vector<diag_literal_id> &ids )
{
    if( arg.is_str() ) {
        ids.push_back( { func, kind.name, arg.str(), kind.is_known } );
    }
}

} // namespace

std::map<std::string_view, dialogue_func> const &get_all_diag_funcs()
{
    return dialogue_funcs;
}

void collect_literal_ids( std::string_view func, std::vector<diag_value> const &params,
                          diag_kwargs const &kwargs, std::vector<diag_literal_id> &ids )
{
    const auto entry = literal_id_args.find( func );
    if( entry == literal_id_args.end() ) {
        return;
    }
    // the table's key outlives the expression's text
    const std::string_view name = entry->first;
    for( const literal_id_arg &arg : entry->second ) {
        if( arg.pos == kwarg_arg ) {
            const auto kw = kwargs.kwargs.find( std::string( arg.kwarg ) );
            if( kw != kwargs.kwargs.end() ) {
                add_literal_id( name, kw->second, arg.kind, ids );
            }
        } else if( arg.pos == every_arg ) {
            for( const diag_value &param : params ) {
                add_literal_id( name, param, arg.kind, ids );
            }
        } else if( static_cast<size_t>( arg.pos ) < params.size() ) {
            add_literal_id( name, params[arg.pos], arg.kind, ids );
        }
    }
}

std::vector<std::string_view> diag_funcs_with_literal_ids()
{
    std::vector<std::string_view> names;
    names.reserve( literal_id_args.size() );
    for( const auto &entry : literal_id_args ) {
        names.push_back( entry.first );
    }
    return names;
}
