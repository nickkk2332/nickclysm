#include "field_type.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "debug.h"
#include "effect_on_condition.h"
#include "enum_conversions.h"
#include "enums.h"
#include "flexbuffer_json.h"
#include "generic_factory.h"
#include "scent_block.h"
#include "string_formatter.h"

static const damage_type_id damage_bash( "bash" );

static const furn_str_id furn_f_ash( "f_ash" );

static const itype_id itype_ash( "ash" );

static const ter_str_id ter_t_dirt( "t_dirt" );

const field_type_str_id fd_null = field_type_str_id::NULL_ID();
const field_type_str_id fd_acid( "fd_acid" );
const field_type_str_id fd_acid_vent( "fd_acid_vent" );
const field_type_str_id fd_bile( "fd_bile" );
const field_type_str_id fd_blood( "fd_blood" );
const field_type_str_id fd_blood_footprint_e( "fd_blood_footprint_e" );
const field_type_str_id fd_blood_footprint_n( "fd_blood_footprint_n" );
const field_type_str_id fd_blood_footprint_s( "fd_blood_footprint_s" );
const field_type_str_id fd_blood_footprint_w( "fd_blood_footprint_w" );
const field_type_str_id fd_blood_insect( "fd_blood_insect" );
const field_type_str_id fd_blood_invertebrate( "fd_blood_invertebrate" );
const field_type_str_id fd_blood_veggy( "fd_blood_veggy" );
const field_type_str_id fd_churned_earth( "fd_churned_earth" );
const field_type_str_id fd_cold_air2( "fd_cold_air2" );
const field_type_str_id fd_cold_air3( "fd_cold_air3" );
const field_type_str_id fd_cold_air4( "fd_cold_air4" );
const field_type_str_id fd_construction_site( "fd_construction_site" );
const field_type_str_id fd_dazzling( "fd_dazzling" );
const field_type_str_id fd_electricity( "fd_electricity" );
const field_type_str_id fd_electricity_unlit( "fd_electricity_unlit" );
const field_type_str_id fd_extinguisher( "fd_extinguisher" );
const field_type_str_id fd_fatigue( "fd_fatigue" );
const field_type_str_id fd_fire( "fd_fire" );
const field_type_str_id fd_fire_vent( "fd_fire_vent" );
const field_type_str_id fd_flame_burst( "fd_flame_burst" );
const field_type_str_id fd_fungal_haze( "fd_fungal_haze" );
const field_type_str_id fd_fungicidal_gas( "fd_fungicidal_gas" );
const field_type_str_id fd_gas_vent( "fd_gas_vent" );
const field_type_str_id fd_gibs_flesh( "fd_gibs_flesh" );
const field_type_str_id fd_gibs_insect( "fd_gibs_insect" );
const field_type_str_id fd_gibs_invertebrate( "fd_gibs_invertebrate" );
const field_type_str_id fd_gibs_veggy( "fd_gibs_veggy" );
const field_type_str_id fd_hot_air1( "fd_hot_air1" );
const field_type_str_id fd_hot_air2( "fd_hot_air2" );
const field_type_str_id fd_hot_air3( "fd_hot_air3" );
const field_type_str_id fd_hot_air4( "fd_hot_air4" );
const field_type_str_id fd_incendiary( "fd_incendiary" );
const field_type_str_id fd_insecticidal_gas( "fd_insecticidal_gas" );
const field_type_str_id fd_laser( "fd_laser" );
const field_type_str_id fd_last_known( "fd_last_known" );
const field_type_str_id fd_nuke_gas( "fd_nuke_gas" );
const field_type_str_id fd_plasma( "fd_plasma" );
const field_type_str_id fd_push_items( "fd_push_items" );
const field_type_str_id fd_relax_gas( "fd_relax_gas" );
const field_type_str_id fd_sap( "fd_sap" );
const field_type_str_id fd_shock_vent( "fd_shock_vent" );
const field_type_str_id fd_slime( "fd_slime" );
const field_type_str_id fd_sludge( "fd_sludge" );
const field_type_str_id fd_smoke( "fd_smoke" );
const field_type_str_id fd_smoke_vent( "fd_smoke_vent" );
const field_type_str_id fd_tear_gas( "fd_tear_gas" );
const field_type_str_id fd_tindalos_rift( "fd_tindalos_rift" );
const field_type_str_id fd_toxic_gas( "fd_toxic_gas" );
const field_type_str_id fd_web( "fd_web" );

