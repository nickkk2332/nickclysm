#include <memory>
#include <optional>
#include <string>

#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "condition.h"
#include "coordinates.h"
#include "creature.h"
#include "debug.h"
#include "dialogue.h"
#include "effect.h"
#include "effect_on_condition.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "global_vars.h"
#include "grapple.h"
#include "item.h"
#include "json_error.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser_diag_value.h"
#include "monster.h"
#include "npc.h"
#include "options_helpers.h"
#include "player_helpers.h"
#include "point.h"
#include "talker.h"
#include "type_id.h"

// The EOC surface of grappling (src/grapple.h): the who-holds-whom conditions u_is_grabbing and
// u_is_grabbed, the effects u_grab, u_release_grab and u_throw_grabbed, and the events
// character_grabs_creature, character_releases_grab and character_throws_creature.  Rolls are
// skipped (a hold set up directly, a tolerated grab) or made where their chance is 0 or 1.

static const effect_on_condition_id
effect_on_condition_EOC_TEST_GRAPPLE_GRAB_ARM( "EOC_TEST_GRAPPLE_GRAB_ARM" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_GRAPPLE_NPC_RELEASES_U( "EOC_TEST_GRAPPLE_NPC_RELEASES_U" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_GRAPPLE_THROW_TOWARD( "EOC_TEST_GRAPPLE_THROW_TOWARD" );

static const efftype_id effect_grabbed( "grabbed" );
static const efftype_id effect_grabbing( "grabbing" );
static const efftype_id effect_pet( "pet" );

static const faction_id faction_your_followers( "your_followers" );

static const flag_id json_flag_GRAB( "GRAB" );
static const flag_id json_flag_GRAB_FILTER( "GRAB_FILTER" );

static const itype_id itype_rock( "rock" );

static const skill_id skill_throw( "throw" );
static const skill_id skill_unarmed( "unarmed" );

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

static talk_effect_t load_effect( const std::string &effect )
{
    const JsonObject jo = json_loader::from_string( R"({ "effect": )" + effect + " }" );
    jo.allow_omitted_members();
    talk_effect_t ret( jo, "effect", "grapple_eoc_test" );
    finalize_conditions();
    return ret;
}

static void apply( const std::string &effect, Creature &alpha, Creature &beta )
{
    dialogue d( get_talker_for( alpha ), get_talker_for( beta ) );
    load_effect( effect ).apply( d );
}

// What the true_eocs or false_eocs of the last effect said: "true", "false" or "".
static std::string result()
{
    const diag_value *const said = get_globals().maybe_get_global_value( "test_grapple_result" );
    return said == nullptr ? std::string() : said->str();
}

static double global_number( const std::string &name )
{
    const diag_value *const value = get_globals().maybe_get_global_value( name );
    return value == nullptr ? 0.0 : value->dbl();
}

static std::string global_string( const std::string &name )
{
    const diag_value *const value = get_globals().maybe_get_global_value( name );
    return value == nullptr ? std::string() : value->str();
}

static void make_thrower( Character &guy )
{
    guy.set_str_base( 10 );
    guy.set_skill_level( skill_unarmed, 2 );
    guy.set_skill_level( skill_throw, 2 );
    REQUIRE( guy.get_arm_str() == 10 );
    guy.set_stamina( guy.get_stamina_max() );
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

// Effects

TEST_CASE( "grapple_eoc_u_grab_sets_up_a_hold", "[grapple][eoc]" )
{
    avatar &you = setup_scene();
    const std::string say = R"(, "true_eocs": [ "EOC_TEST_GRAPPLE_SAY_TRUE" ], )"
                            R"("false_eocs": [ "EOC_TEST_GRAPPLE_SAY_FALSE" ] })";
    monster &zed = spawn_test_monster( "mon_zombie", you.pos_bub() + tripoint::east );
    const int moves = you.get_moves();
    const int stamina = you.get_stamina();

    SECTION( "directly: no roll, no cost" ) {
        apply( R"({ "u_grab": { })" + say, you, zed );
        CHECK( result() == "true" );
        REQUIRE( grapple::holds( you, zed ) );
        const effect *const hold = grapple::hold_effect( you, zed );
        REQUIRE( hold != nullptr );
        CHECK( hold->get_bp() == no_part() );
        CHECK( hold->get_intensity() == grapple::grab_strength( you, zed ) );
        CHECK( you.get_moves() == moves );
        CHECK( you.get_stamina() == stamina );
    }
    SECTION( "a strength from a variable on a named part, through the data loader" ) {
        npc &guy = spawn_npc( you.pos_bub().xy() + point::south, "thug" );
        get_globals().set_global_value( "test_grapple_strength", diag_value( 37.0 ) );
        dialogue d( get_talker_for( you ), get_talker_for( guy ) );
        effect_on_condition_EOC_TEST_GRAPPLE_GRAB_ARM->activate( d );
        CHECK( result() == "true" );
        const effect *const hold = grapple::hold_effect( you, guy );
        REQUIRE( hold != nullptr );
        CHECK( hold->get_bp() == body_part_arm_l.id() );
        CHECK( hold->get_intensity() == 37 );
    }
    SECTION( "a hand is held by its main part, and a monster as a whole" ) {
        npc &guy = spawn_npc( you.pos_bub().xy() + point::south, "thug" );
        apply( R"({ "u_grab": { "body_part": "hand_r", "strength": 150 } })", you, guy );
        const effect *const hold = grapple::hold_effect( you, guy );
        REQUIRE( hold != nullptr );
        CHECK( hold->get_bp() == body_part_hand_r->main_part.id() );
        // Strength is 1 to 100
        CHECK( hold->get_intensity() == 100 );
        grapple::end_hold( you, &guy );
        apply( R"({ "u_grab": { "body_part": "head" } })", you, zed );
        const effect *const on_zed = grapple::hold_effect( you, zed );
        REQUIRE( on_zed != nullptr );
        CHECK( on_zed->get_bp() == no_part() );
    }
    SECTION( "an NPC grabs the avatar" ) {
        npc &guy = spawn_npc( you.pos_bub().xy() + point::south, "thug" );
        get_globals().set_global_value( "test_grapple_strength", diag_value( 25.0 ) );
        dialogue d( get_talker_for( guy ), get_talker_for( you ) );
        effect_on_condition_EOC_TEST_GRAPPLE_GRAB_ARM->activate( d );
        CHECK( result() == "true" );
        CHECK( grapple::holds( guy, you ) );
        CHECK( you.has_effect( effect_grabbed, body_part_arm_l ) );
    }
    SECTION( "npc_grab: the beta talker grabs the alpha" ) {
        apply( R"({ "npc_grab": { } })", zed, you );
        CHECK( grapple::holds( you, zed ) );
    }
    SECTION( "refusals run false_eocs and change nothing" ) {
        npc &guy = spawn_npc( you.pos_bub().xy() + point::south, "thug" );
        SECTION( "a monster can't grab this way" ) {
            apply( R"({ "u_grab": { })" + say, zed, you );
            CHECK_FALSE( zed.has_effect_with_flag( json_flag_GRAB_FILTER ) );
            CHECK_FALSE( you.has_effect_with_flag( json_flag_GRAB ) );
        }
        SECTION( "already holding someone" ) {
            REQUIRE( grapple::start_hold( you, zed, 20, no_part() ) );
            apply( R"({ "u_grab": { })" + say, you, guy );
            CHECK( grapple::holds( you, zed ) );
        }
        SECTION( "out of reach" ) {
            monster &far = spawn_test_monster( "mon_zombie", you.pos_bub() + tripoint( 3, 0, 0 ) );
            apply( R"({ "u_grab": { })" + say, you, far );
            CHECK_FALSE( far.has_effect_with_flag( json_flag_GRAB ) );
        }
        SECTION( "the part named is held already" ) {
            guy.add_effect( effect_grabbed, 1_days, body_part_arm_l, true, 10 );
            apply( R"({ "u_grab": { "body_part": "arm_l" })" + say, you, guy );
            CHECK( grapple::hold_effect( you, guy ) == nullptr );
        }
        SECTION( "an unknown part from a variable" ) {
            get_globals().set_global_value( "test_grapple_part",
                                            diag_value( std::string( "tentacle_9" ) ) );
            const std::string msg = capture_debugmsg_during( [&]() {
                apply( R"({ "u_grab": { "body_part": { "global_val": "test_grapple_part" } })" + say,
                       you, guy );
            } );
            CHECK_THAT( msg, Catch::Matchers::Contains( "tentacle_9" ) );
            CHECK( grapple::hold_effect( you, guy ) == nullptr );
        }
        SECTION( "attempting it with a weapon in hand" ) {
            item rock( itype_rock );
            REQUIRE( you.wield( rock ) );
            you.set_moves( moves );
            apply( R"({ "u_grab": { "attempt": true })" + say, you, zed );
            CHECK( grapple::hold_effect( you, zed ) == nullptr );
        }
        CHECK( result() == "false" );
        CHECK_FALSE( grapple::holds( you, guy ) );
        CHECK( you.get_moves() == moves );
        CHECK( you.get_stamina() == stamina );
    }
}

TEST_CASE( "grapple_eoc_u_grab_attempt_is_the_grab_action", "[grapple][eoc]" )
{
    avatar &you = setup_scene();
    const int moves = you.get_moves();
    const int stamina = you.get_stamina();

    SECTION( "a pet lets you, at the strength given" ) {
        monster &cow = spawn_test_monster( "mon_cow", you.pos_bub() + tripoint::east );
        cow.friendly = -1;
        cow.add_effect( effect_pet, 1_turns, true );
        REQUIRE( grapple::grab_tolerated( you, cow ) );
        apply( R"({ "u_grab": { "attempt": true, "strength": 44 }, )"
               R"("true_eocs": [ "EOC_TEST_GRAPPLE_SAY_TRUE" ] })", you, cow );
        CHECK( result() == "true" );
        const effect *const hold = grapple::hold_effect( you, cow );
        REQUIRE( hold != nullptr );
        CHECK( hold->get_intensity() == 44 );
    }
    SECTION( "a follower lets you, by the part named" ) {
        npc &guy = spawn_npc( you.pos_bub().xy() + point::east, "thug" );
        guy.set_fac( faction_your_followers );
        REQUIRE( grapple::grab_tolerated( you, guy ) );
        apply( R"({ "u_grab": { "attempt": true, "body_part": "leg_r" } })", you, guy );
        const effect *const hold = grapple::hold_effect( you, guy );
        REQUIRE( hold != nullptr );
        CHECK( hold->get_bp() == body_part_leg_r.id() );
        CHECK( hold->get_intensity() == grapple::grab_strength( you, guy ) );
    }
    // The grab action's costs
    CHECK( you.get_moves() <= moves - 100 );
    CHECK( you.get_stamina() == stamina - grapple::arm_stamina_cost() );
}

TEST_CASE( "grapple_eoc_u_release_grab", "[grapple][eoc]" )
{
    avatar &you = setup_scene();
    monster &zed = spawn_test_monster( "mon_zombie", you.pos_bub() + tripoint::east );

    SECTION( "a character lets go" ) {
        REQUIRE( grapple::start_hold( you, zed, 20, no_part() ) );
        monster &other = spawn_test_monster( "mon_zombie", you.pos_bub() + tripoint::west );
        // Only the other talker: it doesn't hold that one, so nothing happens
        apply( R"({ "u_release_grab": { "target": "npc" } })", you, other );
        CHECK( grapple::holds( you, zed ) );
        WHEN( "it lets go of the other talker" ) {
            apply( R"({ "u_release_grab": { "target": "npc" } })", you, zed );
        }
        WHEN( "it lets go of everything" ) {
            apply( R"({ "u_release_grab": { } })", you, other );
        }
        CHECK_FALSE( grapple::holds( you, zed ) );
        CHECK_FALSE( you.has_effect_with_flag( json_flag_GRAB_FILTER ) );
        CHECK_FALSE( zed.has_effect_with_flag( json_flag_GRAB ) );
    }
    SECTION( "npc_release_grab: an NPC lets go of the avatar, through the data loader" ) {
        npc &guy = spawn_npc( you.pos_bub().xy() + point::south, "thug" );
        REQUIRE( grapple::start_hold( guy, you, 20, body_part_arm_r.id() ) );
        dialogue d( get_talker_for( you ), get_talker_for( guy ) );
        CHECK( effect_on_condition_EOC_TEST_GRAPPLE_NPC_RELEASES_U->activate( d ) );
        CHECK_FALSE( grapple::holds( guy, you ) );
        CHECK_FALSE( you.has_effect( effect_grabbed, body_part_arm_r ) );
        CHECK_FALSE( guy.has_effect_with_flag( json_flag_GRAB_FILTER ) );
    }
    SECTION( "a monster lets go of one creature, then of everything" ) {
        // It holds your left arm, and a thug next to it by the torso
        monster &grabber = spawn_test_monster( "mon_debug_grabber",
                                               you.pos_bub() + tripoint::north );
        npc &guy = spawn_npc( grabber.pos_bub().xy() + point::north, "thug" );
        grabber.add_grab( body_part_arm_l );
        grabber.add_grab( body_part_torso );
        you.add_effect( effect_grabbed, 1_days, body_part_arm_l, true, 20 );
        guy.add_effect( effect_grabbed, 1_days, body_part_torso, true, 20 );
        REQUIRE( grapple::holds( grabber, you ) );
        REQUIRE( grapple::holds( grabber, guy ) );

        apply( R"({ "u_release_grab": { "target": "npc" } })", grabber, you );
        CHECK_FALSE( you.has_effect( effect_grabbed, body_part_arm_l ) );
        CHECK_FALSE( grabber.is_grabbing( body_part_arm_l ) );
        CHECK( grapple::holds( grabber, guy ) );
        CHECK( grabber.has_effect_with_flag( json_flag_GRAB_FILTER ) );

        apply( R"({ "npc_release_grab": { } })", you, grabber );
        CHECK_FALSE( guy.has_effect( effect_grabbed, body_part_torso ) );
        CHECK_FALSE( grabber.has_effect_with_flag( json_flag_GRAB_FILTER ) );
    }
    SECTION( "a monster lets go of a monster it holds as a whole" ) {
        monster &grabber = spawn_test_monster( "mon_debug_grabber", zed.pos_bub() + tripoint::east );
        grabber.add_effect( effect_grabbing, 1_days );
        zed.add_effect( effect_grabbed, 1_days );
        REQUIRE( grapple::holds( grabber, zed ) );
        apply( R"({ "u_release_grab": { "target": "npc" } })", grabber, zed );
        CHECK_FALSE( zed.has_effect_with_flag( json_flag_GRAB ) );
        CHECK_FALSE( grabber.has_effect_with_flag( json_flag_GRAB_FILTER ) );
    }
}

TEST_CASE( "grapple_eoc_effect_load_errors", "[grapple][eoc]" )
{
    CHECK_NOTHROW( load_effect( R"({ "u_release_grab": { } })" ) );
    CHECK_NOTHROW( load_effect( R"({ "u_release_grab": { "target": "npc" } })" ) );
    CHECK_NOTHROW( load_effect( R"({ "npc_release_grab": { "target": "u" } })" ) );
    CHECK_THROWS_AS( load_effect( R"({ "u_release_grab": { "target": "u" } })" ), JsonError );
    CHECK_THROWS_AS( load_effect( R"({ "npc_release_grab": { "target": "npc" } })" ),
                     JsonError );
    CHECK_THROWS_AS( load_effect( R"({ "u_release_grab": "npc" })" ), JsonError );
    CHECK_THROWS_AS( load_effect( R"({ "u_grab": true })" ), JsonError );
    CHECK_THROWS_AS( load_effect( R"({ "u_grab": { "attempt": "yes" } })" ), JsonError );
    CHECK_THROWS_AS( load_effect( R"({ "u_throw_grabbed": 30 })" ), JsonError );
}

TEST_CASE( "grapple_eoc_u_throw_grabbed", "[grapple][eoc]" )
{
    avatar &you = setup_scene();
    override_option no_animations( "ANIMATIONS", "false" );
    const std::string say = R"(, "true_eocs": [ "EOC_TEST_GRAPPLE_SAY_TRUE" ], )"
                            R"("false_eocs": [ "EOC_TEST_GRAPPLE_SAY_FALSE" ] })";
    map &here = get_map();
    // 50 kg, medium size
    monster &mon = spawn_test_monster( "mon_test_speed_desc_base", you.pos_bub() + tripoint::east );
    REQUIRE( grapple::start_hold( you, mon, 20, no_part() ) );
    const tripoint_bub_ms start = mon.pos_bub();
    monster &bystander = spawn_test_monster( "mon_zombie", you.pos_bub() + tripoint::west );

    SECTION( "straight away, as far as the throw goes" ) {
        make_thrower( you );
        const float full_force = grapple::throw_force( you, mon );
        REQUIRE( grapple::throw_range( full_force ) == 3 );
        const int moves = you.get_moves() - grapple::throw_move_cost( you );
        const int stamina = you.get_stamina() - grapple::throw_stamina_cost( you, mon, full_force );
        apply( R"({ "u_throw_grabbed": { })" + say, you, bystander );
        CHECK( result() == "true" );
        CHECK( mon.pos_bub() == start + tripoint( 3, 0, 0 ) );
        CHECK_FALSE( grapple::holds( you, mon ) );
        CHECK_FALSE( you.has_effect_with_flag( json_flag_GRAB_FILTER ) );
        CHECK( you.get_moves() == moves );
        CHECK( you.get_stamina() == stamina );
    }
    SECTION( "diagonally away" ) {
        make_thrower( you );
        grapple::end_hold( you, &mon );
        mon.setpos( here, you.pos_bub() + tripoint( 1, 1, 0 ) );
        REQUIRE( grapple::start_hold( you, mon, 20, no_part() ) );
        apply( R"({ "u_throw_grabbed": { } })", you, bystander );
        CHECK( mon.pos_bub() == you.pos_bub() + tripoint( 4, 4, 0 ) );
    }
    SECTION( "toward a location variable, through the data loader" ) {
        make_thrower( you );
        get_globals().set_global_value( "test_grapple_throw_to",
                                        diag_value( here.get_abs( start + tripoint( 0, 2, 0 ) ) ) );
        dialogue d( get_talker_for( you ), get_talker_for( bystander ) );
        effect_on_condition_EOC_TEST_GRAPPLE_THROW_TOWARD->activate( d );
        CHECK( result() == "true" );
        CHECK( mon.pos_bub() == start + tripoint( 0, 2, 0 ) );
    }
    SECTION( "with a force of its own" ) {
        make_thrower( you );
        apply( R"({ "u_throw_grabbed": { "force": 20 } })", you, bystander );
        CHECK( mon.pos_bub() == start + tripoint( 2, 0, 0 ) );
    }
    SECTION( "npc_throw_grabbed: an NPC throws what it holds" ) {
        grapple::end_hold( you, &mon );
        npc &guy = spawn_npc( mon.pos_bub().xy() + point::south, "thug" );
        make_thrower( guy );
        REQUIRE( grapple::start_hold( guy, mon, 20, no_part() ) );
        apply( R"({ "npc_throw_grabbed": { } })", you, guy );
        // Away from the thug: north
        CHECK( mon.pos_bub() == start + tripoint( 0, -3, 0 ) );
        CHECK_FALSE( grapple::holds( guy, mon ) );
    }
    SECTION( "refusals run false_eocs and change nothing" ) {
        int moves = you.get_moves();
        int stamina = you.get_stamina();
        SECTION( "too weak" ) {
            REQUIRE( grapple::throw_refusal( you, mon ) );
            apply( R"({ "u_throw_grabbed": { })" + say, you, bystander );
        }
        SECTION( "not through yourself" ) {
            make_thrower( you );
            moves = you.get_moves();
            stamina = you.get_stamina();
            const tripoint_abs_ms behind = here.get_abs( you.pos_bub() + tripoint::west );
            get_globals().set_global_value( "test_grapple_throw_to", diag_value( behind ) );
            dialogue d( get_talker_for( you ), get_talker_for( bystander ) );
            effect_on_condition_EOC_TEST_GRAPPLE_THROW_TOWARD->activate( d );
        }
        SECTION( "a monster doesn't throw" ) {
            apply( R"({ "npc_throw_grabbed": { })" + say, you, bystander );
        }
        CHECK( result() == "false" );
        CHECK( mon.pos_bub() == start );
        CHECK( grapple::holds( you, mon ) );
        CHECK( you.get_moves() == moves );
        CHECK( you.get_stamina() == stamina );
    }
    SECTION( "holding nothing" ) {
        make_thrower( you );
        grapple::end_hold( you, &mon );
        apply( R"({ "u_throw_grabbed": { })" + say, you, bystander );
        CHECK( result() == "false" );
        CHECK( mon.pos_bub() == start );
    }
}

// Events

TEST_CASE( "grapple_eoc_events", "[grapple][eoc]" )
{
    avatar &you = setup_scene();
    override_option no_animations( "ANIMATIONS", "false" );
    map &here = get_map();
    // 50 kg, medium size
    monster &mon = spawn_test_monster( "mon_test_speed_desc_base", you.pos_bub() + tripoint::east );
    const std::string no_character = std::to_string( character_id().get_value() );

    SECTION( "the avatar grabs a monster, and throws it" ) {
        REQUIRE( grapple::start_hold( you, mon, 23, no_part() ) );
        CHECK( global_number( "test_grapple_grabs" ) == 1 );
        CHECK( you.get_value( "test_grapple_event" ) == "grabs" );
        CHECK( mon.get_value( "test_grapple_event" ) == "grabbed" );
        CHECK( global_string( "test_grapple_victim" ) == no_character );
        CHECK( global_string( "test_grapple_victim_type" ) == "mon_test_speed_desc_base" );
        CHECK( global_string( "test_grapple_victim_name" ) == mon.get_name() );
        CHECK( global_string( "test_grapple_bodypart" ) == "bp_null" );
        CHECK( global_number( "test_grapple_event_strength" ) == 23 );

        make_thrower( you );
        REQUIRE( grapple::throw_held_creature( you, mon, mon.pos_bub() + tripoint( 2, 0, 0 ) ) );
        // The throw ends the hold first
        CHECK( global_number( "test_grapple_releases" ) == 1 );
        CHECK( global_number( "test_grapple_throws" ) == 1 );
        CHECK( you.get_value( "test_grapple_event" ) == "throws" );
        CHECK( mon.get_value( "test_grapple_event" ) == "thrown" );
        CHECK( global_string( "test_grapple_victim_type" ) == "mon_test_speed_desc_base" );
        CHECK( global_number( "test_grapple_throw_distance" ) == 2 );
        // Two thirds of the full force of 31
        CHECK( global_number( "test_grapple_throw_force" ) == 21 );
        CHECK( global_number( "test_grapple_grabs" ) == 1 );
    }
    SECTION( "an NPC grabs the avatar with u_grab, and lets go" ) {
        npc &guy = spawn_npc( you.pos_bub().xy() + point::south, "thug" );
        apply( R"({ "u_grab": { "body_part": "arm_l", "strength": 31 } })", guy, you );
        REQUIRE( grapple::holds( guy, you ) );
        CHECK( global_number( "test_grapple_grabs" ) == 1 );
        CHECK( guy.get_value( "test_grapple_event" ) == "grabs" );
        CHECK( you.get_value( "test_grapple_event" ) == "grabbed" );
        CHECK( global_string( "test_grapple_victim" ) == std::to_string( you.getID().get_value() ) );
        CHECK( global_string( "test_grapple_victim_type" ) == "mon_null" );
        CHECK( global_string( "test_grapple_victim_name" ) == you.get_name() );
        CHECK( global_string( "test_grapple_bodypart" ) == "arm_l" );
        CHECK( global_number( "test_grapple_event_strength" ) == 31 );

        apply( R"({ "u_release_grab": { } })", guy, you );
        CHECK( global_number( "test_grapple_releases" ) == 1 );
        CHECK( guy.get_value( "test_grapple_event" ) == "releases" );
        CHECK( you.get_value( "test_grapple_event" ) == "released" );
        CHECK( global_string( "test_grapple_victim_name" ) == you.get_name() );
    }
    SECTION( "the avatar throws with u_throw_grabbed" ) {
        REQUIRE( grapple::start_hold( you, mon, 20, no_part() ) );
        make_thrower( you );
        apply( R"({ "u_throw_grabbed": { } })", you, mon );
        CHECK( global_number( "test_grapple_throws" ) == 1 );
        CHECK( global_number( "test_grapple_throw_distance" ) == 3 );
        CHECK( global_number( "test_grapple_throw_force" ) == 31 );
    }
    SECTION( "a hold lost with the victim out of reach names no victim" ) {
        REQUIRE( grapple::start_hold( you, mon, 20, no_part() ) );
        mon.setpos( here, you.pos_bub() + tripoint( 3, 0, 0 ) );
        mon.set_value( "test_grapple_event", std::string( "none" ) );
        you.process_turn();
        REQUIRE_FALSE( you.has_effect_with_flag( json_flag_GRAB_FILTER ) );
        CHECK( global_number( "test_grapple_releases" ) == 1 );
        CHECK( you.get_value( "test_grapple_event" ) == "releases" );
        CHECK( mon.get_value( "test_grapple_event" ) == "none" );
        CHECK( global_string( "test_grapple_victim_name" ).empty() );
        CHECK( global_string( "test_grapple_victim_type" ) == "mon_null" );
    }
    SECTION( "refusals send nothing" ) {
        npc &guy = spawn_npc( you.pos_bub().xy() + point::south, "thug" );
        REQUIRE( grapple::start_hold( you, mon, 20, no_part() ) );
        REQUIRE( global_number( "test_grapple_grabs" ) == 1 );
        apply( R"({ "u_grab": { } })", you, guy );
        apply( R"({ "u_release_grab": { "target": "npc" } })", you, guy );
        apply( R"({ "u_throw_grabbed": { } })", you, guy );
        CHECK( global_number( "test_grapple_grabs" ) == 1 );
        CHECK( global_number( "test_grapple_releases" ) == 0 );
        CHECK( global_number( "test_grapple_throws" ) == 0 );
        CHECK( grapple::holds( you, mon ) );
    }
}
