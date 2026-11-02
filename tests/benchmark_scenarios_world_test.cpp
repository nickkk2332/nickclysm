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
#include "player_helpers.h"
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
