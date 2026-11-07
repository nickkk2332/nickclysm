#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "activity_actor_definitions.h"
#include "activity_handlers.h"
#include "activity_item_handling.h"
#include "avatar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "clzones.h"
#include "construction.h"
#include "coordinates.h"
#include "enums.h"
#include "flexbuffer_json.h"
#include "item.h"
#include "item_location.h"
#include "itype.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "mapdata.h"
#include "mapgen_functions.h"
#include "mapgendata.h"
#include "npc.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "point.h"
#include "requirements.h"
#include "rng.h"
#include "sounds.h"
#include "type_id.h"
#include "units.h"
#include "vehicle.h"
#include "vpart_position.h"

static const activity_id ACT_FETCH_REQUIRED( "ACT_FETCH_REQUIRED" );

static const construction_str_id construction_constr_wall_log( "constr_wall_log" );

static const faction_id faction_your_followers( "your_followers" );

static const furn_str_id furn_f_crate_c( "f_crate_c" );
static const furn_str_id furn_f_crate_o( "f_crate_o" );
static const furn_str_id furn_f_gun_safe_el( "f_gun_safe_el" );
static const furn_str_id furn_f_gunsafe_mj( "f_gunsafe_mj" );
static const furn_str_id furn_f_gunsafe_ml( "f_gunsafe_ml" );
static const furn_str_id furn_f_gunsafe_o( "f_gunsafe_o" );
static const furn_str_id furn_f_safe_o( "f_safe_o" );
static const furn_str_id furn_f_test_m26a_card_reader( "f_test_m26a_card_reader" );
static const furn_str_id furn_f_test_m26a_el_safe( "f_test_m26a_el_safe" );
static const furn_str_id furn_f_test_m26a_vault_safe( "f_test_m26a_vault_safe" );

static const itype_id itype_backpack( "backpack" );
static const itype_id itype_fertilizer_commercial( "fertilizer_commercial" );
static const itype_id itype_hammer( "hammer" );
static const itype_id itype_seed_corn( "seed_corn" );
static const itype_id itype_test_audit_hook_widget( "test_audit_hook_widget" );
static const itype_id itype_test_lamp( "test_lamp" );
static const itype_id itype_test_lockpick( "test_lockpick" );
static const itype_id itype_test_textbook_fabrication( "test_textbook_fabrication" );

static const proficiency_id proficiency_prof_lockpicking( "prof_lockpicking" );
static const proficiency_id proficiency_prof_lockpicking_expert( "prof_lockpicking_expert" );

static const skill_id skill_mechanics( "mechanics" );
static const skill_id skill_traps( "traps" );

static const ter_str_id ter_t_card_reader_broken( "t_card_reader_broken" );
static const ter_str_id ter_t_card_science( "t_card_science" );
static const ter_str_id ter_t_door_c( "t_door_c" );
static const ter_str_id ter_t_door_frame( "t_door_frame" );
static const ter_str_id ter_t_door_locked_alarm( "t_door_locked_alarm" );
static const ter_str_id ter_t_door_metal_c( "t_door_metal_c" );
static const ter_str_id ter_t_door_metal_locked( "t_door_metal_locked" );
static const ter_str_id ter_t_floor( "t_floor" );
static const ter_str_id ter_t_test_m26a_card_reader( "t_test_m26a_card_reader" );
static const ter_str_id ter_t_test_m26a_easy_lock( "t_test_m26a_easy_lock" );
static const ter_str_id ter_t_test_m26a_vault_door( "t_test_m26a_vault_door" );
static const ter_str_id ter_t_thconc_floor( "t_thconc_floor" );

static const update_mapgen_id update_mapgen_test_m26a_zone_options( "test_m26a_zone_options" );

static const vproto_id vehicle_prototype_test_shopping_cart( "test_shopping_cart" );

