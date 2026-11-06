#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character_martial_arts.h"
#include "condition.h"
#include "coordinates.h"
#include "creature.h"
#include "creature_ref.h"
#include "creature_tracker.h"
#include "damage.h"
#include "dialogue.h"
#include "enums.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "execution.h"
#include "execution_runtime.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "global_vars.h"
#include "horde_entity.h"
#include "item.h"
#include "item_location.h"
#include "item_uid.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser_diag_value.h"
#include "memory_fast.h"
#include "monster.h"
#include "npc.h"
#include "pimpl.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "point.h"
#include "rng.h"
#include "talker.h"
#include "type_id.h"
#include "units.h"
#include "vehicle.h"

static const activity_id ACT_EXECUTION( "ACT_EXECUTION" );

static const damage_type_id damage_pure( "pure" );

static const efftype_id effect_stunned( "stunned" );

static const execution_id execution_definition_test_exec_clock( "test_exec_clock" );
static const execution_id execution_definition_test_exec_local( "test_exec_local" );
static const execution_id execution_definition_test_exec_weapon( "test_exec_weapon" );

static const itype_id itype_knife_combat( "knife_combat" );

static const matec_id test_exec_post_hit( "test_exec_post_hit" );

static const mtype_id mon_zombie_fat( "mon_zombie_fat" );

static const ter_str_id ter_t_floor( "t_floor" );
static const ter_str_id ter_t_open_air( "t_open_air" );
static const ter_str_id ter_t_wall( "t_wall" );
static const ter_str_id ter_t_water_dp( "t_water_dp" );

static const vpart_id vpart_aisle( "aisle" );
static const vpart_id vpart_board( "board" );
static const vpart_id vpart_frame( "frame" );

static const vproto_id vehicle_prototype_none( "none" );

namespace
{
struct execution_world {
    time_point old_time = calendar::turn;
    execution_world() {
        g->executions().clear();
        clear_avatar();
        clear_map_without_vision( -2, 1 );
        get_globals().clear_global_values();
        get_avatar().setpos( get_map(), tripoint_bub_ms( 60, 60, 0 ) );
    }
    ~execution_world() {
        g->executions().clear();
        calendar::turn = old_time;
    }
};

void tick( int count = 1 )
{
    for( int n = 0; n < count; ++n ) {
        calendar::turn += 1_turns;
        g->executions().process();
    }
}

std::string save_executions()
{
    std::ostringstream text;
    JsonOut jsout( text );
    g->executions().serialize( jsout );
    return text.str();
}

int64_t start( const std::string &id, Creature &actor, Creature &target )
{
    dialogue d( get_talker_for( actor ), get_talker_for( target ) );
    const execution_start_result result = g->executions().start( execution_id( id ), d,
                                          UINT64_C( 12345 ) );
    REQUIRE( result.reason == "queued" );
    REQUIRE( result.id > 0 );
    return result.id;
}

execution_view view( int64_t id )
{
    const auto result = g->executions().inspect( id );
    REQUIRE( result.has_value() );
    return *result;
}

void finish_scene( int64_t id )
{
    for( int n = 0; n < 20 && view( id ).state == "running"; ++n ) {
        tick();
    }
    CHECK( view( id ).state == "completed" );
}

double counter( const std::string &name )
{
    const diag_value *value = get_globals().maybe_get_global_value( name );
    return value ? value->dbl() : 0;
}

talk_effect_t execution_effect( const std::string &text )
{
    const JsonObject jo = json_loader::from_string( text );
    talk_effect_t result( jo, "effect", "execution_tests" );
    finalize_conditions();
    return result;
}

std::string save_monster( const monster &who )
{
    std::ostringstream text;
    JsonOut jsout( text );
    who.serialize( jsout );
    return text.str();
}
} // namespace

TEST_CASE( "executions_queue_start_and_advance_once_per_game_turn", "[executions][eoc]" )
{
    execution_world world;
    avatar &actor = get_avatar();
    npc &target = spawn_npc( actor.pos_bub().xy() + point_rel_ms( 2, 0 ), "thug" );
    const int64_t id = start( "test_exec_clock", actor, target );
    CHECK( view( id ).state == "pending" );
    CHECK( counter( "exec_starts" ) == 0 );
    g->executions().drain_starts();
    REQUIRE( view( id ).state == "running" );
    CHECK( counter( "exec_starts" ) == 1 );
    CHECK( counter( "exec_entries" ) == 1 );
    CHECK( counter( "exec_ticks" ) == 0 );
    const time_point due = view( id ).due;
    CHECK( due >= calendar::turn + 2_turns );
    CHECK( due <= calendar::turn + 4_turns );
    const int delay = to_turns<int>( due - calendar::turn );
    for( int n = 0; n < 10; ++n ) {
        g->executions().process();
        g->executions().drain_starts();
    }
    CHECK( counter( "exec_entries" ) == 1 );
    CHECK( counter( "exec_ticks" ) == 0 );
    tick( delay );
    CHECK( view( id ).state == "completed" );
    CHECK( counter( "exec_ticks" ) == delay );
    CHECK( counter( "exec_completes" ) == 1 );
    CHECK( counter( "exec_ends" ) == 1 );
    CHECK_FALSE( g->executions().controls( target ) );
    tick( 2 );
    CHECK( counter( "exec_ends" ) == 1 );
}

