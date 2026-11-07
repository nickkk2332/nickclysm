#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "cata_variant.h"
#include "condition.h"
#include "coordinates.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "enums.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_extras.h"
#include "map_helpers.h"
#include "map_scale_constants.h"
#include "mapbuffer.h"
#include "mapgen_functions.h"
#include "mapgen_parameter.h"
#include "mapgendata.h"
#include "math_parser_diag_value.h"
#include "overmapbuffer.h"
#include "player_helpers.h"
#include "point.h"
#include "regional_settings.h"
#include "ret_val.h"
#include "talker.h"
#include "timed_event.h"
#include "type_id.h"
#include "units.h"
#include "vehicle.h"
#include "vpart_position.h"
#include "weighted_list.h"

static const furn_str_id furn_f_chair( "f_chair" );

static const map_extra_id map_extra_test_audit_m08a_mx( "test_audit_m08a_mx" );

static const oter_str_id oter_field( "field" );
static const oter_str_id oter_forest( "forest" );
static const oter_str_id oter_river_north( "river_north" );

static const ter_str_id ter_t_clay( "t_clay" );
static const ter_str_id ter_t_dirt( "t_dirt" );
static const ter_str_id ter_t_floor( "t_floor" );
static const ter_str_id ter_t_grass( "t_grass" );
static const ter_str_id ter_t_linoleum_gray( "t_linoleum_gray" );
static const ter_str_id ter_t_linoleum_white( "t_linoleum_white" );
static const ter_str_id ter_t_sand( "t_sand" );
static const ter_str_id ter_t_water_moving_dp( "t_water_moving_dp" );
static const ter_str_id ter_t_water_moving_sh( "t_water_moving_sh" );

static const update_mapgen_id update_mapgen_test_audit_m08a_place_eoc(
    "test_audit_m08a_place_eoc" );
static const update_mapgen_id update_mapgen_test_audit_m08a_place_turned(
    "test_audit_m08a_place_turned" );
static const update_mapgen_id update_mapgen_test_audit_m08a_variable_range(
    "test_audit_m08a_variable_range" );
static const update_mapgen_id update_mapgen_test_audit_queue_point( "test_audit_queue_point" );

static const vproto_id vehicle_prototype_test_shopping_cart( "test_shopping_cart" );

namespace
{
// Collects the events sent while it lives (its base class unsubscribes it)
struct audit_event_catcher : event_subscriber {
    std::vector<cata::event> events;
    audit_event_catcher() {
        get_event_bus().subscribe( this );
    }
    using event_subscriber::notify;
    void notify( const cata::event &e ) override {
        events.push_back( e );
    }
};

talk_effect_t load_m08a_effect( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    return talk_effect_t( jo, "effect", "audit_m_m08a_test" );
}

std::string m08a_saved_timed_events()
{
    std::ostringstream os;
    JsonOut jsout( os );
    timed_event_manager::serialize_all( jsout );
    return os.str();
}

void m08a_load_timed_events( const std::string &saved )
{
    get_timed_events() = timed_event_manager();
    timed_event_manager::unserialize_all( json_loader::from_string( saved ).get_array() );
}

// An overmap tile west or north of the reality bubble, as the MGEN-B04 test uses
tripoint_abs_omt m08a_omt_outside_bubble( const point &offset )
{
    return project_to<coords::omt>( get_map().get_abs_sub() ) + tripoint_rel_omt( offset.x, offset.y,
            0 );
}

// The terrain at a tile of an overmap tile, loaded on its own
ter_id m08a_ter_at( const tripoint_abs_omt &omt, const point &p )
{
    tinymap tm;
    tm.load( omt, false );
    return tm.ter( tripoint_omt_ms( p.x, p.y, 0 ) );
}

// Clears the avatar's EOC queue, and again when it goes
struct m08a_eoc_queue_cleaner {
    m08a_eoc_queue_cleaner() {
        effect_on_conditions::clear( get_avatar() );
    }
    ~m08a_eoc_queue_cleaner() {
        effect_on_conditions::clear( get_avatar() );
    }
};
} // namespace

