#include <functional>
#include <string>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "dialogue.h"
#include "faction.h"
#include "game.h"
#include "math_parser.h"
#include "math_parser_type.h"
#include "player_helpers.h"
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
