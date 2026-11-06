#include <stdint.h>
#include <functional>
#include <iterator>
#include <list>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "condition.h"
#include "coordinates.h"
#include "dialogue.h"
#include "enums.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "item.h"
#include "item_location.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "math_parser.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "point.h"
#include "ret_val.h"
#include "talker.h"
#include "type_id.h"
#include "units.h"
#include "veh_utils.h"
#include "vehicle.h"
#include "vpart_position.h"
#include "vpart_range.h"

static const itype_id itype_9mm( "9mm" );
static const itype_id itype_apple( "apple" );
static const itype_id itype_backpack( "backpack" );
static const itype_id itype_bag_canvas( "bag_canvas" );
static const itype_id itype_battery( "battery" );
static const itype_id itype_biodiesel( "biodiesel" );
static const itype_id itype_bottle_plastic( "bottle_plastic" );
static const itype_id itype_diesel( "diesel" );
static const itype_id itype_duffelbag( "duffelbag" );
static const itype_id itype_gasoline( "gasoline" );
static const itype_id itype_lighter( "lighter" );
static const itype_id itype_remotevehcontrol( "remotevehcontrol" );
static const itype_id itype_rock( "rock" );
static const itype_id itype_water_clean( "water_clean" );

static const vpart_id vpart_diesel_engine_inline4( "diesel_engine_inline4" );
static const vpart_id vpart_frame( "frame" );
static const vpart_id vpart_headlight( "headlight" );
static const vpart_id vpart_tank( "tank" );
static const vpart_id vpart_test_force_field( "test_force_field" );
static const vpart_id vpart_wide_headlight( "wide_headlight" );

static const vproto_id vehicle_prototype_car( "car" );
static const vproto_id vehicle_prototype_none( "none" );

static constexpr tripoint_bub_ms car_pos( 60, 60, 0 );
// clear of the car placed at car_pos
static constexpr tripoint_bub_ms second_veh_pos( 60, 66, 0 );

static conditional_t::func load_condition( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    conditional_t::func condition;
    read_condition( jo, "condition", condition, false );
    return condition;
}

static double eval_math( dialogue &d, const std::string &expression )
{
    math_exp exp;
    REQUIRE( exp.parse( expression ) );
    return exp.eval( d );
}

static item &wear_backpack( Character &you )
{
    std::optional<std::list<item>::iterator> worn = you.wear_item( item( itype_backpack ), false );
    REQUIRE( worn.has_value() );
    // The item lives in the worn list; taking the iterator out first keeps Clang from reading
    // `**worn` as a reference into the local optional.
    const std::list<item>::iterator it = *worn;
    return *it;
}

// The single item of the given type in the container pockets of a container.
static item &content_of( item &container, const itype_id &id )
{
    for( item *content : container.all_items_top( pocket_type::CONTAINER ) ) {
        if( content->typeId() == id ) {
            return *content;
        }
    }
    FAIL( "no " << id.str() << " in " << container.typeId().str() );
    return container;
}

// INV-J03: no math function exposed the carried weight, the carrying capacity or the free room
// in a character's pockets, so an EOC could not check whether the player can take a reward.
TEST_CASE( "inv-j03_math_reads_character_load_and_capacity", "[audit][eoc][inventory]" )
{
    clear_avatar();
    clear_map();
    avatar &u = get_avatar();
    wear_backpack( u );
    // wearing does not refresh the carried weight cache; that is not what is tested here
    u.invalidate_weight_carried_cache();
    dialogue d( get_talker_for( u ), nullptr );

    const double carried_before = eval_math( d, "u_weight_carried()" );
    const double free_before = eval_math( d, "u_free_volume()" );
    const item rock( itype_rock );
    REQUIRE( u.i_add( rock ) );
    u.invalidate_weight_carried_cache();

    CHECK( eval_math( d, "u_weight_carried()" ) - carried_before ==
           Approx( units::to_milligram( rock.weight() ) ) );
    CHECK( eval_math( d, "u_weight_carried()" ) ==
           Approx( units::to_milligram( u.weight_carried() ) ) );
    CHECK( eval_math( d, "u_weight_capacity()" ) ==
           Approx( units::to_milligram( u.weight_capacity() ) ) );
    CHECK( eval_math( d, "u_weight_capacity()" ) > 0 );
    CHECK( free_before - eval_math( d, "u_free_volume()" ) ==
           Approx( units::to_milliliter( rock.volume() ) ) );
    CHECK( eval_math( d, "u_volume_capacity()" ) ==
           Approx( units::to_milliliter( u.volume_capacity() ) ) );
    CHECK( eval_math( d, "u_volume_capacity()" ) >= 25000 );
    CHECK( eval_math( d, "u_volume_carried()" ) ==
           Approx( units::to_milliliter( u.volume_carried() ) ) );
}

