#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "activity_actor_definitions.h"
#include "avatar.h"
#include "avatar_action.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "character_id.h"
#include "character_martial_arts.h"
#include "condition.h"
#include "coordinates.h"
#include "creature.h"
#include "creature_ref.h"
#include "creature_tracker.h"
#include "damage.h"
#include "dialogue.h"
#include "effect.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "execution.h"
#include "execution_combat.h"
#include "execution_fatal.h"
#include "execution_runtime.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "global_vars.h"
#include "item.h"
#include "item_location.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "martialarts.h"
#include "math_parser_diag_value.h"
#include "messages.h"
#include "monster.h"
#include "npc.h"
#include "options_helpers.h"
#include "pimpl.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "point.h"
#include "rng.h"
#include "type_id.h"
#include "uistate.h"
#include "units.h"
#include "weakpoint.h"

static const activity_id ACT_EXECUTION( "ACT_EXECUTION" );
static const activity_id ACT_WAIT( "ACT_WAIT" );

static const damage_type_id damage_pure( "pure" );

static const dimension_id dimension_world_scarlet( "scarlet" );

static const efftype_id effect_stunned( "stunned" );

static const execution_id
execution_definition_execution_avatar_fatal_knife_stagger( "execution_avatar_fatal_knife_stagger" );
static const execution_id
execution_definition_execution_fatal_knife_stagger( "execution_fatal_knife_stagger" );
static const execution_id
execution_definition_test_exec_avatar_fatal_hold( "test_exec_avatar_fatal_hold" );

static const field_type_str_id field_fd_fire( "fd_fire" );

static const itype_id itype_execution_fatal_test_knife( "execution_fatal_test_knife" );

static const matype_id test_style_execution_fatal( "test_style_execution_fatal" );