static const zone_type_id zone_type_CONSTRUCTION_BLUEPRINT( "CONSTRUCTION_BLUEPRINT" );
static const zone_type_id zone_type_FARM_PLOT( "FARM_PLOT" );
static const zone_type_id zone_type_LOOT_UNSORTED( "LOOT_UNSORTED" );
static const zone_type_id zone_type_NO_NPC_PICKUP( "NO_NPC_PICKUP" );
static const zone_type_id zone_type_SOURCE_FIREWOOD( "SOURCE_FIREWOOD" );
static const zone_type_id zone_type_STUDY_ZONE( "STUDY_ZONE" );
static const zone_type_id zone_type_TEST_M26A_NO_SUCH_ZONE( "TEST_M26A_NO_SUCH_ZONE" );
static const zone_type_id zone_type_TEST_M26A_ZONE( "TEST_M26A_ZONE" );
static const zone_type_id zone_type_UNLOAD_ALL( "UNLOAD_ALL" );

namespace
{
const tripoint_bub_ms work_pos( 60, 60, 0 );
// beyond PICKUP_RANGE of the work spot, so nothing there is at hand already
const tripoint_bub_ms far_loot( 60, 70, 0 );
const tripoint_bub_ms other_loot( 66, 70, 0 );

void zones_setup()
{
    clear_map_without_vision();
    clear_vehicles();
    clear_avatar();
    clear_npcs();
    zone_manager::get_manager().clear();
    set_time_to_day();
}

void add_tile_zone( const zone_type_id &type, const tripoint_bub_ms &p )
{
    map &here = get_map();
    zone_manager::get_manager().add( type.str(), type, faction_your_followers, false, true,
                                     here.get_abs( p ), here.get_abs( p ), nullptr, true );
}

npc &follower_at( const tripoint_bub_ms &pos )
{
    map &here = get_map();
    npc &guy = spawn_npc( pos.xy(), "test_talker" );
    clear_character( guy, true );
    guy.setpos( here, pos );
    guy.set_fac( faction_your_followers );
    guy.set_attitude( NPCATT_FOLLOW );
    guy.set_mission( NPC_MISSION_NULL );
    return guy;
}

requirement_id one_hammer()
{
    return multi_activity_actor::synthesize_requirements( { { item_comp( itype_hammer, 1 ) } }, {},
    {} );
}

fetch_required_activity_actor construction_fetch( const requirement_id &needed )
{
    map &here = get_map();
    return fetch_required_activity_actor( needed, do_activity_reason::NO_COMPONENTS,
                                          here.get_abs( work_pos + tripoint::north ), here.get_abs( work_pos ) );
}

int hammers_at( const tripoint_bub_ms &p )
{
    int n = 0;
    for( const item &it : get_map().i_at( p ) ) {
        n += it.typeId() == itype_hammer ? 1 : 0;
    }
    return n;
}

int hammers_in_cargo( const tripoint_bub_ms &p )
{
    int n = 0;
    if( const std::optional<vpart_reference> cargo = get_map().veh_at( p ).cargo() ) {
        for( const item &it : cargo->items() ) {
            n += it.typeId() == itype_hammer ? 1 : 0;
        }
    }
    return n;
}

bool plans_from( const std::vector<std::tuple<tripoint_bub_ms, itype_id, int>> &plan,
                 const tripoint_bub_ms &p )
{
    for( const std::tuple<tripoint_bub_ms, itype_id, int> &step : plan ) {
        if( std::get<0>( step ) == p ) {
            return true;
        }
    }
    return false;
}
} // namespace

