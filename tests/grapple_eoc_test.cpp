#include <memory>
#include <optional>
#include <string>

#include "avatar.h"
#include "bodypart.h"
#include "cata_catch.h"
#include "character.h"
#include "condition.h"
#include "coordinates.h"
#include "creature.h"
#include "debug.h"
#include "dialogue.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "grapple.h"
#include "json_error.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "monster.h"
#include "npc.h"
#include "player_helpers.h"
#include "point.h"
#include "talker.h"
#include "type_id.h"

// The EOC surface of grappling (src/grapple.h): the who-holds-whom conditions u_is_grabbing and
// u_is_grabbed.

static const efftype_id effect_grabbed( "grabbed" );
static const efftype_id effect_grabbing( "grabbing" );

static const flag_id json_flag_GRAB( "GRAB" );
static const flag_id json_flag_GRAB_FILTER( "GRAB_FILTER" );

static constexpr tripoint_bub_ms scene_center{ 60, 60, 0 };

static avatar &setup_scene()
{
    clear_map_without_vision();
    clear_creatures();
    clear_avatar();
    get_globals().clear_global_values();
    avatar &you = get_avatar();
    you.setpos( get_map(), scene_center );
    return you;
}

static const bodypart_id &no_part()
{
    static const bodypart_id null_part = bodypart_str_id::NULL_ID().id();
    return null_part;
}

static bool test_condition( const std::string &condition, const Creature &alpha,
                            const Creature &beta )
{
    const JsonObject jo = json_loader::from_string( R"({ "condition": )" + condition + " }" );
    jo.allow_omitted_members();
    conditional_t::func cond;
    read_condition( jo, "condition", cond, false );
    const const_dialogue d( get_const_talker_for( alpha ), get_const_talker_for( beta ) );
    return cond( d );
}

static void load_condition( const std::string &condition )
{
    const JsonObject jo = json_loader::from_string( R"({ "condition": )" + condition + " }" );
    jo.allow_omitted_members();
    conditional_t::func cond;
    read_condition( jo, "condition", cond, false );
}

// Conditions

