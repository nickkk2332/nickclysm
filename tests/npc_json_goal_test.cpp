#include <algorithm>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "avatar.h"
#include "behavior.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "condition.h"
#include "coordinates.h"
#include "creature_tracker.h"
#include "debug.h"
#include "dialogue.h"
#include "errand.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "faction.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "global_vars.h"
#include "item.h"
#include "item_location.h"
#include "json.h"
#include "json_error.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser_diag_value.h"
#include "monster.h"
#include "npc.h"
#include "npc_arbitration.h"
#include "npc_decision_category.h"
#include "npc_goal_state.h"
#include "npc_json_goal.h"
#include "npc_turn_helpers.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "point.h"
#include "stats_tracker.h"
#include "talker.h"
#include "type_id.h"

// JSON goals (doc/JSON/NPC_BEHAVIOR.md): decision categories as JSON, goals defined on behavior
// leaves, what runs them in npc::move(), their saves, what EOCs read about them and the decision
// debug view.  The goals, trees and hooks are in data/mods/TEST_DATA/npc_goals.json; the hooks
// count their runs and record their context values in globals.

static decision_category category( const std::string &id )
{
    return decision_category( npc_decision_category_id( id ) );
}

TEST_CASE( "npc_decision_categories_keep_the_built_in_order", "[npc][npc_goal]" )
{
    // the closed enum this replaced, first to last
    const std::vector<std::string> built_in = {
        "combat", "investigate", "needs", "follow", "order", "duty", "camp_work", "camp_travel",
        "free_time", "idle", "unmodeled"
    };
    std::vector<std::string> resolved;
    for( const npc_decision_category_id &id : npc_decision_category::order() ) {
        if( std::find( built_in.begin(), built_in.end(), id.str() ) != built_in.end() ) {
            resolved.push_back( id.str() );
        }
    }
    CHECK( resolved == built_in );
    const std::vector<const decision_category *> constants = {
        &decision_category::combat, &decision_category::investigate, &decision_category::needs,
        &decision_category::follow, &decision_category::order, &decision_category::duty,
        &decision_category::camp_work, &decision_category::camp_travel,
        &decision_category::free_time, &decision_category::idle, &decision_category::unmodeled
    };
    for( size_t i = 0; i + 1 < constants.size(); ++i ) {
        CAPTURE( category_name( *constants[i] ), category_name( *constants[i + 1] ) );
        CHECK( *constants[i] < *constants[i + 1] );
        CHECK( constants[i]->id().str() == built_in[i] );
    }
    // the built-in goals keep their categories, now read from the JSON
    CHECK( bt_goal_to_category( "flee" ) == decision_category::combat );
    CHECK( bt_goal_to_category( "start_fire" ) == decision_category::needs );
    CHECK( bt_goal_to_category( "follow_embarked" ) == decision_category::follow );
    CHECK( bt_goal_to_category( "hold_position" ) == decision_category::duty );
    CHECK( bt_goal_to_category( "return_to_camp" ) == decision_category::camp_travel );
    CHECK( bt_goal_to_category( "idle" ) == decision_category::idle );
    for( const std::string &goal : builtin_npc_goals() ) {
        CAPTURE( goal );
        CHECK( bt_goal_to_category( goal ) != decision_category::unmodeled );
    }
    CHECK( decision_category::needs.hysteresis() == Approx( 0.1f ) );
}

TEST_CASE( "npc_decision_category_placed_before_and_after", "[npc][npc_goal]" )
{
    // TEST_DATA: test_goal_top before combat, test_goal_early before needs, test_goal_late
    // after free_time
    const std::vector<npc_decision_category_id> &order = npc_decision_category::order();
    const auto at = [&order]( const std::string & id ) {
        return std::find( order.begin(), order.end(), npc_decision_category_id( id ) ) - order.begin();
    };
    CHECK( at( "test_goal_top" ) == 0 );
    CHECK( at( "test_goal_top" ) + 1 == at( "combat" ) );
    CHECK( at( "test_goal_early" ) + 1 == at( "needs" ) );
    CHECK( at( "investigate" ) + 1 == at( "test_goal_early" ) );
    CHECK( at( "free_time" ) + 1 == at( "test_goal_late" ) );
    CHECK( at( "test_goal_late" ) + 1 == at( "idle" ) );
    CHECK( category( "test_goal_early" ).hysteresis() == Approx( 0.5f ) );
    CHECK( category( "test_goal_late" ).hysteresis() == Approx( 0.1f ) );
    CHECK( bt_goal_to_category( "test_goal_wait" ) == category( "test_goal_late" ) );
    CHECK( bt_goal_to_category( "test_goal_wait_top" ) == category( "test_goal_top" ) );

    SECTION( "resolving an order" ) {
        const auto id = []( const char *name ) {
            return npc_decision_category_id( name );
        };
        std::vector<std::string> errors;
        const std::vector<npc_decision_category_id> resolved = resolve_category_order( {
            { id( "a" ), std::nullopt, std::nullopt },
            { id( "x" ), std::nullopt, id( "a" ) },
            { id( "b" ), std::nullopt, std::nullopt },
            { id( "y" ), std::nullopt, id( "a" ) },
            { id( "z" ), id( "a" ), std::nullopt },
            { id( "w" ), std::nullopt, id( "y" ) },
        }, errors );
        // after a in load order, w after y, z before a
        CHECK( resolved == std::vector<npc_decision_category_id> {
            id( "z" ), id( "a" ), id( "x" ), id( "y" ), id( "w" ), id( "b" )
        } );
        CHECK( errors.empty() );

        resolve_category_order( {
            { id( "a" ), std::nullopt, std::nullopt },
            { id( "u" ), id( "nowhere" ), std::nullopt },
            { id( "s" ), std::nullopt, id( "s" ) },
            { id( "p" ), std::nullopt, id( "q" ) },
            { id( "q" ), id( "p" ), std::nullopt },
        }, errors );
        REQUIRE( errors.size() == 4 );
        CHECK( errors[0].find( "doesn't exist" ) != std::string::npos );
        CHECK( errors[1].find( "itself" ) != std::string::npos );
        CHECK( errors[2].find( "loop" ) != std::string::npos );
    }
    SECTION( "a category goes before or after, not both" ) {
        npc_decision_category cat;
        const JsonObject jo = json_loader::from_string(
                                  R"({ "id": "x", "before": "needs", "after": "combat" })" );
        jo.allow_omitted_members();
        CHECK_THROWS_AS( cat.load( jo, "test" ), JsonError );
    }
}

