#include "weather_type.h"

#include <utility>

#include "condition.h"
#include "debug.h"
#include "flexbuffer_json.h"
#include "generic_factory.h"
#include "weather.h"

static const efftype_id effect_glare( "glare" );
static const efftype_id effect_snow_glare( "snow_glare" );

namespace
{
generic_factory<weather_type> weather_type_factory( "weather_type" );
// Weather type ids named elsewhere in JSON, with where; checked once everything is loaded
std::vector<std::pair<std::string, std::string>> pending_weather_refs;
} // namespace

namespace io
{
template<>
std::string enum_to_string<precip_class>( precip_class data )
{
    switch( data ) {
        case precip_class::none:
            return "none";
        case precip_class::very_light:
            return "very_light";
        case precip_class::light:
            return "light";
        case precip_class::heavy:
            return "heavy";
        case precip_class::last:
            break;
    }
    cata_fatal( "Invalid precip_class" );
}

template<>
std::string enum_to_string<weather_sound_category>( weather_sound_category data )
{
    switch( data ) {
        case weather_sound_category::drizzle:
            return "drizzle";
        case weather_sound_category::flurries:
            return "flurries";
        case weather_sound_category::rainy:
            return "rainy";
        case weather_sound_category::rainstorm:
            return "rainstorm";
        case weather_sound_category::snow:
            return "snow";
        case weather_sound_category::snowstorm:
            return "snowstorm";
        case weather_sound_category::thunder:
            return "thunder";
        case weather_sound_category::silent:
            return "silent";
        case weather_sound_category::portal_storm:
            return "portal_storm";
        case weather_sound_category::clear:
            return "clear";
        case weather_sound_category::sunny:
            return "sunny";
        case weather_sound_category::cloudy:
            return "cloudy";
        case weather_sound_category::last:
            break;
    }
    cata_fatal( "Invalid weather sound category." );
}

} // namespace io

template<>
const weather_type &weather_type_id::obj() const
{
    return weather_type_factory.obj( *this );
}

/** @relates string_id */
template<>
bool string_id<weather_type>::is_valid() const
{
    return weather_type_factory.is_valid( *this );
}

void weather_type::finalize()
{

}

void weather_type::check() const
{
    for( const auto &type : required_weathers ) {
        if( !type.is_valid() ) {
            debugmsg( "Weather type %s does not exist.", type.c_str() );
        }
    }
    if( glare_override ) {
        for( const weather_glare &entry : *glare_override ) {
            if( !entry.effect.is_valid() ) {
                debugmsg( "Weather type %s: glare effect %s does not exist.", id.str(), entry.effect.str() );
            }
        }
    }
}

double weather_type::get_precip_rate() const
{
    return precip_rate_override ? *precip_rate_override : precip_mm_per_hour( precip );
}

int weather_type::get_wetness() const
{
    if( wetness_override ) {
        return *wetness_override;
    }
    switch( precip ) {
        case precip_class::very_light:
            return 5;
        case precip_class::light:
            return 30;
        case precip_class::heavy:
            return 60;
        case precip_class::none:
        case precip_class::last:
            break;
    }
    return 0;
}

time_duration weather_type::get_field_decay() const
{
    if( field_decay_override ) {
        return *field_decay_override;
    }
    switch( precip ) {
        case precip_class::very_light:
            return 5_turns;
        case precip_class::light:
            return 15_turns;
        case precip_class::heavy:
            return 45_turns;
        case precip_class::none:
        case precip_class::last:
            break;
    }
    return 0_turns;
}

// Without a "glare" list: snow glare in winter at moderate sun, sun glare at high sun
static const std::vector<weather_glare> &default_glare()
{
    static const std::vector<weather_glare> glare = []() {
        std::vector<weather_glare> ret;
        weather_glare snow;
        snow.effect = effect_snow_glare;
        snow.min_irradiance = irradiance::moderate;
        snow.condition = []( const_dialogue const & ) {
            return season_of_year( calendar::turn ) == WINTER;
        };
        ret.push_back( snow );
        weather_glare sun;
        sun.effect = effect_glare;
        sun.min_irradiance = irradiance::high;
        ret.push_back( sun );
        return ret;
    }
    ();
    return glare;
}

const std::vector<weather_glare> &weather_type::get_glare() const
{
    return glare_override ? *glare_override : default_glare();
}

void weather_glare::load( const JsonObject &jo )
{
    mandatory( jo, false, "effect", effect );
    mandatory( jo, false, "min_irradiance", min_irradiance );
    optional( jo, false, "duration", duration, 20_turns );
    optional( jo, false, "repeat_duration", repeat_duration, 10_turns );
    if( jo.has_member( "condition" ) ) {
        read_condition( jo, "condition", condition, true );
    }
}

