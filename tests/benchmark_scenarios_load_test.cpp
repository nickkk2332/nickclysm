// Load and save scenarios for the benchmark suite: statistics rebuilt on game load, map
// quads saved and looked up in a compressed world, nested items read from a save.
//
// These are hidden ([.]) and only run when asked for, e.g.
//     tests/cata_test "[benchmark_scenario]"
// or through tools/benchmark/run_benchmarks.py. They follow tests/benchmark_scenarios_test.cpp:
// an untimed, seeded setup, one untimed warm-up, then BENCH_REPS timed repetitions (default
// 5), reported as one line:
//     BENCH <name> median_ms=... min_ms=... max_ms=... reps=... work="..."
//
// Keep each scenario's work identical between versions of the code, so timings stay
// comparable. If you change a scenario, rename it so old results aren't compared with
// new ones.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <ratio>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "calendar.h"
#include "cata_catch.h"
#include "cata_path.h"
#include "cata_variant.h"
#include "coordinates.h"
#include "event.h"
#include "filesystem.h"
#include "flexbuffer_json.h"
#include "item.h"
#include "json.h"
#include "json_loader.h"
#include "map_scale_constants.h"
#include "mapbuffer.h"
#include "overmap.h"
#include "overmapbuffer.h"
#include "path_info.h"
#include "pocket_type.h"
#include "point.h"
#include "ret_val.h"
#include "rng.h"
#include "stats_tracker.h"
#include "string_formatter.h"
#include "submap.h"
#include "type_id.h"
#include "worldfactory.h"

class event_statistic;

static const itype_id itype_bottle_plastic( "bottle_plastic" );
static const itype_id itype_can_beans( "can_beans" );
static const itype_id itype_flashlight( "flashlight" );
static const itype_id itype_hammer( "hammer" );
static const itype_id itype_scrap( "scrap" );
static const itype_id itype_spoon( "spoon" );
static const itype_id itype_test_waterproof_bag( "test_waterproof_bag" );

static const string_id<event_statistic>
event_statistic_last_overmap_special_avatar_entered( "last_overmap_special_avatar_entered" );

static const ter_str_id ter_t_dirt( "t_dirt" );

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

// The overmap buffer as the test harness sets it up: only the overmap at the origin,
// with no specials.
void restore_test_overmaps()
{
    overmap_buffer.clear();
    overmap_special_batch empty_specials( point_abs_om{} );
    overmap_buffer.create_custom_overmap( point_abs_om{}, empty_specials );
}

void remove_saved_overmap( const point_abs_om &om )
{
    const std::string name = overmapbuffer::terrain_filename( om );
    const cata_path dimension = PATH_INFO::current_dimension_save_path();
    remove_file( dimension / name );
    remove_file( dimension / zzip_overmap_directory / ( name + std::string( zzip_suffix ) ) );
    remove_file( overmapbuffer::player_filename( om ) );
}

// The map quads these scenarios write are in segment (3, 3) of every z-level, which no other
// test uses.
constexpr int scenario_segment = 3;

cata_path segment_dir( int z )
{
    return PATH_INFO::current_dimension_save_path() / "maps" /
           string_format( "%d.%d.%d", scenario_segment, scenario_segment, z );
}

void remove_scenario_segments()
{
    for( int z = -OVERMAP_DEPTH; z <= OVERMAP_HEIGHT; ++z ) {
        const cata_path dir = segment_dir( z );
        cata_path zzip = dir;
        zzip += zzip_suffix;
        std::error_code ec;
        std::filesystem::remove( zzip.get_unrelative_path(), ec );
        std::filesystem::remove_all( dir.get_unrelative_path(), ec );
    }
}

// Adds the four submaps of an OMT to a mapbuffer: uniform rock, or with one dirt tile, so
// that the quad is saved.
void add_quad( mapbuffer &mb, const tripoint_abs_omt &omt, bool uniform )
{
    const tripoint_abs_sm base = project_to<coords::sm>( omt );
    for( int dx = 0; dx < 2; ++dx ) {
        for( int dy = 0; dy < 2; ++dy ) {
            std::unique_ptr<submap> sm = std::make_unique<submap>();
            if( !uniform ) {
                sm->set_ter( point_sm_ms( 1, 1 ), ter_t_dirt.id() );
            }
            REQUIRE( mb.add_submap( base + tripoint_rel_sm( dx, dy, 0 ), sm ) );
        }
    }
}

