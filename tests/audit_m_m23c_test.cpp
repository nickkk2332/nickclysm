#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "coordinates.h"
#include "effect_source.h"
#include "enums.h"
#include "global_vars.h"
#include "map.h"
#include "map_helpers.h"
#include "math_parser_diag_value.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"
#include "units.h"
#include "veh_type.h"
#include "vehicle.h"

static const damage_type_id damage_pure( "pure" );

static const efftype_id effect_test_m23c_veh_acceleration( "test_m23c_veh_acceleration" );
static const efftype_id effect_test_m23c_veh_damage_taken( "test_m23c_veh_damage_taken" );
static const efftype_id effect_test_m23c_veh_drag( "test_m23c_veh_drag" );
static const efftype_id effect_test_m23c_veh_engine_power( "test_m23c_veh_engine_power" );
static const efftype_id effect_test_m23c_veh_max_velocity( "test_m23c_veh_max_velocity" );
static const efftype_id effect_test_m23c_veh_power_flow( "test_m23c_veh_power_flow" );
static const efftype_id effect_test_m23c_veh_traction( "test_m23c_veh_traction" );
static const efftype_id effect_test_m23c_veh_weight( "test_m23c_veh_weight" );

static const proficiency_id proficiency_prof_aircraft_mechanic( "prof_aircraft_mechanic" );
static const proficiency_id proficiency_prof_boat_pilot( "prof_boat_pilot" );
static const proficiency_id proficiency_prof_driver( "prof_driver" );
static const proficiency_id proficiency_prof_helicopter_pilot( "prof_helicopter_pilot" );

static const skill_id skill_driving( "driving" );
static const skill_id skill_mechanics( "mechanics" );

static const ter_str_id ter_t_water_dp( "t_water_dp" );

static const vpart_id vpart_engine_1cyl( "engine_1cyl" );
static const vpart_id vpart_engine_electric_small( "engine_electric_small" );
static const vpart_id vpart_foot_pedals( "foot_pedals" );
static const vpart_id vpart_frame( "frame" );
static const vpart_id vpart_test_m23c_spirit_engine( "test_m23c_spirit_engine" );

static const vproto_id vehicle_prototype_car( "car" );
static const vproto_id vehicle_prototype_scooter_electric_test( "scooter_electric_test" );
static const vproto_id vehicle_prototype_scooter_test( "scooter_test" );
static const vproto_id vehicle_prototype_solar_panel_test( "solar_panel_test" );
static const vproto_id vehicle_prototype_test_m23c_pilot_cart( "test_m23c_pilot_cart" );
static const vproto_id vehicle_prototype_test_m23c_spirit_cart( "test_m23c_spirit_cart" );

// clear of the avatar, which clear_avatar() puts at 60,60
static constexpr tripoint_bub_ms veh_pos( 66, 60, 0 );

namespace
{

// Every test starts from the same world: a fixed time, no other vehicles, creatures or globals.
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

vehicle &place( map &here, const vproto_id &proto )
{
    vehicle *veh = here.add_vehicle( proto, veh_pos, 0_degrees, 100, veh_spawn_status::UNDAMAGED );
    REQUIRE( veh != nullptr );
    return *veh;
}

void enchant( vehicle &veh, const efftype_id &eff )
{
    veh.add_effect( effect_source::empty(), eff, 1_hours, true );
    veh.recalculate_enchantment_cache();
}

double var_dbl( const vehicle &veh, const std::string &name )
{
    const diag_value *value = veh.maybe_get_value( name );
    REQUIRE( value != nullptr );
    return value->dbl();
}

// Index of the first part of the vehicle of a type, not removed.
int part_of( const vehicle &veh, const vpart_id &id )
{
    int found = -1;
    for( int idx = 0; idx < veh.part_count() && found < 0; idx++ ) {
        const vehicle_part &vp = veh.part( idx );
        if( !vp.removed && !vp.is_fake && vp.info().id == id ) {
            found = idx;
        }
    }
    CAPTURE( id.str() );
    REQUIRE( found >= 0 );
    return found;
}

} // namespace

