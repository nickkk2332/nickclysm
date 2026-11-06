#include <sstream>
#include <string>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "dialogue.h"
#include "execution.h"
#include "execution_runtime.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "message_log.h"
#include "messages.h"
#include "monster.h"
#include "options_helpers.h"
#include "player_helpers.h"
#include "rng.h"
#include "talker.h"
#include "type_id.h"

namespace
{
struct narration_world {
    time_point old_time = calendar::turn;
    override_option enabled{ "EXECUTION_NARRATION", "true" };
    scoped_weather_override weather{ weather_type_id( "clear" ) };
    narration_world() {
        g->executions().clear();
        clear_avatar();
        clear_map_without_vision( -1, 1 );
        calendar::turn = calendar::turn_zero + 12_hours;
        get_avatar().setpos( get_map(), tripoint_bub_ms( 60, 60, 0 ) );
        get_map().build_map_cache( 0 );
        // clear_avatar() leaves the cached sight limit of whichever test ran before.
        get_avatar().recalc_sight_limits();
        Messages::clear_messages();
    }
    ~narration_world() {
        g->executions().clear();
        calendar::turn = old_time;
    }
};

monster &victim()
{
    return spawn_test_monster( "mon_zombie", get_avatar().pos_bub() + tripoint_rel_ms( 2, 0, 0 ) );
}

int64_t start_narration( const std::string &definition, Creature &target )
{
    dialogue d( get_talker_for( get_avatar() ), get_talker_for( target ) );
    const auto result = g->executions().start( execution_id( definition ), d, 90210 );
    REQUIRE( result.id > 0 );
    g->executions().drain_starts();
    return result.id;
}

void advance_narration()
{
    calendar::turn += 1_turns;
    g->executions().process();
}

std::string save_narration()
{
    std::ostringstream text;
    JsonOut out( text );
    out.start_object();
    Messages::serialize( out );
    out.member( "executions", g->executions() );
    out.end_object();
    return text.str();
}
} // namespace

TEST_CASE( "execution_narration_appends_without_rewriting_chronological_history",
           "[executions][messages]" )
{
    narration_world world;
    monster &target = victim();
    REQUIRE( get_avatar().sees( get_map(), target ) );
    const int64_t id = start_narration( "test_exec_narration", target );
    const bool intervening_combat = GENERATE( false, true );
    if( intervening_combat ) {
        add_msg( "A different creature attacks." );
    }
    advance_narration();
    advance_narration();
    const auto segments = g->executions().narration( id );
    REQUIRE( segments.size() == 3 );
    CHECK( segments[0].group == segments[1].group );
    CHECK( segments[1].group == segments[2].group );
    CHECK( segments[0].id != segments[1].id );
    CHECK( segments[0].when + 1_turns == segments[1].when );
    CHECK( segments[1].when + 1_turns == segments[2].when );
    CHECK( segments[1].text != segments[2].text );
    const auto history = Messages::recent_messages( 20 );
    REQUIRE( history.size() == ( intervening_combat ? 4 : 3 ) );
    CHECK( history.front().second == segments[0].text );
    const auto compact = Messages::recent_messages_compact( 20 );
    REQUIRE( compact.size() == ( intervening_combat ? 3 : 1 ) );
    if( intervening_combat ) {
        CHECK( history[1].second == "A different creature attacks." );
        CHECK( compact[1].second == history[1].second );
        CHECK( compact[2].second == segments[1].text + " " + segments[2].text );
    } else {
        CHECK( compact[0].second == segments[0].text + " " + segments[1].text + " " + segments[2].text );
    }
}

TEST_CASE( "execution_narration_groups_are_owned_by_the_specific_instance",
           "[executions][messages]" )
{
    narration_world world;
    monster &target = victim();
    const int64_t one = start_narration( "test_exec_narration", target );
    const int64_t two = start_narration( "test_exec_narration", target );
    advance_narration();
    advance_narration();
    const auto first = g->executions().narration( one );
    const auto second = g->executions().narration( two );
    REQUIRE( first.size() == 3 );
    REQUIRE( second.size() == 3 );
    CHECK( first[0].group != second[0].group );
    CHECK( first[2].group == first[0].group );
    CHECK( second[2].group == second[0].group );
    CHECK( Messages::recent_messages_compact( 20 ).size() == 6 );
}