static const faction_id faction_your_followers( "your_followers" );

static const itype_id itype_backpack( "backpack" );
static const itype_id itype_bottle_plastic( "bottle_plastic" );
static const itype_id itype_crowbar( "crowbar" );
static const itype_id itype_tshirt( "tshirt" );
static const itype_id itype_water_clean( "water_clean" );

static const mtype_id mon_zombie( "mon_zombie" );

static constexpr tripoint_bub_ms goal_origin{ 50, 50, 0 };

static const std::vector<std::string> goal_globals = {
    "test_goal_starts", "test_goal_start_id", "test_goal_turns", "test_goal_turns_active",
    "test_goal_ends", "test_goal_end_reason", "test_goal_end_id", "test_goal_end_turns",
    "test_goal_interrupts", "test_goal_interrupt_reason", "test_goal_timeouts",
    "test_goal_timeout_reason", "test_goal_spot", "test_goal_listen", "test_goal_changed_old",
    "test_goal_changed_new", "test_goal_changed_u", "test_goal_name", "test_goal_end_pos"
};

static void forget_goal_globals()
{
    for( const std::string &name : goal_globals ) {
        get_globals().remove_global_value( name );
    }
}

static std::string global_string( const std::string &name )
{
    const diag_value *const value = get_globals().maybe_get_global_value( name );
    return value == nullptr ? std::string() : value->str();
}

static double global_number( const std::string &name )
{
    const diag_value *const value = get_globals().maybe_get_global_value( name );
    return value == nullptr ? 0.0 : value->dbl();
}

// where the NPC stood when on_end ran
static std::optional<tripoint_bub_ms> end_pos()
{
    const diag_value *const value = get_globals().maybe_get_global_value( "test_goal_end_pos" );
    if( value == nullptr || !value->is_tripoint() ) {
        return std::nullopt;
    }
    return get_map().get_bub( value->tripoint() );
}

// npc::move() walks this tree while the test runs; the globals go too
struct goal_test_tree {
    explicit goal_test_tree( const std::string &root ) {
        forget_goal_globals();
        set_npc_decision_tree_for_test( string_id<behavior::node_t>( root ) );
    }
    goal_test_tree( const goal_test_tree & ) = delete;
    goal_test_tree &operator=( const goal_test_tree & ) = delete;
    ~goal_test_tree() {
        set_npc_decision_tree_for_test( std::nullopt );
        forget_goal_globals();
    }
    static void use( const std::string &root ) {
        set_npc_decision_tree_for_test( string_id<behavior::node_t>( root ) );
    }
};

// nothing pressing: needs met, no post, no task
static void calm( npc &guy )
{
    guy.set_hunger( 0 );
    guy.set_thirst( 0 );
    guy.set_sleepiness( 0 );
    guy.set_stored_kcal( guy.get_healthy_kcal() );
    guy.set_all_parts_temp_conv( BODYTEMP_NORM );
    guy.set_all_parts_temp_cur( BODYTEMP_NORM );
    guy.set_mission( NPC_MISSION_NULL );
    guy.guard_pos = std::nullopt;
    guy.clear_ai_guard_pos();
}

// an NPC alone in an empty field, the avatar out of its way
static npc &goal_npc()
{
    clear_map_with_vision();
    clear_avatar();
    clear_npcs();
    set_time_to_day();
    map &here = get_map();
    get_avatar().setpos( here, goal_origin + tripoint( -20, 0, 0 ) );
    npc &guy = spawn_npc( goal_origin.xy(), "test_talker" );
    clear_character( guy, true );
    guy.setpos( here, goal_origin );
    calm( guy );
    here.build_map_cache( 0 );
    return guy;
}

