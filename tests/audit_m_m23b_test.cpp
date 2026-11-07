#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "cata_variant.h"
#include "coordinates.h"
#include "damage.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "fault.h"
#include "global_vars.h"
#include "item.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "mapdata.h"
#include "monster.h"
#include "player_helpers.h"
#include "point.h"
#include "rng.h"
#include "sounds.h"
#include "talker.h"
#include "tileray.h"
#include "trap.h"
#include "type_id.h"
#include "units.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vpart_position.h"
#include "vpart_range.h"

static const damage_type_id damage_bash( "bash" );
static const damage_type_id damage_cut( "cut" );
static const damage_type_id damage_electric( "electric" );
static const damage_type_id damage_pure( "pure" );

static const fault_id fault_axle_broken( "fault_axle_broken" );
static const fault_id fault_cracked_rim( "fault_cracked_rim" );
static const fault_id fault_engine_filter_air( "fault_engine_filter_air" );
static const fault_id fault_engine_filter_fuel( "fault_engine_filter_fuel" );
static const fault_id fault_engine_starter( "fault_engine_starter" );
static const fault_id fault_flat_tire_riding_on_rims( "fault_flat_tire_riding_on_rims" );
static const fault_id fault_punctured_tires( "fault_punctured_tires" );
static const fault_id fault_tire_treads( "fault_tire_treads" );
static const fault_id test_veh_j04_bent_tie_rod( "test_veh_j04_bent_tie_rod" );
static const fault_id test_veh_j04_low_oil( "test_veh_j04_low_oil" );

static const ter_str_id ter_t_railroad_track( "t_railroad_track" );
static const ter_str_id ter_t_railroad_track_d( "t_railroad_track_d" );
static const ter_str_id ter_t_railroad_track_recessed( "t_railroad_track_recessed" );
static const ter_str_id ter_t_railroad_track_small( "t_railroad_track_small" );
static const ter_str_id ter_t_railroad_track_small_d( "t_railroad_track_small_d" );

static const trap_str_id tr_test_veh_j05_spikes( "tr_test_veh_j05_spikes" );

static const vpart_id vpart_frame( "frame" );
static const vpart_id vpart_funnel( "funnel" );
static const vpart_id vpart_muffler( "muffler" );
static const vpart_id vpart_plating_spiked( "plating_spiked" );
static const vpart_id vpart_rail_wheel( "rail_wheel" );
static const vpart_id vpart_seatbelt( "seatbelt" );
static const vpart_id vpart_spring_plate( "spring_plate" );
static const vpart_id vpart_test_veh_j06_shock_ram( "test_veh_j06_shock_ram" );
static const vpart_id vpart_test_veh_j07_typed_muffler( "test_veh_j07_typed_muffler" );
static const vpart_id vpart_wheel( "wheel" );
static const vpart_id vpart_wheel_mount_wood( "wheel_mount_wood" );
static const vpart_id vpart_wheel_wood( "wheel_wood" );

static const vproto_id vehicle_prototype_car( "car" );
static const vproto_id vehicle_prototype_electric_car( "electric_car" );
static const vproto_id vehicle_prototype_none( "none" );

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

vehicle &place( map &here, const vproto_id &proto, int fuel = 0 )
{
    vehicle *veh = here.add_vehicle( proto, veh_pos, 0_degrees, fuel, veh_spawn_status::UNDAMAGED );
    REQUIRE( veh != nullptr );
    return *veh;
}