namespace io
{

template<>
std::string enum_to_string<game_message_type>( game_message_type data )
{
    switch( data ) {
            // *INDENT-OFF*
        case game_message_type::m_good: return "good";
        case game_message_type::m_bad: return "bad";
        case game_message_type::m_mixed: return "mixed";
        case game_message_type::m_warning: return "warning";
        case game_message_type::m_info: return "info";
        case game_message_type::m_neutral: return "neutral";
        case game_message_type::m_debug: return "debug";
        case game_message_type::m_headshot: return "headshot";
        case game_message_type::m_critical: return "critical";
        case game_message_type::m_grazing: return "grazing";
            // *INDENT-ON*
        case game_message_type::num_game_message_type:
            break;
    }
    cata_fatal( "Invalid game_message_type" );
}

template<>
std::string enum_to_string<description_affix>( description_affix data )
{
    switch( data ) {
        // *INDENT-OFF*
        case description_affix::DESCRIPTION_AFFIX_IN: return "in";
        case description_affix::DESCRIPTION_AFFIX_COVERED_IN: return "covered_in";
        case description_affix::DESCRIPTION_AFFIX_ON: return "on";
        case description_affix::DESCRIPTION_AFFIX_UNDER: return "under";
        case description_affix::DESCRIPTION_AFFIX_ILLUMINATED_BY: return "illuminated_by";
        // *INDENT-ON*
        case description_affix::DESCRIPTION_AFFIX_NUM:
            break;
    }
    debugmsg( "Invalid description affix value '%d'.", data );
    return "invalid";
}

template<>
std::string enum_to_string<field_decay_mode>( field_decay_mode data )
{
    switch( data ) {
        // *INDENT-OFF*
        case field_decay_mode::exponential: return "exponential";
        case field_decay_mode::linear: return "linear";
        case field_decay_mode::fire: return "fire";
        // *INDENT-ON*
        case field_decay_mode::last:
            break;
    }
    cata_fatal( "Invalid field_decay_mode" );
}

template<>
std::string enum_to_string<field_contact_body_parts>( field_contact_body_parts data )
{
    switch( data ) {
        // *INDENT-OFF*
        case field_contact_body_parts::ground_contact: return "ground_contact";
        case field_contact_body_parts::all: return "all";
        case field_contact_body_parts::list: return "list";
        // *INDENT-ON*
        case field_contact_body_parts::last:
            break;
    }
    cata_fatal( "Invalid field_contact_body_parts" );
}

template<>
std::string enum_to_string<field_processor_type>( field_processor_type data )
{
    switch( data ) {
        // *INDENT-OFF*
        case field_processor_type::fire: return "fire";
        case field_processor_type::fungicide: return "fungicide";
        case field_processor_type::fall: return "fall";
        case field_processor_type::extinguish: return "extinguish";
        case field_processor_type::arc: return "arc";
        case field_processor_type::push_items: return "push_items";
        case field_processor_type::vent_cycle: return "vent_cycle";
        case field_processor_type::shock_vent: return "shock_vent";
        case field_processor_type::acid_vent: return "acid_vent";
        case field_processor_type::incendiary: return "incendiary";
        case field_processor_type::fungal_haze: return "fungal_haze";
        case field_processor_type::last_known: return "last_known";
        // *INDENT-ON*
        case field_processor_type::last:
            break;
    }
    cata_fatal( "Invalid field_processor_type" );
}

template<>
std::string enum_to_string<field_contact_type>( field_contact_type data )
{
    switch( data ) {
        // *INDENT-OFF*
        case field_contact_type::acid: return "acid";
        case field_contact_type::sap: return "sap";
        case field_contact_type::sludge: return "sludge";
        case field_contact_type::fire: return "fire";
        case field_contact_type::tear_gas: return "tear_gas";
        case field_contact_type::fungal_haze: return "fungal_haze";
        case field_contact_type::flame_burst: return "flame_burst";
        case field_contact_type::electricity: return "electricity";
        case field_contact_type::teleport: return "teleport";
        case field_contact_type::shut_off: return "shut_off";
        case field_contact_type::incendiary: return "incendiary";
        case field_contact_type::fungicidal_gas: return "fungicidal_gas";
        case field_contact_type::insecticidal_gas: return "insecticidal_gas";
        case field_contact_type::smoke: return "smoke";
        case field_contact_type::relax_gas: return "relax_gas";
        case field_contact_type::dazzling: return "dazzling";
        case field_contact_type::toxic_gas: return "toxic_gas";
        case field_contact_type::nuke_gas: return "nuke_gas";
        // *INDENT-ON*
        case field_contact_type::last:
            break;
    }
    cata_fatal( "Invalid field_contact_type" );
}

} // namespace io

generic_factory<field_type> &get_all_field_types()
{
    static generic_factory<field_type> all_field_types( "field types" );
    return all_field_types;
}

/** @relates int_id */
template<>
bool int_id<field_type>::is_valid() const
{
    return get_all_field_types().is_valid( *this );
}

/** @relates int_id */
template<>
const field_type &int_id<field_type>::obj() const
{
    return get_all_field_types().obj( *this );
}

/** @relates int_id */
template<>
const string_id<field_type> &int_id<field_type>::id() const
{
    return get_all_field_types().convert( *this );
}

/** @relates string_id */
template<>
bool string_id<field_type>::is_valid() const
{
    return get_all_field_types().is_valid( *this );
}

/** @relates string_id */
template<>
const field_type &string_id<field_type>::obj() const
{
    return get_all_field_types().obj( *this );
}

