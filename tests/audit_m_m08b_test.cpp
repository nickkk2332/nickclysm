#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character_id.h"
#include "coordinates.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "global_vars.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "map_scale_constants.h"
#include "mapgen_functions.h"
#include "mapgen_post_process.h"
#include "mapgen_primitives.h"
#include "mapgendata.h"
#include "math_parser_diag_value.h"
#include "monster.h"
#include "npc.h"
#include "overmapbuffer.h"
#include "player_helpers.h"
#include "point.h"
#include "regional_settings.h"
#include "type_id.h"

static const effect_on_condition_id effect_on_condition_EOC_TEST_M08B_MARK( "EOC_TEST_M08B_MARK" );

static const field_type_str_id field_fd_acid( "fd_acid" );
static const field_type_str_id field_fd_blood( "fd_blood" );
static const field_type_str_id field_fd_fire( "fd_fire" );
static const field_type_str_id field_fd_smoke( "fd_smoke" );

static const furn_str_id furn_f_chair( "f_chair" );
static const furn_str_id furn_f_rubble( "f_rubble" );
static const furn_str_id furn_f_stool( "f_stool" );
static const furn_str_id furn_f_table( "f_table" );

static const oter_str_id oter_field( "field" );

static const pp_generator_id pp_generator_riot_damage_road( "riot_damage_road" );
static const pp_generator_id pp_generator_test_m08b_pp_burn( "test_m08b_pp_burn" );
static const pp_generator_id pp_generator_test_m08b_pp_copy( "test_m08b_pp_copy" );
static const pp_generator_id pp_generator_test_m08b_pp_fields( "test_m08b_pp_fields" );
static const pp_generator_id pp_generator_test_m08b_pp_ruin( "test_m08b_pp_ruin" );

static const ter_str_id ter_t_concrete_wall( "t_concrete_wall" );
static const ter_str_id ter_t_dirt( "t_dirt" );
static const ter_str_id ter_t_floor( "t_floor" );
static const ter_str_id ter_t_grass( "t_grass" );
static const ter_str_id ter_t_sand( "t_sand" );
static const ter_str_id ter_t_thconc_floor( "t_thconc_floor" );
static const ter_str_id ter_t_wall( "t_wall" );
static const ter_str_id ter_t_water_sh( "t_water_sh" );

static const update_mapgen_id update_mapgen_test_m08b_condition( "test_m08b_condition" );
static const update_mapgen_id update_mapgen_test_m08b_npc( "test_m08b_npc" );
static const update_mapgen_id update_mapgen_test_m08b_place_on( "test_m08b_place_on" );

namespace
{
// Runs an update_mapgen on the reality bubble's map, at its (0, 0) corner
void run_update_here( const update_mapgen_id &id )
{
    mapgendata md( get_map(), mapgendata::dummy_settings );
    run_mapgen_update_func( id, md, false );
}

// Fills the overmap tile at the map's (0, 0) corner with ter, without furniture, items or fields
void fill_first_omt( map &here, const ter_str_id &ter )
{
    for( int x = 0; x < SEEX * 2; x++ ) {
        for( int y = 0; y < SEEY * 2; y++ ) {
            const tripoint_bub_ms p( x, y, 0 );
            here.furn_set( p, furn_str_id::NULL_ID() );
            here.i_clear( p );
            here.clear_fields( p );
            here.ter_set( p, ter );
        }
    }
}

int count_field_in_first_omt( const map &here, const field_type_str_id &fd )
{
    int count = 0;
    for( int x = 0; x < SEEX * 2; x++ ) {
        for( int y = 0; y < SEEY * 2; y++ ) {
            if( here.get_field_intensity( tripoint_bub_ms( x, y, 0 ), fd.id() ) > 0 ) {
                count++;
            }
        }
    }
    return count;
}
} // namespace