// The fetch for a follower's zone job only left out tiles of vehicle NO_NPC_PICKUP zones, of any
// faction: the faction's NO_NPC_PICKUP zones on the ground were fetched from.
TEST_CASE( "act-b04_followers_fetch_nothing_from_no_npc_pickup_zones",
           "[audit][activities][zones]" )
{
    zones_setup();
    on_out_of_scope cleanup( []() {
        clear_npcs();
        clear_map();
        clear_avatar();
    } );
    map &here = get_map();
    zone_manager &mgr = zone_manager::get_manager();
    get_avatar().setpos( here, work_pos + tripoint( -5, -5, 0 ) );
    add_tile_zone( zone_type_LOOT_UNSORTED, far_loot );
    add_tile_zone( zone_type_NO_NPC_PICKUP, far_loot );
    add_tile_zone( zone_type_LOOT_UNSORTED, other_loot );
    here.add_item( far_loot, item( itype_hammer ) );
    npc &guy = follower_at( work_pos + tripoint::west );
    guy.backlog.emplace_front( multi_build_construction_activity_actor() );
    REQUIRE( guy.get_fac_id() == faction_your_followers );

    const tripoint_abs_ms from = here.get_abs( work_pos );
    CHECK( mgr.get_point_set_loot( from, MAX_VIEW_DISTANCE, false, faction_your_followers ).count(
               far_loot ) == 1 );
    CHECK( mgr.get_point_set_loot( from, MAX_VIEW_DISTANCE, true, faction_your_followers ).count(
               far_loot ) == 0 );
    CHECK( mgr.get_point_set_loot( from, MAX_VIEW_DISTANCE, true, faction_your_followers ).count(
               other_loot ) == 1 );

    fetch_required_activity_actor fetch = construction_fetch( one_hammer() );
    SECTION( "the only hammer lies in the protected stash" ) {
        CHECK( fetch.requirements_map( guy ).empty() );
    }
    SECTION( "another hammer lies in an open loot zone" ) {
        here.add_item( other_loot, item( itype_hammer ) );
        const std::vector<std::tuple<tripoint_bub_ms, itype_id, int>> plan = fetch.requirements_map(
                    guy );
        CHECK( plans_from( plan, other_loot ) );
        CHECK_FALSE( plans_from( plan, far_loot ) );
    }
}

// Discarding the zone manager's changes removed the vehicle zones added since, while
// range-iterating the list remove() erases from: of three, one survived. The personal zone
// count grew on each load and was never reset.
TEST_CASE( "act-b05_reverting_removes_every_added_vehicle_zone", "[audit][zones]" )
{
    zones_setup();
    on_out_of_scope cleanup( []() {
        zone_manager::get_manager().clear();
        clear_vehicles();
        clear_map();
    } );
    map &here = get_map();
    zone_manager &mgr = zone_manager::get_manager();
    vehicle *cart = here.add_vehicle( vehicle_prototype_test_shopping_cart, far_loot, 0_degrees, 0,
                                      veh_spawn_status::UNDAMAGED );
    REQUIRE( cart != nullptr );
    const std::optional<vpart_reference> cargo = here.veh_at( far_loot ).cargo();
    REQUIRE( cargo );
    for( int i = 0; i < 3; ++i ) {
        zone_data zone( "cart " + std::to_string( i ), zone_type_LOOT_UNSORTED, faction_your_followers,
                        false, true, here.get_abs( far_loot ), here.get_abs( far_loot ) );
        mgr.create_vehicle_loot_zone( *cart, cargo->mount_pos(), zone );
    }
    REQUIRE( cart->loot_zones.size() == 3 );

    mgr.revert_vzones();
    CHECK( cart->loot_zones.empty() );
    // and none of them went to the list of removed zones to restore
    mgr.revert_vzones();
    CHECK( cart->loot_zones.empty() );
}

TEST_CASE( "act-b05_personal_zone_count_follows_the_zones", "[audit][zones]" )
{
    zones_setup();
    on_out_of_scope cleanup( []() {
        zone_manager::get_manager().clear();
    } );
    zone_manager &mgr = zone_manager::get_manager();
    mgr.add( "personal", zone_type_LOOT_UNSORTED, faction_your_followers, false, true,
             tripoint_rel_ms( -1, -1, 0 ), tripoint_rel_ms( 1, 1, 0 ) );
    REQUIRE( mgr.has_personal_zones() );

    SECTION( "loading the zones twice counts the personal zone once" ) {
        std::ostringstream saved;
        JsonOut jsout( saved );
        mgr.serialize( jsout );
        mgr.deserialize( json_loader::from_string( saved.str() ) );
        mgr.deserialize( json_loader::from_string( saved.str() ) );
        std::vector<zone_manager::ref_zone_data> zones = mgr.get_zones( faction_your_followers );
        REQUIRE( zones.size() == 1 );
        REQUIRE( zones.front().get().get_is_personal() );
        REQUIRE( mgr.has_personal_zones() );
        mgr.remove( zones.front().get() );
        CHECK_FALSE( mgr.has_personal_zones() );
    }
    SECTION( "clearing the zones resets the count" ) {
        mgr.clear();
        CHECK_FALSE( mgr.has_personal_zones() );
    }
}

