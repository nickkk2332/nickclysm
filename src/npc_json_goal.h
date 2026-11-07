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
#include <utility>
#include <vector>

#include "calendar.h"
#include "npc_decision_category.h"
#include "type_id.h"

class Creature;
class JsonObject;
class npc;
struct const_dialogue;
struct npc_challenger;
struct npc_pursuit;

namespace behavior
{
class node_t;
class oracle_t;
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

// the loaded categories' "goals" lists, as claims
std::vector<npc_goal_claim> category_goal_claims();
// Errors in the categories' "goals" lists and the nodes' claims together: an unknown category, a
// goal given two categories, a built-in goal given none.
std::vector<std::string> check_goal_claims( const std::vector<npc_goal_claim> &list_claims,
        const std::vector<npc_goal_claim> &node_claims );

// The JSON goals of the loaded behavior nodes
class npc_goal_registry
{
    public:
        // The "category" and an inline "goal" of behavior node `node`.  With a goal object it
        // registers the goal, sets goal_id to its id and returns true; with none (a plain goal
        // string, or no goal) it returns false and leaves goal_id alone.  Throws on a goal that
        // takes a built-in goal's id, has no category, or another node defined already.  A node
        // loaded again replaces what it defined only when it gives "goal" or "category" again:
        // an override that changes only its conditions or score keeps its goal.
        bool load_node( const JsonObject &jo, const string_id<behavior::node_t> &node,
                        std::string_view src, std::string &goal_id );
        const npc_json_goal *find( std::string_view id ) const;
        const std::map<std::string, npc_json_goal, std::less<>> &all() const {
            return goals_;
        }
        const std::vector<npc_goal_claim> &claims() const {
            return claims_;
        }
        // A goal made in code (a custom need's) by `source`, such as "npc_need <id>": it replaces
        // what that source gave before. Returns why it can't be added: its id is a built-in
        // goal's or another's goal; "" when it was added.
        std::string add_goal( npc_json_goal goal, const std::string &source );
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
// the rank of the earliest category with a JSON goal, worked out after loading; INT_MAX for none
int best_rank();
void refresh_ranks();
// for tests: puts `other` in place of the loaded goals (swap back to restore)
void swap_registry_for_test( npc_goal_registry &other );
} // namespace npc_json_goals

// A need content gives NPCs ("type": "npc_need", doc/JSON/NPC_BEHAVIOR.md#custom-needs): an
// urgency, the urgency it acts from, and the JSON goal that answers it. Each one becomes a leaf
// of the decision tree, by default under npc_needs next to the built-in needs, which it leaves
// as they are.
struct npc_custom_need {
    string_id<npc_custom_need> id;
    std::vector<std::pair<string_id<npc_custom_need>, mod_id>> src;
    bool was_loaded = false;

    // with the NPC as u: a number, a variable or math
    std::function<double( const_dialogue const & )> urgency;
    // the urgency the NPC acts on the need from
    float act = 0.5f;
    // only NPCs it holds for feel the need; empty: every NPC
    std::function<bool( const_dialogue const & )> condition;
    npc_decision_category_id category;
    // the behavior node its leaf is added under
    string_id<behavior::node_t> parent;
    // what the NPC does about it: the hooks of its goal, as a JSON goal's
    std::vector<effect_on_condition_id> on_start;
    std::vector<effect_on_condition_id> on_turn;
    std::vector<effect_on_condition_id> on_end;
    std::vector<effect_on_condition_id> on_interrupt;
    std::vector<effect_on_condition_id> on_timeout;
    // empty: the goal is done once the need isn't felt
    std::function<bool( const_dialogue const & )> until;
    std::optional<time_duration> timeout;

    void load( const JsonObject &jo, std::string_view src );
    void check() const;

