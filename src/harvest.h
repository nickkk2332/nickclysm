#pragma once
#ifndef CATA_SRC_HARVEST_H
#define CATA_SRC_HARVEST_H

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "translation.h"
#include "type_id.h"

class JsonObject;
class JsonValue;
class butchery_requirements;
class item;
template <typename T> class generic_factory;

enum class butcher_type : int;
enum class creature_size : int;

using butchery_requirements_id = string_id<butchery_requirements>;

// One step of a butchery yield: the amount times `mult`, plus (with `random`) a random amount
// between the amount times random_min and the amount times random_max. Each part is rounded down.
struct harvest_yield {
    double mult = 1.0;
    bool random = false;
    double random_min = 0.0;
    double random_max = 0.0;
    // For action yields: creatures smaller than this give nothing of the drop type.
    std::optional<creature_size> min_size;

    int apply( int amount ) const;
    void deserialize( const JsonValue &jv );
};

// A yield step that applies when the corpse has any of `flags` (any corpse when empty) and the
// butchery is one of `actions` (every action when empty). With `skip`, the drop is not
// harvested at all.
struct harvest_corpse_rule {
    std::vector<flag_id> flags;
    std::vector<butcher_type> actions;
    bool skip = false;
    harvest_yield yield;

    bool applies( butcher_type action, const item &corpse ) const;
    void deserialize( const JsonObject &jo );
};

// Divides the yield by 1 + penalty * (1 - practice of the proficiency, from 0 to 1).
struct harvest_proficiency_penalty {
    proficiency_id proficiency;
    double penalty = 0.0;

    void deserialize( const JsonObject &jo );
};

// Butchering a corpse with drops of this type trains the first of `proficiencies` the
// butcher doesn't have (the last one when they have them all), for the given fraction of the
// butchery time.
struct harvest_proficiency_training {
    std::vector<proficiency_id> proficiencies;
    std::map<butcher_type, double> time_fraction;

    void deserialize( const JsonObject &jo );
};

class harvest_drop_type
{
    public:
        static void load_harvest_drop_types( const JsonObject &jo, const std::string &src );
        static void finalize_all();
        static void reset();
        void load( const JsonObject &jo, std::string_view src );
        static const std::vector<harvest_drop_type> &get_all();

        const harvest_drop_type_id &getId() const {
            return id;
        }
        // Get skills required for harvesting rolls
        const std::vector<skill_id> &get_harvest_skills() const {
            return harvest_skills;
        }
        // Is the associated harvest drop an item group?
        bool is_item_group() const {
            return is_group_;
        }
        // Is the associated harvest drop a single itype?
        bool is_itype() const {
            return !is_group_;
        }
        // Should the associated drop only spawn on dissection?
        bool dissect_only() const {
            return dissect_only_;
        }
        // Message to display for the associated drop when field dressing
        translation field_dress_msg( bool succeeded ) const;
        // Message to display for the associated drop when doing quick/full butchery
        translation butcher_msg( bool succeeded ) const;
        // Message to display when failed to dissect the associated drop
        translation dissect_msg( bool succeeded ) const;

        // The yield of this drop type from a butchery action, or nullptr if the action gives
        // nothing of it.
        const harvest_yield *action_yield( butcher_type action ) const;
        // `weight` reduced for a corpse of damage level `damage_level`, rounded.
        int corpse_damage_yield( int weight, int damage_level ) const;
        // Yield multiplier for a gibbed corpse.
        double gib_yield() const {
            return gib_yield_;
        }
        // Do drops of this type scatter as chunks when the monster is splattered?
        bool survives_splatter() const {
            return survives_splatter_;
        }
        // Does a drop of this type make the butchery action available?
        bool enables_action( butcher_type action ) const;
        const std::vector<harvest_corpse_rule> &corpse_flag_yield() const {
            return corpse_flag_yield_;
        }
        const std::vector<harvest_proficiency_penalty> &proficiency_penalties() const {
            return proficiency_penalties_;
        }
        const std::vector<harvest_proficiency_training> &trains_proficiencies() const {
            return trains_proficiencies_;
        }

