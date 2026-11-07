#include <functional>
#include <string>
#include <vector>

#include "avatar.h"
#include "avatar_action.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "coordinates.h"
#include "debug.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "game.h"
#include "global_vars.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser_diag_value.h"
#include "omdata.h"
#include "overmapbuffer.h"
#include "player_helpers.h"
#include "point.h"
#include "talker.h"
#include "teleport.h"
#include "type_id.h"
#include "units.h"
#include "veh_type.h"
#include "veh_utils.h"
#include "vehicle.h"

static const effect_on_condition_id
effect_on_condition_EOC_TEST_AQF_ADD_WET( "EOC_TEST_AQF_ADD_WET" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_AQF_NO_SUCH_EOC( "EOC_TEST_AQF_NO_SUCH_EOC" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_AQF_VPART_ACTIVATED( "EOC_TEST_AQF_VPART_ACTIVATED" );

static const oter_str_id oter_test_aqf_omt_a( "test_aqf_omt_a" );
static const oter_str_id oter_test_aqf_omt_b( "test_aqf_omt_b" );
static const oter_str_id oter_test_aqf_omt_ping( "test_aqf_omt_ping" );
static const oter_str_id oter_test_aqf_omt_pong( "test_aqf_omt_pong" );

static const oter_type_str_id oter_type_test_aqf_omt_a( "test_aqf_omt_a" );

static const ter_str_id ter_t_pavement( "t_pavement" );

static const trap_str_id test_aqf_omt_teleport_trap( "test_aqf_omt_teleport_trap" );

static const vpart_id vpart_controls( "controls" );
static const vpart_id vpart_frame( "frame" );
static const vpart_id vpart_test_aqf_eoc_part( "test_aqf_eoc_part" );

static const vproto_id vehicle_prototype_motorcycle( "motorcycle" );
static const vproto_id vehicle_prototype_none( "none" );

namespace
{
// Gives an OMT another terrain for the length of a test.
class omt_terrain_override
{
    public:
        omt_terrain_override( const tripoint_abs_omt &where, const oter_str_id &ter ) :
            omt( where ), saved( overmap_buffer.ter( where ) ) {
            overmap_buffer.ter_set( omt, ter.id() );
        }
        ~omt_terrain_override() {
            overmap_buffer.ter_set( omt, saved );
        }
        omt_terrain_override( const omt_terrain_override & ) = delete;
        omt_terrain_override &operator=( const omt_terrain_override & ) = delete;
    private:
        tripoint_abs_omt omt;
        oter_id saved;
};

// The tile on the avatar's row, offset tiles east of the west edge of the avatar's OMT.
tripoint_bub_ms omt_tile( const map &here, const avatar &u, int offset )
{
    const tripoint_abs_ms pos = u.pos_abs();
    const int west_edge = project_to<coords::ms>( u.pos_abs_omt() ).x();
    return here.get_bub( pos + tripoint_rel_ms( west_edge + offset - pos.x(), 0, 0 ) );
}

// The exit_eoc of the terrain left (a) and the entry_eoc of the terrain entered (b) each ran
// once, with the OMT they belong to and the terrain on each side as context.  Terrain a's
// entry_eoc and terrain b's exit_eoc count wrong hooks, so running either fails the check.
void check_one_crossing( const tripoint_abs_omt &left, const tripoint_abs_omt &entered )
{
    const global_variables &globals = get_globals();
    CHECK( globals.maybe_get_global_value( "aqf_omt_wrong_hooks" ) == nullptr );
    REQUIRE( globals.get_global_value( "aqf_omt_exits" ) == 1 );
    REQUIRE( globals.get_global_value( "aqf_omt_entries" ) == 1 );
    CHECK( globals.get_global_value( "aqf_exit_omt_pos" ).tripoint() ==
           project_to<coords::ms>( left ) );
    CHECK( globals.get_global_value( "aqf_entry_omt_pos" ).tripoint() ==
           project_to<coords::ms>( entered ) );
    CHECK( globals.get_global_value( "aqf_exit_left" ).str() == oter_test_aqf_omt_a.str() );
    CHECK( globals.get_global_value( "aqf_exit_entered" ).str() == oter_test_aqf_omt_b.str() );
    CHECK( globals.get_global_value( "aqf_entry_left" ).str() == oter_test_aqf_omt_a.str() );
    CHECK( globals.get_global_value( "aqf_entry_entered" ).str() == oter_test_aqf_omt_b.str() );
}
} // namespace

// LOOP-J02 / OMAP-J02: crossing an OMT boundary gave the hooks no context, so one EOC couldn't
// serve many terrains. Both now get omt_pos (the OMT the hook belongs to), left_oter_id and
// entered_oter_id, and the exit_eoc that runs is the one of the terrain left.
TEST_CASE( "loop-j02_omt_hooks_get_the_omt_and_both_terrains", "[audit][overmap][eoc]" )
{
    clear_avatar();
    clear_map_without_vision();
    get_globals().clear_global_values();
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms start = omt_tile( here, u, 5 );
    u.setpos( here, start );
    const tripoint_abs_ms old_pos = u.pos_abs();
    const tripoint_abs_omt omt_a = u.pos_abs_omt();
    const tripoint_abs_omt omt_b = omt_a + tripoint_rel_omt( 1, 0, 0 );
    const omt_terrain_override set_a( omt_a, oter_test_aqf_omt_a );
    const omt_terrain_override set_b( omt_b, oter_test_aqf_omt_b );

    u.setpos( here, start + tripoint_rel_ms( 24, 0, 0 ) );
    REQUIRE( u.pos_abs_omt() == omt_b );
    cata_event_dispatch::avatar_moves( old_pos, u, here );
    check_one_crossing( omt_a, omt_b );
}

// LOOP-J02: only walking and stairs ran the OMT hooks; a teleport into another OMT ran neither.
TEST_CASE( "loop-j02_omt_hooks_run_when_the_avatar_teleports", "[audit][overmap][eoc]" )
{
    clear_avatar();
    clear_map_without_vision();
    get_globals().clear_global_values();
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms start = omt_tile( here, u, 5 );
    u.setpos( here, start );
    const tripoint_abs_omt omt_a = u.pos_abs_omt();
    const tripoint_abs_omt omt_b = omt_a + tripoint_rel_omt( 1, 0, 0 );
    const omt_terrain_override set_a( omt_a, oter_test_aqf_omt_a );
    const omt_terrain_override set_b( omt_b, oter_test_aqf_omt_b );

    REQUIRE( teleport::teleport_to_point( u, start + tripoint_rel_ms( 24, 0, 0 ), true, false,
                                          false ) );
    REQUIRE( u.pos_abs_omt() == omt_b );
    check_one_crossing( omt_a, omt_b );
}

// LOOP-J02: now that a teleport runs the OMT hooks, an entry_eoc that teleports the avatar into
// an OMT whose entry_eoc teleports it back would recurse until the stack ran out.  The hooks
// stop after 8 crossings in a row, with a debugmsg.
TEST_CASE( "loop-j02_omt_hooks_that_keep_moving_the_avatar_are_stopped",
           "[audit][overmap][eoc]" )
{
    clear_avatar();
    clear_map_without_vision();
    get_globals().clear_global_values();
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms start = omt_tile( here, u, 5 );
    u.setpos( here, start );
    const tripoint_abs_omt omt_ping = u.pos_abs_omt();
    const tripoint_abs_omt omt_pong = omt_ping + tripoint_rel_omt( 1, 0, 0 );
    const omt_terrain_override set_ping( omt_ping, oter_test_aqf_omt_ping );
    const omt_terrain_override set_pong( omt_pong, oter_test_aqf_omt_pong );

    const std::string msg = capture_debugmsg_during( [&u, &start]() {
        teleport::teleport_to_point( u, start + tripoint_rel_ms( 24, 0, 0 ), true, false, false );
    } );
    CHECK_THAT( msg, Catch::Matchers::Contains( "moved the avatar 8 times in a row" ) );
    CHECK( get_globals().get_global_value( "aqf_omt_ping_pongs" ) == 8 );
}

// LOOP-J02: a teleport made during a step (here by a trap stepped on) is reported by the step,
// once, from where the step started, not once by the teleport and again by the step.
TEST_CASE( "loop-j02_a_teleport_trap_stepped_on_runs_the_omt_hooks_once", "[audit][overmap][eoc]" )
{
    clear_avatar();
    clear_map_without_vision();
    get_globals().clear_global_values();
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms start = omt_tile( here, u, 5 );
    u.setpos( here, start );
    const tripoint_abs_omt omt_a = u.pos_abs_omt();
    const tripoint_abs_omt omt_b = omt_a + tripoint_rel_omt( 1, 0, 0 );
    const omt_terrain_override set_a( omt_a, oter_test_aqf_omt_a );
    const omt_terrain_override set_b( omt_b, oter_test_aqf_omt_b );
    // The trap is in the same OMT; it sends the avatar 24 tiles east, into the next one.
    here.trap_set( start + tripoint_rel_ms::east, test_aqf_omt_teleport_trap.id() );

    REQUIRE( avatar_action::move( u, here, tripoint_rel_ms::east ) );
    REQUIRE( u.pos_abs_omt() == omt_b );
    check_one_crossing( omt_a, omt_b );
}

// LOOP-J02: vehicle travel into another OMT ran neither hook.
TEST_CASE( "loop-j02_omt_hooks_run_when_a_vehicle_carries_the_avatar",
           "[audit][overmap][eoc][vehicle]" )
{
    clear_avatar();
    clear_map_without_vision();
    clear_vehicles();
    build_test_map( ter_t_pavement.id() );
    get_globals().clear_global_values();
    avatar &u = get_avatar();
    map &here = get_map();
    const on_out_of_scope get_off( [&u, &here]() {
        if( u.in_vehicle ) {
            here.unboard_vehicle( u.pos_bub( here ) );
        }
        clear_vehicles();
    } );
    // Three tiles east of the OMT the motorcycle drives west into.
    const tripoint_bub_ms start = omt_tile( here, u, 2 );
    const tripoint_abs_omt omt_a = project_to<coords::omt>( here.get_abs( start ) );
    const tripoint_abs_omt omt_b = omt_a + tripoint_rel_omt( -1, 0, 0 );
    const omt_terrain_override set_a( omt_a, oter_test_aqf_omt_a );
    const omt_terrain_override set_b( omt_b, oter_test_aqf_omt_b );

    vehicle *veh = here.add_vehicle( vehicle_prototype_motorcycle, start, 180_degrees, 1,
                                     veh_spawn_status::UNDAMAGED );
    REQUIRE( veh != nullptr );
    veh->check_falling_or_floating();
    veh->tags.insert( "IN_CONTROL_OVERRIDE" );
    veh->engine_on = true;
    u.setpos( here, start );
    here.board_vehicle( start, &u );
    REQUIRE( u.in_vehicle );
    REQUIRE( u.pos_abs_omt() == omt_a );

    veh->cruise_velocity = 400;
    veh->velocity = 400;
    for( int turn = 0; turn < 12 && u.pos_abs_omt() == omt_a; ++turn ) {
        here.vehmove();
    }
    REQUIRE( u.in_vehicle );
    REQUIRE( u.pos_abs_omt() == omt_b );
    check_one_crossing( omt_a, omt_b );
}

// OMAP-J02: oter_type_t::check() didn't look at entry_eoc or exit_eoc, so a typo only showed up
// as an "invalid id" debugmsg when the avatar crossed into or out of the terrain.
TEST_CASE( "omap-j02_unknown_entry_and_exit_eocs_are_reported_at_load", "[audit][overmap][eoc]" )
{
    const oter_type_t &loaded = oter_type_test_aqf_omt_a.obj();
    oter_type_t ter = loaded;
    CHECK( capture_debugmsg_during( [&ter]() {
        ter.check();
    } ).empty() );

    ter.entry_EOC = effect_on_condition_EOC_TEST_AQF_NO_SUCH_EOC;
    CHECK_THAT( capture_debugmsg_during( [&ter]() {
        ter.check();
    } ), Catch::Matchers::Contains( "Invalid entry_eoc id 'EOC_TEST_AQF_NO_SUCH_EOC'" ) );

    ter = loaded;
    ter.exit_EOC = effect_on_condition_EOC_TEST_AQF_NO_SUCH_EOC;
    CHECK_THAT( capture_debugmsg_during( [&ter]() {
        ter.check();
    } ), Catch::Matchers::Contains( "Invalid exit_eoc id 'EOC_TEST_AQF_NO_SUCH_EOC'" ) );
}

// LOOP-J03: wet_character returned early unless the turn was a multiple of 6, so u_add_wet did
// nothing 5 turns out of 6. Only the rain's per-turn path keeps that rate now.
TEST_CASE( "loop-j03_u_add_wet_works_on_any_turn", "[audit][weather][eoc]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    const time_point saved_turn = calendar::turn;
    const on_out_of_scope restore_turn( [saved_turn]() {
        calendar::turn = saved_turn;
    } );
    calendar::turn = calendar::turn_zero + 1_turns;
    REQUIRE_FALSE( calendar::once_every( 6_seconds ) );
    REQUIRE( u.get_part_wetness( body_part_torso ) == 0 );

    dialogue d( get_talker_for( u ), nullptr );
    effect_on_condition_EOC_TEST_AQF_ADD_WET->activate( d );
    CHECK( u.get_part_wetness( body_part_torso ) > 0 );
}

// VINT-J03: a part with the EOC_ACTIVATION flag and no activatable_eoc loaded, then crashed the
// game on an empty optional when activated; the id itself was never checked either.
TEST_CASE( "vint-j03_eoc_activation_flag_and_field_are_checked_at_load", "[audit][vehicle][eoc]" )
{
    CHECK( capture_debugmsg_during( []() {
        vpart_test_aqf_eoc_part->check();
    } ).empty() );

    SECTION( "the flag without an activatable_eoc" ) {
        vpart_info part = vpart_test_aqf_eoc_part.obj();
        part.activatable_eoc.reset();
        CHECK_THAT( capture_debugmsg_during( [&part]() {
            part.check();
        } ), Catch::Matchers::Contains( "EOC_ACTIVATION flag and an activatable_eoc" ) );
    }
    SECTION( "an activatable_eoc without the flag" ) {
        vpart_info part = vpart_frame.obj();
        part.activatable_eoc = effect_on_condition_EOC_TEST_AQF_VPART_ACTIVATED;
        CHECK_THAT( capture_debugmsg_during( [&part]() {
            part.check();
        } ), Catch::Matchers::Contains( "EOC_ACTIVATION flag and an activatable_eoc" ) );
    }
    SECTION( "an unknown activatable_eoc" ) {
        vpart_info part = vpart_test_aqf_eoc_part.obj();
        part.activatable_eoc = effect_on_condition_EOC_TEST_AQF_NO_SUCH_EOC;
        CHECK_THAT( capture_debugmsg_during( [&part]() {
            part.check();
        } ), Catch::Matchers::Contains( "unknown activatable_eoc EOC_TEST_AQF_NO_SUCH_EOC" ) );
    }
}

// VINT-J03: every tile of the vehicle listed every EOC_ACTIVATION part, and the EOC ran with no
// beta talker and nothing about the part. The part's own tile now lists only its parts (the
// controls list them all), and the EOC gets the vehicle as beta talker and vpart_id and vpart_pos.
TEST_CASE( "vint-j03_activating_a_vehicle_part_runs_its_eoc_with_the_vehicle",
           "[audit][vehicle][eoc]" )
{
    clear_avatar();
    clear_map_without_vision();
    clear_vehicles();
    get_globals().clear_global_values();
    map &here = get_map();
    avatar &u = get_avatar();
    const on_out_of_scope cleanup( []() {
        clear_vehicles();
    } );
    const tripoint_bub_ms origin = u.pos_bub() + tripoint_rel_ms( 0, 3, 0 );
    vehicle *veh = here.add_vehicle( vehicle_prototype_none, origin, 0_degrees, 0,
                                     veh_spawn_status::UNDAMAGED );
    REQUIRE( veh != nullptr );
    REQUIRE( veh->install_part( here, point_rel_ms::zero, vpart_frame ) >= 0 );
    const int controls = veh->install_part( here, point_rel_ms::zero, vpart_controls );
    REQUIRE( controls >= 0 );
    REQUIRE( veh->install_part( here, point_rel_ms( 1, 0 ), vpart_frame ) >= 0 );
    const int first = veh->install_part( here, point_rel_ms( 1, 0 ), vpart_test_aqf_eoc_part );
    REQUIRE( first >= 0 );
    REQUIRE( veh->install_part( here, point_rel_ms( 2, 0 ), vpart_frame ) >= 0 );
    REQUIRE( veh->install_part( here, point_rel_ms( 2, 0 ), vpart_test_aqf_eoc_part ) >= 0 );
    here.add_vehicle_to_cache( veh );
    const tripoint_bub_ms first_pos = veh->bub_part_pos( here, first );
    const tripoint_bub_ms controls_pos = veh->bub_part_pos( here, controls );

    // The "Activate" entries the menu at p offers.
    const auto activations = [&here, veh]( const tripoint_bub_ms & p ) {
        veh_menu menu( *veh, "TEST" );
        veh->build_interact_menu( menu, &here, p, false );
        std::vector<veh_menu_item> found;
        for( const veh_menu_item &it : menu.get_items() ) {
            if( it._text.rfind( "Activate ", 0 ) == 0 ) {
                found.push_back( it );
            }
        }
        return found;
    };

    SECTION( "the part's own tile lists only that part" ) {
        const std::vector<veh_menu_item> items = activations( first_pos );
        REQUIRE( items.size() == 1 );
        CHECK( items[0]._text.find( "test activation switch" ) != std::string::npos );
        items[0]._on_submit();
        const global_variables &globals = get_globals();
        CHECK( globals.get_global_value( "aqf_vpart_activations" ) == 1 );
        CHECK( globals.get_global_value( "aqf_vpart_id" ).str() == vpart_test_aqf_eoc_part.str() );
        CHECK( globals.get_global_value( "aqf_vpart_pos" ).tripoint() ==
               here.get_abs( first_pos ) );
        const diag_value *on_vehicle = veh->maybe_get_value( "aqf_vpart_activated" );
        REQUIRE( on_vehicle != nullptr );
        CHECK( on_vehicle->dbl() == 1 );
    }
    SECTION( "the controls list every part" ) {
        CHECK( activations( controls_pos ).size() == 2 );
    }
}
