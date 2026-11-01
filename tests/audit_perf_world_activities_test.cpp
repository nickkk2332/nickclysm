#include <string>
#include <vector>

#include "activity_actor_definitions.h"
#include "activity_handlers.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "clzones.h"
#include "coordinates.h"
#include "crafting_enums.h"
#include "item.h"
#include "item_location.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "map_selector.h"
#include "mapdata.h"
#include "memory_fast.h"
#include "planting_memo.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "recipe.h"
#include "ret_val.h"
#include "rng.h"
#include "type_id.h"
#include "weather.h"

static const faction_id faction_your_followers( "your_followers" );

static const itype_id itype_2x4( "2x4" );
static const itype_id itype_seed_buckwheat( "seed_buckwheat" );

static const recipe_id recipe_cudgel_test_unattended_with_qual(
    "cudgel_test_unattended_with_qual" );

static const ter_str_id ter_t_dirt( "t_dirt" );

static const zone_type_id zone_type_FARM_PLOT( "FARM_PLOT" );

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

// ACT-P02: the farm check asked warm_enough_to_plant (about 500 weather samples, each
// drawing random numbers for the wind) for every tile of a plot with a seed set, before
// looking at what the tile needs. A tile that only needs tilling now samples no weather.
TEST_CASE( "act-p02_farm_check_of_a_tile_to_till_samples_no_weather", "[audit][activity]" )
{
    clear_avatar();
    clear_map();
    zone_manager &zm = zone_manager::get_manager();
    zm.clear();
    set_time( calendar::turn_zero + 12_hours );
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms origin( 60, 60, 0 );
    const tripoint_bub_ms plot = origin + tripoint::east;
    u.setpos( here, origin );
    here.ter_set( plot, ter_t_dirt );
    REQUIRE( here.has_flag( ter_furn_flag::TFLAG_PLOWABLE, plot ) );
    // under the sky, so the planting check goes on to the weather
    REQUIRE( here.is_outside( plot ) );
    shared_ptr_fast<plot_options> options = make_shared_fast<plot_options>();
    options->deserialize(
        json_loader::from_string( R"({ "seed": "seed_buckwheat" })" ).get_object() );
    zm.add( "plot", zone_type_FARM_PLOT, faction_your_followers, false, true,
            here.get_abs( plot ), here.get_abs( plot ), options );

    const cata_default_random_engine before = rng_get_engine();
    multi_farm_activity_actor actor;
    const activity_reason_info info = actor.multi_activity_can_do( u, plot );
    CHECK( info.reason == do_activity_reason::NEEDS_TILLING );
    CHECK( rng_get_engine() == before );
    zm.clear();
}

// ACT-P02: within a farm scan, the temperatures warm_enough_to_plant checks are sampled once
// per overmap tile and seed, and give the same answer.
TEST_CASE( "act-p02_planting_memo_samples_the_weather_once_per_overmap_tile",
           "[audit][activity]" )
{
    clear_avatar();
    clear_map();
    set_time( calendar::turn_zero + 12_hours );
    map &here = get_map();
    const tripoint_abs_omt omt = project_to<coords::omt>( here.get_abs( { 60, 60, 0 } ) );
    const tripoint_bub_ms omt_origin = here.get_bub( project_to<coords::ms>( omt ) );
    const tripoint_bub_ms first = omt_origin + tripoint_rel_ms( 1, 1, 0 );
    const tripoint_bub_ms second = omt_origin + tripoint_rel_ms( 20, 20, 0 );
    const ret_val<void> uncached = warm_enough_to_plant( second, itype_seed_buckwheat );
    {
        const scoped_planting_memo memo;
        const ret_val<void> at_first = warm_enough_to_plant( first, itype_seed_buckwheat );
        const cata_default_random_engine before = rng_get_engine();
        const ret_val<void> at_second = warm_enough_to_plant( second, itype_seed_buckwheat );
        CHECK( rng_get_engine() == before );
        CHECK( at_first.success() == uncached.success() );
        CHECK( at_second.success() == uncached.success() );
        CHECK( std::string( at_second.c_str() ) == std::string( uncached.c_str() ) );
    }
    // out of the scope, each call samples again
    const cata_default_random_engine before = rng_get_engine();
    warm_enough_to_plant( second, itype_seed_buckwheat );
    CHECK( rng_get_engine() != before );
}
