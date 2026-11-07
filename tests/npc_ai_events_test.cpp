#include <algorithm>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "avatar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "cata_variant.h"
#include "character_id.h"
#include "coordinates.h"
#include "debug.h"
#include "dialogue.h"
#include "event.h"
#include "faction.h"
#include "game.h"
#include "global_vars.h"
#include "item.h"
#include "map.h"
#include "map_helpers_tests.h"
#include "memorial_logger.h"
#include "npc.h"
#include "npc_ai_hooks_helpers.h"
#include "npc_attitude_cause.h"
#include "npc_opinion.h"
#include "npc_turn_helpers.h"
#include "npctalk.h"
#include "pimpl.h"
#include "point.h"
#include "stats_tracker.h"
#include "type_id.h"

// NPC-J04: the events an NPC's attitude and allegiance changes fire, whatever changes them
// (doc/JSON/EFFECT_ON_CONDITION.md#npc-attitude-events), and the data-driven mutiny faction.

static const efftype_id effect_npc_flee_player( "npc_flee_player" );
static const efftype_id effect_npc_run_away( "npc_run_away" );

static const faction_id faction_amf( "amf" );
static const faction_id faction_debuggers( "debuggers" );
static const faction_id faction_tacoma_commune( "tacoma_commune" );
static const faction_id faction_test_npc_ai_deserters( "test_npc_ai_deserters" );
static const faction_id faction_test_npc_ai_hawks( "test_npc_ai_hawks" );
static const faction_id faction_your_followers( "your_followers" );

static const itype_id itype_rock( "rock" );

TEST_CASE( "npc_attitude_changed_fires_once_per_change", "[npc][npc_ai_hooks][event]" )
{
    const ai_hooks_world world;
    npc &guy = hooks_npc();
    npc_event_recorder rec;

    guy.set_attitude( NPCATT_TALK );
    std::vector<cata::event> changes = rec.of( event_type::npc_attitude_changed );
    REQUIRE( changes.size() == 1 );
    CHECK( changes[0].get<cata_variant_type::character_id>( "npc" ) == guy.getID() );
    CHECK( str_field( changes[0], "npc_name" ) == guy.name );
    CHECK( str_field( changes[0], "old_attitude" ) == "NPCATT_NULL" );
    CHECK( str_field( changes[0], "new_attitude" ) == "NPCATT_TALK" );
    // nothing says why
    CHECK( str_field( changes[0], "cause" ).empty() );
    CHECK( rec.of( event_type::npc_becomes_hostile ).empty() );

    // no change, no event
    guy.set_attitude( NPCATT_TALK );
    CHECK( rec.of( event_type::npc_attitude_changed ).size() == 1 );
    // NPCATT_FLEE is NPCATT_FLEE_TEMP: asked of an NPC that flees already, it changes nothing
    guy.set_attitude( NPCATT_FLEE_TEMP );
    REQUIRE( rec.of( event_type::npc_attitude_changed ).size() == 2 );
    guy.set_attitude( NPCATT_FLEE );
    CHECK( guy.get_attitude() == NPCATT_FLEE_TEMP );
    CHECK( rec.of( event_type::npc_attitude_changed ).size() == 2 );

    // the cause in force
    {
        const npc_attitude_cause outer( npc_attitude_cause::ai );
        {
            const npc_attitude_cause inner( npc_attitude_cause::panic );
            guy.set_attitude( NPCATT_NULL );
        }
        guy.set_attitude( NPCATT_TALK );
    }
    changes = rec.of( event_type::npc_attitude_changed );
    REQUIRE( changes.size() == 4 );
    CHECK( str_field( changes[2], "cause" ) == "panic" );
    CHECK( str_field( changes[3], "cause" ) == "ai" );
    CHECK( std::string( npc_attitude_cause::current() ).empty() );

    // an activity finishing puts the old attitude back
    guy.set_attitude( NPCATT_ACTIVITY );
    guy.revert_after_activity();
    changes = rec.of( event_type::npc_attitude_changed );
    REQUIRE( changes.size() == 6 );
    CHECK( str_field( changes[5], "old_attitude" ) == "NPCATT_ACTIVITY" );
    CHECK( str_field( changes[5], "new_attitude" ) == "NPCATT_TALK" );
    CHECK( guy.get_attitude() == NPCATT_TALK );

    // an NPC no one meets (a template's, a stand-in) isn't reported
    npc nobody;
    REQUIRE_FALSE( nobody.getID().is_valid() );
    nobody.set_attitude( NPCATT_KILL );
    CHECK( rec.events.size() == 6 );
}

