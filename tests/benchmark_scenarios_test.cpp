// Repeatable performance scenarios for the engine's hot paths.
//
// These are hidden ([.]) and only run when asked for, e.g.
//     tests/cata_test "[benchmark_scenario]"
// or through tools/benchmark/run_benchmarks.py, which also times data loading and
// compares runs. Each scenario builds a fixed, seeded setup (not timed), then times a
// fixed amount of work. It repeats that BENCH_REPS times (default 5) and prints one
// machine-readable line:
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
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "coordinates.h"
#include "game.h"
#include "item.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "monster.h"
#include "options_helpers.h"
#include "player_helpers.h"
#include "point.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "requirements.h"
#include "rng.h"
#include "temp_crafting_inventory.h"
#include "type_id.h"
#include "weather.h"
#include "weather_type.h"

static const itype_id itype_2x4( "2x4" );
static const itype_id itype_apple( "apple" );
static const itype_id itype_backpack( "backpack" );
static const itype_id itype_bottle_plastic( "bottle_plastic" );
static const itype_id itype_can_beans( "can_beans" );
static const itype_id itype_flashlight( "flashlight" );
static const itype_id itype_hammer( "hammer" );
static const itype_id itype_jacket_light( "jacket_light" );
static const itype_id itype_rock( "rock" );
static const itype_id itype_scrap( "scrap" );

static const ter_str_id ter_t_brick_wall( "t_brick_wall" );
static const ter_str_id ter_t_utility_light( "t_utility_light" );

static const trait_id trait_DEBUG_NODMG( "DEBUG_NODMG" );

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
    for( int r = 0; r < reps; ++r ) {
        rng_set_engine_seed( scenario_seed );
        setup();
        const auto start = std::chrono::steady_clock::now();
        work();
        const auto end = std::chrono::steady_clock::now();
        samples.push_back( std::chrono::duration<double, std::milli>( end - start ).count() );
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

// The monster part of a game turn, as game::do_turn() and monmove() run it (without the
// avatar's input handling and screen updates, which tests can't drive).
void monster_turn()
{
    map &here = get_map();
    calendar::turn += 1_turns;
    // do_turn() rebuilds the vision caches for monsters right before monmove()
    here.build_map_cache( get_avatar().posz(), true );
    for( monster &critter : g->all_monsters() ) {
        if( critter.is_dead() ) {
            continue;
        }
        critter.process_turn();
        here.creature_in_field( critter );
        while( critter.get_moves() > 0 && !critter.is_dead() ) {
            critter.made_footstep = false;
            critter.plan();
            critter.move();
            critter.process_triggers();
            here.creature_in_field( critter );
        }
    }
    g->cleanup_dead();
}

// Spawns monsters of the given type on a grid of free tiles inside the reality bubble.
int spawn_grid( const std::string &type, int count, const tripoint_bub_ms &avoid, int min_dist,
                int z )
{
    map &here = get_map();
    int spawned = 0;
    for( int y = 6; y < MAPSIZE_Y - 6 && spawned < count; y += 6 ) {
        for( int x = 6; x < MAPSIZE_X - 6 && spawned < count; x += 6 ) {
            const tripoint_bub_ms p( x, y, z );
            if( rl_dist( p, avoid ) < min_dist || !here.passable( p ) ) {
                continue;
            }
            spawn_test_monster( type, p );
            ++spawned;
        }
    }
    return spawned;
}

// A dense base: a 30x30 area around the avatar with three items on every tile.
void build_dense_base( const tripoint_bub_ms &center )
{
    map &here = get_map();
    const std::vector<itype_id> kinds = { itype_apple, itype_2x4, itype_rock, itype_scrap,
                                          itype_bottle_plastic, itype_jacket_light, itype_can_beans,
                                          itype_hammer, itype_flashlight
                                        };
    size_t next = 0;
    for( int dy = -15; dy < 15; ++dy ) {
        for( int dx = -15; dx < 15; ++dx ) {
            const tripoint_bub_ms p = center + tripoint_rel_ms( dx, dy, 0 );
            if( p == center ) {
                continue;
            }
            for( int i = 0; i < 3; ++i ) {
                here.add_item_or_charges( p, item( kinds[next++ % kinds.size()] ) );
            }
        }
    }
}

// Gives the avatar a backpack holding a mixed load.
void fill_inventory( avatar &u )
{
    u.wear_item( item( itype_backpack ), false );
    const std::vector<itype_id> kinds = { itype_apple, itype_rock, itype_scrap, itype_can_beans,
                                          itype_bottle_plastic
                                        };
    for( int i = 0; i < 60; ++i ) {
        u.i_add( item( kinds[i % kinds.size()] ) );
    }
}

} // namespace

// MONAI-P01, MONAI-P02 and general monster planning: monsters that can't see the player
// wander with no target, which is their normal state.
TEST_CASE( "bench_monsters_idle", "[.][benchmark_scenario]" )
{
    fixed_weather weather;
    run_scenario( "monsters_idle", "200 zombies with no target, 30 turns",
    []() {
        clear_avatar();
        clear_map_and_put_player_underground();
        const int spawned = spawn_grid( "mon_zombie", 200, tripoint_bub_ms( 0, 0, 0 ), 0, 0 );
        REQUIRE( spawned == 200 );
    }, []() {
        for( int turn = 0; turn < 30; ++turn ) {
            monster_turn();
        }
    } );
}