template<>
int_id<field_type> string_id<field_type>::id_or( const int_id<field_type> &fallback ) const
{
    if( get_all_field_types().initialized ) {
        return get_all_field_types().convert( *this, fallback, false );
    }
    return fallback;
}

/** @relates string_id */
template<>
int_id<field_type> string_id<field_type>::id() const
{
    return get_all_field_types().convert( *this, fd_null.id_or( int_id<field_type>() ) );
}

/** @relates int_id */
template<>
int_id<field_type>::int_id( const string_id<field_type> &id ) : _id( id.id() )
{
}

// Reads an enum from a JSON string, as a JSON error naming the value if it is unknown
template<typename E>
static E read_field_enum( const JsonValue &jv, const char *what )
{
    const std::string name = jv.get_string();
    const std::optional<E> value = io::string_to_enum_optional<E>( name );
    if( !value ) {
        jv.throw_error( string_format( "unknown %s \"%s\"", what, name ) );
    }
    return *value;
}

void field_processor_def::set_defaults()
{
    // The values the vanilla field using this processor had in C++
    switch( type ) {
        case field_processor_type::fire:
            smoke_field = fd_smoke;
            fuel_field = fd_web;
            ash_item = itype_ash;
            ash_furniture = furn_f_ash;
            break;
        case field_processor_type::fungicide:
            terrain = ter_t_dirt;
            break;
        case field_processor_type::push_items:
            damage_type = damage_bash;
            break;
        case field_processor_type::shock_vent:
            field = fd_electricity;
            break;
        case field_processor_type::acid_vent:
            field = fd_acid;
            break;
        case field_processor_type::incendiary:
            field = fd_fire;
            break;
        case field_processor_type::fall:
        case field_processor_type::extinguish:
        case field_processor_type::arc:
        case field_processor_type::vent_cycle:
        case field_processor_type::fungal_haze:
        case field_processor_type::last_known:
        case field_processor_type::last:
            break;
    }
}

void field_processor_def::load( const JsonValue &jv )
{
    if( jv.test_string() ) {
        type = read_field_enum<field_processor_type>( jv, "field processor" );
        if( type == field_processor_type::extinguish || type == field_processor_type::vent_cycle ) {
            jv.throw_error( string_format( "the %s processor needs an object with its %s",
                                           jv.get_string(), type == field_processor_type::extinguish ? "fields" : "next_field" ) );
        }
        set_defaults();
        return;
    }
    const JsonObject jo = jv.get_object();
    type = read_field_enum<field_processor_type>( jo.get_member( "type" ), "field processor" );
    set_defaults();
    // Each type reads only its own members, so a member another type takes is an error
    switch( type ) {
        case field_processor_type::fire:
            optional( jo, true, "spread_chance", spread_chance );
            optional( jo, true, "smoke_field", smoke_field );
            optional( jo, true, "fuel_field", fuel_field );
            optional( jo, true, "ash_item", ash_item );
            optional( jo, true, "ash_furniture", ash_furniture );
            break;
        case field_processor_type::fungicide:
            optional( jo, true, "terrain", terrain );
            break;
        case field_processor_type::extinguish:
            mandatory( jo, false, "fields", fields );
            break;
        case field_processor_type::arc:
            optional( jo, true, "field", field );
            optional( jo, true, "grounded_weight", grounded_weight );
            optional( jo, true, "creature_weight", creature_weight );
            optional( jo, true, "other_weight", other_weight );
            break;
        case field_processor_type::push_items:
            optional( jo, true, "items", items );
            optional( jo, true, "damage_type", damage_type );
            optional( jo, true, "damage", damage );
            break;
        case field_processor_type::vent_cycle:
            mandatory( jo, false, "next_field", field );
            optional( jo, true, "weaken_chance", weaken_chance );
            optional( jo, true, "next_intensity", next_intensity );
            break;
        case field_processor_type::shock_vent:
        case field_processor_type::acid_vent:
        case field_processor_type::incendiary:
            optional( jo, true, "field", field );
            break;
        case field_processor_type::last_known:
            optional( jo, true, "max_age", max_age );
            break;
        case field_processor_type::fall:
        case field_processor_type::fungal_haze:
        case field_processor_type::last:
            break;
    }
}

// A field id as an int id, or fd_null when it is empty or unknown (check() reports those)
static field_type_id resolve_field_id( const field_type_str_id &fd )
{
    return !fd.is_empty() && fd.is_valid() ? fd.id() : fd_null.id();
}

void field_processor_def::finalize( const field_type_id &owner )
{
    smoke_field_id = resolve_field_id( smoke_field );
    // An arc spreads as its own field unless told otherwise
    field_id = type == field_processor_type::arc &&
               field.is_empty() ? owner : resolve_field_id( field );
    fuel_field_id = resolve_field_id( fuel_field );
    field_ids.clear();
    for( const field_type_str_id &fd : fields ) {
        field_ids.emplace_back( resolve_field_id( fd ) );
    }
}

