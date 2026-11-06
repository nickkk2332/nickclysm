#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>

#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "coordinates.h"
#include "debug.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "item.h"
#include "item_category.h"
#include "json_error.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "math_parser_diag_value.h"
#include "mapdata.h"
#include "mapgen.h"
#include "mapgen_functions.h"
#include "mapgen_parameter.h"
#include "mapgendata.h"
#include "omdata.h"
#include "options_helpers.h"
#include "overmapbuffer.h"
#include "point.h"
#include "start_location.h"
#include "type_id.h"

static const item_group_id Item_spawn_data_test_audit_two_hammers( "test_audit_two_hammers" );

static const itype_id itype_hammer( "hammer" );

static const oter_str_id oter_field( "field" );

static const overmap_special_id overmap_special_natural_spring( "natural_spring" );

static const start_location_id start_location_audit_test_sloc( "audit_test_sloc" );
static const start_location_id start_location_sloc_field( "sloc_field" );

static const ter_str_id ter_t_floor( "t_floor" );
static const ter_str_id ter_t_linoleum_gray( "t_linoleum_gray" );
static const ter_str_id ter_t_linoleum_white( "t_linoleum_white" );

static const update_mapgen_id update_mapgen_test_audit_place_hammer( "test_audit_place_hammer" );
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

static bool mentions( const std::string_view messages, const std::string_view what )
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
// were never validated. (Review: the chat topic check knew only JSON topics, so it reported
// the ones dialogue answers in C++, such as TALK_DONE, as unknown.)
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
                "eocs": [ "EOC_CAFFEINE_ADDICTION" ],
                "chat_topics": [ "TALK_CIVILIAN_PANIC", "TALK_DONE", "TALK_TRAIN",
                    "TALK_MISSION_OFFER" ] } ],
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

static int hammers_at( const tripoint_bub_ms &p )
{
    int count = 0;
    for( const item &it : get_map().i_at( p ) ) {
        if( it.typeId() == itype_hammer ) {
            count++;
        }
    }
    return count;
}

// Above a rate of 1, place_items re-rolled the whole group for every item and added the
// same-category results rate more times: 2 hammers at rate 2 gave 10 instead of 4. And only
// "items"/"place_items" applied the rates; "item"/"place_item", loot, liquids, corpses and
// vehicle cargo ignored them.
TEST_CASE( "mgen-b05_item_category_spawn_rate_scales_each_item_once", "[audit][mapgen]" )
{
    clear_map();
    override_option spawn_rate( "ITEM_SPAWNRATE", "1" );
    map &here = get_map();
    const item_category &tools = item( itype_hammer ).get_category_of_contents();
    on_out_of_scope reset_rate( [&tools]() {
        tools.set_spawn_rate( 1.0f );
    } );
    const tripoint_bub_ms group_spot( 30, 30, 0 );
    // test_audit_place_hammer places one hammer at ( 5, 5 )
    const tripoint_bub_ms item_spot( 5, 5, 0 );
    const auto place_hammer = [&here]() {
        mapgendata md( here, mapgendata::dummy_settings );
        run_mapgen_update_func( update_mapgen_test_audit_place_hammer, md, false );
    };

    SECTION( "rate 2 doubles every item" ) {
        tools.set_spawn_rate( 2.0f );
        here.place_items( Item_spawn_data_test_audit_two_hammers, 100, group_spot, group_spot, true,
                          calendar::start_of_cataclysm );
        CHECK( hammers_at( group_spot ) == 4 );
        place_hammer();
        CHECK( hammers_at( item_spot ) == 2 );
    }
    SECTION( "rate 0 removes every item" ) {
        tools.set_spawn_rate( 0.0f );
        here.place_items( Item_spawn_data_test_audit_two_hammers, 100, group_spot, group_spot, true,
                          calendar::start_of_cataclysm );
        CHECK( hammers_at( group_spot ) == 0 );
        place_hammer();
        CHECK( hammers_at( item_spot ) == 0 );
    }
    SECTION( "rate 1 changes nothing" ) {
        here.place_items( Item_spawn_data_test_audit_two_hammers, 100, group_spot, group_spot, true,
                          calendar::start_of_cataclysm );
        CHECK( hammers_at( group_spot ) == 2 );
        place_hammer();
        CHECK( hammers_at( item_spot ) == 1 );
    }
}

