#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "avatar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "city.h"
#include "condition.h"
#include "coordinates.h"
#include "cube_direction.h"
#include "dialogue.h"
#include "dialogue_helpers.h"
#include "enums.h"
#include "flexbuffer_json.h"
#include "game_constants.h"
#include "json_loader.h"
#include "map_helpers.h"
#include "math_parser_diag_value.h"
#include "omdata.h"
#include "overmap.h"
#include "overmap_connection.h"
#include "overmap_location.h"
#include "overmap_noise.h"
#include "overmap_test_helper.h"
#include "overmapbuffer.h"
#include "player_helpers.h"
#include "point.h"
#include "regional_settings.h"
#include "simplexnoise.h"
#include "talker.h"
#include "type_id.h"

static const oter_str_id oter_bridge_north( "bridge_north" );
static const oter_str_id oter_bridge_road_north( "bridge_road_north" );
static const oter_str_id oter_bridgehead_ground_north( "bridgehead_ground_north" );
static const oter_str_id oter_bridgehead_ground_south( "bridgehead_ground_south" );
static const oter_str_id oter_bridgehead_ramp_north( "bridgehead_ramp_north" );
static const oter_str_id oter_bridgehead_ramp_south( "bridgehead_ramp_south" );
static const oter_str_id oter_field( "field" );
static const oter_str_id oter_forest( "forest" );
static const oter_str_id oter_forest_thick( "forest_thick" );
static const oter_str_id oter_forest_water( "forest_water" );
static const oter_str_id oter_lake_water_cube( "lake_water_cube" );
static const oter_str_id oter_ocean_bed( "ocean_bed" );
static const oter_str_id oter_ocean_surface( "ocean_surface" );
static const oter_str_id oter_ravine( "ravine" );
static const oter_str_id oter_ravine_edge( "ravine_edge" );
static const oter_str_id oter_ravine_floor_edge( "ravine_floor_edge" );
static const oter_str_id oter_road_nesw_manhole( "road_nesw_manhole" );
static const oter_str_id oter_road_ns( "road_ns" );
static const oter_str_id oter_sewer_isolated( "sewer_isolated" );
static const oter_str_id oter_solid_earth( "solid_earth" );
static const oter_str_id oter_test_m09b_flagged_copy_omt( "test_m09b_flagged_copy_omt" );
static const oter_str_id oter_test_m09b_flagged_omt( "test_m09b_flagged_omt" );

static const oter_type_str_id oter_type_bridge( "bridge" );
static const oter_type_str_id oter_type_bridge_road( "bridge_road" );
static const oter_type_str_id oter_type_bridgehead_ground( "bridgehead_ground" );
static const oter_type_str_id oter_type_bridgehead_ramp( "bridgehead_ramp" );
static const oter_type_str_id oter_type_road_nesw_manhole( "road_nesw_manhole" );
static const oter_type_str_id oter_type_sewer( "sewer" );

static const overmap_connection_id overmap_connection_local_road( "local_road" );
static const overmap_connection_id
overmap_connection_test_m09b_connection( "test_m09b_connection" );
static const overmap_connection_id
overmap_connection_test_m09b_connection_changed( "test_m09b_connection_changed" );

static const overmap_location_id
overmap_location_test_m09b_extended_location( "test_m09b_extended_location" );
static const overmap_location_id
overmap_location_test_m09b_safehouse_location( "test_m09b_safehouse_location" );

static conditional_t::func load_condition( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    conditional_t::func condition;
    read_condition( jo, "condition", condition, false );
    finalize_conditions();
    return condition;
}