void field_processor_def::check( const field_type_str_id &owner ) const
{
    const std::string name = io::enum_to_string( type );
    const auto check_field = [&]( const field_type_str_id & fd, const char *member ) {
        if( !fd.is_empty() && !fd.is_null() && !fd.is_valid() ) {
            debugmsg( "Field type \"%s\": the %s processor's %s \"%s\" isn't a field type.",
                      owner.c_str(), name, member, fd.c_str() );
        }
    };
    check_field( smoke_field, "smoke_field" );
    check_field( field, type == field_processor_type::vent_cycle ? "next_field" : "field" );
    check_field( fuel_field, "fuel_field" );
    for( const field_type_str_id &fd : fields ) {
        check_field( fd, "fields" );
    }
    if( !ash_item.is_empty() && !ash_item.is_valid() ) {
        debugmsg( "Field type \"%s\": the %s processor's ash_item \"%s\" isn't an item.",
                  owner.c_str(), name, ash_item.c_str() );
    }
    if( !ash_furniture.is_empty() && !ash_furniture.is_valid() ) {
        debugmsg( "Field type \"%s\": the %s processor's ash_furniture \"%s\" isn't furniture.",
                  owner.c_str(), name, ash_furniture.c_str() );
    }
    if( !terrain.is_empty() && !terrain.is_valid() ) {
        debugmsg( "Field type \"%s\": the %s processor's terrain \"%s\" isn't terrain.",
                  owner.c_str(), name, terrain.c_str() );
    }
    for( const itype_id &it : items ) {
        if( !it.is_valid() ) {
            debugmsg( "Field type \"%s\": the %s processor's items has \"%s\", which isn't an item.",
                      owner.c_str(), name, it.c_str() );
        }
    }
    if( !damage_type.is_empty() && !damage_type.is_valid() ) {
        debugmsg( "Field type \"%s\": the %s processor's damage_type \"%s\" isn't a damage type.",
                  owner.c_str(), name, damage_type.c_str() );
    }
    if( weaken_chance < 1 ) {
        debugmsg( "Field type \"%s\": the %s processor's weaken_chance must be at least 1.",
                  owner.c_str(), name );
    }
}

void field_contact_damage::deserialize( const JsonObject &jo )
{
    mandatory( jo, false, "damage_type", type );
    if( jo.has_array( "amount" ) ) {
        const JsonArray ja = jo.get_array( "amount" );
        if( ja.size() != 2 ) {
            ja.throw_error( "amount must be a number or [ min, max ]" );
        }
        min_amount = ja.get_int( 0 );
        max_amount = ja.get_int( 1 );
    } else {
        mandatory( jo, false, "amount", min_amount );
        max_amount = min_amount;
    }
    if( max_amount < min_amount ) {
        jo.throw_error_at( "amount", "the maximum amount is below the minimum" );
    }
    if( jo.has_array( "body_parts" ) ) {
        body_parts = field_contact_body_parts::list;
        mandatory( jo, false, "body_parts", parts );
    } else if( jo.has_member( "body_parts" ) ) {
        body_parts = read_field_enum<field_contact_body_parts>( jo.get_member( "body_parts" ),
                     "body_parts" );
        if( body_parts == field_contact_body_parts::list ) {
            jo.throw_error_at( "body_parts", "list the body parts in an array" );
        }
    }
    optional( jo, false, "monster_mult", monster_mult, 1.0f );
    optional( jo, false, "message", message );
    optional( jo, false, "message_npc", message_npc );
}

const field_intensity_level &field_type::get_intensity_level( int level ) const
{
    if( level < 0 || static_cast<size_t>( level ) >= intensity_levels.size() ) {
        // level + 1 for the original intensity number
        debugmsg( "Unknown intensity level %d for field type %s.", level + 1, id.str() );
        return intensity_levels.back();
    }
    return intensity_levels[level];
}

void field_effect::deserialize( const JsonObject &jo )
{
    mandatory( jo, false, "effect_id", id );
    optional( jo, false, "min_duration", min_duration );
    optional( jo, false, "max_duration", max_duration );
    optional( jo, false, "intensity", intensity );
    optional( jo, false, "body_part", bp, bodypart_str_id::NULL_ID() );
    optional( jo, false, "is_environmental", is_environmental );
    optional( jo, false, "immune_inside", immune_inside );
    optional( jo, false, "immune_in_vehicle", immune_in_vehicle );
    optional( jo, false, "immune_inside_vehicle", immune_inside_vehicle );
    optional( jo, false, "immune_outside_vehicle", immune_outside_vehicle );
    optional( jo, false, "chance_in_vehicle", chance_in_vehicle );
    optional( jo, false, "chance_inside_vehicle", chance_inside_vehicle );
    optional( jo, false, "chance_outside_vehicle", chance_outside_vehicle );
    optional( jo, false, "message", message );
    optional( jo, false, "message_npc", message_npc );
    const auto game_message_type_reader = enum_flags_reader<game_message_type> { "game message types" };
    optional( jo, false, "message_type", env_message_type, game_message_type_reader );
    JsonObject jid = jo.get_object( "immunity_data" );
    field_types::load_immunity( jid, immunity_data );
}

