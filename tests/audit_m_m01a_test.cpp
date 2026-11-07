#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "avatar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "condition.h"
#include "coordinates.h"
#include "creature.h"
#include "debug.h"
#include "dialogue.h"
#include "flexbuffer_json.h"
#include "json_loader.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "monster.h"
#include "mtype.h"
#include "player_helpers.h"
#include "point.h"
#include "talker.h"

static talk_effect_t load_effect( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    talk_effect_t effect( jo, "effect", "audit_m_m01a_test" );
    // Math is parsed after loading; outside data loading that has to be done by hand.
    finalize_conditions();
    return effect;
}

// EOC-B09 follow-up: a conversation whose talker had no topics started from an empty topic
// stack and read its back(). open_dialogue without a "topic" reaches it with a monster, item,
// vehicle or furniture talker that has no chat_topics.
TEST_CASE( "eoc-b09_open_dialogue_with_a_talker_without_topics", "[audit][npc_talk]" )
{
    clear_map();
    clear_avatar();
    avatar &u = get_avatar();
    monster &zed = spawn_test_monster( "mon_zombie", u.pos_bub() + point( 3, 0 ) );
    on_out_of_scope restore( []() {
        clear_creatures();
        clear_avatar();
    } );
    REQUIRE( zed.type->chat_topics.empty() );

    const talk_effect_t open = load_effect( R"({ "effect": "open_dialogue" })" );
    dialogue d( get_talker_for( u ), get_talker_for( zed ) );
    // Without the check this enters the dialogue window with nothing to show
    const std::string messages = capture_debugmsg_during( [&]() {
        open.apply( d );
    } );
    CAPTURE( messages );
    CHECK( messages.find( "nothing to talk about" ) != std::string::npos );
}

// EOC-B13 follow-up: u_spellcasting_adjustment read the spellbook of the u talker's
// character without checking there was one, so a monster u crashed.
TEST_CASE( "eoc-b13_spellcasting_adjustment_needs_a_character", "[audit][eoc]" )
{
    clear_map();
    clear_avatar();
    avatar &u = get_avatar();
    monster &zed = spawn_test_monster( "mon_zombie", u.pos_bub() + point( 3, 0 ) );
    on_out_of_scope restore( []() {
        clear_creatures();
        clear_avatar();
    } );

    const talk_effect_t adjust = load_effect(
                                     R"json({ "effect": { "math": [ "u_spellcasting_adjustment('damage') = 0.5" ] } })json" );

    SECTION( "a monster has no spellbook: a math error, not a crash" ) {
        dialogue d( get_talker_for( zed ), nullptr );
        CHECK_FALSE( capture_debugmsg_during( [&]() {
            adjust.apply( d );
        } ).empty() );
    }

    SECTION( "a character's spellbook is adjusted as before" ) {
        // clear_avatar leaves no spells, so there is nothing to adjust and nothing to report
        dialogue d( get_talker_for( u ), nullptr );
        CHECK( capture_debugmsg_during( [&]() {
            adjust.apply( d );
        } ).empty() );
    }
}
