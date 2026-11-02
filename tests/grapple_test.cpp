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

static const flag_id json_flag_GRAB_FILTER( "GRAB_FILTER" );

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

    }
}
