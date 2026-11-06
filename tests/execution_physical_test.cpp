#include <functional>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "creature_ref.h"
#include "creature_tracker.h"
#include "damage.h"
#include "dialogue.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "execution.h"
#include "execution_combat.h"
#include "execution_runtime.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "global_vars.h"
#include "incapacitation.h"
#include "item.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "messages.h"
#include "monster.h"
#include "npc.h"
#include "npc_turn_helpers.h"
#include "options_helpers.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "talker.h"
#include "type_id.h"
#include "uistate.h"

namespace
{
struct physical_world {
    time_point old_time = calendar::turn;
    quit_status old_quit = g->uquit;
    bool old_distraction = uistate.distraction_attack;
    override_option animations{ "ANIMATIONS", "false" };
    override_option deathcam{ "DEATHCAM", "never" };
    physical_world() {
        g->executions().clear();
        clear_avatar();
        clear_map_without_vision( -2, 1 );
        Messages::clear_messages();
        get_globals().clear_global_values();
        get_avatar().setpos( get_map(), tripoint_bub_ms( 60, 60, 0 ) );
        get_avatar().set_all_parts_hp_cur( 10000 );
        calendar::turn = calendar::turn_zero + 12_hours;
        uistate.distraction_attack = true;
        g->uquit = QUIT_NO;
    }
    ~physical_world() {
        g->executions().clear();
        clear_avatar();
        get_map().vertical_shift( 0 );
        get_avatar().setpos( get_map(), tripoint_bub_ms( 60, 60, 0 ), false );
        Messages::clear_messages();
        uistate.distraction_attack = old_distraction;
        g->uquit = old_quit;
        calendar::turn = old_time;
    }
};

Creature &victim( bool character = false )
{
    const tripoint_bub_ms pos = get_avatar().pos_bub() + tripoint_rel_ms::east;
    if( character ) {
        npc &who = spawn_npc( pos.xy(), "thug" );
        who.clear_worn();
        who.set_all_parts_hp_cur( 10000 );
        return who;
    }
    return spawn_test_monster( "mon_execution_trigger_test", pos );
}

int64_t start( const std::string &definition, Creature &actor, Creature &target, uint64_t seed = 7 )
{
    dialogue context( get_talker_for( actor ), get_talker_for( target ) );
    const auto result = g->executions().start( execution_id( definition ), context, seed );
    INFO( result.reason );
    REQUIRE( result.id != 0 );
    return result.id;
}

execution_view view( int64_t id )
{
    const auto result = g->executions().inspect( id );
    REQUIRE( result );
    return *result;
}

void tick( int count = 1 )
{
    for( int n = 0; n < count; ++n ) {
        calendar::turn += 1_turns;
        g->executions().process();
    }
}

std::string save()
{
    std::ostringstream text;
    JsonOut out( text );
    g->executions().serialize( out );
    return text.str();
}

void restore( const std::string &data )
{
    g->executions().reset_for_load();
    g->executions().deserialize( json_loader::from_string( data ) );
    g->executions().end_restore();
}

struct damage_observer : event_subscriber {
    using event_subscriber::notify;
    std::vector<std::pair<const Creature *, const Creature *>> hits;
    std::function<void()> callback;
    void notify( const cata::event &event ) override {
        if( event.type() == event_type::character_takes_damage ) {
            hits.emplace_back( nullptr, nullptr );
            if( callback ) {
                callback();
            }
        }
    }
    void notify( const cata::event &event, std::unique_ptr<talker> receiver,
                 std::unique_ptr<talker> source ) override {
        if( event.type() == event_type::character_takes_damage ||
            event.type() == event_type::monster_takes_damage ) {
            hits.emplace_back( receiver ? receiver->get_const_creature() : nullptr,
                               source ? source->get_const_creature() : nullptr );
            if( callback ) {
                callback();
            }
        }
    }
};
} // namespace