// a follower close by, armed: it fights what comes near (an unarmed NPC rates no attack)
static npc &armed_follower()
{
    npc &guy = goal_npc();
    guy.set_fac( faction_your_followers );
    guy.set_attitude( NPCATT_FOLLOW );
    guy.rules.set_flag( ally_rule::follow_close );
    guy.set_wielded_item( item( itype_crowbar ) );
    get_avatar().setpos( get_map(), goal_origin + tripoint( -2, 0, 0 ) );
    get_map().build_map_cache( 0 );
    REQUIRE( guy.is_player_ally() );
    return guy;
}

// saves the NPC and loads it back; what isn't saved starts over, as in a new session
static void save_and_load( npc &guy )
{
    std::ostringstream os;
    JsonOut jsout( os );
    guy.serialize( jsout );
    guy.walk.reset();
    guy.path.clear();
    guy.set_committed_goal_for_test( "" );
    *guy.json_goal = npc_goal_state();
    guy.setID( character_id(), true );
    guy.deserialize( json_loader::from_string( os.str() ).get_object() );
}

// loads behavior node `node` into a registry of its own
static bool load_goal_node( npc_goal_registry &reg, const std::string &node,
                            const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    jo.allow_omitted_members();
    std::string goal;
    const bool loaded = reg.load_node( jo, string_id<behavior::node_t>( node ), "test", goal );
    finalize_conditions();
    return loaded;
}

static bool has_error( const std::vector<std::string> &errors, const std::string &part )
{
    return std::any_of( errors.begin(), errors.end(), [&part]( const std::string & e ) {
        return e.find( part ) != std::string::npos;
    } );
}

TEST_CASE( "npc_json_goal_load_errors", "[npc][npc_goal]" )
{
    npc_goal_registry reg;
    // a plain goal string is no JSON goal
    CHECK_FALSE( load_goal_node( reg, "n0", R"({ "goal": "follow_player" })" ) );
    CHECK( load_goal_node( reg, "n1",
                           R"({ "category": "needs", "goal": { "id": "test_load_goal", "timeout": "2 hours" } })" ) );
    REQUIRE( reg.find( "test_load_goal" ) != nullptr );
    CHECK( reg.find( "test_load_goal" )->timeout == 2_hours );
    CHECK( reg.check().empty() );

    // a built-in goal's id
    CHECK_THROWS_AS( load_goal_node( reg, "n2",
                                     R"({ "category": "needs", "goal": { "id": "drink_water" } })" ), JsonError );
    // no category
    CHECK_THROWS_AS( load_goal_node( reg, "n3", R"({ "goal": { "id": "test_load_other" } })" ),
                     JsonError );
    // defined by another node already
    CHECK_THROWS_AS( load_goal_node( reg, "n4",
                                     R"({ "category": "needs", "goal": { "id": "test_load_goal" } })" ), JsonError );
    // a category with no goal, a timeout of nothing
    CHECK_THROWS_AS( load_goal_node( reg, "n5", R"({ "category": "needs" })" ), JsonError );
    CHECK_THROWS_AS( load_goal_node( reg, "n6",
                                     R"({ "category": "needs", "goal": { "id": "test_load_x", "timeout": 0 } })" ), JsonError );
    // a node loaded again replaces what it defined
    CHECK( load_goal_node( reg, "n1",
                           R"({ "category": "follow", "goal": { "id": "test_load_goal" } })" ) );
    CHECK( reg.find( "test_load_goal" )->category == npc_decision_category_id( "follow" ) );
    CHECK( reg.check().empty() );

    SECTION( "an unknown category" ) {
        CHECK( load_goal_node( reg, "n7",
                               R"({ "category": "no_such_category", "goal": { "id": "test_load_lost" } })" ) );
        CHECK( has_error( reg.check(), "no_such_category, which doesn't exist" ) );
    }
    SECTION( "one goal given two categories" ) {
        // by two nodes
        CHECK_FALSE( load_goal_node( reg, "n8",
                                     R"({ "category": "needs", "goal": "test_load_goal" })" ) );
        CHECK( has_error( reg.check(), "goal test_load_goal is given two categories" ) );
    }
    SECTION( "a built-in goal given another category" ) {
        CHECK_FALSE( load_goal_node( reg, "n9", R"({ "category": "needs", "goal": "fight" })" ) );
        CHECK( has_error( reg.check(), "goal fight is given two categories" ) );
    }
    SECTION( "the same category twice is no error" ) {
        CHECK_FALSE( load_goal_node( reg, "n10", R"({ "category": "combat", "goal": "fight" })" ) );
        CHECK( reg.check().empty() );
    }
    SECTION( "a hook that doesn't exist" ) {
        CHECK( load_goal_node( reg, "n11",
                               R"({ "category": "needs", "goal": { "id": "test_load_hook", "on_turn": [ "EOC_NO_SUCH_HOOK" ] } })" ) );
        CHECK( has_error( reg.check(), "EOC_NO_SUCH_HOOK, which doesn't exist" ) );
    }
}

