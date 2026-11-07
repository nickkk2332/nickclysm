#include "math_parser_diag.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "behavior_oracle.h"
#include "bionics.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_compiler_support.h"
#include "cata_utility.h"
#include "cata_variant.h"
#include "character.h"
#include "character_id.h"
#include "character_oracle.h"
#include "city.h"
#include "condition.h"
#include "coordinates.h"
#include "creature.h"
#include "damage.h"
#include "debug.h"
#include "dialogue.h"
#include "dialogue_helpers.h"
#include "effect.h"
#include "enum_conversions.h"
#include "enums.h"
#include "faction.h"
#include "field.h"
#include "game.h"
#include "input_popup.h"
#include "item.h"
#include "item_location.h"
#include "item_pocket.h"
#include "itype.h"
#include "magic.h"
#include "map.h"
#include "map_iterator.h"
#include "math_parser_diag_value.h"
#include "math_parser_type.h"
#include "mod_manager.h"
#include "mod_setting.h"
#include "mongroup.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "npc_json_goal.h"
#include "options.h"
#include "overmapbuffer.h"
#include "pimpl.h"
#include "player_activity.h"
#include "point.h"
#include "power_math.h"
#include "proficiency.h"
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

double addiction_intensity_eval( const_dialogue const &d, char scope,
                                 std::vector<diag_value> const &params,
                                 diag_kwargs const & /* kwargs */ )
{
    return d.const_actor( is_beta( scope ) )
           ->get_addiction_intensity( addiction_id( params[0].str( d ) ) );
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
                   diag_kwargs const & /* kwargs */ )
{
    damage_type_id dt( params[0].str( d ) );
    bodypart_id bp( params[1].str( d ) );
    return d.const_actor( is_beta( scope ) )->armor_at( dt, bp );
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

double light_level_eval( const_dialogue const &d, char /* scope */,
                         std::vector<diag_value> const &params, diag_kwargs const &/* kwargs */ )
{
    const map &here = get_map();
    tripoint_abs_ms loc_val = params[0].tripoint( d );

    return static_cast<double>( here.light_at( here.get_bub( loc_val ) ) );
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

double field_strength_eval( const_dialogue const &d, char scope,
                            std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    std::optional<var_info> loc_var;
    diag_value loc_val = kwargs.kwarg_or( "location" );

    tripoint_abs_ms loc;

    if( !loc_val.is_empty() ) {
        loc = loc_val.tripoint( d );
    } else if( scope == 'g' ) {
        throw math::syntax_error(
            R"("field_strength" needs either an actor scope (u/n) or a 'location' kwarg)" );
    } else {
        loc = d.const_actor( is_beta( scope ) )->pos_abs();
    }

    map &here = get_map();

    field_type_id ft = field_type_id( params[0].str( d ) );
    field_entry *fp = here.field_at( here.get_bub( loc ) ).find_field( ft );
    return fp ? fp->get_field_intensity() : 0;
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
};

bool _matches_attitude_filter( Creature const &critter, mon_filter filter )
{
    monster const *mon = critter.as_monster();
    switch( filter ) {
        case mon_filter::enemies:
            return mon->friendly == 0;
        case mon_filter::friends:
            return mon->friendly != 0;
        case mon_filter::both:
            return true;
    }
    return false;
}

template <class ID>
bool _filter_monster( Creature const &critter, std::vector<ID> const &ids, int radius,
                      tripoint_abs_ms const &loc, f_monster_match<ID> f, mon_filter filter )
{
    if( critter.is_monster() ) {
        bool const id_filter =
        ids.empty() || std::any_of( ids.begin(), ids.end(), [&critter, f]( ID const & id ) {
            return ( *f )( critter, id );
        } );

        return id_filter && _matches_attitude_filter( critter, filter ) &&
               radius >= rl_dist( critter.pos_abs(), loc );
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
    } else if( filter_str != "hostile" ) {
        throw math::runtime_error( R"(Unknown attitude filter "%s" for monsters_nearby())",
                                   filter_str );
    }

    std::vector<Creature *> const targets =
    g->get_creatures_if( [&mids, &radius, &loc, f, filter]( const Creature & critter ) {
        return _filter_monster( critter, mids, radius, loc, f, filter );
    } );
    return static_cast<double>( targets.size() );
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

    skill_id skill( params[0].str( d ) );
    std::string const &format = format_value.str( d );
    if( format != "raw" && format != "percentage" ) {
        throw math::runtime_error( R"(Unknown format type "%s" for skill_exp)", format );
    }
    bool raw = format == "raw";
    d.actor( is_beta( scope ) )->set_skill_exp( skill, val, raw );
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
// the others on the character they run for (u_ and n_ scopes).
double time_until_eoc_eval( const_dialogue const &d, char scope,
                            std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    diag_value unit_val = kwargs.kwarg_or( "unit" );

    effect_on_condition_id eoc_id( params[0].str( d ) );
    queued_eocs const *queue = &g->queued_global_effect_on_conditions;
    if( scope != 'g' ) {
        Character const *chr = d.const_actor( is_beta( scope ) )->get_const_character();
        if( chr == nullptr ) {
            return -1;
        }
        queue = &chr->queued_effect_on_conditions;
    }
    std::optional<time_point> soonest;
    for( queued_eoc const &eoc : queue->list ) {
        if( eoc.eoc == eoc_id && ( !soonest || eoc.time < *soonest ) ) {
            soonest = eoc.time;
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

double weather_eval( const_dialogue const &d, char /* scope */,
                     std::vector<diag_value> const &params, diag_kwargs const &kwargs )
{
    weather_manager const &weather = get_weather_const();
    std::string const &aspect = params[0].str( d );
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
        return precip_mm_per_hour( weather.weather_id->precip );
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
    { "addiction_intensity", { "un", 1, addiction_intensity_eval } },
    { "addiction_turns", { "un", 1, addiction_turns_eval, addiction_turns_ass } },
    { "activity_moves_left", { "un", 0, activity_moves_left_eval, activity_moves_left_ass } },
    { "armor", { "un", 2, armor_eval } },
    { "arm_strength", { "un", 0, arm_strength_eval } },
    { "artifact_resonance", { "un", 0, artifact_resonance_eval } },
    { "attack_speed", { "un", 0, attack_speed_eval } },
    { "speed", { "un", 0, move_speed_eval } },
    { "characters_nearby", { "ung", 0, characters_nearby_eval, {}, { "radius", "attitude", "location", "allow_hallucinations" } } },
    { "charge_count", { "un", 1, charge_count_eval } },
    { "consumption_count", { "un", 1, consumption_count_eval, {}, { "hours" } } },
    { "consumption_count_total", { "un", 0, consumption_count_total_eval, {}, { "hours" } } },
    { "coverage", { "un", 1, coverage_eval } },
    { "daily_health", { "un", 0, daily_health_eval, daily_health_ass, { "cap" } } },
    { "damage_level", { "un", 0, damage_level_eval } },
    { "degradation", { "un", 0, degradation_eval, degradation_ass } },
    { "distance", { "g", 2, distance_eval } },
    { "light_level", { "g", 1, light_level_eval } },
    { "effect_intensity", { "un", 1, effect_intensity_eval, effect_intensity_ass, { "bodypart" } } },
    { "effect_duration", { "un", 1, effect_duration_eval, effect_duration_ass, { "bodypart", "unit" } } },
    { "limb_score", { "un", 1, limb_score_eval, {}, { "type" } } },
    { "health", { "un", 0, health_eval, health_ass } },
    { "hunger", { "un", 0, hunger_eval, hunger_ass } },
    { "encumbrance", { "un", 1, encumbrance_eval } },
    { "energy", { "g", 1, energy_eval } },
    { "event_statistic", { "g", 1, event_statistic_eval } },
    { "faction_like", { "g", 1, faction_like_eval, faction_like_ass } },
    { "faction_respect", { "g", 1, faction_respect_eval, faction_respect_ass } },
    { "faction_trust", { "g", 1, faction_trust_eval, faction_trust_ass } },
    { "faction_food_supply", { "g", 1, faction_food_supply_eval, {}, { "vitamin" } } },
    { "faction_wealth", { "g", 1, faction_wealth_eval, faction_wealth_ass } },
    { "faction_power", { "g", 1, faction_power_eval, faction_power_ass } },
    { "faction_size", { "g", 1, faction_size_eval, faction_size_ass } },
    { "field_strength", { "ung", 1, field_strength_eval, {}, { "location" } } },
    { "gender", { "un", 0, gender_eval, gender_ass } },
    { "gun_damage", { "un", 1, gun_damage_eval } },
    { "game_option", { "g", 1, option_eval } },
    { "has_flag", { "un", 1, has_flag_eval } },
    { "has_trait", { "un", 1, has_trait_eval } },
    { "sum_traits_of_category", { "un", 1, sum_traits_of_category_eval, {}, { "type" } } },
    { "sum_traits_of_category_char_has", { "un", 1, sum_traits_of_category_char_has_eval, {}, { "type" } } },
    { "has_active_trait", { "un", 1, has_active_trait_eval } },
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
    { "item_count", { "un", 1, item_count_eval } },
    { "item_rad", { "un", 1, item_rad_eval, {}, { "aggregate" } } },
    { "melee_damage", { "un", 1, melee_damage_eval } },
    { "mod_load_order", { "g", 1, mod_order_eval } },
    { "mod_setting", { "g", 1, mod_setting_eval, mod_setting_ass, {}, mod_setting_check, mod_setting_assign_check } },
    { "monsters_nearby", { "ung", -1, monsters_nearby_eval, {}, { "radius", "attitude", "location" } } },
    { "mon_species_nearby", { "ung", -1, monster_species_nearby_eval, {}, { "radius", "attitude", "location" } } },
    { "mon_groups_nearby", { "ung", -1, monster_groups_nearby_eval, {}, { "radius", "attitude", "location" } } },
    // OMAP-J01: overmap functions
    { "omt_distance", { "g", 2, omt_distance_eval } },
    { "horde_size", { "g", 1, horde_size_eval, {}, { "radius" } } },
    { "city_distance", { "g", 1, city_distance_eval, {}, { "known", "min_size", "max_size", "max_distance", "name" } } },
    { "city_size", { "g", 1, city_size_eval, {}, { "known", "min_size", "max_size", "max_distance", "name" } } },
    { "moon_phase", { "g", 0, moon_phase_eval } },
    { "morale", { "un", 0, morale_eval, morale_ass, { "raw" } } },
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
    { "skill_exp", { "un", 1, skill_exp_eval, skill_exp_ass, { "format" } } },
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
    { "warmth", { "un", 1, warmth_eval } },
    { "weather", { "g", 1, weather_eval, weather_ass, { "duration", "unit" } } },
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

} // namespace

std::map<std::string_view, dialogue_func> const &get_all_diag_funcs()
{
    return dialogue_funcs;
}
