// Repeatable performance scenarios for the world group of audit findings: inventory
// traversal, submap loading, overmap searches and zone jobs.
//
// Like tests/benchmark_scenarios_test.cpp, these are hidden ([.]) and only run when asked
// for, e.g.
//     tests/cata_test "[benchmark_scenario]"
// or through tools/benchmark/run_benchmarks.py. Each scenario builds a fixed, seeded setup
// (not timed), then times a fixed amount of work. After one untimed warm-up, it repeats that
// BENCH_REPS times (default 5) and prints one machine-readable line:
//     BENCH <name> median_ms=... min_ms=... max_ms=... reps=... work="..."
//
// Keep each scenario's work identical between versions of the code, so timings stay
// comparable. If you change a scenario, rename it so old results aren't compared with
// new ones.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "coordinates.h"
#include "item.h"
#include "item_location.h"
#include "map.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "point.h"
#include "pocket_type.h"
#include "rng.h"
#include "type_id.h"
#include "visitable.h"

static const itype_id itype_apple( "apple" );
static const itype_id itype_backpack( "backpack" );
static const itype_id itype_bag_plastic( "bag_plastic" );
static const itype_id itype_bottle_plastic( "bottle_plastic" );
static const itype_id itype_can_beans( "can_beans" );
static const itype_id itype_hammer( "hammer" );
static const itype_id itype_jacket_light( "jacket_light" );
static const itype_id itype_rock( "rock" );
static const itype_id itype_scrap( "scrap" );

static const quality_id qual_CUT( "CUT" );
static const quality_id qual_HAMMER( "HAMMER" );

static const ter_str_id ter_t_water_dp( "t_water_dp" );

namespace
{

constexpr unsigned int scenario_seed = 4242;

int bench_reps()
{
    const char *env = std::getenv( "BENCH_REPS" );
    const int reps = env ? std::atoi( env ) : 5;
    return std::max( reps, 1 );
}

// Runs setup (untimed) then work (timed) several times and prints the result line.
void run_scenario( const std::string &name, const std::string &work_description,
                   const std::function<void()> &setup, const std::function<void()> &work )
{
    std::vector<double> samples;
    const int reps = bench_reps();
    // One untimed warm-up repetition first, so cold caches and first-use allocations
    // don't inflate the first sample.
    for( int r = -1; r < reps; ++r ) {
        rng_set_engine_seed( scenario_seed );
        setup();
        const auto start = std::chrono::steady_clock::now();
        work();
        const auto end = std::chrono::steady_clock::now();
        if( r >= 0 ) {
            samples.push_back( std::chrono::duration<double, std::milli>( end - start ).count() );
        }
    }
    std::sort( samples.begin(), samples.end() );
    std::printf( "BENCH %s median_ms=%.2f min_ms=%.2f max_ms=%.2f reps=%d work=\"%s\"\n",
                 name.c_str(), samples[samples.size() / 2], samples.front(), samples.back(), reps,
                 work_description.c_str() );
    std::fflush( stdout );
}

// Gives the avatar a backpack holding a mixed load, as benchmark_scenarios_test.cpp does,
// plus a light jacket and a few plastic bags of rocks, so some items sit two containers deep.
void fill_nested_inventory( avatar &u )
{
    u.wear_item( item( itype_backpack ), false );
    u.wear_item( item( itype_jacket_light ), false );
    const std::vector<itype_id> kinds = { itype_apple, itype_rock, itype_scrap, itype_can_beans,
                                          itype_bottle_plastic
                                        };
    for( int i = 0; i < 60; ++i ) {
        u.i_add( item( kinds[i % kinds.size()] ) );
    }
    for( int i = 0; i < 4; ++i ) {
        item bag( itype_bag_plastic );
        for( int j = 0; j < 3; ++j ) {
            REQUIRE( bag.put_in( item( itype_rock ), pocket_type::CONTAINER ).success() );
        }
        u.i_add( bag );
    }
}

// Restores calendar::turn when it goes out of scope.
struct restore_turn {
    time_point saved = calendar::turn;
    ~restore_turn() {
        calendar::turn = saved;
    }
};

// Four overmap tiles of the reality bubble (clear_map generated them) that the lake
// scenarios flood, away from the avatar's corner.
std::vector<tripoint_abs_omt> lake_omts()
{
    const tripoint_abs_omt base = project_to<coords::omt>( get_map().get_abs_sub() );
    return { base + tripoint_rel_omt( 2, 2, 0 ), base + tripoint_rel_omt( 3, 2, 0 ),
             base + tripoint_rel_omt( 2, 3, 0 ), base + tripoint_rel_omt( 3, 3, 0 ) };
}

// Turns the surface of an overmap tile into deep water, clearing any ice.
void flood( const tripoint_abs_omt &omt )
{
    map &here = get_map();
    const tripoint_abs_ms origin = project_to<coords::ms>( omt );
    for( int y = 0; y < 2 * SEEY; ++y ) {
        for( int x = 0; x < 2 * SEEX; ++x ) {
            here.ter_set( here.get_bub( origin + tripoint_rel_ms( x, y, 0 ) ), ter_t_water_dp );
        }
    }
}

// Surface tiles of an overmap tile that aren't deep water any more (frozen).
int frozen_tiles( const tripoint_abs_omt &omt )
{
    map &here = get_map();
    const tripoint_abs_ms origin = project_to<coords::ms>( omt );
    int frozen = 0;
    for( int y = 0; y < 2 * SEEY; ++y ) {
        for( int x = 0; x < 2 * SEEX; ++x ) {
            frozen += here.ter( here.get_bub( origin + tripoint_rel_ms( x, y, 0 ) ) ) != ter_t_water_dp;
        }
    }
    return frozen;
}

// Loading a smallmap runs the phase change pass over its submaps on every z-level.
void load_smallmap( const tripoint_abs_omt &omt )
{
    smallmap sm;
    sm.load( omt, false );
}

// Noon of the first day of the given season, plus some days.
time_point day_in_season( season_type season, int days_in )
{
    time_point t = calendar::turn_zero + 12_hours;
    while( season_of_year( t ) != season ) {
        t += 1_days;
    }
    return t + days_in * 1_days;
}

// The first winter noon on which the whole lake freezes when loaded, so the frozen
// scenario re-freezes every lake tile on every load.
time_point freezing_day()
{
    const std::vector<tripoint_abs_omt> omts = lake_omts();
    const time_point first = day_in_season( WINTER, 0 );
    for( int day = 0; day < 91; ++day ) {
        calendar::turn = first + day * 1_days;
        int frozen = 0;
        for( const tripoint_abs_omt &omt : omts ) {
            flood( omt );
            load_smallmap( omt );
            frozen += frozen_tiles( omt );
        }
        if( frozen == static_cast<int>( omts.size() ) * 4 * SEEX * SEEY ) {
            return calendar::turn;
        }
    }
    return calendar::before_time_starts;
}

} // namespace