TEST_CASE( "npc_json_goal_hysteresis_rules", "[npc][npc_goal]" )
{
    const auto interrupts = []( const std::string & current, float current_score,
    const std::string & challenger, float challenger_score ) {
        npc_pursuit pursuit;
        pursuit.goal = current;
        pursuit.selection = current_score;
        return should_interrupt( pursuit, { challenger, challenger_score, false } );
    };
    // within a category, 10% by default: 0.5 holds against up to 0.55
    CHECK_FALSE( interrupts( "test_goal_hysteresis_a", 0.5f, "test_goal_hysteresis_b", 0.54f ) );
    CHECK( interrupts( "test_goal_hysteresis_a", 0.5f, "test_goal_hysteresis_b", 0.56f ) );
    // 50% for test_goal_early: 0.4 holds against up to 0.6
    CHECK_FALSE( interrupts( "test_goal_wait_early", 0.4f, "test_goal_wait_early_b", 0.59f ) );
    CHECK( interrupts( "test_goal_wait_early", 0.4f, "test_goal_wait_early_b", 0.61f ) );
    // a JSON goal and a built-in one of its category weigh scores the same way, not by the
    // margins between two needs (0.5 would hold against 0.6 there)
    CHECK_FALSE( interrupts( "drink_water", 0.5f, "test_goal_need", 0.54f ) );
    CHECK( interrupts( "drink_water", 0.5f, "test_goal_need", 0.56f ) );
    CHECK( interrupts( "test_goal_need", 0.5f, "drink_water", 0.6f ) );
    CHECK_FALSE( interrupts( "eat_food", 0.5f, "drink_water", 0.6f ) );
    // across categories the order decides, whatever the scores
    CHECK( interrupts( "test_goal_wait", 0.9f, "drink_water", 0.01f ) );
    CHECK_FALSE( interrupts( "drink_water", 0.01f, "test_goal_wait", 1.0f ) );
    CHECK_FALSE( interrupts( "test_goal_wait_early", 0.0f, "drink_water", 1.0f ) );
    CHECK( interrupts( "drink_water", 1.0f, "test_goal_wait_early", 0.0f ) );
    CHECK_FALSE( interrupts( "test_goal_wait_top", 0.0f, "fight", 1.0f ) );
    // a later category, idle included, never takes over
    CHECK_FALSE( interrupts( "test_goal_wait", 0.0f, "idle", 1.0f ) );
    npc_pursuit waiting;
    waiting.goal = "test_goal_wait";
    CHECK( std::string( judge_interrupt( waiting, { "idle", 1.0f, true } ).why ) ==
           "its category comes later" );
}

// the NPC on the goal the tree picks now
static void adopt( npc &guy, const std::string &goal )
{
    run_npc_turn( guy );
    REQUIRE( guy.get_committed_goal() == goal );
    REQUIRE( guy.json_goal->goal == goal );
}

TEST_CASE( "npc_json_goal_errand_walks_picks_up_and_returns", "[npc][npc_goal][errand]" )
{
    npc &guy = goal_npc();
    goal_test_tree tree( "test_goal_errand_tree" );
    guy.wear_item( item( itype_backpack ), false );
    map &here = get_map();
    const tripoint_bub_ms spot = goal_origin + tripoint( 6, 2, 0 );
    here.add_item( spot, item( itype_tshirt ) );
    get_globals().set_global_value( "test_goal_spot", here.get_abs( spot ) );

    adopt( guy, "test_goal_fetch" );
    CHECK( global_number( "test_goal_starts" ) == 1 );
    CHECK( global_string( "test_goal_start_id" ) == "test_goal_fetch" );
    // on_start sent it for the t-shirt, and the walk is the goal's own
    REQUIRE( guy.walk );
    CHECK( guy.walk->kind == walk_kind::pickup );
    CHECK( npc_goal_runtime::owns_walk( guy ) );
    const tripoint_bub_ms start = goal_origin;
    CHECK( guy.pos_bub() != start );

    // the walk counts as a player order to the tree; it doesn't take over from the goal
    bool walked_as_the_goal = true;
    const int turns = run_npc_turns( guy, 60, [&]( const npc & n ) {
        if( n.json_goal->goal != "test_goal_fetch" ) {
            return true;
        }
        walked_as_the_goal &= n.get_committed_goal() == "test_goal_fetch" ||
                              n.get_committed_goal().empty();
        return false;
    } );
    CAPTURE( turns );
    CHECK( walked_as_the_goal );
    CHECK( guy.has_item_with( []( const item & it ) {
        return it.typeId() == itype_tshirt;
    } ) );
    // back where it started when the goal ended
    CHECK( end_pos() == start );
    CHECK( global_number( "test_goal_ends" ) == 1 );
    CHECK( global_string( "test_goal_end_reason" ) == "done" );
    CHECK( global_string( "test_goal_end_id" ) == "test_goal_fetch" );
    CHECK( global_number( "test_goal_interrupts" ) == 0 );
    CHECK( global_number( "test_goal_starts" ) == 1 );
    CHECK_FALSE( guy.json_goal->active() );
    // carrying a t-shirt it could wear, the tree doesn't pick the goal again
    run_npc_turns( guy, 3 );
    CHECK( global_number( "test_goal_starts" ) == 1 );
    CHECK( guy.get_committed_goal() != "test_goal_fetch" );
}

