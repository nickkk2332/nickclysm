#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <utility>

#include "activity_actor_definitions.h"
#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "condition.h"
#include "coordinates.h"
#include "creature.h"
#include "creature_tracker.h"
#include "damage.h"
#include "dialogue.h"
#include "effect.h"
#include "effect_source.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "execution.h"
#include "execution_runtime.h"
#include "execution_trigger.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "global_vars.h"
#include "incapacitation.h"
#include "item.h"
#include "item_location.h"
#include "json.h"
#include "json_loader.h"
#include "kill_tracker.h"
#include "magic.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser_diag_value.h"
#include "mattack_common.h"
#include "memory_fast.h"
#include "monster.h"
#include "npc.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "point.h"
#include "rng.h"
#include "talker.h"
#include "type_id.h"

static const damage_type_id damage_pure( "pure" );

static const efftype_id effect_knocked_out( "knocked_out" );
static const efftype_id effect_sleep( "sleep" );
static const efftype_id effect_stunned( "stunned" );
static const efftype_id effect_test_knockout_immune( "test_knockout_immune" );
static const efftype_id effect_test_knockout_uncanny( "test_knockout_uncanny" );
static const efftype_id effect_test_knockout_vulnerable( "test_knockout_vulnerable" );

static const execution_trigger_id
execution_trigger_demo_strength_takedown_trigger( "demo_strength_takedown_trigger" );

static const matec_id tec_none( "tec_none" );

static const spell_id spell_test_spell_pew( "test_spell_pew" );

static const ter_str_id ter_t_floor( "t_floor" );
static const ter_str_id ter_t_open_air( "t_open_air" );

namespace
{

struct knockout_world {
    time_point old_time = calendar::turn;
    knockout_world() {
        g->executions().clear();
        clear_avatar();
        clear_map_without_vision( -2, 1 );
        get_globals().clear_global_values();
        get_avatar().setpos( get_map(), tripoint_bub_ms( 60, 60, 0 ) );
    }
    ~knockout_world() {
        g->executions().clear();
        clear_avatar();
        calendar::turn = old_time;
    }
};

Creature &subject( const std::string &kind )
{
    if( kind == "avatar" ) {
        return get_avatar();
    }
    if( kind == "npc" ) {
        return spawn_npc( get_avatar().pos_bub().xy() + point_rel_ms( 2, 0 ), "thug" );
    }
    return spawn_test_monster( "mon_execution_knockout_animal", get_avatar().pos_bub() +
                               tripoint_rel_ms( 2, 0, 0 ) );
}

execution_view view( int64_t id )
{
    const auto result = g->executions().inspect( id );
    REQUIRE( result.has_value() );
    return *result;
}

int64_t start( const std::string &definition, Creature &actor, Creature &target )
{
    dialogue d( get_talker_for( actor ), get_talker_for( target ) );
    const execution_start_result result = g->executions().start( execution_id( definition ), d,
                                          UINT64_C( 41723 ) );
    REQUIRE( result.reason == "queued" );
    REQUIRE( result.id > 0 );
    g->executions().drain_starts();
    return result.id;
}

void tick( int turns = 1 )
{
    for( int n = 0; n < turns; ++n ) {
        calendar::turn += 1_turns;
        g->executions().process();
    }
}

std::string save_actor( const Creature &who )
{
    std::ostringstream text;
    JsonOut out( text );
    if( const monster *mon = who.as_monster() ) {
        mon->serialize( out );
    } else if( const npc *person = who.as_npc() ) {
        person->serialize( out );
    } else {
        static_cast<const avatar &>( who ).serialize( out );
    }
    return text.str();
}

std::string save_scene()
{
    std::ostringstream text;
    JsonOut out( text );
    g->executions().serialize( out );
    return text.str();
}

std::string save_effect( const effect &e )
{
    std::ostringstream text;
    JsonOut out( text );
    e.serialize( out );
    return text.str();
}
} // namespace

