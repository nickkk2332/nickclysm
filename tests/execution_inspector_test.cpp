#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character_id.h"
#include "coordinates.h"
#include "creature.h"
#include "creature_ref.h"
#include "creature_tracker.h"
#include "dialogue.h"
#include "execution_combat.h"
#include "execution_inspector.h"
#include "execution_runtime.h"
#include "execution_trigger.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "global_vars.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser_diag_value.h"
#include "monster.h"
#include "player_helpers.h"
#include "point.h"
#include "rng.h"
#include "type_id.h"

static const execution_trigger_id
execution_trigger_test_exec_expanded_thrown_living( "test_exec_expanded_thrown_living" );

namespace
{
struct inspector_world {
    time_point old_time = calendar::turn;
    inspector_world() {
        g->executions().clear();
        clear_avatar();
        clear_map_without_vision( -2, 1 );
        get_globals().clear_global_values();
        get_avatar().setpos( get_map(), tripoint_bub_ms( 60, 60, 0 ) );
        calendar::turn = calendar::turn_zero + 12_hours;
    }
    ~inspector_world() {
        g->executions().clear();
        clear_avatar();
        calendar::turn = old_time;
    }
};

std::string save()
{
    std::ostringstream text;
    JsonOut out( text );
    g->executions().serialize( out );
    return text.str();
}

void restore( const std::string &text )
{
    g->executions().reset_for_load();
    g->executions().deserialize( json_loader::from_string( text ) );
    g->executions().end_restore();
}

int64_t start( Creature &target, const std::string &definition = "test_exec_clock" )
{
    dialogue d( get_talker_for( get_avatar() ), get_talker_for( target ) );
    const execution_start_result result = g->executions().start( execution_id( definition ), d, 712 );
    INFO( result.reason );
    REQUIRE( result.id != 0 );
    return result.id;
}

execution_combat::attack_snapshot packet( Creature &target )
{
    execution_combat::attack_snapshot out;
    out.id = g->executions().next_attack_id();
    out.producer = execution_combat::channel::thrown;
    out.actor = creature_ref::bind( get_avatar() );
    out.target = creature_ref::bind( target );
    out.source_kind = "avatar";
    out.target_kind = "monster";
    out.weapon = "execution_expanded_throw";
    out.hit = true;
    out.hp_loss = 4;
    out.damage["pure"] = 4;
    out.actor_position = get_avatar().pos_abs();
    out.target_position = target.pos_abs();
    return out;
}

const execution_trigger_candidate &candidate( const execution_trigger_report &report,
        const std::string &id )
{
    const auto found = std::find_if( report.candidates.begin(), report.candidates.end(),
    [&]( const auto & entry ) {
        return entry.rule == id;
    } );
    REQUIRE( found != report.candidates.end() );
    return *found;
}
} // namespace

TEST_CASE( "execution_inspector_is_read_only_for_pending_running_and_finished_scenes",
           "[executions][execution_inspector]" )
{
    inspector_world world;
    monster &target = spawn_test_monster( "mon_execution_expanded_victim",
                                          get_avatar().pos_bub() + tripoint_rel_ms::east );
    const int64_t id = start( target );
    const std::string phase = GENERATE( "pending", "running", "completed" );
    if( phase != "pending" ) {
        g->executions().drain_starts();
    }
    if( phase == "completed" ) {
        for( int i = 0; i < 5; ++i ) {
            calendar::turn += 1_turns;
            g->executions().process();
        }
    }
    const std::string before = save();
    // NOLINTNEXTLINE(cata-determinism)
    const cata_default_random_engine rng_before = rng_get_engine();
    const time_point when = calendar::turn;
    const tripoint_abs_ms position = target.pos_abs();
    const int moves = target.get_moves();
    for( int i = 0; i < 3; ++i ) {
        const auto rows = g->executions().inspect_all();
        REQUIRE( rows.size() == 1 );
        CHECK( rows.front().view.id == id );
        CHECK( rows.front().view.state == phase );
        CHECK( rows.front().details.at( "target_uid" ) == std::to_string( target.persistent_id() ) );
        CHECK_FALSE( execution_inspector::format_scene( rows.front() ).empty() );
        CHECK( g->executions().trigger_history().empty() );
    }
    CHECK( save() == before );
    CHECK( rng_get_engine() == rng_before );
    CHECK( calendar::turn == when );
    CHECK( target.pos_abs() == position );
    CHECK( target.get_moves() == moves );
}

TEST_CASE( "execution_inspector_draws_survive_saves_without_rerolls_or_duplicate_starts",
           "[executions][execution_inspector][save]" )
{
    inspector_world world;
    monster &target = spawn_test_monster( "mon_execution_expanded_victim",
                                          get_avatar().pos_bub() + tripoint_rel_ms::east );
    start( target );
    g->executions().drain_starts();
    const auto before = g->executions().inspect_all().front();
    REQUIRE_FALSE( before.draws.empty() );
    CHECK( before.draws.back().purpose == "transition_delay" );
    CHECK( before.draws.back().sampled );
    const std::string report_text = execution_inspector::format_scene( before );
    const std::string text = save();
    restore( text );
    CHECK( execution_inspector::format_scene( g->executions().inspect_all().front() ) == report_text );
    CHECK( save() == text );
    CHECK( get_globals().get_global_value( "exec_starts" ).dbl() == 1 );
    const std::size_t old_size = before.draws.size();
    calendar::turn += 5_turns;
    g->executions().process();
    CHECK( g->executions().inspect_all().front().draws.size() == old_size );
}