TEST_CASE( "execution_shove_moves_checked_tiles_with_a_saved_source_axis",
           "[executions][execution_physical][save]" )
{
    physical_world world;
    Creature &target = victim();
    const tripoint_bub_ms origin = target.pos_bub();
    const int64_t id = start( "test_exec_physical_shove", get_avatar(), target );
    g->executions().drain_starts();
    CHECK( target.pos_bub() == origin + tripoint_rel_ms::east );
    CHECK( view( id ).context.at( "execution_result_physical" ) == "moving" );
    const std::string data = save();
    // The source moves around after the shove has committed, while it is a free role.
    get_avatar().setpos( get_map(), origin + tripoint_rel_ms( 5, 2, 0 ) );
    restore( data );
    tick( 2 );
    CHECK( target.pos_bub() == origin + tripoint_rel_ms( 3, 0, 0 ) );
    CHECK( view( id ).outcome == "finished" );
    CHECK( view( id ).context.at( "execution_moved_physical" ).dbl() == 3 );
    CHECK_FALSE( g->executions().controls( target ) );
}

TEST_CASE( "execution_shove_respects_walls_occupancy_corners_and_support",
           "[executions][execution_physical][map]" )
{
    physical_world world;
    const std::string blocker = GENERATE( "wall", "creature", "air", "water" );
    Creature &target = victim();
    const tripoint_bub_ms before = target.pos_bub();
    const tripoint_bub_ms next = before + tripoint_rel_ms::east;
    if( blocker == "wall" ) {
        get_map().ter_set( next, ter_id( "t_concrete_wall" ) );
    } else if( blocker == "creature" ) {
        spawn_test_monster( "mon_execution_trigger_test", next );
    } else {
        get_map().ter_set( next, ter_id( blocker == "air" ? "t_open_air" : "t_water_dp" ) );
    }
    const int hp = target.get_hp();
    const int64_t id = start( "test_exec_physical_shove", get_avatar(), target );
    g->executions().drain_starts();
    CHECK( target.pos_bub() == before );
    CHECK( target.get_hp() == hp );
    CHECK( view( id ).context.at( "execution_result_physical" ) ==
           ( blocker == "wall" || blocker == "creature" ? "blocked" : "unsupported" ) );
}

TEST_CASE( "execution_shove_refuses_a_nonadjacent_or_incapacitated_source",
           "[executions][execution_physical]" )
{
    physical_world world;
    const std::string mode = GENERATE( "distant", "unconscious", "hallucination", "sleep", "narcosis" );
    Creature &target = victim();
    if( mode == "unconscious" ) {
        REQUIRE( incapacitation::apply( get_avatar(), nullptr, 10_turns, true ) == "applied" );
    } else if( mode == "hallucination" ) {
        target.as_monster()->hallucination = true;
    } else if( mode == "sleep" || mode == "narcosis" ) {
        get_avatar().add_effect( efftype_id( mode ), 10_turns );
    } else {
        target.setpos( get_map(), target.pos_bub() + tripoint_rel_ms::east );
    }
    const tripoint_bub_ms before = target.pos_bub();
    const int64_t id = start( "test_exec_physical_shove", get_avatar(), target );
    g->executions().drain_starts();
    CHECK( target.pos_bub() == before );
    CHECK( view( id ).context.at( "execution_result_physical" ) ==
           ( mode == "unconscious" || mode == "sleep" || mode == "narcosis" ? "incapacitated" :
             mode == "hallucination" ? "unsupported" : "out_of_range" ) );
}

TEST_CASE( "execution_fling_uses_native_motion_and_source_for_character_and_monster_impacts",
           "[executions][execution_physical]" )
{
    physical_world world;
    const bool character = GENERATE( false, true );
    Creature &target = victim( character );
    get_map().ter_set( target.pos_bub() + tripoint_rel_ms( 2, 0, 0 ), ter_id( "t_concrete_wall" ) );
    const tripoint_bub_ms before = target.pos_bub();
    const int hp = target.get_hp();
    damage_observer events;
    get_event_bus().subscribe( &events );
    const int64_t id = start( "test_exec_physical_fling", get_avatar(), target );
    g->executions().drain_starts();
    CHECK( target.pos_bub().x() > before.x() );
    CHECK( target.get_hp() < hp );
    CHECK( view( id ).context.at( "execution_result_physical" ) == "moved" );
    CHECK( view( id ).context.at( "execution_velocity" ).dbl() == 40 );
    REQUIRE_FALSE( events.hits.empty() );
    for( const auto &hit : events.hits ) {
        CHECK( hit.first == &target );
        CHECK( hit.second == &get_avatar() );
    }
    CHECK_FALSE( g->executions().controls( target ) );
    CHECK( g->executions().physical_instigator() == nullptr );
}