// OMAP-J12: overmap terrain flags were a closed enum; any other name was a load error. A mod's
// own flag is now kept, copy-from can extend and delete both kinds, and overmap locations and
// the FLAG match type test them.
TEST_CASE( "omap-j12_overmap_terrain_keeps_a_mods_own_flags", "[audit][overmap]" )
{
    const oter_t &flagged = oter_test_m09b_flagged_omt.obj();
    CHECK( flagged.has_flag( "TEST_M09B_SAFEHOUSE" ) );
    CHECK( flagged.has_flag( "SOURCE_FOOD" ) );
    CHECK( flagged.has_flag( oter_flags::source_food ) );
    CHECK( flagged.has_flag( oter_flags::no_rotate ) );
    CHECK_FALSE( flagged.has_flag( "RISK_HIGH" ) );
    CHECK_FALSE( flagged.has_flag( "TEST_M09B_OUTPOST" ) );

    const oter_t &copy = oter_test_m09b_flagged_copy_omt.obj();
    CHECK( copy.has_flag( "TEST_M09B_OUTPOST" ) );
    CHECK( copy.has_flag( oter_flags::no_rotate ) );
    CHECK( copy.has_flag( oter_flags::should_not_spawn ) );
    CHECK_FALSE( copy.has_flag( "TEST_M09B_SAFEHOUSE" ) );
    CHECK_FALSE( copy.has_flag( oter_flags::source_food ) );
    CHECK_FALSE( copy.has_flag( "SOURCE_FOOD" ) );

    CHECK( is_ot_match( "TEST_M09B_SAFEHOUSE", oter_test_m09b_flagged_omt.id(),
                        ot_match_type::flag ) );
    CHECK( is_ot_match( "SOURCE_FOOD", oter_test_m09b_flagged_omt.id(), ot_match_type::flag ) );
    CHECK_FALSE( is_ot_match( "TEST_M09B_SAFEHOUSE", oter_test_m09b_flagged_copy_omt.id(),
                              ot_match_type::flag ) );
    CHECK_FALSE( is_ot_match( "SOURCE_FOOD", oter_test_m09b_flagged_copy_omt.id(),
                              ot_match_type::flag ) );

    // A location by a mod's own flag, and (OMAP-J11) a copy-from of it that extends its terrains.
    const overmap_location &by_flag = *overmap_location_test_m09b_safehouse_location;
    CHECK( by_flag.test( oter_test_m09b_flagged_omt.id() ) );
    CHECK_FALSE( by_flag.test( oter_test_m09b_flagged_copy_omt.id() ) );
    const overmap_location &extended = *overmap_location_test_m09b_extended_location;
    CHECK( extended.test( oter_test_m09b_flagged_omt.id() ) );
    CHECK( extended.test( oter_test_m09b_flagged_copy_omt.id() ) );
    CHECK_FALSE( extended.test( oter_field.id() ) );
}

TEST_CASE( "omap-j12_searches_and_conditions_test_overmap_terrain_flags",
           "[audit][overmap][eoc]" )
{
    clear_avatar();
    clear_map();
    avatar &u = get_avatar();
    const tripoint_abs_omt here = u.pos_abs_omt();
    const tripoint_abs_omt flagged_omt = here + point( 7, 3 );
    const oter_id saved = overmap_buffer.ter( flagged_omt );
    overmap_buffer.ter_set( flagged_omt, oter_test_m09b_flagged_omt.id() );
    const on_out_of_scope restore( [&flagged_omt, &saved]() {
        overmap_buffer.ter_set( flagged_omt, saved );
    } );

    // No other terrain has the mod's flag.
    CHECK( overmap_buffer.find_closest( here, "TEST_M09B_SAFEHOUSE", 20, false, ot_match_type::flag,
                                        true ) == flagged_omt );

    dialogue d( get_talker_for( u ), nullptr );
    write_var_value( var_type::context, "omt_loc", &d, project_to<coords::ms>( flagged_omt ) );
    CHECK( load_condition(
               R"({ "condition": { "map_omt_has_flag": "TEST_M09B_SAFEHOUSE", "loc": { "context_val": "omt_loc" } } })" )
           (
               d ) );
    CHECK( load_condition(
               R"({ "condition": { "map_omt_has_flag": "SOURCE_FOOD", "loc": { "context_val": "omt_loc" } } })" )(
               d ) );
    CHECK_FALSE( load_condition(
                     R"({ "condition": { "map_omt_has_flag": "RISK_HIGH", "loc": { "context_val": "omt_loc" } } })" )(
                     d ) );
    // Without "loc": the alpha talker's tile.
    const bool here_has_flag = overmap_buffer.ter( here )->has_flag( "TEST_M09B_SAFEHOUSE" );
    CHECK( load_condition( R"({ "condition": { "map_omt_has_flag": "TEST_M09B_SAFEHOUSE" } })" )(
               d ) == here_has_flag );
}