// The zone manager copied the zone types once per process, so the types of a world loaded
// later (its mods) were rejected and its zones of those types deleted on load.
TEST_CASE( "act-b06_zone_manager_follows_reloaded_zone_types", "[audit][zones]" )
{
    const std::string fixture =
        R"({ "id": "TEST_M26A_ZONE", "name": "m26a test zone", "display_field": "fd_null",)"
        R"( "description": "ACT-B06 fixture: the test renames it and checks that the zone manager)"
        R"( follows.", "hidden": true })";
    const std::string renamed =
        R"({ "id": "TEST_M26A_ZONE", "name": "m26a renamed zone", "display_field": "fd_null",)"
        R"( "hidden": true })";
    zone_manager &mgr = zone_manager::get_manager();
    REQUIRE( mgr.has_type( zone_type_TEST_M26A_ZONE ) );
    REQUIRE( mgr.get_name_from_type( zone_type_TEST_M26A_ZONE ) == "m26a test zone" );
    on_out_of_scope restore( [&fixture]() {
        zone_type::load_zones( json_loader::from_string( fixture ).get_object(), "test_m26a_restore" );
        zone_type::finalize_all();
    } );

    zone_type::load_zones( json_loader::from_string( renamed ).get_object(), "test_m26a_reload" );
    zone_type::finalize_all();
    CHECK( mgr.get_name_from_type( zone_type_TEST_M26A_ZONE ) == "m26a renamed zone" );
    REQUIRE( mgr.get_types().count( zone_type_TEST_M26A_ZONE ) == 1 );
    CHECK( mgr.get_types().at( zone_type_TEST_M26A_ZONE ).name() == "m26a renamed zone" );
    // every loaded type, and only those
    CHECK( mgr.get_types().size() == zone_type::get_all().size() );
    for( const zone_type &type : zone_type::get_all() ) {
        CAPTURE( type.id.str() );
        CHECK( mgr.has_type( type.id ) );
    }
    CHECK_FALSE( mgr.has_type( zone_type_TEST_M26A_NO_SUCH_ZONE ) );
}

// The fetch counted vehicle cargo on loot tiles as there for a construction, but planned only
// from the ground, so the job was called off; a fetch from the ground under a cargo part removed
// the item from the cargo part (where it isn't) and so copied it.
TEST_CASE( "act-b07_fetch_takes_what_it_counts_in_vehicle_cargo", "[audit][activities][zones]" )
{
    zones_setup();
    on_out_of_scope cleanup( []() {
        clear_vehicles();
        clear_map();
        clear_avatar();
    } );
    map &here = get_map();
    avatar &u = get_avatar();
    u.setpos( here, work_pos + tripoint::west );
    u.wear_item( item( itype_backpack ), false, false );
    add_tile_zone( zone_type_LOOT_UNSORTED, far_loot );
    REQUIRE( here.add_vehicle( vehicle_prototype_test_shopping_cart, far_loot, 0_degrees, 0,
                               veh_spawn_status::UNDAMAGED ) != nullptr );
    std::optional<vpart_reference> cargo = here.veh_at( far_loot ).cargo();
    REQUIRE( cargo );
    u.backlog.emplace_front( multi_build_construction_activity_actor() );
    here.build_map_cache( 0 );
    const tripoint_bub_ms dest = work_pos + tripoint::north;

    SECTION( "in the cart's cargo" ) {
        REQUIRE( cargo->vehicle().add_item( here, cargo->part(), item( itype_hammer ) ) );
        fetch_required_activity_actor fetch = construction_fetch( one_hammer() );
        const std::vector<std::tuple<tripoint_bub_ms, itype_id, int>> plan = fetch.requirements_map( u );
        REQUIRE( plans_from( plan, far_loot ) );
        CHECK( fetch.fetch_activity( u, far_loot, ACT_FETCH_REQUIRED ) );
        CHECK( hammers_in_cargo( far_loot ) == 0 );
        CHECK( hammers_at( dest ) == 1 );
    }
    SECTION( "on the ground under the cart" ) {
        here.add_item( far_loot, item( itype_hammer ) );
        REQUIRE( hammers_at( far_loot ) == 1 );
        REQUIRE( hammers_in_cargo( far_loot ) == 0 );
        fetch_required_activity_actor fetch = construction_fetch( one_hammer() );
        REQUIRE( plans_from( fetch.requirements_map( u ), far_loot ) );
        CHECK( fetch.fetch_activity( u, far_loot, ACT_FETCH_REQUIRED ) );
        CHECK( hammers_at( far_loot ) == 0 );
        CHECK( hammers_at( dest ) == 1 );
    }
}