// An empty vehicle with these parts at mount 0,0
vehicle &place_parts( map &here, const std::vector<vpart_id> &parts )
{
    vehicle &veh = place( here, vehicle_prototype_none );
    for( const vpart_id &id : parts ) {
        REQUIRE( veh.install_part( here, point_rel_ms::zero, id ) != -1 );
    }
    here.add_vehicle_to_cache( &veh );
    veh.refresh();
    return veh;
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

// Gives the part exactly these faults, whether its item could have them or not
void set_faults( vehicle_part &vp, const std::vector<fault_id> &faults )
{
    item base = vp.get_base();
    const std::set<fault_id> old = vp.faults();
    for( const fault_id &f : old ) {
        base.remove_fault( f );
    }
    for( const fault_id &f : faults ) {
        base.set_fault( f, true );
    }
    vp.set_base( std::move( base ) );
}

// The events sent while it lives, with whether their talkers were a vehicle and a monster
struct event_catcher : event_subscriber {
    struct caught {
        cata::event e;
        bool alpha_vehicle = false;
        bool beta_monster = false;
        bool has_beta = false;
    };
    std::vector<caught> events;

    event_catcher() {
        get_event_bus().subscribe( this );
    }
    using event_subscriber::notify;
    void notify( const cata::event &e ) override {
        events.push_back( { e, false, false, false } );
    }
    void notify( const cata::event &e, std::unique_ptr<talker> alpha,
                 std::unique_ptr<talker> beta ) override {
        caught c{ e, false, false, false };
        c.alpha_vehicle = alpha && alpha->get_vehicle() != nullptr;
        c.has_beta = beta != nullptr;
        c.beta_monster = beta && beta->get_monster() != nullptr;
        events.push_back( c );
    }
    std::vector<caught> of( const event_type type ) const {
        std::vector<caught> found;
        for( const caught &c : events ) {
            if( c.e.type() == type ) {
                found.push_back( c );
            }
        }
        return found;
    }
};

} // namespace

// VEH-B13: the engine's watts per kilogram were an int, so a vehicle under 1 W/kg (a loaded
// wagon behind an animal) did not move at all and a bicycle lost about a fifth of its push.
TEST_CASE( "veh-b13_acceleration_keeps_fractional_watts_per_kg", "[audit][vehicle]" )
{
    const int cmps = vmiph_to_cmps( 1000 );
    REQUIRE( cmps > 0 );
    // under 1 W/kg used to give 0: "too heavy for its engine" with a working engine
    CHECK( vehicle::accel_from_power_ratio( 0.5, cmps ) > 0 );
    // 1.9 W/kg used to count as 1
    CHECK( vehicle::accel_from_power_ratio( 1.9, cmps ) >
           vehicle::accel_from_power_ratio( 1.0, cmps ) );
    // only the result is truncated now: never less than the old sum, and at most the two
    // truncations it lost above it
    for( const int ratio : {
             1, 4, 30, 120
         } ) {
        CAPTURE( ratio );
        const int old_accel = cmps_to_vmiph( 100 * 100 * ratio / cmps );
        const int new_accel = vehicle::accel_from_power_ratio( ratio, cmps );
        CHECK( new_accel >= old_accel );
        CHECK( new_accel <= old_accel + 3 );
    }
}

// VEH-B14: noise_and_smoke() returned before the sound unless a combustion engine ran, so
// electric, muscle and animal vehicles were silent at any speed, and a coasting vehicle with its
// engines off never got there at all.
TEST_CASE( "veh-b14_vehicles_without_combustion_make_noise", "[audit][vehicle]" )
{
    const restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
    reset_world();
    const on_out_of_scope cleanup( []() {
        clear_vehicles();
        sounds::reset_sounds();
    } );
    map &here = get_map();

    SECTION( "coasting with the engine off" ) {
        vehicle &veh = place( here, vehicle_prototype_car );
        veh.engine_on = false;
        veh.velocity = 5000;
        veh.idle( here, true );
        // the speed noise: velocity / 500
        CHECK( static_cast<int>( veh.vehicle_noise ) == 10 );
        veh.velocity = 0;
        veh.idle( here, true );
        CHECK( static_cast<int>( veh.vehicle_noise ) == 0 );
    }
    SECTION( "an electric motor makes its noise_factor noise" ) {
        vehicle &veh = place( here, vehicle_prototype_electric_car, 100 );
        veh.engine_on = true;
        REQUIRE( veh.total_power( here ) > 0_W );
        for( const vpart_reference &vp : veh.get_all_parts() ) {
            if( vp.info().has_flag( VPFLAG_ENGINE ) ) {
                REQUIRE_FALSE( veh.is_engine_type_combustion( vp.part() ) );
            }
        }
        veh.noise_and_smoke( here, 0 );
        CHECK( static_cast<int>( veh.vehicle_noise ) > 0 );
    }
}