    // its urgency for `who`, clamped to 0 to 1; 0 for a creature that isn't an NPC
    float urgency_for( const Creature &who ) const;
    // `who` is an NPC, the condition holds for it and the urgency is at act or above
    bool felt_by( const Creature &who ) const;
    // the leaf the need becomes ("npc_need_" and its id), and the goal it gives
    string_id<behavior::node_t> node_id() const;
    npc_json_goal goal() const;
};

namespace npc_custom_needs
{
void load( const JsonObject &jo, const std::string &src );
void finalize();
void check_consistency();
void reset();
const std::vector<npc_custom_need> &get_all();
const npc_custom_need *find( std::string_view id );
// whether `name` may name a need in npc_need_urgency(): a built-in need or a loaded custom one.
// While data loads, a name that isn't one yet is taken and checked by check_consistency().
bool may_name_need( const std::string &name );
} // namespace npc_custom_needs

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
// with the NPC's need_critical_urgency (npc_ai_tuning) in place of the default
npc_interrupt_verdict judge_interrupt( const npc_pursuit &current,
                                       const npc_challenger &challenger, float critical_urgency );

// What the NPC decision debug view shows for an NPC
struct npc_decision_report {
    struct condition_line {
        std::string label;
        bool holds = false;
    };
    // one line per node of the tree, depth first
    struct node_line {
        int depth = 0;
        std::string id;
        std::vector<condition_line> conditions;
        // running, success or failure
        std::string result;
        std::optional<float> score;
        // a leaf's goal, with its category
        std::string goal;
        std::string category;
        // on the path to the goal the tree picks
        bool picked = false;
    };
    struct category_line {
        std::string id;
        int rank = 0;
        float hysteresis = 0.0f;
    };
    std::vector<node_line> nodes;
    std::vector<category_line> categories;
    std::string picked_goal;
    float picked_score = 0.0f;
    std::string picked_category;
    // the goal the NPC is committed to, "idle" for none
    std::string committed_goal;
    std::string committed_category;
    float committed_score = 0.0f;
    int committed_turns = 0;
    // why the committed goal stays, or what takes over
    std::string verdict;
};

// What the NPC's goal comes to this turn, worked out without acting on it: npc::move() acts
// on it, and the decision debug view shows it.
struct npc_goal_choice {
    // what the tree picks, or a sleep it comes back to, and its score
    std::string picked;
    float picked_score = 0.0f;
    // the goal the NPC was committed to, empty for none, and its score now
    std::string committed;
    float committed_score = 0.0f;
    // the pick is a walk the committed JSON goal's EOCs sent the NPC on: the goal at work
    bool own_walk = false;
    // the committed goal is over: a built-in one by its own rule, a JSON one for json_end
    bool completed = false;
    std::string json_end;
    // the pick takes over from a committed goal that isn't over, and why it does or doesn't
    bool interrupted = false;
    npc_interrupt_verdict verdict;
    // the goal the NPC is on this turn, and its score
    std::string goal;
    float score = 0.0f;
};

// The NPC's goal decision and its JSON goals; a friend of npc
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
        static float score( const npc_json_goal &goal, const behavior::oracle_t &oracle );
        // its node's conditions hold
        static bool node_applies( const npc_json_goal &goal, const behavior::oracle_t &oracle );
        // What the goal comes to this turn: the tree's pick, the committed goal's end, a
        // challenger taking over.  Changes nothing but the caches the walk fills; with an
        // oracle that doesn't use them, nothing at all.  npc::move() calls sync() first.
        static npc_goal_choice choose( npc &guy, const behavior::oracle_t &oracle );
        // A danger reaction of category `reaction` (a fight, a flight, a sound) is about to
        // take the NPC's move.  True when a JSON goal of a category at or above it keeps the
        // NPC instead: the one it's on, or one the tree picks now.  A JSON goal below it ends
        // as interrupted.
        static bool holds_against( npc &guy, const decision_category &reaction );
        // the NPC is on a JSON goal of `cat` or of a category before it
        static bool outranks( const npc &guy, const decision_category &cat );
        // sends npc_goal_changed when the current goal isn't the one last reported
        static void report_change( npc &guy );

        // what the NPC decision debug view shows; changes nothing
        static npc_decision_report report( const npc &guy );

        // how often holds_against() walked the tree, for tests
        static int reaction_tree_walks;
};

#endif // CATA_SRC_NPC_JSON_GOAL_H