// INV-P02: every inventory query walks the carried items through item_location, building a
// location (and its heap-allocated implementation) for each item it visits.
TEST_CASE( "bench_inventory_queries", "[.][benchmark_scenario]" )
{
    run_scenario( "inventory_queries",
                  "amount, charges, quality, filter and wielded-item queries on a full inventory, 300 rounds",
    []() {
        clear_avatar();
        fill_nested_inventory( get_avatar() );
    }, []() {
        const avatar &u = get_avatar();
        int64_t checksum = 0;
        for( int round = 0; round < 300; ++round ) {
            checksum += u.amount_of( itype_rock );
            checksum += u.charges_of( itype_scrap );
            checksum += u.has_quality( qual_HAMMER ) ? 1 : 0;
            checksum += u.max_quality( qual_CUT );
            checksum += static_cast<int64_t>( u.items_with( []( const item & it ) {
                return it.is_food();
            } ).size() );
            // no hammer is carried, so this walks everything
            checksum += u.has_item_with( []( const item & it ) {
                return it.typeId() == itype_hammer;
            } ) ? 1 : 0;
            // unarmed
            for( int i = 0; i < 10; ++i ) {
                checksum += u.get_wielded_item() ? 1 : 0;
            }
        }
        CHECK( checksum > 0 );
    } );
}

// MGEN-P01: loading a submap runs the phase change pass on each of its tiles, on every
// z-level. Each lake tile samples the weather ten times. Here, in summer, nothing freezes.
TEST_CASE( "bench_phase_change_lake_thawed", "[.][benchmark_scenario]" )
{
    restore_turn turn_guard;
    run_scenario( "phase_change_lake_thawed",
                  "40 smallmap loads over 4 overmap tiles of open lake in summer",
    []() {
        clear_avatar();
        clear_map();
        get_avatar().setpos( get_map(), tripoint_bub_ms( 6, 6, 0 ) );
        calendar::turn = day_in_season( SUMMER, 30 );
        for( const tripoint_abs_omt &omt : lake_omts() ) {
            flood( omt );
        }
    }, []() {
        const std::vector<tripoint_abs_omt> omts = lake_omts();
        for( int i = 0; i < 40; ++i ) {
            load_smallmap( omts[i % omts.size()] );
        }
        CHECK( frozen_tiles( omts.front() ) == 0 );
    } );
}

// MGEN-P01: a frozen tile is thawed back to water and frozen again, through two full
// ter_set calls, every time its submap is loaded.
TEST_CASE( "bench_phase_change_lake_frozen", "[.][benchmark_scenario]" )
{
    restore_turn turn_guard;
    time_point cold_day = calendar::before_time_starts;
    run_scenario( "phase_change_lake_frozen",
                  "40 smallmap loads over 4 overmap tiles of frozen lake in winter",
    [&cold_day]() {
        clear_avatar();
        clear_map();
        get_avatar().setpos( get_map(), tripoint_bub_ms( 6, 6, 0 ) );
        if( cold_day == calendar::before_time_starts ) {
            cold_day = freezing_day();
            REQUIRE( cold_day != calendar::before_time_starts );
        }
        calendar::turn = cold_day;
        for( const tripoint_abs_omt &omt : lake_omts() ) {
            flood( omt );
            // freeze it, so the timed loads find ice with the water recorded under it
            load_smallmap( omt );
            REQUIRE( frozen_tiles( omt ) == 4 * SEEX * SEEY );
        }
    }, []() {
        const std::vector<tripoint_abs_omt> omts = lake_omts();
        for( int i = 0; i < 40; ++i ) {
            load_smallmap( omts[i % omts.size()] );
        }
        CHECK( frozen_tiles( omts.front() ) == 4 * SEEX * SEEY );
    } );
}
