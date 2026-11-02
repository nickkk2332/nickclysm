#include <string>

#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "coordinates.h"
#include "magic_enchantment.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "mattack_actors.h"
#include "mattack_common.h"
#include "monster.h"
#include "mtype.h"
#include "options_helpers.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"
#include "units.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vpart_position.h"

// World-side grappling ported from Cataclysm: The Last Generation (TLG): monster grabs pulling
// riders out of vehicles, and resisting crowd crush.

static const efftype_id effect_downed( "downed" );
static const efftype_id effect_grabbed( "grabbed" );
static const efftype_id effect_overburdened( "overburdened" );
static const efftype_id effect_test_crowd_crush_immune( "test_crowd_crush_immune" );
static const efftype_id effect_test_no_breathing( "test_no_breathing" );

static const json_character_flag json_flag_SUFFOCATION_IMMUNE( "SUFFOCATION_IMMUNE" );

static const limb_score_id limb_score_breathing( "breathing" );

static const move_mode_id move_mode_prone( "prone" );

static const trait_id trait_SLIMESPAWNER( "SLIMESPAWNER" );
static const trait_id trait_TRANSPIRATION( "TRANSPIRATION" );

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

static double crowd_crush_resist_value( const Character &you )
{
    return you.enchantment_cache->modify_value( enchant_vals::mod::CROWD_CRUSH_RESIST, 0.0 );
}

// The resist chance at the minimum crowd: TLG's base 5%, with its posture and trait modifiers
// now carried by effects, mutations and external options.
TEST_CASE( "crowd_crush_resist_chance", "[grapple][suffer][oxygen]" )
{
    clear_avatar();
    Character &you = get_player_character();
    you.recalculate_enchantment_cache();
    REQUIRE( you.get_limb_score( limb_score_breathing ) == Approx( 1.0f ) );
    REQUIRE( crowd_crush_resist_value( you ) == Approx( 0.0 ) );

    SECTION( "standing, unburdened" ) {
        CHECK( you.crowd_crush_resist_chance() == Approx( 0.05f ) );
    }
    SECTION( "downed" ) {
        you.add_effect( effect_downed, 1_hours );
        you.recalculate_enchantment_cache();
        CHECK( crowd_crush_resist_value( you ) == Approx( -4.0 ) );
        CHECK( you.crowd_crush_resist_chance() == Approx( 0.01f ) );
    }
    SECTION( "prone" ) {
        you.set_movement_mode( move_mode_prone );
        REQUIRE( you.is_prone() );
        CHECK( you.crowd_crush_resist_chance() == Approx( 0.01f ) );
    }
    SECTION( "prone and downed count once" ) {
        you.set_movement_mode( move_mode_prone );
        you.add_effect( effect_downed, 1_hours );
        you.recalculate_enchantment_cache();
        CHECK( you.crowd_crush_resist_chance() == Approx( 0.01f ) );
    }
    SECTION( "overburdened" ) {
        you.add_effect( effect_overburdened, 1_hours );
        you.recalculate_enchantment_cache();
        CHECK( you.crowd_crush_resist_chance() == Approx( 0.01f ) );
    }
    SECTION( "transpiration" ) {
        you.set_mutation( trait_TRANSPIRATION );
        you.recalculate_enchantment_cache();
        CHECK( you.crowd_crush_resist_chance() == Approx( 0.09f ) );
    }
    SECTION( "omnicellular, more so lying down" ) {
        you.set_mutation( trait_SLIMESPAWNER );
        you.recalculate_enchantment_cache();
        CHECK( crowd_crush_resist_value( you ) == Approx( 4.0 ) );
        you.add_effect( effect_downed, 1_hours );
        you.recalculate_enchantment_cache();
        // +4, +4 more while down, -4 for being down: TLG's exemption from the posture penalty.
        CHECK( crowd_crush_resist_value( you ) == Approx( 4.0 ) );
        you.remove_effect( effect_downed );
        you.set_movement_mode( move_mode_prone );
        REQUIRE( you.is_prone() );
        you.recalculate_enchantment_cache();
        CHECK( crowd_crush_resist_value( you ) == Approx( 8.0 ) );
    }
    SECTION( "full resistance is immunity" ) {
        you.add_effect( effect_test_crowd_crush_immune, 1_hours );
        you.add_effect( effect_downed, 1_hours );
        you.recalculate_enchantment_cache();
        CHECK( you.crowd_crush_resist_chance() == 1.0f );
    }
    clear_avatar();
}

// Six crushers around a standing medium character take 1 oxygen per turn by default.
static int oxygen_after_crowding( Character &you )
{
    const tripoint_bub_ms center( 60, 60, 0 );
    you.setpos( get_map(), center );
    you.oxygen = you.get_oxygen_max();
    you.add_effect( effect_grabbed, 1_hours, body_part_torso, false, 2, true );
    for( const tripoint &offset : {
             tripoint::east, tripoint::west, tripoint::north, tripoint::south,
             tripoint::north_west, tripoint::south_west
         } ) {
        spawn_test_monster( "mon_debug_memory", center + offset );
    }
    for( int turn = 0; turn < 10; ++turn ) {
        you.suffer();
    }
    return you.oxygen;
}

TEST_CASE( "crowd_crush_can_be_turned_off_or_resisted", "[grapple][suffer][oxygen]" )
{
    clear_map_without_vision();
    clear_avatar();
    Character &you = get_player_character();
    const int full = you.get_oxygen_max();

    SECTION( "on by default" ) {
        CHECK( oxygen_after_crowding( you ) == full - 10 );
    }
    SECTION( "the world option off means no oxygen loss" ) {
        override_option crowd_crush_off( "CROWD_CRUSH", "false" );
        CHECK( oxygen_after_crowding( you ) == full );
    }
    SECTION( "full CROWD_CRUSH_RESIST means no oxygen loss" ) {
        you.add_effect( effect_test_crowd_crush_immune, 1_hours );
        you.recalculate_enchantment_cache();
        CHECK( oxygen_after_crowding( you ) == full );
    }
    SECTION( "not needing to breathe means no oxygen loss" ) {
        you.add_effect( effect_test_no_breathing, 1_hours );
        REQUIRE( you.has_flag( json_flag_SUFFOCATION_IMMUNE ) );
        CHECK( oxygen_after_crowding( you ) == full );
    }
    clear_creatures();
}
