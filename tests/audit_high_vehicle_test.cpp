#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "condition.h"
#include "coordinates.h"
#include "dialogue.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "item.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "math_parser.h"
#include "math_parser_diag_value.h"
#include "player_helpers.h"
#include "point.h"
#include "talker.h"
#include "type_id.h"
#include "units.h"
#include "veh_type.h"
#include "veh_utils.h"
#include "vehicle.h"
#include "vpart_position.h"

static const itype_id itype_battery( "battery" );
static const itype_id itype_car_headlight( "car_headlight" );
static const itype_id itype_gasoline( "gasoline" );
static const itype_id itype_wheel( "wheel" );

static const ter_str_id ter_t_floor( "t_floor" );
static const ter_str_id ter_t_wall( "t_wall" );

static const vpart_id vpart_frame( "frame" );
static const vpart_id vpart_headlight( "headlight" );
static const vpart_id vpart_seat( "seat" );
static const vpart_id vpart_wheel( "wheel" );

static const vproto_id vehicle_prototype_car( "car" );
static const vproto_id vehicle_prototype_none( "none" );
static const vproto_id vehicle_prototype_test_vint_j01_talking_cart( "test_vint_j01_talking_cart" );

// clear of the avatar, which clear_avatar() puts at 60,60
static constexpr tripoint_bub_ms veh_pos( 66, 60, 0 );

namespace
{

// Every test starts from the same world: a fixed time, no other vehicles, creatures or globals.
// The caller restores the turn.
void reset_world()
{
    calendar::turn = calendar::start_of_cataclysm + 1_days;
    clear_avatar();
    clear_map();
    clear_creatures();
    clear_npcs();
    clear_vehicles();
    get_globals().clear_global_values();
}

double eval_math( dialogue &d, const std::string_view expression )
{
    math_exp exp;
    REQUIRE( exp.parse( expression ) );
    return exp.eval( d );
}

bool check_condition( dialogue &d, const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    conditional_t::func condition;
    read_condition( jo, "condition", condition, false );
    return condition( d );
}

void run_effect( dialogue &d, const std::string &json )
{
    const JsonObject jo = json_loader::from_string( R"({ "effect": )" + json + " }" );
    jo.allow_omitted_members();
    const talk_effect_t effect( jo, "effect", "audit_high_vehicle_test" );
    effect.apply( d );
}

// A context variable an effect set.
double context( dialogue &d, const std::string &name )
{
    const diag_value *value = d.maybe_get_value( name );
    REQUIRE( value != nullptr );
    return value->dbl();
}

vehicle &place_car( map &here )
{
    vehicle *veh = here.add_vehicle( vehicle_prototype_car, veh_pos, 0_degrees, 0,
                                     veh_spawn_status::UNDAMAGED );
    REQUIRE( veh != nullptr );
    return *veh;
}

// A row of frames at mounts 0,0 to (count - 1),0.
vehicle &place_frames( map &here, int count )
{
    vehicle *veh = here.add_vehicle( vehicle_prototype_none, veh_pos, 0_degrees, 0,
                                     veh_spawn_status::UNDAMAGED );
    REQUIRE( veh != nullptr );
    for( int x = 0; x < count; x++ ) {
        REQUIRE( veh->install_part( here, point_rel_ms( x, 0 ), vpart_frame ) != -1 );
    }
    here.add_vehicle_to_cache( veh );
    return *veh;
}

// Indices of the vehicle's parts of a type, not removed.
std::vector<int> parts_of( const vehicle &veh, const vpart_id &id )
{
    std::vector<int> found;
    for( int idx = 0; idx < veh.part_count(); idx++ ) {
        const vehicle_part &vp = veh.part( idx );
        if( !vp.removed && !vp.is_fake && vp.info().id == id ) {
            found.push_back( idx );
        }
    }
    return found;
}

// Items of a type on the ground around the vehicle.
int items_on_ground( map &here, const vehicle &veh, const itype_id &id )
{
    int found = 0;
    for( const tripoint_bub_ms &p : here.points_in_radius( veh.pos_bub( here ), 8 ) ) {
        for( const item &it : here.i_at( p ) ) {
            found += it.typeId() == id ? 1 : 0;
        }
    }
    return found;
}

} // namespace

