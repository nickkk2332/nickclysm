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

// "true" or "false": which of EOC_TEST_ERRAND_TRUE and EOC_TEST_ERRAND_FALSE ran last; "" for none
static std::string result()
{
    return global_string( "test_errand_result" );
}

static std::string reason()
{
    return global_string( "test_errand_reason" );
}

static void set_spot( const tripoint_bub_ms &p )
{
    get_globals().set_global_value( "test_errand_spot", get_map().get_abs( p ) );
}

static talk_effect_t load_effect( const std::string &effect )
{
    const JsonObject jo = json_loader::from_string( R"({ "effect": )" + effect + " }" );
    jo.allow_omitted_members();
    talk_effect_t ret( jo, "effect", "npc_errand_test" );
    finalize_conditions();
    return ret;
}

static bool test_condition( const std::string &condition, const_dialogue const &d )
{
    const JsonObject jo = json_loader::from_string( R"({ "condition": )" + condition + " }" );
    jo.allow_omitted_members();
    conditional_t::func cond;
    read_condition( jo, "condition", cond, false );
    return cond( d );
}

static void load_condition( const std::string &condition )
{
    const JsonObject jo = json_loader::from_string( R"({ "condition": )" + condition + " }" );
    jo.allow_omitted_members();
    conditional_t::func cond;
    read_condition( jo, "condition", cond, false );
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

static const std::string walk_to_spot = R"({ "u_walk_to": { "global_val": "test_errand_spot" },
    "true_eocs": [ "EOC_TEST_ERRAND_TRUE" ], "false_eocs": [ "EOC_TEST_ERRAND_FALSE" ] )";

// u_walk_to with these fields added
static std::string walk_to_spot_with( const std::string &fields )
{
    return walk_to_spot + ", " + fields + " }";
}

static void walk_npc( npc &guy, const std::string &effect )
{
    dialogue d( get_talker_for( guy ), nullptr );
    load_effect( effect ).apply( d );
}

TEST_CASE( "u_walk_to_npc_arrives", "[npc][eoc][errand]" )
{
    npc &guy = errand_npc();
    const tripoint_bub_ms there = errand_origin + tripoint( 7, 3, 0 );
    set_spot( there );
    walk_npc( guy, walk_to_spot + " }" );
    REQUIRE( guy.walk );
    CHECK( result().empty() );
    run_npc_turn( guy );
    CHECK( guy.get_committed_goal() == "goto_ordered_position" );
    CHECK( rl_dist( guy.pos_bub(), there ) < 7 );
    run_until_walk_ends( guy, 20 );
    CHECK_FALSE( guy.walk );
    CHECK( guy.pos_bub() == there );
    CHECK( result() == "true" );
    CHECK( reason() == "arrived" );
    // the walker is u, and there's no npc
    CHECK( global_string( "test_errand_u" ) == guy.get_name() );
    CHECK( global_string( "test_errand_npc" ) == "none" );
    CHECK( global_number( "test_errand_runs" ) == 1 );
}

TEST_CASE( "npc_walk_to_sends_the_beta_talker", "[npc][eoc][errand]" )
{
    npc &guy = errand_npc();
    const tripoint_bub_ms there = errand_origin + tripoint( 0, 6, 0 );
    set_spot( there );
    dialogue d( get_talker_for( get_avatar() ), get_talker_for( guy ) );
    load_effect( R"({ "npc_walk_to": { "global_val": "test_errand_spot" },
        "true_eocs": [ "EOC_TEST_ERRAND_TRUE" ] })" ).apply( d );
    CHECK_FALSE( get_avatar().walk );
    REQUIRE( guy.walk );
    run_until_walk_ends( guy, 20 );
    CHECK( guy.pos_bub() == there );
    CHECK( result() == "true" );
    // the EOCs run later, with the walker as u
    CHECK( global_string( "test_errand_u" ) == guy.get_name() );
}

TEST_CASE( "u_walk_to_range_and_context", "[npc][eoc][errand]" )
{
    npc &guy = errand_npc();
    const tripoint_bub_ms there = errand_origin + tripoint( 9, 0, 0 );
    set_spot( there );
    SECTION( "it stops within range" ) {
        walk_npc( guy, walk_to_spot_with( R"("range": 3)" ) );
        run_until_walk_ends( guy, 20 );
        CHECK( square_dist( guy.pos_bub(), there ) == 3 );
        CHECK( result() == "true" );
    }
    SECTION( "the context values of the EOC that sent it reach its EOCs" ) {
        global_variables::impl_t context;
        context["errand_tag"] = diag_value( std::string( "shed" ) );
        dialogue d( get_talker_for( guy ), nullptr, {}, context );
        load_effect( R"({ "u_walk_to": { "global_val": "test_errand_spot" },
            "true_eocs": [ "EOC_TEST_ERRAND_TAG" ] })" ).apply( d );
        run_until_walk_ends( guy, 20 );
        CHECK( global_string( "test_errand_tag" ) == "shed" );
    }
}

