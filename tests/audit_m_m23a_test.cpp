#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "coordinates.h"
#include "debug.h"
#include "effect_source.h"
#include "enums.h"
#include "global_vars.h"
#include "item.h"
#include "map.h"
#include "map_helpers.h"
#include "math_parser_diag_value.h"
#include "player_helpers.h"
#include "point.h"
#include "rng.h"
#include "type_id.h"
#include "units.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vpart_position.h"
#include "vpart_range.h"

static const damage_type_id damage_pure( "pure" );

static const efftype_id effect_grabbed( "grabbed" );

static const fault_id fault_engine_immobiliser( "fault_engine_immobiliser" );

static const itype_id itype_gasoline( "gasoline" );

static const proficiency_id proficiency_prof_driver( "prof_driver" );

static const vpart_id vpart_ap_portable_generator( "ap_portable_generator" );
static const vpart_id vpart_controls( "controls" );
static const vpart_id vpart_frame( "frame" );
static const vpart_id vpart_hd_tow_cable( "hd_tow_cable" );
static const vpart_id vpart_rebar_plate( "rebar_plate" );
static const vpart_id vpart_roller_drum( "roller_drum" );
static const vpart_id vpart_seat( "seat" );
static const vpart_id vpart_tank( "tank" );

static const vproto_id vehicle_prototype_beetle( "beetle" );
static const vproto_id vehicle_prototype_car( "car" );
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

// A row of frames at mounts 0,0 to (count - 1),0, its origin at pos.
vehicle &place_frames( map &here, const tripoint_bub_ms &pos, int count )
{
    vehicle *veh = here.add_vehicle( vehicle_prototype_none, pos, 0_degrees, 0,
                                     veh_spawn_status::UNDAMAGED );
    REQUIRE( veh != nullptr );
    for( int x = 0; x < count; x++ ) {
        REQUIRE( veh->install_part( here, point_rel_ms( x, 0 ), vpart_frame ) != -1 );
    }
    here.add_vehicle_to_cache( veh );
    return *veh;
}

vehicle &place_car( map &here, int fuel )
{
    vehicle *veh = here.add_vehicle( vehicle_prototype_car, veh_pos, 0_degrees, fuel,
                                     veh_spawn_status::UNDAMAGED );
    REQUIRE( veh != nullptr );
    return *veh;
}

// The move cost a wheel used to get: the entries tried in turn, the first that applied winning.
int first_applicable_move_cost( const vpslot_wheel &wheel, const std::set<std::string> &flags,
                                int move_cost )
{
    for( const veh_ter_mod &mod : wheel.terrain_modifiers ) {
        const bool ter_has_flag = flags.count( mod.terrain_flag ) > 0;
        if( ter_has_flag && mod.move_override ) {
            return mod.move_override;
        } else if( !ter_has_flag && mod.move_penalty ) {
            return move_cost + mod.move_penalty;
        }
    }
    return move_cost;
}

vpslot_wheel wheel_with( const std::vector<veh_ter_mod> &mods )
{
    vpslot_wheel wheel;
    wheel.terrain_modifiers = mods;
    return wheel;
}

// The vehicle's fuel tank, emptied, with its debt from earlier turns cleared.
vehicle_part &empty_tank( vehicle &veh )
{
    for( const vpart_reference &vpr : veh.get_all_parts() ) {
        vehicle_part &vp = vpr.part();
        if( vp.info().id == vpart_tank ) {
            vp.ammo_unset();
            veh.fuel_remainder.clear();
            return vp;
        }
    }
    FAIL( "the vehicle has no tank" );
    return veh.part( 0 );
}

} // namespace