// MGEN-J05: pre_burn always wrote t_wall_burnt/t_floor_burnt/t_dirt, and no sub-generator could
// be limited by a condition
TEST_CASE( "mgen-j05_pre_burn_writes_its_terrain_where_its_condition_holds",
           "[audit][mapgen][post_process]" )
{
    clear_avatar();
    clear_map();
    get_globals().clear_global_values();
    restore_on_out_of_scope restore_turn( calendar::turn );
    on_out_of_scope clear_globals( []() {
        get_globals().clear_global_values();
    } );
    // past scaling_days_end, so it burns for sure
    calendar::turn = calendar::start_of_cataclysm + 14_days;
    map &here = get_map();
    const tripoint_abs_omt pos = project_to<coords::omt>( here.get_abs_sub() );
    const tripoint_bub_ms wall( 1, 1, 0 );
    const tripoint_bub_ms floor_tile( 1, 7, 0 );
    const tripoint_bub_ms ground( 1, 9, 0 );
    fill_first_omt( here, ter_t_floor );
    here.ter_set( wall, ter_t_wall );
    here.ter_set( ground, ter_t_grass );

    SECTION( "the condition fails: nothing burns" ) {
        pp_generator_test_m08b_pp_burn.obj().execute( here, pos );
        CHECK( here.ter( wall ) == ter_t_wall.id() );
        CHECK( here.ter( floor_tile ) == ter_t_floor.id() );
        CHECK( here.ter( ground ) == ter_t_grass.id() );
    }
    SECTION( "the condition holds: it burns into the terrain given" ) {
        get_globals().set_global_value( "test_m08b_pp_on", "yes" );
        pp_generator_test_m08b_pp_burn.obj().execute( here, pos );
        CHECK( here.ter( wall ) == ter_t_concrete_wall.id() );
        CHECK( here.ter( floor_tile ) == ter_t_thconc_floor.id() );
        CHECK( here.ter( ground ) == ter_t_sand.id() );
    }
}

// MGEN-J05: fire and blood were always fd_fire and fd_blood
TEST_CASE( "mgen-j05_fire_and_blood_place_the_field_given", "[audit][mapgen][post_process]" )
{
    clear_avatar();
    clear_map();
    restore_on_out_of_scope restore_turn( calendar::turn );
    // early enough that fire goes anywhere
    calendar::turn = calendar::start_of_cataclysm + 1_days;
    map &here = get_map();
    const tripoint_abs_omt pos = project_to<coords::omt>( here.get_abs_sub() );
    fill_first_omt( here, ter_t_floor );
    on_out_of_scope cleanup( [&here]() {
        fill_first_omt( here, ter_t_floor );
    } );

    pp_generator_test_m08b_pp_fields.obj().execute( here, pos );
    CHECK( count_field_in_first_omt( here, field_fd_smoke ) > 0 );
    CHECK( count_field_in_first_omt( here, field_fd_acid ) > 0 );
    CHECK( count_field_in_first_omt( here, field_fd_fire ) == 0 );
    CHECK( count_field_in_first_omt( here, field_fd_blood ) == 0 );
}

// MGEN-J05: a copy-from could only replace all of its parent's sub-generators
TEST_CASE( "mgen-j05_a_copy_deletes_and_extends_sub_generators", "[audit][mapgen][post_process]" )
{
    const std::vector<pp_sub_generator> &subs = pp_generator_test_m08b_pp_copy->sub_generators();
    std::vector<sub_generator_type> types;
    types.reserve( subs.size() );
    for( const pp_sub_generator &sg : subs ) {
        types.push_back( sg.type );
    }
    // test_pp_riot's: bash_damage, move_items, add_fire, pre_burn, place_blood
    const std::vector<sub_generator_type> expected = {
        sub_generator_type::bash_damage, sub_generator_type::move_items,
        sub_generator_type::pre_burn, sub_generator_type::place_blood
    };
    CHECK( types == expected );
    REQUIRE_FALSE( subs.empty() );
    CHECK( subs.back().attempts == 10 );
    CHECK( subs.back().field == field_fd_acid );
}

// MGEN-J05: the ruin generator's content was Aftershock's, fixed
TEST_CASE( "mgen-j05_ruin_generator_takes_its_ids", "[audit][mapgen][post_process]" )
{
    const std::vector<pp_sub_generator> &subs = pp_generator_test_m08b_pp_ruin->sub_generators();
    REQUIRE( subs.size() == 1 );
    const pp_sub_generator &ruin = subs.front();
    CHECK( ruin.type == sub_generator_type::aftershock_ruin );
    CHECK( ruin.ruin_oter == oter_field );
    CHECK( ruin.ruin_wall_ter == ter_t_concrete_wall );
    CHECK( ruin.ruin_floor_ter == ter_t_thconc_floor );
    CHECK( ruin.ruin_ground_floor_ter == ter_t_dirt );
    CHECK( ruin.ruin_cover_ter == ter_t_sand );
    CHECK( ruin.wreckage_furn == furn_f_rubble );
}