// VINT-J01: the math functions counted parts and read their health only by flag, as a share of
// the durability; a quest for "four working wheels" could not name the wheel, and a gauge could
// not show hit points. The fuel and battery reads and writes (VEH-J01) work on a stock car.
TEST_CASE( "vint-j01_math_reads_parts_by_id_fuel_and_battery", "[audit][vehicle][eoc]" )
{
    const restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
    reset_world();
    const on_out_of_scope cleanup( []() {
        clear_vehicles();
    } );
    map &here = get_map();
    vehicle &car = place_car( here );
    dialogue d( get_talker_for( car ), nullptr );

    const std::vector<int> wheels = parts_of( car, vpart_wheel );
    REQUIRE( wheels.size() == 4 );
    int working = 0;
    for( int idx = 0; idx < car.part_count(); idx++ ) {
        const vehicle_part &vp = car.part( idx );
        working += !vp.removed && !vp.is_fake && !vp.is_broken() ? 1 : 0;
    }

    SECTION( "counts by part id" ) {
        CHECK( eval_math( d, "u_vehicle_part_count('part': 'wheel')" ) == 4 );
        CHECK( eval_math( d, "u_vehicle_part_count('WHEEL', 'part': 'wheel')" ) == 4 );
        CHECK( eval_math( d, "u_vehicle_part_count('WHEEL', 'part': 'frame')" ) == 0 );
        // the flag alone still counts as before
        CHECK( eval_math( d, "u_vehicle_part_count('WHEEL')" ) == 4 );
        // no flag and no part: every working part
        CHECK( eval_math( d, "u_vehicle_part_count()" ) == working );

        car.set_hp( car.part( wheels[0] ), 0, true );
        REQUIRE( car.part( wheels[0] ).is_broken() );
        CHECK( eval_math( d, "u_vehicle_part_count('part': 'wheel')" ) == 3 );
        CHECK( eval_math( d, "u_vehicle_part_broken_count('part': 'wheel')" ) == 1 );
        CHECK( eval_math( d, "u_vehicle_part_broken_count('WHEEL', 'part': 'frame')" ) == 0 );
    }

    SECTION( "health in hit points or percent" ) {
        vehicle_part &flat = car.part( wheels[0] );
        vehicle_part &worn = car.part( wheels[1] );
        const int durability = worn.info().durability;
        car.set_hp( flat, 0, true );
        car.set_hp( worn, durability / 2, true );
        std::vector<double> hp;
        for( const int idx : wheels ) {
            hp.push_back( car.part( idx ).hp() );
        }
        const double average = std::accumulate( hp.begin(), hp.end(), 0.0 ) / hp.size();
        CHECK( eval_math( d, "u_vehicle_part_hp('part': 'wheel', 'format': 'hp')" ) == 0 );
        CHECK( eval_math( d, "u_vehicle_part_hp('WHEEL', 'format': 'hp', 'aggregate': 'max')" ) ==
               durability );
        CHECK( eval_math( d, "u_vehicle_part_hp('part': 'wheel', 'format': 'hp', "
                          "'aggregate': 'average')" ) == Approx( average ) );
        // percent stays the default
        CHECK( eval_math( d, "u_vehicle_part_hp('WHEEL', 'aggregate': 'max')" ) == Approx( 100 ) );
        CHECK( eval_math( d, "u_vehicle_part_hp('part': 'wheel', 'aggregate': 'average')" ) ==
               Approx( 100.0 * average / durability ) );
    }

    SECTION( "fuel and battery of a stock car" ) {
        eval_math( d, "u_vehicle_fuel('gasoline') = 3000" );
        CHECK( car.fuel_left( here, itype_gasoline ) == 3000 );
        CHECK( eval_math( d, "u_vehicle_fuel('gasoline')" ) == 3000 );
        eval_math( d, "u_vehicle_fuel('gasoline') -= 1000" );
        CHECK( car.fuel_left( here, itype_gasoline ) == 2000 );
        CHECK( eval_math( d, "u_vehicle_battery_max()" ) ==
               car.fuel_capacity( here, itype_battery ) );
        eval_math( d, "u_vehicle_battery() = 50" );
        CHECK( car.battery_left( here, false ) == 50 );
        CHECK( eval_math( d, "u_vehicle_battery()" ) == 50 );
    }
}

