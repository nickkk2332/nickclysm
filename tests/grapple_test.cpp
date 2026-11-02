#include <algorithm>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "coordinates.h"
#include "creature.h"
#include "dialogue.h"
#include "effect.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "grapple.h"
#include "item.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "options_helpers.h"
#include "player_helpers.h"
#include "point.h"
#include "talker.h"
#include "teleport.h"
#include "type_id.h"
#include "units.h"

// Grabs started by characters (src/grapple.h), ported from Cataclysm: The Last Generation.
// Every roll below is either skipped (a tolerated grab, a hold set up directly) or made at
// inputs where its chance is 0 or 1.

static const efftype_id effect_grabbed( "grabbed" );
static const efftype_id effect_grabbing( "grabbing" );
static const efftype_id effect_incorporeal( "incorporeal" );
static const efftype_id effect_pet( "pet" );
static const efftype_id effect_winded( "winded" );

static const flag_id json_flag_GRAB( "GRAB" );
static const flag_id json_flag_GRAB_FILTER( "GRAB_FILTER" );

static const itype_id itype_rock( "rock" );

static const limb_score_id limb_score_grip( "grip" );

static const skill_id skill_melee( "melee" );
static const skill_id skill_unarmed( "unarmed" );

static constexpr tripoint_bub_ms grabber_pos{ 60, 60, 0 };

static avatar &setup_grapple_scene()
{
    clear_map_without_vision();
    clear_creatures();
    clear_avatar();
    avatar &you = get_avatar();
    you.setpos( get_map(), grabber_pos );
    return you;
}

static const bodypart_id &no_part()
{
    static const bodypart_id null_part = bodypart_str_id::NULL_ID().id();
    return null_part;
}

// Whether grabber holds victim, as both sides see it.
static bool holds( const Character &grabber, const Creature &victim )
{
    return grabber.grabbed_creature() == &victim && grabber.is_grabbing_creature() &&
           grapple::hold_effect( grabber, victim ) != nullptr &&
           grabber.has_effect_with_flag( json_flag_GRAB_FILTER );
}

// Whether nothing is left of grabber's hold on victim, on either side.
static bool released( const Character &grabber, const Creature &victim )
{
    return grabber.grabbed_creature() == nullptr && !grabber.is_grabbing_creature() &&
           grapple::hold_effect( grabber, victim ) == nullptr &&
           !grabber.has_effect_with_flag( json_flag_GRAB_FILTER );
}

static monster &spawn_held( Character &you, const std::string &type, int strength = 20 )
{
    monster &mon = spawn_test_monster( type, you.pos_bub() + tripoint::east );
    REQUIRE( grapple::start_hold( you, mon, strength, no_part() ) );
    REQUIRE( holds( you, mon ) );
    return mon;
}

static void make_pet( monster &mon )
{
    mon.friendly = -1;
    mon.add_effect( effect_pet, 1_turns, true );
    REQUIRE( mon.is_pet() );
}

// Phase 0: groundwork

TEST_CASE( "grapple_remove_grab_clears_the_grabbing_effect", "[grapple][grab]" )
{
    avatar &you = setup_grapple_scene();
    monster &mon = spawn_test_monster( "mon_debug_grabber", you.pos_bub() + tripoint::east );
    mon.add_grab( body_part_arm_l );
    mon.add_grab( body_part_torso );
    REQUIRE( mon.has_effect( effect_grabbing ) );

    mon.remove_grab( body_part_torso );
    CHECK( mon.has_effect( effect_grabbing ) );
    CHECK( mon.is_grabbing( body_part_arm_l ) );

    // The last limb let go of: the grab filter goes, instead of being added again.
    mon.remove_grab( body_part_arm_l );
    CHECK( !mon.has_effect( effect_grabbing ) );
    CHECK( !mon.is_grabbing( body_part_arm_l ) );
}