TEST_CASE( "execution_fling_player_follows_native_position_without_distraction_prompts",
           "[executions][execution_physical]" )
{
    physical_world world;
    npc &source = spawn_npc( get_avatar().pos_bub().xy() + point_rel_ms( -1, 0 ), "thug" );
    get_map().ter_set( get_avatar().pos_bub() + tripoint_rel_ms( 2, 0, 0 ),
                       ter_id( "t_concrete_wall" ) );
    const tripoint_abs_ms before = get_avatar().pos_abs();
    damage_observer events;
    get_event_bus().subscribe( &events );
    const int64_t id = start( "test_exec_physical_fling", source, get_avatar() );
    g->executions().drain_starts();
    CHECK( get_avatar().pos_abs().x() > before.x() );
    CHECK( view( id ).outcome == "finished" );
    CHECK( get_avatar().activity.is_null() );
    REQUIRE_FALSE( events.hits.empty() );
    for( const auto &hit : events.hits ) {
        CHECK( hit.second == &source );
    }
}

TEST_CASE( "execution_fling_landing_and_collateral_use_the_actual_instigator",
           "[executions][execution_physical]" )
{
    physical_world world;
    const bool character_blocker = GENERATE( false, true );
    Creature &target = victim();
    Creature *blocker = nullptr;
    const tripoint_bub_ms position = target.pos_bub() + tripoint_rel_ms::east;
    if( character_blocker ) {
        npc &who = spawn_npc( position.xy(), "thug" );
        who.clear_worn();
        who.set_all_parts_hp_cur( 10000 );
        blocker = &who;
    } else {
        blocker = &spawn_test_monster( "mon_execution_trigger_test", position );
    }
    const int before = blocker->get_hp();
    damage_observer events;
    get_event_bus().subscribe( &events );
    start( "test_exec_physical_fling", get_avatar(), target );
    g->executions().drain_starts();
    CHECK( blocker->get_hp() < before );
    bool collateral = false;
    for( const auto &hit : events.hits ) {
        CHECK( hit.second == &get_avatar() );
        collateral = collateral || hit.first == blocker;
    }
    CHECK( collateral );
    CHECK( get_creature_tracker().creature_at( position ) == blocker );
}

TEST_CASE( "execution_fling_keeps_explicit_environmental_child_damage_unattributed",
           "[executions][execution_physical]" )
{
    physical_world world;
    Creature &target = victim( true );
    get_map().ter_set( target.pos_bub() + tripoint_rel_ms::east, ter_id( "t_concrete_wall" ) );
    damage_observer events;
    bool nested = false;
    const bool direct = GENERATE( false, true );
    events.callback = [&]() {
        if( !nested ) {
            nested = true;
            if( direct ) {
                target.apply_damage( nullptr, bodypart_id( "arm_l" ), 1 );
            } else {
                target.deal_damage( nullptr, bodypart_id( "arm_l" ),
                                    damage_instance( damage_type_id( "pure" ), 1 ) );
            }
        }
    };
    get_event_bus().subscribe( &events );
    start( "test_exec_physical_fling", get_avatar(), target );
    g->executions().drain_starts();
    REQUIRE( events.hits.size() >= 2 );
    CHECK( events.hits[0].second == &get_avatar() );
    CHECK( events.hits[1].second == nullptr );
}

