#pragma once
#ifndef CATA_SRC_TRAP_H
#define CATA_SRC_TRAP_H

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "color.h"
#include "coords_fwd.h"
#include "flat_set.h"
#include "magic.h"
#include "translation.h"
#include "type_id.h"
#include "units.h"

class Character;
class Creature;
class JsonObject;
class JsonValue;
class item;
class map;
struct const_dialogue;

namespace trapfunc
{
// p is the point where the trap is (not where the creature is)
// creature is the creature that triggered the trap,
// item is the item that triggered the trap,
// creature and item can be nullptr.
bool none( const tripoint_bub_ms &, Creature *, item * );
bool bubble( const tripoint_bub_ms &p, Creature *c, item *i );
bool glass( const tripoint_bub_ms &p, Creature *c, item *i );
bool cot( const tripoint_bub_ms &p, Creature *c, item *i );
bool beartrap( const tripoint_bub_ms &p, Creature *c, item *i );
bool snare_light( const tripoint_bub_ms &p, Creature *c, item *i );
bool snare_heavy( const tripoint_bub_ms &p, Creature *c, item *i );
bool snare_species( const tripoint_bub_ms &p, Creature *critter, item *trap_item );
bool board( const tripoint_bub_ms &p, Creature *c, item *i );
bool caltrops( const tripoint_bub_ms &p, Creature *c, item *i );
bool caltrops_glass( const tripoint_bub_ms &p, Creature *c, item *i );
bool eocs( const tripoint_bub_ms &p, Creature *critter, item * );
bool tripwire( const tripoint_bub_ms &p, Creature *c, item *i );
bool crossbow( const tripoint_bub_ms &p, Creature *c, item *i );
bool shotgun( const tripoint_bub_ms &p, Creature *c, item *i );
bool blade( const tripoint_bub_ms &p, Creature *c, item *i );
bool landmine( const tripoint_bub_ms &p, Creature *c, item *i );
bool goo( const tripoint_bub_ms &p, Creature *c, item *i );
bool dissector( const tripoint_bub_ms &p, Creature *c, item *i );
bool sinkhole( const tripoint_bub_ms &p, Creature *c, item *i );
bool pit( const tripoint_bub_ms &p, Creature *c, item *i );
bool pit_spikes( const tripoint_bub_ms &p, Creature *c, item *i );
bool pit_glass( const tripoint_bub_ms &p, Creature *c, item *i );
bool lava( const tripoint_bub_ms &p, Creature *c, item *i );
bool boobytrap( const tripoint_bub_ms &p, Creature *c, item *i );
bool temple_flood( const tripoint_bub_ms &p, Creature *c, item *i );
bool temple_toggle( const tripoint_bub_ms &p, Creature *c, item *i );
bool glow( const tripoint_bub_ms &p, Creature *c, item *i );
bool hum( const tripoint_bub_ms &p, Creature *c, item *i );
bool shadow( const tripoint_bub_ms &p, Creature *c, item *i );
bool map_regen( const tripoint_bub_ms &p, Creature *c, item *i );
bool drain( const tripoint_bub_ms &p, Creature *c, item *i );
bool snake( const tripoint_bub_ms &p, Creature *c, item *i );
bool thick_ice( const tripoint_bub_ms &p, Creature *c, item *i );
bool thin_ice( const tripoint_bub_ms &p, Creature *c, item *i );
bool cast_spell( const tripoint_bub_ms &p, Creature *critter, item * );
bool dummy_trap( const tripoint_bub_ms &p, Creature *critter, item * );
} // namespace trapfunc

struct vehicle_handle_trap_data {
    bool remove_trap = false;
    bool do_explosion = false;
    bool is_falling = false;
    int chance = 100;
    int damage = 0;
    int shrapnel = 0;
    int sound_volume = 0;
    translation sound;
    std::string sound_type;
    std::string sound_variant;
    // the double represents the count or chance to spawn.
    std::vector<std::pair<itype_id, double>> spawn_items;
    trap_str_id set_trap = trap_str_id::NULL_ID();
    // The wheel moves on along the runover chain of its faults, as when running over a
    // sharp item (VEH-J05)
    bool runover_faults = false;
    // Faults set on the wheel, those it can have
    std::vector<fault_id> wheel_faults;
    // Run once the vehicles have moved, with the vehicle as alpha (u) and its driver, if any, as
    // beta (npc)
    std::vector<effect_on_condition_id> eocs;

