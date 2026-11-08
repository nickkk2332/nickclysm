#pragma once
#ifndef CATA_SRC_FIELD_TYPE_H
#define CATA_SRC_FIELD_TYPE_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "lightmap.h"

#include "calendar.h"
#include "catacharset.h"
#include "color.h"
#include "effect.h"
#include "effect_source.h"
#include "enums.h"
#include "map_field.h"
#include "mapdata.h"
#include "translation.h"
#include "type_id.h"

class JsonObject;
struct field_type;
template <typename E> struct enum_traits;
template <typename T> class generic_factory;

enum class description_affix : int {
    DESCRIPTION_AFFIX_IN,
    DESCRIPTION_AFFIX_COVERED_IN,
    DESCRIPTION_AFFIX_ON,
    DESCRIPTION_AFFIX_UNDER,
    DESCRIPTION_AFFIX_ILLUMINATED_BY,
    DESCRIPTION_AFFIX_NUM
};

namespace std
{
template <>
struct hash<description_affix> {
    std::size_t operator()( const description_affix &k ) const noexcept {
        return static_cast<size_t>( k );
    }
};
} // namespace std

template<>
struct enum_traits<description_affix> {
    static constexpr description_affix last = description_affix::DESCRIPTION_AFFIX_NUM;
};

generic_factory<field_type> &get_all_field_types();

struct field_immunity_data {
    std::vector<json_character_flag> immunity_data_flags;
    std::vector<std::pair<bp_type, int>> immunity_data_body_part_env_resistance;
    std::vector < std::pair<bp_type, flag_id>> immunity_data_part_item_flags;
    std::vector < std::pair<bp_type, flag_id>> immunity_data_part_item_flags_any;
};

struct field_effect {
    efftype_id id;
    std::vector<std::pair<efftype_id, mod_id>> src; // NOLINT(cata-serialize)
    time_duration min_duration = 0_seconds;
    time_duration max_duration = 0_seconds;
    int intensity = 0;
    bodypart_str_id bp;
    field_immunity_data immunity_data;
    bool is_environmental = true;
    bool immune_inside  = false;
    bool immune_in_vehicle  = false;
    bool immune_inside_vehicle  = false;
    bool immune_outside_vehicle = false;
    int chance_in_vehicle = 0;
    int chance_inside_vehicle = 0;
    int chance_outside_vehicle = 0;
    game_message_type env_message_type = m_neutral;
    translation message;
    translation message_npc;
    time_duration get_duration() const {
        return rng( min_duration, max_duration );
    }
    std::string get_message() const {
        return message.translated();
    }
    std::string get_message_npc() const {
        return message_npc.translated();
    }
    effect get_effect( const time_point &start_time = calendar::turn ) const {
        return effect( effect_source::empty(), &id.obj(), get_duration(), bp, false, intensity,
                       start_time );
    }

    void deserialize( const JsonObject &jo );
};

// Which body parts a field's contact_damage hits on a character
enum class field_contact_body_parts : int {
    // The parts touching the ground: feet (hands and feet on all fours), or all of them lying down
    ground_contact,
    // Every main body part
    all,
    // The listed parts
    list,
    last
};

template<>
struct enum_traits<field_contact_body_parts> {
    static constexpr field_contact_body_parts last = field_contact_body_parts::last;
};

// Damage a field intensity deals each time a creature is on it ("contact_damage")
struct field_contact_damage {
    damage_type_id type;
    int min_amount = 0;
    int max_amount = 0;
    field_contact_body_parts body_parts = field_contact_body_parts::ground_contact;
    std::vector<bodypart_str_id> parts;
    // A monster takes the amount times this, once, on its torso
    float monster_mult = 1.0f;
    translation message;
    translation message_npc;

    void deserialize( const JsonObject &jo );
};

struct field_intensity_level {
    translation name;
    uint32_t symbol = PERCENT_SIGN_UNICODE;
    nc_color color = c_white;
    bool dangerous = false;
    bool transparent = true;
    int move_cost = 0;
    int extra_radiation_min = 0;
    int extra_radiation_max = 0;
    int radiation_hurt_damage_min = 0;
    int radiation_hurt_damage_max = 0;
    translation radiation_hurt_message;
    int intensity_upgrade_chance = 0;
    time_duration intensity_upgrade_duration = 0_turns;
    int monster_spawn_chance = 0;
    int monster_spawn_count = 0;
    int monster_spawn_radius = 0;
    mongroup_id monster_spawn_group;
    float light_emitted = 0.0f;
    light_color_rgb light_color{};
    float local_light_override = -1.0f;
    float translucency = 0.0f;
    int concentration = 0;
    int convection_temperature_mod = 0;
    int scent_neutralization = 0;
    // Volume a sound loses passing through a tile with this field
    int sound_attenuation = 0;
    // Heat it radiates, as a terrain's heat_radiation does: a fire's intensity
    int heat_radiation = 0;
    // The hot air the fire, vent_cycle and incendiary processors spread around it
    field_type_str_id hot_air_field;
    // hot_air_field resolved in finalize(), fd_null for none
    field_type_id hot_air_field_id;
    std::vector<field_effect> field_effects;
    std::vector<field_contact_damage> contact_damage;
    // Run with the creature on it as u, and the context values loc, field and intensity
    effect_on_condition_id eoc_on_contact;
};