TEST_CASE( "execution_fling_stops_safely_when_a_damage_callback_cancels_or_removes_the_body",
           "[executions][execution_physical]" )
{
    physical_world world;
    const bool remove = GENERATE( false, true );
    monster &target = static_cast<monster &>( victim() );
    get_map().ter_set( target.pos_bub() + tripoint_rel_ms::east, ter_id( "t_concrete_wall" ) );
    const tripoint_bub_ms before = target.pos_bub();
    const int64_t id = start( "test_exec_physical_fling", get_avatar(), target );
    damage_observer events;
    bool changed = false;
    events.callback = [&]() {
        if( !changed ) {
            changed = true;
            if( remove ) {
                get_creature_tracker().remove( target );
            } else {
                g->executions().cancel( id, "test_cancelled" );
            }
        }
    };
    get_event_bus().subscribe( &events );
    g->executions().drain_starts();
    CHECK( changed );
    CHECK( view( id ).state == "interrupted" );
    CHECK( view( id ).reason == ( remove ? "participant_unavailable" : "test_cancelled" ) );
    if( !remove ) {
        CHECK( target.pos_bub() == before );
    }
    CHECK( g->executions().physical_instigator() == nullptr );
}

TEST_CASE( "execution_fling_native_fall_retains_attribution_without_fake_damage",
           "[executions][execution_physical][map]" )
{
    physical_world world;
    Creature &target = victim();
    for( int x = 1; x <= 5; ++x ) {
        const tripoint_bub_ms pos = target.pos_bub() + tripoint_rel_ms( x, 0, 0 );
        get_map().ter_set( pos, ter_id( "t_open_air" ) );
        get_map().ter_set( pos + tripoint_rel_ms( 0, 0, -1 ), ter_id( "t_floor" ) );
    }
    damage_observer events;
    get_event_bus().subscribe( &events );
    const int64_t id = start( "test_exec_physical_fling", get_avatar(), target );
    g->executions().drain_starts();
    CHECK( target.pos_abs().z() == -1 );
    CHECK( view( id ).context.at( "execution_fell" ).dbl() == 1 );
    REQUIRE_FALSE( events.hits.empty() );
    for( const auto &hit : events.hits ) {
        CHECK( hit.second == &get_avatar() );
    }
}

TEST_CASE( "execution_native_counterattacks_keep_combat_costs_and_do_not_retrigger",
           "[executions][execution_physical]" )
{
    physical_world world;
    const bool monster = GENERATE( false, true );
    Creature &counter = victim( !monster );
    if( Character *person = counter.as_character() ) {
        person->set_dex_base( 30 );
        person->set_skill_level( skill_id( "melee" ), 10 );
        person->reset_stats();
    }
    get_avatar().set_dodges_left( 0 );
    const int hp = get_avatar().get_hp();
    const int64_t id = start( "test_exec_physical_counter", get_avatar(), counter );
    g->executions().drain_starts();
    CHECK( view( id ).context.at( "execution_result_physical" ) == "attacked" );
    CHECK( view( id ).context.at( "execution_attack_moves" ).dbl() > 0 );
    CHECK( get_avatar().get_hp() < hp );
    CHECK_FALSE( g->executions().last_trigger_report() );
    CHECK( get_avatar().activity.is_null() );
    CHECK_FALSE( g->executions().controls( counter ) );
}

TEST_CASE( "execution_native_counter_refuses_an_unconscious_fighter",
           "[executions][execution_physical]" )
{
    physical_world world;
    Creature &counter = victim( true );
    const std::string mode = GENERATE( "knocked_out", "sleep", "narcosis" );
    const int64_t id = start( "test_exec_physical_counter", get_avatar(), counter );
    if( mode == "knocked_out" ) {
        REQUIRE( incapacitation::apply( counter, &get_avatar(), 10_turns, true ) == "applied" );
    } else {
        counter.add_effect( efftype_id( mode ), 10_turns );
    }
    const int hp = get_avatar().get_hp();
    g->executions().drain_starts();
    CHECK( view( id ).context.at( "execution_result_physical" ) == "incapacitated" );
    CHECK( get_avatar().get_hp() == hp );
}

TEST_CASE( "execution_physical_exception_unwinds_instigator_and_action_ownership",
           "[executions][execution_physical]" )
{
    physical_world world;
    Creature &target = victim();
    const int64_t id = start( "test_exec_physical_fling", get_avatar(), target );
    damage_observer events;
    bool threw = false;
    events.callback = [&]() {
        if( !threw ) {
            threw = true;
            throw std::runtime_error( "Native impact callback exception" );
        }
    };
    get_event_bus().subscribe( &events );
    CHECK_THROWS_AS( g->executions().drain_starts(), std::runtime_error );
    CHECK_FALSE( execution_combat::attack_in_progress() );
    CHECK( g->executions().physical_instigator() == nullptr );
    CHECK( view( id ).state == "interrupted" );
    events.hits.clear();
    target.apply_damage( nullptr, bodypart_id( "torso" ), 1 );
    REQUIRE( events.hits.size() == 1 );
    CHECK( events.hits.front().second == nullptr );
}