TEST_CASE( "execution_knockouts_outlast_the_scene_without_death_or_rewards",
           "[executions][knockout]" )
{
    knockout_world world;
    const std::string kind = GENERATE( "npc", "monster" );
    Creature &target = subject( kind );
    const int hp = target.get_hp();
    const int kills = g->get_kill_tracker().total_kill_count();
    const int64_t id = start( "test_exec_knockout", get_avatar(), target );
    REQUIRE( view( id ).context.at( "execution_result_ko" ) == "applied" );
    REQUIRE( incapacitation::unconscious( target ) );
    const time_duration duration = target.get_effect( effect_knocked_out ).get_duration();
    CHECK( duration >= 10_turns );
    CHECK( duration <= 30_turns );
    CHECK( target.in_sleep_state() );
    CHECK( target.is_on_ground() );
    CHECK_FALSE( target.has_effect( effect_sleep ) );
    CHECK_FALSE( target.has_effect( effect_stunned ) );
    CHECK( g->executions().controls( target ) );
    tick();
    CHECK( view( id ).state == "completed" );
    CHECK_FALSE( g->executions().controls( target ) );
    CHECK( incapacitation::unconscious( target ) );
    g->executions().clear();
    CHECK( target.get_effect( effect_knocked_out ).get_duration() == duration - 1_turns );
    CHECK( target.get_hp() == hp );
    CHECK_FALSE( target.is_dead_state() );
    CHECK( target.get_killer() == nullptr );
    CHECK( g->get_kill_tracker().total_kill_count() == kills );
    CHECK( get_map().i_at( target.pos_bub() ).empty() );
}

TEST_CASE( "execution_knockout_of_the_controlled_avatar_completes_and_blocks_input",
           "[executions][knockout][activity]" )
{
    knockout_world world;
    avatar &who = get_avatar();
    Creature &other = subject( "npc" );
    const int64_t id = start( "test_exec_knockout_avatar", who, other );
    CHECK( view( id ).state == "completed" );
    CHECK( view( id ).context.at( "execution_result_ko" ) == "applied" );
    CHECK( who.activity.is_null() );
    REQUIRE( incapacitation::unconscious( who ) );
    who.set_moves( 500 );
    who.process_activity(); // Runs before the turn loop considers human input, even with no activity.
    CHECK( who.get_moves() == 0 );
    CHECK( who.is_blind() );
    CHECK( who.is_deaf() );
    CHECK_FALSE( who.can_hear( other.pos_bub(), 1000 ) );
    CHECK_FALSE( who.sees( get_map(), other ) );
    who.wake_up(); // Ordinary sleep/alarms cannot clear a physiological knockout.
    CHECK( incapacitation::unconscious( who ) );
    calendar::turn += 20_turns;
    who.process_effects();
    CHECK_FALSE( incapacitation::unconscious( who ) );
    CHECK_FALSE( who.is_blind() );
    CHECK_FALSE( who.is_deaf() );
    CHECK( who.get_moves() <= 0 );
}

TEST_CASE( "knockout_clock_ignores_speed_and_repeated_effect_processing",
           "[executions][knockout][effect]" )
{
    knockout_world world;
    const std::string kind = GENERATE( "avatar", "npc", "monster" );
    Creature &who = subject( kind );
    REQUIRE( incapacitation::apply( who, nullptr, 10_turns, false ) == "applied" );
    who.set_speed_base( 1000 );
    for( int n = 0; n < 5; ++n ) {
        who.process_effects();
        who.process_turn();
        CHECK( who.get_effect( effect_knocked_out ).get_duration() == 10_turns );
        CHECK( who.get_moves() <= 0 );
    }
    calendar::turn += 9_turns;
    CHECK( incapacitation::unconscious( who ) );
    CHECK( who.get_effect( effect_knocked_out ).get_duration() == 1_turns );
    calendar::turn += 1_turns;
    CHECK_FALSE( incapacitation::unconscious( who ) );
    who.process_effects();
    CHECK_FALSE( who.has_effect( effect_knocked_out ) );
}