// Gives every z-level's segment a saved quad, as a world the player has explored has.
void prepare_scenario_segments()
{
    remove_scenario_segments();
    mapbuffer mb;
    const int omt = scenario_segment * SEG_SIZE;
    for( int z = -OVERMAP_DEPTH; z <= OVERMAP_HEIGHT; ++z ) {
        add_quad( mb, tripoint_abs_omt( omt, omt, z ), false );
    }
    mb.save( true );
}

// A body bag holding six assorted items and, above the bottom level, two more such bags.
item bag_tree( int levels )
{
    item bag( itype_test_waterproof_bag );
    for( const itype_id &kind : {
             itype_scrap, itype_can_beans, itype_bottle_plastic, itype_flashlight, itype_hammer,
             itype_spoon
         } ) {
        REQUIRE( bag.put_in( item( kind ), pocket_type::CONTAINER ).success() );
    }
    if( levels > 1 ) {
        for( int i = 0; i < 2; ++i ) {
            REQUIRE( bag.put_in( bag_tree( levels - 1 ), pocket_type::CONTAINER ).success() );
        }
    }
    return bag;
}

} // namespace

// EVT-P01: on game load, last_overmap_special_avatar_entered (behind 16 achievements) is
// rebuilt from every OMT the avatar has entered. Here the avatar has entered 400 OMTs spread
// over four overmaps that are saved but not loaded, as after a game load.
TEST_CASE( "bench_stats_overmap_special_history", "[.][benchmark_scenario]" )
{
    const std::vector<point_abs_om> overmaps = {
        point_abs_om( 3, 3 ), point_abs_om( 3, 4 ), point_abs_om( 4, 3 ), point_abs_om( 4, 4 )
    };
    // Each entered OMT, with the special there (or none), as the event records it.
    std::vector<cata::event> history;
    cata_variant expected;
    overmap_buffer.clear();
    time_point when = calendar::turn_zero;
    for( const point_abs_om &om_pos : overmaps ) {
        overmap &om = overmap_buffer.get( om_pos );
        const tripoint_abs_omt corner( project_to<coords::omt>( om_pos ), 0 );
        std::vector<tripoint_abs_omt> entered;
        // a 10x10 grid over the overmap, plus up to 20 OMTs that belong to a special
        for( int y = 5; y < OMAPY; y += OMAPY / 10 ) {
            for( int x = 5; x < OMAPX; x += OMAPX / 10 ) {
                entered.push_back( corner + tripoint_rel_omt( x, y, 0 ) );
            }
        }
        int special_omts = 0;
        for( int y = 0; y < OMAPY && special_omts < 20; ++y ) {
            for( int x = 0; x < OMAPX && special_omts < 20; ++x ) {
                if( om.overmap_special_at( tripoint_om_omt( x, y, 0 ) ) ) {
                    entered.push_back( corner + tripoint_rel_omt( x, y, 0 ) );
                    ++special_omts;
                }
            }
        }
        entered.resize( 100 );
        for( const tripoint_abs_omt &p : entered ) {
            const std::optional<overmap_special_id> special = overmap_buffer.overmap_special_at( p );
            if( special ) {
                expected = cata_variant( *special );
            }
            when += 1_minutes;
            history.emplace_back( event_type::avatar_enters_omt, when, cata::event::data_type{
                { "pos", cata_variant( p.raw() ) },
                { "oter_id", cata_variant( overmap_buffer.ter( p ) ) },
                { "overmap_special", cata_variant( special.value_or( overmap_special_id() ) ) }
            } );
        }
        om.save();
    }
    REQUIRE( history.size() == 400 );
    REQUIRE( expected != cata_variant() );

    std::unique_ptr<stats_tracker> stats;
    run_scenario( "stats_overmap_special_history",
                  "last overmap special entered, over 400 OMTs entered in 4 saved, unloaded overmaps",
    [&]() {
        overmap_buffer.clear();
        stats = std::make_unique<stats_tracker>();
        for( const cata::event &e : history ) {
            stats->notify( e );
        }
    }, [&]() {
        CHECK( stats->value_of( event_statistic_last_overmap_special_avatar_entered ) == expected );
    } );
    stats.reset();
    for( const point_abs_om &om_pos : overmaps ) {
        remove_saved_overmap( om_pos );
    }
    restore_test_overmaps();
}