TEST_CASE( "u_walk_to_ends_at_once", "[npc][eoc][errand]" )
{
    npc &guy = errand_npc();
    SECTION( "already there" ) {
        set_spot( errand_origin + tripoint( 1, 1, 0 ) );
        walk_npc( guy, walk_to_spot_with( R"("range": 1)" ) );
        CHECK_FALSE( guy.walk );
        CHECK( result() == "true" );
        CHECK( reason() == "arrived" );
    }
    SECTION( "no way there" ) {
        const tripoint_bub_ms there = errand_origin + tripoint( 6, 0, 0 );
        wall_off( there );
        set_spot( there );
        walk_npc( guy, walk_to_spot + " }" );
        CHECK_FALSE( guy.walk );
        CHECK( result() == "false" );
        CHECK( reason() == "unreachable" );
        CHECK( global_string( "test_errand_u" ) == guy.get_name() );
    }
    SECTION( "outside the loaded map" ) {
        get_globals().set_global_value( "test_errand_spot",
                                        guy.pos_abs() + tripoint( 400, 0, 0 ) );
        walk_npc( guy, walk_to_spot + " }" );
        CHECK_FALSE( guy.walk );
        CHECK( reason() == "unreachable" );
    }
}

TEST_CASE( "u_walk_to_give_up_after", "[npc][eoc][errand]" )
{
    npc &guy = errand_npc();
    const tripoint_bub_ms there = errand_origin + tripoint( 6, 0, 0 );
    wall_off( there );
    set_spot( there );
    walk_npc( guy, walk_to_spot_with( R"("give_up_after": 5)" ) );
    // with a time limit it waits for a way to open
    REQUIRE( guy.walk );
    run_npc_turns( guy, 4 );
    CHECK( guy.walk );
    CHECK( result().empty() );
    run_until_walk_ends( guy, 5 );
    CHECK_FALSE( guy.walk );
    CHECK( result() == "false" );
    CHECK( reason() == "timeout" );
}

TEST_CASE( "u_walk_to_sent_again_goes_on", "[npc][eoc][errand]" )
{
    npc &guy = errand_npc();
    set_spot( errand_origin + tripoint( 10, 0, 0 ) );
    walk_npc( guy, walk_to_spot_with( R"("give_up_after": "1 minutes")" ) );
    REQUIRE( guy.walk );
    const int serial = guy.walk->serial;
    const time_point started = guy.walk->started;
    const std::optional<time_point> give_up_at = guy.walk->give_up_at;
    run_npc_turns( guy, 3 );
    // as a goal whose EOCs run each turn would send it
    walk_npc( guy, walk_to_spot_with( R"("give_up_after": "1 minutes", "range": 1)" ) );
    REQUIRE( guy.walk );
    CHECK( guy.walk->serial == serial );
    CHECK( guy.walk->started == started );
    CHECK( guy.walk->give_up_at == give_up_at );
    CHECK( guy.walk->range == 1 );
    SECTION( "somewhere else replaces it, without its EOCs" ) {
        set_spot( errand_origin + tripoint( 0, -5, 0 ) );
        walk_npc( guy, walk_to_spot + " }" );
        REQUIRE( guy.walk );
        CHECK( guy.walk->serial != serial );
        CHECK( result().empty() );
    }
}

TEST_CASE( "u_walk_to_run", "[npc][eoc][errand]" )
{
    npc &guy = errand_npc();
    set_spot( errand_origin + tripoint( 8, 0, 0 ) );
    walk_npc( guy, walk_to_spot_with( R"("run": true)" ) );
    run_npc_turn( guy );
    CHECK( guy.is_running() );
    run_until_walk_ends( guy, 20 );
    CHECK( result() == "true" );
    CHECK_FALSE( guy.is_running() );
}

TEST_CASE( "u_walk_to_is_saved", "[npc][eoc][errand]" )
{
    npc &guy = errand_npc();
    const tripoint_bub_ms there = errand_origin + tripoint( 9, 2, 0 );
    set_spot( there );
    global_variables::impl_t context;
    context["errand_tag"] = diag_value( std::string( "saved" ) );
    dialogue d( get_talker_for( guy ), nullptr, {}, context );
    load_effect( R"({ "u_walk_to": { "global_val": "test_errand_spot" }, "range": 1,
        "give_up_after": "10 minutes", "interruptible": false,
        "true_eocs": [ "EOC_TEST_ERRAND_TAG", "EOC_TEST_ERRAND_TRUE" ] })" ).apply( d );
    run_npc_turns( guy, 2 );
    REQUIRE( guy.walk );
    const walk_order before = *guy.walk;
    save_and_load( guy );
    REQUIRE( guy.walk );
    const walk_order &after = *guy.walk;
    CHECK( after.kind == walk_kind::eoc );
    CHECK( after.target == before.target );
    CHECK( after.range == 1 );
    CHECK( after.give_up_at == before.give_up_at );
    CHECK_FALSE( after.interruptible );
    CHECK( after.true_eocs == before.true_eocs );
    CHECK( after.started == before.started );
    run_until_walk_ends( guy, 20 );
    CHECK( square_dist( guy.pos_bub(), there ) <= 1 );
    CHECK( result() == "true" );
    CHECK( global_string( "test_errand_tag" ) == "saved" );
}