// VEH-B03: the terrain modifiers stopped at the first entry that applied, in sorted key order,
// so the road roller's drum ({ "FLAT": [0,6], "ROAD": [0,10] }) did better off-road than on
// dirt. Overrides now beat penalties, the largest penalty applies (they don't stack, as the
// shipped wheels were tuned) and the order of the entries doesn't matter.
TEST_CASE( "veh-b03_wheel_terrain_modifiers_ignore_entry_order", "[audit][vehicle]" )
{
    const std::set<std::string> rough;
    const std::set<std::string> dirt{ "FLAT" };
    const std::set<std::string> road{ "FLAT", "ROAD" };
    const std::set<std::string> rails{ "RAIL" };
    constexpr int base = 2;

    SECTION( "a standard wheel, in either order" ) {
        for( const vpslot_wheel &wheel : {
        wheel_with( { { "FLAT", 0, 4 }, { "ROAD", 0, 2 } } ),
    wheel_with( { { "ROAD", 0, 2 }, { "FLAT", 0, 4 } } )
         } ) {
            CHECK( wheel.terrain_move_cost( rough, base ) == base + 4 );
            CHECK( wheel.terrain_move_cost( dirt, base ) == base + 2 );
            CHECK( wheel.terrain_move_cost( road, base ) == base );
        }
    }
    SECTION( "a bigger ROAD penalty: rough ground is no better than dirt" ) {
        const vpslot_wheel drum = wheel_with( { { "FLAT", 0, 6 }, { "ROAD", 0, 10 } } );
        CHECK( drum.terrain_move_cost( rough, base ) == base + 10 );
        CHECK( drum.terrain_move_cost( dirt, base ) == base + 10 );
        CHECK( drum.terrain_move_cost( road, base ) == base );
    }
    SECTION( "an override beats a penalty listed before it" ) {
        const vpslot_wheel wheel = wheel_with( { { "FLAT", 0, 4 }, { "RAIL", 2, 8 } } );
        CHECK( wheel.terrain_move_cost( rails, 5 ) == 2 );
        CHECK( wheel.terrain_move_cost( dirt, base ) == base + 8 );
        CHECK( wheel.terrain_move_cost( rough, base ) == base + 8 );
    }
    SECTION( "only the road roller's drum changes among the loaded wheels" ) {
        int wheels = 0;
        for( const vpart_info &vpi : vehicles::parts::get_all() ) {
            if( !vpi.wheel_info ) {
                continue;
            }
            ++wheels;
            std::vector<std::string> flags;
            for( const veh_ter_mod &mod : vpi.wheel_info->terrain_modifiers ) {
                flags.push_back( mod.terrain_flag );
            }
            REQUIRE( flags.size() < 8 );
            const unsigned int combos = 1U << flags.size();
            for( unsigned int combo = 0; combo < combos; ++combo ) {
                std::set<std::string> on_tile;
                for( size_t i = 0; i < flags.size(); ++i ) {
                    if( combo & ( 1U << i ) ) {
                        on_tile.insert( flags[i] );
                    }
                }
                const int before = first_applicable_move_cost( *vpi.wheel_info, on_tile, base );
                const int now = vpi.wheel_info->terrain_move_cost( on_tile, base );
                if( before != now ) {
                    CAPTURE( vpi.id.str(), combo );
                    CHECK( vpi.id == vpart_roller_drum );
                }
            }
        }
        CHECK( wheels > 10 );
        const vpart_info &drum = vpart_roller_drum.obj();
        REQUIRE( drum.wheel_info );
        CHECK( drum.wheel_info->terrain_move_cost( rough, base ) >=
               drum.wheel_info->terrain_move_cost( dirt, base ) );
    }
}

// VEH-B04: a gasoline generator (a REACTOR with a tank) was filled with plutonium cells at spawn,
// which it can't hold, so it came empty; and a fueled reactor without "power" loaded cleanly,
// then divided by zero.
TEST_CASE( "veh-b04_reactor_spawns_with_its_own_fuel_and_needs_power", "[audit][vehicle]" )
{
    const restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
    reset_world();
    const on_out_of_scope cleanup( []() {
        clear_vehicles();
    } );
    map &here = get_map();

    SECTION( "spawned full of its own fuel" ) {
        vehicle *veh = here.add_vehicle( vehicle_prototype_none, veh_pos, 0_degrees, 0,
                                         veh_spawn_status::UNDAMAGED );
        REQUIRE( veh != nullptr );
        const int gen_idx = veh->install_part( here, point_rel_ms::zero, vpart_ap_portable_generator );
        REQUIRE( gen_idx != -1 );
        here.add_vehicle_to_cache( veh );
        veh->init_state( here, 100, veh_spawn_status::UNDAMAGED );
        const vehicle_part &gen = veh->part( gen_idx );
        CHECK( gen.ammo_current() == itype_gasoline );
        CHECK( gen.ammo_remaining() > 0 );
    }
    SECTION( "a fueled reactor needs a positive power" ) {
        vpart_info generator = vpart_ap_portable_generator.obj();
        REQUIRE( generator.has_flag( "REACTOR" ) );
        REQUIRE_FALSE( generator.has_flag( "PERPETUAL" ) );
        const std::string clean = capture_debugmsg_during( [&generator]() {
            generator.check();
        } );
        CHECK( clean.find( "reactor part" ) == std::string::npos );
        generator.power = 0_W;
        const std::string reported = capture_debugmsg_during( [&generator]() {
            generator.check();
        } );
        CHECK( reported.find( "reactor part ap_portable_generator needs a positive" ) !=
               std::string::npos );
    }
}

