// The behavior tree from JSON (NPC-J03): editing the shared tree with copy-from, extend and
// delete, and the checks done at load.
// The test nodes are in data/mods/TEST_DATA/npc_behavior_tree.json.

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "behavior.h"
#include "cata_catch.h"
#include "character.h"
#include "character_oracle.h"
#include "coordinates.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "npc.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"

static const faction_id faction_your_followers( "your_followers" );

static const mtype_id mon_zombie( "mon_zombie" );

static const string_id<behavior::node_t> behavior_node_t_npc_decision( "npc_decision" );
static const string_id<behavior::node_t>
behavior_node_t_test_bt_fight_anyway( "test_bt_fight_anyway" );
static const string_id<behavior::node_t> behavior_node_t_test_bt_inserts( "test_bt_inserts" );
static const string_id<behavior::node_t> behavior_node_t_test_bt_moved( "test_bt_moved" );
static const string_id<behavior::node_t> behavior_node_t_test_bt_no_combat( "test_bt_no_combat" );
static const string_id<behavior::node_t> behavior_node_t_test_bt_replaced( "test_bt_replaced" );

namespace
{

// The node and, below it, its children: "id:goal" for a leaf, "id(child,child)" otherwise.
std::string dump( const behavior::node_t &node )
{
    const std::vector<const behavior::node_t *> &children = node.get_children();
    if( children.empty() ) {
        return node.id.str() + ":" + node.goal();
    }
    std::string out = node.id.str() + "(";
    for( size_t i = 0; i < children.size(); ++i ) {
        out += ( i == 0 ? "" : "," ) + dump( *children[i] );
    }
    return out + ")";
}

std::vector<std::string> child_names( const behavior::node_t &node )
{
    std::vector<std::string> names;
    for( const behavior::node_t *child : node.get_children() ) {
        names.push_back( child->id.str() );
    }
    return names;
}

// The goal and score the tree rooted at `root` picks for `who`.
std::pair<std::string, float> decide( const behavior::node_t &root, const Character &who )
{
    behavior::character_oracle_t oracle( &who );
    behavior::tree tree;
    tree.add( &root );
    return tree.tick_full( &oracle );
}

npc &plain_npc( const point_bub_ms &where = point_bub_ms( 50, 50 ) )
{
    npc &guy = spawn_npc( where, "test_talker" );
    clear_character( guy );
    guy.guard_pos = std::nullopt;
    guy.clear_ai_guard_pos();
    return guy;
}

// A node loaded from JSON the way the factory does it; with a base, as copy-from does.
behavior::node_t load_node( const std::string &id, const std::string &json,
                            const behavior::node_t *base = nullptr )
{
    behavior::node_t node;
    if( base != nullptr ) {
        node = *base;
        node.was_loaded = true;
    }
    node.id = string_id<behavior::node_t>( id );
    JsonObject jo = json_loader::from_string( json );
    jo.allow_omitted_members();
    node.load( jo, "" );
    return node;
}

// Nodes from (id, JSON) pairs, linked as finalization links the loaded ones.
std::vector<std::string> link( std::vector<behavior::node_t> &nodes,
                               const std::vector<std::pair<std::string, std::string>> &defs )
{
    nodes.clear();
    nodes.reserve( defs.size() );
    for( const auto &[id, json] : defs ) {
        nodes.push_back( load_node( id, json ) );
    }
    return behavior::link_children( nodes );
}

bool mentions( const std::vector<std::string> &problems, const std::string &text )
{
    for( const std::string &problem : problems ) {
        if( problem.find( text ) != std::string::npos ) {
            return true;
        }
    }
    return false;
}

} // namespace

// The vanilla tree loads to the same shape as before it moved onto generic_factory's
// copy-from, extend and delete.
TEST_CASE( "npc_decision_tree_loads_unchanged", "[npc][behavior]" )
{
    CHECK( dump( behavior_node_t_npc_decision.obj() ) ==
           "npc_decision("
           "npc_combat(npc_flee:flee,npc_fight:fight),"
           "npc_investigate:investigate_sound,"
           "npc_priorities("
           "npc_needs("
           "npc_homeostasis(npc_regulate_temperature:regulate_temperature,"
           "npc_make_fire:regulate_temperature),"
           "npc_thirst(npc_obtain_water(npc_drink_water:drink_water,npc_seek_water:drink_water)),"
           "npc_hunger(npc_obtain_food(npc_eat_food:eat_food,npc_forage_food:eat_food)),"
           "npc_sleepiness(npc_go_to_sleep:go_to_sleep)),"
           "npc_duty(npc_return_to_post:return_to_guard_pos,npc_hold_post:hold_position),"
           "npc_follow_embarked:follow_embarked,"
           "npc_follow:follow_player,"
           "npc_player_order:goto_ordered_position,"
           "npc_camp_work:camp_work,"
           "npc_return_to_camp:return_to_camp,"
           "npc_free_time:free_time))" );
}