TEST_CASE( "grapple_held_monster_does_not_keep_its_own_grab_alive", "[grapple][grab]" )
{
    avatar &you = setup_grapple_scene();
    monster &mon = spawn_test_monster( "mon_zombie", you.pos_bub() + tripoint{ 5, 0, 0 } );
    mon.add_effect( effect_grabbed, 1_days, true, 10 );
    mon.add_effect( effect_grabbing, 1_days, true, 1 );

    SECTION( "nobody else is held: the grab filter goes" ) {
        mon.process_turn();
        CHECK( !mon.has_effect_with_flag( json_flag_GRAB_FILTER ) );
    }
    SECTION( "a held neighbour keeps it" ) {
        you.setpos( get_map(), mon.pos_bub() + tripoint::west );
        you.add_effect( effect_grabbed, 1_days, body_part_torso, true, 10 );
        mon.process_turn();
        CHECK( mon.has_effect_with_flag( json_flag_GRAB_FILTER ) );
    }
}

TEST_CASE( "grapple_monster_grab_at_range_is_not_an_orphan", "[grapple][grab][audit]" )
{
    // CHAR-B09: the strider grabs at range 2; its grab must not be dropped as an orphan.
    avatar &you = setup_grapple_scene();
    map &here = get_map();
    monster &strider = spawn_test_monster( "mon_zombie_strider",
                                           you.pos_bub() + tripoint{ 2, 0, 0 } );
    REQUIRE( grapple::monster_grab_reach( strider ) >= 2 );
    strider.add_grab( body_part_arm_l );
    you.add_effect( effect_grabbed, 1_days, body_part_arm_l, true, 50 );

    // An attack only removes orphan grabs.
    you.try_remove_grab( true );
    CHECK( you.has_effect( effect_grabbed, body_part_arm_l ) );
    CHECK( grapple::monster_grabber( you, body_part_arm_l.id() ) == &strider );
    strider.process_turn();
    CHECK( strider.is_grabbing( body_part_arm_l ) );

    // Out of reach, it is an orphan after all.
    const int beyond_reach = grapple::monster_grab_reach( strider ) + 1;
    strider.setpos( here, you.pos_bub() + tripoint{ beyond_reach, 0, 0 } );
    you.try_remove_grab( true );
    CHECK( !you.has_effect( effect_grabbed, body_part_arm_l ) );
}

TEST_CASE( "grapple_character_grab_strength", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    REQUIRE( you.get_limb_score( limb_score_grip ) == Approx( 1.0f ) );
    REQUIRE( you.get_size() == creature_size::medium );

    SECTION( "a fresh survivor" ) {
        REQUIRE( you.get_arm_str() == 8 );
        // 1 + 1.0 * ( 8 + 0 + 0 + 3 )
        CHECK( grapple::grab_strength( you, you ) == 12 );
    }
    SECTION( "TLG's worked example" ) {
        you.set_str_base( 10 );
        you.set_skill_level( skill_unarmed, 2 );
        you.set_skill_level( skill_melee, 2 );
        REQUIRE( you.get_arm_str() == 10 );
        // 1 + 1.0 * ( 10 + 2 + 2 + 3 )
        CHECK( grapple::grab_strength( you, you ) == 18 );

        dialogue d( get_talker_for( you ), std::make_unique<talker>() );
        math_exp strength;
        REQUIRE( strength.parse( "u_val('grab_strength')" ) );
        CHECK( strength.eval( d ) == Approx( 18 ) );
        math_exp arm_strength;
        REQUIRE( arm_strength.parse( "u_arm_strength()" ) );
        CHECK( arm_strength.eval( d ) == Approx( 10 ) );

        // While holding something, it reads the strength of that hold.
        spawn_held( you, "mon_zombie", 33 );
        CHECK( strength.eval( d ) == Approx( 33 ) );
    }
}

// Phase 1: holding, and every way a hold ends