// The nested mapgen "rotation" was read and never used. A chunk that sets "allow_rotation"
// now turns, with what it places, its setmaps and the chunks it places; one that doesn't is
// placed as written, as before.
TEST_CASE( "mgen-b08_nested_chunk_with_allow_rotation_turns", "[audit][mapgen]" )
{
    clear_map();
    map &here = get_map();
    mapgendata md( here, mapgendata::dummy_settings );
    REQUIRE( run_mapgen_update_func( update_mapgen_test_audit_m08a_place_turned, md,
                                     false ).success() );

    // test_audit_m08a_turned: 3x3 at ( 5, 5 ), one quarter turn: ( 0, 0 ) goes to ( 2, 0 ) and
    // ( 1, 0 ) to ( 2, 1 )
    CHECK( here.ter( tripoint_bub_ms( 7, 5, 0 ) ) == ter_t_linoleum_white );
    CHECK( here.furn( tripoint_bub_ms( 7, 6, 0 ) ) == furn_f_chair );
    CHECK( here.ter( tripoint_bub_ms( 5, 5, 0 ) ) == ter_t_grass );
    CHECK( here.furn( tripoint_bub_ms( 6, 5, 0 ) ) != furn_f_chair );

    // test_audit_m08a_not_turned asks for a rotation without allowing it: as written
    CHECK( here.ter( tripoint_bub_ms( 10, 5, 0 ) ) == ter_t_linoleum_white );

    // test_audit_m08a_turned_outer: 4x4 at ( 15, 5 ), one quarter turn, places the 2x2
    // test_audit_m08a_inner at its ( 0, 0 ). The inner chunk's ( 0, 0 ) and ( 1, 0 ) are the
    // outer chunk's, which turn to ( 3, 0 ) and ( 3, 1 ).
    CHECK( here.ter( tripoint_bub_ms( 18, 5, 0 ) ) == ter_t_linoleum_gray );
    CHECK( here.ter( tripoint_bub_ms( 18, 6, 0 ) ) == ter_t_linoleum_white );
    CHECK( here.ter( tripoint_bub_ms( 15, 5, 0 ) ) == ter_t_grass );
    CHECK( here.ter( tripoint_bub_ms( 16, 5, 0 ) ) == ter_t_grass );
    clear_map();
}

// The top mapgen was finalized before its nested chunks, so it never got the parameters a
// chunk nested in a nested chunk declares; and a "null" choice ended the merge of the choices
// after it.
TEST_CASE( "mgen-b09_parameters_of_nested_in_nested_chunks_reach_the_top", "[audit][mapgen]" )
{
    const mapgen_parameters params = get_map_special_params( "test_audit_m08a_nest_omt" );
    // test_audit_m08a_nest_c, placed by _b, placed by _a, placed by the OMT's mapgen
    CHECK( params.map.count( "audit_m08a_grandchild_floor" ) == 1 );
    // test_audit_m08a_nest_after_null comes after "null" in one choice
    CHECK( params.map.count( "audit_m08a_after_null_floor" ) == 1 );
}

