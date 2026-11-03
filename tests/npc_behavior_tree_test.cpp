// The behavior tree from JSON (NPC-J03): editing the shared tree with copy-from, extend and
// delete; dialogue conditions and math scores; evaluate_every; and the checks done at load.
// The test nodes are in data/mods/TEST_DATA/npc_behavior_tree.json.

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "avatar.h"
#include "behavior.h"
#include "behavior_oracle.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "character_id.h"
#include "character_oracle.h"
#include "condition.h"
#include "coordinates.h"
#include "creature.h"
#include "debug.h"
#include "dialogue.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "item.h"
#include "item_group.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser.h"
#include "monster.h"
#include "monster_oracle.h"
#include "npc.h"
#include "player_helpers.h"
#include "point.h"
#include "talker.h"
#include "type_id.h"

static const faction_id faction_no_faction( "no_faction" );
static const faction_id faction_your_followers( "your_followers" );

static const item_group_id Item_spawn_data_test_bottle_water( "test_bottle_water" );

static const itype_id itype_backpack( "backpack" );

static const mtype_id mon_zombie( "mon_zombie" );

static const string_id<behavior::node_t> behavior_node_t_ABSORB_ITEMS( "ABSORB_ITEMS" );
static const string_id<behavior::node_t> behavior_node_t_npc_decision( "npc_decision" );
static const string_id<behavior::node_t> behavior_node_t_npc_thirst( "npc_thirst" );
static const string_id<behavior::node_t>
behavior_node_t_test_bt_beta_condition( "test_bt_beta_condition" );
static const string_id<behavior::node_t> behavior_node_t_test_bt_beta_math( "test_bt_beta_math" );
static const string_id<behavior::node_t> behavior_node_t_test_bt_cached( "test_bt_cached" );
static const string_id<behavior::node_t>
behavior_node_t_test_bt_fight_anyway( "test_bt_fight_anyway" );
static const string_id<behavior::node_t>
behavior_node_t_test_bt_fight_devout( "test_bt_fight_devout" );
static const string_id<behavior::node_t> behavior_node_t_test_bt_has_trait( "test_bt_has_trait" );
static const string_id<behavior::node_t> behavior_node_t_test_bt_inserts( "test_bt_inserts" );
static const string_id<behavior::node_t>
behavior_node_t_test_bt_lacks_trait( "test_bt_lacks_trait" );
static const string_id<behavior::node_t> behavior_node_t_test_bt_moved( "test_bt_moved" );
static const string_id<behavior::node_t> behavior_node_t_test_bt_no_combat( "test_bt_no_combat" );
static const string_id<behavior::node_t>
behavior_node_t_test_bt_pray_or_drink( "test_bt_pray_or_drink" );
static const string_id<behavior::node_t> behavior_node_t_test_bt_replaced( "test_bt_replaced" );
static const string_id<behavior::node_t> behavior_node_t_test_bt_reported( "test_bt_reported" );
static const string_id<behavior::node_t> behavior_node_t_test_bt_score_half( "test_bt_score_half" );
static const string_id<behavior::node_t> behavior_node_t_test_bt_score_math( "test_bt_score_math" );
static const string_id<behavior::node_t>
behavior_node_t_test_bt_score_named( "test_bt_score_named" );
static const string_id<behavior::node_t>
behavior_node_t_test_bt_score_oracle( "test_bt_score_oracle" );
static const string_id<behavior::node_t>
behavior_node_t_test_bt_thirst_half( "test_bt_thirst_half" );

static const trait_id trait_IGNORE_SOUND( "IGNORE_SOUND" );

namespace
{

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
    // no allow_omitted_members(): a field that loading ignores fails the test
    const JsonObject jo = json_loader::from_string( json );
    node.load( jo, "" );
    return node;
}