    void deserialize( const JsonObject &jo );
};

using trap_function = std::function<bool( const tripoint_bub_ms &, Creature *, item * )>;

// One damage a built-in trap action deals when it is set off (JSON "trigger_damage"): "amount",
// or a random amount from "min" to "max", of a damage type.
struct trap_damage_roll {
    damage_type_id type;
    int min = 0;
    int max = 0;

    void deserialize( const JsonObject &jo );
};

// JSON numbers that the built-in actions "beartrap", "board", "caltrops" and "caltrops_glass"
// use in place of their own (TERR-J08). An unset value keeps the action's.
struct trap_trigger_tuning {
    // "trigger_damage": replaces the action's whole damage
    std::vector<trap_damage_roll> damage;
    // "tetanus_chance": one in this many cutting hits gives tetanus; 0 never does
    std::optional<int> tetanus_chance;
    // "tetanus_chance_resistant": the same for a character with infection resistance
    std::optional<int> tetanus_chance_resistant;
    // "break_weight": a creature this heavy always breaks the trap, a lighter one may
    std::optional<units::mass> break_weight;
    // "self_disarm_chance": percent chance that a trap that didn't break disarms itself
    std::optional<int> self_disarm_chance;
};

/**
 * Some traps aren't actually traps in the usual sense of the word. We use traps to implement
 * funnels (the main map keeps a list of traps and we iterate over that list during rain
 * in order to fill the containers with water).
 * Use @ref is_benign to check for that kind of "non-dangerous" traps. Traps that are not benign
 * are considered dangerous.
 *
 * Some traps are always invisible. They are never revealed as such to the player.
 * They can still be triggered. Use @ref is_always_invisible to check for that.
 *
 * Traps names can be empty. This usually applies to always invisible traps.
 *
 * Some traps are always revealed to the player (e.g. funnels). Other traps can be spotted when
 * the player is close (also needs perception stat).
 *
 * Use @ref map::can_see_trap_at or @ref trap::can_see to check whether a creature knows about a
 * given trap. Monsters / NPCs should base their behavior on that information and not on a simple
 * check for any trap being there (e.g. don't use `map::tr_at(...).is_null()` - that would reveal
 * the existence of the trap).
 */
struct trap {
        trap_str_id id;
        std::vector<std::pair<trap_str_id, mod_id>> src;
        trap_id loadid;

        bool was_loaded = false;

        int sym = 0;
        nc_color color;
    private:
        /**
         * How easy it is to spot the trap. Smaller values means it's easier to spot.
         * 1 to ??, affects detection
         */
        // @TODO it can be negative (?)
        // @TODO Add checks for it having proper values
        // @TODO check usage in combination with is_always_invisible
        int visibility = 1;
        // 0 to ??, affects avoidance
        int avoidance = 0;
        /*
         * This is used when disarming the trap. A value of 0 means disarming will always work
         * (e.g. for funnels), a values of 99 means it can not be disarmed at all. Smaller values
         * makes it easier to disarm the trap.
         */
        int difficulty = 0;
        // 0 to ??, trap radius
        int trap_radius = 0;
        bool benign = false;
        bool always_invisible = false;
        update_mapgen_id map_regen;
        trap_function act;
        // the JSON "action" that act runs
        std::string action_name;
        translation name_;

        std::optional<translation> memorial_male;
        std::optional<translation> memorial_female;

        std::optional<translation> trigger_message_u;
        std::optional<translation> trigger_message_npc;

        cata::flat_set<flag_id> _flags;

        /**
         * If an item with this weight or more is thrown onto the trap, it triggers.
         */
        units::mass trigger_weight = 500_gram;
        /**
         * Determines how much sound is needed to trigger the trap. Defined as {min,max}.
         */
        std::pair<int, int> sound_threshold = {0, 0};
        int funnel_radius_mm = 0;

