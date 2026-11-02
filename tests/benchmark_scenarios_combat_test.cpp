// Repeatable performance scenarios for combat: shot pellets, shrapnel, and how NPCs pick
// an attack. They follow tests/benchmark_scenarios_test.cpp: hidden ([.]) and only run
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
#include <memory>
#include <string>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "character_id.h"
#include "coordinates.h"
#include "creature_tracker.h"
#include "explosion.h"
#include "game.h"
#include "item.h"
#include "itype.h"
#include "iuse_actor.h"
#include "magic.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "monster.h"
#include "npc.h"
#include "npc_attack.h"
#include "options_helpers.h"
#include "pimpl.h"
#include "player_helpers.h"
#include "point.h"
#include "rng.h"
#include "type_id.h"
#include "weather.h"
#include "weather_type.h"

static const itype_id itype_273x110flock( "273x110flock" );
static const itype_id itype_brogyaga( "brogyaga" );
static const itype_id itype_grenade_act( "grenade_act" );
static const itype_id itype_shot_bird( "shot_bird" );
static const itype_id itype_shotgun_s( "shotgun_s" );

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

// Fixed, mild weather, so results don't depend on the natural weather at the test map.
struct fixed_weather {
    scoped_weather_override weather{ WEATHER_CLEAR };
    fixed_weather() {
        weather.with_windspeed( 0 );
    }
};

const tripoint_bub_ms scenario_center( MAPSIZE_X / 2, MAPSIZE_Y / 2, 0 );

// A shot from a standard shooter, as the ranged balance tests fire them.
void fire_once( standard_npc &shooter, const itype_id &gun, const itype_id &ammo,
                const tripoint_bub_ms &target )
{
    arm_shooter( shooter, gun, {}, ammo );
    shooter.recoil = 0;
    shooter.fire_gun( get_map(), target, 1, *shooter.get_wielded_item() );
}

} // namespace

// CMBT-P01: a 27.3x110 flockshot round fires 24155 pellets, each walked along its own
// trajectory. One round at an unarmored target 6 tiles away (the pellets fly on past it),
// one at a target 2 tiles away whose armor stops every pellet that hits it.
TEST_CASE( "bench_shot_flockshot", "[.][benchmark_scenario]" )
{
    fixed_weather weather;
    std::unique_ptr<standard_npc> shooter;
    run_scenario( "shot_flockshot",
                  "2 rounds of 27.3x110 flockshot: at an unarmored target 6 tiles away, at an armored one 2 tiles away",
    [&shooter]() {
        clear_avatar();
        clear_map_and_put_player_underground();
        shooter = std::make_unique<standard_npc>( "Shooter", scenario_center,
                  std::vector<itype_id>(), 5, 10, 10, 10, 10 );
        spawn_test_monster( "mon_test_shotgun_0_bullet", scenario_center + tripoint_rel_ms( 6, 0, 0 ) );
        spawn_test_monster( "mon_test_shotgun_30_bullet",
                            scenario_center + tripoint_rel_ms( -2, 0, 0 ) );
    }, [&shooter]() {
        fire_once( *shooter, itype_brogyaga, itype_273x110flock,
                   scenario_center + tripoint_rel_ms( 6, 0, 0 ) );
        fire_once( *shooter, itype_brogyaga, itype_273x110flock,
                   scenario_center + tripoint_rel_ms( -2, 0, 0 ) );
    } );
    clear_creatures();
}

// CMBT-P01: the common case, 12 gauge birdshot (100 pellets) at a target 5 tiles away.
TEST_CASE( "bench_shot_birdshot", "[.][benchmark_scenario]" )
{
    fixed_weather weather;
    std::unique_ptr<standard_npc> shooter;
    run_scenario( "shot_birdshot", "30 rounds of 12 gauge birdshot at a target 5 tiles away",
    [&shooter]() {
        clear_avatar();
        clear_map_and_put_player_underground();
        shooter = std::make_unique<standard_npc>( "Shooter", scenario_center,
                  std::vector<itype_id>(), 5, 10, 10, 10, 10 );
        spawn_test_monster( "mon_test_shotgun_5_bullet", scenario_center + tripoint_rel_ms( 5, 0, 0 ) );
    }, [&shooter]() {
        for( int shot = 0; shot < 30; ++shot ) {
            fire_once( *shooter, itype_shotgun_s, itype_shot_bird,
                       scenario_center + tripoint_rel_ms( 5, 0, 0 ) );
        }
    } );
    clear_creatures();
}

// CMBT-P02: a grenade among armored zombies (bullet armor 51) that survive it, so every
// fragment that hits them is resolved against their armor: about 5400 for the one standing
// on the grenade, 170 for each of the 8 next to it and 40 for each of the 16 two tiles away.
TEST_CASE( "bench_shrapnel_armored", "[.][benchmark_scenario]" )
{
    fixed_weather weather;
    const item grenade( itype_grenade_act );
    const explosion_data &ex = dynamic_cast<const explosion_iuse *>
                               ( grenade.type->countdown_action.get_actor_ptr() )->explosion;
    run_scenario( "shrapnel_armored",
                  "1 grenade on an armored zombie, with 8 more 1 tile away and 16 more 2 tiles away",
    []() {
        clear_avatar();
        clear_map_and_put_player_underground();
        for( int dy = -2; dy <= 2; ++dy ) {
            for( int dx = -2; dx <= 2; ++dx ) {
                spawn_test_monster( "mon_zombie_armored", scenario_center + tripoint_rel_ms( dx, dy, 0 ) );
            }
        }
    }, [&ex]() {
        explosion_handler::explosion( nullptr, scenario_center, ex );
        explosion_handler::process_explosions();
    } );
    clear_creatures();
}
