// Repeatable performance scenarios for the engine's hot paths.
//
// These are hidden ([.]) and only run when asked for, e.g.
//     tests/cata_test "[benchmark_scenario]"
// or through tools/benchmark/run_benchmarks.py, which also times data loading and
// compares runs. Each scenario builds a fixed, seeded setup (not timed), then times a
// fixed amount of work. After one untimed warm-up, it repeats that BENCH_REPS times
// (default 5) and prints one machine-readable line:
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
#include <initializer_list>
#include <map>
#include <memory>
#include <ratio>
#include <string>
#include <utility>
#include <vector>

#include "activity_actor_definitions.h"
#include "avatar.h"
#include "build_reqs.h"
#include "calendar.h"
#include "cata_catch.h"
#include "clzones.h"
#include "construction.h"
#include "coordinates.h"
#include "crafting.h"
#include "crafting_enums.h"
#include "crafting_gui_helpers.h"
#include "enums.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "item.h"
#include "item_location.h"
#include "item_wakeup.h"
#include "json_loader.h"
#include "magic.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "map_iterator.h"
#include "map_scale_constants.h"
#include "map_selector.h"
#include "memory_fast.h"
#include "monster.h"
#include "options_helpers.h"
#include "overmap.h"
#include "overmapbuffer.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "point.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "requirements.h"
#include "rng.h"
#include "temp_crafting_inventory.h"
#include "type_id.h"
#include "units.h"
#include "vehicle.h"
#include "weather.h"
#include "weather_type.h"

static const faction_id faction_your_followers( "your_followers" );

static const furn_str_id furn_f_plant_seed( "f_plant_seed" );

static const itype_id itype_2x4( "2x4" );
static const itype_id itype_apple( "apple" );
static const itype_id itype_backpack( "backpack" );
static const itype_id itype_bottle_plastic( "bottle_plastic" );
static const itype_id itype_can_beans( "can_beans" );
static const itype_id itype_flashlight( "flashlight" );
static const itype_id itype_hammer( "hammer" );
static const itype_id itype_jacket_light( "jacket_light" );
static const itype_id itype_microwave( "microwave" );
static const itype_id itype_rock( "rock" );
static const itype_id itype_scrap( "scrap" );
static const itype_id itype_seed_buckwheat( "seed_buckwheat" );

static const recipe_id recipe_bat( "bat" );
static const recipe_id recipe_crossbow( "crossbow" );
static const recipe_id
recipe_cudgel_test_unattended_with_qual( "cudgel_test_unattended_with_qual" );

static const spell_id spell_test_spell_random_damage( "test_spell_random_damage" );

static const ter_str_id ter_t_brick_wall( "t_brick_wall" );
static const ter_str_id ter_t_dirtmound( "t_dirtmound" );
static const ter_str_id ter_t_utility_light( "t_utility_light" );

static const trait_id trait_DEBUG_NODMG( "DEBUG_NODMG" );

static const vproto_id vehicle_prototype_bus( "bus" );

static const zone_type_id zone_type_FARM_PLOT( "FARM_PLOT" );
static const zone_type_id zone_type_LOOT_AMMO( "LOOT_AMMO" );
static const zone_type_id zone_type_LOOT_BOOKS( "LOOT_BOOKS" );
static const zone_type_id zone_type_LOOT_CLOTHING( "LOOT_CLOTHING" );
static const zone_type_id zone_type_LOOT_DRINK( "LOOT_DRINK" );
static const zone_type_id zone_type_LOOT_FOOD( "LOOT_FOOD" );
static const zone_type_id zone_type_LOOT_OTHER( "LOOT_OTHER" );
static const zone_type_id zone_type_LOOT_SPARE_PARTS( "LOOT_SPARE_PARTS" );
static const zone_type_id zone_type_LOOT_TOOLS( "LOOT_TOOLS" );
static const zone_type_id zone_type_LOOT_WEAPONS( "LOOT_WEAPONS" );

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