TEST_CASE( "behavior_copy_from_extend_and_delete_children", "[npc][behavior]" )
{
    // copy-from leaves the original alone
    CHECK( child_names( behavior_node_t_npc_decision.obj() ) ==
           std::vector<std::string> { "npc_combat", "npc_investigate", "npc_priorities" } );
    CHECK( child_names( behavior_node_t_test_bt_no_combat.obj() ) ==
           std::vector<std::string> { "npc_investigate", "npc_priorities" } );
    const std::vector<std::string> inserted = {
        "test_bt_leaf_first", "npc_combat", "test_bt_leaf_middle", "npc_investigate",
        "npc_priorities", "test_bt_leaf_last"
    };
    CHECK( child_names( behavior_node_t_test_bt_inserts.obj() ) == inserted );
    // inserted before the child it deletes: replaced in place
    CHECK( child_names( behavior_node_t_test_bt_replaced.obj() ) ==
           std::vector<std::string> { "test_bt_leaf_first", "npc_investigate", "npc_priorities" } );
    // deleted and inserted again: moved
    CHECK( child_names( behavior_node_t_test_bt_moved.obj() ) ==
           std::vector<std::string> { "npc_priorities", "npc_combat", "npc_investigate" } );
}

TEST_CASE( "behavior_tree_without_combat", "[npc][behavior]" )
{
    clear_map();
    npc &guy = plain_npc();
    guy.set_fac( faction_your_followers );
    map &here = get_map();
    get_player_character().setpos( here, guy.pos_bub() + tripoint( 0, 0, -2 ) );
    REQUIRE( g->place_critter_at( mon_zombie, guy.pos_bub() + point::east ) != nullptr );
    guy.recalc_sight_limits();
    guy.regen_ai_cache();
    REQUIRE( guy.get_ai_danger() > 0 );
    REQUIRE( decide( behavior_node_t_npc_decision.obj(), guy ).first == "fight" );

    const std::string without_combat = decide( behavior_node_t_test_bt_no_combat.obj(), guy ).first;
    CHECK( without_combat != "fight" );
    CHECK( without_combat != "flee" );

    // combat replaced in place, and an insert before it
    CHECK( decide( behavior_node_t_test_bt_replaced.obj(), guy ).first == "test_first" );
    CHECK( decide( behavior_node_t_test_bt_inserts.obj(), guy ).first == "test_first" );
    clear_creatures();
    clear_npcs();
}

TEST_CASE( "behavior_built_ins_reconditioned", "[npc][behavior]" )
{
    clear_map();
    npc &guy = plain_npc();
    REQUIRE( decide( behavior_node_t_npc_decision.obj(), guy ).first == "idle" );

    // npc_fight without its npc_has_target condition
    CHECK( decide( behavior_node_t_test_bt_fight_anyway.obj(), guy ).first == "fight" );
    clear_npcs();
}