void field_type::load( const JsonObject &jo, std::string_view src )
{
    optional( jo, was_loaded, "legacy_enum_id", legacy_enum_id, -1 );
    if( jo.has_array( "intensity_levels" ) ) {
        // A copy-from that lists levels replaces the inherited ones rather than adding to them
        intensity_levels.clear();
    }
    // Each level inherits whatever it leaves out from the level below it, so the per-level
    // reads always apply their fallback, even on a copy-from
    for( const JsonObject jao : jo.get_array( "intensity_levels" ) ) {
        field_intensity_level intensity_level;
        field_intensity_level fallback_intensity_level = !intensity_levels.empty() ? intensity_levels.back()
                : intensity_level;
        // The first level defaults to a concentration of 1
        const int fallback_concentration = !intensity_levels.empty() ?
                                           intensity_levels.back().concentration : 1;
        optional( jao, false, "name", intensity_level.name, fallback_intensity_level.name );
        optional( jao, false, "sym", intensity_level.symbol, unicode_codepoint_from_symbol_reader,
                  fallback_intensity_level.symbol );
        intensity_level.color = jao.has_member( "color" ) ? color_from_string( jao.get_string( "color" ) ) :
                                fallback_intensity_level.color;
        optional( jao, false, "transparent", intensity_level.transparent,
                  fallback_intensity_level.transparent );
        optional( jao, false, "dangerous", intensity_level.dangerous,
                  fallback_intensity_level.dangerous );
        optional( jao, false, "move_cost", intensity_level.move_cost,
                  fallback_intensity_level.move_cost );
        optional( jao, false, "extra_radiation_min", intensity_level.extra_radiation_min,
                  fallback_intensity_level.extra_radiation_min );
        optional( jao, false, "extra_radiation_max", intensity_level.extra_radiation_max,
                  fallback_intensity_level.extra_radiation_max );
        optional( jao, false, "radiation_hurt_damage_min",
                  intensity_level.radiation_hurt_damage_min,
                  fallback_intensity_level.radiation_hurt_damage_min );
        optional( jao, false, "radiation_hurt_damage_max",
                  intensity_level.radiation_hurt_damage_max,
                  fallback_intensity_level.radiation_hurt_damage_max );
        optional( jao, false, "radiation_hurt_message", intensity_level.radiation_hurt_message,
                  fallback_intensity_level.radiation_hurt_message );
        optional( jao, false, "intensity_upgrade_chance", intensity_level.intensity_upgrade_chance,
                  fallback_intensity_level.intensity_upgrade_chance );
        optional( jao, false, "intensity_upgrade_duration",
                  intensity_level.intensity_upgrade_duration,
                  fallback_intensity_level.intensity_upgrade_duration );
        optional( jao, false, "monster_spawn_chance", intensity_level.monster_spawn_chance,
                  fallback_intensity_level.monster_spawn_chance );
        optional( jao, false, "monster_spawn_count", intensity_level.monster_spawn_count,
                  fallback_intensity_level.monster_spawn_count );
        optional( jao, false, "monster_spawn_radius", intensity_level.monster_spawn_radius,
                  fallback_intensity_level.monster_spawn_radius );
        optional( jao, false, "monster_spawn_group", intensity_level.monster_spawn_group,
                  fallback_intensity_level.monster_spawn_group );
        optional( jao, false, "light_emitted", intensity_level.light_emitted,
                  fallback_intensity_level.light_emitted );
        if( jao.has_array( "light_color" ) ) {
            JsonArray jarr = jao.get_array( "light_color" );
            intensity_level.light_color.r = jarr.get_int( 0 ) / 255.0f;
            intensity_level.light_color.g = jarr.get_int( 1 ) / 255.0f;
            intensity_level.light_color.b = jarr.get_int( 2 ) / 255.0f;
        } else {
            intensity_level.light_color = fallback_intensity_level.light_color;
        }
        optional( jao, false, "light_override", intensity_level.local_light_override,
                  fallback_intensity_level.local_light_override );
        optional( jao, false, "translucency", intensity_level.translucency,
                  fallback_intensity_level.translucency );
        optional( jao, false, "concentration", intensity_level.concentration,
                  fallback_concentration );
        optional( jao, false, "convection_temperature_mod",
                  intensity_level.convection_temperature_mod,
                  fallback_intensity_level.convection_temperature_mod );
        if( jao.has_array( "effects" ) ) {
            for( const JsonObject joe : jao.get_array( "effects" ) ) {
                field_effect fe;
                fe.deserialize( joe );
                intensity_level.field_effects.emplace_back( fe );
            }
        } else {
            // Use effects from previous intensity level
            intensity_level.field_effects = fallback_intensity_level.field_effects;
        }
        optional( jao, false, "scent_neutralization", intensity_level.scent_neutralization,
                  fallback_intensity_level.scent_neutralization );
        optional( jao, false, "sound_attenuation", intensity_level.sound_attenuation,
                  numeric_bound_reader<int> { 0, 1000 }, fallback_intensity_level.sound_attenuation );
        optional( jao, false, "heat_radiation", intensity_level.heat_radiation,
                  fallback_intensity_level.heat_radiation );
        optional( jao, false, "hot_air_field", intensity_level.hot_air_field,
                  fallback_intensity_level.hot_air_field );
        if( jao.has_array( "contact_damage" ) ) {
            for( const JsonObject jcd : jao.get_array( "contact_damage" ) ) {
                field_contact_damage cd;
                cd.deserialize( jcd );
                intensity_level.contact_damage.emplace_back( cd );
            }
        } else {
            intensity_level.contact_damage = fallback_intensity_level.contact_damage;
        }
        if( jao.has_member( "eoc_on_contact" ) ) {
            intensity_level.eoc_on_contact = effect_on_conditions::load_inline_eoc(
                                                 jao.get_member( "eoc_on_contact" ), src );
        } else {
            intensity_level.eoc_on_contact = fallback_intensity_level.eoc_on_contact;
        }
        intensity_levels.emplace_back( intensity_level );
    }
    if( intensity_levels.empty() ) {
        jo.throw_error_at( "id", "No intensity levels defined for field type" );
    }

    if( jo.has_object( "npc_complain" ) ) {
        JsonObject joc = jo.get_object( "npc_complain" );
        // Start from the inherited values, so a partial copy-from override keeps the rest
        auto &[chance, issue, duration, speech] = npc_complain_data;
        optional( joc, was_loaded, "chance", chance, 0 );
        optional( joc, was_loaded, "issue", issue );
        optional( joc, was_loaded, "duration", duration, 0_turns );
        optional( joc, was_loaded, "speech", speech );
    }

    JsonObject jid = jo.get_object( "immunity_data" );
    field_types::load_immunity( jid, immunity_data );

    optional( jo, was_loaded, "immune_mtypes", immune_mtypes );
    optional( jo, was_loaded, "block_mtypes", block_mtypes );
    optional( jo, was_loaded, "underwater_age_speedup", underwater_age_speedup, 0_turns );
    optional( jo, was_loaded, "outdoor_age_speedup", outdoor_age_speedup, 0_turns );
    optional( jo, was_loaded, "decay_amount_factor", decay_amount_factor, 0 );
    optional( jo, was_loaded, "percent_spread", percent_spread, 0 );
    optional( jo, was_loaded, "scent_neutralization_radius", scent_neutralization_radius, 1 );
    optional( jo, was_loaded, "apply_slime_factor", apply_slime_factor, 0 );
    optional( jo, was_loaded, "gas_absorption_factor", gas_absorption_factor, 0_turns );
    optional( jo, was_loaded, "is_splattering", is_splattering, false );
    optional( jo, was_loaded, "dirty_transparency_cache", dirty_transparency_cache, false );
    optional( jo, was_loaded, "has_fire", has_fire, false );
    optional( jo, was_loaded, "has_acid", has_acid, false );
    optional( jo, was_loaded, "has_elec", has_elec, false );
    optional( jo, was_loaded, "has_fume", has_fume, false );
    optional( jo, was_loaded, "moppable", moppable, false );
    optional( jo, was_loaded, "priority", priority, 0 );
    optional( jo, was_loaded, "half_life", half_life, 0_turns );
    if( jo.has_member( "linear_half_life" ) ) {
        // The older spelling of "decay_mode": "linear"
        if( jo.has_member( "decay_mode" ) ) {
            jo.throw_error_at( "linear_half_life", "give either linear_half_life or decay_mode" );
        }
        decay_mode = jo.get_bool( "linear_half_life" ) ? field_decay_mode::linear :
                     field_decay_mode::exponential;
    } else if( jo.has_member( "decay_mode" ) ) {
        decay_mode = read_field_enum<field_decay_mode>( jo.get_member( "decay_mode" ), "decay_mode" );
    } else if( !was_loaded ) {
        decay_mode = field_decay_mode::exponential;
    }
    optional( jo, was_loaded, "indestructible", indestructible, false );
    const auto description_affix_reader = enum_flags_reader<description_affix> { "description affixes" };
    optional( jo, was_loaded, "description_affix", desc_affix, description_affix_reader,
              description_affix::DESCRIPTION_AFFIX_IN );
    if( jo.has_member( "phase" ) ) {
        phase = jo.get_enum_value<phase_id>( "phase", phase_id::PNULL );
    }
    optional( jo, was_loaded, "accelerated_decay", accelerated_decay, false );
    optional( jo, was_loaded, "display_items", display_items, true );
    optional( jo, was_loaded, "display_field", display_field, false );
    optional( jo, was_loaded, "legacy_make_rubble", legacy_make_rubble, false );
    optional( jo, was_loaded, "wandering_field", wandering_field, field_type_str_id::NULL_ID() );

    optional( jo, was_loaded, "mopsafe", mopsafe, false );

    optional( jo, was_loaded, "decrease_intensity_on_contact", decrease_intensity_on_contact, false );
    optional( jo, was_loaded, "opaque_variant", opaque_variant, field_type_str_id::NULL_ID() );
    // A list given replaces the inherited one
    if( jo.has_array( "processors" ) ) {
        processor_defs.clear();
        for( const JsonValue jv : jo.get_array( "processors" ) ) {
            field_processor_def def;
            def.load( jv );
            processor_defs.emplace_back( def );
        }
    }
    if( jo.has_array( "contact_processors" ) ) {
        contact_processors.clear();
        for( const JsonValue jv : jo.get_array( "contact_processors" ) ) {
            contact_processors.emplace_back( read_field_enum<field_contact_type>( jv,
                                             "contact processor" ) );
        }
    }
    if( jo.has_object( "bash" ) ) {
        if( !bash_info ) {
            bash_info.emplace();
        }
        bash_info->load( jo.get_object( "bash" ), was_loaded, "field " + id.str(), src );
    }
    if( was_loaded && jo.has_member( "copy-from" ) && looks_like.empty() ) {
        looks_like = jo.get_string( "copy-from" );
    }
    jo.read( "looks_like", looks_like );
}