TEST_CASE( "grapple_eoc_conditions_tell_who_holds_whom", "[grapple][eoc]" )
{
    avatar &you = setup_scene();
    map &here = get_map();
    monster &zed = spawn_test_monster( "mon_zombie", you.pos_bub() + tripoint::east );
    const auto check = [&]( const std::string & condition, const Creature & alpha,
    const Creature & beta ) {
        CAPTURE( condition );
        return test_condition( condition, alpha, beta );
    };

    SECTION( "nobody holds anyone" ) {
        CHECK_FALSE( check( R"({ "u_is_grabbing": "npc" })", you, zed ) );
        CHECK_FALSE( check( R"({ "u_is_grabbing": "any" })", you, zed ) );
        CHECK_FALSE( check( R"({ "u_is_grabbed": "any" })", you, zed ) );
        CHECK_FALSE( check( R"({ "npc_is_grabbed": "u" })", you, zed ) );
    }
    SECTION( "the avatar holds a monster" ) {
        REQUIRE( grapple::start_hold( you, zed, 20, no_part() ) );
        CHECK( check( R"({ "u_is_grabbing": "npc" })", you, zed ) );
        CHECK( check( R"({ "u_is_grabbing": "any" })", you, zed ) );
        CHECK( check( R"({ "npc_is_grabbed": "u" })", you, zed ) );
        CHECK( check( R"({ "npc_is_grabbed": "any" })", you, zed ) );
        CHECK_FALSE( check( R"({ "u_is_grabbed": "npc" })", you, zed ) );
        CHECK_FALSE( check( R"({ "u_is_grabbed": "any" })", you, zed ) );
        CHECK_FALSE( check( R"({ "npc_is_grabbing": "u" })", you, zed ) );
        CHECK_FALSE( check( R"({ "npc_is_grabbing": "any" })", you, zed ) );
        // The monster as u
        CHECK( check( R"({ "u_is_grabbed": "npc" })", zed, you ) );
        CHECK( check( R"({ "npc_is_grabbing": "u" })", zed, you ) );

        AND_WHEN( "a third creature is held by another" ) {
            monster &other = spawn_test_monster( "mon_zombie", zed.pos_bub() + tripoint::east );
            monster &holder = spawn_test_monster( "mon_debug_grabber",
                                                  other.pos_bub() + tripoint::east );
            holder.add_effect( effect_grabbing, 1_days );
            other.add_effect( effect_grabbed, 1_days );
            REQUIRE( grapple::holds( holder, other ) );
            // The flags alone can't tell this apart from holding it
            REQUIRE( test_condition(
                         R"({ "and": [ { "u_has_flag": "GRAB_FILTER" }, { "npc_has_flag": "GRAB" } ] })",
                         you, other ) );
            CHECK_FALSE( check( R"({ "u_is_grabbing": "npc" })", you, other ) );
            CHECK( check( R"({ "u_is_grabbing": "any" })", you, other ) );
            CHECK( check( R"({ "npc_is_grabbed": "any" })", you, other ) );
            CHECK_FALSE( check( R"({ "npc_is_grabbed": "u" })", you, other ) );
            CHECK( check( R"({ "u_is_grabbing": "npc" })", holder, other ) );
        }
        AND_WHEN( "the hold is out of reach, though the effects are left" ) {
            zed.setpos( here, you.pos_bub() + tripoint( 3, 0, 0 ) );
            REQUIRE( you.has_effect_with_flag( json_flag_GRAB_FILTER ) );
            REQUIRE( zed.has_effect_with_flag( json_flag_GRAB ) );
            CHECK_FALSE( check( R"({ "u_is_grabbing": "npc" })", you, zed ) );
            CHECK_FALSE( check( R"({ "u_is_grabbing": "any" })", you, zed ) );
            CHECK_FALSE( check( R"({ "npc_is_grabbed": "any" })", you, zed ) );
        }
    }
    SECTION( "a monster holds the avatar by a limb" ) {
        monster &grabber = spawn_test_monster( "mon_debug_grabber", you.pos_bub() + tripoint::west );
        grabber.add_grab( body_part_arm_l );
        you.add_effect( effect_grabbed, 1_days, body_part_arm_l, true, 20 );
        CHECK( check( R"({ "u_is_grabbed": "npc" })", you, grabber ) );
        CHECK( check( R"({ "u_is_grabbed": "any" })", you, grabber ) );
        CHECK( check( R"({ "npc_is_grabbing": "u" })", you, grabber ) );
        CHECK( check( R"({ "npc_is_grabbing": "any" })", you, grabber ) );
        CHECK( check( R"({ "u_is_grabbing": "npc" })", grabber, you ) );
        // The zombie next to you holds someone else: not you
        monster &dog = spawn_test_monster( "mon_dog", zed.pos_bub() + tripoint::east );
        zed.add_effect( effect_grabbing, 1_days );
        dog.add_effect( effect_grabbed, 1_days );
        REQUIRE( grapple::holds( zed, dog ) );
        REQUIRE( test_condition(
                     R"({ "and": [ { "u_has_flag": "GRAB" }, { "npc_has_flag": "GRAB_FILTER" } ] })",
                     you, zed ) );
        CHECK_FALSE( check( R"({ "u_is_grabbed": "npc" })", you, zed ) );
        CHECK( check( R"({ "npc_is_grabbing": "any" })", you, zed ) );
    }
    SECTION( "an NPC holds the avatar" ) {
        npc &guy = spawn_npc( you.pos_bub().xy() + point::south, "thug" );
        REQUIRE( grapple::start_hold( guy, you, 20, body_part_arm_r.id() ) );
        CHECK( check( R"({ "u_is_grabbed": "npc" })", you, guy ) );
        CHECK( check( R"({ "u_is_grabbed": "any" })", you, guy ) );
        CHECK( check( R"({ "npc_is_grabbing": "u" })", you, guy ) );
        CHECK( check( R"({ "u_is_grabbing": "npc" })", guy, you ) );
        CHECK( check( R"({ "u_is_grabbing": "any" })", guy, you ) );
        CHECK_FALSE( check( R"({ "u_is_grabbed": "npc" })", you, zed ) );
    }
    SECTION( "a talker that is no creature holds nothing" ) {
        const const_dialogue d( std::make_unique<talker>(), get_const_talker_for( zed ) );
        const JsonObject jo = json_loader::from_string(
                                  R"({ "condition": { "u_is_grabbing": "any" } })" );
        conditional_t::func cond;
        read_condition( jo, "condition", cond, false );
        CHECK_FALSE( cond( d ) );
    }
}

TEST_CASE( "grapple_eoc_condition_load_errors", "[grapple][eoc]" )
{
    CHECK_NOTHROW( load_condition( R"({ "u_is_grabbing": "npc" })" ) );
    CHECK_NOTHROW( load_condition( R"({ "u_is_grabbing": "any" })" ) );
    CHECK_NOTHROW( load_condition( R"({ "npc_is_grabbing": "u" })" ) );
    CHECK_NOTHROW( load_condition( R"({ "u_is_grabbed": "npc" })" ) );
    CHECK_NOTHROW( load_condition( R"({ "npc_is_grabbed": "any" })" ) );
    // Naming the talker itself, or anything else, is an error
    CHECK_THROWS_AS( load_condition( R"({ "u_is_grabbing": "u" })" ), JsonError );
    CHECK_THROWS_AS( load_condition( R"({ "npc_is_grabbing": "npc" })" ), JsonError );
    CHECK_THROWS_AS( load_condition( R"({ "u_is_grabbed": "u" })" ), JsonError );
    CHECK_THROWS_AS( load_condition( R"({ "npc_is_grabbed": "npc" })" ), JsonError );
    CHECK_THROWS_AS( load_condition( R"({ "u_is_grabbed": "anyone" })" ), JsonError );
    CHECK_THROWS_AS( load_condition( R"({ "u_is_grabbing": true })" ), JsonError );
}