TEST_CASE( "execution_narration_resume_keeps_groups_choices_and_segment_deadlines",
           "[executions][messages][save]" )
{
    narration_world world;
    monster &target = victim();
    const int64_t id = start_narration( "test_exec_narration", target );
    advance_narration();
    const std::string saved = save_narration();
    const time_point saved_time = calendar::turn;
    const auto before = g->executions().narration( id );
    REQUIRE( before.size() == 2 );
    advance_narration();
    const auto expected = g->executions().narration( id );
    const JsonObject obj = json_loader::from_string( saved );
    Messages::deserialize( obj );
    g->executions().deserialize( obj.get_object( "executions" ) );
    calendar::turn = saved_time;
    g->executions().process();
    REQUIRE( g->executions().narration( id ).size() == 2 );
    CHECK( g->executions().narration( id )[0].id == before[0].id );
    advance_narration();
    const auto actual = g->executions().narration( id );
    REQUIRE( actual.size() == 3 );
    CHECK( actual[2].text == expected[2].text );
    CHECK( actual[2].when == expected[2].when );
    CHECK( actual[2].group == before[0].group );
    CHECK( Messages::recent_messages( 20 ).size() == 3 );
    CHECK( Messages::recent_messages_compact( 20 ).size() == 1 );
}

TEST_CASE( "execution_narration_missing_or_unobserved_anchors_create_fresh_visible_groups",
           "[executions][messages]" )
{
    narration_world world;
    monster &target = victim();
    const bool hidden = GENERATE( false, true );
    if( hidden ) {
        get_avatar().add_effect( efftype_id( "blind" ), 1_hours );
        get_avatar().recalc_sight_limits();
        REQUIRE_FALSE( get_avatar().sees( get_map(), target ) );
    }
    const int64_t id = start_narration( "test_exec_narration", target );
    if( hidden ) {
        CHECK( g->executions().narration( id ).empty() );
        get_avatar().remove_effect( efftype_id( "blind" ) );
        get_avatar().recalc_sight_limits();
    }
    Messages::clear_messages();
    advance_narration();
    advance_narration();
    const auto history = Messages::recent_messages( 20 );
    REQUIRE( history.size() == 2 );
    CHECK( history[0].second.find( "staggers" ) == std::string::npos );
    CHECK( Messages::recent_messages_compact( 20 ).size() == 1 );
    const auto segments = g->executions().narration( id );
    REQUIRE( segments.size() == ( hidden ? 2 : 3 ) );
    CHECK( segments.back().group == segments[segments.size() - 2].group );
    if( !hidden ) {
        CHECK( segments[0].group != segments[1].group );
    }
}

TEST_CASE( "execution_narration_selects_visual_or_heard_text_without_exposing_unseen_names",
           "[executions][messages]" )
{
    narration_world world;
    monster &target = victim();
    const bool blind = GENERATE( false, true );
    const bool deaf = GENERATE( false, true );
    if( blind ) {
        get_avatar().add_effect( efftype_id( "blind" ), 1_hours );
        get_avatar().recalc_sight_limits();
        REQUIRE_FALSE( get_avatar().sees( get_map(), target ) );
    }
    if( deaf ) {
        get_avatar().add_effect( efftype_id( "deaf" ), 1_hours );
        REQUIRE( get_avatar().is_deaf() );
    }
    Messages::clear_messages();
    const int64_t id = start_narration( "test_exec_narration_senses", target );
    const auto segments = g->executions().narration( id );
    if( blind && deaf ) {
        CHECK( segments.empty() );
        CHECK( Messages::size() == 0 );
    } else {
        REQUIRE( segments.size() == 1 );
        if( blind ) {
            CHECK( segments[0].audience == "audible" );
            CHECK( segments[0].text == "You hear someone coughing." );
            CHECK( segments[0].text.find( target.get_name() ) == std::string::npos );
        } else {
            CHECK( segments[0].audience == "visible" );
            CHECK( segments[0].text.find( "stares at" ) != std::string::npos );
        }
    }
}