TEST_CASE( "npc_json_goal_errand_resumes_after_a_load", "[npc][npc_goal][errand]" )
{
    npc &guy = goal_npc();
    goal_test_tree tree( "test_goal_errand_tree" );
    guy.wear_item( item( itype_backpack ), false );
    map &here = get_map();
    const tripoint_bub_ms spot = goal_origin + tripoint( 8, 0, 0 );
    here.add_item( spot, item( itype_tshirt ) );
    get_globals().set_global_value( "test_goal_spot", here.get_abs( spot ) );
    adopt( guy, "test_goal_fetch" );
    run_npc_turns( guy, 2 );
    REQUIRE( guy.walk );
    const npc_goal_state before = *guy.json_goal;
    REQUIRE( before.turns_active > 0 );

    save_and_load( guy );
    CHECK( guy.json_goal->goal == "test_goal_fetch" );
    CHECK( guy.json_goal->started == before.started );
    CHECK( guy.json_goal->turns_active == before.turns_active );
    // the walk came back with the goal's context, so it's still the goal's own
    REQUIRE( guy.walk );
    CHECK( npc_goal_runtime::owns_walk( guy ) );
    // nothing committed yet, but the NPC is on the goal
    CHECK( guy.get_committed_goal().empty() );
    CHECK( npc_goal_runtime::current_goal( guy ) == "test_goal_fetch" );

    run_npc_turn( guy );
    CHECK( guy.get_committed_goal() == "test_goal_fetch" );
    CHECK( guy.get_intention().since == before.started );
    run_npc_turns( guy, 60, []( const npc & n ) {
        return !n.json_goal->active();
    } );
    CHECK( end_pos() == goal_origin );
    CHECK( global_string( "test_goal_end_reason" ) == "done" );
    // on_start ran once, before the save
    CHECK( global_number( "test_goal_starts" ) == 1 );
}

TEST_CASE( "npc_json_goal_until_timeout_and_end_hooks", "[npc][npc_goal]" )
{
    npc &guy = goal_npc();
    SECTION( "until" ) {
        goal_test_tree tree( "test_goal_until_tree" );
        adopt( guy, "test_goal_until" );
        // the goal goes on alone, whatever the tree picks
        goal_test_tree::use( "test_goal_idle_tree" );
        run_npc_turns( guy, 3 );
        CHECK( guy.get_committed_goal() == "test_goal_until" );
        CHECK( global_number( "test_goal_turns" ) == 4 );
        CHECK( global_number( "test_goal_turns_active" ) == 4 );
        CHECK( guy.json_goal->turns_active == 4 );
        CHECK( global_number( "test_goal_ends" ) == 0 );
        guy.set_value( "test_goal_finish", 1.0 );
        run_npc_turn( guy );
        CHECK_FALSE( guy.json_goal->active() );
        CHECK( guy.get_committed_goal().empty() );
        CHECK( global_number( "test_goal_ends" ) == 1 );
        CHECK( global_string( "test_goal_end_reason" ) == "done" );
        CHECK( global_number( "test_goal_end_turns" ) == 4 );
        CHECK( global_number( "test_goal_turns" ) == 4 );
        CHECK( global_number( "test_goal_interrupts" ) == 0 );
        CHECK( global_number( "test_goal_timeouts" ) == 0 );
        guy.remove_value( "test_goal_finish" );
    }
    SECTION( "timeout" ) {
        goal_test_tree tree( "test_goal_timeout_tree" );
        adopt( guy, "test_goal_timeout" );
        goal_test_tree::use( "test_goal_idle_tree" );
        const time_point started = guy.json_goal->started;
        run_npc_turns( guy, 10, []( const npc & n ) {
            return !n.json_goal->active();
        } );
        CHECK_FALSE( guy.json_goal->active() );
        CHECK( calendar::turn - started == 6_turns );
        CHECK( global_number( "test_goal_timeouts" ) == 1 );
        CHECK( global_string( "test_goal_timeout_reason" ) == "timeout" );
        CHECK( global_number( "test_goal_ends" ) == 1 );
        CHECK( global_string( "test_goal_end_reason" ) == "timeout" );
        CHECK( global_number( "test_goal_interrupts" ) == 0 );
    }
    SECTION( "a talk function that clears the goal interrupts it" ) {
        goal_test_tree tree( "test_goal_until_tree" );
        adopt( guy, "test_goal_until" );
        guy.clear_committed_goal();
        CHECK_FALSE( guy.json_goal->active() );
        CHECK( global_number( "test_goal_interrupts" ) == 1 );
        CHECK( global_string( "test_goal_interrupt_reason" ) == "interrupted" );
        CHECK( global_string( "test_goal_end_reason" ) == "interrupted" );
        CHECK( global_number( "test_goal_timeouts" ) == 0 );
    }
}

