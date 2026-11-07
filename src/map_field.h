#pragma once
#ifndef CATA_SRC_MAP_FIELD_H
#define CATA_SRC_MAP_FIELD_H

#include <cstdint>
#include <optional>
#include <set>
#include <vector>

#include "calendar.h"
#include "coords_fwd.h"
#include "type_id.h"

class JsonValue;
class field_entry;
class monster;
struct field_proc_data;
struct field_type;
template <typename E> struct enum_traits;

/**
 * A behaviour a field type runs on every one of its fields each turn, picked in the field
 * type's "processors" (see doc/JSON/JSON_INFO.md, "Field types").
 */
enum class field_processor_type : int {
    // Burns items, terrain and furniture, spreads, smokes and makes hot air (fd_fire)
    fire,
    // Turns fungal terrain into "terrain" and removes fungal furniture (fd_fungicidal_gas, fd_fire)
    fungicide,
    // Falls down through open air (fd_acid)
    fall,
    // Puts out the listed fields on its tile, one intensity for one (fd_extinguisher)
    extinguish,
    // Arcs to nearby tiles, preferring walls and creatures (fd_electricity)
    arc,
    // Pushes items to neighbouring tiles holding the same field (fd_push_items)
    push_items,
    // Weakens, then turns into "next_field" (fd_fire_vent and fd_flame_burst)
    vent_cycle,
    // Recharges, then shoots bolts of "field" (fd_shock_vent)
    shock_vent,
    // Recharges, then spills "field" around it (fd_acid_vent)
    acid_vent,
    // Sets flammable neighbours alight with "field" and makes hot air (fd_incendiary)
    incendiary,
    // Spreads fungus (fd_fungal_haze)
    fungal_haze,
    // Goes away once older than "max_age" or when a creature other than the avatar is on it
    // (fd_last_known)
    last_known,
    last
};

template<>
struct enum_traits<field_processor_type> {
    static constexpr field_processor_type last = field_processor_type::last;
};

/**
 * What a field does to a creature on its tile, picked in the field type's "contact_processors".
 * Each is the contact behaviour a vanilla field used to get by its id.
 */
enum class field_contact_type : int {
    acid,
    sap,
    sludge,
    fire,
    tear_gas,
    fungal_haze,
    flame_burst,
    electricity,
    teleport,
    shut_off,
    incendiary,
    fungicidal_gas,
    insecticidal_gas,
    smoke,
    relax_gas,
    dazzling,
    toxic_gas,
    nuke_gas,
    last
};

template<>
struct enum_traits<field_contact_type> {
    static constexpr field_contact_type last = field_contact_type::last;
};

/** A processor picked in JSON, with its parameters. Ids are resolved in finalize(). */
struct field_processor_def {
    field_processor_type type = field_processor_type::last;
    // fire: percent chance per intensity above 1 to spread to a flammable neighbour
    int spread_chance = 25;
    // fire: the field burning makes; arc: the field it spreads as (else its own);
    // vent_cycle: what it turns into; shock_vent: its bolts; acid_vent: what it spills;
    // incendiary: the fire it starts
    field_type_str_id smoke_field;
    field_type_str_id field;
    field_type_id smoke_field_id;
    field_type_id field_id;
    // fire: a field next to it that it spreads into readily, destroying it (webs)
    field_type_str_id fuel_field;
    field_type_id fuel_field_id;
    // fire: the item and furniture left by burning FLAMMABLE_ASH terrain and furniture
    itype_id ash_item;
    furn_str_id ash_furniture;
    // fungicide: what fungal terrain becomes
    ter_str_id terrain;
    // extinguish: the fields it puts out
    std::vector<field_type_str_id> fields;
    std::vector<field_type_id> field_ids;
    // arc: weights for arcing to a wall, a creature or open ground, from a passable tile
    int grounded_weight = 6;
    int creature_weight = 6;
    int other_weight = 1;
    // push_items: the items it pushes (all if empty), and what it does to whoever they hit
    std::set<itype_id> items;
    damage_type_id damage_type;
    int damage = 6;
    // vent_cycle: one in this many turns it loses an intensity until it's down to 1
    int weaken_chance = 1;
    // vent_cycle: the intensity of the field it turns into
    int next_intensity = 3;
    // last_known: how long it lasts
    time_duration max_age = 5_seconds;

    // The parameters the vanilla field using this processor had, for the members JSON leaves out
    void set_defaults();
    // A processor's name, or an object with its type and parameters
    void load( const JsonValue &jv );
    // owner: the field type the processor belongs to
    void finalize( const field_type_id &owner );
    void check( const field_type_str_id &owner ) const;
};

namespace map_field_processing
{

/**
 * Pointer to the "field processor" - a function that will be called each alive field entry of a matching field type
 * during field processing.
 */
using FieldProcessorPtr = void( * )( const tripoint_bub_ms &p, field_entry &cur,
                                     field_proc_data &pd );

/** A processor and its JSON parameters (the default ones for the processors JSON doesn't list). */
struct field_processor {
    FieldProcessorPtr fn = nullptr;
    field_processor_def def;
};

/**
 * Returns list of "field processors" for a given field type
 */
std::vector<field_processor> processors_for_type( const field_type &ft );

/**
 * Whether a monster is immune to a field because of the field's contact processors (gases that
 * spare non-breathers, sap and sludge that fliers pass over), or nullopt if they don't say.
 */
std::optional<bool> monster_contact_immunity( const field_type &ft, const monster &z );

} // namespace map_field_processing

#endif // CATA_SRC_MAP_FIELD_H
