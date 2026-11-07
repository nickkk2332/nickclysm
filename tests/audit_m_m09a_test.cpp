#include <algorithm>
#include <climits>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "avatar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "city.h"
#include "condition.h"
#include "coordinates.h"
#include "debug.h"
#include "dialogue.h"
#include "dialogue_helpers.h"
#include "effect_on_condition.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "json_loader.h"
#include "map_helpers.h"
#include "map_scale_constants.h"
#include "mapdata.h"
#include "math_parser_diag_value.h"
#include "mission.h"
#include "omdata.h"
#include "overmap.h"
#include "overmap_worldgen.h"
#include "overmapbuffer.h"
#include "player_helpers.h"
#include "point.h"
#include "regional_settings.h"
#include "talker.h"
#include "type_id.h"

static const effect_on_condition_id
effect_on_condition_EOC_TEST_M09A_RADIO_ADD( "EOC_TEST_M09A_RADIO_ADD" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_M09A_RADIO_REMOVE( "EOC_TEST_M09A_RADIO_REMOVE" );

static const forest_biome_component_id
forest_biome_component_test_m09a_comp_a( "test_m09a_comp_a" );
static const forest_biome_component_id
forest_biome_component_test_m09a_comp_b( "test_m09a_comp_b" );
static const forest_biome_component_id forest_biome_component_water_forest( "water_forest" );

static const forest_biome_mapgen_id
forest_biome_mapgen_biome_forest_thick_default( "biome_forest_thick_default" );
static const forest_biome_mapgen_id forest_biome_mapgen_test_m09a_biome( "test_m09a_biome" );

static const mongroup_id GROUP_OCEAN_DEEP( "GROUP_OCEAN_DEEP" );
static const mongroup_id GROUP_OCEAN_SHORE( "GROUP_OCEAN_SHORE" );
static const mongroup_id GROUP_RIVER( "GROUP_RIVER" );
static const mongroup_id GROUP_SUBWAY_CITY( "GROUP_SUBWAY_CITY" );
static const mongroup_id GROUP_SWAMP( "GROUP_SWAMP" );
static const mongroup_id GROUP_ZOMBIE_HORDE( "GROUP_ZOMBIE_HORDE" );

static const oter_str_id oter_field( "field" );
static const oter_str_id oter_test_forest_very_thick( "test_forest_very_thick" );

static const oter_type_str_id oter_type_fema_entrance( "fema_entrance" );
static const oter_type_str_id oter_type_field( "field" );
static const oter_type_str_id oter_type_lmoe( "lmoe" );
static const oter_type_str_id oter_type_radio_tower( "radio_tower" );
static const oter_type_str_id oter_type_radio_tower_1( "radio_tower_1" );

static const overmap_special_id overmap_special_Radio_Tower( "Radio Tower" );
static const overmap_special_id overmap_special_test_m09a_eoc_special( "test_m09a_eoc_special" );
static const overmap_special_id overmap_special_test_m09a_near_city( "test_m09a_near_city" );
static const overmap_special_id overmap_special_test_m09a_wilderness( "test_m09a_wilderness" );

static const region_settings_id region_settings_default( "default" );

// Generates the overmap at `where` without any specials (or with the given ones). These tests use
// overmaps far to the west of the avatar, which no other test uses.
static overmap &generate_overmap( const point_abs_om &where,
                                  const std::vector<const overmap_special *> &specials = {} )
{
    overmap_special_batch batch( where, specials );
    overmap_buffer.create_custom_overmap( where, batch );
    overmap *om = overmap_buffer.get_existing( where );
    REQUIRE( om != nullptr );
    return *om;
}

static mission_target_params parse_target( const std::string &json )
{
    const JsonValue jv = json_loader::from_string( json );
    return mission_util::parse_mission_om_target( jv.get_object() );
}

// OMAP-B08: requires_city() is false for "city_distance": [ N, -1 ] when the city sizes start at
// 0, and can_belong_to_city then returned true without looking at the minimum: about 80 vanilla
// specials (LMOE shelters, Riverside Dwelling...) could spawn right next to a city.
TEST_CASE( "omap-b08_open_ended_city_distance_keeps_its_minimum", "[audit][overmap]" )
{
    const city town( "m09a town", point_om_omt( 90, 90 ), 4 );
    const city hamlet( "m09a hamlet", point_om_omt( 90, 90 ), 2 );

    // [ 5, -1 ] with city sizes [ 0, 20 ]
    const overmap_special &wilderness = overmap_special_test_m09a_wilderness.obj();
    REQUIRE( !wilderness.requires_city() );
    CHECK( wilderness.can_belong_to_city( nullptr, INT_MAX ) );
    CHECK( wilderness.can_belong_to_city( &town, 6 ) );
    CHECK_FALSE( wilderness.can_belong_to_city( &town, 5 ) );
    CHECK_FALSE( wilderness.can_belong_to_city( &town, 2 ) );
    CHECK_FALSE( wilderness.can_belong_to_city( &town, 0 ) );

    // [ 1, 10 ] with city sizes [ 4, 16 ]: as before
    const overmap_special &near_city = overmap_special_test_m09a_near_city.obj();
    REQUIRE( near_city.requires_city() );
    CHECK( near_city.can_belong_to_city( &town, 5 ) );
    CHECK( near_city.can_belong_to_city( &town, 10 ) );
    CHECK_FALSE( near_city.can_belong_to_city( &town, 11 ) );
    CHECK_FALSE( near_city.can_belong_to_city( &town, 1 ) );
    CHECK_FALSE( near_city.can_belong_to_city( &hamlet, 5 ) );
    CHECK_FALSE( near_city.can_belong_to_city( nullptr, INT_MAX ) );
}

// OMAP-B08: the city rules only saw the cities of the special's own overmap, so a wilderness
// special could spawn right across the border from a city.
TEST_CASE( "omap-b08_city_rules_see_cities_on_existing_neighbours", "[audit][overmap]" )
{
    const point_abs_om west_pos( -70, 70 );
    const point_abs_om east_pos( -69, 70 );
    overmap &west = generate_overmap( west_pos );
    west.clear_cities();
    // 5 tiles from the east edge of the west overmap
    west.cities.emplace_back( "m09a border town", point_om_omt( OMAPX - 5, 90 ), 3 );
    // Made after the west overmap's cities are set, so whatever it looked up while it was
    // generated already saw them.
    overmap &east = generate_overmap( east_pos );
    east.clear_cities();
    east.cities.emplace_back( "m09a far town", point_om_omt( 150, 90 ), 2 );

    int distance = 0;
    const city *nearest = east.nearest_city_across_borders( tripoint_om_omt( 2, 90, 0 ),
                          distance );
    REQUIRE( nearest != nullptr );
    CHECK( nearest->name == "m09a border town" );
    // 7 tiles from its center (it is at x = -5 seen from the east overmap), minus its size
    CHECK( nearest->pos == point_om_omt( -5, 90 ) );
    CHECK( distance == 4 );
    CHECK_FALSE( overmap_special_test_m09a_wilderness->can_belong_to_city( nearest, distance ) );

    // Its own cities still count
    const city *own = east.nearest_city_across_borders( tripoint_om_omt( 150, 95, 0 ), distance );
    REQUIRE( own != nullptr );
    CHECK( own->name == "m09a far town" );
    CHECK( distance == 3 );
}

// OMAP-J03: forest biome components were rolled in the order of the set they were read into, which
// is the order the ids were first seen, not their "sequence".
TEST_CASE( "omap-j03_biome_components_roll_in_sequence_order", "[audit][overmap][mapgen]" )
{
    const forest_biome_mapgen &biome = forest_biome_mapgen_test_m09a_biome.obj();
    REQUIRE( biome.components_in_sequence.size() == 2 );
    CHECK( biome.components_in_sequence[0] == forest_biome_component_test_m09a_comp_b );
    CHECK( biome.components_in_sequence[1] == forest_biome_component_test_m09a_comp_a );
    // Both always succeed, so the first in the sequence always gives the feature.
    const ter_furn_id *expected = forest_biome_component_test_m09a_comp_b->types.pick();
    REQUIRE( expected != nullptr );
    for( int i = 0; i < 10; ++i ) {
        CHECK( biome.pick() == *expected );
    }

    // In vanilla thick forests the water component (sequence 3) used to be rolled first.
    const forest_biome_mapgen &thick = forest_biome_mapgen_biome_forest_thick_default.obj();
    REQUIRE( !thick.components_in_sequence.empty() );
    CHECK( thick.components_in_sequence.back() == forest_biome_component_water_forest );
}

// OMAP-J04: the city layout and the special density were compile-time constants.
TEST_CASE( "omap-j04_city_layout_settings_default_to_the_old_constants", "[audit][overmap]" )
{
    const region_settings &region = region_settings_default.obj();
    CHECK( region.special_sector_size == OMSPEC_FREQ );
    CHECK( region.special_attempts_per_sector == 10 );

    const region_settings_city &cities = region.get_settings_city();
    CHECK( cities.empty_lot_chance == 4 );
    CHECK( cities.min_size == 2 );
    CHECK( cities.max_size == 55 );
    REQUIRE( cities.size_variations.size() == 4 );
    CHECK( cities.size_variations[0].weight == 2 );
    CHECK( cities.size_variations[0].multiply == 1 );
    CHECK( cities.size_variations[0].divide == 3 );
    CHECK( cities.size_variations[3].weight == 1 );
    CHECK( cities.size_variations[3].multiply == 2 );
    CHECK( cities.size_variations[3].divide == 1 );
    CHECK( cities.max_spacing == 10 );
    CHECK( cities.block_width[0] == 3 );
    CHECK( cities.block_width[1] == 5 );
    CHECK( cities.street_shrink[0] == 1 );
    CHECK( cities.street_shrink[1] == 3 );
    CHECK( cities.extra_branch_chance == 5 );
    CHECK( cities.megacity_size == 40 );
    REQUIRE( cities.megacity_seeds.size() == 5 );
    CHECK( cities.megacity_seeds[2] == point( OMAPX / 2, OMAPY / 2 ) );

    const JsonValue jv = json_loader::from_string( R"({
        "city_size": 4, "empty_lot_chance": 2, "min_size": 3, "max_size": 30,
        "size_variations": [ { "weight": 1, "multiply": 3, "divide": 2 } ],
        "max_spacing": 6, "block_width": [ 2, 3 ], "street_shrink": [ 0, 1 ],
        "extra_branch_chance": 2, "megacity_size": 12, "megacity_seeds": [ [ 60, 70 ] ]
    })" );
    region_settings_city loaded;
    loaded.load( jv.get_object(), "" );
    CHECK( loaded.empty_lot_chance == 2 );
    CHECK( loaded.min_size == 3 );
    CHECK( loaded.max_size == 30 );
    REQUIRE( loaded.size_variations.size() == 1 );
    CHECK( loaded.size_variations[0].multiply == 3 );
    CHECK( loaded.size_variations[0].divide == 2 );
    CHECK( loaded.max_spacing == 6 );
    CHECK( loaded.block_width[0] == 2 );
    CHECK( loaded.street_shrink[1] == 1 );
    CHECK( loaded.extra_branch_chance == 2 );
    CHECK( loaded.megacity_size == 12 );
    REQUIRE( loaded.megacity_seeds.size() == 1 );
    CHECK( loaded.megacity_seeds[0] == point( 60, 70 ) );
}