TEST_CASE( "knockouts_suppress_native_movement_combat_activities_and_specials",
           "[executions][knockout]" )
{
    knockout_world world;
    const std::string kind = GENERATE( "avatar", "npc", "monster" );
    Creature &who = subject( kind );
    REQUIRE( incapacitation::apply( who, nullptr, 20_turns, true ) == "applied" );
    const tripoint_abs_ms position = who.pos_abs();
    const int hp = get_avatar().get_hp();
    item invalid_gun;
    // NOLINTNEXTLINE(cata-determinism)
    const cata_default_random_engine before = rng_get_engine();
    if( Character *person = who.as_character() ) {
        CHECK_FALSE( person->move_effects( false ) );
        CHECK_FALSE( person->melee_attack_abstract( get_avatar(), false, tec_none ) );
        CHECK( person->fire_gun( get_map(), get_avatar().pos_bub(), 1, invalid_gun, {} ) == 0 );
        CHECK( person->get_dodge() == 0 );
        if( npc *n = person->as_npc() ) {
            n->set_moves( 500 );
            n->move_to( n->pos_bub() + tripoint_rel_ms::east );
            n->move();
            CHECK( n->get_moves() == 0 );
        }
    } else {
        monster &mon = *who.as_monster();
        mon.set_moves( 500 );
        CHECK_FALSE( mon.can_act() );
        CHECK_FALSE( mon.melee_attack( get_avatar(), 1000.0f ) );
        CHECK( mon.get_dodge() == 0 );
        const mtype_special_attack probe( "knockout_probe", []( monster * ) {
            get_globals().set_global_value( "knockout_special_fired", 1 );
            return true;
        } );
        CHECK_FALSE( probe->call( mon ) );
        CHECK( get_globals().get_global_value( "knockout_special_fired" ).dbl() == 0 );
        mon.move();
        CHECK( mon.get_moves() == 0 );
    }
    CHECK( who.pos_abs() == position );
    CHECK( get_avatar().get_hp() == hp );
    CHECK( rng_get_engine() == before );
}

TEST_CASE( "knockout_wakeup_depends_on_actual_HP_loss_and_authored_depth",
           "[executions][knockout][damage]" )
{
    knockout_world world;
    const std::string kind = GENERATE( "avatar", "npc", "monster" );
    const bool wake = GENERATE( false, true );
    Creature &who = subject( kind );
    REQUIRE( incapacitation::apply( who, nullptr, 20_turns, wake ) == "applied" );
    who.deal_damage( nullptr, body_part_torso.id(), damage_instance( damage_pure, 0 ) );
    who.heal_bp( body_part_torso, 1 );
    CHECK( incapacitation::unconscious( who ) );
    who.apply_damage( nullptr, body_part_torso.id(), 1 );
    CHECK( incapacitation::unconscious( who ) == !wake );
    CHECK_FALSE( who.is_dead_state() );
}

TEST_CASE( "knockout_prevents_evasion_and_forced_dodges_even_against_a_bad_attack_roll",
           "[executions][knockout][dodge]" )
{
    knockout_world world;
    const std::string kind = GENERATE( "avatar", "npc", "monster" );
    Creature &who = subject( kind );
    REQUIRE( incapacitation::apply( who, nullptr, 20_turns, false ) == "applied" );
    // NOLINTNEXTLINE(cata-determinism)
    const cata_default_random_engine before = rng_get_engine();
    CHECK( who.dodge_roll() == 0 );
    CHECK_FALSE( who.dodge_check( -10000.0f, true, 0.0f ) );
    CHECK( rng_get_engine() == before );
}

