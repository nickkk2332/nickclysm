#include <string>
#include <utility>

#include "avatar.h"
#include "behavior.h"
#include "cata_catch.h"
#include "character_oracle.h"
#include "dialogue.h"
#include "event.h"
#include "faction.h"
#include "game.h"
#include "global_vars.h"
#include "json_error.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser.h"
#include "monster.h"
#include "npc.h"
#include "npc_ai_hooks_helpers.h"
#include "npc_class.h"
#include "point.h"
#include "talker.h"
#include "type_id.h"

// NPC-J05: what EOCs read and set of an NPC's AI (its attitude, mission, post, danger, panic,
// needs and personality) and of factions' relations, with the NPC as either talker, and without
// one.

static const efftype_id effect_npc_flee_player( "npc_flee_player" );

static const faction_id faction_tacoma_commune( "tacoma_commune" );
static const faction_id faction_test_npc_ai_doves( "test_npc_ai_doves" );
static const faction_id faction_test_npc_ai_hawks( "test_npc_ai_hawks" );
static const faction_id faction_your_followers( "your_followers" );

TEST_CASE( "npc_personality_math_reads_and_sets_traits", "[npc][npc_ai_hooks][math_parser]" )
{
    const ai_hooks_world world;
    npc &guy = hooks_npc();
    guy.personality.aggression = 3;
    guy.personality.bravery = -4;
    guy.personality.collector = 5;
    guy.personality.altruism = -6;
    dialogue d( get_talker_for( get_avatar() ), get_talker_for( guy ) );
    CHECK( eval_math( "n_npc_personality('aggression')", d ) == 3 );
    CHECK( eval_math( "n_npc_personality('bravery')", d ) == -4 );
    CHECK( eval_math( "n_npc_personality('collector')", d ) == 5 );
    CHECK( eval_math( "n_npc_personality('altruism')", d ) == -6 );
    // u is the avatar: no personality
    CHECK( eval_math( "u_npc_personality('aggression')", d ) == 0 );

    // set, clamped to -10..10
    run_effect( R"([ { "math": [ "n_npc_personality('bravery') = 7" ] } ])", d );
    CHECK( guy.personality.bravery == 7 );
    run_effect( R"([ { "math": [ "n_npc_personality('aggression') += 20" ] } ])", d );
    CHECK( guy.personality.aggression == 10 );
    run_effect( R"([ { "math": [ "n_npc_personality('altruism') = -15" ] } ])", d );
    CHECK( guy.personality.altruism == -10 );
    // the avatar has none to set
    run_effect( R"([ { "math": [ "u_npc_personality('collector') = 1" ] } ])", d );
    CHECK( guy.personality.collector == 5 );

    // with the NPC as u, also without a prefix; no beta talker reads 0
    const dialogue alone( get_talker_for( guy ), nullptr );
    CHECK( eval_math( "npc_personality('collector')", alone ) == 5 );
    CHECK( eval_math( "u_npc_personality('collector')", alone ) == 5 );
    CHECK( eval_math( "n_npc_personality('collector')", alone ) == 0 );
    dialogue alone_rw( get_talker_for( guy ), nullptr );
    run_effect( R"([ { "math": [ "npc_personality('collector') = -2" ] } ])", alone_rw );
    CHECK( guy.personality.collector == -2 );
    run_effect( R"([ { "math": [ "n_npc_personality('collector') = 4" ] } ])", alone_rw );
    CHECK( guy.personality.collector == -2 );

    // a monster has none
    monster &zombie = spawn_test_monster( "mon_zombie", npc_spot + tripoint( 3, 0, 0 ) );
    const dialogue with_monster( get_talker_for( guy ), get_talker_for( zombie ) );
    CHECK( eval_math( "n_npc_personality('bravery')", with_monster ) == 0 );
    clear_creatures();

    // a trait name written out is checked when the expression is parsed; one from a variable
    // when it's read
    math_exp unknown;
    CHECK_THROWS_WITH( unknown.parse( "npc_personality('greed')", false ),
                       Catch::Matchers::Contains( "isn't aggression, bravery, collector or altruism" ) );
    get_globals().set_global_value( "test_ai_var", std::string( "greed" ) );
    math_exp from_variable;
    REQUIRE( from_variable.parse( "npc_personality(test_ai_var)" ) );
    CHECK_THROWS_WITH( from_variable.eval( alone ), Catch::Matchers::Contains( "isn't aggression" ) );
}