        static void check_consistency();

    private:
        harvest_drop_type_id id;
        std::vector<std::pair<harvest_drop_type_id, mod_id>> src;
        bool is_group_ = false;
        bool dissect_only_ = false;
        bool was_loaded = false;
        std::vector<skill_id> harvest_skills;
        std::string msg_fielddress_success;
        std::string msg_fielddress_fail;
        std::string msg_butcher_success;
        std::string msg_butcher_fail;
        std::string msg_dissect_success;
        std::string msg_dissect_fail;
        // Without "action_yield" in JSON: the drop comes at full yield from full butchery,
        // field dressing and dissection, at 0 from dismembering, and not from the rest.
        bool has_action_yield_ = false;
        std::map<butcher_type, harvest_yield> action_yield_;
        // float, as the multipliers were before they moved to JSON, so the rounding stays the same
        std::vector<float> corpse_damage_yield_;
        double gib_yield_ = 1.0;
        bool survives_splatter_ = false;
        std::set<butcher_type> enables_actions_;
        std::vector<harvest_corpse_rule> corpse_flag_yield_;
        std::vector<harvest_proficiency_penalty> proficiency_penalties_;
        std::vector<harvest_proficiency_training> trains_proficiencies_;
        friend class generic_factory<harvest_drop_type>;
        friend struct mod_tracker;
};

// Could be reused for butchery
struct harvest_entry {
    // drop can be either an itype_id or a group id
    std::string drop = "null";
    std::pair<float, float> base_num = { 1.0f, 1.0f };
    // This is multiplied by survival and added to the above
    // TODO: Make it a map: skill->scaling
    std::pair<float, float> scale_num = { 0.0f, 0.0f };

    int max = 1000;
    harvest_drop_type_id type;
    float mass_ratio = 0.00f;

    static harvest_entry load( const JsonObject &jo, const std::string &src );

    std::vector<flag_id> flags;
    std::vector<fault_id> faults;

    // The drop type's rules; the default rules for an entry without a valid type.
    const harvest_drop_type &drop_type() const;

    bool was_loaded = false;
    void load( const JsonObject &jo );
    void deserialize( const JsonObject &jo );

    // only compares mandatory members for reader identity checks
    bool operator==( const harvest_entry &rhs ) const;
};

class harvest_list
{
    public:
        harvest_list();

        itype_id leftovers = itype_id( "ruined_chunks" );

        harvest_id id;
        std::vector<std::pair<harvest_id, mod_id>> src;

        std::string message() const;

        bool is_null() const;

        const std::vector<harvest_entry> &entries() const {
            return entries_;
        }

        bool empty() const {
            return entries().empty();
        }

        bool has_entry_type( const harvest_drop_type_id &type ) const;

        /**
         * Returns a set of cached, translated names of the items this harvest entry could produce.
         * Filled in at finalization and not valid before that stage.
         */
        const std::set<std::string> &names() const {
            return names_;
        }

        const butchery_requirements &get_butchery_requirements() const {
            return butchery_requirements_.obj();
        }

        std::string describe( int at_skill = -1 ) const;

        std::vector<harvest_entry>::const_iterator begin() const;
        std::vector<harvest_entry>::const_iterator end() const;
        std::vector<harvest_entry>::const_reverse_iterator rbegin() const;
        std::vector<harvest_entry>::const_reverse_iterator rend() const;

        /** Fills out the set of cached names. */
        static void finalize_all();
        void finalize();

        /** Check consistency of all loaded harvest data */
        static void check_consistency();
        /** Reset all loaded harvest data */
        static void reset();

        bool was_loaded = false;
        void load( const JsonObject &obj, std::string_view );
        static void load_harvest_list( const JsonObject &jo, const std::string &src );
        static const std::vector<harvest_list> &get_all();

    private:
        std::vector<harvest_entry> entries_;
        std::set<std::string> names_;
        translation message_;
        butchery_requirements_id butchery_requirements_;

};

#endif // CATA_SRC_HARVEST_H