TEST_CASE( "npc_attitude_changed_runs_eocs_with_the_npc_as_u", "[npc][npc_ai_hooks][event][eoc]" )
{
    const ai_hooks_world world;
    npc &guy = hooks_npc();
    get_globals().set_global_value( "test_ai_listen", 1.0 );
    guy.op_of_u.fear = -10;
    guy.make_angry();
    REQUIRE( guy.get_attitude() == NPCATT_KILL );
    CHECK( global_number( "test_ai_attitude_changes" ) == 1 );
    CHECK( global_string( "test_ai_old" ) == "NPCATT_NULL" );
    CHECK( global_string( "test_ai_new" ) == "NPCATT_KILL" );
    CHECK( global_string( "test_ai_cause" ) == "angered" );
    CHECK( global_string( "test_ai_u" ) == guy.get_name() );
    // what statistics don't keep
    CHECK_FALSE( stats_tracker::records( event_type::npc_attitude_changed ) );
    CHECK( stats_tracker::records( event_type::npc_becomes_hostile ) );
    CHECK( stats_tracker::records( event_type::npc_recruited ) );
    CHECK( stats_tracker::records( event_type::npc_dismissed ) );
    CHECK( stats_tracker::records( event_type::npc_mutinies ) );
}

TEST_CASE( "npc_becomes_hostile_whenever_the_npc_turns_hostile", "[npc][npc_ai_hooks][event]" )
{
    const ai_hooks_world world;
    npc &guy = hooks_npc();
    npc_event_recorder rec;
    const auto hostile_count = [&rec]() {
        return rec.of( event_type::npc_becomes_hostile ).size();
    };

    // into the hostile group
    guy.set_attitude( NPCATT_MUG );
    REQUIRE( hostile_count() == 1 );
    const cata::event first = rec.of( event_type::npc_becomes_hostile ).front();
    CHECK( first.get<cata_variant_type::character_id>( "npc" ) == guy.getID() );
    CHECK( str_field( first, "npc_name" ) == guy.name );
    CHECK( str_field( first, "cause" ).empty() );
    // within it, only a turn to kill counts
    guy.set_attitude( NPCATT_WAIT_FOR_LEAVE );
    CHECK( hostile_count() == 1 );
    guy.set_attitude( NPCATT_KILL );
    CHECK( hostile_count() == 2 );
    // a flight doesn't calm it down: back to killing is no new hostility, nor is de-escalating
    guy.set_attitude( NPCATT_FLEE_TEMP );
    guy.set_attitude( NPCATT_KILL );
    guy.set_attitude( NPCATT_MUG );
    guy.set_attitude( NPCATT_KILL );
    CHECK( hostile_count() == 2 );
    // a neutral attitude does
    guy.set_attitude( NPCATT_NULL );
    guy.set_attitude( NPCATT_KILL );
    CHECK( hostile_count() == 3 );
    guy.set_attitude( NPCATT_NULL );

    // the hostile talk effect: once, after the attitude changed, caused by an effect
    dialogue d( get_talker_for( get_avatar() ), get_talker_for( guy ) );
    rec.events.clear();
    run_effect( R"([ "hostile" ])", d );
    REQUIRE( guy.get_attitude() == NPCATT_KILL );
    REQUIRE( hostile_count() == 1 );
    CHECK( str_field( rec.of( event_type::npc_becomes_hostile ).front(), "cause" ) == "effect" );
    CHECK( rec.of( event_type::npc_attitude_changed ).size() == 1 );
    run_effect( R"([ "hostile" ])", d );
    CHECK( hostile_count() == 1 );

    // angered by the avatar
    guy.set_attitude( NPCATT_NULL );
    rec.events.clear();
    guy.op_of_u.fear = -10;
    guy.make_angry();
    REQUIRE( hostile_count() == 1 );
    CHECK( str_field( rec.of( event_type::npc_becomes_hostile ).front(), "cause" ) == "angered" );
}