TEST_CASE( "omap-j04_region_city_settings_shape_generated_cities", "[audit][overmap]" )
{
    // Straight south of the origin, where the default region has no ocean.
    const point_abs_om where( 0, 50 );
    const tripoint_abs_omt where_omt = project_to<coords::omt>( tripoint_abs_om( where, 0 ) );
    const region_settings &region = overmap_buffer.get_settings( where_omt );
    region_settings_city &city_spec =
        const_cast<region_settings_city &>( region.get_settings_city() );
    REQUIRE( !city_spec.is_megacity );
    REQUIRE( city::get_all().empty() );

    SECTION( "random city sizes stay within min_size and max_size" ) {
        restore_on_out_of_scope<int> restore_min( city_spec.min_size );
        restore_on_out_of_scope<int> restore_max( city_spec.max_size );
        city_spec.min_size = 5;
        city_spec.max_size = 5;
        overmap &om = generate_overmap( where );
        REQUIRE( !om.cities.empty() );
        for( const city &c : om.cities ) {
            CHECK( c.size == 5 );
        }
    }
    SECTION( "a megacity region puts its cities at its seeds" ) {
        restore_on_out_of_scope<bool> restore_mega( city_spec.is_megacity );
        restore_on_out_of_scope<int> restore_size( city_spec.megacity_size );
        restore_on_out_of_scope<std::vector<point>> restore_seeds( city_spec.megacity_seeds );
        city_spec.is_megacity = true;
        city_spec.megacity_size = 6;
        city_spec.megacity_seeds = { point( 60, 70 ) };
        overmap &om = generate_overmap( where + point_rel_om( 1, 0 ) );
        REQUIRE( om.cities.size() == 1 );
        CHECK( om.cities[0].pos == point_om_omt( 60, 70 ) );
        CHECK( om.cities[0].size == 6 );
    }
}