// MAP-P03, saving: a compressed world's map save after exploring 25 OMTs. Each OMT column has
// 21 quads; the ground level is saved, the 20 others are uniform (sky and rock) and are not.
TEST_CASE( "bench_mapbuffer_save_uniform", "[.][benchmark_scenario]" )
{
    prepare_scenario_segments();
    std::unique_ptr<mapbuffer> mb;
    const int first = scenario_segment * SEG_SIZE + 4;
    run_scenario( "mapbuffer_save_uniform",
                  "save 25 explored OMTs: 25 ground quads and 500 uniform quads, compressed world",
    [&]() {
        mb = std::make_unique<mapbuffer>();
        for( int y = first; y < first + 5; ++y ) {
            for( int x = first; x < first + 5; ++x ) {
                for( int z = -OVERMAP_DEPTH; z <= OVERMAP_HEIGHT; ++z ) {
                    add_quad( *mb, tripoint_abs_omt( x, y, z ), z != 0 );
                }
            }
        }
    }, [&]() {
        mb->save( true );
        CHECK( mb->begin() == mb->end() );
    } );
    mb.reset();
    remove_scenario_segments();
}

// MAP-P03, loading: generating an OMT first asks the mapbuffer for all 84 of its submaps
// (map::generate). In an explored, compressed world, most of them were uniform and never
// saved, so each is a lookup miss.
TEST_CASE( "bench_mapbuffer_lookup_missing", "[.][benchmark_scenario]" )
{
    prepare_scenario_segments();
    std::unique_ptr<mapbuffer> mb;
    const int first = scenario_segment * SEG_SIZE + 14;
    run_scenario( "mapbuffer_lookup_missing",
                  "look up the 84 submaps of 25 OMTs not on disk, compressed world",
    [&]() {
        mb = std::make_unique<mapbuffer>();
    }, [&]() {
        int found = 0;
        for( int y = first; y < first + 5; ++y ) {
            for( int x = first; x < first + 5; ++x ) {
                for( int z = -OVERMAP_DEPTH; z <= OVERMAP_HEIGHT; ++z ) {
                    const tripoint_abs_sm base = project_to<coords::sm>( tripoint_abs_omt( x, y, z ) );
                    for( int dx = 0; dx < 2; ++dx ) {
                        for( int dy = 0; dy < 2; ++dy ) {
                            found += mb->submap_exists( base + tripoint_rel_sm( dx, dy, 0 ) ) ? 1 : 0;
                        }
                    }
                }
            }
        }
        CHECK( found == 0 );
    } );
    mb.reset();
    remove_scenario_segments();
}

// INV-P01: reading a container tree from a save. Five levels of bags (31 bags, each with six
// items, some of which hold items of their own), read 20 times.
TEST_CASE( "bench_item_nested_deserialize", "[.][benchmark_scenario]" )
{
    const item tree = bag_tree( 5 );
    const size_t item_count = tree.all_items_ptr().size();
    REQUIRE( item_count > 200 );
    std::ostringstream os;
    JsonOut jsout( os );
    tree.serialize( jsout );
    const std::string saved = os.str();
    run_scenario( "item_nested_deserialize",
                  "read a 5-level tree of 31 bags holding 186 items (and their contents), 20 times",
    []() {}, [&]() {
        size_t read = 0;
        for( int i = 0; i < 20; ++i ) {
            item loaded;
            JsonValue jv = json_loader::from_string( saved );
            loaded.deserialize( jv.get_object() );
            read += loaded.all_items_ptr().size();
        }
        CHECK( read == 20 * item_count );
    } );
}