TEST_CASE( "u_walk_to_and_a_threat", "[npc][eoc][errand]" )
{
    // a follower, armed: it fights what comes near (an unarmed NPC rates no attack)
    npc &guy = errand_follower();
    map &here = get_map();
    guy.set_wielded_item( item( itype_crowbar ) );
    const tripoint_bub_ms there = errand_origin + tripoint( 10, 0, 0 );
    set_spot( there );
    const bool interruptible = GENERATE( true, false );
    CAPTURE( interruptible );
    walk_npc( guy, walk_to_spot_with( interruptible ? R"("range": 0)" :
                                      R"("interruptible": false)" ) );
    run_npc_turns( guy, 2 );
    REQUIRE( guy.walk );
    monster *zed = g->place_critter_at( mon_zombie, guy.pos_bub() + tripoint( 0, 3, 0 ) );
    REQUIRE( zed != nullptr );
    here.build_map_cache( 0 );
    REQUIRE( guy.sees( here, *zed ) );
    // a turn without a step: the fight came first
    bool set_aside = false;
    for( int i = 0; i < 3 && !set_aside; ++i ) {
        run_npc_turn( guy );
        set_aside = !guy.walk || guy.walk->last_step < calendar::turn - 1_turns;
    }
    REQUIRE( set_aside );
    if( interruptible ) {
        // the walk waits for the fight
        REQUIRE( guy.walk );
        CHECK( result().empty() );
        clear_creatures();
        here.build_map_cache( 0 );
        run_until_walk_ends( guy, 30 );
        CHECK( guy.pos_bub() == there );
        CHECK( result() == "true" );
    } else {
        CHECK_FALSE( guy.walk );
        CHECK( result() == "false" );
        CHECK( reason() == "interrupted" );
        clear_creatures();
    }
}

TEST_CASE( "u_walk_to_avatar_auto_move", "[eoc][errand]" )
{
    clear_map_with_vision();
    clear_avatar();
    clear_npcs();
    forget_errand_globals();
    map &here = get_map();
    avatar &you = get_avatar();
    you.setpos( here, errand_origin );
    here.build_map_cache( 0 );
    const tripoint_bub_ms there = errand_origin + tripoint( 6, 2, 0 );
    const tripoint_abs_ms there_abs = here.get_abs( there );
    set_spot( there );
    dialogue d( get_talker_for( you ), nullptr );
    SECTION( "it walks there step by step" ) {
        load_effect( walk_to_spot_with( R"("run": true)" ) ).apply( d );
        REQUIRE( you.walk );
        REQUIRE( you.has_destination() );
        CHECK( you.destination_point == there_abs );
        CHECK( you.is_running() );
        // what the game's turn does with an auto-move
        for( int i = 0; i < 20 && you.walk; ++i ) {
            errand::avatar_turn( you );
            if( !you.walk ) {
                break;
            }
            REQUIRE( you.has_destination() );
            const action_id act = you.get_next_auto_move_direction();
            you.set_moves( you.get_speed() );
            avatar_action::move( you, here,
                                 tripoint_rel_ms( get_delta_from_movement_action( act, iso_rotate::yes ), 0 ) );
            calendar::turn += 1_turns;
        }
        CHECK_FALSE( you.walk );
        // the map follows the avatar, so its own coordinates moved
        CHECK( you.pos_abs() == there_abs );
        CHECK( result() == "true" );
        CHECK( global_string( "test_errand_u" ) == you.get_name() );
        CHECK_FALSE( you.is_running() );
    }
    SECTION( "a stopped auto-move ends it" ) {
        load_effect( walk_to_spot + " }" ).apply( d );
        REQUIRE( you.walk );
        you.abort_automove();
        errand::avatar_turn( you );
        CHECK_FALSE( you.walk );
        CHECK( result() == "false" );
        CHECK( reason() == "interrupted" );
    }
    SECTION( "loaded from a save, the auto-move starts again" ) {
        walk_order w;
        w.target = there_abs;
        errand::set_walk( you, w );
        REQUIRE_FALSE( you.has_destination() );
        errand::avatar_turn( you );
        REQUIRE( you.walk );
        CHECK( you.has_destination() );
        you.clear_destination();
        you.walk.reset();
    }
    SECTION( "no way there" ) {
        wall_off( there );
        load_effect( walk_to_spot + " }" ).apply( d );
        CHECK_FALSE( you.walk );
        CHECK_FALSE( you.has_destination() );
        CHECK( reason() == "unreachable" );
    }
}