// map::mirror moved terrain but not vehicles (nor their caches, spawns, zones, NPCs or
// queued points), so a mirrored update checked vehicle collisions on the wrong side.
TEST_CASE( "mgen-b10_mirror_moves_vehicles_and_their_caches", "[audit][mapgen]" )
{
    clear_map();
    clear_vehicles();
    on_out_of_scope cleanup( []() {
        clear_vehicles();
        clear_map();
    } );
    map &here = get_map();
    const tripoint_bub_ms spot( 3, 5, 0 );
    const tripoint_bub_ms mirrored_east_west( SEEX * 2 - 1 - 3, 5, 0 );
    vehicle *cart = here.add_vehicle( vehicle_prototype_test_shopping_cart, spot, 0_degrees, 0,
                                      veh_spawn_status::UNDAMAGED );
    REQUIRE( cart != nullptr );
    REQUIRE( here.veh_at( spot ).has_value() );

    here.mirror( true, false );
    const optional_vpart_position moved = here.veh_at( mirrored_east_west );
    REQUIRE( moved.has_value() );
    CHECK( &moved->vehicle() == cart );
    CHECK_FALSE( here.veh_at( spot ).has_value() );
    CHECK( cart->pos_bub( here ) == mirrored_east_west );
    CHECK( to_degrees( cart->turn_dir ) == Approx( 180.0 ) );

    // Mirroring again gives back the vehicle as it was
    here.mirror( true, false );
    CHECK( here.veh_at( spot ).has_value() );
    CHECK_FALSE( here.veh_at( mirrored_east_west ).has_value() );
    CHECK( to_degrees( cart->turn_dir ) == Approx( 0.0 ) );

    // North and south swap: a vehicle facing south faces north
    here.mirror( false, true );
    const tripoint_bub_ms mirrored_north_south( 3, SEEY * 2 - 1 - 5, 0 );
    CHECK( here.veh_at( mirrored_north_south ).has_value() );
    here.mirror( false, true );
    REQUIRE( here.veh_at( spot ).has_value() );
}

TEST_CASE( "mgen-b10_mirrored_update_mirrors_its_queued_points", "[audit][mapgen]" )
{
    clear_map();
    set_queued_points();
    global_variables &globals = get_globals();
    on_out_of_scope cleanup( [&globals]() {
        globals.remove_global_value( "audit_queued_point" );
    } );
    const tripoint_abs_omt omt = m08a_omt_outside_bubble( point( -3, -6 ) );
    overmap_buffer.ter_set( omt, oter_field.id() );

    // test_audit_queue_point queues ( 2, 3 ); mirrored east-west it is ( 21, 3 )
    run_mapgen_update_func( update_mapgen_test_audit_queue_point, omt, {}, nullptr, false, true,
                            false, 0 );
    set_queued_points();
    const diag_value *point_value = globals.maybe_get_global_value( "audit_queued_point" );
    REQUIRE( point_value != nullptr );
    CHECK( point_value->tripoint() == project_to<coords::ms>( omt ) + tripoint_rel_ms( 21, 3, 0 ) );
}

// Mapgen had no way to run an EOC: "place_eocs" queues one with its place and the overmap
// terrain, run with the next queued EOCs.
TEST_CASE( "mgen-j01_place_eocs_runs_its_eoc_with_the_place", "[audit][mapgen][eoc]" )
{
    clear_avatar();
    clear_map();
    m08a_eoc_queue_cleaner clean_queue;
    global_variables &globals = get_globals();
    on_out_of_scope cleanup( [&globals]() {
        globals.remove_global_value( "audit_m08a_eoc_loc" );
        globals.remove_global_value( "audit_m08a_eoc_oter" );
    } );
    const tripoint_abs_omt omt = m08a_omt_outside_bubble( point( -6, -3 ) );
    overmap_buffer.ter_set( omt, oter_field.id() );

    run_mapgen_update_func( update_mapgen_test_audit_m08a_place_eoc, omt, {}, nullptr, false );
    // Nothing runs during map generation
    CHECK( globals.maybe_get_global_value( "audit_m08a_eoc_loc" ) == nullptr );
    set_queued_points();
    effect_on_conditions::process_effect_on_conditions( get_avatar() );

    const diag_value *loc = globals.maybe_get_global_value( "audit_m08a_eoc_loc" );
    REQUIRE( loc != nullptr );
    CHECK( loc->tripoint() == project_to<coords::ms>( omt ) + tripoint_rel_ms( 3, 4, 0 ) );
    const diag_value *oter = globals.maybe_get_global_value( "audit_m08a_eoc_oter" );
    REQUIRE( oter != nullptr );
    CHECK( oter->str() == "field" );
}