// How a field loses intensity as it ages
enum class field_decay_mode : int {
    // One intensity after a random time, two half lives on average
    exponential,
    // One intensity every half life
    linear,
    // fd_fire's curve: one intensity once half_life (in turns) is less than 2d(age in turns)
    fire,
    last
};

template<>
struct enum_traits<field_decay_mode> {
    static constexpr field_decay_mode last = field_decay_mode::last;
};

// NOLINTNEXTLINE(cata-static-int_id-constants)
const field_type_id INVALID_FIELD_TYPE_ID = field_type_id( -1 );
extern const field_type_str_id fd_null;
extern const field_type_str_id fd_acid;
extern const field_type_str_id fd_acid_vent;
extern const field_type_str_id fd_bile;
extern const field_type_str_id fd_blood;
extern const field_type_str_id fd_blood_footprint_n;
extern const field_type_str_id fd_blood_footprint_w;
extern const field_type_str_id fd_blood_footprint_s;
extern const field_type_str_id fd_blood_footprint_e;
extern const field_type_str_id fd_blood_insect;
extern const field_type_str_id fd_blood_invertebrate;
extern const field_type_str_id fd_blood_veggy;
extern const field_type_str_id fd_churned_earth;
extern const field_type_str_id fd_construction_site;
extern const field_type_str_id fd_cold_air2;
extern const field_type_str_id fd_cold_air3;
extern const field_type_str_id fd_cold_air4;
extern const field_type_str_id fd_dazzling;
extern const field_type_str_id fd_electricity;
extern const field_type_str_id fd_electricity_unlit;
extern const field_type_str_id fd_extinguisher;
extern const field_type_str_id fd_fatigue;
extern const field_type_str_id fd_fire;
extern const field_type_str_id fd_fire_vent;
extern const field_type_str_id fd_flame_burst;
extern const field_type_str_id fd_fungal_haze;
extern const field_type_str_id fd_fungicidal_gas;
extern const field_type_str_id fd_gas_vent;
extern const field_type_str_id fd_gibs_flesh;
extern const field_type_str_id fd_gibs_insect;
extern const field_type_str_id fd_gibs_invertebrate;
extern const field_type_str_id fd_gibs_veggy;
extern const field_type_str_id fd_hot_air1;
extern const field_type_str_id fd_hot_air2;
extern const field_type_str_id fd_hot_air3;
extern const field_type_str_id fd_hot_air4;
extern const field_type_str_id fd_incendiary;
extern const field_type_str_id fd_insecticidal_gas;
extern const field_type_str_id fd_laser;
extern const field_type_str_id fd_last_known;
extern const field_type_str_id fd_nuke_gas;
extern const field_type_str_id fd_plasma;
extern const field_type_str_id fd_push_items;
extern const field_type_str_id fd_relax_gas;
extern const field_type_str_id fd_sap;
extern const field_type_str_id fd_shock_vent;
extern const field_type_str_id fd_slime;
extern const field_type_str_id fd_sludge;
extern const field_type_str_id fd_smoke;
extern const field_type_str_id fd_smoke_vent;
extern const field_type_str_id fd_tear_gas;
extern const field_type_str_id fd_tindalos_rift;
extern const field_type_str_id fd_toxic_gas;
extern const field_type_str_id fd_web;

struct field_type {
    public:
        void load( const JsonObject &jo, std::string_view src );
        void finalize();
        void check() const;

        // Used by generic_factory
        field_type_str_id id;
        std::vector<std::pair<field_type_str_id, mod_id>> src;
        bool was_loaded = false;

        int legacy_enum_id = -1;

        std::vector<field_intensity_level> intensity_levels;

