#include <vector>

#include "activity_actor_definitions.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "coordinates.h"
#include "crafting_enums.h"
#include "item.h"
#include "item_location.h"
#include "map.h"
#include "map_helpers.h"
#include "map_selector.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "recipe.h"
#include "type_id.h"

static const itype_id itype_2x4( "2x4" );

static const recipe_id recipe_cudgel_test_unattended_with_qual(
    "cudgel_test_unattended_with_qual" );

// CRAFT-P01: a crafter waiting (do_wait) on an unattended step reran the whole env check,
// reservations and inventory scans included, every turn, although its poll was armed a
// minute ahead and the item wakeups run it then. A waiting turn now runs it only when the
// poll is due (or none is armed).
TEST_CASE( "craft-p01_waiting_turn_runs_env_check_only_when_due", "[audit][craft]" )
{
    clear_avatar();
    clear_map();
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms origin( 60, 60, 0 );
    u.setpos( here, origin );
    // No oven anywhere, so the env check pauses the step whenever it runs.

    item ingredient( itype_2x4, calendar::turn );
    item placed( &recipe_cudgel_test_unattended_with_qual.obj(), 1, ingredient );
    item &on_map = here.add_item( origin, placed );
    REQUIRE( on_map.is_craft() );
    on_map.set_current_step( 1 );
    on_map.set_crafter_id( u.getID() );
    // all defaults: do_wait
    on_map.set_step_plans( std::vector<attention_plan>( 2 ) );

    const time_point t0 = calendar::turn;
    on_map.set_passive_started_at( t0 );
    on_map.set_ready_at( t0 + 10_minutes );
    on_map.set_env_check_at( t0 + 1_minutes );

    item_location loc( map_cursor( here.get_abs( origin ) ), &on_map );
    u.activity = player_activity( craft_activity_actor( loc, /*is_long=*/false ) );
    u.activity.targets.push_back( loc );

    for( int turn = 0; turn < 3; ++turn ) {
        u.set_moves( u.get_speed() );
        u.activity.do_turn( u );
        calendar::turn += 1_turns;
    }
    // Before the fix the first waiting turn already ran the check and paused the step.
    CHECK( on_map.get_pause_started_at() == calendar::before_time_starts );

    calendar::turn = t0 + 1_minutes;
    u.set_moves( u.get_speed() );
    u.activity.do_turn( u );
    CHECK( on_map.get_pause_started_at() != calendar::before_time_starts );
    CHECK( on_map.get_saved_ready_at() == t0 + 10_minutes );
}