// The node and its subtree, a line each: its id, then what summary() gives, indented by depth.
std::string describe( const behavior::node_t &node, int depth = 0 )
{
    std::string out = std::string( 2 * depth, ' ' ) + node.id.str() + ": " + node.summary() + "\n";
    for( const behavior::node_t *child : node.get_children() ) {
        out += describe( *child, depth + 1 );
    }
    return out;
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

// The vanilla tree loads to the same nodes, with the same strategies, conditions and scores, as
// before it moved onto generic_factory's copy-from, extend and delete.
TEST_CASE( "npc_decision_tree_loads_unchanged", "[npc][behavior]" )
{
    CHECK( describe( behavior_node_t_npc_decision.obj() ) ==
           "npc_decision: strategy fallback\n"
           "  npc_combat: strategy fallback; if npc_in_danger\n"
           "    npc_flee: if npc_should_flee; goal flee\n"
           "    npc_fight: if npc_has_target; goal fight\n"
           "  npc_investigate: if npc_has_sound_alerts; goal investigate_sound\n"
           "  npc_priorities: strategy utility\n"
           "    npc_needs: strategy utility\n"
           "      npc_homeostasis: strategy fallback; if npc_needs_temperature_regulation; "
           "score npc_temperature_urgency\n"
           "        npc_regulate_temperature: if npc_can_regulate_temperature; goal regulate_temperature\n"
           "        npc_make_fire: if npc_can_make_fire; goal regulate_temperature\n"
           "      npc_thirst: strategy sequential; if npc_needs_water_badly; score npc_thirst_urgency\n"
           "        npc_obtain_water: strategy fallback\n"
           "          npc_drink_water: if npc_has_water; goal drink_water\n"
           "          npc_seek_water: if npc_can_obtain_water; goal drink_water\n"
           "      npc_hunger: strategy sequential; if npc_needs_food_badly; score npc_hunger_urgency\n"
           "        npc_obtain_food: strategy fallback\n"
           "          npc_eat_food: if npc_has_food; goal eat_food\n"
           "          npc_forage_food: if npc_can_obtain_food; goal eat_food\n"
           "      npc_sleepiness: strategy sequential; if npc_needs_sleep_badly; "
           "score npc_sleepiness_urgency\n"
           "        npc_go_to_sleep: if npc_can_sleep; goal go_to_sleep\n"
           "    npc_duty: strategy fallback; score npc_duty_urgency\n"
           "      npc_return_to_post: if npc_on_shift, npc_displaced_from_post; goal return_to_guard_pos\n"
           "      npc_hold_post: if npc_on_shift; goal hold_position\n"
           "    npc_follow_embarked: if npc_should_embark; score npc_embark_urgency; goal follow_embarked\n"
           "    npc_follow: if npc_is_following; score npc_following_urgency; goal follow_player\n"
           "    npc_player_order: if npc_has_goto_order; score npc_goto_order_urgency; "
           "goal goto_ordered_position\n"
           "    npc_camp_work: if npc_has_camp_job; score npc_camp_work_urgency; goal camp_work\n"
           "    npc_return_to_camp: if npc_is_away_from_camp; score npc_return_to_camp_urgency; "
           "goal return_to_camp\n"
           "    npc_free_time: if npc_is_camp_idle; score npc_free_time_urgency; goal free_time\n" );
    // inverted conditions and arguments, from the monster goals
    CHECK( describe( behavior_node_t_ABSORB_ITEMS.obj() ) ==
           "ABSORB_ITEMS: strategy sequential; if monster_not_hallucination, !monster_split_possible, "
           "monster_special_available(!SPLIT), monster_special_available(ABSORB_ITEMS), "
           "monster_items_available; goal ABSORB_ITEMS\n" );
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

    // combat replaced by a node that needs a trait
    CHECK( decide( behavior_node_t_test_bt_replaced.obj(), guy ).first == without_combat );
    guy.set_mutation( trait_IGNORE_SOUND );
    CHECK( decide( behavior_node_t_test_bt_replaced.obj(), guy ).first == "test_first" );
    guy.unset_mutation( trait_IGNORE_SOUND );

    // the inserts come before and after combat
    CHECK( decide( behavior_node_t_test_bt_inserts.obj(), guy ).first == "fight" );
    guy.set_mutation( trait_IGNORE_SOUND );
    CHECK( decide( behavior_node_t_test_bt_inserts.obj(), guy ).first == "test_first" );
    guy.unset_mutation( trait_IGNORE_SOUND );
    clear_creatures();
    clear_npcs();
}

TEST_CASE( "behavior_built_ins_reconditioned_and_rescored", "[npc][behavior]" )
{
    clear_map();
    npc &guy = plain_npc();
    REQUIRE( decide( behavior_node_t_npc_decision.obj(), guy ).first == "idle" );

    // npc_fight without its npc_has_target condition
    CHECK( decide( behavior_node_t_test_bt_fight_anyway.obj(), guy ).first == "fight" );

    // ... and with a dialogue condition in its place
    guy.set_value( "bt_devotion", 10.0 );
    CHECK( decide( behavior_node_t_test_bt_fight_devout.obj(), guy ).first == "idle" );
    guy.set_value( "bt_devotion", 75.0 );
    CHECK( decide( behavior_node_t_test_bt_fight_devout.obj(), guy ).first == "fight" );

    // npc_thirst scored at half its urgency
    guy.set_thirst( 700 );
    guy.wear_item( item( itype_backpack ), false );
    guy.i_add( item_group::items_from( Item_spawn_data_test_bottle_water ).front() );
    const auto [goal, score] = decide( behavior_node_t_npc_thirst.obj(), guy );
    REQUIRE( goal == "drink_water" );
    REQUIRE( score > 0.0f );
    const auto [half_goal, half_score] = decide( behavior_node_t_test_bt_thirst_half.obj(), guy );
    CHECK( half_goal == "drink_water" );
    CHECK( half_score == Approx( score / 2 ) );
    clear_npcs();
}

TEST_CASE( "behavior_condition_predicate", "[npc][behavior]" )
{
    clear_map();
    npc &guy = plain_npc();
    const behavior::node_t &has_trait = behavior_node_t_test_bt_has_trait.obj();
    const behavior::node_t &lacks_trait = behavior_node_t_test_bt_lacks_trait.obj();

    CHECK( decide( has_trait, guy ).first == "idle" );
    CHECK( decide( lacks_trait, guy ).first == "test_lacks_trait" );
    guy.set_mutation( trait_IGNORE_SOUND );
    CHECK( decide( has_trait, guy ).first == "test_has_trait" );
    CHECK( decide( lacks_trait, guy ).first == "idle" );
    guy.unset_mutation( trait_IGNORE_SOUND );

    // a false condition is a failure (not a success), so a fallback goes on to its next child
    const behavior::character_oracle_t oracle( &guy );
    CHECK( has_trait.tick( &oracle ).result == behavior::status_t::failure );
    // without a creature to read, it fails too
    CHECK( has_trait.tick( nullptr ).result == behavior::status_t::failure );
    clear_npcs();
}

TEST_CASE( "behavior_score_forms", "[npc][behavior]" )
{
    clear_map();
    npc &guy = plain_npc();
    guy.set_thirst( 600 );
    guy.set_value( "bt_devotion", 75.0 );
    behavior::character_oracle_t oracle( &guy );
    const float thirst = oracle.thirst_urgency( "" );
    REQUIRE( thirst == Approx( 0.5f ).margin( 0.01f ) );

    CHECK( decide( behavior_node_t_test_bt_score_named.obj(), guy ).second == Approx( thirst ) );
    CHECK( decide( behavior_node_t_test_bt_score_half.obj(), guy ).second == Approx( thirst / 2 ) );
    CHECK( decide( behavior_node_t_test_bt_score_oracle.obj(), guy ).second == Approx( thirst * 2 ) );
    CHECK( decide( behavior_node_t_test_bt_score_math.obj(), guy ).second == Approx( 0.75f ) );

    // a utility node picks by math scores: devotion 0.75 beats thirst 0.5, then loses to 1.0
    const behavior::node_t &pray_or_drink = behavior_node_t_test_bt_pray_or_drink.obj();
    CHECK( decide( pray_or_drink, guy ).first == "test_pray" );
    guy.set_thirst( 1200 );
    CHECK( decide( pray_or_drink, guy ).first == "test_drink" );

    // no subject: math scores 0
    CHECK( behavior_node_t_test_bt_score_math->tick( nullptr ).score == 0.0f );
    clear_npcs();
}

TEST_CASE( "math_oracle_function_reads_named_scores", "[npc][behavior][math_parser]" )
{
    clear_map();
    npc &guy = plain_npc();
    guy.set_thirst( 600 );
    const const_dialogue d( get_const_talker_for( guy ), nullptr );
    const behavior::character_oracle_t oracle( &guy );
    REQUIRE( oracle.thirst_urgency( "" ) == Approx( 0.5f ).margin( 0.01f ) );

    math_exp thirst;
    REQUIRE( thirst.parse( "oracle('npc_thirst_urgency')" ) );
    CHECK( thirst.eval( d ) == Approx( oracle.thirst_urgency( "" ) ) );

    // a name written as a literal is checked when the expression is parsed, at load
    math_exp unknown;
    CHECK_THROWS_WITH( unknown.parse( "oracle('no_such_score')", false ),
                       Catch::Matchers::Contains( "\"no_such_score\" is not a named score" ) );
    load_node( "test_val_bad",
               "{ \"goal\": \"g\", \"score\": { \"math\": [ \"oracle('no_such_score')\" ] } }" );
    CHECK_THROWS_WITH( finalize_conditions(), Catch::Matchers::Contains( "is not a named score" ) );
    // one from a variable, when it is read
    guy.set_value( "bt_score_name", std::string( "no_such_score" ) );
    math_exp from_variable;
    REQUIRE( from_variable.parse( "oracle(u_bt_score_name)" ) );
    CHECK_THROWS_WITH( from_variable.eval( d ), Catch::Matchers::Contains( "is not a named score" ) );

    monster &zombie = spawn_test_monster( "mon_zombie", tripoint_bub_ms( 55, 55, 0 ) );
    const const_dialogue monster_d( get_const_talker_for( zombie ), nullptr );
    CHECK_THROWS_WITH( thirst.eval( monster_d ),
                       Catch::Matchers::Contains( "needs u to be a character" ) );
    clear_creatures();
    clear_npcs();
}

TEST_CASE( "behavior_predicate_and_score_arguments", "[behavior]" )
{
    std::string predicate_got = "none";
    behavior::predicate_map["test_bt_echo"] = [&predicate_got]( const behavior::oracle_t *,
    std::string_view argument ) {
        predicate_got = std::string( argument );
        return behavior::status_t::running;
    };
    behavior::predicates_with_argument.insert( "test_bt_echo" );
    behavior::score_predicate_map["test_bt_echo_score"] = []( const behavior::oracle_t *,
    std::string_view argument ) {
        return argument == "high" ? 0.9f : 0.1f;
    };
    behavior::scores_with_argument.insert( "test_bt_echo_score" );

    SECTION( "the argument reaches the predicate" ) {
        const behavior::node_t node = load_node( "test_bt_arg", R"({ "goal": "g",
            "conditions": [ { "predicate": "test_bt_echo", "argument": "hello" } ] })" );
        CHECK( node.tick( nullptr ).result == behavior::status_t::running );
        CHECK( predicate_got == "hello" );
    }
    SECTION( "the argument reaches the score, in both forms" ) {
        const behavior::node_t named = load_node( "test_bt_arg", R"({ "goal": "g",
            "score": { "named": "test_bt_echo_score", "argument": "high", "mult": 2 } })" );
        CHECK( named.tick( nullptr ).score == Approx( 1.8f ) );
        const behavior::node_t legacy = load_node( "test_bt_arg", R"({ "goal": "g",
            "score": "test_bt_echo_score", "score_argument": "low" })" );
        CHECK( legacy.tick( nullptr ).score == Approx( 0.1f ) );
    }
    SECTION( "an argument for a predicate that reads none is an error" ) {
        CHECK_THROWS_WITH( load_node( "test_bt_arg", R"({ "goal": "g",
            "conditions": [ { "predicate": "npc_has_water", "argument": "clean" } ] })" ),
                           Catch::Matchers::Contains( "predicate \"npc_has_water\" takes no argument" ) );
        CHECK_THROWS_WITH( load_node( "test_bt_arg", R"({ "goal": "g",
            "score": { "named": "npc_thirst_urgency", "argument": "x" } })" ),
                           Catch::Matchers::Contains( "score \"npc_thirst_urgency\" takes no argument" ) );
        CHECK_THROWS_WITH( load_node( "test_bt_arg", R"({ "goal": "g",
            "conditions": [ { "predicate": "condition", "argument": "x", "condition": { "u_has_trait": "IGNORE_SOUND" } } ] })" ),
                           Catch::Matchers::Contains( "takes no argument" ) );
    }
    SECTION( "a predicate that reads one needs it" ) {
        CHECK_THROWS_WITH( load_node( "test_bt_arg", R"({ "goal": "g",
            "conditions": [ { "predicate": "monster_special_available" } ] })" ),
                           Catch::Matchers::Contains( "needs an \"argument\"" ) );
        CHECK_THROWS_WITH( load_node( "test_bt_arg", R"({ "goal": "g",
            "conditions": [ { "predicate": "condition" } ] })" ),
                           Catch::Matchers::Contains( "needs a \"condition\"" ) );
    }
    SECTION( "unknown predicates and scores are errors" ) {
        CHECK_THROWS_WITH( load_node( "test_bt_arg", R"({ "goal": "g",
            "conditions": [ { "predicate": "npc_is_devout" } ] })" ),
                           Catch::Matchers::Contains( "uses predicate \"npc_is_devout\"" ) );
        CHECK_THROWS_WITH( load_node( "test_bt_arg", R"({ "goal": "g",
            "score": { "named": "npc_devotion_urgency" } })" ),
                           Catch::Matchers::Contains( "names score \"npc_devotion_urgency\"" ) );
        CHECK_THROWS_WITH( load_node( "test_bt_arg", R"({ "goal": "g",
            "score": { "named": "npc_thirst_urgency", "math": [ "1" ] } })" ),
                           Catch::Matchers::Contains( "either \"named\" or \"math\"" ) );
    }

    behavior::predicate_map.erase( "test_bt_echo" );
    behavior::predicates_with_argument.erase( "test_bt_echo" );
    behavior::score_predicate_map.erase( "test_bt_echo_score" );
    behavior::scores_with_argument.erase( "test_bt_echo_score" );
}