// place_variables set its variable to the low end of an x or y range, unlike every other piece
TEST_CASE( "mgen-j01_place_variables_rolls_its_range", "[audit][mapgen]" )
{
    clear_map();
    set_queued_points();
    global_variables &globals = get_globals();
    on_out_of_scope cleanup( [&globals]() {
        globals.remove_global_value( "audit_m08a_ranged_point" );
    } );
    const tripoint_abs_omt omt = m08a_omt_outside_bubble( point( -6, -6 ) );
    overmap_buffer.ter_set( omt, oter_field.id() );
    const tripoint_abs_ms origin = project_to<coords::ms>( omt );

    bool rolled_above_low_end = false;
    for( int i = 0; i < 40; ++i ) {
        run_mapgen_update_func( update_mapgen_test_audit_m08a_variable_range, omt, {}, nullptr, false );
        set_queued_points();
        const diag_value *value = globals.maybe_get_global_value( "audit_m08a_ranged_point" );
        REQUIRE( value != nullptr );
        // x is [ 3, 5 ], y is 7
        const tripoint_rel_ms local = value->tripoint() - origin;
        CHECK( local.x() >= 3 );
        CHECK( local.x() <= 5 );
        CHECK( local.y() == 7 );
        rolled_above_low_end |= local.x() > 3;
    }
    CHECK( rolled_above_low_end );
}

// Nothing told EOCs that an overmap tile was generated: omt_generated is sent, once map
// generation is over (at the start of the next turn).
TEST_CASE( "mgen-j01_omt_generated_is_sent_after_generation", "[audit][mapgen][event]" )
{
    clear_map();
    // A tile near the bubble that no test has generated yet
    std::optional<tripoint_abs_omt> fresh;
    const tripoint_abs_omt bubble_omt = project_to<coords::omt>( get_map().get_abs_sub() );
    for( int dy = 12; dy <= 20 && !fresh; dy += 2 ) {
        for( int dx = -20; dx <= 20 && !fresh; dx += 2 ) {
            const tripoint_abs_omt omt = bubble_omt + tripoint_rel_omt( dx, dy, 0 );
            if( !MAPBUFFER.submap_exists( project_to<coords::sm>( omt ) ) ) {
                fresh = omt;
            }
        }
    }
    if( !fresh ) {
        WARN( "every overmap tile tried was generated already; skipped" );
        return;
    }
    overmap_buffer.ter_set( *fresh, oter_field.id() );
    send_queued_mapgen_events();
    audit_event_catcher catcher;
    {
        tinymap tm;
        tm.load( *fresh, false );
    }
    // Queued, not sent, while the map is made
    for( const cata::event &e : catcher.events ) {
        CHECK( e.type() != event_type::omt_generated );
    }
    send_queued_mapgen_events();

    const tripoint_abs_ms location = project_to<coords::ms>( *fresh );
    bool found = false;
    for( const cata::event &e : catcher.events ) {
        if( e.type() == event_type::omt_generated &&
            e.get<cata_variant_type::tripoint>( "location" ) == location.raw() ) {
            found = true;
            CHECK( e.get<cata_variant_type::oter_id>( "oter_id" ) == oter_field.id() );
        }
    }
    CHECK( found );
}