// VINT-J01: nothing told an EOC which vehicle a talker is, so "bring me a station wagon" could
// not be checked.
TEST_CASE( "vint-j01_vehicle_type_condition_and_mutator", "[audit][vehicle][eoc]" )
{
    const restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
    reset_world();
    const on_out_of_scope cleanup( []() {
        clear_vehicles();
    } );
    map &here = get_map();
    vehicle &car = place_car( here );
    avatar &u = get_avatar();

    dialogue d( get_talker_for( car ), nullptr );
    CHECK( check_condition( d, R"({ "condition": { "u_vehicle_type": "car" } })" ) );
    CHECK_FALSE( check_condition( d, R"({ "condition": { "u_vehicle_type": "humvee" } })" ) );
    CHECK( check_condition( d, R"({ "condition": { "compare_string": )"
                            R"([ "car", { "mutator": "u_vehicle_type" } ] } })" ) );

    dialogue with_car( get_talker_for( u ), get_talker_for( car ) );
    CHECK( check_condition( with_car, R"({ "condition": { "npc_vehicle_type": "car" } })" ) );
    // the avatar isn't a vehicle
    CHECK_FALSE( check_condition( with_car, R"({ "condition": { "u_vehicle_type": "car" } })" ) );
    CHECK( check_condition( with_car, R"({ "condition": { "compare_string": )"
                            R"([ "", { "mutator": "u_vehicle_type" } ] } })" ) );

    // a vehicle that isn't built from a prototype
    vehicle *built = here.add_vehicle( vehicle_prototype_none, veh_pos + tripoint_rel_ms( 0, 6, 0 ),
                                       0_degrees, 0, veh_spawn_status::UNDAMAGED );
    REQUIRE( built != nullptr );
    REQUIRE( built->install_part( here, point_rel_ms::zero, vpart_frame ) != -1 );
    dialogue d_built( get_talker_for( *built ), nullptr );
    CHECK( check_condition( d_built, R"({ "condition": { "u_vehicle_type": "none" } })" ) );
}

// VINT-J01: no effect could repair a vehicle part, so "repair nanites" or a mechanic's spell
// had nothing to call.
TEST_CASE( "vint-j01_repair_vehicle_parts", "[audit][vehicle][eoc]" )
{
    const restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
    reset_world();
    const on_out_of_scope cleanup( []() {
        clear_vehicles();
    } );
    map &here = get_map();
    vehicle &car = place_car( here );
    dialogue d( get_talker_for( car ), nullptr );
    const std::vector<int> wheels = parts_of( car, vpart_wheel );
    REQUIRE( wheels.size() == 4 );
    const int durability = car.part( wheels[0] ).info().durability;
    car.set_hp( car.part( wheels[0] ), 0, true );
    car.set_hp( car.part( wheels[1] ), durability / 2, true );
    REQUIRE( car.part( wheels[0] ).is_broken() );

    // broken parts are left alone by default: the game replaces them
    run_effect( d, R"({ "u_repair_vehicle_parts": 100, "percent": true, "part": "wheel" })" );
    CHECK( context( d, "vehicle_parts_repaired" ) == 1 );
    CHECK( context( d, "vehicle_parts_fixed" ) == 0 );
    CHECK( car.part( wheels[1] ).hp() == durability );
    CHECK( car.part( wheels[0] ).is_broken() );

    // "fix_broken" mends it in place, by the hit points given
    run_effect( d, R"({ "u_repair_vehicle_parts": 50, "fix_broken": true, "flag": "WHEEL" })" );
    CHECK( context( d, "vehicle_parts_repaired" ) == 1 );
    CHECK( context( d, "vehicle_parts_fixed" ) == 1 );
    const vehicle_part &mended = car.part( wheels[0] );
    CHECK_FALSE( mended.is_broken() );
    CHECK( mended.hp() == Approx( 50 ).margin( 1 ) );
    CHECK( eval_math( d, "u_vehicle_part_count('part': 'wheel')" ) == 4 );

    // "count" repairs that many of the damaged parts that match
    car.set_hp( car.part( wheels[2] ), durability / 2, true );
    car.set_hp( car.part( wheels[3] ), durability / 2, true );
    run_effect( d,
                R"({ "u_repair_vehicle_parts": 100, "percent": true, "flag": "WHEEL", "count": 1 })" );
    CHECK( context( d, "vehicle_parts_repaired" ) == 1 );
    int full = 0;
    for( const int idx : wheels ) {
        full += car.part( idx ).hp() == durability ? 1 : 0;
    }
    // wheel 1 and one of wheels 2 and 3; wheel 0 is still at 50
    CHECK( full == 2 );

    // a part at full health has nothing to repair
    run_effect( d, R"({ "u_repair_vehicle_parts": 100, "part": "frame" })" );
    CHECK( context( d, "vehicle_parts_repaired" ) == 0 );
}