// VEH-J02: rail turning compared terrain ids against two hardcoded lists that missed the small and
// recessed tracks, and both branches bumped the straight-course counter, so a diagonal course
// allowed two straight tiles instead of one.
TEST_CASE( "veh-j02_rail_direction_comes_from_terrain_flags", "[audit][vehicle]" )
{
    CHECK( ter_t_railroad_track->has_flag( ter_furn_flag::TFLAG_RAIL_STRAIGHT ) );
    CHECK( ter_t_railroad_track_d->has_flag( ter_furn_flag::TFLAG_RAIL_DIAGONAL ) );
    CHECK( ter_t_railroad_track_small->has_flag( ter_furn_flag::TFLAG_RAIL_STRAIGHT ) );
    CHECK( ter_t_railroad_track_small_d->has_flag( ter_furn_flag::TFLAG_RAIL_DIAGONAL ) );
    CHECK( ter_t_railroad_track_recessed->has_flag( ter_furn_flag::TFLAG_RAIL_STRAIGHT ) );

    const restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
    reset_world();
    const on_out_of_scope cleanup( []() {
        clear_vehicles();
        clear_map();
    } );
    map &here = get_map();
    vehicle &veh = place_parts( here, { vpart_frame, vpart_rail_wheel } );
    veh.velocity = 1000;
    const tripoint_bub_ms start = veh.pos_bub( here );

    // Lays the three tiles ahead of the wheel on a course, and counts the wheels that land on rail
    const auto wheels_on_rail = [&]( const units::angle dir, const std::array<ter_str_id, 3> &ters ) {
        tileray ray;
        ray.init( dir );
        ray.advance( 1 );
        const tripoint_rel_ms step( ray.dx(), ray.dy(), 0 );
        tripoint_bub_ms p = start;
        for( const ter_str_id &ter : ters ) {
            p += step;
            here.ter_set( p, ter.id() );
        }
        int on_rail = 0;
        int one_axis = 0;
        veh.precalculate_vehicle_turning( here, dir, true, ter_furn_flag::TFLAG_RAIL, on_rail, one_axis );
        return on_rail;
    };

    const ter_str_id &straight = ter_t_railroad_track;
    const ter_str_id &diagonal = ter_t_railroad_track_d;
    const ter_str_id &small_track = ter_t_railroad_track_small;
    const ter_str_id &small_track_d = ter_t_railroad_track_small_d;
    SECTION( "straight course" ) {
        CHECK( wheels_on_rail( 0_degrees, { small_track, small_track, small_track } ) == 1 );
        // the small diagonal track was in neither list, so it counted as straight
        CHECK( wheels_on_rail( 0_degrees, { small_track_d, small_track_d, small_track_d } ) == 0 );
    }
    SECTION( "diagonal course" ) {
        CHECK( wheels_on_rail( 45_degrees, { straight, diagonal, diagonal } ) == 1 );
        // two straight tiles are one too many on a diagonal course
        CHECK( wheels_on_rail( 45_degrees, { straight, straight, diagonal } ) == 0 );
    }
}