TEST_CASE( "knockout_damage_wakeup_is_visible_to_damage_callbacks",
           "[executions][knockout][damage]" )
{
    knockout_world world;
    const std::string kind = GENERATE( "npc", "monster" );
    Creature &who = subject( kind );
    REQUIRE( incapacitation::apply( who, &get_avatar(), 20_turns, true ) == "applied" );
    struct observer : event_subscriber {
        using event_subscriber::notify;
        Creature *who = nullptr;
        bool fired = false;
        bool awake = false;
        void notify( const cata::event &e ) override {
            if( e.type() == event_type::character_takes_damage ||
                e.type() == event_type::monster_takes_damage ) {
                fired = true;
                awake = !incapacitation::unconscious( *who );
            }
        }
    } callback;
    callback.who = &who;
    get_event_bus().subscribe( &callback );
    who.apply_damage( &get_avatar(), body_part_torso.id(), 1 );
    CHECK( callback.fired );
    CHECK( callback.awake );
}

TEST_CASE( "knockouts_allow_environmental_and_fatal_damage_through_normal_death",
           "[executions][knockout][damage]" )
{
    knockout_world world;
    SECTION( "distributed environmental damage wakes a light knockout" ) {
        avatar &who = get_avatar();
        REQUIRE( incapacitation::apply( who, nullptr, 20_turns, true ) == "applied" );
        const int hp = who.get_hp();
        who.hurtall( 1, nullptr );
        CHECK( who.get_hp() < hp );
        CHECK_FALSE( incapacitation::unconscious( who ) );
    }
    SECTION( "a deep knockout offers no protection against death" ) {
        monster &who = *subject( "monster" ).as_monster();
        const int kills = g->get_kill_tracker().total_kill_count();
        REQUIRE( incapacitation::apply( who, &get_avatar(), 20_turns, false ) == "applied" );
        who.apply_damage( &get_avatar(), body_part_torso.id(), who.get_hp() );
        who.check_dead_state( &get_map() );
        who.check_dead_state( &get_map() );
        CHECK( who.is_dead_state() );
        CHECK( who.get_hp() <= 0 );
        CHECK( g->get_kill_tracker().total_kill_count() == kills + 1 );
    }
}

TEST_CASE( "knockout_admission_reports_immunity_physiology_and_existing_states",
           "[executions][knockout]" )
{
    knockout_world world;
    SECTION( "explicit character immunity" ) {
        avatar &who = get_avatar();
        who.add_effect( effect_test_knockout_immune, 1_minutes );
        CHECK( incapacitation::apply( who, nullptr, 20_turns, true ) == "immune" );
        CHECK_FALSE( incapacitation::unconscious( who ) );
    }
    SECTION( "monster type immunity" ) {
        monster &who = spawn_test_monster( "mon_execution_knockout_immune",
                                           get_avatar().pos_bub() + tripoint_rel_ms::east );
        CHECK( incapacitation::apply( who, nullptr, 20_turns, true ) == "immune" );
    }
    SECTION( "transient monster immunity also works alongside native monster flags" ) {
        Creature &who = subject( "monster" );
        who.add_effect( effect_test_knockout_immune, 1_minutes );
        CHECK( incapacitation::apply( who, nullptr, 20_turns, true ) == "immune" );
    }
    SECTION( "unsupported physiologies can explicitly opt in" ) {
        const std::string type = GENERATE( "mon_zombie", "mon_manhack" );
        monster &who = spawn_test_monster( type, get_avatar().pos_bub() + tripoint_rel_ms::east );
        CHECK( incapacitation::apply( who, nullptr, 20_turns, true ) == "unsupported" );
        who.add_effect( effect_test_knockout_vulnerable, 1_minutes );
        CHECK( incapacitation::apply( who, nullptr, 20_turns, true ) == "applied" );
    }
    SECTION( "ordinary sleep stays independent" ) {
        avatar &who = get_avatar();
        who.fall_asleep( 1_hours );
        CHECK( incapacitation::apply( who, nullptr, 20_turns, false ) == "already_incapacitated" );
        CHECK_FALSE( who.has_effect( effect_knocked_out ) );
        CHECK( who.has_effect( effect_sleep ) );
    }
    SECTION( "dead targets are unavailable" ) {
        monster &who = *subject( "monster" ).as_monster();
        who.set_hp( 0 );
        CHECK( incapacitation::apply( who, nullptr, 20_turns, true ) == "unavailable" );
    }
    SECTION( "a seated character is explicitly unsupported" ) {
        avatar &who = get_avatar();
        who.in_vehicle = true;
        CHECK( incapacitation::apply( who, nullptr, 20_turns, true ) == "unsupported" );
        who.in_vehicle = false;
    }
}

