#include <functional>
#include <string>
#include <tuple>

#include "calendar.h"
#include "cata_catch.h"
#include "coordinates.h"
#include "debug.h"
#include "field.h"
#include "field_type.h"
#include "flexbuffer_json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "mapdata.h"
#include "point.h"
#include "type_id.h"

static const field_type_str_id field_fd_blood( "fd_blood" );
static const field_type_str_id field_fd_smoke( "fd_smoke" );
static const field_type_str_id field_fd_weedsmoke( "fd_weedsmoke" );

static const ter_str_id ter_t_dirt( "t_dirt" );
static const ter_str_id ter_t_floor( "t_floor" );
static const ter_str_id ter_t_nl_metal_wall( "t_nl_metal_wall" );
static const ter_str_id ter_t_nl_metal_wall_border( "t_nl_metal_wall_border" );
static const ter_str_id ter_t_window( "t_window" );
static const ter_str_id ter_t_window_boarded( "t_window_boarded" );
static const ter_str_id ter_t_window_boarded_shutters_closed( "t_window_boarded_shutters_closed" );
static const ter_str_id ter_t_window_boarded_shutters_open( "t_window_boarded_shutters_open" );
static const ter_str_id
ter_t_window_boarded_shutters_open_fully( "t_window_boarded_shutters_open_fully" );
static const ter_str_id ter_t_window_frame( "t_window_frame" );

// Rain ages outdoor fields by amount / decay_amount_factor. Before the fix the loop aged a
// copy of each field entry, so nothing on the map changed, and only the bubble's own level
// was looked at.
TEST_CASE( "map-b03_rain_ages_outdoor_fields_on_the_map", "[audit][map][weather]" )
{
    clear_map( -2, 1 );
    map &here = get_map();
    const tripoint_bub_ms outdoor( 30, 30, 0 );
    const tripoint_bub_ms indoor( 90, 90, 0 );
    const tripoint_bub_ms above( 30, 90, 1 );
    here.ter_set( indoor, ter_t_floor );
    here.build_map_cache( 0, true );
    REQUIRE( here.is_outside( outdoor ) );
    REQUIRE_FALSE( here.is_outside( indoor ) );
    REQUIRE( here.is_outside( above ) );

    REQUIRE( here.add_field( outdoor, field_fd_blood, 1, 0_turns ) );
    REQUIRE( here.add_field( indoor, field_fd_blood, 1, 0_turns ) );
    REQUIRE( here.add_field( above, field_fd_smoke, 1, 0_turns ) );
    REQUIRE( field_fd_blood->decay_amount_factor == 2 );
    REQUIRE( field_fd_smoke->decay_amount_factor == 5 );

    here.decay_fields_and_scent( 60_turns );

    CHECK( here.get_field_age( outdoor, field_fd_blood ) == 30_turns );
    CHECK( here.get_field_age( indoor, field_fd_blood ) == 0_turns );
    CHECK( here.get_field_age( above, field_fd_smoke ) == 12_turns );
}

// A copy-from that lists intensity_levels replaces the parent's levels, each level inherits
// what it leaves out from the level below, and a partial npc_complain keeps the parent's
// other values. Before the fix the levels were appended to the parent's (6 levels here), a
// missing key took the struct default, and the npc_complain locals started uninitialised.
TEST_CASE( "map-b04_field_type_copy_from_replaces_and_inherits_levels", "[audit][map][field]" )
{
    const field_type &parent = field_fd_weedsmoke.obj();
    REQUIRE( parent.get_max_intensity() == 3 );
    REQUIRE( std::get<1>( parent.npc_complain_data ) == "weed_smoke" );
    REQUIRE( std::get<2>( parent.npc_complain_data ) == 10_minutes );

    // What generic_factory does for copy-from: copy the parent, then load the child over it
    field_type child = parent;
    child.was_loaded = true;
    const JsonObject jo = json_loader::from_string( R"({
        "intensity_levels": [
            { "name": "wisp", "concentration": 3, "move_cost": 5 },
            { "name": "puff" },
            { "name": "cloud", "concentration": 7 }
        ],
        "npc_complain": { "chance": 40 }
    })" );
    child.load( jo, "" );

    REQUIRE( child.get_max_intensity() == 3 );
    CHECK( child.get_intensity_level( 0 ).name.translated() == "wisp" );
    CHECK( child.get_intensity_level( 0 ).concentration == 3 );
    CHECK( child.get_intensity_level( 0 ).move_cost == 5 );
    CHECK( child.get_intensity_level( 1 ).name.translated() == "puff" );
    CHECK( child.get_intensity_level( 1 ).concentration == 3 );
    CHECK( child.get_intensity_level( 1 ).move_cost == 5 );
    CHECK( child.get_intensity_level( 2 ).concentration == 7 );
    CHECK( child.get_intensity_level( 2 ).move_cost == 5 );

    CHECK( std::get<0>( child.npc_complain_data ) == 40 );
    CHECK( std::get<1>( child.npc_complain_data ) == "weed_smoke" );
    CHECK( std::get<2>( child.npc_complain_data ) == 10_minutes );
}

