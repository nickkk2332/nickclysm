#include <string>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "condition.h"
#include "coordinates.h"
#include "dialogue.h"
#include "flexbuffer_json.h"
#include "json_error.h"
#include "json_loader.h"
#include "map_helpers.h"
#include "npc.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"

static const activity_id ACT_GAME( "ACT_GAME" );
static const activity_id ACT_MEDITATE( "ACT_MEDITATE" );
static const activity_id ACT_MULTIPLE_FARM( "ACT_MULTIPLE_FARM" );
static const activity_id ACT_WAIT_NPC( "ACT_WAIT_NPC" );

static talk_effect_t load_effect( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    jo.allow_omitted_members();
    talk_effect_t effect( jo, "effect", "audit_activities_crafting_test" );
    finalize_conditions();
    return effect;
}

// ACT-J02: u_assign_activity always built a generic activity, and Character::assign_activity
// refuses (with a debugmsg) any activity that has an actor. The godco dialogue's ACT_WAIT_NPC
// and ACT_GAME, and every zone job, never started.
TEST_CASE( "act-j02_assign_activity_starts_actor_activities", "[audit][activity]" )
{
    clear_avatar();
    clear_map_without_vision();
    avatar &u = get_avatar();

    SECTION( "waiting for the NPC you talk to, as the godco dialogue does" ) {
        npc &guy = spawn_npc( u.pos_bub().xy() + point::east, "thug" );
        const talk_effect_t effect = load_effect(
                                         R"({ "effect": { "u_assign_activity": "ACT_WAIT_NPC", )"
                                         R"("duration": "30 minutes" } })" );
        dialogue d( get_talker_for( u ), get_talker_for( guy ) );
        effect.apply( d );
        CHECK( u.activity.id() == ACT_WAIT_NPC );
        CHECK( u.activity.moves_total == to_moves<int>( 30_minutes ) );
    }
    SECTION( "a game without a game item, as Jeremiah offers" ) {
        const talk_effect_t effect = load_effect(
                                         R"({ "effect": { "u_assign_activity": "ACT_GAME", )"
                                         R"("duration": "45 minutes" } })" );
        dialogue d( get_talker_for( u ), nullptr );
        effect.apply( d );
        CHECK( u.activity.id() == ACT_GAME );
        CHECK( u.activity.moves_total == to_moves<int>( 45_minutes ) );
    }
    SECTION( "a zone job for an NPC" ) {
        npc &guy = spawn_npc( u.pos_bub().xy() + point::east, "thug" );
        const talk_effect_t effect = load_effect(
                                         R"({ "effect": { "npc_assign_activity": )"
                                         R"("ACT_MULTIPLE_FARM" } })" );
        dialogue d( get_talker_for( u ), get_talker_for( guy ) );
        effect.apply( d );
        CHECK( guy.activity.id() == ACT_MULTIPLE_FARM );
    }
    SECTION( "an activity id from a variable" ) {
        const talk_effect_t effect = load_effect(
                                         R"({ "effect": { "u_assign_activity": )"
                                         R"({ "context_val": "act" } } })" );
        dialogue d( get_talker_for( u ), nullptr );
        d.set_value( "act", ACT_MEDITATE.str() );
        effect.apply( d );
        CHECK( u.activity.id() == ACT_MEDITATE );
        CHECK( u.activity.moves_total == to_moves<int>( 20_minutes ) );
    }
    clear_avatar();
}

// ACT-J02: an activity whose actor needs more than a duration can't be started from an EOC.
// A plain id naming one used to load and then fail every time it ran; now it fails at load.
TEST_CASE( "act-j02_assign_activity_rejects_actors_it_cannot_start", "[audit][activity]" )
{
    CHECK_THROWS_AS( load_effect( R"({ "effect": { "u_assign_activity": "ACT_READ" } })" ),
                     JsonError );
    CHECK_THROWS_AS( load_effect( R"({ "effect": { "u_assign_activity": "ACT_MULTIPLE_FARM", )"
                                  R"("duration": "1 hours" } })" ), JsonError );
    CHECK_NOTHROW( load_effect( R"({ "effect": { "u_assign_activity": "ACT_GENERIC_EOC", )"
                                R"("duration": "10 minutes" } })" ) );
    CHECK_NOTHROW( load_effect( R"({ "effect": { "u_assign_activity": "ACT_WAIT" } })" ) );
}