TEST_CASE( "knockout_reapplication_and_scene_repetition_do_not_extend_the_deadline",
           "[executions][knockout]" )
{
    knockout_world world;
    Creature &who = subject( "monster" );
    const int64_t id = start( "test_exec_knockout_repeat", get_avatar(), who );
    REQUIRE( incapacitation::unconscious( who ) );
    tick();
    CHECK( view( id ).state == "completed" );
    CHECK( view( id ).context.at( "execution_result_ko" ) == "already_incapacitated" );
    CHECK( who.get_effect( effect_knocked_out ).get_duration() == 19_turns );
    CHECK( incapacitation::apply( who, nullptr, 1_hours, false ) == "already_incapacitated" );
    CHECK( who.get_effect( effect_knocked_out ).get_duration() == 19_turns );
    CHECK( who.get_effect( effect_knocked_out ).get_intensity() == 1 );
}

TEST_CASE( "a_new_knockout_replaces_an_expired_entry_with_its_own_source_and_depth",
           "[executions][knockout][effect]" )
{
    knockout_world world;
    const std::string kind = GENERATE( "avatar", "npc", "monster" );
    const bool monster_source = GENERATE( false, true );
    Creature &who = subject( kind );
    Creature *source;
    if( monster_source ) {
        source = &spawn_test_monster( "mon_execution_knockout_animal",
                                      get_avatar().pos_bub() + tripoint_rel_ms::west );
    } else {
        source = &spawn_npc( get_avatar().pos_bub().xy() + point_rel_ms( -2, 0 ), "thug" );
    }
    REQUIRE( incapacitation::apply( who, nullptr, 10_turns, true ) == "applied" );
    who.apply_damage( nullptr, body_part_torso.id(), 1 );
    REQUIRE_FALSE( incapacitation::unconscious( who ) );
    REQUIRE( who.has_effect( effect_knocked_out ) );
    REQUIRE( incapacitation::apply( who, source, 20_turns, false ) == "applied" );
    const effect_source &origin = who.get_effect( effect_knocked_out ).get_source();
    if( monster_source ) {
        CHECK( origin.get_mfaction_id() == source->as_monster()->faction );
    } else {
        CHECK( origin.resolve_creature() == source );
    }
    CHECK( who.get_effect( effect_knocked_out ).get_duration() == 20_turns );
    CHECK( who.get_effect( effect_knocked_out ).get_intensity() == 2 );
    who.apply_damage( nullptr, body_part_torso.id(), 1 );
    CHECK( incapacitation::unconscious( who ) );
}

TEST_CASE( "explicit_knockout_removal_restores_cached_character_vision_immediately",
           "[executions][knockout][effect]" )
{
    knockout_world world;
    const std::string kind = GENERATE( "avatar", "npc" );
    Character &who = *subject( kind ).as_character();
    who.recalc_sight_limits();
    const int sight = who.sight_range( 100.0f );
    REQUIRE( sight > 0 );
    REQUIRE( incapacitation::apply( who, nullptr, 20_turns, false ) == "applied" );
    CHECK( who.sight_range( 100.0f ) == 1 ); // Native sight range retains the actor's own tile.
    who.remove_effect( effect_knocked_out );
    CHECK_FALSE( incapacitation::unconscious( who ) );
    CHECK( who.sight_range( 100.0f ) == sight );
}