// MGEN-J05: city roads always ran riot_damage_road; it is now the region's city setting
TEST_CASE( "mgen-j05_city_roads_take_their_region_generators", "[audit][mapgen][post_process]" )
{
    const region_settings &region = DEFAULT_REGION.obj();
    REQUIRE( region.city_spec.has_value() );
    const std::vector<pp_generator_id> expected = { pp_generator_riot_damage_road };
    CHECK( region.get_settings_city().road_post_process == expected );
}

// MGEN-J06: patrol ranges were rolled once at load, and a spawn couldn't run an EOC
TEST_CASE( "mgen-j06_spawn_data_patrol_ranges_and_eocs", "[audit][mapgen][monster]" )
{
    spawn_data data;
    data.deserialize( json_loader::from_string(
                          R"({ "patrol": [ { "x": [ 0, 23 ], "y": [ 2, 5 ] },)"
                          R"( { "x": 7, "y": 8 } ], "eocs": [ "EOC_TEST_M08B_MARK" ] })" ) );
    REQUIRE( data.patrol.size() == 2 );
    REQUIRE( data.eocs.size() == 1 );
    CHECK( data.eocs.front() == effect_on_condition_EOC_TEST_M08B_MARK );

    SECTION( "each monster rolls its own route within the ranges" ) {
        std::vector<int> xs;
        for( int i = 0; i < 50; i++ ) {
            const std::vector<point_rel_ms> route = data.rolled_patrol();
            REQUIRE( route.size() == 2 );
            CHECK( route[0].x() >= 0 );
            CHECK( route[0].x() <= 23 );
            CHECK( route[0].y() >= 2 );
            CHECK( route[0].y() <= 5 );
            CHECK( route[1] == point_rel_ms( 7, 8 ) );
            xs.push_back( route[0].x() );
        }
        // fifty rolls in [0, 23] all the same: about 1 in 10^67
        CHECK( std::count( xs.begin(), xs.end(), xs.front() ) < 50 );
    }
    SECTION( "a pending spawn keeps the ranges and the EOCs in a save" ) {
        std::ostringstream saved;
        JsonOut json( saved );
        data.serialize_saved( json );
        spawn_data loaded;
        loaded.deserialize_saved( json_loader::from_string( saved.str() ) );
        CHECK( loaded.patrol == data.patrol );
        CHECK( loaded.eocs == data.eocs );
    }
    SECTION( "an older save's rolled points load as fixed points" ) {
        spawn_data loaded;
        loaded.deserialize_saved( json_loader::from_string( R"({ "patrol": [ [ 3, 4 ] ] })" ) );
        const std::vector<point_rel_ms> route = loaded.rolled_patrol();
        REQUIRE( route.size() == 1 );
        CHECK( route.front() == point_rel_ms( 3, 4 ) );
    }
    SECTION( "the EOCs run on the monster once it is placed" ) {
        clear_avatar();
        clear_map();
        on_out_of_scope cleanup( []() {
            clear_creatures();
        } );
        monster &placed = spawn_test_monster( "mon_zombie", tripoint_bub_ms( 65, 65, 0 ) );
        data.apply( placed );
        const diag_value *mark = placed.maybe_get_value( "test_m08b_mark" );
        REQUIRE( mark != nullptr );
        CHECK( mark->dbl() == 1 );
    }
}