// INV-J03: an item talker could not report what its pockets hold or how much room is left.
TEST_CASE( "inv-j03_math_reads_container_contents", "[audit][eoc][inventory]" )
{
    clear_avatar();
    clear_map();
    avatar &u = get_avatar();
    item &pack = wear_backpack( u );
    item_location pack_loc( u, &pack );
    dialogue d( get_talker_for( u ), get_talker_for( pack_loc ) );

    const double capacity = eval_math( d, "n_volume_capacity()" );
    CHECK( capacity == Approx( units::to_milliliter( pack.get_volume_capacity() ) ) );
    CHECK( eval_math( d, "n_contents_count()" ) == 0 );
    CHECK( eval_math( d, "n_contents_volume()" ) == 0 );
    CHECK( eval_math( d, "n_contents_weight()" ) == 0 );
    CHECK( eval_math( d, "n_remaining_volume()" ) == Approx( capacity ) );
    const double weight_capacity = eval_math( d, "n_weight_capacity()" );
    CHECK( weight_capacity == Approx( units::to_milligram( pack.get_total_weight_capacity() ) ) );
    CHECK( eval_math( d, "n_remaining_weight()" ) == Approx( weight_capacity ) );

    const item rock( itype_rock );
    const item ammo( itype_9mm, calendar::turn, 10 );
    for( int i = 0; i < 3; ++i ) {
        REQUIRE( pack.put_in( rock, pocket_type::CONTAINER ).success() );
    }
    REQUIRE( pack.put_in( ammo, pocket_type::CONTAINER ).success() );
    const units::volume stored_volume = rock.volume() * 3 + ammo.volume();
    const units::mass stored_weight = rock.weight() * 3 + ammo.weight();

    // a stack counted by charges counts each charge
    CHECK( eval_math( d, "n_contents_count()" ) == 13 );
    CHECK( eval_math( d, "n_contents_volume()" ) ==
           Approx( units::to_milliliter( stored_volume ) ) );
    CHECK( eval_math( d, "n_contents_weight()" ) ==
           Approx( units::to_milligram( stored_weight ) ) );
    CHECK( eval_math( d, "n_remaining_volume()" ) ==
           Approx( capacity - units::to_milliliter( stored_volume ) ) );
    CHECK( eval_math( d, "n_remaining_weight()" ) ==
           Approx( weight_capacity - units::to_milligram( stored_weight ) ) );
    // the same name on a character reads the character's pockets instead
    CHECK( eval_math( d, "u_volume_capacity()" ) ==
           Approx( units::to_milliliter( u.volume_capacity() ) ) );
}

