#include <array>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "avatar.h"
#include "cata_catch.h"
#include "coordinates.h"
#include "line.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "mapgen.h"
#include "mapgen_primitives.h"
#include "mapgendata.h"
#include "maptile_fwd.h"
#include "messages.h"
#include "pathfinding.h"
#include "player_helpers.h"
#include "point.h"
#include "submap.h"
#include "type_id.h"
#include "vehicle.h"
#include "weather.h"

static const furn_str_id furn_f_chair( "f_chair" );

static const ter_str_id ter_t_floor( "t_floor" );
static const ter_str_id ter_t_stairs_down( "t_stairs_down" );
static const ter_str_id ter_t_stairs_up( "t_stairs_up" );

// Which of the 8 horizontal neighbours of pos get_wind_blockers() blocks
static std::set<point_rel_ms> wind_blocked_offsets( int winddirection, const tripoint_bub_ms &pos )
{
    map &here = get_map();
    const std::tuple<maptile, maptile, maptile> blockers =
        here.get_wind_blockers( winddirection, pos );
    const std::array<point_sm_ms, 3> blocked = { {
            std::get<0>( blockers ).pos(),
            std::get<1>( blockers ).pos(),
            std::get<2>( blockers ).pos()
        }
    };
    std::set<point_rel_ms> result;
    for( int dx = -1; dx <= 1; dx++ ) {
        for( int dy = -1; dy <= 1; dy++ ) {
            const point_rel_ms off( dx, dy );
            if( off == point_rel_ms::zero ) {
                continue;
            }
            const point_sm_ms neigh = here.maptile_at( pos + off ).pos();
            for( const point_sm_ms &b : blocked ) {
                if( b == neigh ) {
                    result.insert( off );
                }
            }
        }
    }
    return result;
}

// Fire and gas must not spread into the wind. winddirection is the compass bearing the
// wind comes from, the same convention convert_wind_to_coord() and get_dirstring() use.
TEST_CASE( "map-b01_wind_blockers_face_the_upwind_side", "[audit][map][weather]" )
{
    clear_map();
    const tripoint_bub_ms pos( 65, 65, 0 );
    // One bearing from the middle of each of the 8 sectors of convert_wind_to_coord()
    const std::array<int, 8> bearings = { { 0, 45, 90, 135, 180, 225, 270, 315 } };
    for( const int dir : bearings ) {
        CAPTURE( dir );
        const rl_vec2d upwind_vec = convert_wind_to_coord( dir );
        const point_rel_ms upwind( static_cast<int>( upwind_vec.x ),
                                   static_cast<int>( upwind_vec.y ) );
        REQUIRE( upwind != point_rel_ms::zero );
        const std::set<point_rel_ms> blocked = wind_blocked_offsets( dir, pos );
        CHECK( blocked.size() == 3 );
        // The tile the wind comes from is blocked, the tile it blows towards is not
        CHECK( blocked.count( upwind ) == 1 );
        CHECK( blocked.count( -upwind ) == 0 );
        // Every blocked tile is on the upwind side
        for( const point_rel_ms &b : blocked ) {
            CAPTURE( b );
            CHECK( b.x() * upwind.x() + b.y() * upwind.y() > 0 );
        }
    }
    // North wind blocks the three northern tiles
    CHECK( wind_blocked_offsets( 0, pos ) == std::set<point_rel_ms> {
        point_rel_ms::north_west, point_rel_ms::north, point_rel_ms::north_east
    } );
}

// Merging a mapgen overlay submap must move its vehicles into the target submap. Before the
// fix the target got a second unique_ptr to each vehicle and the overlay then deleted them,
// leaving dangling pointers that were freed again when the target was destroyed.
TEST_CASE( "map-b02_merge_submaps_moves_overlay_vehicles", "[audit][map][mapgen]" )
{
    std::unique_ptr<submap> target = std::make_unique<submap>();
    target->ensure_nonuniform();
    std::unique_ptr<submap> overlay = std::make_unique<submap>();
    overlay->ensure_nonuniform();

    std::unique_ptr<vehicle> veh = std::make_unique<vehicle>( vproto_id() );
    veh->name = "merged overlay vehicle";
    const vehicle *const veh_ptr = veh.get();
    overlay->vehicles.push_back( std::move( veh ) );

    target->merge_submaps( overlay.get(), true );

    CHECK( overlay->vehicles.empty() );
    REQUIRE( target->vehicles.size() == 1 );
    REQUIRE( target->vehicles.front().get() == veh_ptr );
    CHECK( target->vehicles.front()->name == "merged overlay vehicle" );

    // Destroying both submaps must free the vehicle exactly once
    overlay.reset();
    target.reset();
}

