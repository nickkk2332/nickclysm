#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "activity_actor_definitions.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "condition.h"
#include "coordinates.h"
#include "debug.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "json_error.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "npc.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "point.h"
#include "talker.h"
#include "type_id.h"

// u_examine.

static const activity_id ACT_CLOSE_TILE( "ACT_CLOSE_TILE" );

static const furn_str_id furn_f_safe_c( "f_safe_c" );
static const furn_str_id furn_f_safe_o( "f_safe_o" );
static const furn_str_id furn_f_workbench( "f_workbench" );
static const furn_str_id furn_test_f_eoc( "test_f_eoc" );

static const ter_str_id ter_t_door_c( "t_door_c" );
static const ter_str_id ter_t_door_o( "t_door_o" );

namespace
{

talk_effect_t load_effect( const std::string &effect )
{
    const JsonObject jo = json_loader::from_string( R"({ "effect": )" + effect + " }" );
    jo.allow_omitted_members();
    talk_effect_t ret( jo, "effect", "eoc_errands_ui_test" );
    finalize_conditions();
    return ret;
}

std::string global_string( const std::string &name )
{
    const diag_value *const value = get_globals().maybe_get_global_value( name );
    return value == nullptr ? std::string() : value->str();
}

npc &setup()
{
    clear_map();
    clear_avatar();
    get_globals().clear_global_values();
    set_time( calendar::turn_zero + 12_hours );
    avatar &u = get_avatar();
    npc &guy = spawn_npc( u.pos_bub().xy() + point::east, "test_talker" );
    clear_character( guy );
    return guy;
}

void teardown()
{
    get_globals().clear_global_values();
    clear_map();
    clear_avatar();
}

// `who` examines `pos`, with the test's true_eocs and false_eocs.
void examine( Character &who, const tripoint_bub_ms &pos )
{
    get_globals().clear_global_values();
    dialogue d( get_talker_for( who ), nullptr );
    d.set_value( "spot", get_map().get_abs( pos ) );
    load_effect( R"({ "u_examine": { "context_val": "spot" }, )"
                 R"("true_eocs": [ "EOC_TEST_ERRAND_STARTED" ], )"
                 R"("false_eocs": [ "EOC_TEST_ERRAND_REFUSED" ] })" ).apply( d );
}

std::string result()
{
    return global_string( "test_errand_result" );
}

std::string reason()
{
    return global_string( "test_errand_reason" );
}

} // namespace

TEST_CASE( "eoc_examine_by_an_npc", "[eoc][iexamine][errands_ui]" )
{
    npc &guy = setup();
    map &here = get_map();
    const tripoint_bub_ms spot = guy.pos_bub() + tripoint_rel_ms( 1, 0, 0 );

    SECTION( "furniture whose examine action is an EOC" ) {
        here.furn_set( spot, furn_test_f_eoc );
        examine( guy, spot );
        CHECK( result() == "started" );
        // EOC_test_f_eoc copies its context into globals.
        CHECK( global_string( "this" ) == "test_f_eoc" );
    }
    SECTION( "a door opens, then closes" ) {
        here.ter_set( spot, ter_t_door_c );
        examine( guy, spot );
        CHECK( result() == "started" );
        CHECK( here.ter( spot ) == ter_t_door_o );
        examine( guy, spot );
        CHECK( result() == "started" );
        REQUIRE( guy.activity.id() == ACT_CLOSE_TILE );
        int guard = 0;
        while( guy.activity.id() == ACT_CLOSE_TILE && ++guard < 1000 ) {
            guy.set_moves( 100 );
            guy.activity.do_turn( guy );
        }
        CHECK( here.ter( spot ) == ter_t_door_c );
    }
    SECTION( "a built-in action that needs no menu: an unlocked safe" ) {
        here.furn_set( spot, furn_f_safe_c );
        examine( guy, spot );
        CHECK( result() == "started" );
        CHECK( here.furn( spot ) == furn_f_safe_o );
    }
    SECTION( "a built-in action with a menu is refused, and no menu opens" ) {
        here.furn_set( spot, furn_f_workbench );
        const std::string msg = capture_debugmsg_during( [&]() {
            examine( guy, spot );
        } );
        CHECK( msg.empty() );
        CHECK( result() == "refused" );
        CHECK_THAT( reason(), Catch::Matchers::Contains( "menu" ) );
        CHECK( here.furn( spot ) == furn_f_workbench );
    }
    SECTION( "nothing to examine" ) {
        examine( guy, spot );
        CHECK( result() == "refused" );
        CHECK_THAT( reason(), Catch::Matchers::Contains( "nothing to do" ) );
    }
    SECTION( "a tile out of reach" ) {
        const tripoint_bub_ms far = guy.pos_bub() + tripoint_rel_ms( 3, 0, 0 );
        here.furn_set( far, furn_test_f_eoc );
        examine( guy, far );
        CHECK( result() == "refused" );
        CHECK_THAT( reason(), Catch::Matchers::Contains( "out of reach" ) );
        CHECK( global_string( "this" ).empty() );
    }
    teardown();
}

TEST_CASE( "eoc_examine_by_the_avatar", "[eoc][iexamine][errands_ui]" )
{
    setup();
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms spot = u.pos_bub() + tripoint_rel_ms( 0, 1, 0 );

    SECTION( "the normal examine runs" ) {
        here.furn_set( spot, furn_test_f_eoc );
        examine( u, spot );
        CHECK( result() == "started" );
        CHECK( global_string( "this" ) == "test_f_eoc" );
    }
    SECTION( "a door opens" ) {
        here.ter_set( spot, ter_t_door_c );
        examine( u, spot );
        CHECK( result() == "started" );
        CHECK( here.ter( spot ) == ter_t_door_o );
    }
    SECTION( "the other talker's tile" ) {
        npc &guy = spawn_npc( spot.xy(), "test_talker" );
        here.furn_set( spot, furn_test_f_eoc );
        dialogue d( get_talker_for( u ), get_talker_for( guy ) );
        load_effect( R"({ "u_examine": "npc" })" ).apply( d );
        CHECK( global_string( "this" ) == "test_f_eoc" );
    }
    CHECK_THROWS_AS( load_effect( R"({ "u_examine": "u" })" ), JsonError );
    teardown();
}
