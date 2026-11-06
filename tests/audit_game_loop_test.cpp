#include <memory>
#include <string>

#include "avatar.h"
#include "cata_catch.h"
#include "coordinates.h"
#include "game.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "monster.h"
#include "npc.h"
#include "options_helpers.h"
#include "player_helpers.h"
#include "point.h"
#include "units.h"

// A flung creature must keep moving along its whole trajectory instead of
// stepping off its start tile once and then colliding with itself.
TEST_CASE( "loop-b01_flung_creature_travels_full_distance", "[audit]" )
{
    clear_map();
    clear_avatar();
    // fling_creature redraws the screen for every step when animations are on
    override_option no_animations( "ANIMATIONS", "false" );
    map &here = get_map();
    // keep the avatar out of the flight path
    get_avatar().setpos( here, tripoint_bub_ms( 60, 70, 0 ) );

    // flvel 50 gives range 5, a fling at 0 degrees goes straight east one tile per step.
    // A controlled landing at this speed deals no landing damage.
    const float flvel = 50.0f;
    const tripoint_rel_ms flight( 5, 0, 0 );

    SECTION( "monster" ) {
        const tripoint_bub_ms start( 50, 60, 0 );
        monster &zed = spawn_test_monster( "mon_zombie", start );
        const int hp_before = zed.get_hp();
        REQUIRE( g->fling_creature( &zed, 0_degrees, flvel, true, true ) );
        // Before the fix it moved one tile, hit itself on the next step and stopped there
        CHECK( zed.pos_bub( here ) == start + flight );
        CHECK( zed.get_hp() == hp_before );
    }

    SECTION( "npc" ) {
        const tripoint_bub_ms start( 50, 50, 0 );
        npc &guy = spawn_npc( start.xy(), "test_talker" );
        REQUIRE( guy.pos_bub( here ) == start );
        REQUIRE( g->fling_creature( &guy, 0_degrees, flvel, true, true ) );
        // Before the fix every step was computed from the start tile, so it ended one tile away
        CHECK( guy.pos_bub( here ) == start + flight );
    }
}
