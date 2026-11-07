#pragma once
#ifndef CATA_SRC_FAULT_H
#define CATA_SRC_FAULT_H

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "calendar.h"
#include "math_parser_diag_value.h"
#include "requirements.h"
#include "translation.h"
#include "type_id.h"
#include "value_ptr.h"
#include "weighted_list.h"

class JsonObject;
class item;
template <typename T> class generic_factory;
template <typename E> struct enum_traits;

namespace sounds
{
enum class sound_t : int;
} // namespace sounds

/**
 * How a fault takes part in the damage a wheel takes from running over sharp items or a
 * trap with vehicle_data "runover_faults" (VEH-J05).
 */
struct fault_runover {
    // Weight to be the first such fault a wheel gets; 0: only reached by escalation
    int weight = 0;
    // Shown to the driver when running something over applies this fault
    translation message;
    // What running something over again does to a wheel with this fault; empty: nothing more
    weighted_int_list<fault_id> escalates_to;

    void deserialize( const JsonObject &jo );
};

/** What a wheel with a fault does each turn its vehicle drives on (VEH-J05) */
struct fault_while_moving {
    translation sound;
    int sound_volume = 0;
    sounds::sound_t sound_type{};
    // One in (wheel durability) chance per turn to become this fault (it replaces this one)
    fault_id escalates_to = fault_id::NULL_ID();
    // Or, without escalates_to, the same chance to take one level of damage
    bool damages_part = false;
    // Shown when the part is damaged; %s is the part name
    translation message;

    void deserialize( const JsonObject &jo );
};

namespace faults
{
void load_fault( const JsonObject &jo, const std::string &src );
void load_fix( const JsonObject &jo, const std::string &src );
void load_group( const JsonObject &jo, const std::string &src );

void reset();
void finalize();
void check_consistency();

std::vector<fault_id> all_of_type( const std::string &type );

const fault_id &random_of_type( const std::string &type );
const fault_id &random_of_type_item_has( const item &it, const std::string &type );
} // namespace faults

enum class fault_severity : int {
    none,
    minor,
    major,
    critical,
    last
};

template<>
struct enum_traits<fault_severity> {
    static constexpr fault_severity last = fault_severity::last;
};

class fault_fix
{
    public:
        fault_fix_id id = fault_fix_id::NULL_ID();
        translation name;
        translation success_msg; // message to print on applying successfully
        time_duration time = 0_seconds;
        std::map<std::string, diag_value> set_variables; // item vars applied to item
        // item vars adjustment(s) applied to item via multiplication;
        std::map<std::string, double> adjust_variables_multiply;
        std::map<skill_id, int> skills; // map of skill_id to required level
        std::set<fault_id> faults_removed; // which faults are removed on applying
        std::set<fault_id> faults_added; // which faults are added on applying
        int mod_damage = 0; // mod_damage with this value is called on item applied to
        int mod_degradation = 0; // mod_degradation with this value is called on item applied to
        std::map<proficiency_id, float>
        time_save_profs; // map of proficiency_id and the time saving for posessing it
        std::map<flag_id, float>
        time_save_flags; // map of flag_id and the time saving for the mendee item posessing it

        const requirement_data &get_requirements() const;

        void finalize();
    private:
        void load( const JsonObject &jo, std::string_view src );
        void check() const;
        bool was_loaded = false; // used by generic_factory
        friend class generic_factory<fault_fix>;
        friend class fault;

        // used to store requirements, since requirement ids are unavailable at the moment of loading
        // transferred into requirements at finalization
        std::vector<std::pair<requirement_id, int>> pending_requirements;

        cata::value_ptr<requirement_data> requirements = cata::make_value<requirement_data>();
};

class fault
{
    public:
        fault_id id = fault_id::NULL_ID();
        std::string name() const;
        std::string type() const; // use a set of types?
        std::string description() const;
        std::string item_prefix() const;
        std::string item_suffix() const;
        std::string message() const;
        std::string color() const;
        fault_severity severity() const;
        double price_mod() const;
        // having this faults adds this much of temporary (will be removed when fault is fixed) degradation
        int degradation_mod() const;
        // Damage applied when fault is applied.
        int instant_damage() const;
        // int is additive (default 0), float is multiplier (default 1)
        std::vector<std::tuple<int, float, damage_type_id>> melee_damage_mod() const;
        // int is additive (default 0), float is multiplier (default 1)
        std::vector<std::tuple<int, float, damage_type_id>> armor_mod() const;
        bool affected_by_degradation() const;
        double encumb_mod_flat() const;
        double encumb_mod_mult() const;
        float contact_area_mod() const;
        float rolling_resistance_mod() const;
        int vehicle_move_penalty_mod() const;
        // Multipliers for a vehicle part with this fault (VEH-J04). An unset engine power or
        // fuel multiplier falls back on the legacy flags REDUCE_ENG_POWER (0.6) and
        // DOUBLE_FUEL_CONSUMPTION (2), so old faults keep working
        double engine_power_mult() const;
        double fuel_consumption_mult() const;
        double exhaust_mult() const;
        float steering_mult() const;
        // Wheel damage from running things over and while driving; null if not set
        const fault_runover *runover() const;
        const fault_while_moving *while_moving() const;
        bool has_flag( const std::string &flag ) const;
        const std::set<fault_id> &get_block_faults() const;

        const std::set<fault_fix_id> &get_fixes() const;
    private:
        void load( const JsonObject &jo, std::string_view );
        void check() const;
        bool was_loaded = false; // used by generic_factory
        friend class generic_factory<fault>;
        friend class fault_fix;

        std::string type_;
        translation name_;
        translation description_;
        translation item_prefix_; // prefix added to affected item's name
        translation item_suffix_;
        translation message_;
        std::string color_;
        fault_severity severity_ = fault_severity::none;
        std::set<fault_fix_id> fixes;
        std::set<std::string> flags;
        std::set<fault_id> block_faults;
        double price_modifier = 1.0;
        int degradation_mod_ = 0;
        int instant_damage_ = 0;
        float contact_area_mod_ = 1.f;
        float rolling_resistance_mod_ = 1.f;
        int vehicle_move_penalty_mod_ = 1.f;
        std::optional<double> engine_power_mult_;
        std::optional<double> fuel_consumption_mult_;
        double exhaust_mult_ = 1.0;
        float steering_mult_ = 1.f;
        std::optional<fault_runover> runover_;
        std::optional<fault_while_moving> while_moving_;
        std::vector<std::tuple<int, float, damage_type_id>> melee_damage_mod_;
        std::vector<std::tuple<int, float, damage_type_id>> armor_mod_;
        int encumbrance_mod_flat_ = 0;
        float encumbrance_mod_mult_ = 1.f;
        // todo add tool_quality_mod_; axe with no handle won't axe
        bool affected_by_degradation_ = false;

};

class fault_group
{
    public:
        fault_group_id id;
        weighted_int_list<fault_id> get_weighted_list() const;

    private:
        void load( const JsonObject &jo, std::string_view );
        bool was_loaded = false;
        friend class generic_factory<fault_group>;
        weighted_int_list<fault_id> fault_weighted_list;
};

#endif // CATA_SRC_FAULT_H
