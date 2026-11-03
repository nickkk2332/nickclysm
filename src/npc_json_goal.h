#pragma once
#ifndef CATA_SRC_NPC_JSON_GOAL_H
#define CATA_SRC_NPC_JSON_GOAL_H

// Goals defined in JSON on a behavior leaf ("goal": { "id", "on_start", ... }), what runs them
// while an NPC is committed to one, and what EOCs and the debug view read about an NPC's goal.
// See doc/JSON/NPC_BEHAVIOR.md.

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "calendar.h"
#include "npc_decision_category.h"
#include "type_id.h"

class JsonObject;
class npc;
struct const_dialogue;
struct npc_challenger;
struct npc_pursuit;

namespace behavior
{
class node_t;
} // namespace behavior

// why a JSON goal ended, passed to its end hooks as the context value "reason"
namespace npc_goal_reason
{
constexpr const char *done = "done";
constexpr const char *timeout = "timeout";
constexpr const char *interrupted = "interrupted";
} // namespace npc_goal_reason

struct npc_json_goal {
    std::string id;
    // the leaf that defines it; while the goal is committed, its score is this node's
    string_id<behavior::node_t> node;
    npc_decision_category_id category;
    std::vector<effect_on_condition_id> on_start;
    std::vector<effect_on_condition_id> on_turn;
    std::vector<effect_on_condition_id> on_end;
    std::vector<effect_on_condition_id> on_interrupt;
    std::vector<effect_on_condition_id> on_timeout;
    // ends the goal as done once true; empty: never
    std::function<bool( const_dialogue const & )> until;
    std::optional<time_duration> timeout;
};

// a category given to a goal, and by what
struct npc_goal_claim {
    std::string goal;
    npc_decision_category_id category;
    // "behavior <node id>" or "npc_decision_category <id>"
    std::string source;
};

// Errors in these claims together with the categories' "goals" lists: an unknown category, a
// goal given two categories.
std::vector<std::string> check_goal_claims( const std::vector<npc_goal_claim> &node_claims );

// The JSON goals of the loaded behavior nodes
class npc_goal_registry
{
    public:
        // The "category" and an inline "goal" of behavior node `node`.  With a goal object it
        // registers the goal, sets goal_id to its id and returns true; with none (a plain goal
        // string, or no goal) it returns false and leaves goal_id alone.  Throws on a goal that
        // takes a built-in goal's id, has no category, or another node defined already.
        bool load_node( const JsonObject &jo, const string_id<behavior::node_t> &node,
                        std::string_view src, std::string &goal_id );
        const npc_json_goal *find( std::string_view id ) const;
        const std::map<std::string, npc_json_goal, std::less<>> &all() const {
            return goals_;
        }
        const std::vector<npc_goal_claim> &claims() const {
            return claims_;
        }
        // errors in the categories given by the nodes, with the categories' "goals" lists
        std::vector<std::string> check() const;
        void clear();

    private:
        std::map<std::string, npc_json_goal, std::less<>> goals_;
        std::vector<npc_goal_claim> claims_;
};

namespace npc_json_goals
{
// the goals of the loaded data
npc_goal_registry &registry();
const npc_json_goal *find( std::string_view id );
void reset();
} // namespace npc_json_goals

// the tree npc::move() walks: npc_decision, or the one a test put in its place
const behavior::node_t &npc_decision_tree();
// for tests: npc::move() walks this tree instead; nullopt goes back to npc_decision
void set_npc_decision_tree_for_test( const std::optional<string_id<behavior::node_t>> &root );

// Why a challenger does or doesn't take over from the goal the NPC pursues
// (should_interrupt() gives the answer alone).
struct npc_interrupt_verdict {
    bool interrupt = false;
    const char *why = "";
};
npc_interrupt_verdict judge_interrupt( const npc_pursuit &current,
                                       const npc_challenger &challenger );

// What JSON goals do to an NPC; a friend of npc
class npc_goal_runtime
{
    public:
        enum class turn_result : int {
            // walk the goal's walk
            walk,
            // go on with the activity
            activity,
            // a task with steps left to take up (a stashed activity, one at a destination): the
            // usual activity code takes it from here
            task,
            // its EOCs spent moves already
            spent,
            // nothing to do: wait out the turn
            wait,
            // the goal ended in its own EOCs
            ended
        };

        // the goal the NPC is committed to: a built-in one, a JSON one (one loaded from a save
        // too), or "idle"
        static const std::string &current_goal( const npc &guy );
        // turns since the current goal started; 0 for idle
        static int turns_on_goal( const npc &guy );

        // before the committed goal is judged: a JSON goal loaded from a save is the commitment
        // again; one that something else replaced ends as interrupted
        static void sync( npc &guy );
        // "done" once until holds, "timeout" once its time is up, else nullopt
        static std::optional<std::string> finish_reason( const npc &guy );
        // ends the NPC's JSON goal: drops the goal's walk, runs on_timeout or on_interrupt for
        // that reason, then on_end; leaves the commitment to the caller
        static void end( npc &guy, const std::string &reason );
        // a turn on `goal`: starts it if it's new, runs on_turn once a turn, says what to do
        static turn_result take_turn( npc &guy, const npc_json_goal &goal, int moves_at_decision );
        // the NPC's walk was started by its JSON goal's EOCs (it carries the goal's goal_id)
        static bool owns_walk( const npc &guy );
        // the goal's score now: its node's, or 0 when the node doesn't apply
        static float score( const npc &guy, const npc_json_goal &goal );
        // A danger reaction of category `reaction` (a fight, a flight, a sound) is about to
        // take the NPC's move.  True when a JSON goal of a category at or above it keeps the
        // NPC instead: the one it's on, or one the tree picks now.  A JSON goal below it ends
        // as interrupted.
        static bool holds_against( npc &guy, const decision_category &reaction );
        // the NPC is on a JSON goal of `cat` or of a category before it
        static bool outranks( const npc &guy, const decision_category &cat );
        // sends npc_goal_changed when the current goal isn't the one last reported
        static void report_change( npc &guy );
};

#endif // CATA_SRC_NPC_JSON_GOAL_H