TEST_CASE( "grapple_hold_ends_on_every_release_path", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    map &here = get_map();
    restore_on_out_of_scope restore_calendar_turn( calendar::turn );
    monster &mon = spawn_held( you, "mon_zombie" );

    SECTION( "letting go" ) {
        you.release_grapple();
    }
    SECTION( "wielding an item" ) {
        REQUIRE( you.can_grapple_while_wielding() );
        item rock( itype_rock );
        REQUIRE( you.wield( rock ) );
        REQUIRE( !you.can_grapple_while_wielding() );
    }
    SECTION( "mounting a creature" ) {
        monster &horse = spawn_test_monster( "mon_horse", you.pos_bub() + tripoint::west );
        make_pet( horse );
        you.mount_creature( horse );
        REQUIRE( you.is_mounted() );
    }
    SECTION( "falling asleep" ) {
        you.fall_asleep( 1_hours );
        you.process_turn();
    }
    SECTION( "getting winded" ) {
        you.add_effect( effect_winded, 10_turns );
        grapple::process_turn( you );
    }
    SECTION( "stamina below the floor" ) {
        you.set_stamina( grapple::stamina_floor() / 2 );
        you.process_turn();
    }
    SECTION( "the grabber turns incorporeal" ) {
        you.add_effect( effect_incorporeal, 1_hours );
        grapple::process_turn( you );
    }
    SECTION( "the grabber is flung" ) {
        override_option no_animations( "ANIMATIONS", "false" );
        g->fling_creature( &you, units::from_degrees( 180 ), 20, false, true );
    }
    SECTION( "the victim is knocked back" ) {
        mon.knock_back_from( you.pos_bub() );
        REQUIRE( mon.pos_bub() != you.pos_bub() + tripoint::east );
    }
    SECTION( "the victim teleports" ) {
        const tripoint_bub_ms away = you.pos_bub() + tripoint{ 0, 5, 0 };
        REQUIRE( teleport::teleport_to_point( mon, away, true, false, false ) );
    }
    SECTION( "the victim is no longer adjacent" ) {
        mon.setpos( here, you.pos_bub() + tripoint{ 3, 0, 0 } );
        you.process_turn();
        // The victim drops its side as an orphan when it next tries to move.
        mon.move_effects( false );
        CHECK( !mon.has_effect_with_flag( json_flag_GRAB ) );
    }
    SECTION( "the victim dies" ) {
        mon.die( &here, nullptr );
    }
    CHECK( released( you, mon ) );
    CHECK( !mon.has_effect( effect_grabbed ) );
}

TEST_CASE( "grapple_hold_ends_when_an_npc_victim_dies", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    map &here = get_map();
    npc &guy = spawn_npc( you.pos_bub().xy() + point::east, "thug" );
    REQUIRE( grapple::start_hold( you, guy, 20, body_part_torso.id() ) );
    REQUIRE( holds( you, guy ) );
    guy.die( &here, nullptr );
    CHECK( released( you, guy ) );
}

TEST_CASE( "grapple_victim_removed_from_the_map_leaves_no_hold", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    monster &mon = spawn_held( you, "mon_zombie" );
    g->remove_zombie( mon );
    // Nothing dangles: the hold is simply gone, and the next turn drops our side of it.
    CHECK( you.grabbed_creature() == nullptr );
    CHECK( !you.is_grabbing_creature() );
    you.process_turn();
    CHECK( !you.has_effect_with_flag( json_flag_GRAB_FILTER ) );
}

TEST_CASE( "grapple_hold_survives_save_and_load", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    monster &mon = spawn_held( you, "mon_zombie", 27 );

    std::ostringstream saved;
    JsonOut jsout( saved );
    mon.serialize( jsout );
    mon.clear_effects();
    REQUIRE( you.grabbed_creature() == nullptr );

    const JsonValue jv = json_loader::from_string( saved.str() );
    mon.deserialize( jv.get_object() );
    CHECK( holds( you, mon ) );
    const effect *const hold = grapple::hold_effect( you, mon );
    REQUIRE( hold != nullptr );
    CHECK( hold->get_intensity() == 27 );
}