// VINT-J01: no effect could install a part, so a vehicle could not grow a turret or be built
// up by a spell.
TEST_CASE( "vint-j01_install_vehicle_part", "[audit][vehicle][eoc]" )
{
    const restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
    reset_world();
    const on_out_of_scope cleanup( []() {
        clear_vehicles();
    } );
    map &here = get_map();
    vehicle &veh = place_frames( here, 2 );
    dialogue d( get_talker_for( veh ), nullptr );

    SECTION( "at a mount point, with a variant" ) {
        run_effect( d, R"({ "u_install_vehicle_part": "frame", "mount_x": 2, "mount_y": 0, )"
                    R"("variant": "cross" })" );
        CHECK( context( d, "vehicle_part_installed" ) == 1 );
        const std::vector<int> frames = parts_of( veh, vpart_frame );
        REQUIRE( frames.size() == 3 );
        const vehicle_part &added = veh.part( frames.back() );
        CHECK( added.mount == point_rel_ms( 2, 0 ) );
        CHECK( added.variant == "cross" );
        const optional_vpart_position there = here.veh_at( veh.mount_to_tripoint( &here,
                                              point_rel_ms( 2, 0 ) ) );
        REQUIRE( there.has_value() );
        CHECK( &there->vehicle() == &veh );

        // not next to the frame: the vehicle menu wouldn't allow it either
        run_effect( d, R"({ "u_install_vehicle_part": "frame", "mount_x": 5, "mount_y": 5 })" );
        CHECK( context( d, "vehicle_part_installed" ) == 0 );
        CHECK( parts_of( veh, vpart_frame ).size() == 3 );
    }

    SECTION( "at a location" ) {
        d.set_value( "spot", here.get_abs( veh.mount_to_tripoint( &here, point_rel_ms::zero ) ) );
        run_effect( d, R"({ "u_install_vehicle_part": "seat", "loc": { "context_val": "spot" } })" );
        CHECK( context( d, "vehicle_part_installed" ) == 1 );
        CHECK( parts_of( veh, vpart_seat ).size() == 1 );
        // a second seat can't share the tile
        run_effect( d, R"({ "u_install_vehicle_part": "seat", "loc": { "context_val": "spot" } })" );
        CHECK( context( d, "vehicle_part_installed" ) == 0 );
        CHECK( parts_of( veh, vpart_seat ).size() == 1 );

        // a new frame next to the vehicle
        const tripoint_bub_ms beside = veh.mount_to_tripoint( &here, point_rel_ms( -1, 0 ) );
        d.set_value( "spot", here.get_abs( beside ) );
        run_effect( d, R"({ "u_install_vehicle_part": "frame", "loc": { "context_val": "spot" } })" );
        CHECK( context( d, "vehicle_part_installed" ) == 1 );
        CHECK( parts_of( veh, vpart_frame ).size() == 3 );
    }

    SECTION( "not into a wall" ) {
        const tripoint_bub_ms beside = veh.mount_to_tripoint( &here, point_rel_ms( 2, 0 ) );
        REQUIRE( here.ter_set( beside, ter_t_wall ) );
        run_effect( d, R"({ "u_install_vehicle_part": "frame", "mount_x": 2, "mount_y": 0 })" );
        CHECK( context( d, "vehicle_part_installed" ) == 0 );
        CHECK( parts_of( veh, vpart_frame ).size() == 2 );
        here.ter_set( beside, ter_t_floor );
    }
}