// INV-J03: there were no conditions for container state or containment, and has_ammo and
// is_rotten only looked at the beta talker.
TEST_CASE( "inv-j03_container_conditions", "[audit][eoc][inventory]" )
{
    clear_avatar();
    clear_map();
    avatar &u = get_avatar();
    item &pack = wear_backpack( u );
    item_location pack_loc( u, &pack );

    const conditional_t::func u_is_container = load_condition(
                R"({ "condition": "u_is_container" })" );
    const conditional_t::func npc_is_container = load_condition(
                R"({ "condition": "npc_is_container" })" );
    const conditional_t::func npc_is_container_empty = load_condition(
                R"({ "condition": "npc_is_container_empty" })" );
    const conditional_t::func npc_is_sealed = load_condition(
                R"({ "condition": "npc_is_sealed" })" );
    const conditional_t::func npc_contains_rock = load_condition(
                R"({ "condition": { "npc_contains_item": "rock" } })" );
    const conditional_t::func npc_contains_bag = load_condition(
                R"({ "condition": { "npc_contains_item": "bag_canvas" } })" );
    const conditional_t::func npc_contains_backpack = load_condition(
                R"({ "condition": { "npc_contains_item": "backpack" } })" );

    {
        dialogue d( get_talker_for( u ), get_talker_for( pack_loc ) );
        CHECK_FALSE( u_is_container( d ) );
        CHECK( npc_is_container( d ) );
        CHECK( npc_is_container_empty( d ) );
        CHECK_FALSE( npc_is_sealed( d ) );
        CHECK_FALSE( npc_contains_rock( d ) );
        // a container does not contain itself
        CHECK_FALSE( npc_contains_backpack( d ) );
    }

    REQUIRE( pack.put_in( item( itype_bag_canvas ), pocket_type::CONTAINER ).success() );
    item &bag = content_of( pack, itype_bag_canvas );
    REQUIRE( bag.put_in( item( itype_rock ), pocket_type::CONTAINER ).success() );
    item &rock = content_of( bag, itype_rock );
    item_location bag_loc( pack_loc, &bag );
    item_location rock_loc( bag_loc, &rock );

    {
        dialogue d( get_talker_for( u ), get_talker_for( pack_loc ) );
        CHECK_FALSE( npc_is_container_empty( d ) );
        CHECK( npc_contains_bag( d ) );
        // nested contents count too
        CHECK( npc_contains_rock( d ) );
    }
    {
        dialogue d( get_talker_for( rock_loc ), get_talker_for( u ) );
        CHECK_FALSE( load_condition( R"({ "condition": "u_is_container" })" )( d ) );
        // any enclosing container counts, not only the direct one
        CHECK( load_condition( R"({ "condition": { "u_is_contained_in": "bag_canvas" } })" )( d ) );
        CHECK( load_condition( R"({ "condition": { "u_is_contained_in": "backpack" } })" )( d ) );
        CHECK_FALSE( load_condition(
                         R"({ "condition": { "u_is_contained_in": "duffelbag" } })" )( d ) );
        // the beta talker is a character, which is not contained in anything
        CHECK_FALSE( load_condition(
                         R"({ "condition": { "npc_is_contained_in": "backpack" } })" )( d ) );
    }
    REQUIRE( itype_duffelbag.is_valid() );

    item bottle( itype_bottle_plastic );
    bottle.fill_with( item( itype_water_clean ) );
    REQUIRE( bottle.seal() );
    item_location bottle_loc = u.i_add( bottle );
    REQUIRE( bottle_loc );
    {
        dialogue d( get_talker_for( u ), get_talker_for( bottle_loc ) );
        CHECK( npc_is_sealed( d ) );
        CHECK( npc_is_container( d ) );
        CHECK_FALSE( npc_is_container_empty( d ) );
    }
}

