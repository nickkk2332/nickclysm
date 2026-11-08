#include <algorithm>
#include <climits>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "activity_handlers.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character_id.h"
#include "coordinates.h"
#include "enums.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "flag.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "iexamine.h"
#include "item.h"
#include "item_location.h"
#include "itype.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "point.h"
#include "skill.h"
#include "talker.h"
#include "type_id.h"
#include "units.h"
#include "veh_appliance.h"
#include "veh_type.h"
#include "veh_utils.h"
#include "vehicle.h"
#include "vehicle_group.h"
#include "vpart_position.h"

static const activity_id ACT_REPAIR_ITEM( "ACT_REPAIR_ITEM" );
static const activity_id ACT_START_ENGINES( "ACT_START_ENGINES" );

static const flag_id json_flag_NO_MACHINE_HARVEST( "NO_MACHINE_HARVEST" );
static const flag_id json_flag_NO_MODIFY_VEHICLE( "NO_MODIFY_VEHICLE" );

static const furn_str_id furn_f_plant_harvest( "f_plant_harvest" );
static const furn_str_id furn_f_plant_seed( "f_plant_seed" );
static const furn_str_id furn_f_plant_unharvested_overgrown( "f_plant_unharvested_overgrown" );
static const furn_str_id furn_f_planter( "f_planter" );
static const furn_str_id furn_f_planter_harvest( "f_planter_harvest" );
static const furn_str_id furn_f_planter_seed( "f_planter_seed" );

static const itype_id itype_fungal_seeds( "fungal_seeds" );
static const itype_id itype_marloss_seed( "marloss_seed" );
static const itype_id itype_rock( "rock" );
static const itype_id itype_seed_tomato( "seed_tomato" );
static const itype_id itype_stick( "stick" );
static const itype_id itype_test_m24a_mending_kit( "test_m24a_mending_kit" );
static const itype_id itype_tshirt( "tshirt" );

static const skill_id skill_mechanics( "mechanics" );
static const skill_id skill_tailor( "tailor" );

static const ter_str_id ter_t_dirt( "t_dirt" );
static const ter_str_id ter_t_floor( "t_floor" );
static const ter_str_id ter_t_greenhouse( "t_greenhouse" );
static const ter_str_id ter_t_greenhouse_tilled( "t_greenhouse_tilled" );

static const vgroup_id VehicleGroup_test_m24a_proto_abstract( "test_m24a_proto_abstract" );
static const vgroup_id VehicleGroup_test_m24a_proto_parent( "test_m24a_proto_parent" );

static const vpart_id vpart_ap_test_m24a_washer( "ap_test_m24a_washer" );
static const vpart_id vpart_frame( "frame" );
static const vpart_id vpart_seat( "seat" );
static const vpart_id vpart_seed_drill( "seed_drill" );
static const vpart_id vpart_seed_drill_advanced( "seed_drill_advanced" );
static const vpart_id vpart_test_m24a_bare_bench( "test_m24a_bare_bench" );
static const vpart_id vpart_test_m24a_easy_seat( "test_m24a_easy_seat" );
static const vpart_id vpart_test_m24a_ench_seat( "test_m24a_ench_seat" );
static const vpart_id vpart_test_m24a_ench_seat_child( "test_m24a_ench_seat_child" );
static const vpart_id vpart_test_m24a_ench_seat_cleared( "test_m24a_ench_seat_cleared" );
static const vpart_id vpart_test_m24a_lock_frame( "test_m24a_lock_frame" );
static const vpart_id vpart_test_m24a_mending_bench( "test_m24a_mending_bench" );
static const vpart_id vpart_test_m24a_proof_seat( "test_m24a_proof_seat" );
static const vpart_id vpart_test_m24a_reaper( "test_m24a_reaper" );
static const vpart_id vpart_test_m24a_simple_seat( "test_m24a_simple_seat" );
static const vpart_id vpart_trunk( "trunk" );