// check() validates the effects, body parts and bash block of a field type. Before the fix it
// only looked at level names.
TEST_CASE( "map-b04_field_type_check_reports_bad_references", "[audit][map][field]" )
{
    field_type child = field_fd_weedsmoke.obj();
    child.was_loaded = true;
    const JsonObject jo = json_loader::from_string( R"({
        "intensity_levels": [
            {
                "name": "haze",
                "effects": [ { "effect_id": "audit_no_such_effect", "body_part": "audit_no_such_part" } ]
            }
        ],
        "bash": { "str_min": 1, "str_max": 2, "hit_field": [ "fd_audit_no_such_field", 1 ] }
    })" );
    child.load( jo, "" );

    const std::string dmsg = capture_debugmsg_during( [&child]() {
        child.check();
    } );
    CHECK_THAT( dmsg, Catch::Contains( "audit_no_such_effect" ) );
    CHECK_THAT( dmsg, Catch::Contains( "audit_no_such_part" ) );
    CHECK_THAT( dmsg, Catch::Contains( "fd_audit_no_such_field" ) );

    // Shipped field types pass
    CHECK( capture_debugmsg_during( []() {
        field_fd_weedsmoke->check();
    } ).empty() );
}

// What generic_factory does for a terrain copy-from: copy the parent, then load the child
static ter_t load_copied_terrain( const ter_t &parent, const std::string &json )
{
    ter_t child = parent;
    child.was_loaded = true;
    const JsonObject jo = json_loader::from_string( json );
    child.load( jo, std::string() );
    return child;
}

// A terrain copy-from keeps the parent's tool activity and shoot blocks, a partial override
// changes only the keys it names, and "delete" removes a block. Before the fix the four tool
// activity blocks were rebuilt empty on every load, a partial "shoot" zeroed the damage
// ranges it left out, and a child overriding bash.ter_set kept the parent's from-above result.
TEST_CASE( "map-b05_terrain_copy_from_keeps_tool_activities_and_shoot", "[audit][map][terrain]" )
{
    const ter_t &parent = ter_t_window_boarded.obj();
    REQUIRE( parent.prying->valid() );
    REQUIRE( parent.prying->result() == ter_t_window );
    REQUIRE( parent.prying->duration() == 2_minutes );
    REQUIRE( parent.shoot );
    REQUIRE( parent.shoot->destroy_dmg_min == 10 );
    REQUIRE( parent.shoot->destroy_dmg_max == 40 );
    REQUIRE( parent.bash );
    REQUIRE( parent.bash->ter_set == ter_t_window_frame );

    SECTION( "a child that leaves the blocks out inherits them" ) {
        const ter_t child = load_copied_terrain( parent, R"({ "copy-from": "t_window_boarded" })" );
        CHECK( child.prying->valid() );
        CHECK( child.prying->result() == ter_t_window );
        CHECK( child.prying->duration() == 2_minutes );
        REQUIRE( child.shoot );
        CHECK( child.shoot->destroy_dmg_max == 40 );
    }

    SECTION( "a partial override changes only the keys it names" ) {
        const ter_t child = load_copied_terrain( parent, R"({
            "copy-from": "t_window_boarded",
            "prying": { "duration": "5 minutes" },
            "shoot": { "chance_to_hit": 50 },
            "bash": { "ter_set": "t_dirt" }
        })" );
        CHECK( child.prying->valid() );
        CHECK( child.prying->result() == ter_t_window );
        CHECK( child.prying->duration() == 5_minutes );
        REQUIRE( child.shoot );
        CHECK( child.shoot->chance_to_hit == 50 );
        CHECK( child.shoot->reduce_dmg_min == parent.shoot->reduce_dmg_min );
        CHECK( child.shoot->reduce_dmg_max == parent.shoot->reduce_dmg_max );
        CHECK( child.shoot->reduce_dmg_max_laser == parent.shoot->reduce_dmg_max_laser );
        CHECK( child.shoot->destroy_dmg_min == 10 );
        CHECK( child.shoot->destroy_dmg_max == 40 );
        REQUIRE( child.bash );
        CHECK( child.bash->ter_set == ter_t_dirt );
        CHECK( child.bash->ter_set_bashed_from_above == ter_t_dirt );
    }

    SECTION( "delete removes an inherited block" ) {
        const ter_t child = load_copied_terrain( parent, R"({
            "copy-from": "t_window_boarded",
            "delete": { "prying": true }
        })" );
        CHECK_FALSE( child.prying->valid() );
    }

    SECTION( "shipped children that never had the parent's block still lack it" ) {
        CHECK_FALSE( ter_t_window_boarded_shutters_closed->prying->valid() );
        CHECK( ter_t_window_boarded_shutters_open->prying->result() ==
               ter_t_window_boarded_shutters_open_fully );
        CHECK_FALSE( ter_t_window_boarded_shutters_open_fully->prying->valid() );
        CHECK( ter_t_nl_metal_wall->oxytorch->valid() );
        CHECK_FALSE( ter_t_nl_metal_wall_border->oxytorch->valid() );
    }
}