namespace
{

struct avatar_world {
    time_point old_time = calendar::turn;
    quit_status old_quit = g->uquit;
    bool old_new_game = g->new_game;
    override_option deathcam{ "DEATHCAM", "never" };
    avatar_world() {
        g->executions().clear();
        clear_avatar();
        clear_map_without_vision( -2, 1 );
        Messages::clear_messages();
        get_globals().clear_global_values();
        get_avatar().setpos( get_map(), tripoint_bub_ms( 60, 60, 0 ) );
        get_globals().set_global_value( "exec_avatar_case", "hold" );
        g->uquit = QUIT_NO;
    }
    ~avatar_world() {
        g->executions().clear();
        clear_avatar();
        get_map().vertical_shift( 0 );
        get_avatar().setpos( get_map(), tripoint_bub_ms( 60, 60, 0 ), false );
        Messages::clear_messages();
        get_globals().remove_global_value( "exec_avatar_case" );
        get_globals().remove_global_value( "exec_avatar_prevent" );
        g->uquit = old_quit;
        g->new_game = old_new_game;
        calendar::turn = old_time;
    }
};

Creature &attacker( bool character = false )
{
    const tripoint_bub_ms position = get_avatar().pos_bub() + tripoint_rel_ms::west;
    if( !character ) {
        return spawn_test_monster( "mon_execution_fatal_attacker", position );
    }
    npc &person = spawn_npc( position.xy(), "thug" );
    item knife( itype_execution_fatal_test_knife );
    REQUIRE( person.wield( knife ) );
    person.martial_arts_data->add_martialart( test_style_execution_fatal );
    person.martial_arts_data->set_style( test_style_execution_fatal, false );
    person.set_str_base( 30 );
    person.set_dex_base( 30 );
    person.reset_stats();
    return person;
}

execution_view view( int64_t id )
{
    const auto result = g->executions().inspect( id );
    REQUIRE( result );
    return *result;
}

int64_t contact( Creature &source, bool accepted = true )
{
    avatar &target = get_avatar();
    {
        execution_combat::attack_scope root( source, target, source.as_monster() ?
                                             execution_combat::channel::monster_melee : execution_combat::channel::character_melee );
        if( Character *person = source.as_character() ) {
            const item_location weapon = person->get_wielded_item();
            root.set_weapon( weapon ? weapon.get_item() : nullptr, "test_style_execution_fatal", "tec_none" );
            root.set_critical( true );
        }
        dealt_damage_instance dealt;
        const bodypart_id bp = body_part_torso.id();
        {
            const execution_combat::primary_hit_scope primary( root );
            target.deal_melee_hit( &source, 1000, true,
                                   damage_instance( damage_pure, 1000 ), dealt, {}, &bp );
        }
        root.resolve( true, source.as_monster() ? std::nullopt : std::optional<bool>( true ), dealt );
        target.check_dead_state( &get_map() );
        if( source.as_character() && execution_fatal::pending( target ) ) {
            ma_event_context ctx;
            ctx.opponent = &target;
            ctx.weapon = itype_execution_fatal_test_knife;
            ctx.style = test_style_execution_fatal;
            ctx.technique = tec_none;
            ctx.bp = body_part_torso;
            ctx.hit = ctx.crit = true;
            ctx.damage = dealt.total_damage();
            ctx.killed = false;
            g->executions().defer_kill( target, ctx );
        }
    }
    const auto report = g->executions().last_trigger_report();
    REQUIRE( report );
    if( accepted ) {
        REQUIRE( report->instance > 0 );
    }
    return report->instance;
}

void tick( int count = 1 )
{
    for( int n = 0; n < count; ++n ) {
        calendar::turn += 1_turns;
        g->executions().process();
    }
}

std::string save_avatar()
{
    std::ostringstream text;
    JsonOut out( text );
    get_avatar().serialize( out );
    return text.str();
}

std::string save_scene()
{
    std::ostringstream text;
    JsonOut out( text );
    g->executions().serialize( out );
    return text.str();
}

void restore( const std::string &actor, const std::string &scene )
{
    g->executions().reset_for_load();
    g->executions().deserialize( json_loader::from_string( scene ) );
    get_avatar().setID( character_id(), true );
    get_avatar().deserialize( json_loader::from_string( actor ) );
    g->uquit = QUIT_NO;
    g->executions().end_restore();
}

int corpses( const tripoint_bub_ms &position )
{
    int count = 0;
    for( const item &it : get_map().i_at( position ) ) {
        count += it.is_corpse();
    }
    return count;
}

struct death_events : event_subscriber {
    using event_subscriber::notify;
    int characters = 0;
    int avatars = 0;
    int kills = 0;
    int presentations = 0;
    void notify( const cata::event &e ) override {
        characters += e.type() == event_type::character_dies;
        avatars += e.type() == event_type::avatar_dies;
        kills += e.type() == event_type::character_kills_character;
        presentations += e.type() == event_type::game_avatar_death;
    }
};
} // namespace

TEST_CASE( "avatar_fatal_execution_preserves_injury_and_defers_game_over_exactly_once",
           "[executions][fatal_execution][avatar_fatal]" )
{
    avatar_world world;
    avatar &target = get_avatar();
    Creature &source = attacker( true );
    death_events events;
    get_event_bus().subscribe( &events );
    const tripoint_bub_ms position = target.pos_bub();
    const int arm_hp = target.get_part_hp_cur( bodypart_id( "arm_l" ) );
    const int64_t id = contact( source );
    CHECK( target.is_dead_state() );
    CHECK( target.get_part_hp_cur( body_part_torso.id() ) == 0 );
    CHECK( target.get_part_hp_cur( bodypart_id( "arm_l" ) ) == arm_hp );
    CHECK( execution_fatal::pending( target ) );
    CHECK_FALSE( target.death_finalized() );
    CHECK( creature_ref::bind( target ).resolve_for_execution() == &target );
    CHECK( get_creature_tracker().creature_at( position ) == &target );
    CHECK_FALSE( g->is_game_over() );
    CHECK( g->uquit == QUIT_NO );
    CHECK( events.characters + events.avatars + events.kills == 0 );
    CHECK( corpses( position ) == 0 );
    g->executions().drain_starts();
    CHECK( view( id ).state == "running" );
    CHECK( target.activity.id() == ACT_EXECUTION );
    tick( 2 );
    CHECK_FALSE( g->is_game_over() );
    tick();
    CHECK( view( id ).outcome == "killed" );
    CHECK( target.death_finalized() );
    CHECK_FALSE( execution_fatal::pending( target ) );
    CHECK( target.activity.is_null() );
    CHECK( g->uquit == QUIT_NO ); // A native callback never opened game-over UI.
    CHECK( events.characters == 1 );
    CHECK( events.avatars == 1 );
    CHECK( events.kills == 1 );
    CHECK( get_globals().get_global_value( "exec_fatal_kills" ).dbl() == 1 );
    CHECK( corpses( position ) == 0 );
    CHECK( events.presentations == 0 );
    REQUIRE( g->is_game_over() );
    CHECK( g->uquit == QUIT_DIED );
    CHECK( corpses( position ) == 1 );
    target.die( &get_map(), &source );
    CHECK( g->is_game_over() );
    CHECK( corpses( position ) == 1 );
    CHECK( events.characters == 1 );
    CHECK( events.avatars == 1 );
    CHECK( events.kills == 1 );
    CHECK( events.presentations == 1 );
}