// INV-J03: has_ammo and is_rotten only worked on the beta talker; they now come in u_/npc_
// pairs, and the old names keep meaning the beta talker.
TEST_CASE( "inv-j03_is_rotten_and_has_ammo_pairs", "[audit][eoc][inventory]" )
{
    clear_avatar();
    clear_map();
    avatar &u = get_avatar();
    wear_backpack( u );
    item apple( itype_apple );
    apple.set_relative_rot( 2.0 );
    item_location apple_loc = u.i_add( apple );
    REQUIRE( apple_loc );
    REQUIRE( apple_loc->rotten() );
    item_location loaded_loc = u.i_add( tool_with_ammo( itype_lighter, 20 ) );
    REQUIRE( loaded_loc );
    item empty_lighter( itype_lighter );
    empty_lighter.ammo_unset();
    item_location empty_loc = u.i_add( empty_lighter );
    REQUIRE( empty_loc );

    const conditional_t::func u_is_rotten = load_condition(
            R"({ "condition": "u_is_rotten" })" );
    const conditional_t::func npc_is_rotten = load_condition(
                R"({ "condition": "npc_is_rotten" })" );
    const conditional_t::func is_rotten = load_condition( R"({ "condition": "is_rotten" })" );
    const conditional_t::func u_has_ammo = load_condition( R"({ "condition": "u_has_ammo" })" );
    const conditional_t::func npc_has_ammo = load_condition(
                R"({ "condition": "npc_has_ammo" })" );
    const conditional_t::func has_ammo = load_condition( R"({ "condition": "has_ammo" })" );

    {
        dialogue d( get_talker_for( apple_loc ), get_talker_for( u ) );
        CHECK( u_is_rotten( d ) );
    }
    {
        dialogue d( get_talker_for( u ), get_talker_for( apple_loc ) );
        CHECK( npc_is_rotten( d ) );
        CHECK( is_rotten( d ) );
    }
    {
        dialogue d( get_talker_for( loaded_loc ), get_talker_for( u ) );
        CHECK( u_has_ammo( d ) );
    }
    {
        dialogue d( get_talker_for( empty_loc ), get_talker_for( u ) );
        CHECK_FALSE( u_has_ammo( d ) );
    }
    {
        dialogue d( get_talker_for( u ), get_talker_for( loaded_loc ) );
        CHECK( npc_has_ammo( d ) );
        CHECK( has_ammo( d ) );
    }
    {
        dialogue d( get_talker_for( u ), get_talker_for( empty_loc ) );
        CHECK_FALSE( npc_has_ammo( d ) );
        CHECK_FALSE( has_ammo( d ) );
    }
}

static vehicle &place_empty_car( map &here )
{
    vehicle *veh = here.add_vehicle( vehicle_prototype_car, car_pos, 0_degrees, 0,
                                     veh_spawn_status::UNDAMAGED );
    REQUIRE( veh != nullptr );
    return *veh;
}

static vehicle &place_two_frames( map &here, const tripoint_bub_ms &pos = car_pos )
{
    vehicle *veh = here.add_vehicle( vehicle_prototype_none, pos, 0_degrees, 0,
                                     veh_spawn_status::UNDAMAGED );
    REQUIRE( veh != nullptr );
    REQUIRE( veh->install_part( here, point_rel_ms::zero, vpart_frame ) != -1 );
    REQUIRE( veh->install_part( here, point_rel_ms( 1, 0 ), vpart_frame ) != -1 );
    return *veh;
}

