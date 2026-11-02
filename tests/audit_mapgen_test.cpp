#include <map>
#include <set>
#include <string>
#include <utility>

#include "cata_catch.h"
#include "coordinates.h"
#include "debug.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "json_error.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "math_parser_diag_value.h"
#include "mapdata.h"
#include "mapgen.h"
#include "mapgen_functions.h"
#include "mapgendata.h"
#include "overmapbuffer.h"
#include "point.h"
#include "type_id.h"

static const oter_str_id oter_field( "field" );

static const ter_str_id ter_t_floor( "t_floor" );
static const ter_str_id ter_t_linoleum_gray( "t_linoleum_gray" );
static const ter_str_id ter_t_linoleum_white( "t_linoleum_white" );

static const update_mapgen_id
update_mapgen_test_audit_place_variable_weight_nest( "test_audit_place_variable_weight_nest" );
static const update_mapgen_id update_mapgen_test_audit_queue_other_point(
    "test_audit_queue_other_point" );
static const update_mapgen_id update_mapgen_test_audit_queue_point( "test_audit_queue_point" );
static const update_mapgen_id update_mapgen_test_audit_update_variants(
    "test_audit_update_variants" );

// Terrains an update mapgen changes, over several runs
static std::set<ter_id> terrains_changed_by( const update_mapgen_id &id, int runs )
{
    std::set<ter_id> seen;
    for( int i = 0; i < runs; ++i ) {
        for( const std::pair<const ter_id, int> &changed :
             get_changed_ids_from_update( id, {} ).first ) {
            seen.insert( changed.first );
        }
    }
    return seen;
}

// Every definition of an update_mapgen_id was added to a list, but only the first one ever ran,
// so the other prison bus and portal dungeon layouts could never appear. One definition is now
// picked per run, by weight.
TEST_CASE( "mgen-b02_update_mapgen_picks_one_of_its_definitions", "[audit][mapgen]" )
{
    // Two definitions of weight 1000: both show up in 40 runs unless 1 in 2^39 bad luck
    const std::set<ter_id> seen = terrains_changed_by( update_mapgen_test_audit_update_variants,
                                  40 );
    CHECK( seen.count( ter_t_floor.id() ) == 1 );
    CHECK( seen.count( ter_t_linoleum_white.id() ) == 1 );
    // The weight 0 definition never runs
    CHECK( seen.count( ter_t_linoleum_gray.id() ) == 0 );
}

// A nested chunk's own "weight" was read with get_int, so a variable or math weight (accepted
// everywhere else mapgen takes a weight) failed to load.
TEST_CASE( "mgen-b02_nested_mapgen_weight_can_be_math", "[audit][mapgen]" )
{
    global_variables &globals = get_globals();
    const update_mapgen_id &place_nest = update_mapgen_test_audit_place_variable_weight_nest;
    // The math weighted chunk places white linoleum, the weight 1000 one gray linoleum
    globals.set_global_value( "audit_nest_weight", 0 );
    const std::set<ter_id> disabled = terrains_changed_by( place_nest, 20 );
    globals.set_global_value( "audit_nest_weight", 1000000 );
    const std::set<ter_id> favoured = terrains_changed_by( place_nest, 20 );
    globals.remove_global_value( "audit_nest_weight" );

    CHECK( disabled.count( ter_t_linoleum_white.id() ) == 0 );
    CHECK( disabled.count( ter_t_linoleum_gray.id() ) == 1 );
    CHECK( favoured.count( ter_t_linoleum_white.id() ) == 1 );
}

// Sets up a 24x24 nested mapgen from the json members in @p members and returns what its
// load-time check() reports
static std::string check_messages_for( const std::string &members )
{
    JsonObject jo = json_loader::from_string( R"({ "mapgensize": [ 24, 24 ], )" + members + "}" );
    jo.allow_omitted_members();
    mapgen_function_json_nested func( std::move( jo ), "audit check mapgen" );
    func.setup();
    func.finalize_parameters();
    return capture_debugmsg_during( [&func]() {
        func.check();
    } );
}