TEST_CASE( "npc_danger_panic_and_need_urgency_math", "[npc][npc_ai_hooks][math_parser]" )
{
    const ai_hooks_world world;
    npc &guy = hooks_npc();
    guy.set_ai_danger( 2.5f );
    guy.mem_combat.panic = 7;
    guy.set_thirst( 600 );
    guy.set_sleepiness( 250 );
    guy.set_stored_kcal( guy.get_healthy_kcal() * 3 / 4 );
    const dialogue d( get_talker_for( guy ), nullptr );
    CHECK( eval_math( "npc_danger()", d ) == Approx( 2.5 ) );
    CHECK( eval_math( "u_npc_danger()", d ) == Approx( 2.5 ) );
    CHECK( eval_math( "npc_panic()", d ) == 7 );
    // the urgencies the arbitration and the built-in scores use
    const behavior::character_oracle_t oracle( &guy );
    CHECK( eval_math( "npc_need_urgency('drink_water')", d ) == Approx( 0.5 ) );
    CHECK( eval_math( "npc_need_urgency('drink_water')", d ) == Approx( oracle.thirst_urgency( "" ) ) );
    CHECK( eval_math( "npc_need_urgency('go_to_sleep')",
                      d ) == Approx( oracle.sleepiness_urgency( "" ) ) );
    CHECK( eval_math( "npc_need_urgency('eat_food')", d ) == Approx( oracle.hunger_urgency( "" ) ) );
    CHECK( eval_math( "npc_need_urgency('eat_food')", d ) == Approx( 0.25 ).margin( 0.01 ) );
    CHECK( eval_math( "npc_need_urgency('regulate_temperature')", d ) ==
           Approx( oracle.temperature_urgency( "" ) ) );

    // the NPC as beta; the avatar, no beta and a monster read 0
    const dialogue with_npc( get_talker_for( get_avatar() ), get_talker_for( guy ) );
    CHECK( eval_math( "n_npc_danger()", with_npc ) == Approx( 2.5 ) );
    CHECK( eval_math( "n_npc_panic()", with_npc ) == 7 );
    CHECK( eval_math( "n_npc_need_urgency('drink_water')", with_npc ) == Approx( 0.5 ) );
    CHECK( eval_math( "u_npc_danger()", with_npc ) == 0 );
    CHECK( eval_math( "u_npc_panic()", with_npc ) == 0 );
    CHECK( eval_math( "u_npc_need_urgency('drink_water')", with_npc ) == 0 );
    CHECK( eval_math( "n_npc_panic()", d ) == 0 );
    CHECK( eval_math( "n_npc_need_urgency('drink_water')", d ) == 0 );
    monster &zombie = spawn_test_monster( "mon_zombie", npc_spot + tripoint( 3, 0, 0 ) );
    const dialogue with_monster( get_talker_for( zombie ), nullptr );
    CHECK( eval_math( "npc_danger()", with_monster ) == 0 );
    CHECK( eval_math( "npc_need_urgency('go_to_sleep')", with_monster ) == 0 );
    clear_creatures();

    math_exp unknown;
    CHECK_THROWS_WITH( unknown.parse( "npc_need_urgency('drink_blood')", false ),
                       Catch::Matchers::Contains( "isn't drink_water, eat_food" ) );
}

// ---- NPC-J05: conditions ----

TEST_CASE( "npc_attitude_and_mission_conditions", "[npc][npc_ai_hooks][eoc]" )
{
    const ai_hooks_world world;
    npc &guy = hooks_npc();
    guy.set_attitude( NPCATT_MUG );
    guy.set_mission( NPC_MISSION_SHOPKEEP );
    const dialogue d( get_talker_for( get_avatar() ), get_talker_for( guy ) );
    CHECK( test_condition( R"({ "npc_attitude": "NPCATT_MUG" })", d ) );
    CHECK_FALSE( test_condition( R"({ "npc_attitude": "NPCATT_KILL" })", d ) );
    CHECK( test_condition( R"({ "npc_mission": "SHOPKEEP" })", d ) );
    CHECK_FALSE( test_condition( R"({ "npc_mission": "GUARD" })", d ) );
    get_globals().set_global_value( "test_ai_var", std::string( "NPCATT_MUG" ) );
    CHECK( test_condition( R"({ "npc_attitude": { "global_val": "test_ai_var" } })", d ) );
    get_globals().set_global_value( "test_ai_var", std::string( "SHOPKEEP" ) );
    CHECK( test_condition( R"({ "npc_mission": { "global_val": "test_ai_var" } })", d ) );
    // u is the avatar
    CHECK_FALSE( test_condition( R"({ "u_attitude": "NPCATT_NULL" })", d ) );
    CHECK_FALSE( test_condition( R"({ "u_mission": "NULL" })", d ) );

    // the NPC as u; no beta
    const dialogue alone( get_talker_for( guy ), nullptr );
    CHECK( test_condition( R"({ "u_attitude": "NPCATT_MUG" })", alone ) );
    CHECK( test_condition( R"({ "u_mission": "SHOPKEEP" })", alone ) );
    CHECK_FALSE( test_condition( R"({ "npc_attitude": "NPCATT_MUG" })", alone ) );
    CHECK_FALSE( test_condition( R"({ "npc_mission": "SHOPKEEP" })", alone ) );

    // a monster
    monster &zombie = spawn_test_monster( "mon_zombie", npc_spot + tripoint( 3, 0, 0 ) );
    const dialogue with_monster( get_talker_for( guy ), get_talker_for( zombie ) );
    CHECK_FALSE( test_condition( R"({ "npc_attitude": "NPCATT_NULL" })", with_monster ) );
    CHECK_FALSE( test_condition( R"({ "npc_mission": "NULL" })", with_monster ) );
    clear_creatures();

    // ids written out are checked at load
    CHECK_THROWS_AS( test_condition( R"({ "npc_attitude": "NPCATT_GREEDY" })", d ), JsonError );
    CHECK_THROWS_AS( test_condition( R"({ "npc_attitude": "NPCATT_LEGACY_1" })", d ), JsonError );
    CHECK_THROWS_AS( test_condition( R"({ "npc_mission": "GUARDING" })", d ), JsonError );
}