        struct comp {
            itype_id item_type;
            int quantity;
            int charges;
            void deserialize( const JsonValue &jv );
        };
        // For disassembly?
        std::vector<comp> components;
    public:
        std::optional<itype_id> trap_item_type;
        // data required for trapfunc::spell()
        fake_spell spell_data;
        std::vector<effect_on_condition_id> eocs;
        // "item_eocs": what an "eocs" trap runs when an item thrown or dropped onto it, or a
        // sound, sets it off (there is no alpha talker)
        std::vector<effect_on_condition_id> item_eocs;
        // "disarm_eocs" and "disarm_fail_eocs": run after the trap is disarmed or taken down,
        // or after a failed attempt, with the disarmer as alpha
        std::vector<effect_on_condition_id> disarm_eocs;
        std::vector<effect_on_condition_id> disarm_fail_eocs;
        // "disarm_condition": the disarmer (alpha) must meet it to try; else
        // "disarm_condition_msg" is shown
        std::function<bool( const_dialogue const & )> disarm_condition;
        translation disarm_condition_msg;
        // "easy_take_down_condition": a disarmer (alpha) who meets it takes the trap down
        // without a roll, as a trap with no avoidance and no difficulty
        std::function<bool( const_dialogue const & )> easy_take_down_condition;
        trap_trigger_tuning tuning;
        int comfort = 0;
        units::temperature_delta floor_bedding_warmth = 0_C_delta;
        vehicle_handle_trap_data vehicle_data;
        std::string name() const;
        /**
         * There are special always invisible traps. See player::search_surroundings
         */
        bool is_always_invisible() const {
            return always_invisible;
        }

        bool operator==( const trap_id &id ) const {
            return loadid == id;
        }
        bool operator!=( const trap_id &id ) const {
            return loadid != id;
        }

        bool has_flag( const flag_id &flag ) const {
            return _flags.count( flag );
        }

        bool has_memorial_msg() const {
            return memorial_male && memorial_female;
        }

        std::string memorial_msg( bool male ) const {
            if( male ) {
                return memorial_male->translated();
            }
            return memorial_female->translated();
        }

        /**
         * Called when the player examines a tile. This is supposed to handled
         * all kind of interaction of the player with the trap, including removal.
         * It also handles visibility of the trap, and it does nothing when
         * called on the null trap.
         */
        // Implemented for historical reasons in iexamine.cpp
        void examine( const tripoint_bub_ms &examp ) const;

        update_mapgen_id map_regen_target() const;

        /**
         * Whether triggering the trap can be avoid (if greater than 0) and if so, this is
         * compared to dodge skill (with some adjustments). Smaller values means it's easier
         * to dodge.
         */
        int get_avoidance() const {
            return avoidance;
        }
        /**
         * If true, this is not really a trap and there won't be any safety queries before stepping
         * onto it (e.g. for funnels).
         */
        bool is_benign() const {
            return benign;
        }
        /**
         * @return True for traps that can simply be taken down without any skill check or similar.
         * This usually applies to traps like funnels, rollmat.
         */
        bool easy_take_down() const;
        // Whether who may try to disarm the trap at pos ("disarm_condition"), and whether
        // they take it down without a roll (easy_take_down or "easy_take_down_condition")
        bool can_try_disarm( Character &who, const tripoint_bub_ms &pos ) const;
        bool easy_take_down( Character &who, const tripoint_bub_ms &pos ) const;
        /**
         * Runs eocs with who as alpha (none when null) and the context values "trap" (this
         * trap's id) and "trap_location" (absolute).
         */
        void run_eocs( const std::vector<effect_on_condition_id> &list, Creature *who,
                       const tripoint_abs_ms &pos ) const;
        // The items in "drops", spawned at p
        void spawn_drops( map &m, const tripoint_bub_ms &p ) const;

        bool is_trivial_to_spot() const;

        void set_trap_data( itype_id trap_item_type_id );
        /**
         * Some traps are part of the terrain (e.g. pits) and can therefore not be disarmed
         * via the usual mechanics. They can be "disarmed" by changing the terrain they are part of.
         */
        bool can_not_be_disarmed() const;

        /**
         * Whether this kind of trap will be detected by ground sonar (e.g. via the bionic).
         */
        bool detected_by_ground_sonar() const;
        /**
         * Whether this kind of trap will be detected by regular sonar ( it is not buried and it's a solid object ).
         */
        bool detected_by_echolocation() const;
        /** Player has not yet seen the trap and returns the variable chance, at this moment,
         of whether the trap is seen or not. */
        bool detect_trap( const tripoint_bub_ms &pos, const Character &p ) const;
        /**
         * Can player/npc p see this kind of trap, either by their memory (they known there is
         * the trap) or by the visibility of the trap (the trap is not hidden at all)?
         */
        bool can_see( const tripoint_bub_ms &pos, const Character &p ) const;