// VEH-J08: handling (the skill and proficiency steering uses), training and the aircraft
// maintenance proficiency were hardcoded; control_requirements now set them per mode, and has a
// "water" key.
TEST_CASE( "veh-j08_control_requirements_set_handling_training_and_water",
           "[audit][vehicle]" )
{
    const time_point turn = calendar::turn;
    reset_world();
    map &here = get_map();
    Character &you = get_player_character();

    SECTION( "a vanilla car keeps the built-in ids" ) {
        vehicle &car = place( here, vehicle_prototype_car );
        CHECK( car.handling_skill() == skill_driving );
        CHECK( car.handling_proficiency() == proficiency_prof_driver );
        CHECK( car.trained_pilot_proficiencies() == std::vector<proficiency_id> { proficiency_prof_driver } );
        CHECK( car.aircraft_maintenance_proficiencies() ==
               std::vector<proficiency_id> { proficiency_prof_aircraft_mechanic } );
    }

    SECTION( "on land the part's land entry applies" ) {
        vehicle &cart = place( here, vehicle_prototype_test_m23c_pilot_cart );
        REQUIRE_FALSE( cart.is_in_water( true ) );
        CHECK( cart.handling_skill() == skill_mechanics );
        CHECK( cart.handling_proficiency() == proficiency_prof_helicopter_pilot );
        CHECK( cart.trained_pilot_proficiencies() ==
               std::vector<proficiency_id> { proficiency_prof_boat_pilot } );
        CHECK( cart.aircraft_maintenance_proficiencies() ==
               std::vector<proficiency_id> { proficiency_prof_helicopter_pilot } );
        CHECK( cart.can_control_on_land( you ) );

        // Driving trains what "trains" lists, not the handling proficiency
        REQUIRE( you.get_proficiency_practice( proficiency_prof_boat_pilot ) == 0.0f );
        REQUIRE( you.get_proficiency_practice( proficiency_prof_driver ) == 0.0f );
        for( int i = 0; i < 120; i++ ) {
            practice_pilot_proficiencies( you, cart );
        }
        CHECK( you.get_proficiency_practice( proficiency_prof_boat_pilot ) > 0.0f );
        CHECK( you.get_proficiency_practice( proficiency_prof_driver ) == 0.0f );
        CHECK( you.get_proficiency_practice( proficiency_prof_helicopter_pilot ) == 0.0f );
    }

    SECTION( "in deep water the water entry applies" ) {
        vehicle &cart = place( here, vehicle_prototype_test_m23c_pilot_cart );
        for( const tripoint_abs_ms &p : cart.get_points() ) {
            here.ter_set( here.get_bub( p ), ter_t_water_dp );
        }
        cart.check_falling_or_floating();
        REQUIRE( cart.is_in_water( true ) );
        // The water entry sets no handling, so the built-in boat ids apply
        CHECK( cart.handling_skill() == skill_driving );
        CHECK( cart.handling_proficiency() == proficiency_prof_boat_pilot );
        CHECK( cart.trained_pilot_proficiencies() ==
               std::vector<proficiency_id> { proficiency_prof_boat_pilot } );
        CHECK_FALSE( cart.can_control_in_water( you ) );
        you.add_proficiency( proficiency_prof_boat_pilot, true );
        CHECK( cart.can_control_in_water( you ) );
    }

    reset_world();
    calendar::turn = turn;
}