// OMAP-J05: no region setting type, dimension or region layout had a check(), so bad ids only
// showed up in worldgen.
TEST_CASE( "omap-j05_region_settings_report_unknown_ids", "[audit][overmap][json]" )
{
    // All the loaded data passes.
    CHECK( capture_debugmsg_during( []() {
        region_settings::check_consistency();
    } ).empty() );
    CHECK( capture_debugmsg_during( []() {
        dimension_world::check_consistency();
    } ).empty() );

    const JsonValue lake_jv = json_loader::from_string(
                                  R"({ "surface_ter": "test_m09a_no_such_oter" })" );
    region_settings_lake lake;
    lake.load( lake_jv.get_object(), "" );
    const std::string lake_msg = capture_debugmsg_during( [&lake]() {
        lake.check();
    } );
    CHECK( lake_msg.find( "test_m09a_no_such_oter" ) != std::string::npos );

    const JsonValue region_jv = json_loader::from_string(
                                    R"({ "cities": "default", "worldgen_monsters": { "swamp": "test_m09a_no_such_group" } })" );
    region_settings region;
    region.load( region_jv.get_object(), "" );
    const std::string region_msg = capture_debugmsg_during( [&region]() {
        region.check();
    } );
    CHECK( region_msg.find( "test_m09a_no_such_group" ) != std::string::npos );
}

