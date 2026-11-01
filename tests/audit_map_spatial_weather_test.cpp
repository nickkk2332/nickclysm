#include <array>
#include <memory>
#include <set>
#include <string>
#include <tuple>

#include "cata_catch.h"
#include "coordinates.h"
#include "line.h"
#include "map.h"
#include "map_helpers.h"
#include "maptile_fwd.h"
#include "point.h"
#include "submap.h"
#include "type_id.h"
#include "vehicle.h"
#include "weather.h"

// Which of the 8 horizontal neighbours of pos get_wind_blockers() blocks
static std::set<point_rel_ms> wind_blocked_offsets( int winddirection, const tripoint_bub_ms &pos )
{
    map &here = get_map();
    const std::tuple<maptile, maptile, maptile> blockers =
        here.get_wind_blockers( winddirection, pos );
    const std::array<point_sm_ms, 3> blocked = { {
            std::get<0>( blockers ).pos(), std::get<1>( blockers ).pos(), std::get<2>( blockers ).pos()
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
