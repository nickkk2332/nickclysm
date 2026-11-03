#include <functional>
#include <string>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "coordinates.h"
#include "creature.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "faction.h"
#include "game.h"
#include "global_vars.h"
#include "line.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser.h"
#include "math_parser_type.h"
#include "monster.h"
#include "player_helpers.h"
#include "point.h"
#include "talker.h"
#include "type_id.h"

static double eval_math( dialogue &d, const std::string &expr )
{
    math_exp exp;
    REQUIRE( exp.parse( expr ) );
    return exp.eval( d );
}

// MATH-B03: the faction_*() functions dereferenced the null pointer faction_manager::get()
// returns for an unknown id, so a typo or a stale variable crashed the game.
TEST_CASE( "math-b03_faction_functions_reject_unknown_ids", "[audit][math_parser]" )
{
    clear_avatar();
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    const std::vector<std::string> readable = { "faction_like", "faction_respect", "faction_trust",
                                                "faction_food_supply", "faction_wealth", "faction_power", "faction_size"
                                              };
    for( const std::string &fn : readable ) {
        CAPTURE( fn );
        CHECK_THROWS_AS( eval_math( d, fn + "('audit_no_such_faction')" ), math::runtime_error );
        if( fn != "faction_food_supply" ) {
            CHECK_THROWS_AS( eval_math( d, fn + "('audit_no_such_faction') = 5" ),
                             math::runtime_error );
        }
    }

    // A faction that exists still reads and writes.
    g->faction_manager_ptr->create_if_needed();
    faction *followers = g->faction_manager_ptr->get( faction_id( "your_followers" ) );
    REQUIRE( followers != nullptr );
    const restore_on_out_of_scope<int> restore_like( followers->likes_u );
    eval_math( d, "faction_like('your_followers') = 7" );
    CHECK( followers->likes_u == 7 );
    CHECK( eval_math( d, "faction_like('your_followers')" ) == 7 );
}

// MATH-B04: an assignment to time('now', 'unit':...) or calories('format':'percent') ignored the
// kwarg, so time('now', 'unit':'hours') += 1 set the calendar to about 2201 turns and
// u_calories('format':'percent') = 50 set 50 kcal.
TEST_CASE( "math-b04_assignments_honour_the_read_kwargs", "[audit][math_parser]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    dialogue d( get_talker_for( u ), nullptr );

    SECTION( "time('now') in a unit" ) {
        const restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
        calendar::turn = calendar::turn_zero + 10_days + 3_hours;
        eval_math( d, "time('now', 'unit':'hours') += 1" );
        CHECK( calendar::turn == calendar::turn_zero + 10_days + 4_hours );
        eval_math( d, "time('now', 'unit':'days') = 20" );
        CHECK( calendar::turn == calendar::turn_zero + 20_days );
        eval_math( d, "time('now') = 90" );
        CHECK( calendar::turn == calendar::turn_zero + 90_seconds );
    }

    SECTION( "calories() as a percent" ) {
        const on_out_of_scope restore_avatar( []() {
            clear_avatar();
        } );
        const int healthy = u.get_healthy_kcal();
        REQUIRE( healthy > 0 );
        eval_math( d, "u_calories('format':'percent') = 50" );
        CHECK( u.get_stored_kcal() == Approx( healthy / 2.0 ).margin( 1 ) );
        CHECK( eval_math( d, "u_calories('format':'percent')" ) == Approx( 50 ).margin( 0.01 ) );
        eval_math( d, "u_calories('format':'percent') += 10" );
        CHECK( eval_math( d, "u_calories('format':'percent')" ) == Approx( 60 ).margin( 0.01 ) );
        // raw still means kcal
        eval_math( d, "u_calories() = 1000" );
        CHECK( u.get_stored_kcal() == 1000 );
    }
}

static const effect_on_condition_id
effect_on_condition_EOC_AUDIT_MATH_B05_GLOBAL( "EOC_AUDIT_MATH_B05_GLOBAL" );
static const effect_on_condition_id
effect_on_condition_EOC_AUDIT_MATH_B05_LOCAL( "EOC_AUDIT_MATH_B05_LOCAL" );