TEST_CASE( "u_is_walking_to", "[npc][eoc][errand]" )
{
    npc &guy = errand_npc();
    const tripoint_bub_ms there = errand_origin + tripoint( 8, 0, 0 );
    set_spot( errand_origin + tripoint( 0, 8, 0 ) );
    const dialogue d( get_talker_for( get_avatar() ), get_talker_for( guy ) );
    CHECK_FALSE( test_condition( R"({ "npc_is_walking_to": "any" })", d ) );
    errand::start_walk( guy, errand::move_order( get_map().get_abs( there ) ) );
    CHECK( test_condition( R"({ "npc_is_walking_to": "any" })", d ) );
    CHECK_FALSE( test_condition( R"({ "u_is_walking_to": "any" })", d ) );
    CHECK_FALSE( test_condition( R"({ "npc_is_walking_to": { "global_val": "test_errand_spot" } })",
                                 d ) );
    set_spot( there );
    CHECK( test_condition( R"({ "npc_is_walking_to": { "global_val": "test_errand_spot" } })", d ) );
}

TEST_CASE( "walk_effects_are_checked_at_load", "[eoc][errand]" )
{
    CHECK_THROWS_AS( load_effect( R"({ "u_walk_to": "shed" })" ), JsonError );
    CHECK_THROWS_AS( load_effect( R"({ "u_walk_to": { "global_val": "x" }, "range": -1 })" ),
                     JsonError );
    CHECK_THROWS_AS( load_effect( R"({ "u_walk_to": { "global_val": "x" }, "give_up_after": 0 })" ),
                     JsonError );
    CHECK_THROWS_AS( load_effect( R"({ "u_walk_to": { "global_val": "x" }, "run": "yes" })" ),
                     JsonError );
    CHECK_THROWS_AS( load_condition( R"({ "u_is_walking_to": "anywhere" })" ), JsonError );
    CHECK_THROWS_AS( load_condition( R"({ "u_is_walking_to": 3 })" ), JsonError );
}

// an item on the ground at p
static item_location place_item( const tripoint_bub_ms &p, const itype_id &type )
{
    item &placed = get_map().add_item( p, item( type ) );
    return item_location( map_cursor( p ), &placed );
}

// runs effect with who as u and the item at loc as npc; loc follows the item
static void apply_to_item( const std::string &effect, Character &who, item_location &loc )
{
    dialogue d( get_talker_for( who ), get_talker_for( loc ) );
    load_effect( effect ).apply( d );
}

static std::string item_effect( const std::string &name, const std::string &fields = "" )
{
    return R"({ ")" + name + R"(": "npc", "true_eocs": [ "EOC_TEST_ERRAND_TRUE" ],
        "false_eocs": [ "EOC_TEST_ERRAND_FALSE" ])" + ( fields.empty() ? "" : ", " + fields ) +
           " }";
}

static void give_backpack( Character &who )
{
    who.wear_item( item( itype_backpack ), false );
}

TEST_CASE( "u_pickup_item_takes_that_item", "[npc][eoc][errand]" )
{
    npc &guy = errand_npc();
    avatar &you = get_avatar();
    const bool by_npc = GENERATE( true, false );
    CAPTURE( by_npc );
    Character &who = by_npc ? static_cast<Character &>( guy ) : you;
    if( !by_npc ) {
        you.setpos( get_map(), errand_origin + tripoint( 0, 5, 0 ) );
    }
    give_backpack( who );
    const tripoint_bub_ms spot = who.pos_bub() + tripoint( 1, 0, 0 );
    item_location loc = place_item( spot, itype_crowbar );
    loc->set_damage( 2000 );
    loc->set_var( "errand_mark", "kept" );
    // a second crowbar it must leave
    place_item( spot, itype_crowbar );
    who.set_moves( 100 );
    apply_to_item( item_effect( "u_pickup_item" ), who, loc );
    CHECK( result() == "true" );
    CHECK( reason() == "done" );
    CHECK( global_string( "test_errand_u" ) == who.get_name() );
    // it costs moves, as the avatar's pickup does
    CHECK( who.get_moves() < 100 );
    // the item talker now points at the item in the pack
    REQUIRE( loc );
    CHECK( loc.carrier() == &who );
    CHECK( loc->typeId() == itype_crowbar );
    CHECK( loc->damage() == 2000 );
    CHECK( loc->get_var( "errand_mark", "" ) == "kept" );
    CHECK( get_map().i_at( spot ).size() == 1 );
    CHECK( who.amount_of( itype_crowbar ) == 1 );
}