TEST_CASE( "avatar_fatal_execution_uses_real_npc_and_monster_melee",
           "[executions][fatal_execution][avatar_fatal][melee]" )
{
    avatar_world world;
    const bool character = GENERATE( false, true );
    Creature &source = attacker( character );
    avatar &target = get_avatar();
    target.set_all_parts_hp_cur( 1 );
    target.set_dodges_left( 0 );
    for( int n = 0; n < 20 && !target.is_dead_state(); ++n ) {
        if( monster *mon = source.as_monster() ) {
            mon->melee_attack( target, 1000.0f );
        } else {
            source.as_character()->melee_attack_abstract( target, false, tec_none );
        }
    }
    REQUIRE( execution_fatal::pending( target ) );
    CHECK_FALSE( g->is_game_over() );
    g->executions().drain_starts();
    tick( 3 );
    CHECK( target.death_finalized() );
    CHECK( get_globals().get_global_value( "exec_fatal_kills" ).dbl() == ( character ? 1 : 0 ) );
    CHECK( g->is_game_over() );
}

TEST_CASE( "avatar_fatal_execution_requires_opt_in_and_preserves_unselected_deaths",
           "[executions][fatal_execution][avatar_fatal][json]" )
{
    avatar_world world;
    REQUIRE( get_avatar().get_killer() == nullptr );
    const std::string mode = GENERATE( "no_opt_in", "zero", "unmatched" );
    get_globals().set_global_value( "exec_avatar_case", mode );
    Creature &source = attacker();
    CHECK( contact( source, false ) == 0 );
    CHECK_FALSE( execution_fatal::pending( get_avatar() ) );
    CHECK( get_avatar().is_dead_state() );
    CHECK( g->is_game_over() );
    CHECK( get_avatar().death_finalized() );
    CHECK( corpses( get_avatar().pos_bub() ) == 1 );
}

TEST_CASE( "ordinary_avatar_self_inflicted_death_keeps_native_attribution_once",
           "[executions][fatal_execution][avatar_fatal]" )
{
    avatar_world world;
    avatar &target = get_avatar();
    death_events events;
    get_event_bus().subscribe( &events );
    target.die( &get_map(), &target );
    CHECK( g->is_game_over() );
    CHECK( target.get_killer() == &target );
    CHECK( events.kills == 1 );
    CHECK( events.avatars == 1 );
    CHECK( g->is_game_over() );
    CHECK( events.kills == 1 );
    CHECK( events.avatars == 1 );
    CHECK( corpses( target.pos_bub() ) == 1 );
}