// VINT-J01: no effect could take a part off. Removal must not invalidate the vehicle talker
// partway through an EOC: parts are marked removed and only cleaned up at the end of the turn.
TEST_CASE( "vint-j01_remove_vehicle_parts_keeps_the_talker_valid", "[audit][vehicle][eoc]" )
{
    const restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
    reset_world();
    const on_out_of_scope cleanup( []() {
        clear_vehicles();
    } );
    map &here = get_map();

    SECTION( "salvaged parts drop, destroyed ones don't" ) {
        vehicle &car = place_car( here );
        dialogue d( get_talker_for( car ), nullptr );
        const int parts_before = car.part_count();
        const int wheels_on_ground = items_on_ground( here, car, itype_wheel );
        run_effect( d, R"({ "u_remove_vehicle_parts": "salvage", "part": "wheel", "count": 1 })" );
        CHECK( context( d, "vehicle_parts_removed" ) == 1 );
        CHECK( context( d, "vehicle_destroyed" ) == 0 );
        CHECK( items_on_ground( here, car, itype_wheel ) == wheels_on_ground + 1 );
        // the talker still works: math, conditions and more effects on the same vehicle
        CHECK( eval_math( d, "u_vehicle_part_count('part': 'wheel')" ) == 3 );
        CHECK( check_condition( d, R"({ "condition": { "u_vehicle_type": "car" } })" ) );
        const int headlights_on_ground = items_on_ground( here, car, itype_car_headlight );
        run_effect( d, R"({ "u_remove_vehicle_parts": "destroy", "part": "headlight" })" );
        CHECK( context( d, "vehicle_parts_removed" ) == 2 );
        CHECK( items_on_ground( here, car, itype_car_headlight ) == headlights_on_ground );
        CHECK( parts_of( car, vpart_headlight ).empty() );
        // nothing has been taken out of the parts list yet
        CHECK( car.part_count() >= parts_before );

        // the end of the turn cleans up
        here.vehmove();
        REQUIRE( here.get_vehicles().size() == 1 );
        CHECK( here.get_vehicles().front().v == &car );
        CHECK( parts_of( car, vpart_wheel ).size() == 3 );
        CHECK( car.part_count() < parts_before );
    }

    SECTION( "a frame with parts on it stays" ) {
        vehicle &car = place_car( here );
        dialogue d( get_talker_for( car ), nullptr );
        const int frames = static_cast<int>( parts_of( car, vpart_frame ).size() );
        d.set_value( "spot", here.get_abs( car.bub_part_pos( here, parts_of( car,
                                           vpart_wheel ).front() ) ) );
        run_effect( d, R"({ "u_remove_vehicle_parts": "destroy", "part": "frame", )"
                    R"("loc": { "context_val": "spot" } })" );
        CHECK( context( d, "vehicle_parts_removed" ) == 0 );
        CHECK( static_cast<int>( parts_of( car, vpart_frame ).size() ) == frames );
    }

    SECTION( "splitting only with split" ) {
        vehicle &veh = place_frames( here, 3 );
        dialogue d( get_talker_for( veh ), nullptr );
        d.set_value( "spot", here.get_abs( veh.mount_to_tripoint( &here, point_rel_ms( 1, 0 ) ) ) );
        run_effect( d, R"({ "u_remove_vehicle_parts": "destroy", "loc": { "context_val": "spot" } })" );
        CHECK( context( d, "vehicle_parts_removed" ) == 0 );
        CHECK( parts_of( veh, vpart_frame ).size() == 3 );

        run_effect( d, R"({ "u_remove_vehicle_parts": "destroy", "split": true, )"
                    R"("loc": { "context_val": "spot" } })" );
        CHECK( context( d, "vehicle_parts_removed" ) == 1 );
        CHECK( context( d, "vehicle_destroyed" ) == 0 );
        // the far frame is a vehicle of its own now; the talker keeps the near one
        CHECK( here.get_vehicles().size() == 2 );
        CHECK( eval_math( d, "u_vehicle_part_count('part': 'frame')" ) == 1 );
        here.vehmove();
        CHECK( here.get_vehicles().size() == 2 );
        CHECK( parts_of( veh, vpart_frame ).size() == 1 );
    }

    SECTION( "the whole vehicle" ) {
        vehicle &veh = place_frames( here, 2 );
        REQUIRE( veh.install_part( here, point_rel_ms::zero, vpart_seat ) != -1 );
        dialogue d( get_talker_for( veh ), nullptr );
        // the seat goes first, then the frames, in as many rounds as it takes
        run_effect( d, R"({ "u_remove_vehicle_parts": "destroy" })" );
        CHECK( context( d, "vehicle_parts_removed" ) == 3 );
        CHECK( context( d, "vehicle_destroyed" ) == 1 );
        // still there for the rest of the EOC
        CHECK( eval_math( d, "u_vehicle_part_count()" ) == 0 );
        run_effect( d, R"({ "u_set_vehicle_owner": "your_followers" })" );
        here.vehmove();
        CHECK( here.get_vehicles().empty() );
    }
}

