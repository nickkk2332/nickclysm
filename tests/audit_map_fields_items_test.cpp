#include <functional>
#include <string>
#include <tuple>

#include "calendar.h"
#include "cata_catch.h"
#include "coordinates.h"
#include "debug.h"
#include "enums.h"
#include "field.h"
#include "field_type.h"
#include "flexbuffer_json.h"
#include "item.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "mapdata.h"
#include "point.h"
#include "type_id.h"
#include "units.h"
#include "vehicle.h"

static const field_type_str_id field_fd_blood( "fd_blood" );
static const field_type_str_id field_fd_smoke( "fd_smoke" );
static const field_type_str_id field_fd_weedsmoke( "fd_weedsmoke" );

static const itype_id itype_jointer( "jointer" );
static const itype_id itype_tshirt( "tshirt" );

static const ter_str_id ter_t_dirt( "t_dirt" );
static const ter_str_id ter_t_floor( "t_floor" );
static const ter_str_id ter_t_nl_metal_wall( "t_nl_metal_wall" );
static const ter_str_id ter_t_nl_metal_wall_border( "t_nl_metal_wall_border" );
static const ter_str_id ter_t_wall( "t_wall" );
static const ter_str_id ter_t_window( "t_window" );
static const ter_str_id ter_t_window_boarded( "t_window_boarded" );
static const ter_str_id ter_t_window_boarded_shutters_closed( "t_window_boarded_shutters_closed" );
static const ter_str_id ter_t_window_boarded_shutters_open( "t_window_boarded_shutters_open" );
static const ter_str_id
ter_t_window_boarded_shutters_open_fully( "t_window_boarded_shutters_open_fully" );
static const ter_str_id ter_t_window_frame( "t_window_frame" );

static const vproto_id vehicle_prototype_bicycle( "bicycle" );

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

static int count_items_of( const tripoint_bub_ms &p, const itype_id &type )
{
    int n = 0;
    for( const item &it : get_map().i_at( p ) ) {
        if( it.typeId() == type ) {
            n++;
        }
    }
    return n;
}

// A bicycle parked away from the items, whose wheel runs over them
static vehicle_part &place_bicycle_wheel( vehicle *&veh )
{
    veh = get_map().add_vehicle( vehicle_prototype_bicycle, tripoint_bub_ms( 40, 40, 0 ),
                                 0_degrees, 0, veh_spawn_status::UNDAMAGED );
    REQUIRE( veh != nullptr );
    REQUIRE( !veh->wheelcache.empty() );
    return veh->part( veh->wheelcache.front() );
}

// When a wheel misses an item it must move on to the next one. Before the fix it re-rolled
// the same item until it hit, so every item under the wheel was hit: here every t-shirt was
// salvaged away, although a wheel hits a soft t-shirt well under 5% of the time.
TEST_CASE( "map-b06_wheel_miss_moves_on_to_the_next_item", "[audit][map][vehicle]" )
{
    clear_map();
    map &here = get_map();
    vehicle *veh = nullptr;
    vehicle_part &wheel = place_bicycle_wheel( veh );
    const item tshirt( itype_tshirt );
    REQUIRE( tshirt.is_salvageable() );
    REQUIRE( vehicle::hit_probability( tshirt, &wheel ) < 0.05 );

    const tripoint_bub_ms pos( 65, 65, 0 );
    const int count = 10;
    for( int n = 0; n < count; n++ ) {
        here.add_item( pos, tshirt );
    }
    REQUIRE( count_items_of( pos, itype_tshirt ) == count );

    here.smash_items( pos, 100000, "audit", &wheel, veh );

    CHECK( count_items_of( pos, itype_tshirt ) >= count / 2 );
}

// An item a wheel ejects can fly to any neighbour that takes items, and is never lost. Before
// the fix it only flew east, south or south-east, and was put into walls or lost off the map.
TEST_CASE( "map-b06_wheel_ejects_items_in_every_direction", "[audit][map][vehicle]" )
{
    clear_map();
    map &here = get_map();
    vehicle *veh = nullptr;
    vehicle_part &wheel = place_bicycle_wheel( veh );
    const item jointer( itype_jointer );
    REQUIRE_FALSE( jointer.is_salvageable() );
    REQUIRE( vehicle::hit_probability( jointer, &wheel ) == 1.0 );

    const tripoint_bub_ms pos( 65, 65, 0 );

    SECTION( "on open ground" ) {
        bool ejected_west_or_north = false;
        for( int trial = 0; trial < 300; trial++ ) {
            for( const tripoint_bub_ms &pt : here.points_in_radius( pos, 1 ) ) {
                here.i_clear( pt );
            }
            here.add_item( pos, jointer );
            here.smash_items( pos, 100000, "audit", &wheel, veh );
            int found = 0;
            for( const tripoint_bub_ms &pt : here.points_in_radius( pos, 1 ) ) {
                const int here_count = count_items_of( pt, itype_jointer );
                found += here_count;
                if( here_count > 0 && ( pt.x() < pos.x() || pt.y() < pos.y() ) ) {
                    ejected_west_or_north = true;
                }
            }
            REQUIRE( found == 1 );
        }
        CHECK( ejected_west_or_north );
    }

    SECTION( "surrounded by walls" ) {
        for( const tripoint_bub_ms &pt : here.points_in_radius( pos, 1 ) ) {
            if( pt != pos ) {
                here.ter_set( pt, ter_t_wall );
                REQUIRE( here.has_flag( ter_furn_flag::TFLAG_NOITEM, pt ) );
            }
        }
        here.add_item( pos, jointer );
        for( int trial = 0; trial < 100; trial++ ) {
            here.smash_items( pos, 100000, "audit", &wheel, veh );
            REQUIRE( count_items_of( pos, itype_jointer ) == 1 );
        }
    }
}