TEST_CASE( "avatar_fatal_execution_query_is_pure_and_manual_fatal_starts_stay_forbidden",
           "[executions][fatal_execution][avatar_fatal][eoc]" )
{
    avatar_world world;
    Creature &source = attacker();
    contact( source );
    dialogue d( get_talker_for( get_avatar() ), get_talker_for( source ) );
    // NOLINTNEXTLINE(cata-determinism)
    const cata_default_random_engine rng = rng_get_engine();
    const std::string scene = save_scene();
    CHECK( conditional_t( "u_death_pending" )( d ) );
    CHECK_FALSE( conditional_t( "npc_death_pending" )( d ) );
    CHECK( rng_get_engine() == rng );
    CHECK( save_scene() == scene );
    dialogue target( get_talker_for( source ), get_talker_for( get_avatar() ) );
    CHECK( g->executions().check_start( execution_definition_test_exec_avatar_fatal_hold, target ) !=
           "eligible" );
}

TEST_CASE( "avatar_fatal_execution_cancels_prior_work_and_blocks_voluntary_actions_and_body_changes",
           "[executions][fatal_execution][avatar_fatal][activity]" )
{
    avatar_world world;
    avatar &target = get_avatar();
    Creature &source = attacker();
    restore_on_out_of_scope restore_distraction( uistate.distraction_attack );
    uistate.distraction_attack = true;
    target.assign_activity( wait_activity_actor( 100_turns ) );
    target.view_offset = tripoint_rel_ms( 3, 2, 0 );
    const int64_t id = contact( source );
    g->executions().drain_starts();
    REQUIRE( view( id ).state == "running" );
    CHECK( target.activity.id() == ACT_EXECUTION );
    CHECK( target.view_offset == tripoint_rel_ms::zero );
    const tripoint_abs_ms position = target.pos_abs();
    const character_id identity = target.getID();
    npc &replacement = spawn_npc( target.pos_bub().xy() + point_rel_ms( 0, 2 ), "thug" );
    const character_id replacement_id = replacement.getID();
    target.control_npc( replacement );
    CHECK( target.getID() == identity );
    CHECK( replacement.getID() == replacement_id );
    target.set_moves( 1000 );
    CHECK_FALSE( avatar_action::move( target, get_map(), tripoint_rel_ms::north ) );
    CHECK_FALSE( target.melee_attack_abstract( source, false, tec_none ) );
    CHECK( target.dodge_roll() == 0 );
    target.process_activity();
    CHECK( target.get_moves() <= 0 );
    CHECK( target.pos_abs() == position );
    CHECK_FALSE( target.is_blind() );
    CHECK_FALSE( target.is_deaf() );
    CHECK( target.can_hear( target.pos_bub(), 0 ) );
    target.cancel_activity();
    g->executions().drain_starts();
    CHECK( target.death_finalized() );
    CHECK( view( id ).reason == "player_cancelled" );
}

TEST_CASE( "avatar_fatal_execution_native_turn_processes_passive_effects_once",
           "[executions][fatal_execution][avatar_fatal][game_loop]" )
{
    avatar_world world;
    avatar &target = get_avatar();
    Creature &source = attacker();
    source.set_moves( -10000 );
    contact( source );
    g->executions().drain_starts();
    target.add_effect( effect_stunned, 5_turns );
    const time_duration before = target.get_effect( effect_stunned ).get_duration();
    calendar::turn += 1_turns;
    g->executions().process();
    CHECK( target.get_effect( effect_stunned ).get_duration() == before );
    g->new_game = true;
    CHECK_FALSE( g->do_turn() );
    CHECK( execution_fatal::pending( target ) );
    CHECK( target.get_effect( effect_stunned ).get_duration() == before - 1_turns );
    CHECK( target.get_moves() <= 0 );
}