void weather_type::load( const JsonObject &jo, std::string_view )
{
    mandatory( jo, was_loaded, "name", name );
    mandatory( jo, was_loaded, "id",  id );

    optional( jo, was_loaded, "color", color, nc_color_reader{} );
    optional( jo, was_loaded, "map_color", map_color, nc_color_reader{} );

    mandatory( jo, was_loaded, "sym", symbol, unicode_codepoint_from_symbol_reader );
    optional( jo, was_loaded, "sun_sym", sun_symbol, unicode_codepoint_from_symbol_reader,
              static_cast<uint32_t>( 0x263C ) ); // ☼

    mandatory( jo, was_loaded, "ranged_penalty", ranged_penalty );
    mandatory( jo, was_loaded, "sight_penalty", sight_penalty );
    mandatory( jo, was_loaded, "light_modifier", light_modifier );
    if( jo.has_number( "temperature_modifier" ) ) {
        // A plain number is degrees Celsius, like a weather generator's base_temperature.
        // The units reader would take it as legacy body temperature units (0.002 K).
        temperature_modifier = units::from_celsius_delta( jo.get_float( "temperature_modifier" ) );
    } else {
        optional( jo, was_loaded, "temperature_modifier", temperature_modifier, 0_K_delta );
    }
    mandatory( jo, was_loaded, "priority", priority );
    optional( jo, was_loaded, "light_multiplier", light_multiplier, 1.f );
    optional( jo, was_loaded, "sun_multiplier", sun_multiplier, 1.f );
    if( jo.has_array( "tint_color" ) ) {
        JsonArray jarr = jo.get_array( "tint_color" );
        tint_color.r = jarr.get_int( 0 ) / 255.0f;
        tint_color.g = jarr.get_int( 1 ) / 255.0f;
        tint_color.b = jarr.get_int( 2 ) / 255.0f;
    }
    optional( jo, was_loaded, "tint_strength", tint_strength, 0.0f );

    mandatory( jo, was_loaded, "sound_attn", sound_attn );
    mandatory( jo, was_loaded, "dangerous", dangerous );
    mandatory( jo, was_loaded, "precip", precip );
    mandatory( jo, was_loaded, "rains", rains );
    optional( jo, was_loaded, "snow_rate", snow_rate );
    if( snow_rate && *snow_rate < 0.0 ) {
        jo.throw_error_at( "snow_rate", "snow_rate can't be negative" );
    }
    if( jo.has_member( "precip_rate" ) ) {
        const double rate = jo.get_float( "precip_rate" );
        if( rate < 0 ) {
            jo.throw_error_at( "precip_rate", "precip_rate must not be negative" );
        }
        precip_rate_override = rate;
    }
    if( jo.has_member( "wetness" ) ) {
        const int wet = jo.get_int( "wetness" );
        if( wet < 0 ) {
            jo.throw_error_at( "wetness", "wetness must not be negative" );
        }
        wetness_override = wet;
    }
    if( jo.has_member( "field_decay" ) ) {
        time_duration decay = 0_turns;
        mandatory( jo, false, "field_decay", decay );
        field_decay_override = decay;
    }
    if( jo.has_member( "glare" ) ) {
        std::vector<weather_glare> glare_list;
        for( JsonObject jg : jo.get_array( "glare" ) ) {
            weather_glare entry;
            entry.load( jg );
            glare_list.push_back( std::move( entry ) );
        }
        glare_override = std::move( glare_list );
    }
    optional( jo, was_loaded, "tiles_animation", tiles_animation, "" );
    optional( jo, was_loaded, "sound_category", sound_category, weather_sound_category::silent );
    optional( jo, was_loaded, "duration_min", duration_min, 5_minutes );
    optional( jo, was_loaded, "duration_max", duration_max, 5_minutes );
    if( duration_min > duration_max ) {
        jo.throw_error( "duration_min must be less than or equal to duration_max" );
    }

    if( jo.has_array( "passive_effects" ) ) {
        for( JsonObject job : jo.get_array( "passive_effects" ) ) {
            field_effect fe;
            fe.deserialize( job );
            passive_effect.emplace_back( fe );
        }
    }
    optional( jo, was_loaded, "debug_cause_eoc", debug_cause_eoc );
    optional( jo, was_loaded, "debug_leave_eoc", debug_leave_eoc );

    optional( jo, was_loaded, "weather_animation", weather_animation );
    optional( jo, was_loaded, "required_weathers", required_weathers );
    // FIXME: generic factory reader for condition
    read_condition( jo, "condition", condition, true );
}

void weather_animation_t::deserialize( const JsonObject &jo )
{
    mandatory( jo, false, "factor", factor );
    mandatory( jo, false, "color", color, nc_color_reader{} );
    mandatory( jo, false, "sym", symbol, unicode_codepoint_from_symbol_reader );
}

void weather_types::reset()
{
    weather_type_factory.reset();
    pending_weather_refs.clear();
}

void weather_types::add_pending_ref( const std::string &id, const std::string &context )
{
    pending_weather_refs.emplace_back( id, context );
}

void weather_types::finalize_all()
{
    weather_type_factory.finalize();
}

const std::vector<weather_type> &weather_types::get_all()
{
    return weather_type_factory.get_all();
}

void weather_types::check_consistency()
{
    if( !WEATHER_CLEAR.is_valid() ) {
        cata_fatal( "Weather type clear is required." );
    }
    if( !WEATHER_NULL.is_valid() ) {
        cata_fatal( "Weather type null is required." );
    }
    weather_type_factory.check();
    for( const auto &[id, context] : pending_weather_refs ) {
        const weather_type_id wid( id );
        if( !wid.is_valid() || wid == WEATHER_NULL ) {
            debugmsg( "Weather type \"%s\" (used by %s) does not exist", id, context );
        }
    }
    pending_weather_refs.clear();
}

void weather_types::load( const JsonObject &jo, const std::string &src )
{
    weather_type_factory.load( jo, src );
}