// VEH-J01: no math function took a vehicle talker, so an EOC could not read or change fuel,
// battery charge, engine state, speed, lock or part condition.
TEST_CASE( "veh-j01_math_reads_and_writes_vehicle_state", "[audit][eoc][vehicle]" )
{
    clear_avatar();
    clear_map();
    clear_vehicles();
    map &here = get_map();
    vehicle &veh = place_empty_car( here );
    dialogue d( get_talker_for( veh ), nullptr );

    SECTION( "fuel" ) {
        REQUIRE( veh.fuel_left( here, itype_gasoline ) == 0 );
        CHECK( eval_math( d, "u_vehicle_fuel('gasoline')" ) == 0 );
        // fills the empty tank that accepts gasoline
        eval_math( d, "u_vehicle_fuel('gasoline') = 5000" );
        CHECK( veh.fuel_left( here, itype_gasoline ) == 5000 );
        CHECK( eval_math( d, "u_vehicle_fuel('gasoline')" ) == 5000 );
        const double capacity = eval_math( d, "u_vehicle_fuel_capacity('gasoline')" );
        CHECK( capacity == veh.fuel_capacity( here, itype_gasoline ) );
        CHECK( capacity >= 5000 );
        eval_math( d, "u_vehicle_fuel('gasoline') -= 3800" );
        CHECK( veh.fuel_left( here, itype_gasoline ) == 1200 );
        // more than the tanks hold fills them up
        eval_math( d, "u_vehicle_fuel('gasoline') = 100000000" );
        CHECK( veh.fuel_left( here, itype_gasoline ) == static_cast<int64_t>( capacity ) );
    }

    SECTION( "fuel_leaves_carried_tanks_alone" ) {
        // a racked vehicle's tank is not counted as fuel, so lowering the fuel must not drain it
        vehicle &racked = place_two_frames( here, second_veh_pos );
        const int carried = racked.install_part( here, point_rel_ms::zero, vpart_tank );
        const int own = racked.install_part( here, point_rel_ms( 1, 0 ), vpart_tank );
        REQUIRE( carried != -1 );
        REQUIRE( own != -1 );
        // vehicle::drain() goes through the parts in order, so it reaches the carried tank first
        REQUIRE( carried < own );
        racked.part( carried ).ammo_set( itype_gasoline, 1000 );
        racked.part( own ).ammo_set( itype_gasoline, 1000 );
        racked.part( carried ).set_flag( vp_flag::carried_flag );
        dialogue dr( get_talker_for( racked ), nullptr );
        REQUIRE( eval_math( dr, "u_vehicle_fuel('gasoline')" ) == 1000 );
        eval_math( dr, "u_vehicle_fuel('gasoline') = 400" );
        CHECK( eval_math( dr, "u_vehicle_fuel('gasoline')" ) == 400 );
        CHECK( racked.part( own ).ammo_remaining() == 400 );
        CHECK( racked.part( carried ).ammo_remaining() == 1000 );
        here.destroy_vehicle( &racked );
    }

    SECTION( "battery" ) {
        const double battery_max = eval_math( d, "u_vehicle_battery_max()" );
        REQUIRE( battery_max > 100 );
        CHECK( battery_max == veh.fuel_capacity( here, itype_battery ) );
        CHECK( eval_math( d, "u_vehicle_battery()" ) == 0 );
        eval_math( d, "u_vehicle_battery() = 100" );
        CHECK( veh.battery_left( here, false ) == 100 );
        CHECK( eval_math( d, "u_vehicle_battery()" ) == 100 );
        CHECK( eval_math( d, "u_vehicle_fuel('battery')" ) == 100 );
        eval_math( d, "u_vehicle_fuel('battery') = 40" );
        CHECK( veh.battery_left( here, false ) == 40 );
    }

    SECTION( "engine_and_speed" ) {
        CHECK( eval_math( d, "u_vehicle_engine_on()" ) == 0 );
        // an engine with no fuel does not start
        eval_math( d, "u_vehicle_engine_on() = 1" );
        CHECK_FALSE( veh.engine_on );
        eval_math( d, "u_vehicle_fuel('gasoline') = 2000" );
        eval_math( d, "u_vehicle_engine_on() = 1" );
        CHECK( veh.engine_on );
        CHECK( eval_math( d, "u_vehicle_engine_on()" ) == 1 );
        CHECK( eval_math( d, "u_vehicle_power()" ) ==
               Approx( units::to_watt( veh.total_power( here ) ) ) );
        CHECK( eval_math( d, "u_vehicle_power()" ) > 0 );
        CHECK( eval_math( d, "u_vehicle_max_speed()" ) ==
               Approx( veh.max_velocity( here ) / 100.0 ) );
        REQUIRE( veh.max_velocity( here ) > 2000 );

        eval_math( d, "u_vehicle_cruise() = 20" );
        CHECK( veh.cruise_velocity == 2000 );
        CHECK( eval_math( d, "u_vehicle_cruise()" ) == Approx( 20 ) );
        // cruise control cannot be set above what the engines reach
        eval_math( d, "u_vehicle_cruise() = 100000" );
        CHECK( veh.cruise_velocity == veh.max_velocity( here ) );

        eval_math( d, "u_vehicle_velocity() = 10" );
        CHECK( veh.velocity == 1000 );
        CHECK( eval_math( d, "u_vehicle_velocity()" ) == Approx( 10 ) );

        eval_math( d, "u_vehicle_engine_on() = 0" );
        CHECK_FALSE( veh.engine_on );
    }

    SECTION( "engine_switches_to_another_fuel" ) {
        // a diesel engine also burns biodiesel; with only biodiesel aboard it starts on that
        vehicle &diesel = place_two_frames( here, second_veh_pos );
        const int engine = diesel.install_part( here, point_rel_ms::zero,
                                                vpart_diesel_engine_inline4 );
        REQUIRE( engine != -1 );
        REQUIRE( diesel.install_part( here, point_rel_ms( 1, 0 ), vpart_tank ) != -1 );
        REQUIRE( diesel.part( engine ).fuel_current() == itype_diesel );
        dialogue dd( get_talker_for( diesel ), nullptr );
        eval_math( dd, "u_vehicle_fuel('biodiesel') = 2000" );
        REQUIRE( diesel.fuel_left( here, itype_biodiesel ) == 2000 );
        eval_math( dd, "u_vehicle_engine_on() = 1" );
        CHECK( diesel.engine_on );
        CHECK( diesel.part( engine ).fuel_current() == itype_biodiesel );
        here.destroy_vehicle( &diesel );
    }

    SECTION( "lock" ) {
        veh.is_locked = false;
        eval_math( d, "u_vehicle_locked() = 1" );
        CHECK( veh.is_locked );
        CHECK( eval_math( d, "u_vehicle_locked()" ) == 1 );
        eval_math( d, "u_vehicle_locked() = 0" );
        CHECK_FALSE( veh.is_locked );
    }

    SECTION( "parts" ) {
        const vehicle_part_with_feature_range<std::string> wheels = veh.get_avail_parts( "WHEEL" );
        const int wheel_count = std::distance( wheels.begin(), wheels.end() );
        REQUIRE( wheel_count >= 2 );
        CHECK( eval_math( d, "u_vehicle_part_count('WHEEL')" ) == wheel_count );
        CHECK( eval_math( d, "u_vehicle_part_broken_count('WHEEL')" ) == 0 );
        CHECK( eval_math( d, "u_vehicle_part_hp('WHEEL')" ) == Approx( 100 ) );

        vehicle_part &flat = ( *wheels.begin() ).part();
        veh.set_hp( flat, 0, true );
        REQUIRE( flat.is_broken() );
        CHECK( eval_math( d, "u_vehicle_part_count('WHEEL')" ) == wheel_count - 1 );
        CHECK( eval_math( d, "u_vehicle_part_broken_count('WHEEL')" ) == 1 );
        CHECK( eval_math( d, "u_vehicle_part_hp('WHEEL')" ) == Approx( 0 ) );
        CHECK( eval_math( d, "u_vehicle_part_hp('WHEEL', 'aggregate': 'max')" ) == Approx( 100 ) );
        CHECK( eval_math( d, "u_vehicle_part_hp('WHEEL', 'aggregate': 'average')" ) ==
               Approx( 100.0 * ( wheel_count - 1 ) / wheel_count ) );
    }
    here.destroy_vehicle( &veh );
}