TEST_CASE( "avatar_fatal_execution_honors_every_native_prevention_stage_without_rewards",
           "[executions][fatal_execution][avatar_fatal][prevention]" )
{
    avatar_world world;
    const std::string stage = GENERATE( "early", "event", "late" );
    const bool scene = GENERATE( false, true );
    if( !scene ) {
        get_globals().set_global_value( "exec_avatar_case", "unmatched" );
    }
    get_globals().set_global_value( "exec_avatar_prevent", stage );
    Creature &source = attacker( true );
    death_events events;
    get_event_bus().subscribe( &events );
    struct prevention : event_subscriber {
        using event_subscriber::notify;
        bool active = false;
        void notify( const cata::event &e ) override {
            if( active && e.type() == event_type::character_dies ) {
                get_avatar().prevent_death();
                get_avatar().assign_activity( wait_activity_actor( 10_turns ) );
            }
        }
    } callback;
    callback.active = stage == "event";
    get_event_bus().subscribe( &callback );
    const int64_t id = contact( source, scene );
    if( scene ) {
        g->executions().drain_starts();
        tick( 3 );
        CHECK( view( id ).outcome == "rescued" );
    }
    CHECK_FALSE( g->is_game_over() );
    CHECK_FALSE( get_avatar().is_dead_state() );
    CHECK_FALSE( get_avatar().death_finalized() );
    CHECK_FALSE( execution_fatal::pending( get_avatar() ) );
    CHECK( get_avatar().get_killer() == nullptr );
    CHECK( events.characters == ( stage == "early" ? 0 : 1 ) );
    CHECK( events.avatars == 0 );
    CHECK( events.kills == 0 );
    CHECK( corpses( get_avatar().pos_bub() ) == 0 );
    CHECK( get_globals().get_global_value( "exec_fatal_kills" ).dbl() == 0 );
    if( stage == "event" ) {
        CHECK( get_avatar().activity.id() == ACT_WAIT );
    }
    if( stage != "event" ) {
        CHECK( get_globals().get_global_value( "exec_avatar_preventions" ).dbl() == 1 );
    }
    callback.active = false;
    get_globals().remove_global_value( "exec_avatar_prevent" );
    get_avatar().set_all_parts_hp_cur( 0 );
    get_avatar().check_dead_state( &get_map() );
    CHECK( g->is_game_over() );
    CHECK( get_avatar().get_killer() == nullptr );
    CHECK( events.kills == 0 );
}

TEST_CASE( "avatar_fatal_execution_remains_exposed_to_native_environmental_damage",
           "[executions][fatal_execution][avatar_fatal][game_loop]" )
{
    avatar_world world;
    avatar &target = get_avatar();
    Creature &source = attacker();
    source.set_moves( -10000 );
    contact( source );
    g->executions().drain_starts();
    // Apply it through the native turn below, rather than add_field's immediate player hit.
    get_map().add_field( target.pos_bub(), field_fd_fire.id(), 3, 0_turns, false );
    calendar::turn += 1_turns;
    g->new_game = true;
    CHECK_FALSE( g->do_turn() );
    CHECK( target.death_finalized() );
    CHECK_FALSE( execution_fatal::pending( target ) );
    CHECK( g->uquit == QUIT_NO );
    CHECK( g->is_game_over() );
    CHECK( corpses( target.pos_bub() ) == 1 );
}

TEST_CASE( "avatar_fatal_finalizer_blocks_reentry_and_partial_saves_before_completion",
           "[executions][fatal_execution][avatar_fatal][prevention]" )
{
    avatar_world world;
    Creature &source = attacker( true );
    struct reentry : event_subscriber {
        using event_subscriber::notify;
        int calls = 0;
        bool save_refused = false;
        bool serialization_refused = false;
        bool ui_deferred = false;
        bool presentation_guarded = false;
        int presentations = 0;
        void notify( const cata::event &e ) override {
            if( e.type() == event_type::game_avatar_death ) {
                ++presentations;
                presentation_guarded = !g->is_game_over() && !g->save();
                get_avatar().prevent_death();
                return;
            }
            if( e.type() != event_type::character_dies ) {
                return;
            }
            ++calls;
            if( calls == 1 ) {
                get_avatar().die( &get_map(), nullptr );
                get_avatar().finalize_death( nullptr );
                ui_deferred = !g->is_game_over() && g->uquit == QUIT_NO;
                save_refused = !g->save();
                try {
                    save_avatar();
                } catch( const JsonError & ) {
                    serialization_refused = true;
                }
            }
        }
    } callback;
    get_event_bus().subscribe( &callback );
    contact( source );
    g->executions().drain_starts();
    tick( 3 );
    CHECK( callback.calls == 1 );
    CHECK( callback.ui_deferred );
    CHECK( callback.save_refused );
    CHECK( callback.serialization_refused );
    CHECK( get_avatar().death_finalized() );
    CHECK( get_globals().get_global_value( "exec_fatal_kills" ).dbl() == 1 );
    CHECK( g->is_game_over() );
    CHECK( callback.presentations == 1 );
    CHECK( callback.presentation_guarded );
    CHECK( get_avatar().death_finalized() );
}