// VEH-B05: towing with no acceleration (engine off), braking returned at "struggles to pull"
// before the brakes were applied.
TEST_CASE( "veh-b05_towing_vehicle_can_brake_with_the_engine_off", "[audit][vehicle]" )
{
    const restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
    reset_world();
    map &here = get_map();
    vehicle &car = place_car( here, 0 );
    vehicle &towed = place_frames( here, veh_pos + tripoint_rel_ms( 0, 8, 0 ), 1 );
    REQUIRE( car.install_part( here, point_rel_ms::zero, vpart_hd_tow_cable ) != -1 );
    REQUIRE( towed.install_part( here, point_rel_ms::zero, vpart_hd_tow_cable ) != -1 );
    REQUIRE( car.tow_data.set_towing( &car, &towed ) );
    const on_out_of_scope cleanup( [&car, &towed]() {
        car.tow_data.clear_towing();
        towed.tow_data.clear_towing();
        clear_vehicles();
    } );
    REQUIRE( car.is_towing() );

    car.engine_on = false;
    car.cruise_velocity = 0;
    car.velocity = 1000;
    REQUIRE( car.current_acceleration( here ) == 0 );
    car.thrust( here, -1 );
    // 20 mph/s of brakes stop a 10 mph car
    CHECK( car.velocity == 0 );
}

// VEH-B06: when the random target part was the armor itself, it was damaged twice, once as
// armor and once as the part behind it.
TEST_CASE( "veh-b06_armor_takes_one_hit_when_it_is_the_target", "[audit][vehicle]" )
{
    const restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
    reset_world();
    const on_out_of_scope cleanup( []() {
        clear_vehicles();
    } );
    map &here = get_map();
    vehicle &veh = place_frames( here, veh_pos, 1 );
    const int armor_idx = veh.install_part( here, point_rel_ms::zero, vpart_rebar_plate );
    REQUIRE( armor_idx != -1 );
    const int frame_idx = 0;
    REQUIRE( veh.part( frame_idx ).info().id == vpart_frame );
    vehicle_part &armor = veh.part( armor_idx );
    vehicle_part &frame = veh.part( frame_idx );
    // set_hp sets the damage before the degradation, and damage can't go below degradation, so
    // a first call clears the wear of the last hit and a second one the damage
    const auto mend = [&]() {
        for( int pass = 0; pass < 2; ++pass ) {
            veh.set_hp( armor, armor.info().durability, false, 0 );
            veh.set_hp( frame, frame.info().durability, false, 0 );
        }
    };
    constexpr int dmg = 10;

    mend();
    veh.damage_direct( here, armor, dmg, damage_pure );
    const int one_hit = armor.damage();
    REQUIRE( one_hit > 0 );

    // Frame or armor is the target at random; either way the armor takes one hit.
    rng_set_engine_seed( 2323 );
    int armor_alone = 0;
    for( int i = 0; i < 40; ++i ) {
        mend();
        veh.damage( here, frame_idx, dmg, damage_pure, true );
        CAPTURE( i );
        CHECK( armor.damage() == one_hit );
        if( frame.damage() == 0 ) {
            ++armor_alone;
        }
    }
    CHECK( armor_alone > 0 );
}

// VEH-B07: the merge size check took both grids' min corners for the max corner, so a grid
// grew without limit up and to the left.
TEST_CASE( "veh-b07_grid_merge_size_cap_applies_both_ways", "[audit][vehicle]" )
{
    const restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
    reset_world();
    const on_out_of_scope cleanup( []() {
        clear_vehicles();
    } );
    map &here = get_map();

    SECTION( "a 24-wide grid takes nothing more on the left" ) {
        vehicle &grid = place_frames( here, veh_pos, 24 );
        vehicle &left = place_frames( here, veh_pos + tripoint_rel_ms::west, 1 );
        CHECK_FALSE( grid.merge_appliance_into_grid( &here, left ) );
        CHECK( here.get_vehicles().size() == 2 );
    }
    SECTION( "nor on the right" ) {
        vehicle &grid = place_frames( here, veh_pos, 24 );
        vehicle &right = place_frames( here, veh_pos + tripoint_rel_ms( 24, 0, 0 ), 1 );
        CHECK_FALSE( grid.merge_appliance_into_grid( &here, right ) );
        CHECK( here.get_vehicles().size() == 2 );
    }
    SECTION( "a 23-wide grid still takes one on the left" ) {
        vehicle &grid = place_frames( here, veh_pos, 23 );
        vehicle &left = place_frames( here, veh_pos + tripoint_rel_ms::west, 1 );
        CHECK( grid.merge_appliance_into_grid( &here, left ) );
        CHECK( here.get_vehicles().size() == 1 );
    }
}