// OMAP-J11: a copy-from of an overmap connection can append and remove subtypes instead of
// restating them all.
TEST_CASE( "omap-j11_overmap_connection_subtypes_can_be_extended_and_deleted",
           "[audit][overmap]" )
{
    const overmap_connection &base = *overmap_connection_test_m09b_connection;
    const overmap_connection &changed = *overmap_connection_test_m09b_connection_changed;

    const overmap_connection::subtype *base_field = base.pick_subtype_for( oter_field.id() );
    const overmap_connection::subtype *base_forest = base.pick_subtype_for( oter_forest.id() );
    REQUIRE( base_field != nullptr );
    REQUIRE( base_forest != nullptr );
    CHECK( base_field->basic_cost == 5 );
    CHECK( base_forest->basic_cost == 20 );
    CHECK( base.pick_subtype_for( oter_forest_water.id() ) == nullptr );

    const overmap_connection::subtype *changed_field = changed.pick_subtype_for( oter_field.id() );
    const overmap_connection::subtype *changed_swamp =
        changed.pick_subtype_for( oter_forest_water.id() );
    REQUIRE( changed_field != nullptr );
    REQUIRE( changed_swamp != nullptr );
    CHECK( changed_field->basic_cost == 5 );
    CHECK( changed_swamp->basic_cost == 40 );
    CHECK( changed.pick_subtype_for( oter_forest.id() ) == nullptr );

    CHECK( base.special_target == special_connection_target::city );
    CHECK( changed.special_target == special_connection_target::nearest_existing );
    CHECK( overmap_connection_local_road->special_target == special_connection_target::city );
}

TEST_CASE( "omap-j11_special_connections_read_a_target_and_a_max_length", "[audit][overmap]" )
{
    overmap_special_connection fixed;
    fixed.deserialize( json_loader::from_string(
                           R"({ "point": [ 0, -1, 0 ], "connection": "local_road", "target": "nearest_existing", "max_length": 12 })" ) );
    CHECK( fixed.route.target == special_connection_target::nearest_existing );
    CHECK( fixed.route.max_length == 12 );

    overmap_special_connection plain;
    plain.deserialize( json_loader::from_string(
                           R"({ "point": [ 0, -1, 0 ], "connection": "local_road" })" ) );
    CHECK_FALSE( plain.route.target.has_value() );
    CHECK_FALSE( plain.route.max_length.has_value() );

    mutable_special_connection mut;
    mut.deserialize( json_loader::from_string(
                         R"({ "connection": "forest_trail", "target": "city" })" ) );
    CHECK( mut.route.target == special_connection_target::city );
    CHECK_FALSE( mut.route.max_length.has_value() );
}