TEST_CASE( "genuine_health_restoration_rescues_the_avatar_and_clears_attribution",
           "[executions][fatal_execution][avatar_fatal][healing]" )
{
    avatar_world world;
    Creature &source = attacker( true );
    death_events events;
    get_event_bus().subscribe( &events );
    const int64_t id = contact( source );
    g->executions().drain_starts();
    get_avatar().set_all_parts_hp_to_max();
    tick();
    CHECK( view( id ).outcome == "rescued" );
    CHECK_FALSE( get_avatar().death_finalized() );
    CHECK_FALSE( g->is_game_over() );
    CHECK( get_avatar().activity.is_null() );
    CHECK( get_avatar().get_killer() == nullptr );
    CHECK( events.characters + events.avatars + events.kills == 0 );
}

TEST_CASE( "avatar_fatal_execution_emergencies_settle_without_opening_death_ui_in_callbacks",
           "[executions][fatal_execution][avatar_fatal][lifecycle]" )
{
    avatar_world world;
    const std::string action = GENERATE( "cancel", "clear", "timeout", "damage", "die", "motion",
                                         "knockback", "fling", "source_removed", "world_transition", "travel", "skip" );
    CAPTURE( action );
    if( action == "timeout" ) {
        get_globals().set_global_value( "exec_avatar_case", "timeout" );
    }
    Creature &source = attacker();
    death_events events;
    get_event_bus().subscribe( &events );
    const int64_t id = contact( source );
    g->executions().drain_starts();
    if( action == "cancel" ) {
        g->executions().cancel( id );
        g->executions().drain_starts();
    } else if( action == "clear" ) {
        g->executions().clear();
    } else if( action == "timeout" ) {
        tick( 3 );
    } else if( action == "damage" ) {
        get_avatar().apply_damage( nullptr, body_part_torso.id(), 1 );
    } else if( action == "die" ) {
        get_avatar().die( &get_map(), nullptr );
    } else if( action == "motion" ) {
        get_avatar().setpos( get_map(), get_avatar().pos_bub() + tripoint_rel_ms::north );
    } else if( action == "knockback" ) {
        g->knockback( source.pos_bub(), get_avatar().pos_bub(), 3, 0, 1 );
    } else if( action == "fling" ) {
        CHECK_FALSE( g->fling_creature( &get_avatar(), 0_degrees, 20, false ) );
    } else if( action == "source_removed" ) {
        get_creature_tracker().remove( *source.as_monster() );
    } else if( action == "skip" ) {
        g->executions().settle_fatal( get_avatar(), "player_cancelled" );
    } else if( action == "travel" ) {
        CHECK_FALSE( g->travel_to_dimension( dimension_world_scarlet, {}, {}, std::nullopt ) );
    } else {
        CHECK( g->executions().prepare_world_transition() );
    }
    CHECK_FALSE( execution_fatal::pending( get_avatar() ) );
    CHECK( get_avatar().death_finalized() );
    CHECK( g->uquit == QUIT_NO );
    CHECK( events.characters == 1 );
    CHECK( events.avatars == 1 );
    CHECK( corpses( get_avatar().pos_bub() ) == 0 );
    CHECK( g->is_game_over() );
    CHECK( corpses( get_avatar().pos_bub() ) == 1 );
}

