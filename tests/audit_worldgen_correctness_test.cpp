#include <functional>
#include <initializer_list>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "avatar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character_id.h"
#include "city.h"
#include "coordinates.h"
#include "debug.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "global_vars.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_extras.h"
#include "map_helpers.h"
#include "mapbuffer.h"
#include "mapgen_functions.h"
#include "mapgendata.h"
#include "math_parser_diag_value.h"
#include "npc.h"
#include "omdata.h"
#include "overmap.h"
#include "overmapbuffer.h"
#include "player_helpers.h"
#include "point.h"
#include "ret_val.h"
#include "submap.h"
#include "type_id.h"

static const overmap_special_id overmap_special_test_twelve_mutable( "test_twelve_mutable" );

static const ter_str_id ter_t_floor( "t_floor" );

static const update_mapgen_id update_mapgen_test_twelve_npc_z( "test_twelve_npc_z" );

TEST_CASE( "worldgen_exit_callbacks_belong_to_the_terrain_left", "[audit][worldgen][eoc]" )
{
    clear_avatar();
    clear_map_without_vision();
    get_globals().clear_global_values();
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_abs_ms old_pos = u.pos_abs();
    const tripoint_abs_ms new_pos = old_pos + tripoint_rel_ms( 24, 0, 0 );
    const tripoint_abs_omt old_omt = project_to<coords::omt>( old_pos );
    const tripoint_abs_omt new_omt = project_to<coords::omt>( new_pos );
    const oter_id saved_old = overmap_buffer.ter( old_omt );
    const oter_id saved_new = overmap_buffer.ter( new_omt );
    const on_out_of_scope restore( [&]() {
        u.setpos( old_pos );
        overmap_buffer.ter_set( old_omt, saved_old );
        overmap_buffer.ter_set( new_omt, saved_new );
    } );
    overmap_buffer.ter_set( old_omt, oter_id( "test_twelve_exit_a" ) );
    const bool destination_has_exit = GENERATE( false, true );
    overmap_buffer.ter_set( new_omt, oter_id( destination_has_exit ? "test_twelve_exit_b" : "field" ) );
    u.setpos( new_pos );
    cata_event_dispatch::avatar_moves( old_pos, u, here );
    CHECK( get_globals().get_global_value( "twelve_exit_a" ) == 1 );
    CHECK_FALSE( get_globals().maybe_get_global_value( "twelve_exit_b" ) );
    if( destination_has_exit ) {
        u.setpos( old_pos );
        cata_event_dispatch::avatar_moves( new_pos, u, here );
        CHECK( get_globals().get_global_value( "twelve_exit_b" ) == 1 );
        CHECK( get_globals().get_global_value( "twelve_exit_a" ) == 1 );
    }
}

TEST_CASE( "worldgen_map_extra_overrides_delete_the_requested_flags", "[audit][worldgen][json]" )
{
    map_extra extra;
    extra.load( json_loader::from_string( R"({ "name": "test extra", "description": "flags",
                "flags": [ "keep", "remove" ] })" ), "test" );
    extra.was_loaded = true;
    SECTION( "delete without extend" ) {
        extra.load( json_loader::from_string( R"({ "delete": { "flags": [ "remove" ] } })" ), "test" );
    }
    SECTION( "extend and delete are independent" ) {
        extra.load( json_loader::from_string( R"({ "extend": { "flags": [ "added" ] },
                    "delete": { "flags": [ "remove" ] } })" ), "test" );
        CHECK( extra.has_flag( "added" ) );
    }
    CHECK( extra.has_flag( "keep" ) );
    CHECK_FALSE( extra.has_flag( "remove" ) );
}

TEST_CASE( "worldgen_mapgen_places_NPCs_on_the_requested_z_level",
           "[audit][worldgen][mapgen][npc]" )
{
    clear_avatar();
    clear_map_without_vision( -2, 1 );
    const tripoint_abs_omt origin = get_avatar().pos_abs_omt();
    map &here = get_map();
    const tripoint_abs_ms origin_ms = project_to<coords::ms>( origin );
    for( const int z : {
             -1, 1
             } ) {
        const int offset = z > 0 ? 4 : 5;
        const tripoint_bub_ms destination = here.get_bub( origin_ms + tripoint_rel_ms( offset,
                                            offset, z ) );
        REQUIRE( here.inbounds( destination ) );
        here.ter_set( destination, ter_t_floor );
    }
    std::vector<character_id> created;
    const on_out_of_scope cleanup( [&]() {
        for( const character_id id : created ) {
            overmap_buffer.remove_npc( id );
        }
        g->unique_npc_despawn( "test_twelve_npc_above" );
        g->unique_npc_despawn( "test_twelve_npc_below" );
    } );
    REQUIRE( run_mapgen_update_func( update_mapgen_test_twelve_npc_z, origin,
                                     {}, nullptr, false ).success() );
    for( const int z : {
             -1, 1
             } ) {
        const std::string name = z > 0 ? "test_twelve_npc_above" : "test_twelve_npc_below";
        npc *person = g->find_npc_by_unique_id( name );
        REQUIRE( person != nullptr );
        created.push_back( person->getID() );
        CHECK( person->pos_abs().z() == origin.z() + z );
        std::ostringstream saved;
        JsonOut jsout( saved );
        person->serialize( jsout );
        npc restored;
        restored.deserialize( json_loader::from_string( saved.str() ) );
        CHECK( restored.pos_abs() == person->pos_abs() );
    }
}

TEST_CASE( "worldgen_mutable_special_protects_every_piece_of_a_chunk",
           "[audit][worldgen][overmap]" )
{
    clear_avatar();
    clear_map_without_vision();
    const point_abs_om distant = project_to<coords::om>( get_avatar().pos_abs_omt().xy() ) +
                                 point_rel_om( 4, 4 );
    overmap &om = overmap_buffer.get( distant );
    const tripoint_om_omt root( 80, 80, 0 );
    const tripoint_om_omt end( 80, 79, 0 );
    const tripoint_om_omt protected_piece( 81, 79, 0 );
    for( const tripoint_om_omt &p : {
             root, end, protected_piece
         } ) {
        om.ter_set( p, oter_id( "field" ) );
    }
    std::unique_ptr<submap> generated = std::make_unique<submap>();
    MAPBUFFER.add_submap( project_to<coords::sm>( project_combine( distant, protected_piece ) ),
                          generated );
    REQUIRE( om.is_omt_generated( protected_piece ) );
    const on_out_of_scope cleanup( []() {
        MAPBUFFER.clear_outside_reality_bubble();
    } );
    const bool protect = GENERATE( false, true );
    special_placement_result result;
    const std::string message = capture_debugmsg_during( [&]() {
        result = overmap_special_test_twelve_mutable->place( om, root,
                 om_direction::type::north, city(), protect );
    } );
    if( protect ) {
        CHECK( result.omts_used.size() == 1 );
        CHECK( om.ter( end ) == oter_id( "field" ) );
        CHECK( om.ter( protected_piece ) == oter_id( "field" ) );
        CHECK( !message.empty() );
    } else {
        CHECK( result.omts_used.size() == 3 );
        CHECK( om.ter( end ) == oter_id( "forest" ) );
        CHECK( om.ter( protected_piece ) == oter_id( "forest" ) );
        CHECK( message.empty() );
    }
}