        bool has_trigger_msg() const {
            return trigger_message_u && trigger_message_npc;
        }
        /**
        * Prints a trap-specific trigger message when player steps on it.
        */
        std::string get_trigger_message_u() const {
            return trigger_message_u->translated();
        }
        /**
        * Prints a trap-specific trigger message when NPC or a monster steps on it.
        */
        std::string get_trigger_message_npc() const {
            return trigger_message_npc->translated();
        }
    private:
        /**
         * Trigger trap effects.
         * @param creature The creature that triggered the trap, it does not necessarily have to
         * be on the place of the trap (traps can be triggered from adjacent, e.g. when disarming
         * them). This can also be a null pointer if the trap has been triggered by some thrown
         * item (which must have the @ref trigger_weight).
         * @param pos The location of the trap in the main map.
         * @param item The item that triggered the trap
         */
        // Don't call from outside this class. Add a wrapper like the ones below instead.
        void trigger( const tripoint_bub_ms &pos, Creature *creature, item *item ) const;
        // Runs "item_eocs" when an item (or a sound, it is null then) set the trap off.
        // Whether there were any.
        bool trigger_item_eocs( const tripoint_bub_ms &pos, const item *it ) const;
    public:
        /*@{*/
        /**
         * This applies the effects of the trap to the world and
         * possibly to the triggering object (creature, item).
         *
         * The function assumes the
         * caller has already checked whether the trap should be activated
         * (e.g. the creature has had a chance to avoid the trap, but it failed).
         */
        void trigger( const tripoint_bub_ms &pos, Creature &creature ) const;
        void trigger( const tripoint_bub_ms &pos, item &item ) const;
        /*@}*/

        void trigger( const tripoint_bub_ms &pos ) const;

        /**
         * If the given item is throw onto the trap, does it trigger the trap?
         */
        bool triggered_by_item( const item &itm ) const;

        /**
         * Called when a trap at the given point in the map has been disarmed.
         * It should spawn trap items (if any) and remove the trap from the map via
         * @ref map::remove_trap.
         */
        void on_disarmed( map &m, const tripoint_bub_ms &p ) const;
        /**
         * This is used when defining area this trap occupies. A value of 0 means trap occupies exactly 1 tile.
         */
        int get_trap_radius() const {
            return trap_radius;
        }
        /**
         * Whether this is the null-traps, aka no trap at all.
         */
        bool is_null() const;
        /**
         * Loads this specific trap.
         */
        void load( const JsonObject &jo, std::string_view src );

        std::string debug_describe() const;

        /*@{*/
        /**
         * @name Funnels
         *
         * Traps can act as funnels, for this they need a @ref trap::funnel_radius_mm > 0.
         * Funnels are usual not hidden at all (@ref trap::visibility == 0), are @ref trap::benign and can
         * be picked up easily (@ref trap::difficulty == 0).
         * The funnel filling is handled in weather.cpp. is_funnel is used the check whether the
         * funnel specific code should be run for this trap.
         */
        bool is_funnel() const;
        double funnel_turns_per_charge( double rain_depth_mm_per_hour ) const;
        /**
         * Returns all trap objects that are actually funnels (is_funnel returns true for all
         * of them).
         */
        static const std::vector<const trap *> &get_funnels();
        /*@}*/

        /*
         * Can the trap be triggered by sounds?
         */
        bool has_sound_trigger() const;
        static const std::vector<const trap *> &get_sound_triggered_traps();
        bool triggered_by_sound( int vol, int dist ) const;

        /*@{*/
        /**
         * @name Initialization
         *
         * Those functions are used by the @ref DynamicDataLoader, see there.
         */
        /**
         * Loads the trap and adds it to the trapmap, and the traplist.
         * @throw JsonError if the json is invalid as usual.
         */
        static void load_trap( const JsonObject &jo, const std::string &src );
        /**
         * Releases the loaded trap objects in trapmap and traplist.
         */
        static void reset();
        /**
         * Stores the actual @ref loadid of the loaded traps in the global tr_* variables.
         * It also sets the trap ids of the terrain types that have built-in traps.
         * Must be called after all traps have been loaded.
         */
        void finalize();
        static void finalize_all();
        /**
         * Checks internal consistency (reference to other things like item ids etc.)
         */
        static void check_consistency();
        /*@}*/
        static size_t count();
};

const trap_function &trap_function_from_string( const std::string &function_name );

extern trap_id tr_null;

#endif // CATA_SRC_TRAP_H