static bool mentions( const std::string &messages, const std::string &what )
{
    return messages.find( what ) != std::string::npos;
}

// jmapgen_nested::check iterated "chunks" twice and never "else_chunks", so an undefined
// parameter there only surfaced at runtime.
TEST_CASE( "mgen-b03_nested_else_chunks_are_checked", "[audit][mapgen]" )
{
    const std::string messages = check_messages_for( R"( "place_nested": [
        { "else_chunks": [ { "param": "audit_no_such_param" } ], "x": 0, "y": 0 } ] )" );
    CAPTURE( messages );
    CHECK( mentions( messages, "undefined parameter 'audit_no_such_param'" ) );
}

// The first PLANT furniture in "set" returned from the whole check, skipping every piece check.
TEST_CASE( "mgen-b03_plant_furniture_does_not_skip_the_other_checks", "[audit][mapgen]" )
{
    const std::string messages = check_messages_for( R"(
        "set": [ { "point": "furniture", "id": "f_plant_unharvested_overgrown", "x": 0, "y": 0 } ],
        "place_items": [ { "item": "audit_no_such_group", "x": 3, "y": 3, "chance": 100 } ] )" );
    CAPTURE( messages );
    CHECK( mentions( messages, "f_plant_unharvested_overgrown" ) );
    CHECK( mentions( messages, "Invalid item_group_id \"audit_no_such_group\"" ) );
}

// Computer eocs and chat topics, corpse groups and the factions owning items and vehicles
// were never validated.
TEST_CASE( "mgen-b03_check_validates_computer_corpse_and_faction_ids", "[audit][mapgen]" )
{
    SECTION( "unknown ids are reported" ) {
        const std::string messages = check_messages_for( R"(
            "place_computers": [ { "name": "audit", "x": 1, "y": 1,
                "eocs": [ "audit_no_such_eoc" ], "chat_topics": [ "AUDIT_NO_SUCH_TOPIC" ] } ],
            "place_corpses": [ { "group": "GROUP_AUDIT_NO_SUCH", "x": 2, "y": 2 } ],
            "place_items": [ { "item": "trash_junkyard", "x": 3, "y": 3, "chance": 100,
                "faction": "audit_no_such_item_faction" } ],
            "place_item": [ { "item": "rock", "x": 4, "y": 4,
                "faction": "audit_no_such_spawn_faction" } ],
            "place_vehicles": [ { "vehicle": "shopping_cart", "x": 12, "y": 12,
                "faction": "audit_no_such_vehicle_faction" } ] )" );
        CAPTURE( messages );
        CHECK( mentions( messages, "\"audit_no_such_eoc\"" ) );
        CHECK( mentions( messages, "\"AUDIT_NO_SUCH_TOPIC\"" ) );
        CHECK( mentions( messages, "\"GROUP_AUDIT_NO_SUCH\"" ) );
        CHECK( mentions( messages, "\"audit_no_such_item_faction\"" ) );
        CHECK( mentions( messages, "\"audit_no_such_spawn_faction\"" ) );
        CHECK( mentions( messages, "\"audit_no_such_vehicle_faction\"" ) );
    }
    SECTION( "known ids pass" ) {
        const std::string messages = check_messages_for( R"(
            "place_computers": [ { "name": "audit", "x": 1, "y": 1,
                "eocs": [ "EOC_CAFFEINE_ADDICTION" ] } ],
            "place_corpses": [ { "group": "GROUP_ZOMBIE", "x": 2, "y": 2 } ],
            "place_items": [ { "item": "trash_junkyard", "x": 3, "y": 3, "chance": 100,
                "faction": "no_faction" } ],
            "place_item": [ { "item": "rock", "x": 4, "y": 4, "faction": "no_faction" } ],
            "place_vehicles": [ { "vehicle": "shopping_cart", "x": 12, "y": 12,
                "faction": "no_faction" } ] )" );
        CAPTURE( messages );
        CHECK( messages.empty() );
    }
}