// VEH-J04: engine faults had only on/off flags with fixed multipliers, and steering checked three
// tyre fault ids. Faults now carry the numbers, the legacy flags standing for the old ones.
TEST_CASE( "veh-j04_engine_and_steering_faults_are_numeric", "[audit][vehicle]" )
{
    CHECK( fault_engine_filter_fuel->engine_power_mult() == Approx( 0.6 ) );
    CHECK( fault_engine_filter_air->fuel_consumption_mult() == Approx( 2.0 ) );
    CHECK( fault_engine_starter->engine_power_mult() == 1.0 );
    CHECK( fault_engine_starter->fuel_consumption_mult() == 1.0 );
    // the steering the code used to hardcode for these ids
    CHECK( fault_punctured_tires->steering_mult() == 0.5f );
    CHECK( fault_flat_tire_riding_on_rims->steering_mult() == 0.1f );
    CHECK( fault_tire_treads->steering_mult() == 0.9f );
    CHECK( test_veh_j04_low_oil->engine_power_mult() == Approx( 0.5 ) );
    CHECK( test_veh_j04_low_oil->fuel_consumption_mult() == Approx( 1.5 ) );
    CHECK( test_veh_j04_low_oil->exhaust_mult() == Approx( 2.0 ) );

    const restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
    reset_world();
    const on_out_of_scope cleanup( []() {
        clear_vehicles();
    } );
    map &here = get_map();
    vehicle &car = place( here, vehicle_prototype_car, 100 );

    SECTION( "engine power and fuel" ) {
        std::optional<int> engine;
        for( int idx = 0; idx < car.part_count(); idx++ ) {
            if( car.part( idx ).info().has_flag( VPFLAG_ENGINE ) && !car.part( idx ).removed ) {
                engine = idx;
                break;
            }
        }
        REQUIRE( engine );
        vehicle_part &vp = car.part( *engine );
        set_faults( vp, {} );
        const units::power power_before = car.total_power( here, false );
        const units::power usage_before = car.engine_fuel_usage( vp );
        REQUIRE( power_before > 0_W );
        REQUIRE( usage_before > 0_W );
        set_faults( vp, { test_veh_j04_low_oil } );
        CHECK( car.total_power( here, false ) < power_before );
        units::power expected_usage = usage_before;
        expected_usage *= 1.5;
        CHECK( car.engine_fuel_usage( vp ) == expected_usage );
        CHECK( vp.fault_exhaust_mult() == Approx( 2.0 ) );
    }
    SECTION( "faults of a steering part and of the wheel on it steer worse" ) {
        // the car steers with its steerable wheel mounts, each under a wheel
        std::vector<int> steering;
        for( int idx = 0; idx < car.part_count(); idx++ ) {
            const vehicle_part &vp = car.part( idx );
            if( !vp.removed && !vp.is_fake && vp.info().has_flag( "STEERABLE" ) ) {
                steering.push_back( idx );
            }
        }
        REQUIRE_FALSE( steering.empty() );
        for( int idx = 0; idx < car.part_count(); idx++ ) {
            if( !car.part( idx ).removed && !car.part( idx ).is_fake ) {
                set_faults( car.part( idx ), {} );
            }
        }
        const float share = 1.0f / static_cast<float>( steering.size() );
        REQUIRE( car.steering_effectiveness( here ) == Approx( 1.0f ) );
        // a modded fault on the steering part itself
        set_faults( car.part( steering[0] ), { test_veh_j04_bent_tie_rod } );
        CHECK( car.steering_effectiveness( here ) == Approx( 1.0f - 0.5f * share ) );
        set_faults( car.part( steering[0] ), {} );

        // a punctured tyre on a steering mount: the old check looked for it on the mount
        std::optional<int> wheel;
        for( const int idx : car.parts_at_relative( car.part( steering[0] ).mount, true ) ) {
            if( idx != steering[0] && car.part( idx ).info().has_flag( VPFLAG_WHEEL ) ) {
                wheel = idx;
            }
        }
        if( wheel ) {
            set_faults( car.part( *wheel ), { fault_punctured_tires } );
            CHECK( car.steering_effectiveness( here ) == Approx( 1.0f - 0.5f * share ) );
        } else {
            // the steering part is the wheel itself
            REQUIRE( car.part( steering[0] ).info().has_flag( VPFLAG_WHEEL ) );
        }
    }
}