TEST_CASE( "executions_EOC_reports_acceptance_and_can_cancel_its_saved_handle",
           "[executions][eoc]" )
{
    execution_world world;
    npc &target = spawn_npc( get_avatar().pos_bub().xy() + point_rel_ms( 2, 0 ), "thug" );
    dialogue d( get_talker_for( get_avatar() ), get_talker_for( target ) );
    execution_effect( R"({ "effect": { "start_execution": "test_exec_clock",
            "true_eocs": [ "EOC_TEST_EXEC_ACCEPTED" ], "false_eocs": [ "EOC_TEST_EXEC_REFUSED" ] } })" ).apply(
                          d );
    CHECK( d.get_value( "reason" ) == "queued" );
    CHECK( counter( "exec_accepted" ) == 1 );
    CHECK( counter( "exec_starts" ) == 0 );
    const int64_t id = std::stoll( d.get_value( "execution_instance" ).str() );
    CHECK( conditional_t( "u_in_execution" )( d ) );
    CHECK( conditional_t( "npc_in_execution" )( d ) );
    execution_effect(
        R"({ "effect": { "cancel_execution": { "context_val": "execution_instance" } } })" ).apply( d );
    g->executions().drain_starts();
    CHECK( view( id ).state == "interrupted" );
    CHECK( view( id ).reason == "cancelled" );
    CHECK( counter( "exec_starts" ) == 0 );
    CHECK( counter( "exec_interrupts" ) == 1 );
    CHECK( counter( "exec_ends" ) == 1 );
    CHECK_FALSE( conditional_t( "npc_in_execution" )( d ) );
}

TEST_CASE( "executions_refuse_invalid_or_competing_participants_atomically", "[executions]" )
{
    execution_world world;
    npc &target = spawn_npc( get_avatar().pos_bub().xy() + point_rel_ms( 2, 0 ), "thug" );
    npc &other = spawn_npc( get_avatar().pos_bub().xy() + point_rel_ms( 3, 1 ), "thug" );
    dialogue missing( get_talker_for( get_avatar() ), nullptr );
    CHECK( g->executions().start( execution_definition_test_exec_clock,
                                  missing ).reason == "missing_participant" );
    dialogue same( get_talker_for( get_avatar() ), get_talker_for( get_avatar() ) );
    CHECK( g->executions().start( execution_definition_test_exec_clock,
                                  same ).reason == "invalid_participant" );
    const int64_t first = start( "test_exec_clock", get_avatar(), target );
    dialogue competing( get_talker_for( other ), get_talker_for( target ) );
    CHECK( g->executions().start( execution_definition_test_exec_clock,
                                  competing ).reason == "participant_busy" );
    CHECK_FALSE( g->executions().controls( other ) );
    CHECK( view( first ).state == "pending" );
}

TEST_CASE( "executions_have_private_typed_locals_and_preserve_trigger_facts", "[executions][eoc]" )
{
    execution_world world;
    npc &target = spawn_npc( get_avatar().pos_bub().xy() + point_rel_ms( 2, 0 ), "thug" );
    dialogue one( get_talker_for( get_avatar() ), get_talker_for( target ) );
    one.set_value( "counter", 10.0 );
    one.set_value( "bp", "arm_l" );
    const execution_start_result first = g->executions().start( execution_definition_test_exec_local,
                                         one, 7 );
    REQUIRE( first.id > 0 );
    one.set_value( "counter", 20.0 );
    one.set_value( "bp", "arm_r" );
    const execution_start_result second = g->executions().start( execution_definition_test_exec_local,
                                          one, 9 );
    REQUIRE( second.id > 0 );
    g->executions().drain_starts();
    tick();
    CHECK( view( first.id ).context.at( "counter" ) == 12 );
    CHECK( view( second.id ).context.at( "counter" ) == 22 );
    CHECK( view( first.id ).context.at( "bp" ) == "arm_l" );
    CHECK( view( second.id ).context.at( "bp" ) == "arm_r" );
    one.set_value( "counter", "wrong type" );
    CHECK( g->executions().start( execution_definition_test_exec_local,
                                  one ).reason == "invalid_parameter" );
}

TEST_CASE( "executions_reject_callback_changes_to_a_local_type", "[executions][eoc]" )
{
    execution_world world;
    monster &target = spawn_test_monster( "mon_zombie", get_avatar().pos_bub() + tripoint_rel_ms( 2, 0,
                                          0 ) );
    const int64_t id = start( "test_exec_bad_local", get_avatar(), target );
    g->executions().drain_starts();
    CHECK( view( id ).state == "interrupted" );
    CHECK( view( id ).reason == "invalid_parameter" );
    CHECK( view( id ).context.at( "counter" ) == 0 );
    CHECK( counter( "exec_ends" ) == 1 );
    CHECK_FALSE( g->executions().controls( target ) );
}

