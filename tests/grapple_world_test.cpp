#include <string>

#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "coordinates.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "mattack_actors.h"
#include "mattack_common.h"
#include "monster.h"
#include "mtype.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"
#include "units.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vpart_position.h"

// World-side grappling ported from Cataclysm: The Last Generation (TLG): monster grabs pulling
// riders out of vehicles.

static const efftype_id effect_downed( "downed" );
static const efftype_id effect_grabbed( "grabbed" );

static const vpart_id vpart_frame( "frame" );
static const vpart_id vpart_seat( "seat" );
static const vpart_id vpart_seatbelt( "seatbelt" );

static const vproto_id vehicle_prototype_none( "none" );

static const tripoint_bub_ms seat_pos( 60, 60, 0 );

// Seats the avatar in a one-tile vehicle at seat_pos, belted in or not.
static vehicle &seat_avatar( bool seatbelt )
{
    map &here = get_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_none, seat_pos, 0_degrees, 0,
                                     veh_spawn_status::UNDAMAGED );
    REQUIRE( veh != nullptr );
    REQUIRE( veh->install_part( here, point_rel_ms::zero, vpart_frame ) >= 0 );
    REQUIRE( veh->install_part( here, point_rel_ms::zero, vpart_seat ) >= 0 );
    if( seatbelt ) {
        REQUIRE( veh->install_part( here, point_rel_ms::zero, vpart_seatbelt ) >= 0 );
    }
    here.add_vehicle_to_cache( veh );
    Character &you = get_player_character();
    you.setpos( here, seat_pos );
    here.board_vehicle( seat_pos, &you );
    REQUIRE( you.in_vehicle );
    REQUIRE( here.veh_at( seat_pos ).avail_part_with_feature( VPFLAG_SEATBELT ).has_value() ==
             seatbelt );
    return *veh;
}

// Runs the monster's grab on the avatar's torso, skipping the to-hit and dodge rolls.
static void grab_avatar( const std::string &grabber_type )
{
    map &here = get_map();
    Character &you = get_player_character();
    monster &grabber = spawn_test_monster( grabber_type, seat_pos + tripoint::east );
    REQUIRE_FALSE( here.has_vehicle_floor( grabber.pos_bub( here ) ) );
    const melee_actor *grab = dynamic_cast<const melee_actor *>
                              ( grabber.type->special_attacks.at( "grab" ).get() );
    REQUIRE( grab != nullptr );
    REQUIRE( grab->grab_data.pull_from_vehicle );
    CHECK( grab->do_grab( grabber, &you, body_part_torso ) == 1 );
    CHECK( you.has_effect( effect_grabbed, body_part_torso ) );
}

TEST_CASE( "grab_pulls_rider_out_of_vehicle", "[grapple][mattack][grab][vehicle]" )
{
    clear_map_without_vision();
    clear_vehicles();
    clear_avatar();
    Character &you = get_player_character();

    SECTION( "a sure pull unboards and downs a rider without a seatbelt" ) {
        vehicle &veh = seat_avatar( false );
        const int moves_before = you.get_moves();
        grab_avatar( "mon_test_vehicle_yanker" );
        CHECK_FALSE( you.in_vehicle );
        CHECK( veh.boarded_parts().empty() );
        CHECK_FALSE( veh.is_passenger( you ) );
        CHECK( you.has_effect( effect_downed ) );
        CHECK( you.get_moves() == moves_before - to_moves<int>( 1_seconds ) );
        CHECK( you.pos_bub() == seat_pos );
    }
    SECTION( "a seatbelt always keeps the rider in" ) {
        seat_avatar( true );
        grab_avatar( "mon_test_vehicle_yanker" );
        CHECK( you.in_vehicle );
        CHECK_FALSE( you.has_effect( effect_downed ) );
    }
    SECTION( "a pull with no chance leaves the rider seated" ) {
        seat_avatar( false );
        grab_avatar( "mon_test_vehicle_yanker_weak" );
        CHECK( you.in_vehicle );
        CHECK_FALSE( you.has_effect( effect_downed ) );
    }
    clear_vehicles();
}