// VEH-J05: running over sharp things picked a C++ chain by fault id, and driving on a flat
// hardcoded its next step and sounds. The chain is fault data now (runover, while_moving).
TEST_CASE( "veh-j05_wheel_faults_follow_the_data_chain", "[audit][vehicle]" )
{
    const restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
    reset_world();
    const on_out_of_scope cleanup( []() {
        clear_vehicles();
        sounds::reset_sounds();
    } );
    map &here = get_map();

    SECTION( "a tyre: punctured, then down to the rims, then nothing more" ) {
        vehicle &car = place( here, vehicle_prototype_car );
        const std::vector<int> wheels = parts_of( car, vpart_wheel );
        REQUIRE_FALSE( wheels.empty() );
        vehicle_part &wheel = car.part( wheels[0] );
        set_faults( wheel, {} );
        std::vector<std::string> messages;
        REQUIRE( vehicle::apply_runover_faults( wheel, &messages ) );
        CHECK( wheel.has_fault( fault_punctured_tires ) );
        CHECK( messages.size() == 1 );
        REQUIRE( vehicle::apply_runover_faults( wheel, &messages ) );
        CHECK( wheel.has_fault( fault_flat_tire_riding_on_rims ) );
        CHECK_FALSE( wheel.has_fault( fault_punctured_tires ) );
        CHECK( messages.size() == 2 );
        CHECK_FALSE( vehicle::apply_runover_faults( wheel, &messages ) );
        CHECK( wheel.has_fault( fault_flat_tire_riding_on_rims ) );
        CHECK( messages.size() == 2 );
    }
    SECTION( "a cart wheel cracks or loses its axle, and a cracked one loses its axle" ) {
        vehicle &cart = place_parts( here, { vpart_frame, vpart_wheel_mount_wood, vpart_wheel_wood } );
        const std::vector<int> wheels = parts_of( cart, vpart_wheel_wood );
        REQUIRE( wheels.size() == 1 );
        vehicle_part &wheel = cart.part( wheels[0] );
        set_faults( wheel, {} );
        REQUIRE( vehicle::apply_runover_faults( wheel, nullptr ) );
        CHECK( wheel.has_fault( fault_cracked_rim ) != wheel.has_fault( fault_axle_broken ) );
        set_faults( wheel, { fault_cracked_rim } );
        REQUIRE( vehicle::apply_runover_faults( wheel, nullptr ) );
        CHECK( wheel.has_fault( fault_axle_broken ) );
        CHECK_FALSE( vehicle::apply_runover_faults( wheel, nullptr ) );
    }
    SECTION( "driving on a puncture: the hiss, then the rims" ) {
        const fault_while_moving *hiss = fault_punctured_tires->while_moving();
        REQUIRE( hiss != nullptr );
        CHECK( hiss->escalates_to == fault_flat_tire_riding_on_rims );
        CHECK( hiss->sound_volume == 5 );
        CHECK_FALSE( hiss->damages_part );
        const fault_while_moving *grind = fault_flat_tire_riding_on_rims->while_moving();
        REQUIRE( grind != nullptr );
        CHECK( grind->escalates_to.is_null() );
        CHECK( grind->damages_part );
        CHECK( grind->sound_volume == 40 );

        vehicle &car = place( here, vehicle_prototype_car );
        const std::vector<int> wheels = parts_of( car, vpart_wheel );
        REQUIRE_FALSE( wheels.empty() );
        for( const int idx : wheels ) {
            set_faults( car.part( idx ), {} );
        }
        set_faults( car.part( wheels[0] ), { fault_punctured_tires } );
        car.velocity = 1000;
        // a one in (durability) chance a turn: a bound far above it, with a fixed seed
        rng_set_engine_seed( 2323 );
        bool rims = false;
        for( int turn = 0; turn < 100000 && !rims; turn++ ) {
            car.check_flats_do_rim_damage_or_sounds( here );
            rims = car.part( wheels[0] ).has_fault( fault_flat_tire_riding_on_rims );
        }
        CHECK( rims );
        CHECK_FALSE( car.part( wheels[0] ).has_fault( fault_punctured_tires ) );
    }
}

// VEH-J05: a trap's vehicle_data could damage a wheel but not give it a fault or run an EOC.
TEST_CASE( "veh-j05_traps_apply_wheel_faults_and_eocs", "[audit][vehicle][eoc]" )
{
    const restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
    reset_world();
    const on_out_of_scope cleanup( []() {
        clear_vehicles();
        clear_map();
        get_globals().clear_global_values();
    } );
    map &here = get_map();
    vehicle &car = place( here, vehicle_prototype_car );
    const std::vector<int> wheels = parts_of( car, vpart_wheel );
    REQUIRE_FALSE( wheels.empty() );
    vehicle_part &wheel = car.part( wheels[0] );
    set_faults( wheel, {} );
    const tripoint_bub_ms pos = car.bub_part_pos( here, wheel );
    here.trap_set( pos, tr_test_veh_j05_spikes.id() );
    REQUIRE( here.tr_at( pos ).id == tr_test_veh_j05_spikes );

    car.handle_trap( &here, pos, wheel );
    // the EOCs run once the vehicles have moved
    CHECK( get_globals().maybe_get_global_value( "test_veh_j05_trap_hits" ) == nullptr );
    car.process_queued_events();

    // runover_faults: the first step of the tyre's chain; wheel_faults: worn treads
    CHECK( wheel.has_fault( fault_punctured_tires ) );
    CHECK( wheel.has_fault( fault_tire_treads ) );
    const global_variables &globals = get_globals();
    CHECK( globals.get_global_value( "test_veh_j05_trap_hits" ) == 1 );
    CHECK( globals.get_global_value( "test_veh_j05_trap_id" ).str() == tr_test_veh_j05_spikes.str() );
    CHECK( globals.get_global_value( "test_veh_j05_vpart_id" ).str() == vpart_wheel.str() );
}