// SPAT-P01 (A* pathing), MONAI-P02 (cub check while the player is seen): a horde paths
// around walls toward an invulnerable player.
TEST_CASE( "bench_horde_pathing", "[.][benchmark_scenario]" )
{
    fixed_weather weather;
    run_scenario( "horde_pathing", "100 zombies pathing around walls to the player, 100 turns",
    []() {
        clear_avatar();
        clear_map();
        avatar &u = get_avatar();
        if( !u.has_trait( trait_DEBUG_NODMG ) ) {
            u.toggle_trait( trait_DEBUG_NODMG );
        }
        const tripoint_bub_ms center( MAPSIZE_X / 2, MAPSIZE_Y / 2, 0 );
        u.setpos( get_map(), center );
        map &here = get_map();
        // Two rings of wall with gaps, so monsters have to path around them.
        for( const int radius : {
                 8, 16
             } ) {
            for( const tripoint_bub_ms &p : here.points_in_radius( center, radius ) ) {
                if( rl_dist( p, center ) == radius && ( p.x() + p.y() ) % 9 != 0 ) {
                    here.ter_set( p, ter_t_brick_wall );
                }
            }
        }
        here.build_map_cache( 0 );
        const int spawned = spawn_grid( "mon_zombie", 100, center, 20, 0 );
        REQUIRE( spawned == 100 );
    }, []() {
        for( int turn = 0; turn < 100; ++turn ) {
            monster_turn();
        }
    } );
}

// SPAT-P02: lighting and vision caches when the player moves every turn in a lit area.
TEST_CASE( "bench_light_vision", "[.][benchmark_scenario]" )
{
    fixed_weather weather;
    run_scenario( "light_vision", "player steps back and forth among lights, 200 cache rebuilds",
    []() {
        clear_avatar();
        clear_map();
        set_time( calendar::turn_zero + 0_hours );  // night, so lights matter
        map &here = get_map();
        const tripoint_bub_ms center( MAPSIZE_X / 2, MAPSIZE_Y / 2, 0 );
        get_avatar().setpos( here, center );
        for( int dy = -30; dy <= 30; dy += 6 ) {
            for( int dx = -30; dx <= 30; dx += 6 ) {
                if( dx != 0 || dy != 0 ) {
                    here.ter_set( center + tripoint_rel_ms( dx, dy, 0 ), ter_t_utility_light );
                }
            }
        }
        here.build_map_cache( 0 );
    }, []() {
        map &here = get_map();
        avatar &u = get_avatar();
        const tripoint_bub_ms center( MAPSIZE_X / 2, MAPSIZE_Y / 2, 0 );
        for( int step = 0; step < 200; ++step ) {
            u.setpos( here, center + tripoint_rel_ms( step % 2, 0, 0 ) );
            here.build_map_cache( 0 );
            here.update_visibility_cache( 0 );
        }
    } );
}

// MAP-P01: crossing submap boundaries over already generated terrain.
TEST_CASE( "bench_submap_travel", "[.][benchmark_scenario]" )
{
    fixed_weather weather;
    const auto walk = []( int steps, int dir ) {
        map &here = get_map();
        avatar &u = get_avatar();
        for( int i = 0; i < steps; ++i ) {
            u.setpos( here, u.pos_bub() + tripoint_rel_ms( 12 * dir, 0, 0 ) );
            g->update_map( u );
        }
    };
    run_scenario( "submap_travel_dense",
                  "20 submap crossings over generated terrain, dense base and full inventory",
    [&walk]() {
        clear_avatar();
        clear_map();
        avatar &u = get_avatar();
        const tripoint_bub_ms center( MAPSIZE_X / 2, MAPSIZE_Y / 2, 0 );
        u.setpos( get_map(), center );
        build_dense_base( center );
        fill_inventory( u );
        // Generate the terrain on the route first, so mapgen isn't timed.
        walk( 10, 1 );
        walk( 10, -1 );
    }, [&walk]() {
        walk( 10, 1 );
        walk( 10, -1 );
    } );
}

// CRAFT-P02: what the crafting menu computes for each recipe it shows, in a dense base.
TEST_CASE( "bench_crafting_recompute", "[.][benchmark_scenario]" )
{
    fixed_weather weather;
    std::vector<const recipe *> recipes;
    for( const auto &entry : recipe_dict ) {
        if( !entry.second.obsolete && !entry.second.is_practice() ) {
            recipes.push_back( &entry.second );
        }
        if( recipes.size() >= 300 ) {
            break;
        }
    }
    REQUIRE( recipes.size() == 300 );
    run_scenario( "crafting_recompute",
                  "time and craftability for 300 recipes, 5 times, in a dense base",
    []() {
        clear_avatar();
        clear_map();
        avatar &u = get_avatar();
        const tripoint_bub_ms center( MAPSIZE_X / 2, MAPSIZE_Y / 2, 0 );
        u.setpos( get_map(), center );
        build_dense_base( center );
        fill_inventory( u );
    }, [&recipes]() {
        avatar &u = get_avatar();
        int64_t checksum = 0;
        for( int redraw = 0; redraw < 5; ++redraw ) {
            u.invalidate_crafting_inventory();
            const temp_crafting_inventory &inv = u.crafting_inventory();
            for( const recipe *rec : recipes ) {
                checksum += u.expected_time_to_craft( *rec );
                checksum += rec->deduped_requirements().can_make_with_inventory( &u, inv,
                            rec->get_component_filter() );
            }
        }
        CHECK( checksum != 0 );
    } );
}
