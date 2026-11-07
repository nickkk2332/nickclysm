#pragma once
#ifndef CATA_SRC_FACTION_CAMP_H
#define CATA_SRC_FACTION_CAMP_H

#include <functional>
#include <list>
#include <map>
#include <optional>
#include <stdint.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "calendar.h"
#include "coords_fwd.h"
#include "memory_fast.h"
#include "mission_companion.h"
#include "translation.h"
#include "type_id.h"
#include "units.h"

template <typename E> struct enum_traits;

namespace catacurses
{
class window;
} // namespace catacurses
class Character;
class basecamp;
class item_location;
class npc;

namespace base_camps
{
enum tab_mode : int;
} // namespace base_camps

enum class farm_ops : int {
    plow = 1,
    plant = 2,
    harvest = 4
};

template<>
struct enum_traits<farm_ops> {
    static constexpr bool is_flag_enum = true;
};

class JsonObject;
class JsonValue;

namespace talk_function
{
void basecamp_mission( npc & );

/// Start a faction camp on the current OM tile
void start_camp( npc & );

void draw_camp_tabs( const catacurses::window &win, base_camps::tab_mode cur_tab,
                     const std::vector<std::vector<mission_entry>> &entries );
std::string name_mission_tabs( const tripoint_abs_omt &omt_pos, const std::string &role_id,
                               const std::string &cur_title, base_camps::tab_mode cur_tab );

/// Returns the OM tiles surrounding the camp, @ref purge removes all tiles that aren't expansions
std::vector<std::pair<std::string, tripoint_abs_omt>> om_building_region(
            const tripoint_abs_omt &omt_pos, int range, bool purge = false );
/// Returns the x and y coordinates of ( omt_tar - omt_pos ), clamped to [-1, 1]
point_rel_omt om_simple_dir( const tripoint_abs_omt &omt_pos, const tripoint_abs_omt &omt_tar );

struct mass_volume {
    units::mass wgt = 0_gram;
    units::volume vol = 0_ml;
    int count = 0;
};
/// Collects items in @ref omt_tgt, each with a @ref chance percent from 0 to 100, returns
/// total mass and volume. Items not taken and liquids stay on the map.
/// @ref take, whether you take the item or count it
mass_volume om_harvest_itm( const shared_ptr_fast<npc> &comp, const tripoint_abs_omt &omt_tgt,
                            int chance = 100, bool take = true );
/// Sends a companion from @ref camp to plant @ref seeds, each a location and a count. The
/// companion carries copies; the chosen counts leave storage only once a companion has set off.
/// Returns the companion, or nullptr if no one went.
shared_ptr_fast<npc> camp_start_planting( basecamp &camp, const mission_id &miss_id,
        const std::list<std::pair<item_location, int>> &seeds, float exertion_level,
        const shared_ptr_fast<npc> &preselected_choice = nullptr );
} // namespace talk_function

enum class risk_diff_level {
    NONE = 0,
    VERY_LOW,
    LOW,
    MEDIUM,
    HIGH,
    VERY_HIGH,
    NUM_RISK_DIFF_LEVELS
};

template<>
struct enum_traits<risk_diff_level> {
    static constexpr risk_diff_level last = risk_diff_level::NUM_RISK_DIFF_LEVELS;
};

/// A camp feature (a blueprint "provides" id, or an upgrade's recipe) a camp mission needs
struct camp_provide_requirement {
    std::string id;
    int amount = 1;

    void deserialize( const JsonValue &jv );
};

/// How well a companion does the work of a camp mission: the sum of its skill levels (with
/// progress) and current stats, each times a weight, plus a constant
struct camp_skill_score {
    std::vector<std::pair<skill_id, double>> skills;
    // "strength", "dexterity", "intelligence" or "perception"
    std::vector<std::pair<std::string, double>> stats;
    double constant = 1.0;

    void deserialize( const JsonObject &jo );
    double evaluate( const Character &who ) const;
};

