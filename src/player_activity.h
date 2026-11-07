#pragma once
#ifndef CATA_SRC_PLAYER_ACTIVITY_H
#define CATA_SRC_PLAYER_ACTIVITY_H

#include <climits>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "activity_actor.h"
#include "calendar.h"
#include "clone_ptr.h"
#include "compatibility.h"
#include "coordinates.h"
#include "enums.h"
#include "global_vars.h"
#include "item_location.h"
#include "memory_fast.h"
#include "point.h"
#include "type_id.h"

class Character;
class JsonObject;
class JsonOut;
class activity_end_hooks;
class avatar;
class monster;
class translation;

class player_activity
{
    private:
        activity_id type;

        std::set<distraction_type> ignored_distractions; // NOLINT(cata-serialize)

        bool ignoreQuery = false; // NOLINT(cata-serialize)

    public:
        cata::clone_ptr<activity_actor> actor;

        /** Total number of moves (1/100th of a second) required to complete the activity */
        int moves_total = 0;
        /** The number of moves (1/100th of a second) remaining in this activity before it is complete. */
        int moves_left = calendar::INDEFINITELY_LONG;
        /** Controls whether this activity can be cancelled at all */
        bool interruptable = true;
        /** Controls whether this activity can be cancelled with 'pause' action */
        bool interruptable_with_kb = true;

        // The members in the following block are deprecated, prefer creating a new
        // activity_actor.
        int index = 0;
        /**
         *   An activity specific value.
         *   DO NOT USE FOR ITEM INDEX
        */
        int position = 0;
        std::string name;
        std::vector<item_location> targets;
        std::vector<int> values;
        std::vector<std::string> str_values;
        // The "context" u_assign_activity gave this activity: values for its EOCs
        global_variables::impl_t eoc_context;
        std::vector<tripoint_abs_ms> coords;
        std::unordered_set<tripoint_abs_ms> coord_set;
        std::vector<weak_ptr_fast<monster>> monsters;
        static constexpr const tripoint_abs_ms &invalid_place = tripoint_abs_ms::invalid;
        tripoint_abs_ms placement;
        // ACT_START_ENGINES needs a relative position because the engine might
        // be in a moving vehicle at the time.
        tripoint_rel_ms relative_placement;

        bool no_drink_nearby_for_auto_consume = false; // NOLINT(cata-serialize)
        bool no_food_nearby_for_auto_consume = false; // NOLINT(cata-serialize)
        /** If true, the activity will be auto-resumed next time the player attempts
         *  an identical activity. This value is set dynamically.
         */
        bool auto_resume = false;
        /** Flag that will suppress the relatively expensive fire refueling search process.
         *  Initially assume there is a fire unless the activity proves not to have one.
         */
        bool have_fire = true; // NOLINT(cata-serialize)

        player_activity();
        // This constructor does not work with activities using the new activity_actor system
        // TODO: delete this constructor once migration to the activity_actor system is complete
        explicit player_activity( activity_id, int turns = 0, int Index = -1, int pos = INT_MIN,
                                  const std::string &name_in = "" );
        /**
         * Create a new activity with the given actor
         */
        explicit player_activity( const activity_actor &actor );

        player_activity( player_activity && ) noexcept = default;
        player_activity( const player_activity & ) = default;
        player_activity &operator=( player_activity && ) noexcept( list_is_noexcept ) = default;
        player_activity &operator=( const player_activity & ) = default;

        explicit operator bool() const {
            return !type.is_null();
        }
        bool is_null() const {
            return type.is_null();
        }
        bool is_multi_type() const;
        /** This replaces the former usage `act.type = ACT_NULL` */
        void set_to_null();

        // This makes player_activity's activity type inherit activity_actor's activity type,
        // in order to synchronize both, due to possible variability of actor's activity type
        // allowed via override of activity_actor::get_type()
        void synchronize_type_with_actor();

        const activity_id &id() const {
            return type;
        }
        bool rooted() const;

        // Question to ask when the activity is to be stopped,
        // e.g. "Stop doing something?", already translated.
        std::string get_stop_phrase() const;

        const translation &get_verb() const;

        int get_value( size_t index, int def = 0 ) const;
        std::string get_str_value( size_t index, std::string_view def = {} ) const;

        /**
         * Helper that returns an activity specific progress message.
         */
        std::optional<std::string> get_progress_message( const avatar &u ) const;