// OMAP-J06: a closest search refused search_range at load time, exclude_seen read a key that
// doesn't exist, create_if_necessary and z-ranges couldn't be set, and only one terrain could be
// searched for.
TEST_CASE( "omap-j06_target_params_parse_the_documented_keys", "[audit][overmap][mission]" )
{
    clear_avatar();
    dialogue d( get_talker_for( get_avatar() ), nullptr );

    // This threw before.
    const mission_target_params closest = parse_target(
            R"({ "om_terrain": "field", "search_range": 20 })" );
    CHECK_FALSE( closest.random );
    CHECK( closest.search_range.evaluate( d ) == 20 );
    CHECK( closest.create_if_necessary );

    // So did this.
    const mission_target_params unseen = parse_target(
            R"({ "om_terrain": "field", "exclude_seen": true })" );
    CHECK( unseen.cant_see );
    CHECK_FALSE( unseen.random );

    const mission_target_params levels = parse_target(
            R"({ "om_terrain": "field", "create_if_necessary": false, "min_z": -2, "max_z": 1 })" );
    CHECK_FALSE( levels.create_if_necessary );
    REQUIRE( levels.min_z.has_value() );
    REQUIRE( levels.max_z.has_value() );
    CHECK( levels.min_z->evaluate( d ) == -2 );
    CHECK( levels.max_z->evaluate( d ) == 1 );

    const mission_target_params several = parse_target(
            R"({ "om_terrain": [ "field", "forest", "forest_thick" ] })" );
    CHECK( several.overmap_terrain.evaluate( d ) == "field" );
    REQUIRE( several.other_overmap_terrains.size() == 2 );
    CHECK( several.other_overmap_terrains[1].evaluate( d ) == "forest_thick" );
}