// A map extra's "eocs" run once it is placed, and map_extra_spawned is sent
TEST_CASE( "mgen-j01_map_extra_runs_its_eocs_and_sends_its_event", "[audit][mapgen][eoc]" )
{
    clear_avatar();
    clear_map();
    m08a_eoc_queue_cleaner clean_queue;
    global_variables &globals = get_globals();
    const tripoint_abs_omt omt = m08a_omt_outside_bubble( point( -9, -3 ) );
    on_out_of_scope cleanup( [&globals, omt]() {
        globals.remove_global_value( "audit_m08a_mx_loc" );
        globals.remove_global_value( "audit_m08a_mx_id" );
        overmap_buffer.delete_extra( omt );
    } );
    overmap_buffer.ter_set( omt, oter_field.id() );
    {
        // Made now if it never was, with whatever extra the field rolls: not counted below
        tinymap tm;
        tm.load( omt, false );
    }
    send_queued_mapgen_events();
    audit_event_catcher catcher;
    {
        tinymap tm;
        tm.load( omt, false );
        MapExtras::apply_function( map_extra_test_audit_m08a_mx, tm, omt );
    }
    CHECK( m08a_ter_at( omt, point::zero ) == ter_t_linoleum_gray );
    CHECK( overmap_buffer.extra( omt ) == map_extra_test_audit_m08a_mx );

    send_queued_mapgen_events();
    const tripoint_abs_ms location = project_to<coords::ms>( omt );
    int spawned = 0;
    for( const cata::event &e : catcher.events ) {
        if( e.type() == event_type::map_extra_spawned &&
            e.get<cata_variant_type::tripoint>( "location" ) == location.raw() &&
            e.get<cata_variant_type::string>( "map_extra" ) == map_extra_test_audit_m08a_mx.str() ) {
            ++spawned;
        }
    }
    CHECK( spawned == 1 );

    effect_on_conditions::process_effect_on_conditions( get_avatar() );
    const diag_value *loc = globals.maybe_get_global_value( "audit_m08a_mx_loc" );
    REQUIRE( loc != nullptr );
    CHECK( loc->tripoint() == location );
    const diag_value *id = globals.maybe_get_global_value( "audit_m08a_mx_id" );
    REQUIRE( id != nullptr );
    CHECK( id->str() == map_extra_test_audit_m08a_mx.str() );
}

// The EOC mapgen_update passed no arguments, no rotation or mirroring and no offset, and
// ignored what happened; now it takes parameters, a rotation, mirroring, an anchor on the
// target tile and success/failure EOCs.
TEST_CASE( "mgen-j02_mapgen_update_takes_parameters_rotation_and_an_anchor",
           "[audit][mapgen][eoc]" )
{
    clear_avatar();
    clear_map();
    global_variables &globals = get_globals();
    const tripoint_abs_omt omt = m08a_omt_outside_bubble( point( -9, -6 ) );
    on_out_of_scope cleanup( [&globals]() {
        globals.remove_global_value( "audit_m08a_target" );
        globals.remove_global_value( "audit_m08a_update_result" );
        globals.remove_global_value( "audit_m08a_update_reason" );
    } );
    overmap_buffer.ter_set( omt, oter_field.id() );
    globals.set_global_value( "audit_m08a_target",
                              project_to<coords::ms>( omt ) + tripoint_rel_ms( 5, 2, 0 ) );

    const talk_effect_t effect = load_m08a_effect( R"({ "effect": {
        "mapgen_update": "test_audit_m08a_mark_origin",
        "target_var": { "global_val": "audit_m08a_target" },
        "offset_from_target": true,
        "ignore_vehicle_collision": true,
        "rotation": 1,
        "parameters": { "audit_m08a_mark": "t_linoleum_gray" },
        "success_eocs": [ "EOC_test_audit_m08a_update_ok" ]
    } })" );
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    effect.apply( d );

    // The update's ( 0, 0 ) lands on the target, ( 1, 0 ) a quarter turn on: south of it
    CHECK( m08a_ter_at( omt, point( 5, 2 ) ) == ter_t_linoleum_gray );
    CHECK( m08a_ter_at( omt, point( 5, 3 ) ) == ter_t_floor );
    const diag_value *result = globals.maybe_get_global_value( "audit_m08a_update_result" );
    REQUIRE( result != nullptr );
    CHECK( result->str() == "test_audit_m08a_mark_origin" );

    const talk_effect_t missing = load_m08a_effect( R"({ "effect": {
        "mapgen_update": "test_audit_m08a_no_such_update",
        "target_var": { "global_val": "audit_m08a_target" },
        "failure_eocs": [ "EOC_test_audit_m08a_update_failed" ]
    } })" );
    missing.apply( d );
    const diag_value *reason = globals.maybe_get_global_value( "audit_m08a_update_reason" );
    REQUIRE( reason != nullptr );
    CHECK( reason->str() == "no_update" );
}

