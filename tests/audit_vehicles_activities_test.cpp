#include <vector>

#include "calendar.h"
#include "cata_catch.h"
#include "coordinates.h"
#include "enums.h"
#include "item.h"
#include "map.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"
#include "units.h"
#include "vehicle.h"

static const itype_id itype_test_vp_unmount_on_move( "test_vp_unmount_on_move" );

static const vpart_id vpart_frame( "frame" );
static const vpart_id vpart_test_vp_unmount_on_move( "test_vp_unmount_on_move" );

static const vproto_id vehicle_prototype_none( "none" );

static constexpr tripoint_bub_ms veh_pos( 60, 60, 0 );

// A one-tile vehicle with a frame, placed at veh_pos and registered with the map caches.
static vehicle &place_frame_vehicle( map &here )
{
    vehicle *veh = here.add_vehicle( vehicle_prototype_none, veh_pos, 0_degrees, 0,
                                     veh_spawn_status::UNDAMAGED );
    REQUIRE( veh != nullptr );
    REQUIRE( veh->install_part( here, point_rel_ms::zero, vpart_frame ) != -1 );
    return *veh;
}

// VEH-B01: gain_moves() called shed_loose_parts() with its default (null) map, so the first
// moving turn of a vehicle carrying an UNMOUNT_ON_MOVE part dereferenced a null map and crashed.
// The part must instead fall off onto the ground under the vehicle.
TEST_CASE( "veh-b01_moving_vehicle_drops_unmount_on_move_part", "[audit][vehicle]" )
{
    clear_avatar();
    clear_map();
    map &here = get_map();
    vehicle &veh = place_frame_vehicle( here );
    const int loose_idx = veh.install_part( here, point_rel_ms::zero,
                                            vpart_test_vp_unmount_on_move );
    REQUIRE( loose_idx != -1 );
    veh.refresh();
    here.add_vehicle_to_cache( &veh );
    REQUIRE( veh.loose_parts == std::vector<int> { loose_idx } );
    REQUIRE( here.i_at( veh_pos ).empty() );

    veh.velocity = 1000;
    veh.gain_moves( here );

    CHECK( veh.loose_parts.empty() );
    CHECK( veh.part( loose_idx ).removed );
    int dropped = 0;
    for( const item &it : here.i_at( veh_pos ) ) {
        if( it.typeId() == itype_test_vp_unmount_on_move ) {
            ++dropped;
        }
    }
    CHECK( dropped == 1 );
}
