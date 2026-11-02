#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "action.h"
#include "activity_type.h"
#include "avatar.h"
#include "avatar_action.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "condition.h"
#include "coordinates.h"
#include "dialogue.h"
#include "errand.h"
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
#include "map_selector.h"
#include "math_parser_diag_value.h"
#include "monster.h"
#include "npc.h"
#include "npc_turn_helpers.h"
#include "npctalk.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "point.h"
#include "talker.h"
#include "type_id.h"

// NPC errands from EOCs (src/errand.h): u_walk_to, u_pickup_item and the other item effects,
// the conditions that go with them, and the NPC movement fixes they rest on (NPC-B14, NPC-B17).
// The true_eocs and false_eocs used here (data/mods/TEST_DATA/errand_eoc.json) record the
// outcome, the reason and the talkers in globals.

static const activity_id ACT_ADD_VARIABLE_COMPLETE( "ACT_ADD_VARIABLE_COMPLETE" );

static const faction_id faction_tacoma_commune( "tacoma_commune" );
static const faction_id faction_your_followers( "your_followers" );

static const flag_id json_flag_NO_UNWIELD( "NO_UNWIELD" );

static const itype_id itype_backpack( "backpack" );
static const itype_id itype_bottle_plastic( "bottle_plastic" );
static const itype_id itype_crowbar( "crowbar" );
static const itype_id itype_fridge_test( "fridge_test" );
static const itype_id itype_hammer( "hammer" );
static const itype_id itype_rock( "rock" );
static const itype_id itype_tshirt( "tshirt" );
static const itype_id itype_water_clean( "water_clean" );

static const mtype_id mon_zombie( "mon_zombie" );

static const ter_str_id ter_t_wall( "t_wall" );

static constexpr tripoint_bub_ms errand_origin{ 50, 50, 0 };

static const std::vector<std::string> errand_globals = {
    "test_errand_result", "test_errand_reason", "test_errand_u", "test_errand_npc",
    "test_errand_runs", "test_errand_tag", "test_errand_spot"
};

static void forget_errand_globals()
{
    for( const std::string &name : errand_globals ) {
        get_globals().remove_global_value( name );
    }
}

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
static npc &errand_npc()
{
    clear_map_with_vision();
    clear_avatar();
    clear_npcs();
    forget_errand_globals();
    set_time_to_day();
    map &here = get_map();
    get_avatar().setpos( here, errand_origin + tripoint( -20, 0, 0 ) );
    npc &guy = spawn_npc( errand_origin.xy(), "test_talker" );
    clear_character( guy, true );
    guy.setpos( here, errand_origin );
    calm( guy );
    here.build_map_cache( 0 );
    return guy;
}

// walls on the tiles around p, so nothing can get there
static void wall_off( const tripoint_bub_ms &p )
{
    map &here = get_map();
    for( const tripoint_bub_ms &q : here.points_in_radius( p, 1 ) ) {
        if( q != p ) {
            here.ter_set( q, ter_t_wall );
        }
    }
    here.build_map_cache( p.z() );
}

static int run_until_walk_ends( npc &guy, const int max_turns )
{
    return run_npc_turns( guy, max_turns, []( const npc & n ) {
        return !n.walk;
    } );
}

// saves the NPC and loads it back
static void save_and_load( npc &guy )
{
    std::ostringstream os;
    JsonOut jsout( os );
    guy.serialize( jsout );
    guy.walk.reset();
    guy.path.clear();
    // loaded NPC starts without an id; the save brings it back
    guy.setID( character_id(), true );
    guy.deserialize( json_loader::from_string( os.str() ).get_object() );
}

// NPC-B17: a step to a tile that isn't adjacent used to be a teleport
TEST_CASE( "npc_move_to_does_not_hop_over_tiles", "[npc][errand]" )
{
    npc &guy = errand_npc();
    SECTION( "move_to a tile three steps away stays put" ) {
        guy.set_moves( 100 );
        guy.move_to( errand_origin + tripoint( 3, 0, 0 ) );
        CHECK( guy.pos_bub() == errand_origin );
        CHECK( guy.path.empty() );
    }
    SECTION( "a stale path is routed again from where the NPC stands" ) {
        // as left by a shove: the path starts three tiles away
        guy.path = { errand_origin + tripoint( 3, 0, 0 ), errand_origin + tripoint( 4, 0, 0 ) };
        guy.set_moves( 100 );
        guy.move_to_next();
        CHECK( guy.pos_bub() == errand_origin + tripoint( 1, 0, 0 ) );
        REQUIRE_FALSE( guy.path.empty() );
        CHECK( guy.path.back() == errand_origin + tripoint( 4, 0, 0 ) );
    }
}