// The zone scan asks multi_activity_can_do about every candidate tile; study and disassembly
// marked the book or item there as taken, and nothing cleared the mark when the tile wasn't
// picked, so other characters left it alone and sorting skipped it for good.
TEST_CASE( "act-b08_zone_scans_leave_items_unmarked", "[audit][activities][zones]" )
{
    zones_setup();
    on_out_of_scope cleanup( []() {
        clear_map();
        clear_avatar();
    } );
    map &here = get_map();
    avatar &u = get_avatar();
    REQUIRE( u.get_faction_id() == faction_your_followers );
    const tripoint_bub_ms spot = u.pos_bub() + tripoint::east;
    here.ter_set( spot, ter_t_floor );

    SECTION( "disassembly" ) {
        item &widget = here.add_item( spot, item( itype_test_audit_hook_widget ) );
        REQUIRE( widget.is_disassemblable() );
        const activity_reason_info info =
            multi_disassemble_activity_actor().multi_activity_can_do( u, spot );
        REQUIRE( info.can_do );
        REQUIRE( info.reason == do_activity_reason::NEEDS_DISASSEMBLE );
        CHECK_FALSE( widget.has_var( "activity_var" ) );
    }
    SECTION( "study" ) {
        u.wear_item( item( itype_backpack ), false, false );
        u.i_add( item( itype_test_lamp ) );
        REQUIRE( u.fine_detail_vision_mod() <= 4 );
        add_tile_zone( zone_type_STUDY_ZONE, spot );
        item &book = here.add_item( spot, item( itype_test_textbook_fabrication ) );
        REQUIRE( u.check_read_condition( book ) == read_condition_result::SUCCESS );
        const activity_reason_info info = multi_study_activity_actor().multi_activity_can_do( u,
                                          spot );
        REQUIRE( info.can_do );
        REQUIRE( info.reason == do_activity_reason::NEEDS_BOOK_TO_LEARN );
        CHECK_FALSE( book.has_var( "activity_var" ) );
    }
}

// Mapgen "zones" could set no zone options: a farm plot never planted and a blueprint built
// the first construction of all.
TEST_CASE( "act-j05_mapgen_zones_take_options", "[audit][zones][mapgen]" )
{
    zones_setup();
    on_out_of_scope cleanup( []() {
        zone_manager::get_manager().clear();
        clear_map();
    } );
    map &here = get_map();
    zone_manager &mgr = zone_manager::get_manager();
    mapgendata md( here, mapgendata::dummy_settings );
    REQUIRE( run_mapgen_update_func( update_mapgen_test_m26a_zone_options, md, false ).success() );
    const auto zone_at = [&]( const zone_type_id & type, int x ) {
        const zone_data *zone = mgr.get_zone_at( here.get_abs( tripoint_bub_ms( x, 2, 0 ) ), type,
                                faction_your_followers );
        REQUIRE( zone != nullptr );
        return zone;
    };

    const plot_options *farm = dynamic_cast<const plot_options *>( &zone_at( zone_type_FARM_PLOT,
                               2 )->get_options() );
    REQUIRE( farm != nullptr );
    CHECK( farm->get_seed() == itype_seed_corn );
    CHECK( farm->get_fertilizer() == itype_fertilizer_commercial );
    REQUIRE( itype_seed_corn->seed );
    CHECK( farm->get_mark() == itype_seed_corn->seed->fruit_id.str() );

    const blueprint_options *blueprint = dynamic_cast<const blueprint_options *>( &zone_at(
            zone_type_CONSTRUCTION_BLUEPRINT, 4 )->get_options() );
    REQUIRE( blueprint != nullptr );
    // as in the zone menu: the half wall is built on into the wall
    CHECK( blueprint->get_index() == construction_constr_wall_log.id() );

    const unload_options *unload = dynamic_cast<const unload_options *>( &zone_at(
                                       zone_type_UNLOAD_ALL, 6 )->get_options() );
    REQUIRE( unload != nullptr );
    CHECK( unload->unload_molle() );
    CHECK_FALSE( unload->unload_mods() );
    CHECK( unload->unload_sparse_only() );
    CHECK( unload->unload_sparse_threshold() == 5 );
    CHECK_FALSE( unload->unload_always() );

    const ignorable_options *firewood = dynamic_cast<const ignorable_options *>( &zone_at(
                                            zone_type_SOURCE_FIREWOOD, 8 )->get_options() );
    REQUIRE( firewood != nullptr );
    CHECK( firewood->get_ignore_contents() );

    // without options, as before
    const plot_options *bare = dynamic_cast<const plot_options *>( &zone_at( zone_type_FARM_PLOT,
                               10 )->get_options() );
    REQUIRE( bare != nullptr );
    CHECK( bare->get_seed().is_empty() );
}