// OMAP-J11: a special's connection was always built from the nearest city's center (so a
// forest trail never reached it). nearest_existing builds it from the nearest tile that already
// has the connection; the default is unchanged.
TEST_CASE( "omap-j11_special_connection_targets", "[audit][overmap]" )
{
    // never generated, so only the terrain set here matters (on the heap: it's big)
    const std::unique_ptr<overmap> om_holder = std::make_unique<overmap>( point_abs_om( 61, -47 ) );
    overmap &om = *om_holder;
    const overmap_connection &road = *overmap_connection_local_road;
    for( int x = 80; x <= 120; ++x ) {
        for( int y = 50; y <= 120; ++y ) {
            om.ter_set( tripoint_om_omt( x, y, 0 ), oter_field.id() );
        }
    }
    const tripoint_om_omt dest( 100, 100, 0 );
    const tripoint_om_omt existing_road( 100, 110, 0 );
    om.ter_set( existing_road, oter_road_ns.id() );
    const city town( point_om_omt( 100, 60 ), 4 );
    REQUIRE( town );
    const auto road_tiles = [&om, &road]( int y_min, int y_max ) {
        int count = 0;
        for( int x = 80; x <= 120; ++x ) {
            for( int y = y_min; y <= y_max; ++y ) {
                if( road.has( om.ter( tripoint_om_omt( x, y, 0 ) ) ) ) {
                    ++count;
                }
            }
        }
        return count;
    };
    REQUIRE( road_tiles( 50, 120 ) == 1 );

    special_connection_route route;
    std::vector<tripoint_om_omt> special_omts;

    SECTION( "the default builds from the city's center, as before" ) {
        om.build_special_connection( road, route, dest, cube_direction::last, town, special_omts,
                                     false );
        CHECK( road.has( om.ter( dest ) ) );
        CHECK( road_tiles( 50, 99 ) > 0 );
        CHECK( road_tiles( 101, 109 ) == 0 );
    }
    SECTION( "nearest_existing builds from the nearest road" ) {
        route.target = special_connection_target::nearest_existing;
        om.build_special_connection( road, route, dest, cube_direction::last, town, special_omts,
                                     false );
        CHECK( road.has( om.ter( dest ) ) );
        CHECK( road_tiles( 101, 109 ) > 0 );
        CHECK( road_tiles( 50, 99 ) == 0 );
    }
    SECTION( "the special's own tiles are no source, and max_length bounds the source" ) {
        route.target = special_connection_target::nearest_existing;
        route.max_length = 15;
        special_omts.push_back( existing_road );
        // no other road within 15, and the city's center is 40 away
        om.build_special_connection( road, route, dest, cube_direction::last, town, special_omts,
                                     false );
        CHECK_FALSE( road.has( om.ter( dest ) ) );
        CHECK( road_tiles( 50, 120 ) == 1 );
    }
    SECTION( "max_length with the city target" ) {
        route.max_length = 30;
        om.build_special_connection( road, route, dest, cube_direction::last, town, special_omts,
                                     false );
        CHECK_FALSE( road.has( om.ter( dest ) ) );
        CHECK( road_tiles( 50, 120 ) == 1 );
    }
}

// OMAP-J10: forest and floodplain noise shapes are region settings now; the defaults must give
// exactly the noise the hardcoded layers gave.
TEST_CASE( "omap-j10_default_noise_shapes_give_the_old_noise", "[audit][overmap]" )
{
    const unsigned int seed = 1920237457u;
    const point_abs_omt base( -3 * OMAPX, 7 * OMAPY );
    const om_noise::om_noise_layer_forest forest( base, seed );
    const om_noise::om_noise_layer_floodplain floodplain( base, seed );
    const float s = static_cast<float>( seed % SIMPLEX_NOISE_RANDOM_SEED_LIMIT );
    for( int x = 0; x < OMAPX; x += 7 ) {
        for( int y = 0; y < OMAPY; y += 11 ) {
            const point_abs_omt p = base + point( x, y );
            // The old layers, as they were written.
            float r = scaled_octave_noise_3d( 4, 0.5, 0.03, 0, 1, p.x(), p.y(), s );
            r = std::pow( r, 2.0f );
            float d = scaled_octave_noise_3d( 6, 0.5, 0.07, 0, 1, p.x(), p.y(), s );
            d = std::pow( d, 3.0f );
            float f = scaled_octave_noise_3d( 4, 0.5, 0.05, 0, 1, p.x(), p.y(), s );
            f = std::pow( f, 2.0f );
            CAPTURE( x, y );
            CHECK( om_noise::om_noise_params::forest().at( p, s ) == r );
            CHECK( om_noise::om_noise_params::forest_detail().at( p, s ) == d );
            CHECK( floodplain.noise_at( point_om_omt( x, y ) ) == f );
            CHECK( forest.noise_at( point_om_omt( x, y ) ) ==
                   Approx( std::max( 0.0f, r - d * 0.5f ) ).margin( 1e-6 ) );
        }
    }

    // A region can change the shape.
    om_noise::om_noise_params bigger = om_noise::om_noise_params::forest();
    bigger.scale = 0.015f;
    const om_noise::om_noise_layer_forest bigger_forest( base, seed, bigger );
    bool differs = false;
    for( int x = 0; x < OMAPX && !differs; x += 7 ) {
        differs = bigger_forest.noise_at( point_om_omt( x, x ) ) !=
                  forest.noise_at( point_om_omt( x, x ) );
    }
    CHECK( differs );
}