TEST_CASE( "npc_recruited_and_dismissed_from_the_follower_list", "[npc][npc_ai_hooks][event]" )
{
    const ai_hooks_world world;
    npc &guy = hooks_npc();
    npc_event_recorder rec;

    g->add_npc_follower( guy.getID() );
    std::vector<cata::event> joined = rec.of( event_type::npc_recruited );
    REQUIRE( joined.size() == 1 );
    CHECK( joined[0].get<cata_variant_type::character_id>( "npc" ) == guy.getID() );
    CHECK( str_field( joined[0], "npc_name" ) == guy.name );
    // already a follower: no change, no event
    g->add_npc_follower( guy.getID() );
    CHECK( rec.of( event_type::npc_recruited ).size() == 1 );

    g->remove_npc_follower( guy.getID() );
    std::vector<cata::event> left = rec.of( event_type::npc_dismissed );
    REQUIRE( left.size() == 1 );
    CHECK( left[0].get<cata_variant_type::character_id>( "npc" ) == guy.getID() );
    CHECK( str_field( left[0], "npc_name" ) == guy.name );
    g->remove_npc_follower( guy.getID() );
    CHECK( rec.of( event_type::npc_dismissed ).size() == 1 );

    // the follow talk effect recruits through the list
    rec.events.clear();
    talk_function::follow( guy );
    CHECK( rec.of( event_type::npc_recruited ).size() == 1 );
    guy.set_fac( faction_tacoma_commune );

    // a follower that dies leaves the list without being dismissed
    rec.events.clear();
    const character_id id = guy.getID();
    REQUIRE( get_avatar().follower_ids.count( id ) == 1 );
    guy.die( &get_map(), nullptr );
    g->cleanup_dead();
    CHECK( get_avatar().follower_ids.count( id ) == 0 );
    CHECK( rec.of( event_type::npc_dismissed ).empty() );
}