// The delayed update (timed event) dropped everything but the id; it keeps the settings,
// through a save and load too
TEST_CASE( "mgen-j02_delayed_mapgen_update_keeps_its_settings", "[audit][mapgen][timed_event]" )
{
    const std::string saved = m08a_saved_timed_events();
    on_out_of_scope restore( [saved]() {
        m08a_load_timed_events( saved );
    } );
    get_timed_events() = timed_event_manager();
    timed_mapgen_update how;
    how.parameters["audit_m08a_mark"] = "t_linoleum_gray";
    how.rotation = 3;
    how.mirror_vertical = true;
    how.anchor = point_omt_ms( 4, 6 );
    how.cancel_on_collision = false;
    get_timed_events().add_mapgen_update( calendar::turn + 1_hours,
                                          get_avatar().pos_abs(), "test_audit_m08a_mark_origin", "audit_m08a_key", how );

    m08a_load_timed_events( m08a_saved_timed_events() );
    const timed_event *queued = get_timed_events().get( timed_event_type::UPDATE_MAPGEN,
                                "audit_m08a_key" );
    REQUIRE( queued != nullptr );
    const timed_mapgen_update &loaded = queued->mapgen_update;
    CHECK( loaded.parameters.count( "audit_m08a_mark" ) == 1 );
    CHECK( loaded.rotation == 3 );
    CHECK_FALSE( loaded.mirror_horizontal );
    CHECK( loaded.mirror_vertical );
    REQUIRE( loaded.anchor.has_value() );
    CHECK( *loaded.anchor == point_omt_ms( 4, 6 ) );
    CHECK_FALSE( loaded.cancel_on_collision );
}

// A map extra could not depend on world state: "condition" gates it, with the tile's place and
// overmap terrain as context
TEST_CASE( "mgen-j03_map_extra_condition_gates_it", "[audit][mapgen]" )
{
    global_variables &globals = get_globals();
    const tripoint_abs_omt omt = m08a_omt_outside_bubble( point( -12, -3 ) );
    on_out_of_scope cleanup( [&globals]() {
        globals.remove_global_value( "audit_m08a_mx_allowed" );
    } );
    globals.remove_global_value( "audit_m08a_mx_allowed" );
    const map_extra &extra = map_extra_test_audit_m08a_mx.obj();
    map &here = get_map();

    overmap_buffer.ter_set( omt, oter_field.id() );
    {
        const mapgendata md( omt, here, 0.0f, calendar::turn, nullptr );
        CHECK_FALSE( extra.is_valid_for( md ) );
        globals.set_global_value( "audit_m08a_mx_allowed", 1 );
        CHECK( extra.is_valid_for( md ) );
    }
    // Only on fields
    overmap_buffer.ter_set( omt, oter_forest.id() );
    const mapgendata md_forest( omt, here, 0.0f, calendar::turn, nullptr );
    CHECK_FALSE( extra.is_valid_for( md_forest ) );
    overmap_buffer.ter_set( omt, oter_field.id() );
}