TEST_CASE( "behavior_evaluate_every_skips_evaluation", "[npc][behavior]" )
{
    const time_point saved_turn = calendar::turn;
    behavior::clear_evaluation_cache();
    int predicate_calls = 0;
    int score_calls = 0;
    behavior::predicate_map["test_bt_count"] = [&predicate_calls]( const behavior::oracle_t *,
    std::string_view ) {
        ++predicate_calls;
        return behavior::status_t::running;
    };
    behavior::score_predicate_map["test_bt_count_score"] = [&score_calls](
    const behavior::oracle_t *, std::string_view ) {
        ++score_calls;
        return 0.5f;
    };

    clear_map();
    npc &first = plain_npc( point_bub_ms( 50, 50 ) );
    npc &second = plain_npc( point_bub_ms( 54, 50 ) );
    behavior::character_oracle_t first_oracle( &first );
    behavior::character_oracle_t second_oracle( &second );
    calendar::turn = calendar::turn_zero + 1_hours;

    SECTION( "cached for 5 turns, separately for each NPC" ) {
        const behavior::node_t node = load_node( "test_bt_count_node", R"({ "goal": "g",
            "evaluate_every": "5 turns", "conditions": [ { "predicate": "test_bt_count" } ],
            "score": "test_bt_count_score" })" );
        CHECK( node.tick( &first_oracle ).score == 0.5f );
        CHECK( predicate_calls == 1 );
        CHECK( score_calls == 1 );
        node.tick( &first_oracle );
        calendar::turn += 4_turns;
        node.tick( &first_oracle );
        CHECK( predicate_calls == 1 );
        CHECK( score_calls == 1 );
        // another NPC has its own evaluation
        node.tick( &second_oracle );
        CHECK( predicate_calls == 2 );
        CHECK( score_calls == 2 );
        calendar::turn += 1_turns;
        node.tick( &first_oracle );
        CHECK( predicate_calls == 3 );
        CHECK( score_calls == 3 );
        // time going back (another game loaded) evaluates again
        calendar::turn -= 3_turns;
        node.tick( &first_oracle );
        CHECK( predicate_calls == 4 );
        // without a subject to tell apart, every tick evaluates
        node.tick( nullptr );
        node.tick( nullptr );
        CHECK( predicate_calls == 6 );
    }
    SECTION( "without evaluate_every, every tick evaluates" ) {
        const behavior::node_t node = load_node( "test_bt_count_node", R"({ "goal": "g",
            "conditions": [ { "predicate": "test_bt_count" } ], "score": "test_bt_count_score" })" );
        node.tick( &first_oracle );
        node.tick( &first_oracle );
        CHECK( predicate_calls == 2 );
        CHECK( score_calls == 2 );
    }
    SECTION( "copy-from keeps evaluate_every; 0 turns it off" ) {
        const behavior::node_t base = load_node( "test_bt_count_node", R"({ "goal": "g",
            "evaluate_every": "5 turns", "conditions": [ { "predicate": "test_bt_count" } ] })" );
        const behavior::node_t inherits = load_node( "test_bt_count_inherits", "{}", &base );
        inherits.tick( &first_oracle );
        inherits.tick( &first_oracle );
        CHECK( predicate_calls == 1 );
        const behavior::node_t uncached = load_node( "test_bt_count_uncached",
                                          R"({ "evaluate_every": "0 turns" })", &base );
        uncached.tick( &first_oracle );
        uncached.tick( &first_oracle );
        CHECK( predicate_calls == 3 );
    }
    SECTION( "a loaded node reuses stale results until they expire" ) {
        const behavior::node_t &cached = behavior_node_t_test_bt_cached.obj();
        first.set_value( "bt_devotion", 75.0 );
        second.set_value( "bt_devotion", 10.0 );
        const auto check = [&cached]( const npc & who, const std::string & goal, float score ) {
            const auto [got_goal, got_score] = decide( cached, who );
            CHECK( got_goal == goal );
            CHECK( got_score == Approx( score ) );
        };
        check( first, "test_cached", 0.75f );
        check( second, "idle", 0.0f );
        first.set_value( "bt_devotion", 60.0 );
        second.set_value( "bt_devotion", 90.0 );
        calendar::turn += 4_turns;
        check( first, "test_cached", 0.75f );
        check( second, "idle", 0.0f );
        calendar::turn += 1_turns;
        check( first, "test_cached", 0.6f );
        check( second, "test_cached", 0.9f );
    }

    behavior::predicate_map.erase( "test_bt_count" );
    behavior::score_predicate_map.erase( "test_bt_count_score" );
    behavior::clear_evaluation_cache();
    calendar::turn = saved_turn;
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
        load_node( "test_val_f", R"({ "goal": "f" })" ),
        // a moved: the anchor "a" for c is the live child, not the deleted one
        load_node( "test_val_moved", R"({ "extend": { "children": [
            { "id": "test_val_a", "after": "test_val_b" },
            { "id": "test_val_c", "before": "test_val_a" } ] },
            "delete": { "children": [ "test_val_a" ] } })", &base )
    };
    CHECK( behavior::link_children( nodes ).empty() );
    CHECK( child_names( nodes[0] ) == std::vector<std::string> { "test_val_a", "test_val_b" } );
    const std::vector<std::string> inserted = {
        "test_val_d", "test_val_a", "test_val_c", "test_val_e", "test_val_b", "test_val_f"
    };
    CHECK( child_names( nodes[1] ) == inserted );
    CHECK( child_names( nodes[8] ) ==
           std::vector<std::string> { "test_val_b", "test_val_c", "test_val_a" } );

    SECTION( "an anchor that is not a child is reported, and the child goes at the end" ) {
        behavior::node_t bad;
        const std::string reported = capture_debugmsg_during( [&]() {
            bad = load_node( "test_val_bad", R"({ "extend": { "children": [
                { "id": "test_val_c", "before": "test_val_x" } ] } })", &base );
        } );
        CHECK_THAT( reported, Catch::Matchers::Contains(
                        "can't insert \"test_val_c\" before \"test_val_x\": it has no such child." ) );
        std::vector<behavior::node_t> linked = { bad, nodes[2], nodes[3], nodes[4] };
        behavior::link_children( linked );
        CHECK( child_names( linked[0] ) ==
               std::vector<std::string> { "test_val_a", "test_val_b", "test_val_c" } );
    }
    SECTION( "before and after in one insert is an error" ) {
        CHECK_THROWS_WITH( load_node( "test_val_bad", R"({ "extend": { "children": [
            { "id": "test_val_c", "before": "test_val_a", "after": "test_val_b" } ] } })", &base ),
                           Catch::Matchers::Contains( "not both" ) );
    }
    SECTION( "the same edit applied twice is reported and skipped" ) {
        // as when two mods delete the same child, or add the same one
        const std::string edit = R"({ "delete": { "children": [ "test_val_a" ] },
            "extend": { "children": [ "test_val_c" ] } })";
        const behavior::node_t once = load_node( "test_val_once", edit, &base );
        behavior::node_t twice;
        const std::string reported = capture_debugmsg_during( [&]() {
            twice = load_node( "test_val_twice", edit, &once );
        } );
        CHECK_THAT( reported, Catch::Matchers::Contains( "can't delete child \"test_val_a\"" ) );
        CHECK_THAT( reported, Catch::Matchers::Contains( "already has child \"test_val_c\"" ) );
        std::vector<behavior::node_t> linked = { twice, nodes[2], nodes[3], nodes[4] };
        behavior::link_children( linked );
        CHECK( child_names( linked[0] ) == std::vector<std::string> { "test_val_b", "test_val_c" } );
    }
    SECTION( "deleting a condition that isn't there is reported and skipped" ) {
        const std::string reported = capture_debugmsg_during( [&]() {
            load_node( "test_val_bad", R"({ "delete": { "conditions": [ "npc_has_water" ] } })", &base );
        } );
        CHECK_THAT( reported, Catch::Matchers::Contains( "can't delete condition \"npc_has_water\"" ) );
    }
    SECTION( "extend and delete need copy-from" ) {
        CHECK_THROWS_WITH( load_node( "test_val_bad", R"({ "extend": { "children": [ "test_val_a" ] } })" ),
                           Catch::Matchers::Contains( "no copy-from" ) );
        CHECK_THROWS_WITH( load_node( "test_val_bad", R"({ "goal": "g",
            "delete": { "conditions": [ "npc_has_water" ] } })" ),
                           Catch::Matchers::Contains( "no copy-from" ) );
    }
    SECTION( "a typo in extend or delete is reported" ) {
        CHECK_THAT( capture_debugmsg_during( [&]() {
            load_node( "test_val_bad", R"({ "extend": { "childs": [ "test_val_c" ] } })", &base );
        } ), Catch::Matchers::Contains( "Invalid or misplaced field name \"childs\"" ) );
        CHECK_THAT( capture_debugmsg_during( [&]() {
            load_node( "test_val_bad", R"({ "delete": { "condition": [ "npc_has_water" ] } })", &base );
        } ), Catch::Matchers::Contains( "Invalid or misplaced field name \"condition\"" ) );
    }
}