TEST_CASE( "avatar_fatal_execution_resumes_saved_control_and_does_not_replay_start_or_death",
           "[executions][fatal_execution][avatar_fatal][save]" )
{
    avatar_world world;
    Creature &source = attacker( true );
    death_events events;
    get_event_bus().subscribe( &events );
    const int64_t id = contact( source );
    g->executions().drain_starts();
    tick();
    const std::string actor = save_avatar();
    const std::string scene = save_scene();
    restore( actor, scene );
    CHECK( execution_fatal::pending( get_avatar() ) );
    CHECK_FALSE( g->is_game_over() );
    CHECK( get_avatar().activity.id() == ACT_EXECUTION );
    CHECK( get_globals().get_global_value( "exec_fatal_starts" ).dbl() == 1 );
    tick( 2 );
    CHECK( view( id ).outcome == "killed" );
    CHECK( get_avatar().activity.is_null() );
    CHECK( events.characters == 1 );
    CHECK( events.avatars == 1 );
    CHECK( events.kills == 1 );
    CHECK( g->is_game_over() );
    const int count = corpses( get_avatar().pos_bub() );
    REQUIRE( count == 1 );
    // Even an explicit final-state checkpoint cannot replay death events or corpse creation.
    restore( save_avatar(), save_scene() );
    Messages::clear_messages();
    CHECK( get_avatar().death_finalized() );
    CHECK( g->is_game_over() );
    add_msg( "Finalized players cannot receive new messages" );
    CHECK( Messages::size() == 0 );
    CHECK( corpses( get_avatar().pos_bub() ) == count );
    CHECK( events.characters == 1 );
    CHECK( events.avatars == 1 );
    CHECK( events.kills == 1 );
    CHECK( events.presentations == 1 );
}

TEST_CASE( "avatar_fatal_restore_settles_missing_expired_or_orphaned_owners",
           "[executions][fatal_execution][avatar_fatal][save]" )
{
    avatar_world world;
    const std::string invalidation = GENERATE( "missing", "expired", "orphan" );
    Creature &source = attacker();
    contact( source );
    g->executions().drain_starts();
    const std::string actor = save_avatar();
    std::string scene = save_scene();
    if( invalidation == "missing" ) {
        const std::string name = "test_exec_avatar_fatal_hold";
        const size_t at = scene.find( name );
        REQUIRE( at != std::string::npos );
        scene.replace( at, name.size(), "missing_avatar_fatal_definition" );
    } else if( invalidation == "expired" ) {
        calendar::turn += 31_turns;
    } else {
        scene = R"({"next_id":1,"instances":[]})";
    }
    restore( actor, scene );
    g->executions().process();
    CHECK( g->is_game_over() );
    CHECK( get_avatar().death_finalized() );
    CHECK_FALSE( execution_fatal::pending( get_avatar() ) );
    CHECK( corpses( get_avatar().pos_bub() ) == 1 );
}

TEST_CASE( "avatar_fatal_execution_hands_off_to_deathcam_without_replaying_events",
           "[executions][fatal_execution][avatar_fatal][deathcam]" )
{
    avatar_world world;
    override_option watch( "DEATHCAM", "always" );
    Creature &source = attacker();
    death_events events;
    get_event_bus().subscribe( &events );
    contact( source );
    g->executions().drain_starts();
    tick( 3 );
    CHECK_FALSE( g->is_game_over() );
    CHECK( g->uquit == QUIT_WATCH );
    CHECK_FALSE( g->is_game_over() );
    CHECK( corpses( get_avatar().pos_bub() ) == 0 );
    CHECK( events.characters == 1 );
    CHECK( events.avatars == 1 );
    CHECK( events.presentations == 1 );
    restore( save_avatar(), save_scene() );
    Messages::clear_messages();
    CHECK_FALSE( g->is_game_over() );
    add_msg( "Restored deathcam cannot receive new messages" );
    CHECK( Messages::size() == 0 );
    CHECK( g->uquit == QUIT_WATCH );
    CHECK( events.presentations == 1 );
    g->uquit = QUIT_DIED;
    CHECK( g->is_game_over() );
    CHECK( g->is_game_over() );
    CHECK( corpses( get_avatar().pos_bub() ) == 1 );
    CHECK( events.characters == 1 );
    CHECK( events.avatars == 1 );
    CHECK( events.presentations == 1 );
}