// No EOC could place a map extra or ask whether a tile has one
TEST_CASE( "mgen-j03_place_map_extra_and_map_has_extra", "[audit][mapgen][eoc]" )
{
    clear_avatar();
    clear_map();
    m08a_eoc_queue_cleaner clean_queue;
    global_variables &globals = get_globals();
    const tripoint_abs_omt omt = m08a_omt_outside_bubble( point( -12, -6 ) );
    on_out_of_scope cleanup( [&globals, omt]() {
        globals.remove_global_value( "audit_m08a_mx_target" );
        globals.remove_global_value( "audit_m08a_mx_loc" );
        globals.remove_global_value( "audit_m08a_mx_id" );
        overmap_buffer.delete_extra( omt );
    } );
    overmap_buffer.ter_set( omt, oter_field.id() );
    overmap_buffer.delete_extra( omt );
    globals.set_global_value( "audit_m08a_mx_target", project_to<coords::ms>( omt ) );

    const JsonObject has_extra_jo = json_loader::from_string(
                                        R"({ "condition": { "map_has_extra": "test_audit_m08a_mx", "loc": { "global_val": "audit_m08a_mx_target" } } })" );
    std::function<bool( const_dialogue const & )> has_extra;
    read_condition( has_extra_jo, "condition", has_extra, false );
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    CHECK_FALSE( has_extra( d ) );

    const talk_effect_t effect = load_m08a_effect(
                                     R"({ "effect": { "place_map_extra": "test_audit_m08a_mx", "target_var": { "global_val": "audit_m08a_mx_target" } } })" );
    effect.apply( d );

    CHECK( m08a_ter_at( omt, point::zero ) == ter_t_linoleum_gray );
    CHECK( has_extra( d ) );
    // Its EOCs, queued as after map generation
    effect_on_conditions::process_effect_on_conditions( get_avatar() );
    const diag_value *loc = globals.maybe_get_global_value( "audit_m08a_mx_loc" );
    REQUIRE( loc != nullptr );
    CHECK( loc->tripoint() == project_to<coords::ms>( omt ) );
}

// The built-in river, lake and ocean mapgens drew fixed terrain: a region can now set it, and
// without settings they draw vanilla's
TEST_CASE( "mgen-j04_river_terrain_comes_from_the_region", "[audit][mapgen]" )
{
    const tripoint_abs_omt omt = m08a_omt_outside_bubble( point( -15, -3 ) );
    const oter_id old_ter = overmap_buffer.ter( omt );
    overmap_buffer.ter_set( omt, oter_river_north.id() );
    small_fake_map fake( ter_t_dirt.id() );
    mapgendata md( omt, *fake.cast_to_map(), 0.0f, calendar::turn, nullptr );
    overmap_buffer.ter_set( omt, old_ter );
    REQUIRE( md.region.overmap_river.has_value() );
    map &m = md.m;
    // The middle of a north-south river is deep water; its west edge is the bank
    const tripoint_bub_ms middle( 12, 20, 0 );

    SECTION( "vanilla terrain without region settings" ) {
        mapgen_river_straight( md );
        CHECK( m.ter( middle ) == ter_t_water_moving_dp );
        for( int x = 0; x < SEEX * 2; ++x ) {
            const ter_id bank = m.ter( tripoint_bub_ms( x, 0, 0 ) );
            CHECK( ( bank == ter_t_grass || bank == ter_t_dirt ) );
        }
        bool shallow = false;
        for( int y = 0; y < SEEY * 2; ++y ) {
            const ter_id t = m.ter( tripoint_bub_ms( 12, y, 0 ) );
            CHECK( ( t == ter_t_grass || t == ter_t_dirt || t == ter_t_clay || t == ter_t_sand ||
                     t == ter_t_water_moving_sh || t == ter_t_water_moving_dp ) );
            shallow |= t == ter_t_water_moving_sh;
        }
        CHECK( shallow );
    }
    SECTION( "the region's terrain" ) {
        // The default region's own settings, changed for the test
        region_settings_river &river = const_cast<region_settings_river &>
                                       ( *md.region.overmap_river.value() );
        const region_settings_river saved = river;
        on_out_of_scope restore( [&river, saved]() {
            river = saved;
        } );
        river.mapgen_deep_water = ter_t_linoleum_white;
        river.mapgen_bank = weighted_int_list<ter_id>();
        river.mapgen_bank.add( ter_t_linoleum_gray.id(), 1 );
        mapgen_river_straight( md );
        CHECK( m.ter( middle ) == ter_t_linoleum_white );
        for( int x = 0; x < SEEX * 2; ++x ) {
            CHECK( m.ter( tripoint_bub_ms( x, 0, 0 ) ) == ter_t_linoleum_gray );
        }
    }
}