TEST_CASE( "behavior_delete_conditions", "[behavior]" )
{
    behavior::predicate_map["test_bt_tagged"] = []( const behavior::oracle_t *, std::string_view ) {
        return behavior::status_t::running;
    };
    behavior::predicates_with_argument.insert( "test_bt_tagged" );

    const behavior::node_t base = load_node( "test_val_base", R"({ "goal": "g", "conditions": [
        { "predicate": "test_bt_tagged", "argument": "a" },
        { "predicate": "npc_on_shift" },
        { "predicate": "test_bt_tagged", "argument": "b" },
        { "predicate": "npc_displaced_from_post", "invert_result": true } ] })" );
    CHECK( base.summary() == "if test_bt_tagged(a), npc_on_shift, test_bt_tagged(b), "
           "!npc_displaced_from_post; goal g" );
    // by name and argument: only that one
    CHECK( load_node( "test_val_one", R"({ "delete": { "conditions": [
        { "predicate": "test_bt_tagged", "argument": "b" } ] } })", &base ).summary() ==
           "if test_bt_tagged(a), npc_on_shift, !npc_displaced_from_post; goal g" );
    // by name: every one using the predicate
    CHECK( load_node( "test_val_all", R"({ "delete": { "conditions": [ "test_bt_tagged" ] } })",
                      &base ).summary() == "if npc_on_shift, !npc_displaced_from_post; goal g" );
    CHECK( load_node( "test_val_two", R"({ "delete": { "conditions": [ "npc_on_shift",
        "npc_displaced_from_post" ] } })", &base ).summary() ==
           "if test_bt_tagged(a), test_bt_tagged(b); goal g" );

    behavior::predicate_map.erase( "test_bt_tagged" );
    behavior::predicates_with_argument.erase( "test_bt_tagged" );
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
        // the link that closes the cycle is left out, so a tick ends; c has lost its only child,
        // so it fails, and the fallback goes on to the leaf
        CHECK( nodes[2].get_children().empty() );
        CHECK( nodes[2].tick( nullptr ).result == behavior::status_t::failure );
        behavior::tree tree;
        tree.add( &nodes[0] );
        CHECK( tree.tick( nullptr ) == "g" );
    }
    SECTION( "a node whose children are all unknown fails" ) {
        const std::vector<std::string> problems = link( nodes, {
            { "test_val_root", R"({ "strategy": "fallback", "children": [ "test_val_mid", "test_val_leaf" ] })" },
            { "test_val_mid", R"({ "strategy": "fallback", "children": [ "test_val_missing" ] })" },
            { "test_val_leaf", R"({ "goal": "g" })" }
        } );
        behavior::tree tree;
        tree.add( &nodes[0] );
        CHECK( tree.tick( nullptr ) == "g" );
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
    SECTION( "NPC and monster predicates in one tree" ) {
        const std::vector<std::string> problems = link( nodes, {
            { "test_val_root", R"({ "strategy": "fallback", "children": [ "test_val_npc", "test_val_monster" ] })" },
            { "test_val_npc", R"({ "conditions": [ { "predicate": "npc_has_target" } ], "goal": "n" })" },
            { "test_val_monster", R"({ "conditions": [ { "predicate": "monster_adjacent_plants" } ], "goal": "m" })" }
        } );
        CHECK( mentions( problems, "Behavior tree \"test_val_root\" uses NPC predicates or scores (in "
                         "\"test_val_npc\") and monster ones (in \"test_val_monster\")" ) );
        const std::string npc_only = R"({ "conditions": [ { "predicate": "npc_has_target" } ],
            "score": "npc_thirst_urgency", "goal": "n" })";
        CHECK( link( nodes, {
            { "test_val_root", R"({ "strategy": "utility", "children": [ "test_val_npc" ] })" },
            { "test_val_npc", npc_only }
        } ).empty() );
    }
}

