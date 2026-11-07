// Repeatable performance scenarios for sound processing: monsters, traps and the avatar
// hearing the sounds of a turn. They follow tests/benchmark_scenarios_test.cpp: hidden ([.])
// and only run when asked for, e.g.
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
#include <ratio>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "coordinates.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "map_scale_constants.h"
#include "options_helpers.h"
#include "player_helpers.h"
#include "point.h"
#include "rng.h"
#include "sounds.h"
#include "type_id.h"
#include "weather_type.h"

static const ter_str_id ter_t_brick_wall( "t_brick_wall" );
static const ter_str_id ter_test_t_soundproof_wall( "test_t_soundproof_wall" );

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

struct fixed_weather {
    scoped_weather_override weather{ WEATHER_CLEAR };
    fixed_weather() {
        weather.with_windspeed( 0 );
    }
};

constexpr int monster_count = 300;
constexpr int sounds_per_turn = 20;
constexpr int footsteps_per_turn = 100;
constexpr int turns = 100;

const tripoint_bub_ms bench_center( MAPSIZE_X / 2, MAPSIZE_Y / 2, 0 );

// A noisy turn: the sounds monsters, traps and the avatar react to, at fixed spots
struct noise_plan {
    std::vector<std::pair<tripoint_bub_ms, int>> sounds;
    std::vector<tripoint_bub_ms> footsteps;
};

std::vector<noise_plan> plan;

// The avatar in the middle, a grid of wall lines with gaps (of `wall` terrain, or none),
// 300 zombies spread over the bubble and the sounds of 100 turns drawn from the seed.
void build_noisy_town( const ter_str_id &wall )
{
    clear_avatar();
    clear_map();
    sounds::reset_sounds();
    map &here = get_map();
    get_avatar().setpos( here, bench_center );
    if( !wall.is_null() ) {
        // walls every 12 tiles both ways, with a door-wide gap every 6
        for( int x = 3; x < MAPSIZE_X - 3; ++x ) {
            for( int y = 3; y < MAPSIZE_Y - 3; ++y ) {
                const bool on_line = x % 12 == 3 || y % 12 == 3;
                const bool gap = ( x + y ) % 6 == 0;
                const tripoint_bub_ms p( x, y, 0 );
                if( on_line && !gap && p != bench_center ) {
                    here.ter_set( p, wall );
                }
            }
        }
    }
    int spawned = 0;
    for( int y = 5; y < MAPSIZE_Y - 5 && spawned < monster_count; y += 6 ) {
        for( int x = 5; x < MAPSIZE_X - 5 && spawned < monster_count; x += 6 ) {
            const tripoint_bub_ms p( x, y, 0 );
            if( p != bench_center && here.passable( p ) ) {
                spawn_test_monster( "mon_zombie", p );
                ++spawned;
            }
        }
    }
    REQUIRE( spawned == monster_count );
    here.build_map_cache( 0 );
    plan.clear();
    for( int turn = 0; turn < turns; ++turn ) {
        noise_plan noises;
        for( int i = 0; i < sounds_per_turn; ++i ) {
            noises.sounds.emplace_back( tripoint_bub_ms( rng( 6, MAPSIZE_X - 7 ), rng( 6, MAPSIZE_Y - 7 ), 0 ),
                                        rng( 20, 60 ) );
        }
        for( int i = 0; i < footsteps_per_turn; ++i ) {
            noises.footsteps.emplace_back( rng( 6, MAPSIZE_X - 7 ), rng( 6, MAPSIZE_Y - 7 ), 0 );
        }
        plan.push_back( std::move( noises ) );
    }
}

// What do_turn() does with a turn's sounds: the avatar's hearing and markers, then the
// monsters and traps
void hear_planned_turns()
{
    avatar &u = get_avatar();
    for( const noise_plan &noises : plan ) {
        for( const std::pair<tripoint_bub_ms, int> &snd : noises.sounds ) {
            sounds::sound( snd.first, snd.second, sounds::sound_t::destructive_activity, "a crash" );
        }
        for( const tripoint_bub_ms &p : noises.footsteps ) {
            sounds::add_footstep( p, 6, 0, nullptr, "footsteps" );
        }
        sounds::process_sound_markers( &u );
        sounds::reset_markers();
        sounds::process_sounds();
    }
}

} // namespace

// SPAT-J01: sounds and footsteps heard by 300 zombies and the avatar. With no terrain that
// muffles on the level the muffling walk is skipped, so vanilla walls cost what open
// ground costs; walls with a sound_attenuation walk one line per sound cluster and hearer.
TEST_CASE( "bench_sound_open_ground", "[.][benchmark_scenario]" )
{
    fixed_weather weather;
    run_scenario( "sound_open_ground",
                  "300 zombies and the avatar hear 20 sounds and 100 footsteps a turn, 100 turns, open ground",
    []() {
        build_noisy_town( ter_str_id::NULL_ID() );
    }, []() {
        hear_planned_turns();
    } );
    clear_map();
}

TEST_CASE( "bench_sound_vanilla_walls", "[.][benchmark_scenario]" )
{
    fixed_weather weather;
    run_scenario( "sound_vanilla_walls",
                  "the same among brick walls, which muffle nothing",
    []() {
        build_noisy_town( ter_t_brick_wall );
    }, []() {
        hear_planned_turns();
    } );
    clear_map();
}

TEST_CASE( "bench_sound_muffling_walls", "[.][benchmark_scenario]" )
{
    fixed_weather weather;
    run_scenario( "sound_muffling_walls",
                  "the same among walls with a sound_attenuation (TEST_DATA)",
    []() {
        build_noisy_town( ter_test_t_soundproof_wall );
    }, []() {
        hear_planned_turns();
    } );
    clear_map();
}