// VEH-J01: u_is_remote_controlled and u_is_driven went through vehicle::remote_controlled(),
// which drops the avatar's remote link when no remote controls are in range, so merely
// checking a condition cut the connection. u_is_driven also ignored autopilot driving.
TEST_CASE( "veh-j01_vehicle_conditions_have_no_side_effects", "[audit][eoc][vehicle]" )
{
    clear_avatar();
    clear_map();
    clear_vehicles();
    map &here = get_map();
    avatar &u = get_avatar();
    vehicle &veh = place_empty_car( here );
    item remote( itype_remotevehcontrol );
    remote.active = true;
    REQUIRE( u.i_add( remote ) );
    REQUIRE( u.has_active_item( itype_remotevehcontrol ) );
    g->setremoteveh( &veh );
    REQUIRE( g->remoteveh() == &veh );

    const conditional_t::func is_remote_controlled = load_condition(
                R"({ "condition": "u_is_remote_controlled" })" );
    const conditional_t::func is_driven = load_condition( R"({ "condition": "u_is_driven" })" );
    dialogue d( get_talker_for( veh ), nullptr );

    // the car has no remote controls, so the link does not reach it
    CHECK_FALSE( is_remote_controlled( d ) );
    CHECK( g->remoteveh() == &veh );
    CHECK_FALSE( is_driven( d ) );
    CHECK( g->remoteveh() == &veh );

    veh.is_patrolling = true;
    CHECK( is_driven( d ) );
    veh.is_patrolling = false;
    veh.is_following = true;
    CHECK( is_driven( d ) );
    veh.is_following = false;
    // the debug override counts as being at the controls, as in vehicle::player_in_control()
    veh.tags.insert( "IN_CONTROL_OVERRIDE" );
    CHECK( is_driven( d ) );
    CHECK( g->remoteveh() == &veh );
    veh.tags.erase( "IN_CONTROL_OVERRIDE" );

    g->setremoteveh( nullptr );
    here.destroy_vehicle( &veh );
}