void field_type::finalize()
{
    dangerous = std::any_of( intensity_levels.begin(), intensity_levels.end(),
    []( const field_intensity_level & elem ) {
        return elem.dangerous;
    } );
    transparent = std::all_of( intensity_levels.begin(), intensity_levels.end(),
    []( const field_intensity_level & elem ) {
        return elem.transparent;
    } );
    muffles_sound = std::any_of( intensity_levels.begin(), intensity_levels.end(),
    []( const field_intensity_level & elem ) {
        return elem.sound_attenuation > 0;
    } );

    if( !wandering_field.is_valid() ) {
        debugmsg( "Invalid wandering_field_id %s in field %s.", wandering_field.c_str(), id.c_str() );
        wandering_field = fd_null;
    }

    for( const mtype_id &m_id : immune_mtypes ) {
        if( !m_id.is_valid() ) {
            debugmsg( "Invalid mtype_id %s in immune_mtypes for field %s.", m_id.c_str(), id.c_str() );
        }
    }

    for( const mtype_id &m_id : block_mtypes ) {
        if( !m_id.is_valid() ) {
            debugmsg( "Invalid mtype_id %s in block_mtypes for field %s.", m_id.c_str(), id.c_str() );
        }
    }

    radiates_heat = std::any_of( intensity_levels.begin(), intensity_levels.end(),
    []( const field_intensity_level & elem ) {
        return elem.heat_radiation > 0;
    } );
    for( field_intensity_level &level : intensity_levels ) {
        level.hot_air_field_id = resolve_field_id( level.hot_air_field );
    }
    contact_mask = 0;
    for( const field_contact_type contact : contact_processors ) {
        contact_mask |= static_cast<uint32_t>( 1 ) << static_cast<int>( contact );
    }

    // Placed as opaque_variant on opaque tiles, and as the field naming this one as its
    // opaque_variant on transparent tiles
    const field_type_id self = id.id();
    transparent_variant_id = self;
    opaque_variant_id = self;
    has_variants = false;
    if( !opaque_variant.is_empty() && !opaque_variant.is_null() && opaque_variant.is_valid() &&
        opaque_variant != id ) {
        opaque_variant_id = opaque_variant.id();
        has_variants = true;
    }
    for( const field_type &other : get_all_field_types().get_all() ) {
        if( other.opaque_variant == id && other.id != id ) {
            transparent_variant_id = other.id.id();
            has_variants = true;
        }
    }

    for( field_processor_def &def : processor_defs ) {
        def.finalize( self );
    }

    // should be the last operation for the type
    processors = map_field_processing::processors_for_type( *this );
}

