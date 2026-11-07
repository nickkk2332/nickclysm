#pragma once
#ifndef CATA_SRC_BEHAVIOR_H
#define CATA_SRC_BEHAVIOR_H

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "type_id.h"

class JsonObject;

namespace behavior
{

class node_t;
class oracle_t;
class strategy_t;

enum class status_t : char { running, success, failure };
struct behavior_return {
    status_t result;
    const node_t *selection;
    float score = 0.0f;
};

// The behavior tree is (at least initially) intended to decide on a goal for a given subject.
// Each time tick is called, it evaluates the tree to find the highest-priority behavior that is
// capable of making forward progress.
// The behavior tree performs a depth-first traversal until it reaches an unmet goal.
// When tick is invoked, it visits root -> node_t::tick -> strategy -> children -> node_t::tick
// At each level, the strategy determines the order in which to visit the current node's children.
// Once a leaf node is reached, it either returns success, indicating its requirements are met,
// failure, indicating that it is incapable of satisfying its requirements, or running, indicating
// that it is capable of addressing its requirements, but that they aren't met yet.
// In practice, the tree is traversed until it reaches a node that returns running,
// meaning that it is capable of making progress if set as a goal.
// The arrangement of the tree and configuration of iteration strategies guarantees that it visits
// nodes in order of descending priority.
// A predicate can be injected into any node to direct iteration,
// including skipping entire subtrees.
class tree
{
    public:
        // Entry point, evaluates the tree and returns the selected goal.
        std::string tick( const oracle_t *subject );
        // Evaluate the tree and return both the goal string and its
        // utility score (from the winning node's score function).
        std::pair<std::string, float> tick_full( const oracle_t *subject );
        // Retrieves the most recently determined goal without re-evaluating the tree.
        std::string goal() const;
        // Score from the most recent tick_full / tick evaluation.
        float last_score() const;
        // Set the root node of the tree.
        void add( const node_t *new_node );
    private:
        const node_t *root = nullptr;
        const node_t *active_node = nullptr;
        float active_score = 0.0f;
};

class node_t
{
    public:
        node_t() = default;
        // Entry point for tree traversal.
        behavior_return tick( const oracle_t *subject ) const;
        std::string goal() const;
        // The children, in the order the strategy visits them.
        const std::vector<const node_t *> &get_children() const;
        // What the node holds, for tests and debugging, as
        // "strategy fallback; if npc_in_danger, !x, y(arg), condition; score s; every 5; goal g",
        // leaving out what it doesn't have.
        std::string summary() const;

        // For the NPC decision debug view (npc_json_goal.h): what each condition says now
        struct condition_trace {
            std::string label;
            status_t result;
        };
        std::vector<condition_trace> trace_conditions( const oracle_t *subject ) const;
        bool has_score() const {
            return score_function_.has_value();
        }

        // Interface to construct a node.
        void set_strategy( const strategy_t *new_strategy );
        void add_predicate( const std::function < status_t ( const oracle_t *, const std::string & )> &
                            new_predicate, const std::string &argument = "", const bool &invert_result = false );
        void set_goal( const std::string &new_goal );
        void add_child( const node_t *new_child );
        using score_type = std::function<float( const oracle_t *, std::string_view )>;
        void set_score_function( const score_type &func, const std::string &argument = "" );

        // Loading interface.
        void load( const JsonObject &jo, std::string_view src );
        void check() const;
        string_id<node_t> id;
        std::vector<std::pair<string_id<node_t>, mod_id>> src;
        bool was_loaded = false;
    private:
        friend std::vector<std::string> link_children( std::vector<node_t> &nodes );
        friend void clear_evaluation_cache();
        friend std::vector<std::string> add_custom_need_leaves();

        std::vector<const node_t *> children;
        // The children named in JSON, in order. link_children() turns them into `children`.
        std::vector<string_id<node_t>> child_ids;
        const strategy_t *strategy = nullptr;
        using predicate_type = std::function<status_t( const oracle_t *, const std::string & )>;
        struct condition_t {
            // The name used in JSON ("condition" for a dialogue condition), which
            // "delete" matches. Empty for predicates added in code.
            std::string predicate;
            predicate_type func;
            std::string argument;
            bool invert = false;
        };
        std::vector<condition_t> conditions;
        status_t process_predicates( const oracle_t *subject ) const;
        // TODO: make into an ID?
        std::string _goal;
        std::optional<std::pair<score_type, std::string>> score_function_;
        // The built-in score's name (empty for math), and how summary() shows the score.
        std::string score_name_;
        std::string score_label_;

        // "evaluate_every": the conditions and score are reused for this many turns, separately
        // for each subject. 0 evaluates them every time.
        int evaluate_every_turns = 0;
        struct cached_evaluation {
            int evaluated_at = 0;
            status_t conditions = status_t::running;
            std::optional<float> score;
        };
        // Keyed by oracle_t::cache_key().
        mutable std::unordered_map<int, cached_evaluation> evaluation_cache;
        // Expired entries are pruned when the cache reaches this size, which then doubles.
        mutable size_t evaluation_cache_prune_at = 256;
        // The subject's cached evaluation, refreshed when it is missing or expired; nullptr
        // when this node or this subject isn't cached.
        cached_evaluation *cached_for( const oracle_t *subject ) const;
        float evaluate_score( const oracle_t *subject, cached_evaluation *cached ) const;

        void load_children( const JsonObject &jo, const JsonObject &extend, const JsonObject &remove );
        void load_conditions( const JsonObject &jo, const JsonObject &extend,
                              const JsonObject &remove );
        void load_score( const JsonObject &jo );
};

/**
 * Resolves each node's child ids to the nodes with those ids. An unknown child, or a link that
 * would close a cycle, is left out, so ticking a tree always ends; a node whose children were all
 * left out fails. Returns one message per problem: unknown or repeated children, cycles, trees
 * whose root reaches no leaf with a goal, and trees that use both NPC and monster predicates or
 * scores. A root is a node that no other node in @p nodes links to.
 */
std::vector<std::string> link_children( std::vector<node_t> &nodes );

// Forgets the results every node keeps for "evaluate_every".
void clear_evaluation_cache();

/**
 * Adds a leaf for each need content defines ("type": "npc_need", src/npc_json_goal.h) to the
 * children of its parent node, after the ones the parent's JSON gives, and registers its goal.
 * finalize() runs it before linking the children; running it again replaces what it added.
 * Returns one message per need it had to leave out.
 */
std::vector<std::string> add_custom_need_leaves();

// Deserialization support.
void load_behavior( const JsonObject &jo, const std::string &src );

void reset();

void finalize();

void check_consistency();

} // namespace behavior

#endif // CATA_SRC_BEHAVIOR_H