/// What a camp mission brings back, rolled when its companion returns
struct camp_mission_results {
    // rolled once per successful attempt, unless the season has its own group; null for none (a
    // default string_id is "", not the null id, and would read as a group that isn't defined)
    item_group_id item_group = item_group_id::NULL_ID();
    std::map<season_type, item_group_id> seasonal_item_groups;
    // corpses: each successful attempt picks one of these, weighted by the group's total frequency
    std::vector<mongroup_id> monster_groups;
    // per cycle of a repeatable mission, or in all for one that isn't
    int attempts = 1;
    // an attempt succeeds when skill_score is greater than a roll from 0 to difficulty
    int difficulty = 0;
    // the companion's, before it trains
    camp_skill_score skill_score;

    void deserialize( const JsonObject &jo );
    /// the item group rolled in @ref season, null if there is none
    item_group_id item_group_in( season_type season ) const;
    bool empty() const;
};

/// A chance of a random encounter on a camp mission, rolled when the companion returns
struct camp_mission_encounter {
    // one in this many returns has an encounter; 0 never does
    int one_in = 0;
    int favor = 0;
    int threat = 0;
    // what the companion was doing, "While %s, ..."
    translation task;

    void deserialize( const JsonObject &jo );
};

/// What a camp mission trains when its companion comes back
struct camp_mission_training {
    // a skill id, a companion_skill_practice group of the skills, or "combat"
    std::string skill;
    int difficulty = 1;

    void deserialize( const JsonObject &jo );
};

class faction_mission
{
    public:
        bool was_loaded = false;
        faction_mission_id id;
        translation name;
        translation description;
        // the skill companions are ranked by when one is chosen, and at least skill_level of it
        skill_id skill_used = skill_id::NULL_ID();
        int skill_level = 0;
        risk_diff_level difficulty = risk_diff_level::NUM_RISK_DIFF_LEVELS;
        risk_diff_level risk = risk_diff_level::NUM_RISK_DIFF_LEVELS;
        // the exertion the mission is fed for; 0 if not given (fed as NO_EXERCISE, not displayed)
        float activity_level = 0.0f;
        translation time;
        uint16_t positions = 1;
        translation items_label;
        std::vector<translation> items_possibilities;
        std::vector<translation> effects;
        translation footer;

        // the code that runs the mission: Camp_Generic runs it from this data
        mission_kind behavior = Camp_Generic;
        // the expansion the mission is offered at must provide each of these, at least this much
        std::vector<camp_provide_requirement> required_provides;
        // how long the mission takes (generic and the fixed-time behaviors)
        std::optional<time_duration> duration;
        // the companion works in cycles of duration until called back, and brings back results
        // for each cycle (generic only)
        bool repeatable = false;
        // menu texts: the entry that sends a companion, the one that brings it back, and the
        // header of the list of companions out on it
        translation action_name;
        translation return_name;
        translation progress;
        // popups when a companion sets off ("<name> departs ...") and comes back
        translation depart_message;
        translation return_message;
        std::optional<camp_mission_training> train;
        camp_mission_results results;
        std::optional<camp_mission_encounter> encounter;
        // run with the companion as alpha and the avatar as beta
        std::vector<effect_on_condition_id> start_eocs;
        std::vector<effect_on_condition_id> return_eocs;

        static void load_faction_missions( const JsonObject &jo, const std::string &src );
        static void reset();
        static void finalize_all();
        static void check_consistency();
        static const std::vector<faction_mission> &get_all();
        void load( const JsonObject &jo, const std::string_view &src );
        void check() const;
        std::string describe( int npc_count = 0,
                              const std::function<std::vector<std::string>()> &get_items_possibilities = nullptr,
                              const std::function<std::vector<std::string>()> &get_description = nullptr ) const;

        /// The exertion the mission's companions are fed for
        float exertion() const;
        /// The skills (and minimum levels) a companion is chosen by
        std::map<skill_id, int> companion_skills() const;
        /// The id a companion sent on this mission at @ref dir carries
        mission_id mission_at( const point_rel_omt &dir ) const;
        /// Offered only at the camp's own tile, with no direction prefix in the menu
        bool base_only() const;
        /// The texts above, or what stands in for one not given
        std::string action_text() const;
        std::string return_name_text() const;
        std::string progress_text() const;
        std::string depart_text() const;
        std::string return_text() const;
        /// The behaviour a JSON "behavior" name selects
        static std::optional<mission_kind> behavior_of( const std::string &name );
        static std::string behavior_name( mission_kind kind );
};

#endif // CATA_SRC_FACTION_CAMP_H