TEST_CASE( "avatar_fatal_checked_movement_and_landing_keep_the_body_and_map_together",
           "[executions][fatal_execution][avatar_fatal][map]" )
{
    avatar_world world;
    const bool fall = GENERATE( false, true );
    get_globals().set_global_value( "exec_avatar_case", fall ? "fall" : "walk" );
    avatar &target = get_avatar();
    Creature &source = attacker();
    const tripoint_bub_ms origin = target.pos_bub();
    if( fall ) {
        get_map().ter_set( origin + tripoint_rel_ms::east, ter_id( "t_open_air" ) );
        get_map().ter_set( origin + tripoint_rel_ms( 1, 0, -1 ), ter_id( "t_floor" ) );
    }
    const int64_t id = contact( source );
    g->executions().drain_starts();
    tick();
    if( fall ) {
        CHECK( target.pos_abs().z() == -1 );
        CHECK( get_map().get_abs_sub().z() == -1 );
        CHECK( target.death_finalized() );
        CHECK( view( id ).outcome == "killed" );
    } else {
        CHECK( target.pos_bub() == origin + tripoint_rel_ms::east );
        CHECK( execution_fatal::pending( target ) );
        tick( 2 );
        CHECK( target.pos_bub() == origin + tripoint_rel_ms( 2, 0, 0 ) );
        CHECK( target.death_finalized() );
    }
    CHECK( g->is_game_over() );
    CHECK( corpses( target.pos_bub() ) == 1 );
    if( fall ) {
        get_map().vertical_shift( 0 );
    }
}

TEST_CASE( "avatar_fatal_opt_in_requires_the_fatal_definition_contract",
           "[executions][fatal_execution][avatar_fatal][json]" )
{
    execution_definition definition;
    CHECK_THROWS_AS( definition.load( json_loader::from_string(
                                          R"({"name":"Invalid player scene","allow_avatar_fatal_target":true,"start":"done","steps":{"done":{"end":"done"}}})" ),
                                      "avatar_fatal_tests" ), JsonError );
    REQUIRE( execution_definition_execution_avatar_fatal_knife_stagger.is_valid() );
    CHECK( execution_definition_execution_avatar_fatal_knife_stagger->allow_avatar_fatal_target );
    CHECK_FALSE( execution_definition_execution_fatal_knife_stagger->allow_avatar_fatal_target );
}

TEST_CASE( "avatar_death_state_loads_legacy_records_and_rejects_inconsistent_flags",
           "[executions][fatal_execution][avatar_fatal][save]" )
{
    avatar_world world;
    std::string legacy = save_avatar();
    for( const std::string name : {
             "death_committed", "death_finalized", "corpse_placed",
             "death_presented", "deathcam_watching"
         } ) {
        const size_t at = legacy.find( "\"" + name + "\"" );
        REQUIRE( at != std::string::npos );
        const size_t end = legacy.find( ',', at );
        REQUIRE( end != std::string::npos );
        legacy.erase( at, end - at + 1 );
    }
    avatar restored;
    restored.deserialize( json_loader::from_string( legacy ) );
    CHECK_FALSE( restored.death_finalized() );
    CHECK_FALSE( restored.death_finalizing() );

    std::string invalid = save_avatar();
    const std::string mode = GENERATE( "finalized_without_commit", "corpse_before_death",
                                       "watching_without_presentation" );
    const auto enable = [&]( const std::string & name ) {
        const size_t at = invalid.find( "\"" + name + "\"" );
        REQUIRE( at != std::string::npos );
        const size_t colon = invalid.find( ':', at );
        const size_t end = invalid.find( ',', colon );
        REQUIRE( colon < end );
        invalid.replace( colon + 1, end - colon - 1, "true" );
    };
    if( mode == "finalized_without_commit" ) {
        enable( "death_finalized" );
    } else if( mode == "corpse_before_death" ) {
        enable( "corpse_placed" );
    } else {
        enable( "death_committed" );
        enable( "death_finalized" );
        enable( "deathcam_watching" );
    }
    avatar rejected;
    CHECK_THROWS_AS( rejected.deserialize( json_loader::from_string( invalid ) ), JsonError );
}