TEST_CASE( "npc_json_goal_waits_out_turns_it_has_nothing_to_do", "[npc][npc_goal]" )
{
    npc &guy = goal_npc();
    goal_test_tree tree( "test_goal_wait_tree" );
    adopt( guy, "test_goal_wait" );
    // a goal that gives the NPC nothing to do still ends its turns
    const int turns = run_npc_turns( guy, 5 );
    CHECK( turns == 5 );
    CHECK( guy.pos_bub() == goal_origin );
    CHECK( global_number( "test_goal_turns" ) == 6 );
    CHECK( global_number( "test_goal_starts" ) == 1 );
}

TEST_CASE( "npc_json_goal_interrupted_by_a_need", "[npc][npc_goal]" )
{
    npc &guy = goal_npc();
    const bool early = GENERATE( false, true );
    CAPTURE( early );
    const std::string goal = early ? "test_goal_wait_early" : "test_goal_wait";
    goal_test_tree tree( early ? "test_goal_wait_early_tree" : "test_goal_wait_tree" );
    adopt( guy, goal );
    // thirsty, with water at hand: the tree picks drink_water
    guy.wear_item( item( itype_backpack ), false );
    item bottle( itype_bottle_plastic );
    REQUIRE( bottle.put_in( item( itype_water_clean, calendar::turn, 2 ),
                            pocket_type::CONTAINER ).success() );
    REQUIRE( guy.i_add( bottle ) );
    guy.set_thirst( 700 );
    run_npc_turn( guy );
    if( early ) {
        // test_goal_early comes before needs
        CHECK( guy.get_committed_goal() == goal );
        CHECK( global_number( "test_goal_interrupts" ) == 0 );
    } else {
        CHECK( guy.get_committed_goal() == "drink_water" );
        CHECK_FALSE( guy.json_goal->active() );
        CHECK( global_number( "test_goal_interrupts" ) == 1 );
        CHECK( global_string( "test_goal_interrupt_reason" ) == "interrupted" );
        CHECK( global_number( "test_goal_ends" ) == 1 );
        CHECK( global_string( "test_goal_end_reason" ) == "interrupted" );
    }
}

TEST_CASE( "npc_json_goal_interrupted_by_combat", "[npc][npc_goal]" )
{
    npc &guy = armed_follower();
    const bool top = GENERATE( false, true );
    CAPTURE( top );
    const std::string goal = top ? "test_goal_wait_top" : "test_goal_wait";
    goal_test_tree tree( top ? "test_goal_wait_top_tree" : "test_goal_wait_tree" );
    adopt( guy, goal );
    map &here = get_map();
    monster *zed = g->place_critter_at( mon_zombie, guy.pos_bub() + tripoint( 0, 3, 0 ) );
    REQUIRE( zed != nullptr );
    here.build_map_cache( 0 );
    REQUIRE( guy.sees( here, *zed ) );
    run_npc_turns( guy, 2 );
    if( top ) {
        // test_goal_top comes before combat: the NPC stays on its goal
        CHECK( guy.get_committed_goal() == goal );
        CHECK( global_number( "test_goal_interrupts" ) == 0 );
        CHECK( global_number( "test_goal_turns" ) == 3 );
    } else {
        CHECK_FALSE( guy.json_goal->active() );
        CHECK( guy.get_committed_goal() != goal );
        CHECK( global_number( "test_goal_interrupts" ) == 1 );
        CHECK( global_string( "test_goal_end_reason" ) == "interrupted" );
    }
    clear_creatures();
}

TEST_CASE( "npc_json_goal_before_combat_is_taken_up_in_a_fight", "[npc][npc_goal]" )
{
    npc &guy = armed_follower();
    goal_test_tree tree( "test_goal_wait_top_tree" );
    map &here = get_map();
    monster *zed = g->place_critter_at( mon_zombie, guy.pos_bub() + tripoint( 0, 3, 0 ) );
    REQUIRE( zed != nullptr );
    here.build_map_cache( 0 );
    run_npc_turn( guy );
    CHECK( guy.get_committed_goal() == "test_goal_wait_top" );
    CHECK( global_number( "test_goal_starts" ) == 1 );
    clear_creatures();
}

TEST_CASE( "npc_json_goal_hysteresis", "[npc][npc_goal]" )
{
    npc &guy = goal_npc();
    goal_test_tree tree( "test_goal_hysteresis_tree" );
    // a's score is the hunger urgency (0.3 here), b's the thirst urgency
    guy.set_stored_kcal( guy.get_healthy_kcal() * 7 / 10 );
    guy.set_thirst( 0 );
    adopt( guy, "test_goal_hysteresis_a" );
    // slightly higher: no more than 10% above a's
    guy.set_thirst( 1200 * 32 / 100 );
    run_npc_turns( guy, 2 );
    CHECK( guy.get_committed_goal() == "test_goal_hysteresis_a" );
    CHECK( global_number( "test_goal_interrupts" ) == 0 );
    // clearly higher
    guy.set_thirst( 1200 * 40 / 100 );
    run_npc_turn( guy );
    CHECK( guy.get_committed_goal() == "test_goal_hysteresis_b" );
    CHECK( global_number( "test_goal_interrupts" ) == 1 );
    CHECK( global_string( "test_goal_end_id" ) == "test_goal_hysteresis_a" );
    CHECK( global_number( "test_goal_starts" ) == 1 );
}