// The real reporting path: a node loaded into the factory, finalization, then the load checks.
TEST_CASE( "behavior_problems_reported_after_finalization", "[behavior]" )
{
    const auto load_into_factory = []( const std::string & json, const std::string & src ) {
        const JsonObject jo = json_loader::from_string( json );
        behavior::load_behavior( jo, src );
    };
    // replace test_bt_reported, which nothing links to, from another source
    load_into_factory( R"({ "id": "test_bt_reported", "strategy": "fallback",
        "children": [ "test_bt_reported", "test_bt_reported_missing" ] })", "test_bt_override" );
    behavior::finalize();
    const std::string reported = capture_debugmsg_during( []() {
        behavior::check_consistency();
    } );
    // and put it back
    load_into_factory( R"({ "id": "test_bt_reported", "goal": "test_reported" })",
                       "test_bt_restore" );
    behavior::finalize();
    CHECK( capture_debugmsg_during( []() {
        behavior::check_consistency();
    } ).empty() );

    CHECK_THAT( reported, Catch::Matchers::Contains( "Behavior \"test_bt_reported\" lists child "
                "\"test_bt_reported\", which closes a cycle: test_bt_reported -> test_bt_reported." ) );
    CHECK_THAT( reported, Catch::Matchers::Contains( "Behavior \"test_bt_reported\" lists child "
                "\"test_bt_reported_missing\", which is not a behavior." ) );
    CHECK_THAT( reported, Catch::Matchers::Contains( "Behavior tree \"test_bt_reported\" reaches no "
                "leaf with a goal" ) );
    CHECK( behavior_node_t_test_bt_reported->goal() == "test_reported" );
}