void field_type::check() const
{
    int i = 0;
    for( const field_intensity_level &intensity_level : intensity_levels ) {
        i++;
        if( intensity_level.name.empty() ) {
            debugmsg( "Intensity level %d defined for field type \"%s\" has empty name.", i, id.c_str() );
        }
        for( const field_effect &fe : intensity_level.field_effects ) {
            if( !fe.id.is_valid() ) {
                debugmsg( "Intensity level %d of field type \"%s\" has invalid effect \"%s\".",
                          i, id.c_str(), fe.id.c_str() );
            }
            if( !fe.bp.is_null() && !fe.bp.is_valid() ) {
                debugmsg( "Intensity level %d of field type \"%s\" has invalid body part \"%s\".",
                          i, id.c_str(), fe.bp.c_str() );
            }
        }
        const mongroup_id &group = intensity_level.monster_spawn_group;
        if( !group.is_empty() && !group.is_null() && !group.is_valid() ) {
            debugmsg( "Intensity level %d of field type \"%s\" has invalid monster group \"%s\".",
                      i, id.c_str(), group.c_str() );
        }
        const field_type_str_id &hot_air = intensity_level.hot_air_field;
        if( !hot_air.is_empty() && !hot_air.is_valid() ) {
            debugmsg( "Intensity level %d of field type \"%s\" has invalid hot_air_field \"%s\".",
                      i, id.c_str(), hot_air.c_str() );
        }
        for( const field_contact_damage &cd : intensity_level.contact_damage ) {
            if( !cd.type.is_valid() ) {
                debugmsg( "Intensity level %d of field type \"%s\" has contact_damage of invalid "
                          "damage type \"%s\".", i, id.c_str(), cd.type.c_str() );
            }
            for( const bodypart_str_id &bp : cd.parts ) {
                if( !bp.is_valid() ) {
                    debugmsg( "Intensity level %d of field type \"%s\" has contact_damage on invalid "
                              "body part \"%s\".", i, id.c_str(), bp.c_str() );
                }
            }
        }
        const effect_on_condition_id &eoc = intensity_level.eoc_on_contact;
        if( !eoc.is_empty() && !eoc.is_valid() ) {
            debugmsg( "Intensity level %d of field type \"%s\" has invalid eoc_on_contact \"%s\".",
                      i, id.c_str(), eoc.c_str() );
        }
    }
    if( !opaque_variant.is_empty() && !opaque_variant.is_null() ) {
        if( !opaque_variant.is_valid() ) {
            debugmsg( "Field type \"%s\" has invalid opaque_variant \"%s\".", id.c_str(),
                      opaque_variant.c_str() );
        } else if( opaque_variant == id || ( !opaque_variant->opaque_variant.is_empty() &&
                                             !opaque_variant->opaque_variant.is_null() ) ) {
            debugmsg( "Field type \"%s\" has opaque_variant \"%s\", which is itself or has an "
                      "opaque_variant of its own.", id.c_str(), opaque_variant.c_str() );
        }
        for( const field_type &other : get_all_field_types().get_all() ) {
            if( other.id != id && other.opaque_variant == opaque_variant ) {
                debugmsg( "Field types \"%s\" and \"%s\" both have opaque_variant \"%s\".",
                          id.c_str(), other.id.c_str(), opaque_variant.c_str() );
            }
        }
    }
    for( const field_processor_def &def : processor_defs ) {
        def.check( id );
    }
    if( bash_info ) {
        bash_info->check( "field " + id.str() );
    }
    // Only a gas that spreads lowers the scent around it (map::spread_gas)
    const bool neutralizes_scent = std::any_of( intensity_levels.begin(), intensity_levels.end(),
    []( const field_intensity_level & level ) {
        return level.scent_neutralization > 0;
    } );
    if( neutralizes_scent && !gas_can_spread() ) {
        debugmsg( "Field type \"%s\" has a scent_neutralization, which only a gas that spreads "
                  "(phase gas and a percent_spread) applies.", id.c_str() );
    }
    if( scent_neutralization_radius < 0 || scent_neutralization_radius > scent_block::border ) {
        debugmsg( "Field type \"%s\" has scent_neutralization_radius %d, outside 0 to %d.",
                  id.c_str(), scent_neutralization_radius, scent_block::border );
    }
}