// LOOP-P01: the weather catch-up a rain collector or solar vehicle runs when the player
// comes back after being away.
TEST_CASE( "bench_weather_catchup", "[.][benchmark_scenario]" )
{
    // Natural weather, restored to whatever was set when the scenario ends.
    scoped_weather_override natural_weather( WEATHER_NULL );
    run_scenario( "weather_catchup", "sum_conditions over 3 days at 4 locations",
    []() {
        clear_avatar();
        clear_map();
    }, []() {
        const tripoint_abs_ms base = get_avatar().pos_abs();
        const time_point start = calendar::turn_zero + 30_days;
        double checksum = 0;
        for( int i = 0; i < 4; ++i ) {
            const weather_sum sum = sum_conditions( start, start + 3_days,
                                                    base + tripoint_rel_ms( i * 24, 0, 0 ) );
            checksum += sum.rain_amount + sum.sunlight + sum.wind_amount;
        }
        CHECK( checksum > 0 );
    } );
}

// OMAP-P01, OMAP-P02: worldgen, mostly overmap special placement.
TEST_CASE( "bench_overmap_generate", "[.][benchmark_scenario]" )
{
    const point_abs_om where( 7, 7 );
    run_scenario( "overmap_generate", "generate one overmap from scratch",
    []() {
        overmap_buffer.clear();
    }, [&where]() {
        const overmap &om = overmap_buffer.get( where );
        CHECK( om.pos() == where );
    } );
    overmap_buffer.clear();
}

