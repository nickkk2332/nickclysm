#pragma once
#ifndef CATA_SRC_TIMED_EVENT_H
#define CATA_SRC_TIMED_EVENT_H

#include <list>
#include <map>
#include <optional>
#include <string>

#include "calendar.h"
#include "coordinates.h"
#include "explosion.h"
#include "point.h"
#include "submap.h"
#include "type_id.h"

class JsonArray;
class JsonOut;

enum class timed_event_type : int {
    NONE,
    HELP,
    WANTED,
    ROBOT_ATTACK,
    SPAWN_WYRMS,
    AMIGARA,
    AMIGARA_WHISPERS,
    ROOTS_DIE,
    TEMPLE_OPEN,
    TEMPLE_FLOOD,
    TEMPLE_SPAWN,
    DIM,
    ARTIFACT_LIGHT,
    DSA_ALRP_SUMMON,
    CUSTOM_LIGHT_LEVEL,
    TRANSFORM_RADIUS,
    UPDATE_MAPGEN,
    REVERT_SUBMAP,
    OVERRIDE_PLACE,
    EXPLOSION,
    NUM_TIMED_EVENT_TYPES
};

/** How a delayed EOC mapgen_update runs (MGEN-J02); the defaults are a plain update */
struct timed_mapgen_update {
    /** The update's parameters, as text: typed by the update when it runs */
    std::map<std::string, std::string> parameters;
    int rotation = 0;
    bool mirror_horizontal = false;
    bool mirror_vertical = false;
    /** The tile of the OMT the update's ( 0, 0 ) goes on, if any */
    std::optional<point_omt_ms> anchor;
    bool cancel_on_collision = true;

    bool is_default() const {
        return parameters.empty() && rotation == 0 && !mirror_horizontal && !mirror_vertical &&
               !anchor && cancel_on_collision;
    }
};

struct timed_event {
    timed_event_type type = timed_event_type::NONE;
    /** On which turn event should be happening. */
    time_point when = calendar::turn_zero;
    /** Which faction is responsible for handling this event. */
    int faction_id = -1;
    /** Where the event happens, in global submap coordinates */
    tripoint_abs_sm map_point = tripoint_abs_sm::invalid;
    /** Where the event happens, in global map coordinates */
    tripoint_abs_ms map_square = tripoint_abs_ms::invalid;
    /** How powerful the effect is */
    int strength = -1;
    /** type of applied effect */
    std::string string_id;
    /** key to alter this event later */
    std::string key;
    /** specifically for EXPLOSION event */
    explosion_data expl_data;
    /** The dimension the event was queued in; events at a location happen only there */
    dimension_id dimension;
    /** specifically for UPDATE_MAPGEN */
    timed_mapgen_update mapgen_update;

    submap revert;
    timed_event( timed_event_type e_t, const time_point &w, int f_id, tripoint_abs_ms p, int s,
                 std::string key );
    timed_event( timed_event_type e_t, const time_point &w, int f_id, tripoint_abs_ms p, int s,
                 std::string s_id, std::string key );
    timed_event( timed_event_type e_t, const time_point &w, int f_id, tripoint_abs_ms p, int s,
                 std::string s_id, submap sr, std::string key );
    // i have little experience with code, but something tell me
    // that storing data in header and templates
    // is horrible if you need to expand it
    timed_event( timed_event_type e_t, const time_point &w, const tripoint_abs_ms &p,
                 explosion_data explos_data );

    // When the time runs out
    void actualize();
    // Every turn
    void per_turn();
};

class timed_event_manager
{
    private:
        std::list<timed_event> events;

    public:
        /**
         * Add an entry to the event queue. Parameters are basically passed
         * through to @ref timed_event::timed_event.
         */
        void add( timed_event_type type, const time_point &when, int faction_id = -1, int strength = -1,
                  const std::string &key = "" );
        /**
         * Add an entry to the event queue. Parameters are basically passed
         * through to @ref timed_event::timed_event.
         */
        void add( timed_event_type type, const time_point &when, int faction_id,
                  const tripoint_abs_ms &where, int strength = -1, const std::string &key = "" );
        void add( timed_event_type type, const time_point &when, int faction_id,
                  const tripoint_abs_ms &where, int strength, const std::string &string_id,
                  const std::string &key = "" );
        void add( timed_event_type type, const time_point &when, int faction_id,
                  const tripoint_abs_ms &where, int strength, const std::string &string_id, submap sr,
                  const std::string &key = "" );
        void add( timed_event_type type, const time_point &when, const tripoint_abs_ms &where,
                  explosion_data expl_data );
        /** An UPDATE_MAPGEN event that runs its update as @p settings say */
        void add_mapgen_update( const time_point &when, const tripoint_abs_ms &where,
                                const std::string &update_id, const std::string &key,
                                const timed_mapgen_update &settings );
        /// @returns Whether at least one element of the given type is queued.
        bool queued( timed_event_type type ) const;
        /// @returns One of the queued events of the given type, or `nullptr`
        /// if no event of that type is queued.
        timed_event *get( timed_event_type type );
        timed_event *get( timed_event_type type, const std::string &key );
        std::list<timed_event> const &get_all() const;
        void set_all( const std::string &key, time_duration time_in_future );
        /// @returns How many queued events have this key.
        int count_keyed( const std::string &key ) const;
        /// Removes the queued events with this key; @returns how many there were.
        int cancel_all( const std::string &key );
        /// Process all queued events, potentially altering the game state and
        /// modifying the event queue.
        void process();
        static void serialize_all( JsonOut &jsout );
        static void unserialize_all( const JsonArray &ja );
};

timed_event_manager &get_timed_events();
bool you_know_where_you_are();

#endif // CATA_SRC_TIMED_EVENT_H