        /**
         * If this returns true, the action can be continued without
         * starting from scratch again (see player::backlog). This is only
         * possible if the player start the very same activity (with the same
         * parameters) again.
         */
        bool can_resume() const;

        void serialize( JsonOut &json ) const;
        void deserialize( const JsonObject &data );
        /** Convert from the old enumeration to the new string_id */
        void deserialize_legacy_type( int legacy_type, activity_id &dest );

        /**
         * Perform necessary initialization to start or resume the activity. Must be
         * called whenever a Character starts a new activity.
         * When resuming an activity, do not call activity_actor::start
         */
        void start_or_resume( Character &who, bool resuming );

        /**
         * Performs the activity for a single turn. If the activity is complete
         * at the end of the turn, do_turn also executes whatever actions, if
         * any, are needed to conclude the activity.
         */
        void do_turn( Character &you );

        /**
         * Performs activity-specific cleanup when Character::cancel_activity() is called,
         * and runs the cancel hooks unless they were taken (see take_end_hooks).
         */
        void canceled( Character &who );

        /**
         * Takes this activity's end hooks so they can run once it is gone: after
         * Character::cancel_activity(), or once its result exists. The activity then doesn't
         * run them itself, so an actor that finishes its work some other way than running out
         * of moves takes them, clears the activity and runs them as a completion.
         * Each activity object hands its hooks out once; later calls get hooks that do nothing.
         */
        activity_end_hooks take_end_hooks();
        /** The end hooks take_end_hooks() would hand out, leaving them with the activity. */
        activity_end_hooks peek_end_hooks() const;

        /**
         * Returns true if activities are similar enough that this activity
         * can be resumed instead of starting the other activity.
         */
        bool can_resume_with( const player_activity &other, const Character &who ) const;

        /**
         * Passes new activity_actor to the old one so it can update its parameter when
         * activity is resumed with another one.
         */
        void set_resume_values( const player_activity &other, const Character &who ) ;

        bool is_interruptible() const;
        bool is_interruptible_with_kb() const;
        bool is_distraction_ignored( distraction_type ) const;
        void ignore_distraction( distraction_type );
        void allow_distractions();
        void inherit_distractions( const player_activity & );

        float exertion_level() const;

        bool do_drop_invalid_inventory() const {
            return !actor || actor->do_drop_invalid_inventory();
        }

        std::map<distraction_type, std::string> get_distractions() const;

    private:
        /**
         * Whether this activity object handed out its end hooks. A copy (one pushed to the
         * backlog, say) starts without it, so it runs its own hooks when it ends.
         */
        struct end_hooks_state {
            bool taken = false;
            end_hooks_state() = default;
            end_hooks_state( const end_hooks_state & ) noexcept {}
            end_hooks_state &operator=( const end_hooks_state & ) noexcept {
                taken = false;
                return *this;
            }
            ~end_hooks_state() = default;
        };
        end_hooks_state end_hooks; // NOLINT(cata-serialize)
};

/**
 * The end hooks of an activity (see player_activity::take_end_hooks): running the type's
 * completion_eoc or cancel_eoc, then sending character_finished_activity.
 */
class activity_end_hooks
{
    public:
        /**
         * Runs the hooks for @p who as a completion, or a cancel. Only the first call does.
         * A cancel that left the activity in the backlog to resume (@p resumable) is reported as
         * "interrupted" rather than "canceled".
         */
        void run( Character &who, bool canceled, bool resumable = false );
    private:
        friend class player_activity;
        bool pending = false;
        activity_id type;
        int moves_total = 0;
        int moves_left = 0;
        // the EOCs' beta talker, see activity_actor::hook_target
        item_location target;
        global_variables::impl_t eoc_context;
};

/**
 * While it lives, the next activity a character is assigned takes these values (the "context"
 * of u_assign_activity) as its eoc_context, for its start, do_turn, completion and cancel EOCs.
 */
class activity_eoc_context_scope
{
    public:
        explicit activity_eoc_context_scope( global_variables::impl_t values );
        ~activity_eoc_context_scope();
        activity_eoc_context_scope( const activity_eoc_context_scope & ) = delete;
        activity_eoc_context_scope &operator=( const activity_eoc_context_scope & ) = delete;
        /** The values, for the first activity assigned that asks; none after that */
        static std::optional<global_variables::impl_t> take();
    private:
        std::optional<global_variables::impl_t> previous;
};

#endif // CATA_SRC_PLAYER_ACTIVITY_H