TEST_CASE( "act-j05_zone_options_load_check", "[audit][zones][mapgen]" )
{
    const auto preset = []( const std::string & json ) {
        zone_options_preset result;
        result.deserialize( json_loader::from_string( json ).get_object() );
        return result;
    };
    const zone_options_preset farm = preset( R"({ "seed": "seed_corn" })" );
    CHECK_FALSE( farm.empty() );
    CHECK( farm.check( zone_type_FARM_PLOT ).empty() );
    CHECK_FALSE( farm.check( zone_type_LOOT_UNSORTED ).empty() );
    CHECK_FALSE( preset( R"({ "seed": "hammer" })" ).check( zone_type_FARM_PLOT ).empty() );
    CHECK_FALSE( preset( R"({ "fertilizer": "hammer" })" ).check( zone_type_FARM_PLOT ).empty() );
    CHECK_FALSE( preset( R"({ "construction": "constr_m26a_no_such" })" ).check(
                     zone_type_CONSTRUCTION_BLUEPRINT ).empty() );
    CHECK_FALSE( preset( R"({ "sparse_threshold": 0 })" ).check( zone_type_UNLOAD_ALL ).empty() );
    CHECK( preset( R"({ "ignore_contents": true })" ).check( zone_type_SOURCE_FIREWOOD ).empty() );
    CHECK( preset( "{}" ).empty() );
}

// A hacked card reader always turned locked metal doors within 3 tiles into open ones and the
// tile under it into a broken reader, whatever its JSON said; a hacked safe always became
// f_safe_o.
TEST_CASE( "act-j06_hacking_does_what_the_card_would", "[audit][activities]" )
{
    clear_map();
    clear_avatar();
    on_out_of_scope cleanup( []() {
        clear_map();
        clear_avatar();
    } );
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms reader = u.pos_bub() + tripoint( 5, 0, 0 );

    SECTION( "terrain reader: its changes, radius and hacked_ter" ) {
        const tripoint_bub_ms near_door = reader + tripoint( 2, 0, 0 );
        const tripoint_bub_ms far_door = reader + tripoint( 3, 0, 0 );
        here.ter_set( reader, ter_t_test_m26a_card_reader );
        here.ter_set( near_door, ter_t_door_metal_locked );
        here.ter_set( far_door, ter_t_door_metal_locked );
        hacking_activity_actor::hack_success( u, reader );
        CHECK( here.ter( near_door ) == ter_t_thconc_floor );
        CHECK( here.ter( far_door ) == ter_t_door_metal_locked );
        CHECK( here.ter( reader ) == ter_t_floor );
    }
    SECTION( "vanilla terrain reader: as before" ) {
        const tripoint_bub_ms door = reader + tripoint( 3, 0, 0 );
        here.ter_set( reader, ter_t_card_science );
        here.ter_set( door, ter_t_door_metal_locked );
        hacking_activity_actor::hack_success( u, reader );
        CHECK( here.ter( door ) == ter_t_door_metal_c );
        CHECK( here.ter( reader ) == ter_t_card_reader_broken );
    }
    SECTION( "furniture reader: its furniture changes; the terrain under it stays" ) {
        const tripoint_bub_ms crate = reader + tripoint::east;
        here.ter_set( reader, ter_t_floor );
        here.furn_set( reader, furn_f_test_m26a_card_reader );
        here.furn_set( crate, furn_f_crate_c );
        hacking_activity_actor::hack_success( u, reader );
        CHECK( here.furn( crate ) == furn_f_crate_o );
        CHECK( here.ter( reader ) == ter_t_floor );
        CHECK( here.furn( reader ) == furn_f_test_m26a_card_reader );
    }
    SECTION( "electronic safes open into their hack_result" ) {
        here.furn_set( reader, furn_f_test_m26a_el_safe );
        hacking_activity_actor::hack_success( u, reader );
        CHECK( here.furn( reader ) == furn_f_gunsafe_o );
        here.furn_set( reader, furn_f_gun_safe_el );
        hacking_activity_actor::hack_success( u, reader );
        CHECK( here.furn( reader ) == furn_f_safe_o );
    }
}