TEST_CASE( "knockout_cancels_ordinary_work_and_blocks_even_forced_activity_processing",
           "[executions][knockout][activity]" )
{
    knockout_world world;
    avatar &who = get_avatar();
    who.assign_activity( wait_activity_actor( 10_minutes ) );
    REQUIRE_FALSE( who.activity.is_null() );
    REQUIRE( incapacitation::apply( who, nullptr, 20_turns, false ) == "applied" );
    CHECK( who.activity.is_null() );
    who.assign_activity( wait_activity_actor( 10_minutes ) );
    who.set_moves( 500 );
    who.process_activity();
    CHECK( who.activity.is_null() );
    CHECK( who.get_moves() <= 0 );
}

TEST_CASE( "knockout_removed_by_its_gain_callback_reports_interruption",
           "[executions][knockout][eoc]" )
{
    knockout_world world;
    Creature &who = subject( "npc" );
    struct remover : event_subscriber {
        using event_subscriber::notify;
        Creature *who = nullptr;
        bool fired = false;
        void notify( const cata::event &e ) override {
            if( !fired && e.type() == event_type::character_gains_effect ) {
                fired = true;
                who->remove_effect( effect_knocked_out );
            }
        }
    } callback;
    callback.who = &who;
    get_event_bus().subscribe( &callback );
    CHECK( incapacitation::apply( who, nullptr, 20_turns, true ) == "interrupted" );
    CHECK( callback.fired );
    CHECK_FALSE( incapacitation::unconscious( who ) );
}

TEST_CASE( "knockout_deadlines_and_depth_survive_actor_serialization_and_elapsed_time",
           "[executions][knockout][save]" )
{
    knockout_world world;
    const std::string kind = GENERATE( "avatar", "npc", "monster" );
    Creature &who = subject( kind );
    REQUIRE( incapacitation::apply( who, nullptr, 20_turns, false ) == "applied" );
    const std::string saved = save_actor( who );
    calendar::turn += 7_turns;
    std::unique_ptr<Creature> restored;
    if( kind == "avatar" ) {
        restored = std::make_unique<avatar>();
    } else if( kind == "npc" ) {
        restored = std::make_unique<npc>();
    } else {
        restored = std::make_unique<monster>();
    }
    if( monster *mon = restored->as_monster() ) {
        mon->deserialize( json_loader::from_string( saved ) );
    } else if( npc *person = restored->as_npc() ) {
        person->deserialize( json_loader::from_string( saved ) );
    } else {
        static_cast<avatar &>( *restored ).deserialize( json_loader::from_string( saved ) );
    }
    REQUIRE( incapacitation::unconscious( *restored ) );
    CHECK( restored->get_effect( effect_knocked_out ).get_duration() == 13_turns );
    restored->apply_damage( nullptr, body_part_torso.id(), 1 );
    CHECK( incapacitation::unconscious( *restored ) );
    calendar::turn += 13_turns;
    CHECK_FALSE( incapacitation::unconscious( *restored ) );
    restored->process_effects();
    CHECK_FALSE( restored->has_effect( effect_knocked_out ) );
}

TEST_CASE( "knockout_and_scene_resume_without_reapplying_or_resampling_the_action",
           "[executions][knockout][save]" )
{
    knockout_world world;
    monster &who = *subject( "monster" ).as_monster();
    const int64_t id = start( "test_exec_knockout", get_avatar(), who );
    const time_duration duration = who.get_effect( effect_knocked_out ).get_duration();
    const std::string actor_data = save_actor( who );
    const std::string scene_data = save_scene();
    g->executions().clear();
    get_creature_tracker().remove( who );
    auto restored = make_shared_fast<monster>();
    restored->deserialize( json_loader::from_string( actor_data ) );
    REQUIRE( get_creature_tracker().add( restored ) );
    g->executions().deserialize( json_loader::from_string( scene_data ) );
    tick();
    CHECK( view( id ).state == "completed" );
    CHECK( restored->get_effect( effect_knocked_out ).get_duration() == duration - 1_turns );
    CHECK( view( id ).context.at( "execution_incapacitation_ticks" ) == to_turns<int>( duration ) );
}