// Pathfinding must not run the avatar's interactive stair climb (prompts, rope use, messages)
// when it expands the avatar's own tile, and must pick stairs relative to the creature that
// is pathing rather than the avatar.
TEST_CASE( "spat-b01_pathfinding_stair_lookup_has_no_side_effects", "[audit][pathfinding]" )
{
    clear_map_without_vision();
    clear_avatar();
    map &here = get_map();
    avatar &u = get_avatar();

    SECTION( "route from the avatar's staircase with a monster on the stairs below" ) {
        const tripoint_bub_ms stairs_top( 65, 65, 0 );
        const tripoint_bub_ms stairs_bottom( 65, 65, -1 );
        const tripoint_bub_ms corridor( 66, 65, -1 );
        const tripoint_bub_ms target( 67, 65, -1 );
        here.ter_set( stairs_top, ter_t_stairs_down );
        here.ter_set( stairs_bottom, ter_t_stairs_up );
        here.ter_set( corridor, ter_t_floor );
        here.ter_set( target, ter_t_floor );
        here.set_pathfinding_cache_dirty( 0 );
        here.set_pathfinding_cache_dirty( -1 );
        u.setpos( here, stairs_top );
        REQUIRE( u.pos_bub( here ) == stairs_top );
        spawn_test_monster( "mon_zombie", stairs_bottom );
        Messages::clear_messages();

        const std::vector<tripoint_bub_ms> path =
            here.route( u, pathfinding_target::point( target ) );

        // Before the fix the pathfinder asked "There's a zombie in the way!  Attempt to push
        // past?", which test mode answers with no, logged the message and found no route.
        CHECK( Messages::size() == 0 );
        const std::vector<tripoint_bub_ms> expected_path = { stairs_bottom, corridor, target };
        CHECK( path == expected_path );
    }

    SECTION( "a creature's way down uses the stairs nearest to it, not to the avatar" ) {
        // find_stairs only searches the overmap tile the stairs are on
        const tripoint_bub_ms probe( 60, 60, 0 );
        const tripoint_abs_omt omt = project_to<coords::omt>( here.get_abs( probe ) );
        const tripoint_bub_ms omt_origin = here.get_bub( project_to<coords::ms>( omt ) );
        const tripoint_bub_ms stairs_top = omt_origin + tripoint_rel_ms( 4, 12, 0 );
        const tripoint_bub_ms near_bottom = omt_origin + tripoint_rel_ms( 5, 12, -1 );
        const tripoint_bub_ms far_bottom = omt_origin + tripoint_rel_ms( 20, 12, -1 );
        here.ter_set( stairs_top, ter_t_stairs_down );
        here.ter_set( near_bottom, ter_t_stairs_up );
        here.ter_set( far_bottom, ter_t_stairs_up );
        here.set_pathfinding_cache_dirty( 0 );
        here.set_pathfinding_cache_dirty( -1 );
        // The avatar stands right above the far staircase
        u.setpos( here, omt_origin + tripoint_rel_ms( 21, 12, 0 ) );
        REQUIRE( u.pos_bub( here ) != stairs_top );

        const pathfinding_settings &settings = u.get_pathfinding_settings();
        CHECK( here.can_pass( stairs_top, near_bottom, settings ) );
        CHECK_FALSE( here.can_pass( stairs_top, far_bottom, settings ) );
    }
}

// A 3D line must reach its end point and move one tile per step. When the minor horizontal
// axis tied with z, the tie branches looped on the shorter axis and stopped after
// min( |dx|, |dy| ) steps, so map::sees() and projectiles skipped most of the tiles.
TEST_CASE( "spat-b02_3d_bresenham_reaches_target_when_z_ties_minor_axis", "[audit][line]" )
{
    const std::vector<tripoint> targets = {
        { 1, 5, 1 }, { 5, 1, 1 }, { 2, 6, 2 }, { 6, 2, 2 },
        { -1, 5, -1 }, { 5, -1, -1 }, { -2, -6, 2 }, { -6, -2, -2 },
        { 3, 4, 3 }, { 4, 3, 3 }
    };
    const tripoint origin( 0, 0, 0 );
    for( const tripoint &target : targets ) {
        CAPTURE( target );
        const std::vector<tripoint> line = line_to( origin, target );
        REQUIRE( !line.empty() );
        CHECK( line.back() == target );
        CHECK( static_cast<int>( line.size() ) == square_dist( origin, target ) );
        tripoint prev = origin;
        for( const tripoint &p : line ) {
            CHECK( square_dist( prev, p ) == 1 );
            prev = p;
        }
    }
}

// A "set" point with a ranged x/y and "repeat" must roll a new tile for every repeat, as
// MAPGEN.md documents and as the line and square ops already do. It used to roll once and
// place every repeat on that same tile.
TEST_CASE( "mgen-b01_set_point_with_repeat_rolls_each_repeat", "[audit][mapgen]" )
{
    clear_map();
    map &here = get_map();
    mapgendata md( here, mapgendata::dummy_settings );
    const tripoint_bub_ms area_min( 10, 10, 0 );
    const tripoint_bub_ms area_max( 19, 19, 0 );
    // 20 chairs over 100 tiles: all of them landing on one tile is a 1 in 100^19 chance
    const jmapgen_setmap chairs( jmapgen_int( area_min.x(), area_max.x() ),
                                 jmapgen_int( area_min.y(), area_max.y() ), jmapgen_int( 0 ),
                                 jmapgen_int( 0 ), jmapgen_int( 0 ), JMAPGEN_SETMAP_FURN,
                                 jmapgen_int( furn_f_chair.id().to_i() ), 1, jmapgen_int( 20 ) );
    REQUIRE( chairs.apply( md, tripoint_rel_ms::zero ) );

    int placed = 0;
    for( const tripoint_bub_ms &p : here.points_in_rectangle( area_min, area_max ) ) {
        if( here.furn( p ) == furn_f_chair ) {
            placed++;
        }
    }
    CHECK( placed > 1 );
}
