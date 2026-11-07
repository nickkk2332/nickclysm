#pragma once
#ifndef CATA_SRC_SCENARIO_H
#define CATA_SRC_SCENARIO_H

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "calendar.h"
#include "coordinates.h"
#include "point.h"
#include "ret_val.h"
#include "translation.h"
#include "type_id.h"
#include "weighted_list.h"

class JsonObject;
class profession;
template<typename T>
class generic_factory;

/** The NPC a scenario starts next to the player (unless it has the LONE_START flag) */
struct scenario_starting_npc {
    // npc classes to pick from, by weight; empty: NC_DOCTOR or a randomly made character, 50/50
    weighted_int_list<npc_class_id> classes;
    // The first talk topic; empty: TALK_SHELTER
    std::string chat;
    // Run with the NPC as alpha (u_) and the avatar as beta (npc_)
    std::vector<effect_on_condition_id> eocs;

    void deserialize( const JsonObject &jo );
};

class scenario
{
    private:
        friend class string_id<scenario>;
        friend class generic_factory<scenario>;
        friend struct mod_tracker;
        string_id<scenario> id;
        bool was_loaded = false;
        translation _name_male;
        translation _name_female;
        translation _description_male;
        translation _description_female;
        translation _start_name;

        bool blacklist = false; // If true, professions is a blacklist.
        bool extra_professions = false; // If true, professions add to default professions.
        std::vector<string_id<profession>> professions; // as specified in JSON, verbatim

        // White/blacklist of hobbies that can be selected with this scenario
        std::set<string_id<profession>> hobby_exclusion;
        bool hobbies_whitelist = true;

        /**
         * @ref permitted_professions populates this vector on the first call, which takes
         * a bit of work. On subsequent calls, this vector is returned.
        */
        mutable std::vector<string_id<profession>> cached_permitted_professions;
        mutable std::vector<string_id<profession>> cached_permitted_hobbies;

        std::set<trait_id> _allowed_traits;
        std::set<trait_id> _forced_traits;
        std::set<trait_id> _forbidden_traits;
        std::vector<start_location_id> _allowed_locs;
        point_rel_om origin_offset = point_rel_om::zero;
        int _point_cost = 0;
        std::set<std::string> flags; // flags for some special properties of the scenario
        // The profession group of hobbies new characters start with, unless their profession
        // names its own: unset, adult_basic_background; an empty id, none (PROG-J01)
        std::optional<profession_group_id> _default_backgrounds;
        map_extra_id _map_extra;
        std::vector<mission_type_id> _missions;
        std::vector<effect_on_condition_id> _eoc;

        // does this scenario require a specific achiement to unlock
        std::optional<achievement_id> _requirement;
        // does this scenario require the requirement even when metaprogression is disabled?
        bool hard_requirement = false;

        bool reveal_locale = true;
        int distance_initial_visibility = 0;

        time_point _default_start_of_cataclysm;
        time_point _default_start_of_game;

        time_point _start_of_cataclysm;
        time_point _start_of_game;

        vproto_id _starting_vehicle = vproto_id::NULL_ID();

        std::vector<std::pair<mongroup_id, float>> _surround_groups;

        std::optional<scenario_starting_npc> _starting_npc;

        void load( const JsonObject &jo, std::string_view src );
        bool scenario_traits_conflict_with_profession_traits( const profession &p ) const;

    public:
        //these three aren't meant for external use, but had to be made public regardless
        scenario();
        static void load_scenario( const JsonObject &jo, const std::string &src );

        // these should be the only ways used to get at scenario
        static const scenario *generic(); // points to the generic, default profession
        // return a random scenario, weighted for use w/ random character creation
        static const scenario *weighted_random();
        static const std::vector<scenario> &get_all();

        // clear scenario map, every scenario pointer becomes invalid!
        static void reset();

        static void finalize_all();
        static void check_all();

        /** Check that item definitions are valid */
        void check() const;

        const string_id<scenario> &ident() const;
        std::string gender_appropriate_name( bool male ) const;
        std::string description( bool male ) const;
        start_location_id start_location() const;
        start_location_id random_start_location() const;
        std::string start_name() const;
        int start_location_count() const;
        int start_location_targets_count() const;

        std::optional<achievement_id> get_requirement() const;

        bool has_hard_requirement() const;
        bool get_reveal_locale() const;
        int get_distance_initial_visibility() const;

        void normalize_calendar() const;
        void reset_calendar() const;

        time_point start_of_cataclysm() const;
        time_point start_of_game() const;
        void change_start_of_cataclysm( const time_point &t ) const;
        void change_start_of_game( const time_point &t ) const;

        vproto_id vehicle() const;

        const profession *weighted_random_profession( bool is_npc = false ) const;
        std::vector<string_id<profession>> permitted_professions( bool is_npc = false ) const;
        std::vector<string_id<profession>> permitted_hobbies( bool is_npc = false ) const;

        bool traitquery( const trait_id &trait ) const;
        std::set<trait_id> get_locked_traits() const;
        bool is_locked_trait( const trait_id &trait ) const;
        bool is_forbidden_trait( const trait_id &trait ) const;

        bool allowed_start( const start_location_id &loc ) const;
        signed int point_cost() const;
        bool has_map_extra() const;
        const map_extra_id &get_map_extra() const;
        const point_rel_om &get_origin_offset() const;
        /**
         * Returns "All", "Limited", or "Almost all" (translated)
         * This is used by newcharacter.cpp
        */
        std::string prof_count_str() const;

        // Is this scenario blacklisted?
        bool scen_is_blacklisted() const;

        /** Such as a seasonal start, fiery start, surrounded start, etc. */
        bool has_flag( const std::string &flag ) const;
        /** The JSON's "default_backgrounds", if it has one (PROG-J01) */
        const std::optional<profession_group_id> &default_backgrounds() const;

        /**
         * Do you have the necessary achievement state
         */
        ret_val<void> can_pick() const;

        /**
         * Do you have the points to afford swapping to this scenario
         */
        ret_val<void> can_afford( const scenario &current_scenario, int points ) const;

        const std::vector<mission_type_id> &missions() const;
        const std::vector<effect_on_condition_id> &eoc() const;
        const std::vector<std::pair<mongroup_id, float>> &surround_groups() const;
        /** The scenario's "starting_npc", if it has one */
        const std::optional<scenario_starting_npc> &starting_npc() const;

        std::vector<std::pair<string_id<scenario>, mod_id>> src;
};

struct scenario_sorter {
    bool male = false;
    bool cities_enabled = false;
    /** @related player */
    bool operator()( const scenario *a, const scenario *b ) const;
};

// SCENARIO_BLACKLIST: every "blacklist" adds its scenarios to the hidden ones, and the last
// "whitelist" hides every scenario it doesn't list, whatever a blacklist says
struct scen_blacklist {
    // The scenarios hidden, worked out by finalize() from the lists below
    std::set<string_id<scenario>> scenarios;
    // Whether a whitelist is in force
    bool whitelist = false;
    // Every blacklist's scenarios
    std::set<string_id<scenario>> blacklisted;
    // The last whitelist's scenarios
    std::set<string_id<scenario>> whitelisted;

    static void load_scen_blacklist( const JsonObject &jo, std::string_view src );
    void load( const JsonObject &jo, std::string_view );
    void finalize();
};

void reset_scenarios_blacklist();

const scenario *get_scenario();
void set_scenario( const scenario *new_scenario );

#endif // CATA_SRC_SCENARIO_H