TEST_CASE( "executions_waiting_deadlines_and_chance_choices_survive_save_resume",
           "[executions][save]" )
{
    execution_world world;
    npc &target = spawn_npc( get_avatar().pos_bub().xy() + point_rel_ms( 2, 0 ), "thug" );
    const int64_t id = start( "test_exec_weighted", get_avatar(), target );
    g->executions().drain_starts();
    const execution_view pending = view( id );
    const time_point saved_time = calendar::turn;
    const std::string saved = save_executions();
    const global_variables::impl_t globals = get_globals().get_global_values();
    // Inspection and unrelated RNG consumption must not change a selected branch/deadline.
    for( int n = 0; n < 100; ++n ) {
        CHECK( view( id ).due == pending.due );
        rng( 0, 100 );
    }
    finish_scene( id );
    const std::string outcome = view( id ).outcome;
    g->executions().clear();
    calendar::turn = saved_time;
    get_globals().set_global_values( globals );
    g->executions().deserialize( json_loader::from_string( saved ) );
    CHECK( view( id ).due == pending.due );
    CHECK( view( id ).step == pending.step );
    CHECK( counter( "exec_starts" ) == 1 );
    finish_scene( id );
    CHECK( view( id ).outcome == outcome );
    CHECK( counter( "exec_starts" ) == 1 );
    CHECK( counter( "exec_completes" ) == 1 );
}

TEST_CASE( "executions_do_not_repeat_a_movement_step_after_save", "[executions][save][map]" )
{
    execution_world world;
    monster &target = spawn_test_monster( "mon_zombie", get_avatar().pos_bub() + tripoint_rel_ms( 2, 0,
                                          0 ) );
    const tripoint_abs_ms beginning = target.pos_abs();
    const int64_t id = start( "test_exec_step", get_avatar(), target );
    g->executions().drain_starts();
    tick();
    REQUIRE( target.pos_abs() == beginning + tripoint_rel_ms::east );
    const std::string saved_scene = save_executions();
    const std::string saved_target = save_monster( target );
    const int64_t target_uid = target.persistent_id();
    const time_point saved_time = calendar::turn;
    finish_scene( id );
    REQUIRE( target.pos_abs() == beginning + tripoint_rel_ms( 4, 0, 0 ) );
    g->executions().clear();
    get_creature_tracker().clear();
    // The same type now occupies the old vector slot and the old origin tile.
    monster &replacement = spawn_test_monster( "mon_zombie", get_map().get_bub( beginning ) );
    const auto restored = make_shared_fast<monster>();
    restored->deserialize( json_loader::from_string( saved_target ) );
    REQUIRE( get_creature_tracker().add( restored ) );
    REQUIRE( restored->persistent_id() == target_uid );
    calendar::turn = saved_time;
    g->executions().deserialize( json_loader::from_string( saved_scene ) );
    finish_scene( id );
    CHECK( restored->pos_abs() == beginning + tripoint_rel_ms( 4, 0, 0 ) );
    CHECK( replacement.pos_abs() == beginning );
}

TEST_CASE( "executions_message_repeats_and_avatar_activity_resume_without_replaying_start",
           "[executions][save]" )
{
    execution_world world;
    npc &actor = spawn_npc( get_avatar().pos_bub().xy() - point_rel_ms( 2, 0 ), "thug" );
    const int64_t id = start( "test_exec_pool", actor, get_avatar() );
    g->executions().drain_starts();
    CHECK( view( id ).transcript.empty() );
    CHECK( get_avatar().activity.id() == ACT_EXECUTION );
    tick();
    REQUIRE( view( id ).transcript.size() == 1 );
    const std::string saved = save_executions();
    const time_point saved_time = calendar::turn;
    const global_variables::impl_t globals = get_globals().get_global_values();
    std::ostringstream activity_text;
    JsonOut activity_out( activity_text );
    get_avatar().activity.serialize( activity_out );
    tick();
    const auto expected = view( id ).transcript;
    REQUIRE( expected.size() == 2 );
    CHECK( expected[0] != expected[1] );
    CHECK( get_avatar().activity.is_null() );
    calendar::turn = saved_time;
    get_avatar().activity.deserialize( json_loader::from_string( activity_text.str() ) );
    get_globals().set_global_values( globals );
    g->executions().deserialize( json_loader::from_string( saved ) );
    g->executions().process();
    CHECK( view( id ).transcript.size() == 1 );
    CHECK( get_avatar().activity.id() == ACT_EXECUTION );
    tick();
    CHECK( view( id ).transcript == expected );
    CHECK( get_avatar().activity.is_null() );
    CHECK( counter( "exec_starts" ) == 1 );
}