TEST_CASE( "omap-j06_target_search_range_and_levels", "[audit][overmap][mission]" )
{
    clear_avatar();
    clear_map();
    avatar &u = get_avatar();
    const tripoint_abs_omt origin = u.pos_abs_omt();
    const tripoint_abs_omt near_tile = origin + tripoint_rel_omt( 2, 0, 0 );
    const tripoint_abs_omt far_tile = origin + tripoint_rel_omt( 6, 0, 0 );
    const tripoint_abs_omt deep_tile = origin + tripoint_rel_omt( 0, 3, -1 );
    const oter_id old_near = overmap_buffer.ter( near_tile );
    const oter_id old_far = overmap_buffer.ter( far_tile );
    const oter_id old_deep = overmap_buffer.ter( deep_tile );
    const on_out_of_scope restore( [&]() {
        overmap_buffer.ter_set( near_tile, old_near );
        overmap_buffer.ter_set( far_tile, old_far );
        overmap_buffer.ter_set( deep_tile, old_deep );
    } );
    dialogue d( get_talker_for( u ), nullptr );

    // The nearest one within 3 tiles, if there is one: none yet, and that's not an error.
    overmap_buffer.ter_set( far_tile, oter_test_forest_very_thick.id() );
    const mission_target_params within_three = parse_target(
                R"({ "om_terrain": "test_forest_very_thick", "search_range": 3, "create_if_necessary": false })" );
    std::optional<tripoint_abs_omt> found;
    const std::string msg = capture_debugmsg_during( [&]() {
        found = mission_util::find_om_terrain_pos( within_three, d );
    } );
    CHECK( msg.empty() );
    CHECK_FALSE( found.has_value() );

    overmap_buffer.ter_set( near_tile, oter_test_forest_very_thick.id() );
    found = mission_util::find_om_terrain_pos( within_three, d );
    REQUIRE( found.has_value() );
    CHECK( *found == near_tile );

    // A random search looks at the origin's level, or at the levels it is given.
    overmap_buffer.ter_set( deep_tile, oter_test_forest_very_thick.id() );
    const mission_target_params surface = parse_target(
            R"({ "om_terrain": "test_forest_very_thick", "random": true, "search_range": 4, "create_if_necessary": false })" );
    for( int i = 0; i < 5; ++i ) {
        found = mission_util::find_om_terrain_pos( surface, d );
        REQUIRE( found.has_value() );
        CHECK( *found == near_tile );
    }
    const mission_target_params below = parse_target(
                                            R"({ "om_terrain": "test_forest_very_thick", "random": true, "search_range": 4, "min_z": -1, "max_z": -1, "create_if_necessary": false })" );
    for( int i = 0; i < 5; ++i ) {
        found = mission_util::find_om_terrain_pos( below, d );
        REQUIRE( found.has_value() );
        CHECK( *found == deep_tile );
    }
}