// Lockpicking rolled a random difficulty for every lock, only t_door_locked_alarm could set off
// an alarm and only f_gunsafe_ml could jam.
TEST_CASE( "act-j07_locks_have_their_own_difficulty_alarm_and_jam", "[audit][activities]" )
{
    clear_map();
    clear_avatar();
    rng_set_engine_seed( 4242 );
    on_out_of_scope cleanup( []() {
        sounds::reset_sounds();
        clear_map();
        clear_avatar();
    } );
    map &here = get_map();
    avatar &u = get_avatar();
    // a mean pick roll of about 18, against difficulties of 0 and 1000
    u.set_skill_level( skill_traps, 10 );
    u.set_skill_level( skill_mechanics, 10 );
    u.set_dex_base( 20 );
    u.set_per_base( 20 );
    u.set_int_base( 20 );
    u.reset_stats();
    u.add_proficiency( proficiency_prof_lockpicking, true );
    u.add_proficiency( proficiency_prof_lockpicking_expert, true );
    u.wear_item( item( itype_backpack ), false, false );
    const item_location pick = u.i_add( item( itype_test_lockpick ) );
    REQUIRE( pick );
    const tripoint_bub_ms lock_pos = u.pos_bub() + tripoint::east;
    const auto pick_lock = [&]() {
        sounds::reset_sounds();
        lockpick_activity_actor actor = lockpick_activity_actor::use_item( pick,
                                        here.get_abs( lock_pos ), u );
        player_activity act( actor );
        actor.finish( act, u );
    };
    const auto alarm_went_off = [&u]() {
        for( const tripoint_bub_ms &p : sounds::get_monster_sounds().first ) {
            if( p == u.pos_bub() ) {
                return true;
            }
        }
        return false;
    };

    CHECK( ter_t_door_locked_alarm->has_flag( ter_furn_flag::TFLAG_ALARMED ) );
    CHECK( furn_f_gunsafe_ml->lockpick_jam_result == furn_f_gunsafe_mj );

    SECTION( "an easy lock opens" ) {
        here.ter_set( lock_pos, ter_t_test_m26a_easy_lock );
        pick_lock();
        CHECK( here.ter( lock_pos ) == ter_t_door_c );
        CHECK_FALSE( alarm_went_off() );
    }
    SECTION( "an alarmed vault door jams and sounds the alarm" ) {
        here.ter_set( lock_pos, ter_t_test_m26a_vault_door );
        pick_lock();
        CHECK( here.ter( lock_pos ) == ter_t_door_frame );
        CHECK( alarm_went_off() );
    }
    SECTION( "a vault safe jams without an alarm" ) {
        here.ter_set( lock_pos, ter_t_floor );
        here.furn_set( lock_pos, furn_f_test_m26a_vault_safe );
        pick_lock();
        CHECK( here.furn( lock_pos ) == furn_f_gunsafe_mj );
        CHECK_FALSE( alarm_went_off() );
    }
}
