#pragma once
#ifndef CATA_SRC_WEATHER_TYPE_H
#define CATA_SRC_WEATHER_TYPE_H

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "calendar.h"
#include "catacharset.h"
#include "color.h"
#include "field_type.h"
#include "lightmap.h"
#include "translation.h"
#include "type_id.h"
#include "units.h"

class JsonObject;
struct const_dialogue;
template <typename E> struct enum_traits;
template<typename T>
class generic_factory;

const weather_type_id WEATHER_NULL( "null" );
const weather_type_id WEATHER_CLEAR( "clear" );

enum class precip_class : int {
    none,
    very_light,
    light,
    heavy,
    last
};
template<>
struct enum_traits<precip_class> {
    static constexpr precip_class last = precip_class::last;
};

enum weather_sound_category : int {
    silent,
    drizzle,
    rainy,
    rainstorm,
    thunder,
    flurries,
    snowstorm,
    snow,
    portal_storm,
    clear,
    sunny,
    cloudy,
    last
};

template<>
struct enum_traits<weather_sound_category> {
    static constexpr weather_sound_category last = weather_sound_category::last;
};

/**
 * Weather animation class.
 */
struct weather_animation_t {
    float factor = 0.0f;
    nc_color color = c_white;
    uint32_t symbol = NULL_UNICODE;
    std::string get_symbol() const {
        return utf32_to_utf8( symbol );
    }

    void deserialize( const JsonObject &jo );
};

/**
 * One kind of glare a weather type can cause in the eyes of characters out in the sun.
 * The first entry of a weather type whose irradiance and condition are met is applied.
 */
struct weather_glare {
    efftype_id effect;
    // The sun's irradiance (W/m2, after the weather's sun_multiplier) must be above this
    float min_irradiance = 0.0f;
    // How long the effect lasts when first applied, and when it is renewed
    time_duration duration = 20_turns;
    time_duration repeat_duration = 10_turns;
    // Checked with the character as alpha; empty means always
    std::function<bool( const_dialogue const & )> condition;

    void load( const JsonObject &jo );
};

struct weather_type {
    public:
        friend class generic_factory<weather_type>;
        bool was_loaded = false;
        weather_type_id id;
        std::vector<std::pair<weather_type_id, mod_id>> src;
        // UI name of weather type.
        translation name;
        // UI color of weather type.
        nc_color color = c_white;
        // Map color of weather type.
        nc_color map_color = c_white;
        // Map glyph of weather type.
        uint32_t symbol = PERCENT_SIGN_UNICODE;
        // Sun glyph of weather type.
        uint32_t sun_symbol = NULL_UNICODE;
        // Penalty to ranged attacks.
        int ranged_penalty = 0;
        // Penalty to per-square visibility, applied in transparency map.
        float sight_penalty = 0.0f;
        // Modification to ambient light.
        int light_modifier = 0;
        // Increases or decreases average temperature when this weather is active
        units::temperature_delta temperature_modifier = 0_K_delta;
        // Multiplier to ambient light.
        float light_multiplier = 1.f;
        // Multiplier to radiation from Sun.
        float sun_multiplier = 1.f;
        // Sound attenuation of a given weather type.
        int sound_attn = 0;
        // If true, our activity gets interrupted.
        bool dangerous = false;
        // Amount of associated precipitation.
        precip_class precip = precip_class::none;
        // Whether said precipitation falls as rain.
        bool rains = false;
        // Optional JSON overrides of what precip implies; unset ones follow precip
        // (see get_precip_rate(), get_wetness(), get_field_decay() and get_glare()).
        std::optional<double> precip_rate_override;
        std::optional<int> wetness_override;
        std::optional<time_duration> field_decay_override;
        std::optional<std::vector<weather_glare>> glare_override;
        /** Rainfall-equivalent precipitation in mm/h: funnels, snow, weather('precipitation') */
        double get_precip_rate() const;
        /** How hard the rain wets characters out in it (wet_character's amount) */
        int get_wetness() const;
        /** How much the rain ages outdoor fields and scent each turn */
        time_duration get_field_decay() const;
        /** The glare this weather can cause, first match wins */
        const std::vector<weather_glare> &get_glare() const;
        // string for tiles animation
        std::string tiles_animation;
        // Information for weather animations
        weather_animation_t weather_animation;
        // if playing sound effects what to use
        weather_sound_category sound_category = weather_sound_category::silent;
        // if multiple weather conditions are true the higher priority wins
        int priority = 0;
        // when this weather should happen
        std::function<bool( const_dialogue const & )> condition;
        std::vector<weather_type_id> required_weathers;
        time_duration duration_min = 0_turns;
        time_duration duration_max = 0_turns;
        std::optional<std::string> debug_cause_eoc;
        std::optional<std::string> debug_leave_eoc;
        // what effects are applied to you when you visit this dimension
        // and what protection you may use to counter it
        std::vector<field_effect> passive_effect;
        // light color tint applied when weather is active.
        // defaults to no color.
        light_color_rgb tint_color{};
        float tint_strength = 0.0f;
        void load( const JsonObject &jo, std::string_view src );
        void finalize();
        void check() const;
        std::string get_symbol() const {
            return utf32_to_utf8( symbol );
        }
        std::string get_sun_symbol() const {
            return utf32_to_utf8( sun_symbol );
        }
        weather_type() = default;
};
namespace weather_types
{
/** Get all currently loaded weather types */
const std::vector<weather_type> &get_all();
/** Finalize all loaded weather types */
void finalize_all();
/** Clear all loaded weather types (invalidating any pointers) */
void reset();
/** Load weather type from JSON definition */
void load( const JsonObject &jo, const std::string &src );
/** Checks all loaded from JSON are valid */
void check_consistency();
/** Records a weather type id named outside weather types (e.g. by an EOC), with where it
 *  came from; check_consistency() reports it if no such weather type was loaded. */
void add_pending_ref( const std::string &id, const std::string &context );
} // namespace weather_types
#endif // CATA_SRC_WEATHER_TYPE_H