// a follower told "Move there!" (NPC-B14)
static npc &errand_follower()
{
    npc &guy = errand_npc();
    guy.set_fac( faction_your_followers );
    guy.set_attitude( NPCATT_FOLLOW );
    guy.rules.set_flag( ally_rule::follow_close );
    get_avatar().setpos( get_map(), errand_origin + tripoint( -2, 0, 0 ) );
    get_map().build_map_cache( 0 );
    REQUIRE( guy.is_player_ally() );
    return guy;
}

TEST_CASE( "follower_move_order_goes_there", "[npc][errand]" )
{
    npc &guy = errand_follower();
    const tripoint_bub_ms there = errand_origin + tripoint( 6, 2, 0 );
    errand::start_walk( guy, errand::move_order( get_map().get_abs( there ) ) );
    REQUIRE( guy.walk );
    run_npc_turn( guy );
    CHECK( guy.get_committed_goal() == "goto_ordered_position" );
    run_until_walk_ends( guy, 20 );
    CHECK_FALSE( guy.walk );
    CHECK( guy.pos_bub() == there );
}

TEST_CASE( "follower_move_order_gives_up_where_it_cant_get", "[npc][errand]" )
{
    npc &guy = errand_follower();
    const tripoint_bub_ms there = errand_origin + tripoint( 6, 0, 0 );
    wall_off( there );
    SECTION( "no way there: it says so at once" ) {
        errand::start_walk( guy, errand::move_order( get_map().get_abs( there ) ) );
        CHECK_FALSE( guy.walk );
    }
    SECTION( "the way is shut after it set off" ) {
        // used to pause every turn for good
        errand::set_walk( guy, errand::move_order( get_map().get_abs( there ) ) );
        run_npc_turns( guy, 3 );
        CHECK_FALSE( guy.walk );
    }
}

TEST_CASE( "follower_move_order_gives_up_when_blocked", "[npc][errand]" )
{
    npc &guy = errand_follower();
    map &here = get_map();
    // a dead end one tile wide, the avatar standing in its mouth
    const tripoint_bub_ms there = errand_origin + tripoint( 6, 0, 0 );
    wall_off( there );
    const tripoint_bub_ms mouth = there + tripoint( -1, 0, 0 );
    here.ter_set( mouth, here.ter( errand_origin ) );
    get_avatar().setpos( here, mouth );
    here.build_map_cache( 0 );
    errand::start_walk( guy, errand::move_order( here.get_abs( there ) ) );
    REQUIRE( guy.walk );
    const int turns = run_until_walk_ends( guy, 40 );
    CHECK_FALSE( guy.walk );
    // asks to pass, waits ten turns, gives up
    CHECK( turns >= errand::walk_patience );
    CHECK( turns < 30 );
}

TEST_CASE( "follower_move_order_ends_next_to_a_taken_spot", "[npc][errand]" )
{
    npc &guy = errand_follower();
    map &here = get_map();
    const tripoint_bub_ms there = errand_origin + tripoint( 5, 0, 0 );
    get_avatar().setpos( here, there );
    here.build_map_cache( 0 );
    errand::start_walk( guy, errand::move_order( here.get_abs( there ) ) );
    run_until_walk_ends( guy, 20 );
    CHECK_FALSE( guy.walk );
    CHECK( square_dist( guy.pos_bub(), there ) == 1 );
}

TEST_CASE( "follower_move_order_is_saved", "[npc][errand]" )
{
    npc &guy = errand_follower();
    const tripoint_abs_ms there = get_map().get_abs( errand_origin + tripoint( 8, 0, 0 ) );
    errand::start_walk( guy, errand::move_order( there ) );
    run_npc_turns( guy, 2 );
    REQUIRE( guy.walk );
    save_and_load( guy );
    REQUIRE( guy.walk );
    CHECK( guy.walk->kind == walk_kind::order );
    CHECK( guy.walk->target == there );
    run_until_walk_ends( guy, 20 );
    CHECK( guy.pos_abs() == there );
}

TEST_CASE( "follow_me_ends_the_move_order", "[npc][errand]" )
{
    npc &guy = errand_follower();
    errand::start_walk( guy, errand::move_order( get_map().get_abs( errand_origin + tripoint( 8, 0,
                        0 ) ) ) );
    REQUIRE( guy.walk );
    SECTION( "Follow me" ) {
        talk_function::follow_only( guy );
    }
    SECTION( "Follow me, to a guard" ) {
        talk_function::stop_guard( guy );
    }
    CHECK_FALSE( guy.walk );
    run_npc_turn( guy );
    CHECK( guy.get_committed_goal() != "goto_ordered_position" );
}