// VEH-J09: vehicle enchantments could only change weight, fuel use and turning difficulty. Each new
// value is checked here by doubling it ("multiply": 1) on a vehicle that has nothing else enchanted.
TEST_CASE( "veh-j09_vehicle_enchantment_values", "[audit][vehicle][enchantments]" )
{
    const time_point turn = calendar::turn;
    reset_world();
    map &here = get_map();

    SECTION( "engine power" ) {
        vehicle &veh = place( here, vehicle_prototype_test_m23c_spirit_cart );
        REQUIRE( units::to_watt( veh.total_power( here, false ) ) == 1000 );
        enchant( veh, effect_test_m23c_veh_engine_power );
        CHECK( units::to_watt( veh.total_power( here, false ) ) == 2000 );
    }

    SECTION( "top speed and safe speed" ) {
        vehicle &veh = place( here, vehicle_prototype_scooter_electric_test );
        const int max_before = veh.max_velocity( here, false );
        const int safe_before = veh.safe_velocity( here, false );
        REQUIRE( max_before > 0 );
        REQUIRE( safe_before > 0 );
        enchant( veh, effect_test_m23c_veh_max_velocity );
        CHECK( veh.max_velocity( here, false ) == max_before * 2 );
        CHECK( veh.safe_velocity( here, false ) == safe_before * 2 );
    }

    SECTION( "acceleration" ) {
        vehicle &veh = place( here, vehicle_prototype_scooter_electric_test );
        veh.engine_on = true;
        const int before = veh.acceleration( here, false );
        REQUIRE( before > 0 );
        enchant( veh, effect_test_m23c_veh_acceleration );
        CHECK( veh.acceleration( here, false ) == before * 2 );
    }

    SECTION( "air, rolling and water drag" ) {
        vehicle &veh = place( here, vehicle_prototype_scooter_electric_test );
        const double air = veh.coeff_air_drag();
        const double rolling = veh.coeff_rolling_drag( here );
        const double water = veh.coeff_water_drag( here );
        REQUIRE( air > 0.0 );
        REQUIRE( rolling > 0.0 );
        REQUIRE( water > 0.0 );
        enchant( veh, effect_test_m23c_veh_drag );
        CHECK( veh.coeff_air_drag() == Approx( air * 2 ) );
        CHECK( veh.coeff_rolling_drag( here ) == Approx( rolling * 2 ) );
        CHECK( veh.coeff_water_drag( here ) == Approx( water * 2 ) );
    }

    SECTION( "wheel traction" ) {
        vehicle &veh = place( here, vehicle_prototype_scooter_electric_test );
        const float area = static_cast<float>( veh.wheel_area() );
        REQUIRE( area > 0.0f );
        // All wheels on a surface with traction: 1
        REQUIRE( veh.k_traction( here, area, veh.total_mass( here ) ) == Approx( 1.0f ) );
        enchant( veh, effect_test_m23c_veh_traction );
        CHECK( veh.k_traction( here, area, veh.total_mass( here ) ) == Approx( 2.0f ) );
    }

    SECTION( "damage the parts take" ) {
        vehicle &veh = place( here, vehicle_prototype_test_m23c_spirit_cart );
        vehicle_part &frame = veh.part( part_of( veh, vpart_frame ) );
        REQUIRE( frame.hp() > 100 );
        // Compared in item damage levels: hp() rounds
        const int damage_full = frame.damage();
        veh.damage_direct( here, frame, 30, damage_pure );
        const int plain = frame.damage() - damage_full;
        REQUIRE( plain > 0 );
        enchant( veh, effect_test_m23c_veh_damage_taken );
        const int damage_before = frame.damage();
        veh.damage_direct( here, frame, 30, damage_pure );
        CHECK( frame.damage() - damage_before == 2 * plain );
    }

    SECTION( "renewable output" ) {
        vehicle &veh = place( here, vehicle_prototype_solar_panel_test );
        const std::int64_t before = units::to_milliwatt( veh.rated_solar_epower() );
        REQUIRE( before > 0 );
        enchant( veh, effect_test_m23c_veh_power_flow );
        CHECK( units::to_milliwatt( veh.rated_solar_epower() ) == before * 2 );
    }

    SECTION( "accessory drain" ) {
        vehicle &veh = place( here, vehicle_prototype_scooter_electric_test );
        REQUIRE_FALSE( veh.accessories.empty() );
        for( const int p : veh.accessories ) {
            veh.part( p ).enabled = true;
        }
        const std::int64_t before = units::to_milliwatt( veh.total_accessory_epower() );
        REQUIRE( before < 0 );
        enchant( veh, effect_test_m23c_veh_power_flow );
        CHECK( units::to_milliwatt( veh.total_accessory_epower() ) == before * 2 );
    }

    // VEH-B15 (cited by VEH-J09): weight_on_wheels() applied TOTAL_WEIGHT on top of total_mass()
    SECTION( "weight on wheels has TOTAL_WEIGHT once" ) {
        vehicle &veh = place( here, vehicle_prototype_scooter_electric_test );
        enchant( veh, effect_test_m23c_veh_weight );
        CHECK( units::to_gram( veh.weight_on_wheels( here ) ) == units::to_gram( veh.total_mass( here ) ) );
    }

    reset_world();
    calendar::turn = turn;
}