// OMAP-J07: the special's EOC condition was tested for every spot and rotation tried, and its
// effect ran before the special was placed, without being told where.
TEST_CASE( "omap-j07_special_eoc_condition_and_placement_context", "[audit][overmap][eoc]" )
{
    global_variables &globvars = get_globals();
    globvars.clear_global_values();
    const on_out_of_scope clear_vars( [&globvars]() {
        globvars.clear_global_values();
    } );
    overmap &om = generate_overmap( point_abs_om( -72, 70 ) );
    const overmap_special &special = overmap_special_test_m09a_eoc_special.obj();
    REQUIRE( special.has_eoc() );

    CHECK_FALSE( special.eoc_condition_allows( om ) );
    globvars.set_global_value( "test_m09a_allow", 1.0 );
    CHECK( special.eoc_condition_allows( om ) );

    // A spot is tested without the condition when the caller tested it for the whole overmap.
    globvars.set_global_value( "test_m09a_allow", 0.0 );
    const tripoint_om_omt origin( 90, 95, 0 );
    om.ter_set( origin, oter_field.id() );
    CHECK_FALSE( om.can_place_special( special, origin, om_direction::type::east, false ) );
    CHECK( om.can_place_special( special, origin, om_direction::type::east, false, false ) );

    // The effect runs once the special is placed, and knows where it went.
    const city town( "m09a town", point_om_omt( 90, 100 ), 4 );
    om.place_special( special, origin, om_direction::type::east, town, false, true );
    CHECK( globvars.get_global_value( "test_m09a_placed" ) == 1 );
    CHECK( globvars.get_global_value( "test_m09a_city_size" ) == 4 );
    // 5 tiles from the town's center, minus its size
    CHECK( globvars.get_global_value( "test_m09a_city_distance" ) == 1 );
    // 5 tiles south of the overmap's center
    CHECK( globvars.get_global_value( "test_m09a_offset" ) == 5 );
    CHECK( globvars.get_global_value( "test_m09a_context_ok" ) == 1 );
}

// OMAP-J08: radio stations came from hardcoded terrain id prefixes, with hardcoded messages.
TEST_CASE( "omap-j08_radio_stations_come_from_terrain_json", "[audit][overmap]" )
{
    using station_type = oter_radio_station::station_type;
    const std::vector<oter_radio_station> &tower = oter_type_radio_tower->radio_stations;
    REQUIRE( tower.size() == 2 );
    CHECK( tower[0].type == station_type::weather );
    CHECK( tower[0].weight == 1 );
    CHECK( tower[1].type == station_type::broadcast );
    CHECK( tower[1].weight == 2 );
    CHECK( tower[1].snippet_category == "radio_archive" );
    CHECK( tower[1].min_strength == RADIO_MIN_STRENGTH );
    CHECK( tower[1].max_strength == RADIO_MAX_STRENGTH );
    CHECK( oter_type_radio_tower_1->radio_stations.size() == 2 );

    REQUIRE( oter_type_lmoe->radio_stations.size() == 1 );
    const oter_radio_station &lmoe = oter_type_lmoe->radio_stations.front();
    CHECK( lmoe.min_strength == RADIO_MIN_STRENGTH / 2 );
    CHECK( lmoe.max_strength == RADIO_MAX_STRENGTH / 2 );
    CHECK( lmoe.message.translated().find( "<station_number>" ) != std::string::npos );
    CHECK( oter_type_fema_entrance->radio_stations.size() == 1 );
    CHECK( oter_type_field->radio_stations.empty() );

    // Each surface tile of such a terrain gets a station.
    overmap_special tower_special = overmap_special_Radio_Tower.obj();
    overmap &om = generate_overmap( point_abs_om( -74, 70 ), { &tower_special } );
    size_t expected = 0;
    for( int x = 0; x < OMAPX; ++x ) {
        for( int y = 0; y < OMAPY; ++y ) {
            const tripoint_om_omt p( x, y, 0 );
            const std::vector<oter_radio_station> &stations = om.ter( p )->get_type_id()->radio_stations;
            if( stations.empty() ) {
                continue;
            }
            ++expected;
            const point_om_sm sm = project_to<coords::sm>( p.xy() );
            const auto station = std::find_if( om.radios.begin(), om.radios.end(),
            [&sm]( const radio_tower & r ) {
                return r.pos == sm;
            } );
            CAPTURE( p );
            REQUIRE( station != om.radios.end() );
            CHECK( station->strength >= RADIO_MIN_STRENGTH / 2 );
            CHECK( station->strength <= RADIO_MAX_STRENGTH );
            if( station->type == radio_type::MESSAGE_BROADCAST ) {
                CHECK( !station->message.empty() );
            }
        }
    }
    CHECK( om.radios.size() == expected );
}