// VINT-J01: parts could only be switched from the electronics menu, so an EMP spell or a
// "sentient car" could not turn the lights or an engine on or off.
TEST_CASE( "vint-j01_toggle_vehicle_parts_and_engines", "[audit][vehicle][eoc]" )
{
    const restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
    reset_world();
    const on_out_of_scope cleanup( []() {
        clear_vehicles();
    } );
    map &here = get_map();
    vehicle &car = place_car( here );
    dialogue d( get_talker_for( car ), nullptr );
    const std::vector<int> headlights = parts_of( car, vpart_headlight );
    REQUIRE( headlights.size() == 2 );
    const auto lights_on = [&car, &headlights]() {
        int on = 0;
        for( const int idx : headlights ) {
            on += car.part( idx ).enabled ? 1 : 0;
        }
        return on;
    };
    eval_math( d, "u_vehicle_battery() = 0" );
    for( const int idx : headlights ) {
        car.part( idx ).enabled = false;
    }

    // a light that uses power can't be on without charge
    run_effect( d, R"({ "u_toggle_vehicle_parts": "on", "flag": "CONE_LIGHT" })" );
    CHECK( context( d, "vehicle_parts_toggled" ) == 0 );
    CHECK( lights_on() == 0 );

    eval_math( d, "u_vehicle_battery() = 1000" );
    run_effect( d, R"({ "u_toggle_vehicle_parts": "on", "flag": "CONE_LIGHT" })" );
    CHECK( context( d, "vehicle_parts_toggled" ) == 2 );
    CHECK( lights_on() == 2 );
    // "toggle" works as the electronics menu does: any on, so all off; none on, so all on
    run_effect( d, R"({ "u_toggle_vehicle_parts": "toggle", "part": "headlight" })" );
    CHECK( lights_on() == 0 );
    run_effect( d, R"({ "u_toggle_vehicle_parts": "toggle", "part": "headlight" })" );
    CHECK( lights_on() == 2 );
    run_effect( d, R"({ "u_toggle_vehicle_parts": "off", "flag": "CONE_LIGHT" })" );
    CHECK( lights_on() == 0 );

    // only the part at a location
    d.set_value( "spot", here.get_abs( car.bub_part_pos( here, headlights[1] ) ) );
    run_effect( d, R"({ "u_toggle_vehicle_parts": "on", "flag": "CONE_LIGHT", )"
                R"("loc": { "context_val": "spot" } })" );
    CHECK( context( d, "vehicle_parts_toggled" ) == 1 );
    CHECK( car.part( headlights[1] ).enabled );
    CHECK_FALSE( car.part( headlights[0] ).enabled );

    // a broken part isn't switched
    car.set_hp( car.part( headlights[0] ), 0, true );
    run_effect( d, R"({ "u_toggle_vehicle_parts": "on", "flag": "CONE_LIGHT" })" );
    CHECK( context( d, "vehicle_parts_toggled" ) == 0 );

    // switching off the only engine of a running vehicle stops it; switching it on doesn't start it
    eval_math( d, "u_vehicle_fuel('gasoline') = 5000" );
    eval_math( d, "u_vehicle_engine_on() = 1" );
    REQUIRE( car.engine_on );
    run_effect( d, R"({ "u_toggle_vehicle_parts": "off", "flag": "ENGINE" })" );
    CHECK( context( d, "vehicle_parts_toggled" ) == 1 );
    CHECK_FALSE( car.engine_on );
    run_effect( d, R"({ "u_toggle_vehicle_parts": "on", "flag": "ENGINE" })" );
    CHECK( context( d, "vehicle_parts_toggled" ) == 1 );
    CHECK_FALSE( car.engine_on );
    eval_math( d, "u_vehicle_engine_on() = 1" );
    CHECK( car.engine_on );
}