TEST_CASE( "npc_on_shift_and_at_post_conditions", "[npc][npc_ai_hooks][eoc]" )
{
    const ai_hooks_world world;
    npc &guy = hooks_npc();
    const dialogue d( get_talker_for( get_avatar() ), get_talker_for( guy ) );
    const dialogue alone( get_talker_for( guy ), nullptr );
    // no post
    CHECK_FALSE( test_condition( R"("npc_on_shift")", d ) );
    CHECK_FALSE( test_condition( R"("npc_at_post")", d ) );

    // a post where it stands, and a class that always works
    guy.guard_pos = guy.pos_abs();
    REQUIRE( guy.myclass->get_work_hours() == std::make_pair( 0, 24 ) );
    CHECK( test_condition( R"("npc_on_shift")", d ) );
    CHECK( test_condition( R"("npc_at_post")", d ) );
    CHECK( test_condition( R"("u_on_shift")", alone ) );
    CHECK( test_condition( R"("u_at_post")", alone ) );
    // the same as the behavior predicate
    CHECK( behavior::character_oracle_t( &guy ).on_shift( "" ) == behavior::status_t::running );

    // the post elsewhere
    guy.guard_pos = guy.pos_abs() + tripoint( 5, 0, 0 );
    CHECK( test_condition( R"("npc_on_shift")", d ) );
    CHECK_FALSE( test_condition( R"("npc_at_post")", d ) );
    // walking with the avatar it isn't on shift
    guy.set_attitude( NPCATT_FOLLOW );
    CHECK_FALSE( test_condition( R"("npc_on_shift")", d ) );
    guy.set_attitude( NPCATT_NULL );

    // the avatar, no beta, a monster
    CHECK_FALSE( test_condition( R"("u_on_shift")", d ) );
    CHECK_FALSE( test_condition( R"("u_at_post")", d ) );
    CHECK_FALSE( test_condition( R"("npc_on_shift")", alone ) );
    CHECK_FALSE( test_condition( R"("npc_at_post")", alone ) );
    monster &zombie = spawn_test_monster( "mon_zombie", npc_spot + tripoint( 3, 0, 0 ) );
    const dialogue with_monster( get_talker_for( guy ), get_talker_for( zombie ) );
    CHECK_FALSE( test_condition( R"("npc_on_shift")", with_monster ) );
    CHECK_FALSE( test_condition( R"("npc_at_post")", with_monster ) );
    clear_creatures();
}