// VEH-J10: a vehicle prototype could set chat topics (VINT-J01) but no variables and no EOC to
// run when one spawns.
TEST_CASE( "veh-j10_prototype_variables_and_spawn_eocs", "[audit][vehicle][eoc]" )
{
    const time_point turn = calendar::turn;
    reset_world();
    map &here = get_map();

    vehicle &first = place( here, vehicle_prototype_test_m23c_spirit_cart );
    CHECK( var_dbl( first, "m23c_watts" ) == 1000 );
    const diag_value *kind = first.maybe_get_value( "m23c_kind" );
    REQUIRE( kind != nullptr );
    CHECK( kind->str() == "cart" );
    // The spawn EOC ran once, on this vehicle
    CHECK( var_dbl( first, "m23c_spawned" ) == 1 );

    // Each vehicle has its own copy: changing one doesn't change the next one spawned
    first.set_value( "m23c_watts", 5 );
    clear_vehicles();
    vehicle &second = place( here, vehicle_prototype_test_m23c_spirit_cart );
    CHECK( var_dbl( second, "m23c_watts" ) == 1000 );
    CHECK( var_dbl( second, "m23c_spawned" ) == 1 );

    // A vanilla prototype without them
    clear_vehicles();
    const vehicle &car = place( here, vehicle_prototype_car );
    CHECK( car.maybe_get_value( "m23c_spawned" ) == nullptr );

    reset_world();
    calendar::turn = turn;
}

// VEH-J11: what an engine does was keyed on its fuel id. Engines now take "power_math" (alpha the
// vehicle, beta the rider), "full_reverse" and "stall_message", defaulting from the old ids.
TEST_CASE( "veh-j11_engine_power_math_reverse_and_stall_message", "[audit][vehicle]" )
{
    const time_point turn = calendar::turn;
    reset_world();
    map &here = get_map();
    avatar &you = get_avatar();

    SECTION( "the defaults come from the fuel ids" ) {
        CHECK( vpart_engine_electric_small->engine_info->full_reverse.value_or( false ) );
        CHECK( vpart_engine_electric_small->engine_info->stall_message.value_or( false ) );
        CHECK_FALSE( vpart_engine_1cyl->engine_info->full_reverse.value_or( true ) );
        CHECK( vpart_engine_1cyl->engine_info->stall_message.value_or( false ) );
        CHECK_FALSE( vpart_foot_pedals->engine_info->stall_message.value_or( true ) );
        // Set in JSON, against the battery default
        CHECK_FALSE( vpart_test_m23c_spirit_engine->engine_info->full_reverse.value_or( true ) );
        CHECK_FALSE( vpart_test_m23c_spirit_engine->engine_info->stall_message.value_or( true ) );

        CHECK( place( here, vehicle_prototype_scooter_electric_test ).has_full_reverse_engine() );
        clear_vehicles();
        CHECK_FALSE( place( here, vehicle_prototype_scooter_test ).has_full_reverse_engine() );
    }

    SECTION( "power from math" ) {
        vehicle &veh = place( here, vehicle_prototype_test_m23c_spirit_cart );
        CHECK_FALSE( veh.has_full_reverse_engine() );
        // u_m23c_watts, from the prototype's variables
        CHECK( units::to_watt( veh.total_power( here, false ) ) == 1000 );
        veh.set_value( "m23c_watts", 2500 );
        CHECK( units::to_watt( veh.total_power( here, false ) ) == 2500 );

        // A rider on the engine's tile is beta
        const int engine = part_of( veh, vpart_test_m23c_spirit_engine );
        const tripoint_bub_ms seat = veh.bub_part_pos( here, engine );
        you.set_value( "m23c_bonus", 400 );
        you.setpos( here, seat );
        here.board_vehicle( seat, &you );
        REQUIRE( veh.get_passenger( engine ) == &you );
        CHECK( units::to_watt( veh.total_power( here, false ) ) == 2900 );
        here.unboard_vehicle( seat );
        CHECK( units::to_watt( veh.total_power( here, false ) ) == 2500 );

        // With no power the engine stops
        veh.set_value( "m23c_watts", 0 );
        veh.engine_on = true;
        veh.idle( here, false );
        CHECK_FALSE( veh.engine_on );
    }

    reset_world();
    calendar::turn = turn;
}