static const vproto_id vehicle_prototype_car( "car" );
static const vproto_id vehicle_prototype_none( "none" );
static const vproto_id vehicle_prototype_test_m24a_proto_child( "test_m24a_proto_child" );
static const vproto_id vehicle_prototype_test_m24a_proto_no_zones( "test_m24a_proto_no_zones" );
static const vproto_id vehicle_prototype_test_m24a_proto_parent( "test_m24a_proto_parent" );

static const zone_type_id zone_type_LOOT_FOOD( "LOOT_FOOD" );
static const zone_type_id zone_type_LOOT_UNSORTED( "LOOT_UNSORTED" );

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

void clean_up_world()
{
    get_avatar().cancel_activity();
    clear_vehicles();
    clear_map();
    clear_avatar();
    get_globals().clear_global_values();
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

// How many of each item type (charges for those counted by charges).
std::map<itype_id, int> count_by_type( const std::list<item> &items )
{
    std::map<itype_id, int> found;
    for( const item &it : items ) {
        found[it.typeId()] += it.count();
    }
    return found;
}

std::map<itype_id, int> items_at( map &here, const tripoint_bub_ms &p )
{
    std::map<itype_id, int> found;
    for( const item &it : here.i_at( p ) ) {
        found[it.typeId()] += it.count();
    }
    return found;
}

int seeds_in( vehicle &veh, int part )
{
    int found = 0;
    for( const item &it : veh.get_items( veh.part( part ) ) ) {
        if( it.typeId() == itype_seed_tomato ) {
            found += it.count();
        }
    }
    return found;
}

// Two tomato seeds in the part's cargo, however seeds are counted.
void load_two_seeds( map &here, vehicle &veh, int part )
{
    item seeds( itype_seed_tomato, calendar::turn );
    if( seeds.count_by_charges() ) {
        seeds.charges = 2;
        REQUIRE( veh.add_item( here, veh.part( part ), seeds ).has_value() );
    } else {
        REQUIRE( veh.add_item( here, veh.part( part ), seeds ).has_value() );
        REQUIRE( veh.add_item( here, veh.part( part ), seeds ).has_value() );
    }
    REQUIRE( seeds_in( veh, part ) == 2 );
}

// Records the VINT-J06 events, with the vehicles their talkers are.
struct vehicle_event_catcher : public event_subscriber {
    struct caught {
        cata::event e;
        const vehicle *alpha;
        const vehicle *beta;
    };
    std::vector<caught> events;

    using event_subscriber::notify;
    void notify( const cata::event &e ) override {
        record( e, nullptr, nullptr );
    }
    void notify( const cata::event &e, std::unique_ptr<talker> alpha,
                 std::unique_ptr<talker> beta ) override {
        record( e, alpha ? alpha->get_const_vehicle() : nullptr,
                beta ? beta->get_const_vehicle() : nullptr );
    }
    void record( const cata::event &e, const vehicle *alpha, const vehicle *beta ) {
        switch( e.type() ) {
            case event_type::vehicle_part_installed:
            case event_type::vehicle_part_removed:
            case event_type::vehicle_part_repaired:
            case event_type::vehicle_engine_started:
            case event_type::vehicle_engine_stopped:
            case event_type::vehicle_part_toggled:
            case event_type::appliance_placed:
                events.push_back( caught{ e, alpha, beta } );
                break;
            default:
                break;
        }
    }
    std::vector<caught> of( event_type type ) const {
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

// VINT-B03: Follow and Patrol called start_engines() without a driver, which queues no start
// activity, so a stopped vehicle's engines never started and the autopilot did nothing.
TEST_CASE( "vint-b03_autopilot_starts_a_stopped_vehicles_engines", "[audit][vehicle]" )
{
    restore_on_out_of_scope restore_turn( calendar::turn );
    reset_world();
    const on_out_of_scope cleanup( []() {
        clean_up_world();
    } );
    map &here = get_map();
    avatar &u = get_avatar();
    vehicle &veh = place_car( here );
    REQUIRE_FALSE( veh.engine_on );
    REQUIRE( u.activity.is_null() );

    SECTION( "follow" ) {
        veh.enable_follow( here );
        CHECK( veh.autopilot_on );
        CHECK( veh.is_following );
        CHECK_FALSE( veh.is_patrolling );
        // the player starts them, with the start checks and messages
        CHECK( u.activity.id() == ACT_START_ENGINES );
    }
    SECTION( "patrol" ) {
        veh.enable_patrol( here );
        CHECK( veh.autopilot_on );
        CHECK( veh.is_patrolling );
        CHECK( u.activity.id() == ACT_START_ENGINES );
    }
    SECTION( "a running vehicle needs no start" ) {
        veh.engine_on = true;
        veh.enable_follow( here );
        CHECK( veh.is_following );
        CHECK( u.activity.is_null() );
        veh.engine_on = false;
    }
}

// VINT-B05: replacing a broken part skipped the NO_INSTALL_PLAYER and NO_MODIFY_VEHICLE gates of
// an install, and NO_MODIFY_VEHICLE wasn't a known flag, so no part could use it.
TEST_CASE( "vint-b05_replacing_a_part_keeps_the_install_gates", "[audit][vehicle]" )
{
    restore_on_out_of_scope restore_turn( calendar::turn );
    reset_world();
    const on_out_of_scope cleanup( []() {
        clean_up_world();
    } );
    // the fixture's lock frame wouldn't load without it
    CHECK( json_flag_NO_MODIFY_VEHICLE.is_valid() );
    map &here = get_map();
    avatar &u = get_avatar();
    vehicle &veh = place_frames( here, 2 );
    const vpart_info &seat = vpart_seat.obj();
    const vpart_info &simple = vpart_test_m24a_simple_seat.obj();
    CHECK( veh_utils::install_forbidden_reason( veh, seat ).empty() );
    CHECK_FALSE( veh_utils::install_forbidden_reason( veh, vpart_test_m24a_proof_seat.obj() ).empty() );
    CHECK_FALSE( veh_utils::modification_forbidden( veh, seat ) );

    SECTION( "a broken part players can't install can't be replaced" ) {
        const int idx = veh.install_part( here, point_rel_ms::zero, vpart_test_m24a_proof_seat );
        REQUIRE( idx != -1 );
        veh.set_hp( veh.part( idx ), 0, true, 0 );
        REQUIRE( veh.part( idx ).is_broken() );
        CHECK_FALSE( veh_utils::repair_part( here, veh, veh.part( idx ), u ) );
        const std::vector<int> left = parts_of( veh, vpart_test_m24a_proof_seat );
        REQUIRE( left.size() == 1 );
        CHECK( veh.part( left[0] ).is_broken() );
    }
    SECTION( "NO_MODIFY_VEHICLE forbids all but SIMPLE_PART parts" ) {
        REQUIRE( veh.install_part( here, point_rel_ms( 2, 0 ), vpart_test_m24a_lock_frame ) != -1 );
        CHECK( veh_utils::modification_forbidden( veh, seat ) );
        CHECK_FALSE( veh_utils::install_forbidden_reason( veh, seat ).empty() );
        CHECK_FALSE( veh_utils::modification_forbidden( veh, simple ) );
        CHECK( veh_utils::install_forbidden_reason( veh, simple ).empty() );
        // nor are its other parts repaired
        const int idx = veh.install_part( here, point_rel_ms::zero, vpart_test_m24a_easy_seat );
        REQUIRE( idx != -1 );
        veh.set_hp( veh.part( idx ), veh.part( idx ).info().durability / 2, true, 0 );
        const int hp_before = veh.part( idx ).hp();
        REQUIRE( hp_before < veh.part( idx ).info().durability );
        CHECK_FALSE( veh_utils::repair_part( here, veh, veh.part( idx ), u ) );
        CHECK( veh.part( idx ).hp() == hp_before );
    }
}

// VINT-B06: XP came from the install skills and install time for every action, so a repair
// trained nothing in a skill only the repair uses, and a quick repair paid like an install.
TEST_CASE( "vint-b06_vehicle_xp_comes_from_the_action_done", "[audit][vehicle]" )
{
    restore_on_out_of_scope restore_turn( calendar::turn );
    reset_world();
    const on_out_of_scope cleanup( []() {
        clean_up_world();
    } );
    map &here = get_map();
    avatar &u = get_avatar();
    const std::map<skill_id, int> needs = { { skill_mechanics, 1 } };
    // up to a level over the requirement: 60 an hour, for the action's own time
    CHECK( veh_utils::calc_xp_gain( needs, 60_minutes, skill_mechanics, u ) == 60 );
    CHECK( veh_utils::calc_xp_gain( needs, 4_minutes, skill_mechanics, u ) == 4 );
    // a skill the action doesn't use
    CHECK( veh_utils::calc_xp_gain( needs, 60_minutes, skill_tailor, u ) == 0 );
    // two levels over it: a quarter
    u.set_skill_level( skill_mechanics, 3 );
    CHECK( veh_utils::calc_xp_gain( needs, 60_minutes, skill_mechanics, u ) == 15 );

    SECTION( "a repair trains a skill only the repair uses" ) {
        vehicle &veh = place_frames( here, 1 );
        const int idx = veh.install_part( here, point_rel_ms::zero, vpart_test_m24a_easy_seat );
        REQUIRE( idx != -1 );
        REQUIRE( veh.part( idx ).info().install_skills.count( skill_tailor ) == 0 );
        veh.set_hp( veh.part( idx ), veh.part( idx ).info().durability / 2, true, 0 );
        REQUIRE( veh.part( idx ).get_base().repairable_levels() > 0 );
        u.set_focus( 100 );
        const auto progress = [&u]() {
            const SkillLevel &level = u.get_skill_level_object( skill_tailor );
            return level.level() * 100000 + level.exercise( true );
        };
        const int before = progress();
        REQUIRE( veh_utils::repair_part( here, veh, veh.part( idx ), u ) );
        CHECK( veh.part( idx ).hp() == veh.part( idx ).info().durability );
        CHECK( progress() > before );
    }
}

// VINT-B07: inline vpart enchantments were named after the part's translated name, so nameless
// and copied parts made the same id; a copy could neither drop nor add to the copied ones.
TEST_CASE( "vint-b07_inline_vpart_enchantments_are_named_after_the_part", "[audit][vehicle]" )
{
    const std::vector<enchantment_id> &parent = vpart_test_m24a_ench_seat->enchantments;
    REQUIRE( parent.size() == 1 );
    CHECK( parent[0].str() == "INLINE_ENCH_vpart_test_m24a_ench_seat_0" );
    CHECK( parent[0].is_valid() );
    const std::vector<enchantment_id> &child = vpart_test_m24a_ench_seat_child->enchantments;
    REQUIRE( child.size() == 2 );
    CHECK( child[0] == parent[0] );
    CHECK( child[1].str() == "INLINE_ENCH_vpart_test_m24a_ench_seat_child_1" );
    CHECK( child[1].is_valid() );
    CHECK( vpart_test_m24a_ench_seat_cleared->enchantments.empty() );
}

// VINT-B08: a prototype copy replaced its parent's items (vanilla cars lost their car kits), a
// zone could not be deleted, a copied part could not be removed, and "pseudo_tools": [] did
// nothing.
TEST_CASE( "vint-b08_copy_from_appends_and_deletes_as_documented", "[audit][vehicle]" )
{
    const vehicle_prototype &child = vehicle_prototype_test_m24a_proto_child.obj();
    const auto has_part = [&child]( const point_rel_ms & pos, const vpart_id & id ) {
        return std::count_if( child.parts.begin(), child.parts.end(),
        [&]( const vehicle_prototype::part_def & pt ) {
            return pt.pos == pos && pt.part == id;
        } );
    };
    CHECK( child.parts.size() == 4 );
    CHECK( has_part( point_rel_ms::zero, vpart_frame ) == 1 );
    CHECK( has_part( point_rel_ms::zero, vpart_seat ) == 1 );
    CHECK( has_part( point_rel_ms( 1, 0 ), vpart_frame ) == 1 );
    CHECK( has_part( point_rel_ms( 1, 0 ), vpart_seat ) == 0 );
    CHECK( has_part( point_rel_ms( 1, 0 ), vpart_trunk ) == 1 );

    // the parent's items, then its own
    REQUIRE( child.item_spawns.size() == 2 );
    CHECK( child.item_spawns[0].pos == point_rel_ms::zero );
    REQUIRE( child.item_spawns[0].item_ids.size() == 1 );
    CHECK( child.item_spawns[0].item_ids[0].first == itype_rock );
    CHECK( child.item_spawns[1].pos == point_rel_ms( 1, 0 ) );
    REQUIRE( child.item_spawns[1].item_ids.size() == 1 );
    CHECK( child.item_spawns[1].item_ids[0].first == itype_stick );
    CHECK( vehicle_prototype_test_m24a_proto_parent->item_spawns.size() == 1 );

    REQUIRE( child.zone_defs.size() == 2 );
    CHECK( child.zone_defs[0].zone_type == zone_type_LOOT_UNSORTED );
    CHECK( child.zone_defs[0].pt == point_rel_ms::zero );
    CHECK( child.zone_defs[1].zone_type == zone_type_LOOT_FOOD );
    CHECK( child.zone_defs[1].pt == point_rel_ms( 1, 0 ) );
    CHECK( vehicle_prototype_test_m24a_proto_no_zones->zone_defs.empty() );
    // zones compare filter with filter (it compared the filter with the other's name)
    vehicle_prototype::zone_def named;
    named.zone_type = zone_type_LOOT_FOOD;
    named.name = "n";
    named.filter = "n";
    vehicle_prototype::zone_def filtered = named;
    filtered.filter = "f";
    CHECK( named == named );
    CHECK_FALSE( named == filtered );

    CHECK_FALSE( vpart_test_m24a_mending_bench->get_pseudo_tools().empty() );
    CHECK( vpart_test_m24a_bare_bench->get_pseudo_tools().empty() );
}

// VINT-B09: picking from a group with nothing to pick dereferenced a null pointer; fuel and
// status were mandatory against the docs and status wasn't checked; abstract prototypes made
// groups of their own.
TEST_CASE( "vint-b09_vehicle_groups_and_spawns_fail_safe", "[audit][vehicle]" )
{
    VehicleGroup empty;
    CHECK( empty.pick().is_null() );
    VehicleGroup weightless;
    weightless.add_vehicle( vehicle_prototype_car, 0 );
    CHECK( weightless.pick().is_null() );
    VehicleGroup one;
    one.add_vehicle( vehicle_prototype_car, 5 );
    CHECK( one.pick() == vehicle_prototype_car );

    CHECK( VehicleGroup_test_m24a_proto_parent.is_valid() );
    CHECK_FALSE( VehicleGroup_test_m24a_proto_abstract.is_valid() );

    // the JsonObject goes inside, so a throw doesn't leave unread members to report
    const auto load_spawn = []( const std::string & json ) {
        const JsonObject jo = json_loader::from_string( json );
        return VehicleFunction_json( jo );
    };
    CHECK_NOTHROW( load_spawn( R"({ "vehicle": "test_m24a_proto_parent", "number": 1,
        "x": 5, "y": 5, "facing": 0 })" ) );
    CHECK_NOTHROW( load_spawn( R"({ "vehicle": "test_m24a_proto_parent", "number": 1,
        "x": 5, "y": 5, "facing": 0, "fuel": 50, "status": 2 })" ) );
    CHECK_THROWS_AS( load_spawn( R"({ "vehicle": "test_m24a_proto_parent", "number": 1,
        "x": 5, "y": 5, "facing": 0, "status": 7 })" ), JsonError );
}

// VINT-J05: the reaper only took f_plant_harvest, left nothing for the planter box, and skipped
// fungal and Marloss seeds by id; the planter only sowed upturned dirt and dropped seeds it
// couldn't sow.
TEST_CASE( "vint-j05_reaper_and_planter_follow_the_crop_data", "[audit][vehicle]" )
{
    restore_on_out_of_scope restore_turn( calendar::turn );
    reset_world();
    const on_out_of_scope cleanup( []() {
        clean_up_world();
    } );
    CHECK( itype_fungal_seeds->has_flag( json_flag_NO_MACHINE_HARVEST ) );
    CHECK( itype_marloss_seed->has_flag( json_flag_NO_MACHINE_HARVEST ) );
    CHECK_FALSE( itype_seed_tomato->has_flag( json_flag_NO_MACHINE_HARVEST ) );
    map &here = get_map();
    vehicle &veh = place_frames( here, 1 );
    const auto install_at_front = [&]( const vpart_id & id ) {
        const int idx = veh.install_part( here, point_rel_ms( 1, 0 ), id );
        REQUIRE( idx != -1 );
        veh.part( idx ).enabled = true;
        here.add_vehicle_to_cache( &veh );
        return idx;
    };

    SECTION( "the reaper" ) {
        const int idx = install_at_front( vpart_test_m24a_reaper );
        const tripoint_bub_ms spot = veh.bub_part_pos( here, veh.part( idx ) );
        here.ter_set( spot, ter_t_floor );
        SECTION( "harvests a planter box and leaves the box" ) {
            here.furn_set( spot, furn_f_planter_harvest );
            here.add_item( spot, item( itype_seed_tomato, calendar::turn ) );
            veh.operate_reaper( here );
            CHECK( here.furn( spot ).id() == furn_f_planter );
            // its fixed yields: 2 plants and 1 seed
            CHECK( items_at( here, spot ) ==
                   count_by_type( iexamine::get_harvest_items( *itype_seed_tomato, 2, 1, false ) ) );
        }
        SECTION( "clears an overgrown crop" ) {
            here.furn_set( spot, furn_f_plant_unharvested_overgrown );
            here.add_item( spot, item( itype_seed_tomato, calendar::turn ) );
            veh.operate_reaper( here );
            CHECK_FALSE( here.has_furn( spot ) );
            CHECK( here.i_at( spot ).empty() );
        }
        SECTION( "leaves a NO_MACHINE_HARVEST crop alone" ) {
            here.furn_set( spot, furn_f_plant_harvest );
            here.add_item( spot, item( itype_fungal_seeds, calendar::turn ) );
            veh.operate_reaper( here );
            CHECK( here.furn( spot ).id() == furn_f_plant_harvest );
            CHECK( items_at( here, spot ).count( itype_fungal_seeds ) == 1 );
        }
    }
    SECTION( "the planter" ) {
        SECTION( "sows a tilled greenhouse" ) {
            const int idx = install_at_front( vpart_seed_drill );
            const tripoint_bub_ms spot = veh.bub_part_pos( here, veh.part( idx ) );
            here.ter_set( spot, ter_t_greenhouse_tilled );
            load_two_seeds( here, veh, idx );
            veh.operate_planter( here );
            CHECK( here.ter( spot ).id() == ter_t_greenhouse );
            CHECK( here.furn( spot ).id() == furn_f_plant_seed );
            CHECK( items_at( here, spot )[itype_seed_tomato] == 1 );
            CHECK( seeds_in( veh, idx ) == 1 );
        }
        SECTION( "sows a planter box" ) {
            const int idx = install_at_front( vpart_seed_drill );
            const tripoint_bub_ms spot = veh.bub_part_pos( here, veh.part( idx ) );
            here.ter_set( spot, ter_t_floor );
            here.furn_set( spot, furn_f_planter );
            load_two_seeds( here, veh, idx );
            veh.operate_planter( here );
            CHECK( here.furn( spot ).id() == furn_f_planter_seed );
            CHECK( items_at( here, spot )[itype_seed_tomato] == 1 );
            CHECK( seeds_in( veh, idx ) == 1 );
        }
        const auto keeps_unsown_seeds = [&]( const vpart_id & drill ) {
            CAPTURE( drill.str() );
            const int idx = install_at_front( drill );
            const tripoint_bub_ms spot = veh.bub_part_pos( here, veh.part( idx ) );
            // plowable, not plantable: no damage, and nothing is sown or dropped
            here.ter_set( spot, ter_t_dirt );
            load_two_seeds( here, veh, idx );
            const int hp_before = veh.part( idx ).hp();
            veh.operate_planter( here );
            CHECK( here.ter( spot ).id() == ter_t_dirt );
            CHECK_FALSE( here.has_furn( spot ) );
            CHECK( here.i_at( spot ).empty() );
            CHECK( seeds_in( veh, idx ) == 2 );
            CHECK( veh.part( idx ).hp() == hp_before );
        };
        SECTION( "keeps a seed it can't sow" ) {
            keeps_unsown_seeds( vpart_seed_drill );
        }
        SECTION( "and so does the advanced one" ) {
            keeps_unsown_seeds( vpart_seed_drill_advanced );
        }
    }
}

// VINT-J06: vehicle work sent no events: repairing, placing an appliance and switching engines
// off now do, with the vehicle as a talker.
TEST_CASE( "vint-j06_vehicle_work_sends_events", "[audit][vehicle][event]" )
{
    restore_on_out_of_scope restore_turn( calendar::turn );
    reset_world();
    vehicle_event_catcher catcher;
    get_event_bus().subscribe( &catcher );
    const on_out_of_scope cleanup( [&catcher]() {
        get_event_bus().unsubscribe( &catcher );
        clean_up_world();
    } );
    map &here = get_map();
    avatar &u = get_avatar();

    SECTION( "a repair" ) {
        vehicle &veh = place_frames( here, 1 );
        const int idx = veh.install_part( here, point_rel_ms::zero, vpart_test_m24a_easy_seat );
        REQUIRE( idx != -1 );
        veh.set_hp( veh.part( idx ), veh.part( idx ).info().durability / 2, true, 0 );
        REQUIRE( veh_utils::repair_part( here, veh, veh.part( idx ), u ) );
        const std::vector<vehicle_event_catcher::caught> repaired =
            catcher.of( event_type::vehicle_part_repaired );
        REQUIRE( repaired.size() == 1 );
        CHECK( repaired[0].e.get<character_id>( "character" ) == u.getID() );
        CHECK( repaired[0].e.get<std::string>( "vpart_id" ) == vpart_test_m24a_easy_seat.str() );
        CHECK( repaired[0].e.get<std::string>( "vehicle_name" ) == veh.name );
        CHECK_FALSE( repaired[0].e.get<bool>( "replaced" ) );
        CHECK( repaired[0].beta == &veh );
    }
    SECTION( "placing an appliance" ) {
        const tripoint_bub_ms spot = u.pos_bub() + tripoint_rel_ms( 1, 0, 0 );
        REQUIRE( place_appliance( here, spot, vpart_ap_test_m24a_washer, u ) );
        const optional_vpart_position ovp = here.veh_at( spot );
        REQUIRE( ovp.has_value() );
        const std::vector<vehicle_event_catcher::caught> placed =
            catcher.of( event_type::appliance_placed );
        REQUIRE( placed.size() == 1 );
        CHECK( placed[0].e.get<character_id>( "character" ) == u.getID() );
        CHECK( placed[0].e.get<std::string>( "vpart_id" ) == vpart_ap_test_m24a_washer.str() );
        CHECK( placed[0].beta == &ovp->vehicle() );
    }
    SECTION( "switching the engines off" ) {
        vehicle &veh = place_car( here );
        veh.stop_engines( here );
        // it wasn't running
        CHECK( catcher.of( event_type::vehicle_engine_stopped ).empty() );
        veh.engine_on = true;
        veh.stop_engines( here );
        CHECK_FALSE( veh.engine_on );
        const std::vector<vehicle_event_catcher::caught> stopped =
            catcher.of( event_type::vehicle_engine_stopped );
        REQUIRE( stopped.size() == 1 );
        // like a stall, with the vehicle as alpha
        CHECK( stopped[0].alpha == &veh );
    }
}

// VINT-J08: a JSON processing cycle can now refuse what it holds, as the vanilla washer,
// dishwasher and autoclave do in C++.
TEST_CASE( "vint-j08_processing_cycle_checks_what_it_holds", "[audit][vehicle]" )
{
    restore_on_out_of_scope restore_turn( calendar::turn );
    reset_world();
    const on_out_of_scope cleanup( []() {
        clean_up_world();
    } );
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms spot = u.pos_bub() + tripoint_rel_ms( 1, 0, 0 );
    REQUIRE( place_appliance( here, spot, vpart_ap_test_m24a_washer, u ) );
    const optional_vpart_position ovp = here.veh_at( spot );
    REQUIRE( ovp.has_value() );
    vehicle &veh = ovp->vehicle();
    const std::optional<vpart_reference> cargo = ovp->cargo();
    REQUIRE( cargo.has_value() );
    const int idx = cargo->part_index();
    REQUIRE( veh.part( idx ).info().processing_cycle.has_value() );

    // a clean shirt: refused
    REQUIRE( veh.add_item( here, veh.part( idx ), item( itype_tshirt ) ).has_value() );
    CHECK_FALSE( processing_cycles::items_meet_condition( veh, idx ) );
    processing_cycles::toggle( veh, idx );
    CHECK_FALSE( processing_cycles::running( veh.part( idx ) ) );

    // only filthy ones: it starts
    veh.get_items( veh.part( idx ) ).clear();
    item filthy( itype_tshirt );
    filthy.set_flag( flag_FILTHY );
    REQUIRE( veh.add_item( here, veh.part( idx ), filthy ).has_value() );
    REQUIRE( veh.add_item( here, veh.part( idx ), filthy ).has_value() );
    CHECK( processing_cycles::items_meet_condition( veh, idx ) );
    processing_cycles::toggle( veh, idx );
    CHECK( processing_cycles::running( veh.part( idx ) ) );
}

// VINT-J09: only six listed tool ids were handed over to the repair as tools on the vehicle;
// any other repair tool a vehicle provides started the repair on a temporary item that was gone
// (an error, or a read of freed memory).
TEST_CASE( "vint-j09_any_vehicle_repair_tool_is_found_on_the_vehicle", "[audit][vehicle]" )
{
    restore_on_out_of_scope restore_turn( calendar::turn );
    reset_world();
    const on_out_of_scope cleanup( []() {
        clean_up_world();
    } );
    map &here = get_map();
    avatar &u = get_avatar();
    vehicle &veh = place_frames( here, 1 );
    const int idx = veh.install_part( here, point_rel_ms::zero, vpart_test_m24a_mending_bench );
    REQUIRE( idx != -1 );
    here.add_vehicle_to_cache( &veh );
    const tripoint_bub_ms pos = veh.bub_part_pos( here, veh.part( idx ) );
    REQUIRE( here.veh_at( pos ).part_with_tool( here, itype_test_m24a_mending_kit ).has_value() );

    // as repair_item_actor::use starts it for a tool that is nowhere
    u.assign_activity( ACT_REPAIR_ITEM, 0, INT_MIN, INT_MIN );
    u.activity.str_values.emplace_back( "repair_metal" );
    u.activity.targets.emplace_back();
    REQUIRE( vehicle::use_vehicle_tool( veh, &here, pos, itype_test_m24a_mending_kit, true ) );
    CHECK( u.activity.id() == ACT_REPAIR_ITEM );
    CHECK( u.activity.index == INT_MIN );
    REQUIRE( u.activity.coords.size() == 1 );
    CHECK( u.activity.coords[0] == here.get_abs( pos ) );
    REQUIRE( u.activity.str_values.size() == 2 );
    CHECK( u.activity.str_values[1] == itype_test_m24a_mending_kit.str() );

    // the repair finds the tool on the vehicle (no "Empty main tool" or "Lost tool" error); a
    // shirt isn't something it mends, so one attempt ends it
    item shirt( itype_tshirt );
    REQUIRE( u.wield( shirt ) );
    u.activity.values.push_back( 1 ); // repeat_type::ONCE
    u.activity.targets.emplace_back( u.get_wielded_item() );
    ::repair_item_finish( &u.activity, &u, true );
    CHECK( u.activity.is_null() );
}