// OMAP-J10: forest, swamp, ocean and ravine terrain are region settings, defaulting to the ids
// that were hardcoded.
TEST_CASE( "omap-j10_region_terrain_settings", "[audit][overmap]" )
{
    const region_settings &region = DEFAULT_REGION.obj();
    REQUIRE( region.overmap_forest.has_value() );
    const region_settings_forest &forest = region.get_settings_forest();
    CHECK( forest.forest_ter == oter_forest );
    CHECK( forest.forest_thick_ter == oter_forest_thick );
    CHECK( forest.swamp_ter == oter_forest_water );
    CHECK( forest.forest_noise.octaves == 4 );
    CHECK( forest.forest_noise.scale == 0.03f );
    CHECK( forest.forest_detail_noise.octaves == 6 );
    CHECK( forest.floodplain_noise.scale == 0.05f );
    if( region.overmap_ocean ) {
        const region_settings_ocean &ocean = region.get_settings_ocean();
        CHECK( ocean.surface == oter_ocean_surface );
        CHECK( ocean.bed == oter_ocean_bed );
    }
    if( region.overmap_ravine ) {
        const region_settings_ravine &ravine = region.get_settings_ravine();
        CHECK( ravine.ravine_terrain == oter_ravine );
        CHECK( ravine.ravine_edge_terrain == oter_ravine_edge );
        CHECK( ravine.ravine_floor_edge_terrain == oter_ravine_floor_edge );
    }
    REQUIRE( region.city_spec.has_value() );
    CHECK( region.get_settings_city().manhole_chance == 2 );
    CHECK( region.get_settings_city().manhole_terrain == oter_road_nesw_manhole );

    region_settings_forest custom_forest;
    custom_forest.load( json_loader::from_string(
                            R"({ "swamp_ter": "field", "noise": { "scale": 0.06 }, "floodplain_noise": { "exponent": 3 } })" ),
                        "audit_m_m09b_test" );
    CHECK( custom_forest.swamp_ter == oter_field );
    CHECK( custom_forest.forest_ter == oter_forest );
    CHECK( custom_forest.forest_noise.scale == Approx( 0.06 ) );
    CHECK( custom_forest.forest_noise.octaves == 4 );
    CHECK( custom_forest.forest_noise.exponent == 2.0f );
    CHECK( custom_forest.forest_detail_noise.scale == 0.07f );
    CHECK( custom_forest.floodplain_noise.exponent == 3.0f );
    CHECK( custom_forest.floodplain_noise.scale == 0.05f );

    region_settings_ocean custom_ocean;
    custom_ocean.load( json_loader::from_string( R"({ "bed_ter": "field" })" ),
                       "audit_m_m09b_test" );
    CHECK( custom_ocean.bed == oter_field );
    CHECK( custom_ocean.surface == oter_ocean_surface );

    region_settings_ravine custom_ravine;
    custom_ravine.load( json_loader::from_string( R"({ "ravine_edge_terrain_id": "field" })" ),
                        "audit_m_m09b_test" );
    CHECK( custom_ravine.ravine_edge_terrain == oter_field );
    CHECK( custom_ravine.ravine_terrain == oter_ravine );
}