size_t field_type::count()
{
    return get_all_field_types().size();
}

void field_types::load( const JsonObject &jo, const std::string &src )
{
    get_all_field_types().load( jo, src );
}

void field_types::finalize_all()
{
    get_all_field_types().finalize();
    for( const field_type &fd : get_all_field_types().get_all() ) {
        const_cast<field_type &>( fd ).finalize();
    }
}

void field_types::check_consistency()
{
    get_all_field_types().check();
}

void field_types::reset()
{
    get_all_field_types().reset();
}

void field_types::load_immunity( const JsonObject &jid, field_immunity_data &fd )
{
    for( const std::string id : jid.get_array( "flags" ) ) {
        fd.immunity_data_flags.emplace_back( id );
    }
    for( JsonArray jao : jid.get_array( "body_part_env_resistance" ) ) {
        fd.immunity_data_body_part_env_resistance.emplace_back(
            io::string_to_enum<bp_type>
            ( jao.get_string( 0 ) ), jao.get_int( 1 ) );
    }
    for( JsonArray jao : jid.get_array( "immunity_flags_worn" ) ) {
        fd.immunity_data_part_item_flags.emplace_back( io::string_to_enum<bp_type>
                ( jao.get_string( 0 ) ), jao.get_string( 1 ) );
    }

    for( JsonArray jao : jid.get_array( "immunity_flags_worn_any" ) ) {
        fd.immunity_data_part_item_flags_any.emplace_back(
            io::string_to_enum<bp_type>
            ( jao.get_string( 0 ) ), jao.get_string( 1 ) );
    }
}

const std::vector<field_type> &field_types::get_all()
{
    return get_all_field_types().get_all();
}

field_type field_types::get_field_type_by_legacy_enum( int legacy_enum_id )
{
    for( const field_type &ft : get_all_field_types().get_all() ) {
        if( legacy_enum_id == ft.legacy_enum_id ) {
            return ft;
        }
    }
    debugmsg( "Cannot find field_type for legacy enum: %d.", legacy_enum_id );
    return field_type();
}