TEST_CASE( "execution_narration_sound_only_requires_hearing_even_when_the_subject_is_visible",
           "[executions][messages]" )
{
    narration_world world;
    monster &target = victim();
    get_avatar().add_effect( efftype_id( "deaf" ), 1_hours );
    Messages::clear_messages();
    const int64_t id = start_narration( "test_exec_narration_audible", target );
    CHECK( g->executions().narration( id ).empty() );
    CHECK( Messages::size() == 0 );
}

TEST_CASE( "execution_narration_display_settings_preserve_mechanics_and_gameplay_rng",
           "[executions][messages]" )
{
    narration_world world;
    get_avatar().set_mutation( trait_id( "DEBUG_CLAIRVOYANCE" ) );
    get_avatar().recalc_sight_limits();
    monster &target = victim();
    dialogue d( get_talker_for( get_avatar() ), get_talker_for( target ) );
    const auto queued = g->executions().start( execution_id( "test_exec_stay" ), d, 90210 );
    REQUIRE( queued.id > 0 );
    const std::string saved = save_narration();
    const time_point initial_time = calendar::turn;
    const tripoint_abs_ms origin = target.pos_abs();
    tripoint_abs_ms expected_position;
    time_point expected_time;
    for( bool hidden : {
             false, true
         } ) {
        const override_option enabled( "EXECUTION_NARRATION", hidden ? "false" : "true" );
        calendar::turn = initial_time;
        target.setpos( origin );
        const JsonObject obj = json_loader::from_string( saved );
        Messages::deserialize( obj );
        g->executions().deserialize( obj.get_object( "executions" ) );
        const auto random_state = rng_get_engine();
        g->executions().drain_starts();
        for( int n = 0; n < 12 && g->executions().inspect( queued.id )->state == "running"; ++n ) {
            advance_narration();
        }
        REQUIRE( g->executions().inspect( queued.id )->state == "completed" );
        CHECK( rng_get_engine() == random_state );
        CHECK( g->executions().inspect( queued.id )->outcome == "wound_sequence_resolved" );
        if( hidden ) {
            CHECK( target.pos_abs() == expected_position );
            CHECK( calendar::turn == expected_time );
            CHECK( g->executions().narration( queued.id ).empty() );
        } else {
            expected_position = target.pos_abs();
            expected_time = calendar::turn;
            CHECK( target.pos_abs() != origin );
            REQUIRE( get_avatar().sees( get_map(), target ) );
            CHECK( g->executions().narration( queued.id ).size() == 3 );
        }
    }
}

TEST_CASE( "owned_messages_cannot_append_to_another_owner_or_reuse_cleared_handles",
           "[messages][executions]" )
{
    message_log log;
    const auto root = log.add_owned( "one", "", "First.", " ", 20 );
    const auto stolen = log.add_owned( "two", root.group, "Second.", " ", 20 );
    CHECK_FALSE( stolen.appended );
    CHECK( stolen.group != root.group );
    const auto append = log.add_owned( "one", root.group, "Third.", " ", 20 );
    CHECK( append.appended );
    CHECK( append.group == root.group );
    log.clear();
    const auto after_clear = log.add_owned( "one", root.group, "After clear.", " ", 20 );
    CHECK_FALSE( after_clear.appended );
    CHECK( after_clear.group != root.group );
}

