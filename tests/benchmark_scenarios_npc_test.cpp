// Repeatable performance scenarios for NPC decisions: walking the behavior tree that picks
// each NPC's goal. They follow tests/benchmark_scenarios_test.cpp: hidden ([.]) and only run
// when asked for, e.g.
//     tests/cata_test "[benchmark_scenario]"
// or through tools/benchmark/run_benchmarks.py. Each scenario builds a fixed, seeded setup
// (not timed), then times a fixed amount of work. After one untimed warm-up, it repeats
// that BENCH_REPS times (default 5) and prints one machine-readable line:
//     BENCH <name> median_ms=... min_ms=... max_ms=... reps=... work="..."
//
// Keep each scenario's work identical between versions of the code, so timings stay
// comparable. If you change a scenario, rename it so old results aren't compared with
// new ones.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include "avatar.h"
#include "behavior.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "character_oracle.h"
#include "coordinates.h"
#include "item.h"
#include "item_group.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "npc.h"
#include "options_helpers.h"
#include "player_helpers.h"
#include "point.h"
#include "rng.h"
#include "type_id.h"
#include "weather.h"
#include "weather_type.h"

static const faction_id faction_your_followers( "your_followers" );

static const item_group_id Item_spawn_data_test_bottle_water( "test_bottle_water" );

static const itype_id itype_backpack( "backpack" );

static const string_id<behavior::node_t> behavior_node_t_npc_decision( "npc_decision" );
static const string_id<behavior::node_t>
behavior_node_t_test_bench_decision( "test_bench_decision" );
static const string_id<behavior::node_t>
behavior_node_t_test_bench_decision_cached( "test_bench_decision_cached" );

namespace
{

constexpr unsigned int scenario_seed = 4242;

int bench_reps()
{
    const char *env = std::getenv( "BENCH_REPS" );
    const int reps = env ? static_cast<int>( std::strtol( env, nullptr, 10 ) ) : 5;
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
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        work();
        const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
        if( r >= 0 ) {
            samples.push_back( std::chrono::duration<double, std::milli>( end - start ).count() );
        }
    }
    std::sort( samples.begin(), samples.end() );
    std::printf( "BENCH %s median_ms=%.2f min_ms=%.2f max_ms=%.2f reps=%d work=\"%s\"\n",
                 name.c_str(), samples[samples.size() / 2], samples.front(), samples.back(), reps,
                 work_description.c_str() );
    static_cast<void>( std::fflush( stdout ) );
}

// Fixed, mild weather, so results don't depend on the natural weather at the test map.
struct fixed_weather {
    scoped_weather_override weather{ WEATHER_CLEAR };
    fixed_weather() {
        weather.with_windspeed( 0 );
    }
};

constexpr int decision_npc_count = 20;
constexpr int decision_turns = 300;

// 20 NPCs around the player in daylight, in two rows 4 tiles above and below them, 2 to 18
// tiles away. Every fourth NPC is a follower; the next a guard (at its post, or 6 tiles from
// it); the next is thirsty and carries water; the last has nothing to do.
std::vector<npc *> spawn_decision_npcs()
{
    map &here = get_map();
    clear_avatar();
    clear_npcs();
    clear_map();
    clear_vehicles();
    const tripoint_bub_ms center( 60, 60, 0 );
    get_avatar().setpos( here, center );
    set_time( calendar::turn_zero + 12_hours );
    std::vector<npc *> npcs;
    for( int i = 0; i < decision_npc_count; ++i ) {
        const point_bub_ms p( 42 + 4 * ( i % 10 ), i < 10 ? 56 : 64 );
        npc &guy = spawn_npc( p, "test_talker" );
        clear_character( guy );
        guy.setpos( here, tripoint_bub_ms( p, 0 ) );
        switch( i % 4 ) {
            case 0:
                guy.set_fac( faction_your_followers );
                guy.set_attitude( NPCATT_FOLLOW );
                break;
            case 1:
                guy.set_guard_pos( i % 8 == 1 ? guy.pos_abs() : guy.pos_abs() + tripoint( 6, 0, 0 ) );
                break;
            case 2: {
                guy.set_thirst( 700 );
                guy.wear_item( item( itype_backpack ), false );
                const item_group::ItemList water = item_group::items_from( Item_spawn_data_test_bottle_water );
                guy.i_add( water.front() );
                break;
            }
            default:
                break;
        }
        npcs.push_back( &guy );
    }
    here.build_map_cache( 0 );
    return npcs;
}

// The decision part of npc::move(): a fresh oracle and tree for every NPC, every turn.
int walk_decision_tree( const std::vector<npc *> &npcs, const behavior::node_t &root )
{
    int busy = 0;
    for( int turn = 0; turn < decision_turns; ++turn ) {
        calendar::turn += 1_turns;
        for( npc *guy : npcs ) {
            behavior::character_oracle_t oracle( guy );
            behavior::tree decision;
            decision.add( &root );
            if( decision.tick_full( &oracle ).first != "idle" ) {
                ++busy;
            }
        }
    }
    return busy;
}

void decision_scenario( const std::string &name, const std::string &work_description,
                        const string_id<behavior::node_t> &root )
{
    fixed_weather weather;
    std::vector<npc *> npcs;
    run_scenario( name, work_description, [&npcs]() {
        npcs = spawn_decision_npcs();
        behavior::clear_evaluation_cache();
    }, [&npcs, &root]() {
        // the followers, guards and thirsty NPCs all have something to do
        CHECK( walk_decision_tree( npcs, root.obj() ) >= decision_turns * decision_npc_count / 2 );
    } );
    clear_npcs();
    clear_avatar();
}

} // namespace

// NPC-J03: npc::move() walks the npc_decision tree for every NPC, every turn.
TEST_CASE( "bench_npc_decision", "[.][benchmark_scenario]" )
{
    decision_scenario( "npc_decision",
                       "300 turns of the npc_decision tree for 20 NPCs: followers, guards, thirsty and idle",
                       behavior_node_t_npc_decision );
}

// NPC-J03: the same, with four nodes added to the priorities
// (data/mods/TEST_DATA/npc_behavior_bench.json): a trait and math condition with a math
// score, a math condition and score calling oracle(), a time condition, and a built-in
// condition with a scaled named score.
TEST_CASE( "bench_npc_decision_condition_math", "[.][benchmark_scenario]" )
{
    decision_scenario( "npc_decision_condition_math",
                       "300 turns of npc_decision with 4 condition and math nodes added, for 20 NPCs",
                       behavior_node_t_test_bench_decision );
}

// NPC-J03: the same nodes, each with "evaluate_every": "5 turns".
TEST_CASE( "bench_npc_decision_condition_math_cached", "[.][benchmark_scenario]" )
{
    decision_scenario( "npc_decision_condition_math_cached",
                       "300 turns of npc_decision with the 4 condition and math nodes evaluated every 5 turns, for 20 NPCs",
                       behavior_node_t_test_bench_decision_cached );
}