        time_duration underwater_age_speedup = 0_turns;
        time_duration outdoor_age_speedup = 0_turns;
        int decay_amount_factor = 0;
        int percent_spread = 0;
        // How far from the field scent_neutralization reaches, in tiles
        int scent_neutralization_radius = 1;
        int apply_slime_factor = 0;
        time_duration gas_absorption_factor = 0_turns;
        bool is_splattering = false;
        bool dirty_transparency_cache = false;
        // set in finalize: some intensity level has a sound_attenuation
        bool muffles_sound = false;
        bool has_fire = false;
        bool has_acid = false;
        bool has_elec = false;
        bool has_fume = false;
        bool moppable = false;
        description_affix desc_affix = description_affix::DESCRIPTION_AFFIX_NUM;
        std::optional<map_fd_bash_info> bash_info;

        // chance, issue, duration, speech
        std::tuple<int, std::string, time_duration, translation> npc_complain_data;
        field_immunity_data immunity_data;

        std::set<mtype_id> immune_mtypes;
        std::set<mtype_id> block_mtypes;

        int priority = 0;
        time_duration half_life = 0_turns;
        phase_id phase = phase_id::PNULL;
        bool accelerated_decay = false;
        bool display_items = true;
        bool display_field = false;
        bool legacy_make_rubble = false;
        field_decay_mode decay_mode = field_decay_mode::exponential;
        bool indestructible = false;
        field_type_str_id wandering_field;
        std::string looks_like;

        bool mopsafe = false;

        bool decrease_intensity_on_contact = false;

        // On a tile that isn't transparent this field is placed as opaque_variant, which turns
        // back into this field on a transparent one (fd_electricity and fd_electricity_unlit)
        field_type_str_id opaque_variant;

        // The per-turn behaviours picked in JSON ("processors"), resolved in finalize()
        std::vector<field_processor_def> processor_defs;
        // What it does to creatures on it ("contact_processors")
        std::vector<field_contact_type> contact_processors;

    private:
        // any_of( intensity_levels::dangerous )
        bool dangerous = false;
        // all_of( intensity_levels::transparent )
        bool transparent = false;

        std::vector<map_field_processing::field_processor> processors;
        // A bit for each field_contact_type in contact_processors
        uint32_t contact_mask = 0;
        // any_of( intensity_levels::heat_radiation > 0 )
        bool radiates_heat = false;
        // The fields placed instead of this one on transparent and opaque tiles (this one when
        // it has no opaque_variant and isn't one)
        field_type_id transparent_variant_id;
        field_type_id opaque_variant_id;
        bool has_variants = false;

    public:
        const field_intensity_level &get_intensity_level( int level = 0 ) const;
        std::string get_name( int level = 0 ) const {
            return get_intensity_level( level ).name.translated();
        }
        std::string get_symbol( int level = 0 ) const {
            return utf32_to_utf8( get_intensity_level( level ).symbol );
        }
        // any_of( intensity_levels::dangerous )
        inline bool is_dangerous() const {
            return dangerous;
        }
        // all_of( intensity_levels::transparent )
        inline bool is_transparent() const {
            return transparent;
        }
        inline int get_max_intensity() const {
            return intensity_levels.size();
        }
        bool gas_can_spread() const {
            return phase == phase_id::GAS && percent_spread > 0;
        }
        const std::vector<map_field_processing::field_processor> &get_processors() const {
            return processors;
        }
        bool has_contact( field_contact_type type ) const {
            return ( contact_mask & ( static_cast<uint32_t>( 1 ) << static_cast<int>( type ) ) ) != 0;
        }
        // Heat this field radiates at an intensity (from 1), or 0
        int heat_radiation( int intensity ) const {
            return radiates_heat ? get_intensity_level( intensity - 1 ).heat_radiation : 0;
        }
        // A fire that can be used to cook and craft: has_fire, and radiates heat at this intensity
        bool is_usable_fire( int intensity ) const {
            return has_fire && heat_radiation( intensity ) > 0;
        }
        // Whether this field turns into another one on a tile of the other transparency
        bool has_transparency_variants() const {
            return has_variants;
        }
        // The field placed instead of this one on a tile of the given transparency
        field_type_id variant_for( bool transparent_tile ) const {
            return transparent_tile ? transparent_variant_id : opaque_variant_id;
        }

        static size_t count();
};

namespace field_types
{

void load( const JsonObject &jo, const std::string &src );
void finalize_all();
void check_consistency();
void reset();

void load_immunity( const JsonObject &jid, field_immunity_data &fd );

const std::vector<field_type> &get_all();
field_type get_field_type_by_legacy_enum( int legacy_enum_id );

} // namespace field_types

#endif // CATA_SRC_FIELD_TYPE_H