TEST_CASE( "owned_messages_trimmed_anchors_do_not_restore_pruned_history",
           "[messages][executions]" )
{
    message_log log;
    const auto root = log.add_owned( "one", "", "Pruned.", " ", 2 );
    log.messages.emplace_back( std::string( "Unrelated." ), m_neutral );
    const auto next = log.add_owned( "one", root.group, "New segment.", " ", 2 );
    CHECK_FALSE( next.appended );
    REQUIRE( log.messages.size() == 2 );
    CHECK( log.messages[0].message == "Unrelated." );
    CHECK( log.messages[1].message == "New segment." );
    CHECK( log.compact_messages( 1 ).size() == 1 );
    CHECK( log.compact_messages( 0 ).empty() );
}

TEST_CASE( "compact_messages_can_leave_cooldown_hidden_messages_out_of_the_count",
           "[messages][executions]" )
{
    message_log log;
    log.messages.emplace_back( std::string( "Visible." ), m_neutral );
    for( int i = 0; i < 3; ++i ) {
        log.messages.emplace_back( std::string( "Hidden." ), m_neutral );
        log.messages.back().cooldown_hidden = true;
    }
    // Counted, the newest hidden message takes the only group.
    const std::vector<game_message> counted = log.compact_messages( 1 );
    REQUIRE( counted.size() == 1 );
    CHECK( counted[0].is_in_cooldown() );
    // Not counted, the sidebar still gets the visible message for its one line.
    const std::vector<game_message> uncounted = log.compact_messages( 1, false );
    REQUIRE( uncounted.size() == 4 );
    CHECK( uncounted[0].message == "Visible." );
    CHECK_FALSE( uncounted[0].is_in_cooldown() );
}

TEST_CASE( "owned_messages_keep_legacy_logs_and_reserve_saved_identities",
           "[messages][executions][save]" )
{
    message_log log;
    log.deserialize( json_loader::from_string( R"({"messages":[
        {"turn":0,"message":"Old message.","count":2,"type":0}],"curmes":0})" ) );
    REQUIRE( log.messages.size() == 1 );
    CHECK( log.messages[0].group.empty() );
    const auto root = log.add_owned( "one", "", "Owned.", " ", 20 );
    std::ostringstream text;
    JsonOut out( text );
    log.serialize( out );
    const auto before = log.add_owned( "one", root.group, "Future.", " ", 20 );
    log.deserialize( json_loader::from_string( text.str() ) );
    const auto resumed = log.add_owned( "one", root.group, "Resumed.", " ", 20 );
    CHECK( resumed.appended );
    CHECK( resumed.segment != before.segment );
    CHECK( log.messages[0].get_with_count() == "Old message. x 2" );
    CHECK( log.compact_messages( 2 )[1].message == "Owned. Resumed." );
    log.deserialize( json_loader::from_string( "{}" ) );
    CHECK( log.messages.empty() );
}

TEST_CASE( "owned_messages_do_not_coalesce_with_ordinary_identical_combat_messages",
           "[messages][executions]" )
{
    Messages::clear_messages();
    add_msg( "Identical words." );
    Messages::add_owned( "scene", "", "Identical words." );
    add_msg( "Identical words." );
    CHECK( Messages::recent_messages( 20 ).size() == 3 );
    CHECK( Messages::recent_messages_compact( 20 ).size() == 3 );
}

TEST_CASE( "execution_narration_schema_rejects_ambiguous_or_unsafe_audiences",
           "[executions][json]" )
{
    const std::string action = GENERATE(
                                   R"({"op":"message","text":"test","audience":"visible_or_audible","volume":10})",
                                   R"({"op":"message","text":"test","audience":"audible","volume":0})",
                                   R"({"op":"message","text":"test","audience":"visible","volume":10})",
                                   R"({"op":"message","text":"test","separator":"unknown"})",
                                   R"({"op":"message","text":"test","append_to":"missing"})" );
    execution_definition def;
    const std::string json = "{\"name\":\"invalid\",\"start\":\"a\",\"steps\":{\"a\":{\"action\":" +
                             action + ",\"end\":\"done\"}}}";
    CAPTURE( action );
    CHECK_THROWS( def.load( json_loader::from_string( json ), "test_data" ) );
}