// MGEN-J06: place_npcs couldn't give an NPC variables or run an EOC on it
TEST_CASE( "mgen-j06_place_npcs_gives_variables_and_runs_eocs", "[audit][mapgen][npc]" )
{
    clear_avatar();
    clear_map();
    std::vector<character_id> created;
    const on_out_of_scope cleanup( [&created]() {
        for( const character_id &id : created ) {
            overmap_buffer.remove_npc( id );
        }
        g->unique_npc_despawn( "test_m08b_npc" );
        clear_npcs();
    } );
    run_update_here( update_mapgen_test_m08b_npc );
    npc *person = g->find_npc_by_unique_id( "test_m08b_npc" );
    REQUIRE( person != nullptr );
    created.push_back( person->getID() );
    const diag_value *tag = person->maybe_get_value( "test_m08b_tag" );
    REQUIRE( tag != nullptr );
    CHECK( tag->str() == "boss" );
    const diag_value *level = person->maybe_get_value( "test_m08b_level" );
    REQUIRE( level != nullptr );
    CHECK( level->dbl() == 3 );
    const diag_value *mark = person->maybe_get_value( "test_m08b_mark" );
    REQUIRE( mark != nullptr );
    CHECK( mark->dbl() == 1 );
}

// MGEN-J07: no mapgen piece could take a condition, and mapgen values couldn't read variables
TEST_CASE( "mgen-j07_mapgen_conditions_and_variable_values", "[audit][mapgen]" )
{
    clear_map();
    get_globals().clear_global_values();
    on_out_of_scope clear_globals( []() {
        get_globals().clear_global_values();
    } );
    map &here = get_map();
    const tripoint_bub_ms chair( 3, 3, 0 );
    const tripoint_bub_ms table( 4, 3, 0 );
    const tripoint_bub_ms stool( 5, 3, 0 );
    const tripoint_bub_ms from_var( 6, 3, 0 );
    here.ter_set( from_var, ter_t_floor );

    SECTION( "without the variables" ) {
        run_update_here( update_mapgen_test_m08b_condition );
        CHECK_FALSE( here.has_furn( chair ) );
        // the dummy map data's overmap terrain is a field
        CHECK( here.furn( table ) == furn_f_table.id() );
        CHECK( here.furn( stool ) != furn_f_stool.id() );
        CHECK( here.ter( from_var ) == ter_t_dirt.id() );
    }
    SECTION( "with the variables" ) {
        get_globals().set_global_value( "test_m08b_gate", "yes" );
        get_globals().set_global_value( "test_m08b_ter", "t_grass" );
        run_update_here( update_mapgen_test_m08b_condition );
        CHECK( here.furn( chair ) == furn_f_chair.id() );
        CHECK( here.furn( table ) == furn_f_table.id() );
        CHECK_FALSE( here.has_furn( stool ) );
        CHECK( here.ter( from_var ) == ter_t_grass.id() );
    }
}

// MGEN-J08: a placement couldn't ask for a kind of tile
TEST_CASE( "mgen-j08_place_on_picks_a_matching_tile", "[audit][mapgen]" )
{
    clear_map();
    map &here = get_map();
    for( int x = 10; x <= 16; x++ ) {
        for( int y = 10; y <= 12; y++ ) {
            here.ter_set( tripoint_bub_ms( x, y, 0 ), ter_t_floor );
        }
    }
    // the chair's only grass
    const tripoint_bub_ms grass( 11, 11, 0 );
    here.ter_set( grass, ter_t_grass );
    // the table's only diggable tile next to water; the water itself isn't diggable
    const tripoint_bub_ms shore( 15, 11, 0 );
    here.ter_set( shore, ter_t_dirt );
    here.ter_set( tripoint_bub_ms( 16, 12, 0 ), ter_t_water_sh );
    // the stool's tile isn't passable
    const tripoint_bub_ms blocked( 20, 20, 0 );
    here.ter_set( blocked, ter_t_wall );

    run_update_here( update_mapgen_test_m08b_place_on );

    int chairs = 0;
    int tables = 0;
    for( int x = 10; x <= 16; x++ ) {
        for( int y = 10; y <= 12; y++ ) {
            const tripoint_bub_ms p( x, y, 0 );
            chairs += here.furn( p ) == furn_f_chair.id() ? 1 : 0;
            tables += here.furn( p ) == furn_f_table.id() ? 1 : 0;
        }
    }
    CHECK( here.furn( grass ) == furn_f_chair.id() );
    CHECK( chairs == 1 );
    CHECK( here.furn( shore ) == furn_f_table.id() );
    CHECK( tables == 1 );
    CHECK( here.furn( blocked ) != furn_f_stool.id() );
}