// MATH-B05: time_until_eoc() returned the turn the EOC was queued for, not the time left, found
// only the first of several queued runs, and couldn't see the EOCs queued on a character.
TEST_CASE( "math-b05_time_until_eoc_is_time_left", "[audit][math_parser][eoc]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    const restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
    const restore_on_out_of_scope<queued_eocs> restore_global( g->queued_global_effect_on_conditions );
    const restore_on_out_of_scope<queued_eocs> restore_local( u.queued_effect_on_conditions );
    calendar::turn = calendar::turn_zero + 100_days;
    dialogue d( get_talker_for( u ), nullptr );

    const std::string global = "time_until_eoc('EOC_AUDIT_MATH_B05_GLOBAL', 'unit':'minutes')";
    CHECK( eval_math( d, global ) == -1 );
    effect_on_conditions::queue_effect_on_condition( 30_minutes,
            effect_on_condition_EOC_AUDIT_MATH_B05_GLOBAL, u, {} );
    effect_on_conditions::queue_effect_on_condition( 10_minutes,
            effect_on_condition_EOC_AUDIT_MATH_B05_GLOBAL, u, {} );
    CHECK( eval_math( d, global ) == Approx( 10 ) );
    calendar::turn += 4_minutes;
    CHECK( eval_math( d, global ) == Approx( 6 ) );

    // Not global: queued on the character.
    effect_on_conditions::queue_effect_on_condition( 20_minutes,
            effect_on_condition_EOC_AUDIT_MATH_B05_LOCAL, u, {} );
    CHECK( eval_math( d, "u_time_until_eoc('EOC_AUDIT_MATH_B05_LOCAL', 'unit':'minutes')" ) ==
           Approx( 20 ) );
    CHECK( eval_math( d, "time_until_eoc('EOC_AUDIT_MATH_B05_LOCAL')" ) == -1 );
    CHECK( eval_math( d, "u_time_until_eoc('EOC_AUDIT_MATH_B05_GLOBAL')" ) == -1 );
}

// MATH-B06, LOOP-B10: before 12:00 noon() went back from noon as far as it was ahead of it (at
// 10:00, 08:00), and time_until('noon') used the time_since formula, so both returned
// |time of day - 12 h|.
TEST_CASE( "math-b06_loop-b10_time_since_and_until_noon", "[audit][math_parser][calendar]" )
{
    clear_avatar();
    const restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    const time_point midnight = calendar::turn_zero + 50_days;
    const std::string since = "time_since('noon', 'unit':'hours')";
    const std::string until = "time_until('noon', 'unit':'hours')";

    calendar::turn = midnight + 10_hours;
    CHECK( noon( calendar::turn ) == midnight + 12_hours );
    CHECK( eval_math( d, since ) == Approx( 22 ) );
    CHECK( eval_math( d, until ) == Approx( 2 ) );

    calendar::turn = midnight + 14_hours;
    CHECK( noon( calendar::turn ) == midnight + 12_hours );
    CHECK( eval_math( d, since ) == Approx( 2 ) );
    CHECK( eval_math( d, until ) == Approx( 22 ) );

    calendar::turn = midnight + 12_hours;
    CHECK( eval_math( d, since ) == Approx( 0 ) );
    CHECK( eval_math( d, until ) == Approx( 0 ) );

    calendar::turn = midnight;
    CHECK( eval_math( d, since ) == Approx( 12 ) );
    CHECK( eval_math( d, until ) == Approx( 12 ) );
}

// MATH-B07: the documented 'allow_hallucinations' kwarg failed to load, and when the talker
// wasn't a character every character in the bubble was counted, whatever the radius.
TEST_CASE( "math-b07_characters_nearby_radius_and_kwargs", "[audit][math_parser]" )
{
    clear_avatar();
    clear_map_without_vision();
    const on_out_of_scope cleanup( []() {
        clear_npcs();
        clear_creatures();
        get_globals().remove_global_value( "audit_math_b07_loc" );
    } );
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms mon_at = u.pos_bub() + tripoint::east * 10;
    monster &mon = spawn_test_monster( "mon_zombie", mon_at );
    spawn_npc( ( mon_at + tripoint::east * 5 ).xy(), "test_talker" );
    REQUIRE( rl_dist( u.pos_abs(), mon.pos_abs() ) == 10 );
    REQUIRE( g->get_characters_if( []( const Character & ) {
        return true;
    } ).size() == 2 );

    dialogue by_monster( get_talker_for( mon ), nullptr );
    CHECK( eval_math( by_monster, "u_characters_nearby('radius': 3)" ) == 0 );
    CHECK( eval_math( by_monster, "u_characters_nearby('radius': 7)" ) == 1 );
    CHECK( eval_math( by_monster, "u_characters_nearby('radius': 12)" ) == 2 );
    CHECK( eval_math( by_monster, "u_characters_nearby('radius': 12, 'attitude': 'any')" ) == 2 );
    // A monster has no attitude to the characters to filter by.
    CHECK_THROWS_AS( eval_math( by_monster,
                                "u_characters_nearby('radius': 12, 'attitude': 'hostile')" ), math::runtime_error );
    // The global scope with a location filters by radius too.
    get_globals().set_global_value( "audit_math_b07_loc", here.get_abs( mon_at ) );
    CHECK( eval_math( by_monster,
                      "characters_nearby('radius': 3, 'location': audit_math_b07_loc)" ) == 0 );

    dialogue by_avatar( get_talker_for( u ), nullptr );
    CHECK( eval_math( by_avatar, "u_characters_nearby('radius': 30, 'allow_hallucinations': 1)" ) ==
           1 );
}