TEST_CASE( "omap-j08_eocs_add_and_remove_radio_stations", "[audit][overmap][eoc]" )
{
    clear_avatar();
    clear_map();
    global_variables &globvars = get_globals();
    globvars.clear_global_values();
    const on_out_of_scope clear_vars( [&globvars]() {
        globvars.clear_global_values();
    } );
    avatar &u = get_avatar();
    const tripoint_abs_omt where = u.pos_abs_omt() + tripoint_rel_omt( 5, 5, 0 );
    const overmap_with_local_coords om_loc = overmap_buffer.get_om_global( where );
    REQUIRE( om_loc.om != nullptr );
    overmap &om = *om_loc.om;
    const point_om_sm where_sm = project_to<coords::sm>( om_loc.local.xy() );
    const auto stations_here = [&om, &om_loc]() {
        return std::count_if( om.radios.begin(), om.radios.end(), [&om_loc]( const radio_tower & r ) {
            return project_to<coords::omt>( r.pos ) == om_loc.local.xy();
        } );
    };
    const size_t before = om.radios.size();
    const std::ptrdiff_t here_before = stations_here();

    dialogue d( get_talker_for( u ), nullptr );
    write_var_value( var_type::context, "radio_loc", &d, project_to<coords::ms>( where ) );
    effect_on_condition_EOC_TEST_M09A_RADIO_ADD->activate( d );
    REQUIRE( om.radios.size() == before + 1 );
    const radio_tower &added = om.radios.back();
    CHECK( added.pos == where_sm );
    CHECK( added.message == "m09a test broadcast" );
    CHECK( added.strength == 77 );
    CHECK( added.type == radio_type::MESSAGE_BROADCAST );
    CHECK( globvars.get_global_value( "test_m09a_frequency" ).dbl() ==
           static_cast<double>( added.frequency ) );
    // Radios hear it: it is 10 submaps from the avatar, with a strength of 77.
    const radio_tower_reference heard = overmap_buffer.find_radio_station( added.frequency );
    CHECK( heard.tower == &added );

    effect_on_condition_EOC_TEST_M09A_RADIO_REMOVE->activate( d );
    CHECK( stations_here() == 0 );
    CHECK( om.radios.size() == before - static_cast<size_t>( here_before ) );
}

// OMAP-J09: the worldgen monster groups and their numbers were hardcoded, so mods overrode the
// global groups for every region and dimension.
TEST_CASE( "omap-j09_worldgen_monsters_default_to_the_old_groups", "[audit][overmap]" )
{
    const region_settings_worldgen_monsters &monsters = region_settings_default->worldgen_monsters;
    CHECK( monsters.city_horde() == GROUP_ZOMBIE_HORDE );
    CHECK( monsters.city_horde_pack_size == 10 );
    CHECK( monsters.swamp() == GROUP_SWAMP );
    CHECK( monsters.river() == GROUP_RIVER );
    CHECK( monsters.ocean_deep() == GROUP_OCEAN_DEEP );
    CHECK( monsters.ocean_shore() == GROUP_OCEAN_SHORE );
    CHECK( monsters.water_min_tiles == 25 );
    CHECK( monsters.water_population_per_tile[0] == 8 );
    CHECK( monsters.water_population_per_tile[1] == 25 );
    CHECK( monsters.deep_ocean_threshold == Approx( 1.25 ) );
    CHECK( monsters.subway_city() == GROUP_SUBWAY_CITY );
    CHECK( monsters.subway_z == -2 );

    // A region changes what it gives and keeps the rest.
    const JsonValue jv = json_loader::from_string(
                             R"({ "river": "GROUP_NULL", "city_horde_pack_size": 20, "subway_z": -3 })" );
    region_settings_worldgen_monsters changed;
    changed.deserialize( jv.get_object() );
    CHECK( changed.river().is_null() );
    CHECK( changed.swamp() == GROUP_SWAMP );
    CHECK( changed.city_horde_pack_size == 20 );
    CHECK( changed.subway_z == -3 );
    CHECK( changed.water_min_tiles == 25 );
}