TEST_CASE( "u_pickup_item_keeps_contents", "[npc][eoc][errand]" )
{
    npc &guy = errand_npc();
    give_backpack( guy );
    item_location loc = place_item( errand_origin + tripoint( 0, 1, 0 ), itype_bottle_plastic );
    loc->fill_with( item( itype_water_clean ), 2 );
    REQUIRE( loc->charges_of( itype_water_clean ) == 2 );
    apply_to_item( item_effect( "u_pickup_item" ), guy, loc );
    CHECK( result() == "true" );
    REQUIRE( loc );
    CHECK( loc.carrier() == &guy );
    CHECK( loc->charges_of( itype_water_clean ) == 2 );
}

TEST_CASE( "u_pickup_item_refusals", "[npc][eoc][errand]" )
{
    npc &guy = errand_npc();
    map &here = get_map();
    const tripoint_bub_ms next_to = errand_origin + tripoint( 1, 0, 0 );
    SECTION( "no room" ) {
        // nothing to carry it in
        item_location loc = place_item( next_to, itype_crowbar );
        apply_to_item( item_effect( "u_pickup_item" ), guy, loc );
        CHECK( reason() == "no_room" );
        CHECK( loc.where() == item_location::type::map );
    }
    SECTION( "too heavy" ) {
        give_backpack( guy );
        item_location loc = place_item( next_to, itype_fridge_test );
        apply_to_item( item_effect( "u_pickup_item" ), guy, loc );
        CHECK( reason() == "too_heavy" );
    }
    SECTION( "out of reach" ) {
        give_backpack( guy );
        item_location loc = place_item( errand_origin + tripoint( 4, 0, 0 ), itype_crowbar );
        apply_to_item( item_effect( "u_pickup_item" ), guy, loc );
        CHECK( reason() == "out_of_reach" );
        CHECK_FALSE( guy.walk );
    }
    SECTION( "someone else carries it" ) {
        give_backpack( guy );
        item_location loc = get_avatar().i_add( item( itype_rock ) );
        apply_to_item( item_effect( "u_pickup_item" ), guy, loc );
        CHECK( reason() == "held" );
    }
    SECTION( "spilt liquid" ) {
        give_backpack( guy );
        item water( itype_water_clean );
        water.charges = 5;
        item_location loc( map_cursor( next_to ), &here.add_item( next_to, water ) );
        apply_to_item( item_effect( "u_pickup_item" ), guy, loc );
        CHECK( reason() == "liquid" );
    }
    SECTION( "someone else's, theft not allowed" ) {
        give_backpack( guy );
        item_location loc = place_item( next_to, itype_crowbar );
        loc->set_owner( faction_your_followers );
        apply_to_item( item_effect( "u_pickup_item", R"("allow_theft": false)" ), guy, loc );
        CHECK( reason() == "owned" );
    }
    SECTION( "gone" ) {
        item_location loc = place_item( next_to, itype_crowbar );
        loc.remove_item();
        apply_to_item( item_effect( "u_pickup_item" ), guy, loc );
        CHECK( reason() == "gone" );
    }
    CHECK( result() == "false" );
    CHECK( global_string( "test_errand_u" ) == guy.get_name() );
}

TEST_CASE( "u_pickup_item_theft", "[npc][eoc][errand]" )
{
    // the avatar takes the NPC's faction's crowbar while it looks on
    npc &guy = errand_npc();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, errand_origin + tripoint( 2, 0, 0 ) );
    give_backpack( you );
    here.build_map_cache( 0 );
    REQUIRE( guy.sees( here, you ) );
    item_location loc = place_item( you.pos_bub() + tripoint( 1, 0, 0 ), itype_crowbar );
    loc->set_owner( faction_tacoma_commune );
    REQUIRE( guy.get_faction_id() == faction_tacoma_commune );
    apply_to_item( item_effect( "u_pickup_item" ), you, loc );
    CHECK( result() == "true" );
    REQUIRE( loc );
    CHECK( loc->is_owned_by( you ) );
    CHECK( guy.get_attitude() == NPCATT_RECOVER_GOODS );
}