TEST_CASE( "executions_hold_NPC_and_monster_actions_without_banking_moves", "[executions]" )
{
    execution_world world;
    const bool use_npc = GENERATE( false, true );
    Creature &target = use_npc ? static_cast<Creature &>( spawn_npc(
                           get_avatar().pos_bub().xy() + point_rel_ms( 2, 0 ), "thug" ) ) :
                       spawn_test_monster( "mon_zombie", get_avatar().pos_bub() + tripoint_rel_ms( 2, 0, 0 ) );
    const int64_t id = start( "test_exec_clock", get_avatar(), target );
    g->executions().drain_starts();
    const tripoint_abs_ms position = target.pos_abs();
    target.set_moves( 900 );
    if( use_npc ) {
        target.as_npc()->move();
    } else {
        target.as_monster()->move();
    }
    CHECK( target.get_moves() == 0 );
    CHECK( target.pos_abs() == position );
    CHECK_FALSE( g->executions().controls( get_avatar() ) );
    finish_scene( id );
    CHECK_FALSE( g->executions().controls( target ) );
    CHECK( target.get_moves() == 0 );
}

TEST_CASE( "executions_avatar_cancellation_and_external_damage_release_control", "[executions]" )
{
    execution_world world;
    npc &actor = spawn_npc( get_avatar().pos_bub().xy() - point_rel_ms( 2, 0 ), "thug" );
    const int64_t id = start( "test_exec_clock", actor, get_avatar() );
    g->executions().drain_starts();
    SECTION( "player cancels" ) {
        get_avatar().cancel_activity();
        g->executions().process();
        CHECK( view( id ).reason == "player_cancelled" );
    }
    SECTION( "damage interrupts even if healed before the next pump" ) {
        monster &third = spawn_test_monster( "mon_zombie", get_avatar().pos_bub() + tripoint_rel_ms( 0, 3,
                                             0 ) );
        const bool whole_body = GENERATE( false, true );
        if( whole_body ) {
            get_avatar().hurtall( 1, &third, false );
        } else {
            get_avatar().deal_damage( &third, bodypart_id( "torso" ), damage_instance( damage_pure,
                                      1 ) );
        }
        get_avatar().healall( 1 );
        g->executions().process();
        CHECK( view( id ).reason == "damaged" );
    }
    CHECK( view( id ).state == "interrupted" );
    CHECK( get_avatar().activity.is_null() );
    CHECK_FALSE( g->executions().controls( get_avatar() ) );
    CHECK( counter( "exec_ends" ) == 1 );
}

TEST_CASE( "executions_checked_motion_stops_at_real_blockers", "[executions][map]" )
{
    execution_world world;
    map &here = get_map();
    const int target_kind = GENERATE( 0, 1, 2 );
    Creature *actor = &get_avatar();
    Creature *target;
    if( target_kind == 0 ) {
        target = &spawn_test_monster( "mon_zombie", get_avatar().pos_bub() + tripoint_rel_ms( 2, 0, 0 ) );
    } else if( target_kind == 1 ) {
        target = &spawn_npc( get_avatar().pos_bub().xy() + point_rel_ms( 2, 0 ), "thug" );
    } else {
        actor = &spawn_npc( get_avatar().pos_bub().xy() - point_rel_ms( 2, 0 ), "thug" );
        target = &get_avatar();
    }
    const tripoint_abs_ms origin = target->pos_abs();
    std::string expected_result = "blocked";
    SECTION( "wall" ) {
        here.ter_set( target->pos_bub() + tripoint_rel_ms::east, ter_t_wall );
    }
    SECTION( "occupied tile" ) {
        spawn_test_monster( "mon_zombie", target->pos_bub() + tripoint_rel_ms::east );
    }
    SECTION( "solid vehicle part" ) {
        vehicle *veh = here.add_vehicle( vehicle_prototype_none, target->pos_bub() + tripoint_rel_ms::east,
                                         0_degrees, 0, veh_spawn_status::UNDAMAGED );
        REQUIRE( veh );
        REQUIRE( veh->install_part( here, point_rel_ms::zero, vpart_frame ) != -1 );
        REQUIRE( veh->install_part( here, point_rel_ms::zero, vpart_board ) != -1 );
        here.add_vehicle_to_cache( veh );
        REQUIRE( here.impassable( target->pos_bub() + tripoint_rel_ms::east ) );
    }
    SECTION( "two solid diagonal corners" ) {
        actor->setpos( here, target->pos_bub() - tripoint_rel_ms( 2, 2, 0 ) );
        here.ter_set( target->pos_bub() + tripoint_rel_ms::east, ter_t_wall );
        here.ter_set( target->pos_bub() + tripoint_rel_ms::south, ter_t_wall );
    }
    SECTION( "deep water requires a swimming action contract" ) {
        here.ter_set( target->pos_bub() + tripoint_rel_ms::east, ter_t_water_dp );
        expected_result = "unsupported";
    }
    const int64_t id = start( "test_exec_step", *actor, *target );
    g->executions().drain_starts();
    tick();
    CHECK( target->pos_abs() == origin );
    CHECK( view( id ).context.at( "execution_result_motion" ) == expected_result );
    CHECK( view( id ).context.at( "execution_moved_motion" ) == 0 );
    CHECK_FALSE( g->executions().controls( *target ) );
}