// OMAP-J13: bridges, manhole sewers and travel ramps came from hardcoded ids; they are terrain
// data now, and vanilla's data gives what the code did.
TEST_CASE( "omap-j13_vertical_terrain_rules_come_from_data", "[audit][overmap]" )
{
    REQUIRE( oter_type_bridge->bridge.has_value() );
    const oter_bridge_rule &bridge = *oter_type_bridge->bridge;
    CHECK( bridge.overpass == oter_type_bridge_road );
    CHECK( bridge.bridgehead_ground == oter_type_bridgehead_ground );
    CHECK( bridge.bridgehead_ramp == oter_type_bridgehead_ramp );
    REQUIRE( oter_type_road_nesw_manhole->below.has_value() );
    CHECK( oter_type_road_nesw_manhole->below->terrain == oter_sewer_isolated );
    CHECK( oter_type_road_nesw_manhole->below->network == oter_below_rule::network_type::sewer );
    CHECK( oter_type_bridgehead_ground->has_flag( oter_flags::ramp ) );
    CHECK( oter_type_bridgehead_ramp->has_flag( oter_flags::ramp ) );
    CHECK_FALSE( oter_type_bridge->has_flag( oter_flags::ramp ) );

    // never generated, so only the terrain set here matters (on the heap: it's big)
    const std::unique_ptr<overmap> om_holder = std::make_unique<overmap>( point_abs_om( 61, -47 ) );
    overmap &om = *om_holder;

    SECTION( "a bridge gets its overpass and bridgeheads" ) {
        for( int y = 99; y <= 101; ++y ) {
            om.ter_set( tripoint_om_omt( 100, y, 0 ), oter_bridge_north.id() );
        }
        overmap_test_helper::generate_over( om, 1 );
        CHECK( om.ter( tripoint_om_omt( 100, 100, 0 ) ) == oter_bridge_north.id() );
        CHECK( om.ter( tripoint_om_omt( 100, 100, 1 ) ) == oter_bridge_road_north.id() );
        CHECK( om.ter( tripoint_om_omt( 100, 99, 0 ) ) == oter_bridgehead_ground_north.id() );
        CHECK( om.ter( tripoint_om_omt( 100, 99, 1 ) ) == oter_bridgehead_ramp_north.id() );
        CHECK( om.ter( tripoint_om_omt( 100, 101, 0 ) ) == oter_bridgehead_ground_south.id() );
        CHECK( om.ter( tripoint_om_omt( 100, 101, 1 ) ) == oter_bridgehead_ramp_south.id() );
    }
    SECTION( "manholes get a sewer below them, joined to the sewer network" ) {
        for( int x = 45; x <= 55; ++x ) {
            for( int y = 45; y <= 61; ++y ) {
                om.ter_set( tripoint_om_omt( x, y, -1 ), oter_solid_earth.id() );
            }
        }
        om.ter_set( tripoint_om_omt( 50, 50, 0 ), oter_road_nesw_manhole.id() );
        om.ter_set( tripoint_om_omt( 50, 56, 0 ), oter_road_nesw_manhole.id() );
        overmap_test_helper::generate_sub( om, -1 );
        CHECK( om.ter( tripoint_om_omt( 50, 50, -1 ) )->get_type_id() == oter_type_sewer );
        CHECK( om.ter( tripoint_om_omt( 50, 56, -1 ) )->get_type_id() == oter_type_sewer );
        int sewer_tiles = 0;
        for( int x = 45; x <= 55; ++x ) {
            for( int y = 51; y <= 55; ++y ) {
                if( om.ter( tripoint_om_omt( x, y, -1 ) )->get_type_id() == oter_type_sewer ) {
                    ++sewer_tiles;
                }
            }
        }
        // the two are joined by a tunnel
        CHECK( sewer_tiles >= 5 );
        CHECK( om.ter( tripoint_om_omt( 50, 60, -1 ) ) == oter_solid_earth.id() );
    }
}