TEST_CASE( "u_pickup_item_walks_there", "[npc][eoc][errand]" )
{
    npc &guy = errand_npc();
    give_backpack( guy );
    const tripoint_bub_ms spot = errand_origin + tripoint( 8, 3, 0 );
    item_location loc = place_item( spot, itype_crowbar );
    loc->set_var( "errand_mark", "walked" );
    const std::string walk_and_take = item_effect( "u_pickup_item", R"("walk": true)" );
    apply_to_item( walk_and_take, guy, loc );
    REQUIRE( guy.walk );
    CHECK( guy.walk->kind == walk_kind::pickup );
    CHECK( result().empty() );
    SECTION( "takes it on arrival" ) {
        run_until_walk_ends( guy, 20 );
        CHECK( square_dist( guy.pos_bub(), spot ) <= 1 );
        CHECK( result() == "true" );
        CHECK( reason() == "done" );
        // npc is the item, in the pack
        CHECK( global_string( "test_errand_npc" ) == item( itype_crowbar ).type_name() );
        CHECK( guy.has_item_with( []( const item & it ) {
            return it.get_var( "errand_mark", "" ) == "walked";
        } ) );
    }
    SECTION( "gone before it gets there" ) {
        run_npc_turns( guy, 2 );
        loc.remove_item();
        run_until_walk_ends( guy, 20 );
        CHECK( result() == "false" );
        CHECK( reason() == "gone" );
        CHECK( global_string( "test_errand_npc" ) == "none" );
    }
    SECTION( "saved and loaded on the way" ) {
        run_npc_turns( guy, 2 );
        save_and_load( guy );
        REQUIRE( guy.walk );
        CHECK( guy.walk->kind == walk_kind::pickup );
        CHECK( guy.walk->item_type == itype_crowbar );
        run_until_walk_ends( guy, 20 );
        CHECK( result() == "true" );
        CHECK( guy.amount_of( itype_crowbar ) == 1 );
    }
    SECTION( "sent again for the same item, the walk goes on" ) {
        run_npc_turns( guy, 2 );
        const int serial = guy.walk->serial;
        apply_to_item( walk_and_take, guy, loc );
        REQUIRE( guy.walk );
        CHECK( guy.walk->serial == serial );
    }
    SECTION( "within reach, it takes it at once" ) {
        guy.walk.reset();
        guy.setpos( get_map(), spot + tripoint( -1, 0, 0 ) );
        apply_to_item( walk_and_take, guy, loc );
        CHECK_FALSE( guy.walk );
        CHECK( result() == "true" );
    }
}

TEST_CASE( "u_wield_item", "[npc][eoc][errand]" )
{
    npc &guy = errand_npc();
    avatar &you = get_avatar();
    const bool by_npc = GENERATE( true, false );
    CAPTURE( by_npc );
    Character &who = by_npc ? static_cast<Character &>( guy ) : you;
    give_backpack( who );
    // the old weapon goes into the pack, with no menu for the avatar
    who.set_wielded_item( item( itype_hammer ) );
    item_location loc = who.i_add( item( itype_crowbar ) );
    loc->set_var( "errand_mark", "kept" );
    who.set_moves( 100 );
    apply_to_item( item_effect( "u_wield_item" ), who, loc );
    CHECK( result() == "true" );
    CHECK( who.get_moves() < 100 );
    REQUIRE( who.get_wielded_item() );
    CHECK( who.get_wielded_item()->typeId() == itype_crowbar );
    CHECK( who.get_wielded_item()->get_var( "errand_mark", "" ) == "kept" );
    CHECK( loc == who.get_wielded_item() );
    CHECK( who.amount_of( itype_hammer ) == 1 );
    SECTION( "wielded already" ) {
        forget_errand_globals();
        apply_to_item( item_effect( "u_wield_item" ), who, loc );
        CHECK( result() == "true" );
    }
    SECTION( "can't wield it" ) {
        item_location stuck = who.i_add( item( itype_rock ) );
        stuck->set_flag( json_flag_NO_UNWIELD );
        apply_to_item( item_effect( "u_wield_item" ), who, stuck );
        CHECK( result() == "false" );
        CHECK( reason() == "cannot_wield" );
        CHECK( who.get_wielded_item()->typeId() == itype_crowbar );
    }
}

TEST_CASE( "u_wield_item_from_the_ground", "[npc][eoc][errand]" )
{
    npc &guy = errand_npc();
    item_location loc = place_item( errand_origin + tripoint( 0, 1, 0 ), itype_crowbar );
    apply_to_item( item_effect( "u_wield_item" ), guy, loc );
    CHECK( result() == "true" );
    REQUIRE( guy.get_wielded_item() );
    CHECK( guy.get_wielded_item()->typeId() == itype_crowbar );
    CHECK( get_map().i_at( errand_origin + tripoint( 0, 1, 0 ) ).empty() );
}

TEST_CASE( "u_wear_item", "[npc][eoc][errand]" )
{
    npc &guy = errand_npc();
    give_backpack( guy );
    SECTION( "a shirt from the pack" ) {
        item_location loc = guy.i_add( item( itype_tshirt ) );
        guy.set_moves( 100 );
        apply_to_item( item_effect( "u_wear_item" ), guy, loc );
        CHECK( result() == "true" );
        CHECK( guy.get_moves() < 100 );
        REQUIRE( loc );
        CHECK( guy.is_worn( *loc ) );
        CHECK( loc->typeId() == itype_tshirt );
    }
    SECTION( "a rock can't be worn" ) {
        item_location loc = guy.i_add( item( itype_rock ) );
        apply_to_item( item_effect( "u_wear_item" ), guy, loc );
        CHECK( result() == "false" );
        CHECK( reason() == "cannot_wear" );
        CHECK_FALSE( guy.is_worn( *loc ) );
    }
}