TEST_CASE( "npc_json_goal_saved_mid_goal", "[npc][npc_goal]" )
{
    npc &guy = goal_npc();
    goal_test_tree tree( "test_goal_until_tree" );
    adopt( guy, "test_goal_until" );
    run_npc_turns( guy, 2 );
    const npc_goal_state before = *guy.json_goal;
    REQUIRE( before.turns_active == 3 );

    save_and_load( guy );
    CHECK( guy.json_goal->goal == "test_goal_until" );
    CHECK( guy.json_goal->started == before.started );
    CHECK( guy.json_goal->turns_active == 3 );
    CHECK( guy.json_goal->last_turn == before.last_turn );

    run_npc_turns( guy, 2 );
    CHECK( guy.get_committed_goal() == "test_goal_until" );
    // on_start doesn't run again; on_turn goes on counting
    CHECK( global_number( "test_goal_starts" ) == 1 );
    CHECK( guy.json_goal->turns_active == 5 );
    CHECK( global_number( "test_goal_turns_active" ) == 5 );
    CHECK( global_number( "test_goal_interrupts" ) == 0 );
    CHECK( guy.get_intention().since == before.started );
}

static bool test_condition( const std::string &condition, const_dialogue const &d )
{
    const JsonObject jo = json_loader::from_string( R"({ "condition": )" + condition + " }" );
    jo.allow_omitted_members();
    conditional_t::func cond;
    read_condition( jo, "condition", cond, false );
    finalize_conditions();
    return cond( d );
}

TEST_CASE( "npc_goal_conditions_and_math", "[npc][npc_goal][eoc]" )
{
    npc &guy = goal_npc();
    goal_test_tree tree( "test_goal_until_tree" );
    const dialogue d( get_talker_for( guy ), nullptr );
    CHECK( test_condition( R"({ "u_npc_goal": "idle" })", d ) );
    CHECK( test_condition( R"({ "npc_goal_category": "idle" })", d ) );
    CHECK( test_condition( R"({ "math": [ "npc_goal_turns() == 0" ] })", d ) );

    adopt( guy, "test_goal_until" );
    run_npc_turns( guy, 3 );
    CHECK( test_condition( R"({ "u_npc_goal": "test_goal_until" })", d ) );
    CHECK_FALSE( test_condition( R"({ "u_npc_goal": "idle" })", d ) );
    get_globals().set_global_value( "test_goal_name", "test_goal_until" );
    CHECK( test_condition( R"({ "u_npc_goal": { "global_val": "test_goal_name" } })", d ) );
    CHECK( test_condition( R"({ "npc_goal_category": "test_goal_late" })", d ) );
    CHECK_FALSE( test_condition( R"({ "npc_goal_category": "needs" })", d ) );
    // taken up four turns ago
    CHECK( test_condition( R"({ "math": [ "npc_goal_turns() == 4" ] })", d ) );
    CHECK( test_condition( R"({ "math": [ "u_npc_goal_turns() == 4" ] })", d ) );

    // the NPC as the beta talker
    const dialogue with_npc( get_talker_for( get_avatar() ), get_talker_for( guy ) );
    CHECK( test_condition( R"({ "npc_npc_goal": "test_goal_until" })", with_npc ) );
    CHECK_FALSE( test_condition( R"({ "u_npc_goal": "test_goal_until" })", with_npc ) );
    CHECK( test_condition( R"({ "math": [ "n_npc_goal_turns() == 4" ] })", with_npc ) );
    CHECK( test_condition( R"({ "math": [ "u_npc_goal_turns() == 0" ] })", with_npc ) );

    // built-in goals too
    guy.set_committed_goal_for_test( "drink_water" );
    CHECK( test_condition( R"({ "npc_goal_category": "needs" })", d ) );
    CHECK( test_condition( R"({ "u_npc_goal": "drink_water" })", d ) );
    guy.set_committed_goal_for_test( "" );
    *guy.json_goal = npc_goal_state();
}

namespace
{
struct goal_change_recorder : public event_subscriber {
    using event_subscriber::notify;
    std::vector<cata::event> changes;
    void notify( const cata::event &e ) override {
        if( e.type() == event_type::npc_goal_changed ) {
            changes.push_back( e );
        }
    }
};
} // namespace

