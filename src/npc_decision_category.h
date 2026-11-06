#pragma once
#ifndef CATA_SRC_NPC_DECISION_CATEGORY_H
#define CATA_SRC_NPC_DECISION_CATEGORY_H

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "type_id.h"

class JsonObject;
class npc_decision_category;

using npc_decision_category_id = string_id<npc_decision_category>;

// A rank of NPC goals, the JSON type "npc_decision_category" (doc/JSON/NPC_BEHAVIOR.md).  A goal
// in a category earlier in the order interrupts one in a later category; within a category the
// higher score wins.  The built-in list is data/json/npcs/npc_decision_categories.json; mods add
// categories before or after one of them.
class npc_decision_category
{
    public:
        npc_decision_category_id id;
        std::vector<std::pair<npc_decision_category_id, mod_id>> src;
        bool was_loaded = false;

        // built-in goals in this category; a JSON goal names its category itself
        std::vector<std::string> goals;
        // where a mod's category goes in the order; a category with neither keeps its load
        // order, after the ones loaded before it
        std::optional<npc_decision_category_id> before;
        std::optional<npc_decision_category_id> after;
        // share of its score a committed goal gains against a challenger of its category
        float hysteresis = 0.1f;

        // position in the resolved order, 0 for the first; set by finalize_all()
        int rank = 0;

        void load( const JsonObject &jo, std::string_view src );

        static void load_category( const JsonObject &jo, const std::string &src );
        static void reset();
        // resolves the order and which category each built-in goal is in
        static void finalize_all();
        static void check_consistency();
        static const std::vector<npc_decision_category> &get_all();
        // every category, first to last
        static const std::vector<npc_decision_category_id> &order();
        // for tests: puts a goal in another category, or in none; returns the one it had
        static std::optional<npc_decision_category_id> set_goal_category_for_test(
            const std::string &goal, const std::optional<npc_decision_category_id> &cat );
};

// One category's place in the order, as resolve_category_order() takes it
struct category_placement {
    npc_decision_category_id id;
    std::optional<npc_decision_category_id> before;
    std::optional<npc_decision_category_id> after;
};

// The order of these categories, given in load order: the ones that name no neighbour keep
// their load order, then each of the others goes right before or after the one it names (after
// the ones placed after that one already).  Placements that can't be made (an unknown or
// unplaceable neighbour, a loop) are left out, with an error for each in errors.
std::vector<npc_decision_category_id> resolve_category_order(
    const std::vector<category_placement> &placements, std::vector<std::string> &errors );

// A category as C++ code sees it, compared by its place in the resolved order.  The built-in
// categories have constants, for the places that act on one of them.
class decision_category
{
    public:
        // unmodeled
        decision_category();
        explicit decision_category( const npc_decision_category_id &id );

        static const decision_category combat;
        static const decision_category investigate;
        static const decision_category needs;
        static const decision_category follow;
        static const decision_category order;
        static const decision_category duty;
        static const decision_category camp_work;
        static const decision_category camp_travel;
        static const decision_category free_time;
        static const decision_category idle;
        static const decision_category unmodeled;

        const npc_decision_category_id &id() const {
            return id_;
        }
        // position in the order; an unknown category ranks as unmodeled
        int rank() const;
        float hysteresis() const;

        bool operator==( const decision_category &other ) const {
            return id_ == other.id_;
        }
        bool operator!=( const decision_category &other ) const {
            return id_ != other.id_;
        }
        // earlier in the order: takes precedence
        bool operator<( const decision_category &other ) const {
            return rank() < other.rank();
        }

    private:
        npc_decision_category_id id_;
};

// the goals with C++ behind them (npc::move() dispatches them); a JSON goal can't take their ids
const std::vector<std::string> &builtin_npc_goals();
bool is_builtin_npc_goal( std::string_view goal );
// The built-in needs.  Two of them weigh their urgencies with margins against each other (see
// judge_interrupt()), in whatever categories they are: the C++ rules of a built-in goal follow
// the goal, its category only ranks it.
bool is_builtin_need_goal( std::string_view goal );

const char *category_name( const decision_category &cat );
// the category of a goal: built-in ones from their category's "goals", JSON ones from their
// node's "category"; unmodeled for any other
decision_category bt_goal_to_category( const std::string &goal );
const char *classify_comparison( const decision_category &bt, const decision_category &cascade );

#endif // CATA_SRC_NPC_DECISION_CATEGORY_H
