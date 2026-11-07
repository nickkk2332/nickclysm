#pragma once
#ifndef CATA_SRC_MAPGEN_PRIMITIVES_H
#define CATA_SRC_MAPGEN_PRIMITIVES_H

#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "cata_assert.h"
#include "coords_fwd.h"
#include "type_id.h"

class JsonObject;
class JsonOut;
class monster;

/*
 * Actually a pair of integers that can rng, for numbers that will never exceed INT_MAX
 */
struct jmapgen_int {
    int16_t val;
    int16_t valmax;
    explicit jmapgen_int( int v ) : val( v ), valmax( v ) {
        cata_assert( v <= std::numeric_limits<int16_t>::max() );
    }
    jmapgen_int( int v, int v2 ) : val( v ), valmax( v2 ) {
        cata_assert( v <= std::numeric_limits<int16_t>::max() );
        cata_assert( v2 <= std::numeric_limits<int16_t>::max() );
    }
    /**
     * Throws as usually if the json is invalid or missing.
     */
    jmapgen_int( const JsonObject &jo, std::string_view tag );
    /**
     * Throws if the json is malformed (e.g. a string not an integer, but does not throw
     * if the member is just missing (the default values are used instead).
     */
    jmapgen_int( const JsonObject &jo, std::string_view tag, const int &def_val,
                 const int &def_valmax );

    int get() const;
};

// An effect a spawned monster starts with (spawn_data "effects")
struct spawn_effect {
    efftype_id id;
    jmapgen_int duration{ 1, 1 }; // turns
    jmapgen_int intensity{ 1, 1 };
    bool permanent = false;
};

struct spawn_data {
    std::map<itype_id, jmapgen_int> ammo;
    std::vector<point_rel_ms> patrol_points_rel_ms;
    // MONDT-J01: what a monster spawned with this data starts with
    std::vector<spawn_effect> effects;
    // its variables, text and numbers
    std::map<std::string, std::string> str_variables;
    std::map<std::string, double> num_variables;
    // empty: the monster type's own
    mfaction_str_id faction;

    bool empty() const;
    void deserialize( const JsonObject &jo );
    // as kept with a pending spawn in a submap's save
    void serialize_saved( JsonOut &jsout ) const;
    void deserialize_saved( const JsonObject &jo );
    // Adds what this data gives a monster once it is placed: patrol route, effects, variables,
    // faction (ammo is set before it is placed)
    void apply( monster &placed ) const;
};

// @p entry (a monster group entry's spawn data) with @p piece (a mapgen piece's) on top
spawn_data merged_spawn_data( spawn_data entry, const spawn_data &piece );

#endif // CATA_SRC_MAPGEN_PRIMITIVES_H