// VEH-B08: the immobilizer fault was set on the alarm part, which can't take it, so locked
// vehicles never had one. Half of them now get it on their engines.
TEST_CASE( "veh-b08_locked_vehicles_can_spawn_immobilized", "[audit][vehicle]" )
{
    const restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
    reset_world();
    const on_out_of_scope cleanup( []() {
        clear_vehicles();
    } );
    map &here = get_map();
    rng_set_engine_seed( 2323 );
    // 1 in 3 is locked, half of those immobilized: 100 spawns all miss with probability 1e-8
    bool immobilized_seen = false;
    for( int i = 0; i < 100 && !immobilized_seen; ++i ) {
        vehicle *veh = here.add_vehicle( vehicle_prototype_beetle, veh_pos, 0_degrees, -1,
                                         veh_spawn_status::UNDAMAGED );
        REQUIRE( veh != nullptr );
        REQUIRE_FALSE( veh->engines.empty() );
        int immobilized = 0;
        for( const int e : veh->engines ) {
            if( veh->part( e ).has_fault( fault_engine_immobiliser ) ) {
                ++immobilized;
            }
        }
        CAPTURE( i, veh->is_locked );
        if( !veh->is_locked ) {
            CHECK( immobilized == 0 );
        } else if( immobilized > 0 ) {
            CHECK( immobilized == static_cast<int>( veh->engines.size() ) );
            immobilized_seen = true;
        }
        here.destroy_vehicle( veh );
    }
    CHECK( immobilized_seen );
}

// VEH-B09: a summoned vehicle's timer ran down once per tile moved, and a piece split off it
// became a permanent vehicle.
TEST_CASE( "veh-b09_summoned_vehicle_timer_and_split_pieces", "[audit][vehicle]" )
{
    const restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
    reset_world();
    const on_out_of_scope cleanup( []() {
        clear_vehicles();
    } );
    map &here = get_map();

    SECTION( "the timer runs down once per turn" ) {
        vehicle &veh = place_frames( here, veh_pos, 1 );
        veh.magic = true;
        veh.summon_time_limit = 10_turns;
        // act_on_map() runs once for every tile moved
        for( int i = 0; i < 3; ++i ) {
            REQUIRE( veh.act_on_map( here ) == &veh );
        }
        CHECK( *veh.summon_time_limit == 10_turns );
        here.vehmove();
        REQUIRE( here.get_vehicles().size() == 1 );
        CHECK( *veh.summon_time_limit == 9_turns );
        veh.summon_time_limit = 0_turns;
        here.vehmove();
        CHECK( here.get_vehicles().empty() );
    }
    SECTION( "a split-off piece is still summoned" ) {
        vehicle &veh = place_frames( here, veh_pos, 3 );
        veh.magic = true;
        veh.summon_time_limit = 50_turns;
        veh.is_locked = true;
        veh.set_value( "m23a_mark", diag_value( 7.0 ) );
        veh.add_effect( effect_source::empty(), effect_grabbed, 10_turns );
        REQUIRE( veh.has_effect( effect_grabbed ) );

        const std::vector<std::vector<int>> far_frame( 1, std::vector<int> { 2 } );
        const std::vector<vehicle *> new_vehicles( 1, nullptr );
        const std::vector<std::vector<point_rel_ms>> mounts( 1 );
        std::vector<vehicle *> added;
        REQUIRE( veh.split_vehicles( here, far_frame, new_vehicles, mounts, &added ) );
        veh.part_removal_cleanup( here );
        REQUIRE( added.size() == 1 );
        const vehicle &piece = *added.front();
        CHECK( piece.magic );
        REQUIRE( piece.summon_time_limit.has_value() );
        CHECK( *piece.summon_time_limit == 50_turns );
        CHECK( piece.is_locked );
        CHECK( piece.has_effect( effect_grabbed ) );
        const diag_value *mark = piece.maybe_get_value( "m23a_mark" );
        REQUIRE( mark != nullptr );
        CHECK( mark->dbl() == Approx( 7.0 ) );
    }
}