TEST_CASE( "executions_capture_direction_and_detect_external_displacement", "[executions][map]" )
{
    execution_world world;
    monster &target = spawn_test_monster( "mon_zombie", get_avatar().pos_bub() + tripoint_rel_ms( 2, 0,
                                          0 ) );
    const tripoint_abs_ms origin = target.pos_abs();
    const int64_t id = start( "test_exec_step", get_avatar(), target );
    g->executions().drain_starts();
    SECTION( "free attacker moves to the other side" ) {
        get_avatar().setpos( get_map(), target.pos_bub() + tripoint_rel_ms( 2, 1, 0 ) );
        tick();
        CHECK( target.pos_abs() == origin + tripoint_rel_ms::east );
        CHECK( view( id ).state == "running" );
    }
    SECTION( "external target displacement interrupts before another step" ) {
        target.setpos( get_map(), target.pos_bub() + tripoint_rel_ms::south );
        tick();
        CHECK( target.pos_abs() == origin + tripoint_rel_ms::south );
        CHECK( view( id ).reason == "displaced" );
    }
}

TEST_CASE( "executions_stagger_ledge_branch_uses_native_gravity_and_can_be_survived",
           "[executions][map]" )
{
    execution_world world;
    map &here = get_map();
    monster &target = spawn_test_monster( "mon_zombie", get_avatar().pos_bub() + tripoint_rel_ms( 2, 0,
                                          0 ) );
    target.set_hp( 5000 );
    const tripoint_abs_ms origin = target.pos_abs();
    const tripoint_bub_ms edge = target.pos_bub() + tripoint_rel_ms::east;
    here.ter_set( edge, ter_t_open_air );
    here.ter_set( edge + tripoint_rel_ms::below, ter_t_floor );
    const bool fall = GENERATE( false, true );
    const int64_t id = start( fall ? "test_exec_fall" : "test_exec_stay", get_avatar(), target );
    g->executions().drain_starts();
    CHECK( g->executions().can_fall( id, "target" ) );
    finish_scene( id );
    CHECK( view( id ).context.at( "execution_result_stagger" ) == "unsupported" );
    CHECK( target.pos_abs() == origin + ( fall ? tripoint_rel_ms( 1, 0,
                                          -1 ) : tripoint_rel_ms::zero ) );
    CHECK_FALSE( target.is_dead_state() );
    if( fall ) {
        CHECK( view( id ).context.at( "execution_fell" ) == 1 );
    }
}

TEST_CASE( "executions_native_fall_death_keeps_the_real_source_and_releases_control",
           "[executions][map]" )
{
    execution_world world;
    map &here = get_map();
    monster &target = spawn_test_monster( "mon_zombie", get_avatar().pos_bub() + tripoint_rel_ms( 2, 0,
                                          0 ) );
    target.set_hp( 1 );
    const tripoint_bub_ms edge = target.pos_bub() + tripoint_rel_ms::east;
    here.ter_set( edge, ter_t_open_air );
    here.ter_set( edge + tripoint_rel_ms::below, ter_t_floor );
    const int64_t id = start( "test_exec_fall", get_avatar(), target );
    g->executions().drain_starts();
    finish_scene( id );
    CHECK( target.is_dead_state() );
    CHECK( target.get_killer() == &get_avatar() );
    CHECK( view( id ).state == "completed" );
    CHECK( view( id ).context.at( "execution_target_dead" ) == 1 );
    CHECK_FALSE( g->executions().controls( target ) );
    CHECK( counter( "exec_ends" ) == 1 );
}

TEST_CASE( "executions_keep_a_removed_fall_source_alive_through_native_callbacks",
           "[executions][map]" )
{
    execution_world world;
    map &here = get_map();
    monster &actor = spawn_test_monster( "mon_zombie", get_avatar().pos_bub() + tripoint_rel_ms( 2, 0,
                                         0 ) );
    monster &target = spawn_test_monster( "mon_zombie", actor.pos_bub() + tripoint_rel_ms( 2, 0, 0 ) );
    target.set_hp( 5000 );
    const tripoint_bub_ms edge = target.pos_bub() + tripoint_rel_ms::east;
    here.ter_set( edge, ter_t_open_air );
    here.ter_set( edge + tripoint_rel_ms::below, ter_t_floor );
    struct remover : event_subscriber {
        using event_subscriber::notify;
        monster *source = nullptr;
        weak_ptr_fast<monster> lifetime;
        bool fired = false;
        bool held_during_callback = false;
        void notify( const cata::event &e ) override {
            if( fired || e.type() != event_type::monster_takes_damage ) {
                return;
            }
            fired = true;
            get_creature_tracker().remove( *source );
            get_creature_tracker().remove_dead();
            held_during_callback = !lifetime.expired();
        }
    } observer;
    observer.source = &actor;
    observer.lifetime = get_creature_tracker().find_by_uid( actor.persistent_id() );
    get_event_bus().subscribe( &observer );
    const int64_t id = start( "test_exec_fall", actor, target );
    g->executions().drain_starts();
    for( int n = 0; n < 12 && view( id ).state == "running"; ++n ) {
        tick();
    }
    CHECK( observer.fired );
    CHECK( observer.held_during_callback );
    CHECK( observer.lifetime.expired() );
    CHECK( view( id ).state == "interrupted" );
    CHECK( view( id ).reason == "participant_unavailable" );
    CHECK_FALSE( g->executions().controls( target ) );
}