TEST_CASE( "npc_offscreen_catchup_does_not_subtract_knockout_elapsed_time_twice",
           "[executions][knockout][npc][save]" )
{
    knockout_world world;
    npc &who = *subject( "npc" ).as_npc();
    REQUIRE( incapacitation::apply( who, nullptr, 1_hours, false ) == "applied" );
    who.on_unload();
    calendar::turn += 10_minutes;
    who.on_load( &get_map() );
    CHECK( who.get_effect( effect_knocked_out ).get_duration() == 50_minutes );
    CHECK( incapacitation::unconscious( who ) );
}

TEST_CASE( "knockout_conditions_are_read_only_and_never_fall_back_to_the_avatar",
           "[executions][knockout][eoc]" )
{
    knockout_world world;
    avatar &who = get_avatar();
    Creature &other = subject( "monster" );
    dialogue d( get_talker_for( who ), get_talker_for( other ) );
    CHECK( conditional_t( "npc_can_be_knocked_out" )( d ) );
    REQUIRE( incapacitation::apply( who, nullptr, 20_turns, false ) == "applied" );
    // NOLINTNEXTLINE(cata-determinism)
    const cata_default_random_engine before = rng_get_engine();
    const std::string state = save_actor( who );
    CHECK( conditional_t( "u_is_knocked_out" )( d ) );
    CHECK_FALSE( conditional_t( "u_can_be_knocked_out" )( d ) );
    CHECK_FALSE( conditional_t( "npc_is_knocked_out" )( d ) );
    dialogue missing( get_talker_for( other ), nullptr );
    CHECK_FALSE( conditional_t( "npc_is_knocked_out" )( missing ) );
    CHECK_FALSE( conditional_t( "npc_can_be_knocked_out" )( missing ) );
    CHECK( rng_get_engine() == before );
    CHECK( save_actor( who ) == state );
}

TEST_CASE( "knockouts_prevent_voluntary_spell_casting_until_recovery",
           "[executions][knockout][magic]" )
{
    knockout_world world;
    avatar &who = get_avatar();
    spell magic( spell_test_spell_pew );
    REQUIRE( magic.can_cast( who ) );
    REQUIRE( incapacitation::apply( who, nullptr, 10_turns, true ) == "applied" );
    CHECK_FALSE( magic.can_cast( who ) );
    who.apply_damage( nullptr, body_part_torso.id(), 1 );
    CHECK( magic.can_cast( who ) );
}

TEST_CASE( "knockout_prevents_uncanny_dodge_from_moving_the_unconscious_body",
           "[executions][knockout][martial_arts]" )
{
    knockout_world world;
    avatar &who = get_avatar();
    who.add_effect( effect_test_knockout_uncanny, 1_minutes );
    who.set_stamina( who.get_stamina_max() );
    REQUIRE( incapacitation::apply( who, nullptr, 10_turns, true ) == "applied" );
    const tripoint_abs_ms position = who.pos_abs();
    // NOLINTNEXTLINE(cata-determinism)
    const cata_default_random_engine before = rng_get_engine();
    const int stamina = who.get_stamina();
    CHECK_FALSE( who.uncanny_dodge() );
    CHECK( who.pos_abs() == position );
    CHECK( who.get_stamina() == stamina );
    CHECK( rng_get_engine() == before );
}