// VINT-J01: vehicle::chat_topics was saved but nothing ever set it and nothing offered to talk,
// so a vehicle could never have dialogue.
TEST_CASE( "vint-j01_vehicle_chat_topics_and_talk_entry", "[audit][vehicle][eoc]" )
{
    const restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
    reset_world();
    const on_out_of_scope cleanup( []() {
        clear_vehicles();
    } );
    map &here = get_map();
    const auto talk_entry = [&here]( vehicle & veh ) {
        veh_menu menu( veh, "TEST" );
        veh.build_interact_menu( menu, &here, veh.pos_bub( here ), false );
        for( const veh_menu_item &it : menu.get_items() ) {
            if( it._text.rfind( "Talk to the ", 0 ) == 0 ) {
                return std::optional<std::string>( it._text );
            }
        }
        return std::optional<std::string>();
    };

    vehicle *cart = here.add_vehicle( vehicle_prototype_test_vint_j01_talking_cart, veh_pos,
                                      0_degrees, 0, veh_spawn_status::UNDAMAGED );
    REQUIRE( cart != nullptr );
    CHECK( cart->chat_topics == std::vector<std::string> { "TALK_TEST_VINT_J01_CART" } );
    CHECK( get_talker_for( *cart )->get_topics( false ) == cart->chat_topics );
    CHECK( talk_entry( *cart ) == std::optional<std::string>( "Talk to the TEST talking cart" ) );

    dialogue d( get_talker_for( *cart ), nullptr );
    run_effect( d, R"({ "u_add_vehicle_chat_topic": "TALK_TEST_VINT_J01_CART_2" })" );
    // added last, so the conversation starts with it
    const std::vector<std::string> both = { "TALK_TEST_VINT_J01_CART", "TALK_TEST_VINT_J01_CART_2" };
    CHECK( cart->chat_topics == both );
    // adding one it has moves it to the end
    run_effect( d, R"({ "u_add_vehicle_chat_topic": "TALK_TEST_VINT_J01_CART" })" );
    const std::vector<std::string> swapped = { "TALK_TEST_VINT_J01_CART_2", "TALK_TEST_VINT_J01_CART" };
    CHECK( cart->chat_topics == swapped );
    run_effect( d, R"({ "u_remove_vehicle_chat_topic": "TALK_TEST_VINT_J01_CART" })" );
    run_effect( d, R"({ "u_remove_vehicle_chat_topic": "TALK_TEST_VINT_J01_CART_2" })" );
    CHECK( cart->chat_topics.empty() );
    CHECK_FALSE( talk_entry( *cart ).has_value() );

    // a stock car has nothing to say until an EOC gives it a topic
    vehicle *const car_ptr = here.add_vehicle( vehicle_prototype_car,
                             veh_pos + tripoint_rel_ms( 0, 8, 0 ), 0_degrees, 0, veh_spawn_status::UNDAMAGED );
    REQUIRE( car_ptr != nullptr );
    vehicle &car = *car_ptr;
    CHECK( car.chat_topics.empty() );
    CHECK_FALSE( talk_entry( car ).has_value() );
    dialogue d_car( get_talker_for( car ), nullptr );
    run_effect( d_car, R"({ "u_add_vehicle_chat_topic": "TALK_TEST_VINT_J01_CART" })" );
    CHECK( talk_entry( car ).has_value() );
}