TEST_CASE( "executions_fall_probe_respects_flight_and_vehicle_support", "[executions][map]" )
{
    execution_world world;
    map &here = get_map();
    const bool flight = GENERATE( false, true );
    monster &target = spawn_test_monster( flight ? "mon_crow" : "mon_zombie",
                                          get_avatar().pos_bub() + tripoint_rel_ms( 2, 0, 0 ) );
    const tripoint_bub_ms edge = target.pos_bub() + tripoint_rel_ms::east;
    here.ter_set( edge, ter_t_open_air );
    here.ter_set( edge + tripoint_rel_ms::below, ter_t_floor );
    if( !flight ) {
        vehicle *veh = here.add_vehicle( vehicle_prototype_none, edge, 0_degrees, 0,
                                         veh_spawn_status::UNDAMAGED );
        REQUIRE( veh );
        REQUIRE( veh->install_part( here, point_rel_ms::zero, vpart_frame ) != -1 );
        REQUIRE( veh->install_part( here, point_rel_ms::zero, vpart_aisle ) != -1 );
        here.add_vehicle_to_cache( veh );
        REQUIRE( here.has_floor_or_support( edge ) );
    }
    const int64_t id = start( "test_exec_fall", get_avatar(), target );
    g->executions().drain_starts();
    CHECK_FALSE( g->executions().can_fall( id, "target" ) );
}

TEST_CASE( "executions_never_rebind_a_removed_participant_to_its_replacement", "[executions]" )
{
    execution_world world;
    monster &actor = spawn_test_monster( "mon_zombie", get_avatar().pos_bub() + tripoint_rel_ms( 2, 0,
                                         0 ) );
    monster &target = spawn_test_monster( "mon_zombie", actor.pos_bub() + tripoint_rel_ms( 2, 0, 0 ) );
    const tripoint_abs_ms old_pos = target.pos_abs();
    const int64_t old_id = target.persistent_id();
    const int64_t id = start( "test_exec_clock", actor, target );
    g->executions().drain_starts();
    get_creature_tracker().remove( target );
    monster &replacement = spawn_test_monster( "mon_zombie", get_map().get_bub( old_pos ) );
    REQUIRE( replacement.persistent_id() != old_id );
    get_creature_tracker().remove( actor );
    // Both missing talkers are valid for global interruption/end callbacks.
    g->executions().process();
    CHECK( view( id ).reason == "participant_unavailable" );
    CHECK( replacement.pos_abs() == old_pos );
    CHECK_FALSE( g->executions().controls( replacement ) );
    CHECK( counter( "exec_interrupts" ) == 1 );
    CHECK( counter( "exec_ends" ) == 1 );
}

TEST_CASE( "executions_weapon_identity_is_not_just_the_item_type", "[executions][eoc]" )
{
    execution_world world;
    avatar &actor = get_avatar();
    item first_knife( itype_knife_combat );
    REQUIRE( actor.wield( first_knife ) );
    npc &target = spawn_npc( actor.pos_bub().xy() + point_rel_ms( 2, 0 ), "thug" );
    const int64_t weapon = actor.get_wielded_item()->uid().get_value();
    const int64_t id = start( "test_exec_weapon", actor, target );
    g->executions().drain_starts();
    actor.remove_weapon();
    item replacement_knife( itype_knife_combat );
    REQUIRE( actor.wield( replacement_knife ) );
    REQUIRE( actor.get_wielded_item()->uid().get_value() != weapon );
    tick();
    CHECK( view( id ).reason == "weapon_lost" );
    dialogue reported( get_talker_for( actor ), get_talker_for( target ) );
    reported.set_value( "weapon_uid", std::to_string( weapon ) );
    reported.set_value( "weapon", "knife_combat" );
    CHECK( g->executions().start( execution_definition_test_exec_weapon,
                                  reported ).reason == "weapon_lost" );
}