// The electronics menu entries of a vehicle.
static std::vector<veh_menu_item> electronics_menu( map &here, vehicle &veh )
{
    veh_menu menu( veh, "TEST" );
    veh.build_electronics_menu( here, menu );
    return menu.get_items();
}

static std::optional<veh_menu_item> menu_entry( const std::vector<veh_menu_item> &items,
        const std::string &text )
{
    for( const veh_menu_item &it : items ) {
        if( it._text.find( text ) != std::string::npos ) {
            return it;
        }
    }
    return std::nullopt;
}

// VINT-J02: the electronics menu only offered toggles for 25 hardcoded flags, so a modded
// powered part could never be switched on. A json_flag with "vehicle_toggle" now adds one.
TEST_CASE( "vint-j02_json_flag_toggle_switches_modded_parts", "[audit][vehicle]" )
{
    clear_avatar();
    clear_map();
    clear_vehicles();
    map &here = get_map();
    vehicle &veh = place_two_frames( here );
    const int first = veh.install_part( here, point_rel_ms::zero, vpart_test_force_field );
    REQUIRE( first != -1 );
    // nothing of its group is on, so it starts off
    CHECK_FALSE( veh.part( first ).enabled );

    const std::optional<veh_menu_item> turn_on =
        menu_entry( electronics_menu( here, veh ), "Turn on test force field" );
    REQUIRE( turn_on.has_value() );
    CHECK( turn_on->_enabled );
    // without a hotkey in JSON the menu picks a free key
    CHECK_FALSE( turn_on->_hotkey_action.has_value() );
    turn_on->_on_submit();
    CHECK( veh.part( first ).enabled );

    // a part installed while its group is on starts on too
    const int second = veh.install_part( here, point_rel_ms( 1, 0 ), vpart_test_force_field );
    REQUIRE( second != -1 );
    CHECK( veh.part( second ).enabled );

    const std::optional<veh_menu_item> turn_off =
        menu_entry( electronics_menu( here, veh ), "Turn off test force field" );
    REQUIRE( turn_off.has_value() );
    turn_off->_on_submit();
    CHECK_FALSE( veh.part( first ).enabled );
    CHECK_FALSE( veh.part( second ).enabled );
    here.destroy_vehicle( &veh );
}

// VINT-J02: the vanilla toggles moved from C++ into vp_flags.json keep their names and
// hotkeys, and the install-time list now covers every toggle (wide angle headlights were
// left out of it, so a new one stayed off while the others were on).
TEST_CASE( "vint-j02_vanilla_toggles_come_from_json", "[audit][vehicle]" )
{
    clear_avatar();
    clear_map();
    clear_vehicles();
    map &here = get_map();
    vehicle &veh = place_two_frames( here );
    REQUIRE( veh.install_part( here, point_rel_ms::zero, vpart_headlight ) != -1 );

    const std::optional<veh_menu_item> headlights =
        menu_entry( electronics_menu( here, veh ), "Turn on headlights" );
    REQUIRE( headlights.has_value() );
    CHECK( headlights->_text == "Turn on headlights" );
    CHECK( headlights->_hotkey_action == std::optional<std::string>( "TOGGLE_HEADLIGHT" ) );

    const int wide = veh.install_part( here, point_rel_ms::zero, vpart_wide_headlight );
    REQUIRE( wide != -1 );
    CHECK_FALSE( veh.part( wide ).enabled );
    veh.part( wide ).enabled = true;
    const int wide_2 = veh.install_part( here, point_rel_ms( 1, 0 ), vpart_wide_headlight );
    REQUIRE( wide_2 != -1 );
    CHECK( veh.part( wide_2 ).enabled );
    here.destroy_vehicle( &veh );
}