// VEH-B10: handling difficulty scaled with signed velocity, so it was negative in reverse:
// free fast steering and instant skid recovery.
TEST_CASE( "veh-b10_reversing_is_as_hard_to_handle_as_driving_forward", "[audit][vehicle]" )
{
    const restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
    reset_world();
    const on_out_of_scope cleanup( []() {
        clear_vehicles();
    } );
    map &here = get_map();
    vehicle &car = place_car( here, 0 );
    // facing one way, heading another: misaligned, so handling is hard at any speed
    car.turn_dir = car.face.dir() + 90_degrees;
    car.velocity = 1000;
    const float forward = car.handling_difficulty( here );
    car.velocity = -1000;
    const float reverse = car.handling_difficulty( here );
    car.velocity = 0;
    car.turn_dir = car.face.dir();
    CHECK( forward > 0.0f );
    CHECK( reverse == Approx( forward ) );
}

// VEH-B11: a vehicle moving with nobody at the controls (autopilot, following) trained the
// player's driving proficiency wherever they were.
TEST_CASE( "veh-b11_driverless_vehicle_trains_nobody", "[audit][vehicle]" )
{
    const restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
    reset_world();
    avatar &you = get_avatar();
    map &here = get_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_none, veh_pos, 0_degrees, 0,
                                     veh_spawn_status::UNDAMAGED );
    REQUIRE( veh != nullptr );
    REQUIRE( veh->install_part( here, point_rel_ms::zero, vpart_frame ) != -1 );
    REQUIRE( veh->install_part( here, point_rel_ms::zero, vpart_seat ) != -1 );
    REQUIRE( veh->install_part( here, point_rel_ms::zero, vpart_controls ) != -1 );
    here.add_vehicle_to_cache( veh );
    const on_out_of_scope cleanup( [&you]() {
        map &m = get_map();
        you.controlling_vehicle = false;
        if( you.in_vehicle ) {
            m.unboard_vehicle( you.pos_bub() );
        }
        clear_vehicles();
        clear_avatar();
    } );
    you.set_focus( 100 );
    REQUIRE( you.get_proficiency_practiced_time( proficiency_prof_driver ) == 0_seconds );

    REQUIRE( veh->get_driver( here ) == nullptr );
    for( int i = 0; i < 20; ++i ) {
        veh->consume_fuel( here, 1000, false );
    }
    CHECK( you.get_proficiency_practiced_time( proficiency_prof_driver ) == 0_seconds );

    // at the controls, driving trains as before
    you.setpos( here, veh_pos );
    here.board_vehicle( veh_pos, &you );
    you.controlling_vehicle = true;
    REQUIRE( veh->get_driver( here ) == &you );
    for( int i = 0; i < 20; ++i ) {
        veh->consume_fuel( here, 1000, false );
    }
    CHECK( you.get_proficiency_practiced_time( proficiency_prof_driver ) > 0_seconds );
}

// VEH-B12: an engine left on with an empty tank built a fuel debt without limit, all taken at
// once from the next refuel.
TEST_CASE( "veh-b12_empty_tank_builds_no_fuel_debt", "[audit][vehicle]" )
{
    const restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
    reset_world();
    const on_out_of_scope cleanup( []() {
        clear_vehicles();
    } );
    map &here = get_map();
    vehicle &car = place_car( here, 0 );
    // what one turn takes from a full tank, with no debt
    vehicle_part &tank = empty_tank( car );
    REQUIRE( tank.ammo_set( itype_gasoline, 3000 ) == 3000 );
    REQUIRE( car.fuel_left( here, itype_gasoline ) == 3000 );
    car.consume_fuel( here, 1000, false );
    const std::int64_t one_turn = 3000 - car.fuel_left( here, itype_gasoline );
    REQUIRE( one_turn > 0 );

    SECTION( "fifty turns on an empty tank" ) {
        empty_tank( car );
        REQUIRE( car.fuel_left( here, itype_gasoline ) == 0 );
        for( int i = 0; i < 50; ++i ) {
            car.consume_fuel( here, 1000, false );
        }
        REQUIRE( tank.ammo_set( itype_gasoline, 3000 ) == 3000 );
        car.consume_fuel( here, 1000, false );
        CHECK( 3000 - car.fuel_left( here, itype_gasoline ) <= one_turn + 1 );
    }
    SECTION( "a large debt from an old save" ) {
        empty_tank( car );
        car.fuel_remainder[itype_gasoline] = units::from_kilojoule( static_cast<std::int64_t>( -100000 ) );
        REQUIRE( tank.ammo_set( itype_gasoline, 3000 ) == 3000 );
        car.consume_fuel( here, 1000, false );
        // this turn, plus at most a turn and a mL owed: not the 100000 kJ (about 3 L)
        CHECK( 3000 - car.fuel_left( here, itype_gasoline ) <= 3 * one_turn );
    }
}