TEST_CASE( "execution_cinematic_clash_has_real_separation_counter_and_throw_branches",
           "[executions][execution_physical][clash]" )
{
    physical_world world;
    const std::string branch = GENERATE( "separate", "counter", "throw" );
    Creature &target = victim( true );
    const tripoint_abs_ms actor_before = get_avatar().pos_abs();
    const tripoint_abs_ms target_before = target.pos_abs();
    const int64_t id = start( "test_exec_clash_" + branch, get_avatar(), target );
    g->executions().drain_starts();
    tick( 4 );
    CHECK( view( id ).state == "completed" );
    if( branch == "separate" ) {
        CHECK( view( id ).outcome == "separated" );
        CHECK( get_avatar().pos_abs().x() < actor_before.x() );
        CHECK( target.pos_abs().x() > target_before.x() );
    } else if( branch == "counter" ) {
        CHECK( view( id ).outcome == "counter_exchange" );
        CHECK( view( id ).context.at( "execution_result_counter" ) == "attacked" );
    } else {
        CHECK( view( id ).outcome == "thrown" );
        CHECK( target.pos_abs().x() > target_before.x() );
    }
    CHECK( get_avatar().activity.is_null() );
    CHECK_FALSE( g->executions().controls( target ) );
}

TEST_CASE( "execution_cinematic_clash_resumes_without_rerolling_and_interrupts_on_real_harm",
           "[executions][execution_physical][clash][save]" )
{
    physical_world world;
    const bool interrupt = GENERATE( false, true );
    Creature &target = victim( true );
    const int64_t id = start( "execution_cinematic_clash", get_avatar(), target, 123 );
    g->executions().drain_starts();
    REQUIRE( view( id ).state == "running" );
    const auto before = view( id );
    const std::string runtime = save();
    std::ostringstream actor;
    JsonOut out( actor );
    get_avatar().serialize( out );
    g->executions().reset_for_load();
    g->executions().deserialize( json_loader::from_string( runtime ) );
    get_avatar().setID( character_id(), true );
    get_avatar().deserialize( json_loader::from_string( actor.str() ) );
    g->executions().end_restore();
    CHECK( view( id ).step == before.step );
    CHECK( view( id ).due == before.due );
    CHECK( view( id ).transcript == before.transcript );
    if( interrupt ) {
        target.apply_damage( nullptr, bodypart_id( "torso" ), 1 );
    }
    tick( 10 );
    CHECK( view( id ).state == ( interrupt ? "interrupted" : "completed" ) );
    if( interrupt ) {
        CHECK( view( id ).reason == "damaged" );
    }
    CHECK( get_avatar().activity.is_null() );
    CHECK_FALSE( g->executions().controls( target ) );
}

TEST_CASE( "execution_physical_schema_requires_control_bounds_and_distinct_sources",
           "[executions][execution_physical][json]" )
{
    const std::string action = GENERATE(
                                   R"({"op":"fling","velocity":161})",
                                   R"({"op":"fling","source":"target"})",
                                   R"({"op":"shove","tiles":5})",
                                   R"({"op":"shove","direction":"forward"})" );
    execution_definition invalid;
    CHECK_THROWS_AS( invalid.load( json_loader::from_string(
                                       R"({"name":"Invalid action","control":{"actor":"scripted","target":"scripted"},"start":"a","steps":{"a":{"action":)"
                                       +
                                       action + R"(,"end":"done"}}})" ), "tests" ), JsonError );
    execution_definition uncontrolled;
    CHECK_THROWS_AS( uncontrolled.load( json_loader::from_string(
                                            R"({"name":"Uncontrolled","control":{"actor":"free","target":"free"},"start":"a","steps":{"a":{"action":{"op":"fling"},"end":"done"}}})" ),
                                        "tests" ), JsonError );
}