TEST_CASE( "execution_inspector_legacy_saves_need_no_draw_log_and_corrupt_logs_are_rejected",
           "[executions][execution_inspector][save]" )
{
    inspector_world world;
    monster &target = spawn_test_monster( "mon_execution_expanded_victim",
                                          get_avatar().pos_bub() + tripoint_rel_ms::east );
    start( target );
    g->executions().drain_starts();
    std::string text = save();
    const size_t begin = text.find( ",\"draws\":" );
    REQUIRE( begin != std::string::npos );
    const size_t end = text.find( ']', begin );
    REQUIRE( end != std::string::npos );
    text.erase( begin, end - begin + 1 );
    CHECK_NOTHROW( restore( text ) );
    CHECK( g->executions().inspect_all().front().draws.empty() );
    execution_random_draw bad;
    CHECK_THROWS_AS( bad.deserialize( json_loader::from_string(
                                          R"({"step":"entry","entry":1,"purpose":"delay","minimum":2,"maximum":4,"value":9,"sampled":true})" ) ),
                     JsonError );
}

TEST_CASE( "execution_inspector_records_actual_chance_and_weight_draws_without_inspection_rng",
           "[executions][execution_inspector]" )
{
    inspector_world world;
    const std::string mode = GENERATE( "chance", "weight" );
    get_globals().set_global_value( "exec_expanded_case", mode );
    monster &target = spawn_test_monster( "mon_execution_expanded_victim",
                                          get_avatar().pos_bub() + tripoint_rel_ms::east );
    g->executions().on_attack_resolved( packet( target ) );
    const auto history = g->executions().trigger_history();
    REQUIRE( history.size() == 1 );
    const auto &report = history.front();
    CHECK( report.facts.at( "attack_channel" ) == "thrown" );
    CHECK( report.actor.id == get_avatar().getID().get_value() );
    if( mode == "chance" ) {
        for( const std::string suffix : {
                 "0", "1"
             } ) {
            const execution_trigger_candidate &row = candidate( report, "test_exec_inspect_chance_" + suffix );
            REQUIRE( row.roll );
            CHECK( *row.roll >= 0 );
            CHECK( *row.roll < 1 );
            CHECK( row.chance == .65 );
        }
        const execution_trigger_candidate &zero = candidate( report, "test_exec_inspect_zero" );
        CHECK( zero.reason == "chance_failed" );
        CHECK_FALSE( zero.roll );
    } else {
        REQUIRE( report.weighted_roll );
        CHECK( report.total_weight == 4 );
        CHECK( *report.weighted_roll >= 1 );
        CHECK( *report.weighted_roll <= 4 );
        CHECK_FALSE( candidate( report, "test_exec_inspect_weight_0" ).roll );
    }
    // NOLINTNEXTLINE(cata-determinism)
    const cata_default_random_engine rng_before = rng_get_engine();
    const std::string saved = save();
    for( int i = 0; i < 5; ++i ) {
        CHECK_FALSE( execution_inspector::format_trigger(
                         g->executions().trigger_history().front() ).empty() );
        g->executions().inspect_all();
    }
    CHECK( rng_get_engine() == rng_before );
    CHECK( save() == saved );
}

TEST_CASE( "execution_inspector_never_reevaluates_conditions_and_bounds_contact_history",
           "[executions][execution_inspector]" )
{
    inspector_world world;
    get_globals().set_global_value( "exec_expanded_case", "living" );
    execution_trigger &rule = const_cast<execution_trigger &>
                              ( execution_trigger_test_exec_expanded_thrown_living.obj() );
    const auto original = rule.condition;
    int evaluations = 0;
    const on_out_of_scope restore_rule( [&]() {
        rule.condition = original;
    } );
    rule.condition = [&]( const const_dialogue & ) {
        ++evaluations;
        return false;
    };
    monster &target = spawn_test_monster( "mon_execution_expanded_victim",
                                          get_avatar().pos_bub() + tripoint_rel_ms::east );
    for( int i = 0; i < 40; ++i ) {
        g->executions().on_attack_resolved( packet( target ) );
    }
    CHECK( evaluations == 40 );
    const auto history = g->executions().trigger_history();
    REQUIRE( history.size() == 32 );
    CHECK( history.front().attack < history.back().attack );
    for( const execution_trigger_report &report : history ) {
        CHECK( candidate( report, rule.id.str() ).reason == "condition" );
        execution_inspector::format_trigger( report );
    }
    g->executions().inspect_all();
    CHECK( evaluations == 40 );
    CHECK_FALSE( g->executions().last_trigger_report()->instance );
}

TEST_CASE( "execution_inspector_reports_lost_original_roles_and_pending_cancellation",
           "[executions][execution_inspector]" )
{
    inspector_world world;
    monster &target = spawn_test_monster( "mon_execution_expanded_victim",
                                          get_avatar().pos_bub() + tripoint_rel_ms::east );
    const int64_t id = start( target );
    const int64_t uid = target.persistent_id();
    const tripoint_bub_ms position = target.pos_bub();
    g->executions().cancel( id, "inspector_test_cancel" );
    get_creature_tracker().remove( target );
    spawn_test_monster( "mon_execution_expanded_victim", position );
    const auto row = g->executions().inspect_all().front();
    CHECK( row.details.at( "target_uid" ) == std::to_string( uid ) );
    CHECK( row.details.at( "target_loaded" ).dbl() == 0 );
    CHECK( row.details.at( "cancel_pending" ) == "inspector_test_cancel" );
}