TEST_CASE( "behavior_positioned_inserts", "[behavior]" )
{
    const behavior::node_t base = load_node( "test_val_base", R"({ "strategy": "fallback",
        "children": [ "test_val_a", "test_val_b" ] })" );
    std::vector<behavior::node_t> nodes = {
        base,
        load_node( "test_val_derived", R"({ "extend": { "children": [
            { "id": "test_val_c", "after": "test_val_a" },
            { "id": "test_val_d", "before": "test_val_a" },
            { "id": "test_val_e", "after": "test_val_c" },
            "test_val_f" ] } })", &base ),
        load_node( "test_val_a", R"({ "goal": "a" })" ),
        load_node( "test_val_b", R"({ "goal": "b" })" ),
        load_node( "test_val_c", R"({ "goal": "c" })" ),
        load_node( "test_val_d", R"({ "goal": "d" })" ),
        load_node( "test_val_e", R"({ "goal": "e" })" ),
        load_node( "test_val_f", R"({ "goal": "f" })" )
    };
    CHECK( behavior::link_children( nodes ).empty() );
    CHECK( child_names( nodes[0] ) == std::vector<std::string> { "test_val_a", "test_val_b" } );
    const std::vector<std::string> inserted = {
        "test_val_d", "test_val_a", "test_val_c", "test_val_e", "test_val_b", "test_val_f"
    };
    CHECK( child_names( nodes[1] ) == inserted );

    SECTION( "an anchor that is not a child is an error" ) {
        CHECK_THROWS_WITH( load_node( "test_val_bad", R"({ "extend": { "children": [
            { "id": "test_val_c", "before": "test_val_x" } ] } })", &base ),
                           Catch::Matchers::Contains( "can't insert \"test_val_c\" before \"test_val_x\"" ) );
        CHECK_THROWS_WITH( load_node( "test_val_bad", R"({ "extend": { "children": [
            { "id": "test_val_c", "before": "test_val_a", "after": "test_val_b" } ] } })", &base ),
                           Catch::Matchers::Contains( "not both" ) );
    }
    SECTION( "deleting a child that isn't there is an error" ) {
        CHECK_THROWS_WITH( load_node( "test_val_bad", R"({ "delete": { "children": [ "test_val_x" ] } })",
                                      &base ), Catch::Matchers::Contains( "can't delete child \"test_val_x\"" ) );
        CHECK_THROWS_WITH( load_node( "test_val_bad",
                                      R"({ "delete": { "conditions": [ "npc_has_water" ] } })", &base ),
                           Catch::Matchers::Contains( "can't delete condition \"npc_has_water\"" ) );
    }
    SECTION( "inserting a child twice is an error" ) {
        CHECK_THROWS_WITH( load_node( "test_val_bad", R"({ "extend": { "children": [ "test_val_a" ] } })",
                                      &base ), Catch::Matchers::Contains( "already has child \"test_val_a\"" ) );
    }
    SECTION( "extend and delete need copy-from" ) {
        CHECK_THROWS_WITH( load_node( "test_val_bad", R"({ "extend": { "children": [ "test_val_a" ] } })" ),
                           Catch::Matchers::Contains( "no copy-from" ) );
        CHECK_THROWS_WITH( load_node( "test_val_bad", R"({ "goal": "g",
            "delete": { "conditions": [ "npc_has_water" ] } })" ),
                           Catch::Matchers::Contains( "no copy-from" ) );
    }
}

TEST_CASE( "behavior_tree_checks_at_load", "[behavior]" )
{
    std::vector<behavior::node_t> nodes;

    SECTION( "a sound tree has no problems" ) {
        CHECK( link( nodes, {
            { "test_val_root", R"({ "strategy": "fallback", "children": [ "test_val_leaf" ] })" },
            { "test_val_leaf", R"({ "goal": "g" })" }
        } ).empty() );
    }
    SECTION( "unknown children" ) {
        const std::vector<std::string> problems = link( nodes, {
            { "test_val_root", R"({ "strategy": "fallback", "children": [ "test_val_missing", "test_val_leaf" ] })" },
            { "test_val_leaf", R"({ "goal": "g" })" }
        } );
        CHECK( mentions( problems,
                         "Behavior \"test_val_root\" lists child \"test_val_missing\", which is not a behavior." ) );
        // the unknown child is left out
        CHECK( child_names( nodes[0] ) == std::vector<std::string> { "test_val_leaf" } );
    }
    SECTION( "a child listed twice" ) {
        const std::vector<std::string> problems = link( nodes, {
            { "test_val_root", R"({ "strategy": "fallback", "children": [ "test_val_leaf", "test_val_leaf" ] })" },
            { "test_val_leaf", R"({ "goal": "g" })" }
        } );
        CHECK( mentions( problems, "lists child \"test_val_leaf\" twice" ) );
        CHECK( child_names( nodes[0] ) == std::vector<std::string> { "test_val_leaf" } );
    }
    SECTION( "cycles" ) {
        const std::vector<std::string> problems = link( nodes, {
            { "test_val_a", R"({ "strategy": "fallback", "children": [ "test_val_b" ] })" },
            { "test_val_b", R"({ "strategy": "fallback", "children": [ "test_val_c", "test_val_leaf" ] })" },
            { "test_val_c", R"({ "strategy": "fallback", "children": [ "test_val_a" ] })" },
            { "test_val_leaf", R"({ "goal": "g" })" }
        } );
        CHECK( mentions( problems, "Behavior \"test_val_c\" lists child \"test_val_a\", which closes a "
                         "cycle: test_val_a -> test_val_b -> test_val_c -> test_val_a." ) );
        // the link that closes the cycle is left out, so a tick ends
        CHECK( nodes[2].get_children().empty() );
        CHECK( nodes[0].tick( nullptr ).result == behavior::status_t::running );
    }
    SECTION( "a root that reaches no leaf" ) {
        const std::vector<std::string> problems = link( nodes, {
            { "test_val_root", R"({ "strategy": "fallback", "children": [ "test_val_mid" ] })" },
            { "test_val_mid", R"({ "strategy": "fallback", "children": [ "test_val_missing" ] })" }
        } );
        CHECK( mentions( problems, "Behavior tree \"test_val_root\" reaches no leaf with a goal" ) );
        // only roots are reported
        CHECK_FALSE( mentions( problems, "Behavior tree \"test_val_mid\"" ) );
    }
}