TEST_CASE( "faction_relations_read_and_set_by_eocs", "[npc_ai_hooks][eoc][faction]" )
{
    const ai_hooks_world world;
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    // the hawks' JSON: kill on sight toward the doves
    CHECK( test_condition(
               R"({ "faction_has_relation": [ "test_npc_ai_hawks", "test_npc_ai_doves", "kill on sight" ] })",
               d ) );
    CHECK_FALSE( test_condition(
                     R"({ "faction_has_relation": [ "test_npc_ai_doves", "test_npc_ai_hawks", "kill on sight" ] })",
                     d ) );
    CHECK_FALSE( test_condition(
                     R"({ "faction_has_relation": [ "test_npc_ai_hawks", "test_npc_ai_doves", "share my stuff" ] })",
                     d ) );
    // a faction that doesn't exist has none
    CHECK_FALSE( test_condition(
                     R"({ "faction_has_relation": [ "test_npc_ai_nobody", "test_npc_ai_doves", "kill on sight" ] })",
                     d ) );

    run_effect(
        R"([ { "set_faction_relation": [ "test_npc_ai_hawks", "test_npc_ai_doves", "kill on sight" ], "set_value_to": false } ])",
        d );
    CHECK_FALSE( g->faction_manager_ptr->get( faction_test_npc_ai_hawks )->has_relationship(
                     faction_test_npc_ai_doves, npc_factions::relationship::kill_on_sight ) );
    CHECK_FALSE( test_condition(
                     R"({ "faction_has_relation": [ "test_npc_ai_hawks", "test_npc_ai_doves", "kill on sight" ] })",
                     d ) );
    get_globals().set_global_value( "test_ai_var", std::string( "share my stuff" ) );
    run_effect(
        R"([ { "set_faction_relation": [ "test_npc_ai_doves", "test_npc_ai_hawks", { "global_val": "test_ai_var" } ] } ])",
        d );
    CHECK( g->faction_manager_ptr->get( faction_test_npc_ai_doves )->has_relationship(
               faction_test_npc_ai_hawks, npc_factions::relationship::share_my_stuff ) );
    CHECK( test_condition(
               R"({ "faction_has_relation": [ "test_npc_ai_doves", "test_npc_ai_hawks", { "global_val": "test_ai_var" } ] })",
               d ) );

    // relation names written out are checked at load, and there are three entries
    CHECK_THROWS_AS( test_condition(
                         R"({ "faction_has_relation": [ "test_npc_ai_hawks", "test_npc_ai_doves", "kill on site" ] })",
                         d ), JsonError );
    CHECK_THROWS_AS( test_condition(
                         R"({ "faction_has_relation": [ "test_npc_ai_hawks", "kill on sight" ] })", d ), JsonError );
    CHECK_THROWS_AS( load_effect(
                         R"([ { "set_faction_relation": [ "test_npc_ai_hawks", "test_npc_ai_doves", "hates" ] } ])" ),
                     JsonError );
}

// ---- NPC-J05: effects ----

TEST_CASE( "npc_set_attitude_goes_through_the_talk_effects", "[npc][npc_ai_hooks][eoc]" )
{
    const ai_hooks_world world;
    npc &guy = hooks_npc();
    npc_event_recorder rec;
    dialogue d( get_talker_for( get_avatar() ), get_talker_for( guy ) );

    // as hostile: one npc_becomes_hostile, caused by an effect
    run_effect( R"([ { "npc_set_attitude": "NPCATT_KILL" } ])", d );
    CHECK( guy.get_attitude() == NPCATT_KILL );
    REQUIRE( rec.of( event_type::npc_becomes_hostile ).size() == 1 );
    CHECK( str_field( rec.of( event_type::npc_becomes_hostile ).front(), "cause" ) == "effect" );
    // as flee: running from the avatar for a while
    run_effect( R"([ { "npc_set_attitude": "NPCATT_FLEE" } ])", d );
    CHECK( guy.get_attitude() == NPCATT_FLEE_TEMP );
    CHECK( guy.has_effect( effect_npc_flee_player ) );
    // as player_leaving: patience from its aggression
    guy.personality.aggression = 4;
    run_effect( R"([ { "npc_set_attitude": "NPCATT_WAIT_FOR_LEAVE" } ])", d );
    CHECK( guy.get_attitude() == NPCATT_WAIT_FOR_LEAVE );
    CHECK( guy.patience == 11 );
    run_effect( R"([ { "npc_set_attitude": "NPCATT_MUG" } ])", d );
    CHECK( guy.get_attitude() == NPCATT_MUG );
    // as follow_only: follows without joining the avatar's faction, mission and post dropped
    guy.set_mission( NPC_MISSION_GUARD );
    guy.guard_pos = guy.pos_abs();
    run_effect( R"([ { "npc_set_attitude": "NPCATT_FOLLOW" } ])", d );
    CHECK( guy.get_attitude() == NPCATT_FOLLOW );
    CHECK( guy.mission == NPC_MISSION_NULL );
    CHECK_FALSE( guy.guard_pos );
    CHECK_FALSE( guy.is_player_ally() );
    // others as they are, from a variable too
    get_globals().set_global_value( "test_ai_var", std::string( "NPCATT_TALK" ) );
    run_effect( R"([ { "npc_set_attitude": { "global_val": "test_ai_var" } } ])", d );
    CHECK( guy.get_attitude() == NPCATT_TALK );

    // the NPC as u
    dialogue alone( get_talker_for( guy ), nullptr );
    run_effect( R"([ { "u_set_attitude": "NPCATT_NULL" } ])", alone );
    CHECK( guy.get_attitude() == NPCATT_NULL );
    // no beta, the avatar, a monster: nothing happens
    rec.events.clear();
    run_effect( R"([ { "npc_set_attitude": "NPCATT_KILL" } ])", alone );
    run_effect( R"([ { "u_set_attitude": "NPCATT_KILL" } ])", d );
    monster &zombie = spawn_test_monster( "mon_zombie", npc_spot + tripoint( 3, 0, 0 ) );
    dialogue with_monster( get_talker_for( guy ), get_talker_for( zombie ) );
    run_effect( R"([ { "npc_set_attitude": "NPCATT_KILL" } ])", with_monster );
    clear_creatures();
    CHECK( guy.get_attitude() == NPCATT_NULL );
    CHECK( rec.events.empty() );

    // attitudes it can't set are load errors
    CHECK_THROWS_AS( load_effect( R"([ { "npc_set_attitude": "NPCATT_ACTIVITY" } ])" ), JsonError );
    CHECK_THROWS_AS( load_effect( R"([ { "npc_set_attitude": "NPCATT_RECOVER_GOODS" } ])" ),
                     JsonError );
    CHECK_THROWS_AS( load_effect( R"([ { "npc_set_attitude": "NPCATT_LEGACY_2" } ])" ), JsonError );
    CHECK_THROWS_AS( load_effect( R"([ { "u_set_attitude": "angry" } ])" ), JsonError );
}