TEST_CASE( "npc_mutinies_to_its_factions_mutiny_faction", "[npc][npc_ai_hooks][event][faction]" )
{
    const ai_hooks_world world;
    npc &guy = hooks_npc();
    const auto make_follower = [&guy]() {
        guy.set_fac( faction_your_followers );
        guy.set_attitude( NPCATT_FOLLOW );
        g->add_npc_follower( guy.getID() );
        REQUIRE( guy.is_player_ally() );
    };
    get_globals().set_global_value( "test_ai_listen", 1.0 );

    // vanilla: to amf
    make_follower();
    npc_event_recorder rec;
    guy.mutiny();
    CHECK( guy.get_fac_id() == faction_amf );
    const std::vector<cata::event> mutinies = rec.of( event_type::npc_mutinies );
    REQUIRE( mutinies.size() == 1 );
    CHECK( mutinies[0].get<cata_variant_type::character_id>( "npc" ) == guy.getID() );
    CHECK( str_field( mutinies[0], "npc_name" ) == guy.name );
    CHECK( mutinies[0].get<cata_variant_type::faction_id>( "old_faction" ) == faction_your_followers );
    CHECK( mutinies[0].get<cata_variant_type::faction_id>( "new_faction" ) == faction_amf );
    // it left the follower list and dropped its attitude on the way, for the mutiny
    REQUIRE( rec.of( event_type::npc_dismissed ).size() == 1 );
    const std::vector<cata::event> changes = rec.of( event_type::npc_attitude_changed );
    REQUIRE( changes.size() == 1 );
    CHECK( str_field( changes[0], "new_attitude" ) == "NPCATT_NULL" );
    CHECK( str_field( changes[0], "cause" ) == "mutiny" );
    CHECK( rec.events.back().type() == event_type::npc_mutinies );
    // the EOC runs with the NPC as u
    CHECK( global_string( "test_ai_mutiny_to" ) == "amf" );
    CHECK( global_string( "test_ai_mutiny_u" ) == guy.get_name() );

    // the faction it leaves says where it goes
    faction *followers = g->faction_manager_ptr->get( faction_your_followers );
    REQUIRE( followers->mutiny_faction == faction_amf );
    followers->mutiny_faction = faction_test_npc_ai_deserters;
    make_follower();
    guy.mutiny();
    CHECK( guy.get_fac_id() == faction_test_npc_ai_deserters );
    CHECK( global_string( "test_ai_mutiny_to" ) == "test_npc_ai_deserters" );

    // a follower that is angered mutinies; the faction it joins isn't angered for it
    make_follower();
    const faction *deserters = g->faction_manager_ptr->get( faction_test_npc_ai_deserters );
    const int deserters_like = deserters->likes_u;
    guy.op_of_u.fear = -10;
    guy.make_angry();
    CHECK( guy.get_fac_id() == faction_test_npc_ai_deserters );
    CHECK( guy.get_attitude() == NPCATT_KILL );
    CHECK( deserters->likes_u == deserters_like );
    // a stranger's faction is, as before
    guy.set_fac( faction_test_npc_ai_hawks );
    guy.set_attitude( NPCATT_NULL );
    const faction *hawks = g->faction_manager_ptr->get( faction_test_npc_ai_hawks );
    const int hawks_like = hawks->likes_u;
    guy.make_angry();
    CHECK( hawks->likes_u == std::min( -15, hawks_like - 5 ) );

    // read from the JSON, amf where it says nothing
    CHECK( g->faction_manager_ptr->get( faction_test_npc_ai_hawks )->mutiny_faction ==
           faction_test_npc_ai_deserters );
    CHECK( g->faction_manager_ptr->get( faction_debuggers )->mutiny_faction == faction_amf );
}

// The cycle a hostile NPC can fall into: it panics and runs (or, stuck, gives up on reaching the
// avatar), and on its next turn its faction sets it on the avatar again.  That's one turn to
// hostility, not one a turn.
TEST_CASE( "npc_becomes_hostile_once_while_it_flees_and_comes_back", "[npc][npc_ai_hooks][event]" )
{
    const ai_hooks_world world;
    set_time_to_day();
    npc &guy = hooks_npc();
    get_avatar().setpos( get_map(), npc_spot + tripoint( -20, 0, 0 ) );
    get_map().build_map_cache( 0 );
    guy.set_fac( faction_test_npc_ai_hawks );
    g->faction_manager_ptr->get( faction_test_npc_ai_hawks )->likes_u = -50;
    REQUIRE( guy.guaranteed_hostile() );
    REQUIRE( guy.sees( get_map(), get_avatar() ) );
    npc_event_recorder rec;

    // on its turn its faction sets it on the avatar
    run_npc_turn( guy );
    REQUIRE( guy.get_attitude() == NPCATT_KILL );
    REQUIRE( rec.of( event_type::npc_becomes_hostile ).size() == 1 );
    CHECK( str_field( rec.of( event_type::npc_becomes_hostile ).front(), "cause" ) == "faction" );

    for( int i = 0; i < 5; ++i ) {
        CAPTURE( i );
        // outmatched, it panics and runs
        guy.remove_effect( effect_npc_run_away );
        guy.mem_combat.assess_enemy = 10.0f;
        guy.mem_combat.assess_ally = 1.0f;
        guy.mem_combat.failing_to_reposition = 0;
        guy.mem_combat.reposition_countdown = 0;
        guy.personality.bravery = -10;
        guy.act_on_danger_assessment();
        REQUIRE( guy.get_attitude() == NPCATT_FLEE_TEMP );
        // and on its turn its faction sets it on the avatar again
        guy.personality.bravery = 0;
        guy.op_of_u.fear = 0;
        run_npc_turn( guy );
    }
    CHECK( rec.of( event_type::npc_becomes_hostile ).size() == 1 );
    // every change is an npc_attitude_changed, with what made it
    int panics = 0;
    int returns = 0;
    for( const cata::event &e : rec.of( event_type::npc_attitude_changed ) ) {
        if( str_field( e, "new_attitude" ) == "NPCATT_FLEE_TEMP" && str_field( e, "cause" ) == "panic" ) {
            panics++;
        } else if( str_field( e, "new_attitude" ) == "NPCATT_KILL" &&
                   str_field( e, "cause" ) == "faction" ) {
            returns++;
        }
    }
    CHECK( panics >= 1 );
    // it came back turn after turn, and turned hostile once
    CHECK( returns >= 4 );
}