TEST_CASE( "u_drop_item", "[npc][eoc][errand]" )
{
    npc &guy = errand_npc();
    give_backpack( guy );
    map &here = get_map();
    SECTION( "from the pack" ) {
        item_location loc = guy.i_add( item( itype_crowbar ) );
        loc->set_var( "errand_mark", "kept" );
        guy.set_moves( 100 );
        apply_to_item( item_effect( "u_drop_item" ), guy, loc );
        CHECK( result() == "true" );
        CHECK( guy.get_moves() < 100 );
        CHECK( guy.amount_of( itype_crowbar ) == 0 );
        REQUIRE( loc );
        CHECK( loc.where() == item_location::type::map );
        CHECK( loc.pos_bub( here ) == guy.pos_bub() );
        CHECK( loc->get_var( "errand_mark", "" ) == "kept" );
    }
    SECTION( "what it wields" ) {
        guy.set_wielded_item( item( itype_crowbar ) );
        item_location loc = guy.get_wielded_item();
        apply_to_item( item_effect( "u_drop_item" ), guy, loc );
        CHECK( result() == "true" );
        CHECK_FALSE( guy.get_wielded_item() );
    }
    SECTION( "what it wears" ) {
        item_location loc;
        for( const item_location &worn : guy.top_items_loc() ) {
            if( worn->typeId() == itype_backpack ) {
                loc = worn;
            }
        }
        REQUIRE( loc );
        apply_to_item( item_effect( "u_drop_item" ), guy, loc );
        CHECK( result() == "true" );
        CHECK_FALSE( guy.is_wearing( itype_backpack ) );
    }
    SECTION( "not its own" ) {
        item_location loc = place_item( errand_origin + tripoint( 1, 0, 0 ), itype_crowbar );
        apply_to_item( item_effect( "u_drop_item" ), guy, loc );
        CHECK( result() == "false" );
        CHECK( reason() == "not_carried" );
    }
}

TEST_CASE( "u_pickup_item_then_wield_it", "[npc][eoc][errand]" )
{
    // the npc talker follows the item into the pack, so the next effect finds it there
    npc &guy = errand_npc();
    give_backpack( guy );
    item_location loc = place_item( errand_origin + tripoint( 1, 1, 0 ), itype_crowbar );
    apply_to_item( R"([ { "u_pickup_item": "npc" }, { "u_wield_item": "npc",
        "true_eocs": [ "EOC_TEST_ERRAND_TRUE" ], "false_eocs": [ "EOC_TEST_ERRAND_FALSE" ] } ])",
                   guy,
                   loc );
    CHECK( result() == "true" );
    REQUIRE( guy.get_wielded_item() );
    CHECK( guy.get_wielded_item()->typeId() == itype_crowbar );
    SECTION( "after a walk, from true_eocs" ) {
        guy.set_wielded_item( item() );
        forget_errand_globals();
        item_location far = place_item( errand_origin + tripoint( 7, 0, 0 ), itype_hammer );
        apply_to_item( R"({ "u_pickup_item": "npc", "walk": true,
            "true_eocs": [ "EOC_TEST_ERRAND_WIELD_IT" ] })", guy, far );
        run_until_walk_ends( guy, 20 );
        CHECK( result() == "true" );
        REQUIRE( guy.get_wielded_item() );
        CHECK( guy.get_wielded_item()->typeId() == itype_hammer );
    }
}

TEST_CASE( "item_effects_are_checked_at_load", "[eoc][errand]" )
{
    CHECK_THROWS_AS( load_effect( R"({ "u_pickup_item": "u" })" ), JsonError );
    CHECK_THROWS_AS( load_effect( R"({ "npc_pickup_item": "npc" })" ), JsonError );
    CHECK_THROWS_AS( load_effect( R"({ "u_wield_item": { "context_val": "x" } })" ), JsonError );
    CHECK_THROWS_AS( load_effect( R"({ "u_wear_item": "item" })" ), JsonError );
    CHECK_THROWS_AS( load_effect( R"({ "u_drop_item": "u" })" ), JsonError );
    CHECK_THROWS_AS( load_effect( R"({ "u_pickup_item": "npc", "run": true })" ), JsonError );
    CHECK_THROWS_AS( load_effect( R"({ "u_pickup_item": "npc", "walk": true, "give_up_after": -5 })" ),
                     JsonError );
}