// "line" and "square" with "bash", "burn" or "variable" made op values with no case in
// apply(), so they loaded and then silently did nothing.
TEST_CASE( "mgen-b03_line_and_square_reject_point_only_ops", "[audit][mapgen]" )
{
    const auto setup = []( const std::string & set ) {
        JsonObject jo = json_loader::from_string(
                            R"({ "mapgensize": [ 24, 24 ], "set": [ )" + set + " ] }" );
        jo.allow_omitted_members();
        mapgen_function_json_nested func( std::move( jo ), "audit setmap mapgen" );
        func.setup();
    };
    CHECK_THROWS_AS( setup( R"({ "line": "bash", "x": 0, "y": 0, "x2": 5, "y2": 5 })" ),
                     JsonError );
    CHECK_THROWS_AS( setup( R"({ "square": "burn", "x": 0, "y": 0, "x2": 5, "y2": 5 })" ),
                     JsonError );
    CHECK_THROWS_AS(
        setup( R"({ "line": "variable", "id": "audit_var", "x": 0, "y": 0, "x2": 5, "y2": 5 })" ),
        JsonError );
    CHECK_NOTHROW( setup( R"({ "point": "bash", "x": 0, "y": 0 })" ) );
}

// map::rotate took a queued variable point's position in its tile from reality bubble
// coordinates with %, which goes negative west or north of the bubble's origin, and it
// dropped queued points from other tiles.
TEST_CASE( "mgen-b04_map_rotate_moves_queued_points_with_their_tile", "[audit][mapgen]" )
{
    clear_map();
    set_queued_points();
    global_variables &globals = get_globals();
    globals.remove_global_value( "audit_queued_point" );
    globals.remove_global_value( "audit_other_queued_point" );

    // Both tiles lie west and north of the reality bubble's origin
    const tripoint_abs_omt bubble_omt = project_to<coords::omt>( get_map().get_abs_sub() );
    const tripoint_abs_omt rotated_omt = bubble_omt + tripoint_rel_omt( -3, -3, 0 );
    const tripoint_abs_omt other_omt = bubble_omt + tripoint_rel_omt( -6, -3, 0 );
    // Fields have no rotation of their own
    overmap_buffer.ter_set( rotated_omt, oter_field.id() );
    overmap_buffer.ter_set( other_omt, oter_field.id() );

    // Queues ( 5, 7 ) in other_omt and leaves it queued
    run_mapgen_update_func( update_mapgen_test_audit_queue_other_point, other_omt, {}, nullptr,
                            false );
    // Rotates rotated_omt by 3 turns, queues ( 2, 3 ) there and rotates it back by 1 turn
    run_mapgen_update_func( update_mapgen_test_audit_queue_point, rotated_omt, {}, nullptr,
                            false, false, false, 1 );
    set_queued_points();

    const tripoint_abs_ms rotated_origin = project_to<coords::ms>( rotated_omt );
    const tripoint_abs_ms other_origin = project_to<coords::ms>( other_omt );
    const diag_value *rotated = globals.maybe_get_global_value( "audit_queued_point" );
    const diag_value *other = globals.maybe_get_global_value( "audit_other_queued_point" );
    REQUIRE( rotated != nullptr );
    // One turn takes ( 2, 3 ) to ( 24 - 3 - 1, 2 )
    CHECK( rotated->tripoint() == rotated_origin + tripoint_rel_ms( 20, 2, 0 ) );
    REQUIRE( other != nullptr );
    CHECK( other->tripoint() == other_origin + tripoint_rel_ms( 5, 7, 0 ) );

    globals.remove_global_value( "audit_queued_point" );
    globals.remove_global_value( "audit_other_queued_point" );
}