TEST_CASE( "npc_set_mission_goes_through_the_talk_effects", "[npc][npc_ai_hooks][eoc]" )
{
    const ai_hooks_world world;
    npc &guy = hooks_npc();
    dialogue d( get_talker_for( get_avatar() ), get_talker_for( guy ) );

    // as assign_guard: a guard where it stands
    run_effect( R"([ { "npc_set_mission": "GUARD" } ])", d );
    CHECK( guy.mission == NPC_MISSION_GUARD );
    CHECK( guy.guard_pos == guy.pos_abs() );
    // as stop_guard
    run_effect( R"([ { "npc_set_mission": "NULL" } ])", d );
    CHECK( guy.mission == NPC_MISSION_NULL );
    // an ally guards as an ally, and goes back to following
    guy.set_fac( faction_your_followers );
    guy.set_attitude( NPCATT_FOLLOW );
    REQUIRE( guy.is_player_ally() );
    run_effect( R"([ { "npc_set_mission": "GUARD" } ])", d );
    CHECK( guy.mission == NPC_MISSION_GUARD_ALLY );
    CHECK( guy.get_attitude() == NPCATT_NULL );
    run_effect( R"([ { "npc_set_mission": "NULL" } ])", d );
    CHECK( guy.mission == NPC_MISSION_NULL );
    CHECK( guy.get_attitude() == NPCATT_FOLLOW );
    CHECK_FALSE( guy.guard_pos );
    guy.set_fac( faction_tacoma_commune );
    guy.set_attitude( NPCATT_NULL );

    // one without a talk effect, as u: kept where it stands
    dialogue alone( get_talker_for( guy ), nullptr );
    run_effect( R"([ { "u_set_mission": "SHOPKEEP" } ])", alone );
    CHECK( guy.mission == NPC_MISSION_SHOPKEEP );
    CHECK( guy.guard_pos == guy.pos_abs() );
    // a camp resident needs a camp
    REQUIRE_FALSE( guy.assigned_camp );
    run_effect( R"([ { "u_set_mission": "CAMP_RESIDENT" } ])", alone );
    CHECK( guy.mission == NPC_MISSION_SHOPKEEP );
    // no beta, the avatar: nothing happens
    run_effect( R"([ { "npc_set_mission": "GUARD_PATROL" } ])", alone );
    run_effect( R"([ { "u_set_mission": "GUARD_PATROL" } ])", d );
    CHECK( guy.mission == NPC_MISSION_SHOPKEEP );

    CHECK_THROWS_AS( load_effect( R"([ { "npc_set_mission": "TRAVELLING" } ])" ), JsonError );
    CHECK_THROWS_AS( load_effect( R"([ { "npc_set_mission": "ACTIVITY" } ])" ), JsonError );
    CHECK_THROWS_AS( load_effect( R"([ { "npc_set_mission": "LEGACY_1" } ])" ), JsonError );
}