TEST_CASE( "npc_goal_changed_event", "[npc][npc_goal][event]" )
{
    npc &guy = goal_npc();
    goal_test_tree tree( "test_goal_until_tree" );
    goal_change_recorder recorder;
    get_event_bus().subscribe( &recorder );
    get_globals().set_global_value( "test_goal_listen", 1.0 );
    adopt( guy, "test_goal_until" );
    REQUIRE( recorder.changes.size() == 1 );
    const cata::event &started = recorder.changes.front();
    CHECK( started.get<cata_variant_type::character_id>( "npc" ) == guy.getID() );
    CHECK( started.get<cata_variant_type::string>( "old_goal" ) == "idle" );
    CHECK( started.get<cata_variant_type::string>( "new_goal" ) == "test_goal_until" );
    // an EOC of the event runs with the NPC as u
    CHECK( global_string( "test_goal_changed_old" ) == "idle" );
    CHECK( global_string( "test_goal_changed_new" ) == "test_goal_until" );
    CHECK( global_string( "test_goal_changed_u" ) == guy.get_name() );

    // no change, no event
    run_npc_turns( guy, 2 );
    CHECK( recorder.changes.size() == 1 );

    goal_test_tree::use( "test_goal_idle_tree" );
    guy.set_value( "test_goal_finish", 1.0 );
    run_npc_turn( guy );
    REQUIRE( recorder.changes.size() == 2 );
    CHECK( recorder.changes.back().get<cata_variant_type::string>( "old_goal" ) == "test_goal_until" );
    CHECK( recorder.changes.back().get<cata_variant_type::string>( "new_goal" ) == "idle" );
    guy.remove_value( "test_goal_finish" );
    get_event_bus().unsubscribe( &recorder );
}

TEST_CASE( "npc_decision_report", "[npc][npc_goal]" )
{
    npc &guy = goal_npc();
    goal_test_tree tree( "test_goal_hysteresis_tree" );
    guy.set_stored_kcal( guy.get_healthy_kcal() * 7 / 10 );
    guy.set_thirst( 0 );
    adopt( guy, "test_goal_hysteresis_a" );
    guy.set_thirst( 1200 * 32 / 100 );

    const npc_decision_report rep = npc_goal_runtime::report( guy );
    REQUIRE( rep.nodes.size() == 3 );
    CHECK( rep.nodes[0].id == "test_goal_hysteresis_tree" );
    CHECK( rep.nodes[0].picked );
    CHECK( rep.nodes[1].id == "test_goal_hysteresis_a" );
    CHECK( rep.nodes[1].depth == 1 );
    CHECK( rep.nodes[1].goal == "test_goal_hysteresis_a" );
    CHECK( rep.nodes[1].category == "test_goal_late" );
    CHECK( rep.nodes[1].result == "running" );
    REQUIRE( rep.nodes[1].score );
    CHECK( *rep.nodes[1].score == Approx( 0.3f ).margin( 0.01f ) );
    CHECK_FALSE( rep.nodes[1].picked );
    CHECK( rep.nodes[2].picked );
    CHECK( rep.picked_goal == "test_goal_hysteresis_b" );
    CHECK( rep.committed_goal == "test_goal_hysteresis_a" );
    CHECK( rep.committed_category == "test_goal_late" );
    CHECK( rep.committed_score == Approx( 0.3f ).margin( 0.01f ) );
    CHECK( rep.verdict.find( "doesn't take over" ) != std::string::npos );
    CHECK( rep.verdict.find( "hysteresis" ) != std::string::npos );
    REQUIRE_FALSE( rep.categories.empty() );
    CHECK( rep.categories.front().id == "test_goal_top" );

    // conditions, true or false
    goal_test_tree::use( "test_goal_errand_tree" );
    const npc_decision_report errand = npc_goal_runtime::report( guy );
    const auto fetch = std::find_if( errand.nodes.begin(), errand.nodes.end(),
    []( const npc_decision_report::node_line & line ) {
        return line.id == "test_goal_fetch";
    } );
    REQUIRE( fetch != errand.nodes.end() );
    REQUIRE( fetch->conditions.size() == 1 );
    CHECK( fetch->conditions[0].label == "not npc_can_wear_warmer_clothes" );
    CHECK( fetch->conditions[0].holds );
    // the report changes nothing
    CHECK( guy.get_committed_goal() == "test_goal_hysteresis_a" );
    CHECK( global_number( "test_goal_starts" ) == 0 );
}

// a goal change is no statistic: nothing is kept in the save
TEST_CASE( "npc_goal_changed_isnt_kept_for_statistics", "[npc][npc_goal][stats]" )
{
    npc &guy = goal_npc();
    goal_test_tree tree( "test_goal_until_tree" );
    get_globals().set_global_value( "test_goal_listen", 1.0 );
    const int before = get_stats().get_events( event_type::npc_goal_changed ).count();
    adopt( guy, "test_goal_until" );
    // the event went out
    CHECK( global_string( "test_goal_changed_new" ) == "test_goal_until" );
    CHECK( get_stats().get_events( event_type::npc_goal_changed ).count() == before );
    CHECK( before == 0 );
    CHECK_FALSE( stats_tracker::records( event_type::npc_goal_changed ) );
    CHECK( stats_tracker::records( event_type::u_var_changed ) );
}