TEST_CASE( "knockout_reference_content_checks_real_character_strength_and_eligibility",
           "[executions][knockout][content]" )
{
    // This checks the executions_demo mod's own rule, so it runs only with that mod loaded, as
    // in CI's mods job.
    if( !execution_trigger_demo_strength_takedown_trigger.is_valid() ) {
        return;
    }
    knockout_world world;
    avatar &actor = get_avatar();
    npc &target = *subject( "npc" ).as_npc();
    actor.set_str_base( 20 );
    actor.reset_stats();
    target.set_str_base( 8 );
    target.reset_stats();
    REQUIRE( actor.get_str() >= target.get_str() + 4 );
    dialogue d( get_talker_for( actor ), get_talker_for( target ) );
    const execution_trigger &rule = *execution_trigger_demo_strength_takedown_trigger;
    CHECK( rule.condition( d ) );
    const int64_t id = start( "execution_strength_takedown", actor, target );
    CHECK( view( id ).context.at( "execution_result_knockout" ) == "applied" );
    tick();
    CHECK( view( id ).state == "completed" );
    CHECK( incapacitation::unconscious( target ) );
    target.remove_effect( effect_knocked_out );
    actor.set_str_base( 8 );
    actor.reset_stats();
    target.set_str_base( 20 );
    target.reset_stats();
    CHECK_FALSE( rule.condition( d ) );
    const int64_t refused = start( "execution_strength_takedown", actor, target );
    CHECK( view( refused ).outcome == "strength_requirement_failed" );
    CHECK_FALSE( incapacitation::unconscious( target ) );
}

TEST_CASE( "knockout_disables_flight_while_native_gravity_still_runs",
           "[executions][knockout][map]" )
{
    knockout_world world;
    map &here = get_map();
    const tripoint_bub_ms above( 62, 60, 1 );
    const tripoint_bub_ms below( 62, 60, 0 );
    here.ter_set( above, ter_t_open_air );
    here.ter_set( below, ter_t_floor );
    monster &who = spawn_test_monster( "mon_crow", above );
    who.set_hp( 10000 );
    REQUIRE( who.flies() );
    REQUIRE( incapacitation::apply( who, nullptr, 20_turns, false ) == "applied" );
    CHECK_FALSE( who.flies() );
    who.set_moves( 500 );
    who.move();
    CHECK( who.pos_bub() == below );
    CHECK( who.get_moves() <= 0 );
    CHECK( incapacitation::unconscious( who ) );
}

TEST_CASE( "knockout_calendar_timing_does_not_change_ordinary_effect_decay",
           "[executions][knockout][effect][save]" )
{
    knockout_world world;
    avatar &who = get_avatar();
    who.add_effect( effect_stunned, 10_turns );
    CHECK_FALSE( who.get_effect( effect_stunned ).has_calendar_deadline() );
    who.process_effects();
    CHECK( who.get_effect( effect_stunned ).get_duration() == 9_turns );
    REQUIRE( incapacitation::apply( who, nullptr, 10_turns, false ) == "applied" );
    effect saved = who.get_effect( effect_knocked_out );
    saved.pause_effect();
    CHECK_FALSE( saved.is_permanent() );
    const std::string data = save_effect( saved );
    calendar::turn += 10_turns;
    effect loaded;
    loaded.deserialize( json_loader::from_string( data ) );
    CHECK( loaded.get_duration() == 0_turns );
}

TEST_CASE( "knockout_definitions_reject_unsupported_states_and_unsafe_parameters",
           "[executions][knockout][json]" )
{
    const std::string field = GENERATE( R"("state":"stunned")", R"("respect_immunity":false)",
                                        R"("source":"nearest")", R"("duration_ticks":0)",
                                        R"("duration_ticks":3601)", R"("duration_ticks":[30,10])" );
    const std::string json = R"({"name":"Invalid knockout","start":"entry","steps":{
        "entry":{"action":{"op":"incapacitate",)" + field + R"(},"end":"done"}}})";
    execution_definition def;
    CHECK_THROWS_AS( def.load( json_loader::from_string( json ), "knockout_tests" ), JsonError );
}