// setup() moved mapgens with a variable or math weight out of the weighted list that
// finalize_parameters, check and get_mapgen_params iterate, so they were never finalized or
// checked and their overmap_special parameters never reached the specials using them.
TEST_CASE( "mgen-b06_variable_weight_mapgen_reports_its_special_parameters", "[audit][mapgen]" )
{
    const mapgen_parameters params = get_map_special_params( "test_audit_variable_weight_omt" );
    CHECK( params.map.count( "audit_special_param" ) == 1 );
}

// start_location::set_parameters raised overmap_buffer.externally_set_args before validating
// anything, so a bad parameter left the flag up with no arguments; the next special generated
// then called value() on its empty arguments and threw bad_optional_access.
TEST_CASE( "mgen-b07_start_location_parameters_cannot_break_mapgen", "[audit][mapgen]" )
{
    clear_map();
    on_out_of_scope reset_flag( []() {
        overmap_buffer.externally_set_args = false;
    } );
    overmap_buffer.externally_set_args = false;
    const tripoint_abs_omt spring_omt = project_to<coords::omt>( get_map().get_abs_sub() ) +
                                        tripoint_rel_omt( 20, 20, 0 );
    overmap_buffer.place_special( *overmap_special_natural_spring, spring_omt,
                                  om_direction::type::north, false, true );
    REQUIRE( overmap_buffer.overmap_special_at( spring_omt ) == overmap_special_natural_spring );
    std::optional<mapgen_arguments> *args = overmap_buffer.mapgen_args( spring_omt );
    REQUIRE( args != nullptr );
    REQUIRE( !args->has_value() );

    SECTION( "a parameter that fails validation sets nothing" ) {
        const std::unordered_map<std::string, std::string> bad_params = {
            { "audit_no_such_param", "x" }
        };
        const std::string messages = capture_debugmsg_during( [&spring_omt, &bad_params]() {
            start_location_sloc_field->set_parameters( spring_omt, bad_params );
        } );
        CHECK( mentions( messages, "audit_no_such_param" ) );
        CHECK_FALSE( overmap_buffer.externally_set_args );
        CHECK_FALSE( args->has_value() );
    }
    SECTION( "another special generated while the flag is up gets its own arguments" ) {
        // As if a start location had set some arguments of a special not generated yet
        overmap_buffer.externally_set_args = true;
        CHECK_NOTHROW( mapgendata( spring_omt, get_map(), 0.0f, calendar::turn, nullptr ) );
        CHECK( args->has_value() );
        // The flag stays up for the special whose arguments were set
        CHECK( overmap_buffer.externally_set_args );
    }
}

// start_location::check() didn't look at "parameters", so typos only showed at game start.
TEST_CASE( "mgen-b07_start_location_check_validates_parameters", "[audit][mapgen]" )
{
    const auto check_messages = []( const std::string & parameters ) {
        const JsonObject jo = json_loader::from_string( R"({ "name": "audit start",
            "terrain": [ { "om_terrain": "shelter", "parameters": )" + parameters + "} ] }" );
        jo.allow_omitted_members();
        start_location sloc;
        sloc.id = start_location_audit_test_sloc;
        sloc.load( jo, "" );
        return capture_debugmsg_during( [&sloc]() {
            sloc.check();
        } );
    };
    const std::string unknown = check_messages( R"({ "audit_no_such_param": "x" })" );
    CAPTURE( unknown );
    CHECK( mentions( unknown, "\"audit_no_such_param\"" ) );
    const std::string bad_value = check_messages( R"({ "shelter_palette": "audit_no_palette" })" );
    CAPTURE( bad_value );
    CHECK( mentions( bad_value, "\"audit_no_palette\"" ) );
    const std::string valid = check_messages( R"({ "shelter_palette": "shelter_basic" })" );
    CAPTURE( valid );
    CHECK( valid.empty() );
}