// VEH-J06: a monster rammed took bash or cut damage to its torso through apply_damage, after a
// manual armor sum, so immunities and ondamage_eocs did not apply; characters always took bash.
// The part's collision_damage is dealt through deal_damage now. VEH-J03: the collision is an event.
TEST_CASE( "veh-j06_ram_damage_goes_through_deal_damage", "[audit][vehicle][event]" )
{
    CHECK( vpart_frame->get_collision_damage().type_damage( damage_bash ) == Approx( 1.0f ) );
    // SHARP parts (blades, spiked plating; the spiked ram isn't SHARP) cut by default
    CHECK( vpart_plating_spiked->get_collision_damage().type_damage( damage_cut ) == Approx( 1.0f ) );
    CHECK( vpart_test_veh_j06_shock_ram->get_collision_damage().type_damage( damage_electric ) ==
           Approx( 1.0f ) );

    const restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
    reset_world();
    const on_out_of_scope cleanup( []() {
        clear_vehicles();
        clear_creatures();
    } );
    map &here = get_map();
    vehicle &car = place( here, vehicle_prototype_car );
    // a ram is a structure of its own: on the tile in front of the car, so the car's whole mass
    // is behind it
    int front = 0;
    for( int p = 0; p < car.part_count(); p++ ) {
        front = std::max( front, car.part( p ).mount.x() );
    }
    const int ram = car.install_part( here, point_rel_ms( front + 1, 0 ),
                                      vpart_test_veh_j06_shock_ram );
    REQUIRE( ram != -1 );
    car.refresh();
    REQUIRE( car.part_with_feature( ram, VPFLAG_ARMOR, true ) == -1 );

    // clear of the car, which is about five tiles long
    const tripoint_bub_ms target_pos = veh_pos + tripoint_rel_ms( 10, 0, 0 );
    monster &zed = spawn_test_monster( "mon_zombie", target_pos );
    const int hp_before = zed.get_hp();
    car.velocity = 2000;

    rng_set_engine_seed( 2323 );
    event_catcher catcher;
    const veh_collision coll = car.part_collision( here, ram, here.get_abs( target_pos ), false,
                               false );
    CHECK( coll.type == veh_coll_body );
    CHECK( zed.get_hp() < hp_before );
    // the collision event waits until the vehicles have moved (idle() sends it)
    CHECK( catcher.of( event_type::vehicle_collides ).empty() );
    car.process_queued_events();

    // deal_damage reported the configured type
    const std::vector<event_catcher::caught> hurt = catcher.of( event_type::monster_takes_damage );
    REQUIRE_FALSE( hurt.empty() );
    CHECK( hurt[0].e.get<damage_type_id>( "damage_type" ) == damage_electric );

    const std::vector<event_catcher::caught> collides = catcher.of( event_type::vehicle_collides );
    REQUIRE( collides.size() == 1 );
    CHECK( collides[0].alpha_vehicle );
    CHECK( collides[0].beta_monster );
    CHECK( collides[0].e.get<std::string>( "collision_type" ) == "body" );
    CHECK( collides[0].e.get<std::string>( "vpart_id" ) == vpart_test_veh_j06_shock_ram.str() );
    CHECK( collides[0].e.get<int>( "velocity_delta" ) > 0 );
}