// Conditions and math scores see an empty beta: npc_ conditions are false and n_ math reads 0.
TEST_CASE( "behavior_conditions_without_beta", "[npc][behavior]" )
{
    clear_map();
    npc &guy = plain_npc();
    guy.set_mutation( trait_IGNORE_SOUND );
    CHECK( decide( behavior_node_t_test_bt_beta_condition.obj(), guy ).first == "idle" );
    const auto [goal, score] = decide( behavior_node_t_test_bt_beta_math.obj(), guy );
    CHECK( goal == "test_beta_math" );
    CHECK( score == Approx( 1.0f ) );
    guy.unset_mutation( trait_IGNORE_SOUND );

    // in a monster's goals too
    monster &zombie = spawn_test_monster( "mon_zombie", tripoint_bub_ms( 55, 55, 0 ) );
    const behavior::monster_oracle_t monster_oracle( &zombie );
    CHECK( behavior_node_t_test_bt_beta_condition->tick( &monster_oracle ).result ==
           behavior::status_t::failure );
    CHECK( behavior_node_t_test_bt_beta_math->tick( &monster_oracle ).score == Approx( 1.0f ) );
    clear_creatures();
    clear_npcs();
}

// A built-in predicate or score given the other kind of oracle fails safely.
TEST_CASE( "behavior_predicates_for_another_oracle", "[npc][behavior]" )
{
    clear_map();
    npc &guy = plain_npc();
    monster &zombie = spawn_test_monster( "mon_zombie", tripoint_bub_ms( 55, 55, 0 ) );
    const behavior::character_oracle_t npc_oracle( &guy );
    const behavior::monster_oracle_t monster_oracle( &zombie );
    const behavior::node_t npc_node = load_node( "test_val_npc", R"({ "goal": "g",
        "conditions": [ { "predicate": "npc_should_flee", "invert_result": true } ],
        "score": "npc_thirst_urgency" })" );
    const behavior::node_t monster_node = load_node( "test_val_monster", R"({ "goal": "g",
        "conditions": [ { "predicate": "monster_not_hallucination" } ] })" );
    REQUIRE( npc_node.tick( &npc_oracle ).result == behavior::status_t::running );
    REQUIRE( monster_node.tick( &monster_oracle ).result == behavior::status_t::running );
    // npc_should_flee fails for a monster, so the inverted condition runs, and the score is 0
    behavior::behavior_return wrong = { behavior::status_t::failure, nullptr };
    CHECK_THAT( capture_debugmsg_during( [&]() {
        wrong = npc_node.tick( &monster_oracle );
    } ), Catch::Matchers::Contains( "\"npc_should_flee\" is for NPCs" ) );
    CHECK( wrong.score == 0.0f );
    CHECK_THAT( capture_debugmsg_during( [&]() {
        CHECK( monster_node.tick( &npc_oracle ).result == behavior::status_t::failure );
    } ), Catch::Matchers::Contains( "\"monster_not_hallucination\" is for monsters" ) );
    clear_creatures();
    clear_npcs();
}

