#include <optional>
#include <vector>

#include "activity_actor_definitions.h"
#include "activity_handlers.h"
#include "activity_item_handling.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "coordinates.h"
#include "enums.h"
#include "explosion.h"
#include "item.h"
#include "map.h"
#include "map_helpers.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "point.h"
#include "requirements.h"
#include "type_id.h"
#include "units.h"
#include "veh_type.h"
#include "vehicle.h"

static const damage_type_id damage_heat( "heat" );

static const itype_id itype_gasoline( "gasoline" );
static const itype_id itype_test_vp_unmount_on_move( "test_vp_unmount_on_move" );

static const vpart_id vpart_frame( "frame" );
static const vpart_id vpart_tank( "tank" );
static const vpart_id vpart_test_vp_unmount_on_move( "test_vp_unmount_on_move" );

static const vproto_id vehicle_prototype_none( "none" );

static constexpr tripoint_bub_ms veh_pos( 60, 60, 0 );

// A one-tile vehicle with only a frame, placed at veh_pos.
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

// VEH-B02: explode_fuel() looked up vehicle_part::fuel_current(), which is the vpart's
// fuel_type and is null for every tank, so a tank full of gasoline could never explode.
// It must key on the tank's contents instead.
TEST_CASE( "veh-b02_heated_gasoline_tank_can_explode", "[audit][vehicle]" )
{
    clear_avatar();
    clear_map();
    explosion_handler::_explosions.clear();
    map &here = get_map();
    vehicle &veh = place_frame_vehicle( here );
    const int tank_idx = veh.install_part( here, point_rel_ms::zero, vpart_tank );
    REQUIRE( tank_idx != -1 );
    veh.refresh();
    here.add_vehicle_to_cache( &veh );
    vehicle_part &tank = veh.part( tank_idx );
    REQUIRE( tank.ammo_set( itype_gasoline, 100 ) == 100 );
    REQUIRE( tank.ammo_current() == itype_gasoline );
    REQUIRE( tank.fuel_current().is_null() );

    // A heat hit too weak to damage the tank still rolls for an explosion. Gasoline's
    // chance_hot is 2, so 100 hits miss every roll with probability 2^-100.
    for( int i = 0; i < 100 && explosion_handler::_explosions.empty(); ++i ) {
        veh.damage_direct( here, tank, 1, damage_heat );
    }

    CHECK( explosion_handler::_explosions.size() == 1 );
    CHECK( tank.ammo_remaining() == 0 );
    CHECK( tank.is_broken() );
    explosion_handler::_explosions.clear();
}

// A frame on veh_pos damaged by several levels (no degradation), with the avatar standing next
// to it. Returns the frame's part index.
static int damaged_frame_with_avatar( map &here, vehicle *&veh_out )
{
    clear_avatar();
    clear_map();
    avatar &u = get_avatar();
    u.setpos( here, veh_pos + tripoint_rel_ms::west );
    vehicle &veh = place_frame_vehicle( here );
    veh.refresh();
    here.add_vehicle_to_cache( &veh );
    const int frame_idx = veh.part_with_feature( point_rel_ms::zero, "MOUNTABLE", false );
    REQUIRE( frame_idx >= 0 );
    vehicle_part &frame = veh.part( frame_idx );
    veh.set_hp( frame, frame.info().durability / 4, false, 0 );
    REQUIRE( frame.is_repairable() );
    REQUIRE( frame.get_base().repairable_levels() >= 2 );
    veh_out = &veh;
    return frame_idx;
}

// ACT-B02 (1): zone repair scaled repair_time by an integer division of damage over max damage,
// which is always 0 for a repairable (not broken) part, so repair_part() restored the whole
// part instantly. It must take repair_time per repairable level, as the vehicle UI charges.
TEST_CASE( "act-b02_zone_vehicle_repair_takes_time_per_damage_level", "[audit][activity]" )
{
    map &here = get_map();
    vehicle *veh = nullptr;
    const int frame_idx = damaged_frame_with_avatar( here, veh );
    avatar &u = get_avatar();
    const vehicle_part &frame = veh->part( frame_idx );
    const time_duration expected = frame.info().repair_time( u ) *
                                   frame.get_base().repairable_levels();
    REQUIRE( expected > 0_seconds );

    u.activity_vehicle_part_index = frame_idx;
    multi_vehicle_repair_activity_actor zone_repair;
    const bool continue_scan = zone_repair.multi_activity_do( u,
                               activity_reason_info::ok( do_activity_reason::NEEDS_VEH_REPAIR ),
                               here.get_abs( veh_pos ), veh_pos );

    CHECK_FALSE( continue_scan );
    REQUIRE( u.activity );
    CHECK( u.activity.moves_left == to_moves<int>( expected ) );
    u.activity.set_to_null();
    u.activity_vehicle_part_index = -1;
}

// ACT-B02 (2): the zone repair fetched and checked a single level of repair requirements, but
// repair_part() needs them once per repairable level, so a zone worker holding one level's
// worth kept failing and retrying. The fetched requirements must cover every level.
TEST_CASE( "act-b02_zone_vehicle_repair_fetches_requirements_for_every_level",
           "[audit][activity]" )
{
    map &here = get_map();
    vehicle *veh = nullptr;
    const int frame_idx = damaged_frame_with_avatar( here, veh );
    avatar &u = get_avatar();
    const vehicle_part &frame = veh->part( frame_idx );
    const int levels = frame.get_base().repairable_levels();
    const requirement_data &per_level = frame.info().repair_requirements();
    const requirement_id one_level =
        multi_activity_actor::hash_and_cache_requirement_data( per_level );
    const requirement_id all_levels =
        multi_activity_actor::hash_and_cache_requirement_data( per_level * levels );
    REQUIRE( one_level != all_levels );

    u.activity_vehicle_part_index = frame_idx;
    activity_reason_info act_info =
        activity_reason_info::fail( do_activity_reason::NEEDS_VEH_REPAIR );
    const std::optional<requirement_id> reqs =
        multi_activity_actor::vehicle_work_requirements( u, act_info, veh_pos );

    REQUIRE( reqs.has_value() );
    CHECK( *reqs == all_levels );
    // Don't leak the part index into later tests: NPC zone workers skip the player's index.
    u.activity_vehicle_part_index = -1;
}

// ACT-B02 (3): with no vehicle at the spot, vehicle_work_requirements() reset the worker's part
// index to 1 (a real part index) instead of -1.
TEST_CASE( "act-b02_vehicle_work_requirements_without_vehicle_clears_part_index",
           "[audit][activity]" )
{
    clear_avatar();
    clear_map();
    avatar &u = get_avatar();
    u.activity_vehicle_part_index = 5;
    activity_reason_info act_info =
        activity_reason_info::fail( do_activity_reason::NEEDS_VEH_REPAIR );

    const std::optional<requirement_id> reqs =
        multi_activity_actor::vehicle_work_requirements( u, act_info, veh_pos );

    CHECK_FALSE( reqs.has_value() );
    CHECK( u.activity_vehicle_part_index == -1 );
}