TEST_CASE( "executions_own_damage_and_effect_actions_return_actual_results", "[executions]" )
{
    execution_world world;
    monster &target = spawn_test_monster( "mon_zombie", get_avatar().pos_bub() + tripoint_rel_ms( 2, 0,
                                          0 ) );
    const int hp = target.get_hp();
    const int64_t id = start( "test_exec_strike", get_avatar(), target );
    g->executions().drain_starts();
    CHECK( target.get_hp() == hp - 5 );
    CHECK( view( id ).context.at( "execution_hp_change" ) == -5 );
    CHECK( view( id ).state == "running" );
    finish_scene( id );
    const int64_t other = start( "test_exec_effect", get_avatar(), target );
    g->executions().drain_starts();
    CHECK( target.has_effect( effect_stunned ) );
    CHECK( view( other ).context.at( "execution_result_effect" ) == "applied" );
}

TEST_CASE( "executions_zero_delay_cycles_timeout_and_callback_reentry_are_bounded", "[executions]" )
{
    execution_world world;
    npc &target = spawn_npc( get_avatar().pos_bub().xy() + point_rel_ms( 2, 0 ), "thug" );
    SECTION( "zero-delay cycle yields then hits its lifetime step budget" ) {
        const int64_t id = start( "test_exec_loop", get_avatar(), target );
        g->executions().drain_starts();
        CHECK( view( id ).state == "running" );
        tick( 3 );
        CHECK( view( id ).reason == "step_budget" );
        CHECK_FALSE( g->executions().controls( target ) );
    }
    SECTION( "timeout" ) {
        const int64_t id = start( "test_exec_timeout", get_avatar(), target );
        g->executions().drain_starts();
        tick();
        CHECK( view( id ).reason == "timeout" );
    }
    SECTION( "cancel from the start callback prevents the first graph action" ) {
        const int64_t id = start( "test_exec_cancel_self", get_avatar(), target );
        g->executions().drain_starts();
        CHECK( view( id ).reason == "cancelled" );
        CHECK( counter( "exec_entries" ) == 0 );
        CHECK( counter( "exec_ends" ) == 1 );
    }
    SECTION( "a nested start waits for a later safe drain" ) {
        const int64_t id = start( "test_exec_reentrant", get_avatar(), target );
        g->executions().drain_starts();
        CHECK( view( id ).state == "running" );
        CHECK( view( id + 1 ).state == "pending" );
        CHECK( counter( "exec_entries" ) == 1 );
        g->executions().drain_starts();
        CHECK( view( id + 1 ).state == "running" );
        CHECK( counter( "exec_entries" ) == 2 );
    }
}

TEST_CASE( "executions_incompatible_saved_revisions_release_their_participants",
           "[executions][save]" )
{
    execution_world world;
    npc &target = spawn_npc( get_avatar().pos_bub().xy() + point_rel_ms( 2, 0 ), "thug" );
    const int64_t id = start( "test_exec_clock", get_avatar(), target );
    g->executions().drain_starts();
    std::string saved = save_executions();
    const size_t revision = saved.find( "\"revision\":1" );
    REQUIRE( revision != std::string::npos );
    saved.replace( revision, std::string( "\"revision\":1" ).size(), "\"revision\":99" );
    g->executions().deserialize( json_loader::from_string( saved ) );
    g->executions().process();
    CHECK( view( id ).state == "interrupted" );
    CHECK_FALSE( g->executions().controls( target ) );
}

TEST_CASE( "executions_melee_post_hit_trigger_carries_weapon_style_and_hit_context",
           "[executions][martial_arts]" )
{
    execution_world world;
    avatar &actor = get_avatar();
    item knife( itype_knife_combat );
    REQUIRE( actor.wield( knife ) );
    const std::string weapon = std::to_string( actor.get_wielded_item()->uid().get_value() );
    monster &target = spawn_test_monster( "mon_test_non_shearable",
                                          actor.pos_bub() + tripoint_rel_ms::east );
    target.set_hp( 10000 );
    for( int attempt = 0; attempt < 100 && counter( "exec_accepted" ) == 0; ++attempt ) {
        actor.set_moves( 100 );
        actor.set_stamina( actor.get_stamina_max() );
        actor.melee_attack_abstract( target, false, test_exec_post_hit );
    }
    REQUIRE( counter( "exec_accepted" ) == 1 );
    const int64_t id = std::stoll( get_globals().get_global_value( "exec_last_id" ).str() );
    const execution_view queued = view( id );
    CHECK( queued.state == "pending" );
    CHECK( queued.context.at( "weapon_uid" ) == weapon );
    CHECK( queued.context.at( "style" ) == actor.martial_arts_data->selected_style().str() );
    CHECK( queued.context.at( "hit" ) == 1 );
    CHECK( queued.context.at( "damage" ).dbl() > 0 );
    CHECK( queued.context.count( "bp" ) == 1 );
    CHECK( counter( "exec_starts" ) == 0 );
    g->executions().drain_starts();
    CHECK( counter( "exec_starts" ) == 1 );
}