TEST_CASE( "npc_attitude_causes_on_its_turn_and_for_a_theft", "[npc][npc_ai_hooks][event]" )
{
    const ai_hooks_world world;
    npc &guy = hooks_npc();
    // a flight that ran out ends on the NPC's turn
    guy.set_attitude( NPCATT_FLEE_TEMP );
    guy.remove_effect( effect_npc_flee_player );
    npc_event_recorder rec;
    run_npc_turn( guy );
    REQUIRE( guy.get_attitude() == NPCATT_NULL );
    std::vector<cata::event> changes = rec.of( event_type::npc_attitude_changed );
    REQUIRE( changes.size() == 1 );
    CHECK( str_field( changes[0], "cause" ) == "ai" );

    // it saw the avatar steal
    item stolen( itype_rock );
    guy.witness_thievery( &stolen );
    guy.known_stolen_item = nullptr;
    REQUIRE( guy.get_attitude() == NPCATT_RECOVER_GOODS );
    changes = rec.of( event_type::npc_attitude_changed );
    REQUIRE( changes.size() == 2 );
    CHECK( str_field( changes[1], "cause" ) == "theft" );
    guy.set_attitude( NPCATT_NULL );
}

// An EOC of the attitude events that changes the attitude again isn't reported: an EOC that always
// does would go on without end.  Its change is an error, said once.
TEST_CASE( "npc_attitude_set_by_its_own_events_isnt_reported", "[npc][npc_ai_hooks][event][eoc]" )
{
    const ai_hooks_world world;
    npc &guy = hooks_npc();
    npc_event_recorder rec;

    get_globals().set_global_value( "test_ai_flip", 1.0 );
    std::string error = capture_debugmsg_during( [&guy]() {
        guy.set_attitude( NPCATT_TALK );
    } );
    get_globals().remove_global_value( "test_ai_flip" );
    CHECK( error.find( "while its attitude events ran" ) != std::string::npos );
    CHECK( rec.of( event_type::npc_attitude_changed ).size() == 1 );
    CHECK( guy.get_attitude() == NPCATT_NULL );

    // an NPC that is no longer hostile once those EOCs ran never turned hostile
    rec.events.clear();
    get_globals().set_global_value( "test_ai_calm", 1.0 );
    error = capture_debugmsg_during( [&guy]() {
        guy.set_attitude( NPCATT_KILL );
    } );
    get_globals().remove_global_value( "test_ai_calm" );
    CHECK( error.find( "while its attitude events ran" ) != std::string::npos );
    CHECK( guy.get_attitude() == NPCATT_NULL );
    CHECK( rec.of( event_type::npc_attitude_changed ).size() == 1 );
    CHECK( rec.of( event_type::npc_becomes_hostile ).empty() );

    // what comes after is reported as usual
    rec.events.clear();
    guy.set_attitude( NPCATT_KILL );
    CHECK( rec.of( event_type::npc_attitude_changed ).size() == 1 );
    CHECK( rec.of( event_type::npc_becomes_hostile ).size() == 1 );
}