// VEH-J03: no event for part destruction or engine stalls; fuel_tank_explodes had no talker.
TEST_CASE( "veh-j03_part_destroyed_and_engine_stall_events", "[audit][vehicle][event]" )
{
    const cata::event::field_list_type tank_fields =
        cata::event::get_field_list( event_type::fuel_tank_explodes );
    REQUIRE( tank_fields.size() == 2 );
    CHECK( tank_fields[1].first == "fuel" );

    const restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
    reset_world();
    const on_out_of_scope cleanup( []() {
        clear_vehicles();
        sounds::reset_sounds();
    } );
    map &here = get_map();
    vehicle &car = place( here, vehicle_prototype_car, 100 );

    SECTION( "a part broken by damage" ) {
        const std::vector<int> wheels = parts_of( car, vpart_wheel );
        REQUIRE_FALSE( wheels.empty() );
        event_catcher catcher;
        // more than its hit points, not so much that mod_hp() overflows scaling it
        car.damage_direct( here, car.part( wheels[0] ), car.part( wheels[0] ).info().durability * 2 );
        REQUIRE( car.part( wheels[0] ).is_broken() );
        // sent once the vehicle is done with its parts, at its next idle()
        CHECK( catcher.of( event_type::vehicle_part_destroyed ).empty() );
        car.idle( here, true );
        const std::vector<event_catcher::caught> destroyed =
            catcher.of( event_type::vehicle_part_destroyed );
        REQUIRE( destroyed.size() == 1 );
        CHECK( destroyed[0].alpha_vehicle );
        CHECK_FALSE( destroyed[0].has_beta );
        CHECK( destroyed[0].e.get<std::string>( "vpart_id" ) == vpart_wheel.str() );
        CHECK( destroyed[0].e.get<damage_type_id>( "damage_type" ) == damage_pure );
    }
    SECTION( "the engine runs out of fuel" ) {
        // the tanks, not the battery, which would stop the ignition instead
        for( const vpart_reference &vp : car.get_all_parts() ) {
            if( vp.part().is_tank() ) {
                vp.part().ammo_unset();
            }
        }
        car.engine_on = true;
        event_catcher catcher;
        car.idle( here, true );
        CHECK_FALSE( car.engine_on );
        const std::vector<event_catcher::caught> stalls =
            catcher.of( event_type::vehicle_engine_stalls );
        REQUIRE( stalls.size() == 1 );
        CHECK( stalls[0].alpha_vehicle );
        CHECK( stalls[0].e.get<std::string>( "reason" ) == "no_power" );
    }
}

// VEH-J07: one untyped bonus meant something else for each part, and the docs had the funnel,
// muffler and seatbelt wrong. Those (and the shock absorber) have typed fields now, falling back
// on bonus, so shipped parts keep their values.
TEST_CASE( "veh-j07_typed_bonus_fields", "[audit][vehicle]" )
{
    // vanilla, from bonus
    CHECK( vpart_muffler->get_muffle_factor() == 40 / 100.0 );
    CHECK( vpart_funnel->get_funnel_radius_mm() == vpart_funnel->bonus );
    CHECK( vpart_seatbelt->get_seatbelt_strength() == vpart_seatbelt->bonus );
    CHECK( vpart_spring_plate->get_shock_absorption() == vpart_spring_plate->bonus );
    // typed
    const vpart_info &typed = vpart_test_veh_j07_typed_muffler.obj();
    CHECK( typed.get_muffle_factor() == Approx( 0.25 ) );
    CHECK( typed.get_funnel_radius_mm() == 150 );
    CHECK( typed.get_seatbelt_strength() == 7 );
    CHECK( typed.get_shock_absorption() == 12 );

    const restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
    reset_world();
    const on_out_of_scope cleanup( []() {
        clear_vehicles();
    } );
    map &here = get_map();
    // the quietest muffler counts, at full health its muffle_factor
    vehicle &veh = place_parts( here, { vpart_frame, vpart_test_veh_j07_typed_muffler } );
    const std::vector<int> mufflers = parts_of( veh, vpart_test_veh_j07_typed_muffler );
    REQUIRE( mufflers.size() == 1 );
    const std::pair<int, double> exhaust = veh.get_exhaust_part();
    CHECK( exhaust.first == mufflers[0] );
    CHECK( exhaust.second == Approx( 0.25 ) );
}