TEST_CASE( "execution_definitions_reject_invalid_graphs_and_unsupported_fields",
           "[executions][json]" )
{
    const auto load = []( const std::string & json ) {
        execution_definition def;
        def.load( json_loader::from_string( json ), "execution_tests" );
    };
    SECTION( "missing next step" ) {
        CHECK_THROWS( load(
                          R"({ "name": "bad", "start": "a", "steps": { "a": { "next": "missing" } } })" ) );
    }
    SECTION( "invalid probability" ) {
        CHECK_THROWS( load( R"({ "name": "bad", "start": "a", "steps": {
            "a": { "next": { "chance": 1.1, "then": "a", "else": "a" } } } })" ) );
    }
    SECTION( "reversed range" ) {
        CHECK_THROWS( load( R"({ "name": "bad", "start": "a", "steps": {
            "a": { "next": { "step": "a", "after_ticks": [3,1] } } } })" ) );
    }
    SECTION( "ambiguous transition" ) {
        CHECK_THROWS( load( R"({ "name": "bad", "start": "a", "steps": {
            "a": { "next": { "step": "a", "chance": 0.5, "then": "a", "else": "a" } } } })" ) );
    }
    SECTION( "fatal reservation is not silently accepted" ) {
        CHECK_THROWS( load( R"({ "name": "bad", "start": "a", "steps": {
            "a": { "action": { "op": "settle_fatal" }, "end": "done" } } })" ) );
    }
    SECTION( "unknown append anchors are rejected" ) {
        CHECK_THROWS( load( R"({ "name": "bad", "start": "a", "steps": {
            "a": { "action": { "op": "message", "text": "test", "append_to": "other" }, "end": "done" } } })" ) );
    }
}

TEST_CASE( "execution_monster_identity_survives_serialization_polymorph_and_horde_storage",
           "[executions][save]" )
{
    execution_world world;
    monster &original = spawn_test_monster( "mon_zombie", get_avatar().pos_bub() + tripoint_rel_ms( 2,
                                            0, 0 ) );
    const int64_t uid = original.persistent_id();
    const creature_ref ref = creature_ref::bind( original );
    original.poly( mon_zombie_fat );
    CHECK( original.persistent_id() == uid );
    CHECK( ref.resolve() == &original );
    monster copy( original );
    CHECK( copy.persistent_id() != uid );
    const horde_entity stored( original );
    REQUIRE( stored.monster_data );
    CHECK( stored.monster_data->persistent_id() == uid );
    const std::string saved = save_monster( original );
    monster loaded;
    loaded.deserialize( json_loader::from_string( saved ) );
    CHECK( loaded.persistent_id() == uid );
    std::string legacy = saved;
    const size_t begin = legacy.find( "\"monster_uid\":" );
    REQUIRE( begin != std::string::npos );
    const size_t end = legacy.find( ',', begin );
    REQUIRE( end != std::string::npos );
    legacy.erase( begin, end - begin + 1 );
    monster migrated;
    migrated.deserialize( json_loader::from_string( legacy ) );
    CHECK( migrated.persistent_id() > uid );
    CHECK( migrated.persistent_id() != loaded.persistent_id() );
}

TEST_CASE( "execution_references_do_not_keep_an_assigned_or_removed_monster_alive",
           "[executions][save]" )
{
    execution_world world;
    monster &original = spawn_test_monster( "mon_zombie", get_avatar().pos_bub() + tripoint_rel_ms( 2,
                                            0, 0 ) );
    const creature_ref ref = creature_ref::bind( original );
    monster replacement( original );
    original = replacement;
    CHECK( original.persistent_id() != ref.id );
    CHECK( ref.resolve() == nullptr );
    const weak_ptr_fast<monster> lifetime = get_creature_tracker().find_by_uid(
            original.persistent_id() );
    REQUIRE_FALSE( lifetime.expired() );
    get_creature_tracker().remove( original );
    get_creature_tracker().remove_dead();
    CHECK( lifetime.expired() );
}

TEST_CASE( "execution_monster_uid_allocation_respects_master_and_dimension_counters",
           "[executions][save]" )
{
    execution_world world;
    const int64_t next = g->assign_monster_uid();
    g->unserialize_master( json_loader::from_string( "{\"next_monster_uid\":" +
                           std::to_string( next + 100 ) + "}" ) );
    CHECK( g->assign_monster_uid() == next + 100 );
    g->unserialize_dimension_data( json_loader::from_string( "{\"next_monster_uid\":" +
                                   std::to_string( next + 200 ) + "}" ) );
    CHECK( g->assign_monster_uid() == next + 200 );
    // Loading older dimension data must not make a future spawn reuse an ID.
    g->unserialize_dimension_data( json_loader::from_string( "{\"next_monster_uid\":1}" ) );
    CHECK( g->assign_monster_uid() == next + 201 );
    CHECK_THROWS( g->reserve_monster_uid( std::numeric_limits<int64_t>::max() ) );
}