// The same work as crafting_recompute, inside the query cache scope the crafting menu opens
// (crafting_gui.cpp), so inventory queries use the per-type index.
TEST_CASE( "bench_crafting_recompute_scoped", "[.][benchmark_scenario]" )
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
    run_scenario( "crafting_recompute_scoped",
                  "time and craftability for 300 recipes, 5 times, in a dense base, menu cache scope",
    []() {
        clear_avatar();
        clear_map();
        avatar &u = get_avatar();
        const tripoint_bub_ms center( MAPSIZE_X / 2, MAPSIZE_Y / 2, 0 );
        u.setpos( get_map(), center );
        build_dense_base( center );
        fill_inventory( u );
    }, [&recipes]() {
        temp_crafting_inventory::query_cache_scope cache_scope;
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

// UIINV-P01: the crafting menu redraws about 100 times a second. Its info panel for one
// selection, redrawn 100 times for each of two recipes (one with tools, qualities and
// proficiencies, one with steps), in a dense base, inside the menu's query cache scope.
// "rebuilt" builds the panel on every redraw, as the menu did before (a lower bound: the old
// draw code also asked for the time and the success chance several times per redraw);
// "cached" reads it from the menu's panel cache.
TEST_CASE( "bench_crafting_panel_redraws", "[.][benchmark_scenario]" )
{
    fixed_weather weather;
    const std::vector<const recipe *> recipes = { &recipe_crossbow.obj(), &recipe_bat.obj() };
    const auto setup = []() {
        clear_avatar();
        clear_map();
        avatar &u = get_avatar();
        const tripoint_bub_ms center( MAPSIZE_X / 2, MAPSIZE_Y / 2, 0 );
        u.setpos( get_map(), center );
        build_dense_base( center );
        fill_inventory( u );
    };
    const auto redraws = [&recipes]( const bool cached ) {
        return [&recipes, cached]() {
            temp_crafting_inventory::query_cache_scope cache_scope;
            avatar &u = get_avatar();
            const std::vector<Character *> group = u.get_crafting_group();
            std::map<const recipe *, availability> availability_cache;
            const recipe_panel_context ctx{ u, group, u.get_group_available_recipes(),
                                            availability_cache };
            recipe_panel_cache panel_cache;
            size_t checksum = 0;
            for( const recipe *rec : recipes ) {
                const availability avail( u, rec );
                for( int redraw = 0; redraw < 100; ++redraw ) {
                    checksum += cached ? panel_cache.get( *rec, avail, 1, ctx ).stats_plain.size() :
                                build_recipe_panel( *rec, avail, 1, ctx ).stats_plain.size();
                }
            }
            CHECK( checksum > 0 );
        };
    };
    run_scenario( "crafting_panel_redraws_rebuilt",
                  "info panel of 2 recipes, 100 redraws each, rebuilt each time, in a dense base",
                  setup, redraws( false ) );
    run_scenario( "crafting_panel_redraws_cached",
                  "info panel of 2 recipes, 100 redraws each, from the panel cache, in a dense base",
                  setup, redraws( true ) );
}

// ACT-P01: zone sorting asks, for every item, which nearby loot zone takes it.
TEST_CASE( "bench_zone_sort_queries", "[.][benchmark_scenario]" )
{
    const tripoint_bub_ms center( MAPSIZE_X / 2, MAPSIZE_Y / 2, 0 );
    run_scenario( "zone_sort_queries",
                  "loot zone lookup for 2000 items, 9 category zones of 400 tiles each",
    [&center]() {
        clear_avatar();
        clear_map();
        zone_manager &zm = zone_manager::get_manager();
        zm.clear();
        map &here = get_map();
        const std::vector<zone_type_id> types = { zone_type_LOOT_FOOD, zone_type_LOOT_DRINK,
                                                  zone_type_LOOT_TOOLS, zone_type_LOOT_CLOTHING, zone_type_LOOT_AMMO,
                                                  zone_type_LOOT_WEAPONS, zone_type_LOOT_BOOKS, zone_type_LOOT_SPARE_PARTS,
                                                  zone_type_LOOT_OTHER
                                                };
        for( size_t i = 0; i < types.size(); ++i ) {
            const tripoint_bub_ms start = center + tripoint_rel_ms( -30 + static_cast<int>( i % 3 ) * 20,
                                          -30 + static_cast<int>( i / 3 ) * 20, 0 );
            zm.add( types[i].str(), types[i], faction_your_followers, false, true, here.get_abs( start ),
                    here.get_abs( start + tripoint_rel_ms( 19, 19, 0 ) ) );
        }
    }, [&center]() {
        const zone_manager &zm = zone_manager::get_manager();
        const tripoint_abs_ms where = get_map().get_abs( center );
        const std::vector<item> items = { item( itype_apple ), item( itype_2x4 ), item( itype_rock ),
                                          item( itype_scrap ), item( itype_bottle_plastic ), item( itype_jacket_light ),
                                          item( itype_can_beans ), item( itype_hammer ), item( itype_flashlight ),
                                          item( itype_backpack )
                                        };
        int found = 0;
        for( int i = 0; i < 200; ++i ) {
            for( const item &it : items ) {
                found += !zm.get_near_zone_type_for_item( it, where, 60, faction_your_followers ).is_null();
            }
        }
        CHECK( found > 0 );
    } );
    zone_manager::get_manager().clear();
}

// MAP-P02: temporary maps, which mapgen, NPC activities, blueprints and vehicles load all
// the time, each with a terrain edit like the ones mapgen and construction make.
TEST_CASE( "bench_temp_map_load", "[.][benchmark_scenario]" )
{
    run_scenario( "temp_map_load",
                  "load 100 tinymaps and 20 smallmaps over generated terrain, one wall edit each",
    []() {
        clear_avatar();
        clear_map();
    }, []() {
        const tripoint_abs_omt base = project_to<coords::omt>( get_map().get_abs_sub() );
        int edits = 0;
        const auto edit = [&edits]( tinymap & tm, int i ) {
            const tripoint_omt_ms p( i % SEEX, ( i * 7 ) % SEEY, 0 );
            const ter_id old_ter = tm.ter( p );
            edits += tm.ter_set( p, ter_t_brick_wall ) ? 1 : 0;
            tm.ter_set( p, old_ter );
        };
        // OMTs well inside the reality bubble, which clear_map has already generated
        for( int i = 0; i < 100; ++i ) {
            tinymap tm;
            tm.load( base + tripoint_rel_omt( 1 + i % 4, 1 + ( i / 4 ) % 4, 0 ), false );
            edit( tm, i );
        }
        for( int i = 0; i < 20; ++i ) {
            smallmap sm;
            sm.load( base + tripoint_rel_omt( 1 + i % 4, 1 + ( i / 4 ) % 4, 0 ), false );
            edit( sm, i );
        }
        CHECK( edits == 120 );
    } );
}

// VEH-P01: the collision check vehmove runs for every tile a moving vehicle advances.
TEST_CASE( "bench_vehicle_collision_check", "[.][benchmark_scenario]" )
{
    vehicle *bus = nullptr;
    run_scenario( "vehicle_collision_check",
                  "collision check of a moving bus over open ground, 2000 tile steps",
    [&bus]() {
        clear_avatar();
        clear_map();
        map &here = get_map();
        get_avatar().setpos( here, tripoint_bub_ms( 30, 30, 0 ) );
        bus = here.add_vehicle( vehicle_prototype_bus, tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0,
                                veh_spawn_status::UNDAMAGED );
        REQUIRE( bus != nullptr );
        bus->velocity = 1000;
    }, [&bus]() {
        map &here = get_map();
        std::vector<veh_collision> colls;
        for( int step = 0; step < 2000; ++step ) {
            colls.clear();
            bus->collision( here, colls, tripoint_rel_ms( step % 2 == 0 ? 1 : -1, 0, 0 ), false );
        }
        CHECK( colls.empty() );
    } );
}

// CRAFT-P01: the crafter waits (the default plan) on an unattended step that needs an
// oven, in a dense base. The game runs the item wakeups each turn, then the activity.
TEST_CASE( "bench_craft_unattended_wait", "[.][benchmark_scenario]" )
{
    const tripoint_bub_ms center( MAPSIZE_X / 2, MAPSIZE_Y / 2, 0 );
    run_scenario( "craft_unattended_wait",
                  "300 turns waiting on an unattended step needing an oven, in a dense base",
    [&center]() {
        clear_avatar();
        clear_map();
        avatar &u = get_avatar();
        map &here = get_map();
        u.setpos( here, center );
        build_dense_base( center );
        fill_inventory( u );
        u.i_add( item( itype_microwave ) );
        u.invalidate_crafting_inventory();
        item ingredient( itype_2x4, calendar::turn );
        item placed( &recipe_cudgel_test_unattended_with_qual.obj(), 1, ingredient );
        item &on_map = here.add_item( center, placed );
        REQUIRE( on_map.is_craft() );
        on_map.set_current_step( 1 );
        on_map.set_crafter_id( u.getID() );
        on_map.set_step_plans( std::vector<attention_plan>( 2 ) );
        item_location loc( map_cursor( here.get_abs( center ) ), &on_map );
        craft_stamp_passive_entry( on_map, u, calendar::turn, loc );
        u.activity = player_activity( craft_activity_actor( loc, false ) );
        u.activity.targets.push_back( loc );
    }, []() {
        avatar &u = get_avatar();
        for( int turn = 0; turn < 300; ++turn ) {
            calendar::turn += 1_turns;
            get_item_wakeups().process( calendar::turn );
            u.set_moves( u.get_speed() );
            u.activity.do_turn( u );
        }
        // the 10 minute step is still running
        CHECK( u.activity );
    } );
}

// ACT-P02: a farm scan over a growing plot, which asks every tile whether it is warm enough
// to plant.
TEST_CASE( "bench_farm_scan", "[.][benchmark_scenario]" )
{
    const tripoint_bub_ms center( MAPSIZE_X / 2, MAPSIZE_Y / 2, 0 );
    run_scenario( "farm_scan", "5 check-only farm scans of a 10x10 plot of planted seeds",
    [&center]() {
        clear_avatar();
        clear_map();
        zone_manager &zm = zone_manager::get_manager();
        zm.clear();
        set_time( calendar::turn_zero + 12_hours );
        avatar &u = get_avatar();
        map &here = get_map();
        u.setpos( here, center );
        const tripoint_bub_ms corner = center + tripoint_rel_ms( 2, 2, 0 );
        for( int dy = 0; dy < 10; ++dy ) {
            for( int dx = 0; dx < 10; ++dx ) {
                const tripoint_bub_ms p = corner + tripoint_rel_ms( dx, dy, 0 );
                here.ter_set( p, ter_t_dirtmound );
                here.furn_set( p, furn_f_plant_seed );
                here.add_item( p, item( itype_seed_buckwheat ) );
            }
        }
        shared_ptr_fast<plot_options> options = make_shared_fast<plot_options>();
        options->deserialize(
            json_loader::from_string( R"({ "seed": "seed_buckwheat" })" ).get_object() );
        const tripoint_bub_ms far_corner = corner + tripoint_rel_ms( 9, 9, 0 );
        zm.add( "plot", zone_type_FARM_PLOT, faction_your_followers, false, true,
                here.get_abs( corner ), here.get_abs( far_corner ), options );
        here.build_map_cache( 0 );
    }, []() {
        avatar &u = get_avatar();
        int scans_with_work = 0;
        for( int scan = 0; scan < 5; ++scan ) {
            multi_farm_activity_actor actor( true );
            player_activity act( actor );
            scans_with_work += actor.simulate_turn( act, u, true ) ? 1 : 0;
        }
        // seeds are growing, nothing to do yet
        CHECK( scans_with_work == 0 );
    } );
    zone_manager::get_manager().clear();
}

// TERR-P01: the build requirements recipe finalization works out for every mapgen-parameter
// combination of every autocalc blueprint, thousands of calls per data load.
TEST_CASE( "bench_blueprint_build_reqs", "[.][benchmark_scenario]" )
{
    // stand-in blueprints: every terrain and furniture some construction builds, in groups
    // of ten
    using changed_ids = std::pair<std::map<ter_id, int>, std::map<furn_id, int>>;
    std::vector<changed_ids> blueprints( 1 );
    int in_last = 0;
    for( const construction &con : get_constructions() ) {
        if( con.post_terrain.empty() ) {
            continue;
        }
        if( con.post_is_furniture ) {
            const furn_str_id furn( con.post_terrain );
            if( !furn.is_valid() ) {
                continue;
            }
            blueprints.back().second[furn.id()] += 1;
        } else {
            const ter_str_id ter( con.post_terrain );
            if( !ter.is_valid() ) {
                continue;
            }
            blueprints.back().first[ter.id()] += 1;
        }
        if( ++in_last == 10 ) {
            blueprints.emplace_back();
            in_last = 0;
        }
    }
    REQUIRE( blueprints.size() > 20 );
    run_scenario( "blueprint_build_reqs",
                  "build reqs of each constructible terrain and furniture, 10 a call, 5 passes",
    []() {}, [&blueprints]() {
        int64_t total_time = 0;
        for( int pass = 0; pass < 5; ++pass ) {
            for( const changed_ids &blueprint : blueprints ) {
                total_time += get_build_reqs_for_furn_ter_ids( blueprint ).time;
            }
        }
        CHECK( total_time > 0 );
    } );
}

// MAGIC-P01: the "Study a spell" menu works out the RANDOM_DAMAGE row on every redraw.
TEST_CASE( "bench_spellbook_damage_preview", "[.][benchmark_scenario]" )
{
    run_scenario( "spellbook_damage_preview",
                  "damage range at max level and per level of a RANDOM_DAMAGE spell, 200 redraws",
    []() {
        clear_avatar();
    }, []() {
        const spell_type &sp = spell_test_spell_random_damage.obj();
        int checksum = 0;
        for( int redraw = 0; redraw < 200; ++redraw ) {
            checksum += sp.damage_at_max_level().second;
            checksum += static_cast<int>( sp.calculate_damage_increment().first );
        }
        CHECK( checksum != 0 );
    } );
}