// What an NPC takes as it's made is its starting attitude: no events, and it counts as reported.
TEST_CASE( "npc_starting_attitude_isnt_reported", "[npc][npc_ai_hooks][event]" )
{
    const ai_hooks_world world;
    npc &guy = hooks_npc();
    npc_event_recorder rec;
    {
        const npc_attitude_cause spawning( npc_attitude_cause::spawn );
        guy.set_attitude( NPCATT_KILL );
    }
    CHECK( rec.events.empty() );
    guy.set_attitude( NPCATT_FLEE_TEMP );
    guy.set_attitude( NPCATT_KILL );
    CHECK( rec.of( event_type::npc_attitude_changed ).size() == 2 );
    CHECK( rec.of( event_type::npc_becomes_hostile ).empty() );
}

// The memorial notes the hostile effect, as when that effect alone sent npc_becomes_hostile;
// not the other effects that make an NPC hostile.
TEST_CASE( "memorial_notes_the_hostile_effect_alone", "[npc][npc_ai_hooks][memorial]" )
{
    const ai_hooks_world world;
    npc &guy = hooks_npc();
    memorial_logger &memorial = get_memorial();
    std::ostringstream saved;
    memorial.save( saved );
    const on_out_of_scope restore_memorial( [&memorial, &saved]() {
        std::istringstream in( saved.str() );
        memorial.load( in );
    } );
    memorial.clear();
    dialogue d( get_talker_for( get_avatar() ), get_talker_for( guy ) );
    for( const char *effect : {
             "start_mugging", "player_leaving", "insult_combat"
         } ) {
        CAPTURE( effect );
        guy.set_attitude( NPCATT_NULL );
        run_effect( std::string( "[ \"" ) + effect + "\" ]", d );
        CHECK( guy.get_attitude_group( guy.get_attitude() ) == attitude_group::hostile );
        CHECK( memorial.dump().empty() );
    }
    guy.set_attitude( NPCATT_NULL );
    run_effect( R"([ "hostile" ])", d );
    REQUIRE( guy.get_attitude() == NPCATT_KILL );
    const std::string log = memorial.dump();
    CHECK( log.find( guy.name + " became hostile." ) != std::string::npos );
    CHECK( log.find( "became hostile." ) == log.rfind( "became hostile." ) );
}

// Recruiting finishes before npc_recruited goes out: what its EOCs do to the recruit stays.
TEST_CASE( "npc_recruited_goes_out_once_recruited", "[npc][npc_ai_hooks][event][eoc]" )
{
    const ai_hooks_world world;
    npc &guy = hooks_npc();
    get_globals().set_global_value( "test_ai_guard_recruits", 1.0 );
    talk_function::follow( guy );
    get_globals().remove_global_value( "test_ai_guard_recruits" );
    CHECK( guy.is_player_ally() );
    CHECK( guy.mission == NPC_MISSION_GUARD_ALLY );
    CHECK( guy.guard_pos == guy.pos_abs() );
    guy.set_fac( faction_tacoma_commune );
}

// Putting the follower list back together recruits no one.
TEST_CASE( "npc_follower_list_rebuilt_without_events", "[npc][npc_ai_hooks][event]" )
{
    const ai_hooks_world world;
    npc &guy = hooks_npc();
    guy.set_fac( faction_your_followers );
    guy.set_attitude( NPCATT_FOLLOW );
    REQUIRE( get_avatar().follower_ids.count( guy.getID() ) == 0 );
    npc_event_recorder rec;
    g->validate_npc_followers();
    CHECK( get_avatar().follower_ids.count( guy.getID() ) == 1 );
    CHECK( rec.of( event_type::npc_recruited ).empty() );
    guy.set_fac( faction_tacoma_commune );
}