TEST_CASE( "u_has_item_in_hand", "[npc][eoc][errand]" )
{
    npc &guy = errand_npc();
    const const_dialogue d( get_const_talker_for( guy ), get_const_talker_for( get_avatar() ) );
    CHECK_FALSE( test_condition( R"({ "u_has_item_in_hand": "crowbar" })", d ) );
    guy.set_wielded_item( item( itype_crowbar ) );
    CHECK( test_condition( R"({ "u_has_item_in_hand": "crowbar" })", d ) );
    CHECK_FALSE( test_condition( R"({ "u_has_item_in_hand": "hammer" })", d ) );
    CHECK_FALSE( test_condition( R"({ "npc_has_item_in_hand": "crowbar" })", d ) );
    get_globals().set_global_value( "test_errand_tag", std::string( "crowbar" ) );
    CHECK( test_condition( R"({ "u_has_item_in_hand": { "global_val": "test_errand_tag" } })", d ) );
    // by flag, or by id and flag
    REQUIRE( guy.get_wielded_item()->has_flag( flag_id( "BELT_CLIP" ) ) );
    CHECK( test_condition( R"({ "u_has_item_in_hand": { "flag": "BELT_CLIP" } })", d ) );
    CHECK_FALSE( test_condition( R"({ "u_has_item_in_hand": { "flag": "SPEAR" } })", d ) );
    CHECK( test_condition( R"({ "u_has_item_in_hand": { "id": "crowbar", "flag": "BELT_CLIP" } })",
                           d ) );
    CHECK_FALSE( test_condition( R"({ "u_has_item_in_hand": { "id": "hammer", "flag": "BELT_CLIP" } })",
                                 d ) );
}

TEST_CASE( "u_can_reach", "[npc][eoc][errand]" )
{
    npc &guy = errand_npc();
    avatar &you = get_avatar();
    const const_dialogue d( get_const_talker_for( guy ), get_const_talker_for( you ) );
    const tripoint_bub_ms there = errand_origin + tripoint( 6, 0, 0 );
    set_spot( there );
    const std::string reach_spot = R"({ "u_can_reach": { "global_val": "test_errand_spot" } )";
    CHECK( test_condition( reach_spot + " }", d ) );
    CHECK( test_condition( R"({ "npc_can_reach": { "global_val": "test_errand_spot" } })", d ) );
    SECTION( "within a distance" ) {
        CHECK( test_condition( reach_spot + R"(, "max_distance": 6 })", d ) );
        CHECK_FALSE( test_condition( reach_spot + R"(, "max_distance": 5 })", d ) );
        CHECK( test_condition( reach_spot + R"(, "range": 1, "max_distance": 5 })", d ) );
    }
    SECTION( "walled off" ) {
        wall_off( there );
        CHECK_FALSE( test_condition( reach_spot + " }", d ) );
        // next to the walls will do
        CHECK( test_condition( reach_spot + R"(, "range": 2 })", d ) );
    }
    SECTION( "a creature in the way doesn't count" ) {
        get_avatar().setpos( get_map(), there );
        CHECK( test_condition( reach_spot + " }", d ) );
    }
}

TEST_CASE( "u_is_doing_activity", "[npc][eoc][errand]" )
{
    npc &guy = errand_npc();
    const const_dialogue d( get_const_talker_for( guy ), get_const_talker_for( get_avatar() ) );
    CHECK_FALSE( test_condition( R"({ "u_is_doing_activity": "any" })", d ) );
    CHECK_FALSE( test_condition( R"({ "u_is_doing_activity": "ACT_ADD_VARIABLE_COMPLETE" })", d ) );
    guy.assign_activity( ACT_ADD_VARIABLE_COMPLETE, 1000 );
    REQUIRE( guy.activity.id() == ACT_ADD_VARIABLE_COMPLETE );
    CHECK( test_condition( R"({ "u_is_doing_activity": "any" })", d ) );
    CHECK( test_condition( R"({ "u_is_doing_activity": "ACT_ADD_VARIABLE_COMPLETE" })", d ) );
    CHECK_FALSE( test_condition( R"({ "u_is_doing_activity": "ACT_READ" })", d ) );
    CHECK_FALSE( test_condition( R"({ "npc_is_doing_activity": "any" })", d ) );
    guy.cancel_activity();
}

TEST_CASE( "errand_conditions_are_checked_at_load", "[eoc][errand]" )
{
    CHECK_THROWS_AS( load_condition( R"({ "u_can_reach": "shed" })" ), JsonError );
    CHECK_THROWS_AS( load_condition( R"({ "u_can_reach": { "global_val": "x" }, "range": -2 })" ),
                     JsonError );
    CHECK_THROWS_AS( load_condition(
                         R"({ "u_can_reach": { "global_val": "x" }, "max_distance": -1 })" ), JsonError );
}