TEST_CASE( "behavior_evaluate_every_needs_a_valid_id", "[npc][behavior]" )
{
    clear_map();
    npc &guy = plain_npc();
    CHECK( behavior::character_oracle_t( &guy ).cache_key() == guy.getID().get_value() );
    // diagnostic walks keep out of the cache
    CHECK_FALSE( behavior::character_oracle_t( &guy, false ).cache_key().has_value() );
    const character_id saved = guy.getID();
    guy.setID( character_id(), true );
    CHECK_FALSE( behavior::character_oracle_t( &guy ).cache_key().has_value() );
    guy.setID( saved, true );
    clear_npcs();
}

TEST_CASE( "is_in_faction_condition", "[npc][eoc]" )
{
    clear_map();
    npc &guy = plain_npc();
    avatar &you = get_avatar();
    const auto check = []( const std::string & json, const Creature & u, const Creature * beta ) {
        const JsonObject jo = json_loader::from_string( json );
        const conditional_t condition( jo );
        const const_dialogue d( get_const_talker_for( u ),
                                beta == nullptr ? nullptr : get_const_talker_for( *beta ) );
        return condition( d );
    };
    guy.set_fac( faction_your_followers );
    CHECK( check( R"({ "u_is_in_faction": "your_followers" })", guy, nullptr ) );
    CHECK_FALSE( check( R"({ "u_is_in_faction": "no_faction" })", guy, nullptr ) );
    CHECK( check( R"({ "npc_is_in_faction": "your_followers" })", you, &guy ) );
    guy.set_fac( faction_no_faction );
    CHECK_FALSE( check( R"({ "u_is_in_faction": "your_followers" })", guy, nullptr ) );
    CHECK( check( R"({ "u_is_in_faction": "no_faction" })", guy, nullptr ) );
    CHECK_FALSE( check( R"({ "npc_is_in_faction": "your_followers" })", you, &guy ) );
    // the avatar, and a faction given by a variable
    CHECK( check( R"({ "u_is_in_faction": "your_followers" })", you, nullptr ) );
    you.set_value( "bt_faction", std::string( "your_followers" ) );
    CHECK( check( R"({ "u_is_in_faction": { "u_val": "bt_faction" } })", you, nullptr ) );
    you.remove_value( "bt_faction" );
    // a monster is in no NPC faction
    monster &zombie = spawn_test_monster( "mon_zombie", tripoint_bub_ms( 55, 55, 0 ) );
    CHECK_FALSE( check( R"({ "u_is_in_faction": "your_followers" })", zombie, nullptr ) );
    clear_creatures();
    clear_npcs();
}
