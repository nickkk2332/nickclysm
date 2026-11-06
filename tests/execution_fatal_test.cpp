#include <algorithm>
#include <memory>
#include <sstream>
#include <string>

#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character_martial_arts.h"
#include "condition.h"
#include "creature_ref.h"
#include "creature_tracker.h"
#include "damage.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "execution.h"
#include "execution_combat.h"
#include "execution_fatal.h"
#include "execution_runtime.h"
#include "execution_trigger.h"
#include "explosion.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "global_vars.h"
#include "incapacitation.h"
#include "item.h"
#include "item_location.h"
#include "json.h"
#include "json_loader.h"
#include "kill_tracker.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "magic_enchantment.h"
#include "martialarts.h"
#include "memory_fast.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "npc_turn_helpers.h"
#include "overmapbuffer.h"
#include "player_helpers.h"
#include "projectile.h"
#include "rng.h"
#include "talker.h"
#include "type_id.h"
#include "weakpoint.h"

namespace
{
const bodypart_str_id torso( "torso" );

struct fatal_world {
    time_point old_time = calendar::turn;
    fatal_world() {
        g->executions().clear();
        clear_avatar();
        clear_map_without_vision( -2, 1 );
        get_globals().clear_global_values();
        get_avatar().setpos( get_map(), tripoint_bub_ms( 60, 60, 0 ) );
        get_globals().set_global_value( "exec_fatal_case", "hold" );
    }
    ~fatal_world() {
        g->executions().clear();
        get_globals().remove_global_value( "exec_fatal_case" );
        clear_avatar();
        calendar::turn = old_time;
    }
};

Creature &victim( const std::string &kind )
{
    const tripoint_bub_ms pos = get_avatar().pos_bub() + tripoint_rel_ms::east;
    if( kind == "npc" ) {
        npc &who = spawn_npc( pos.xy(), "thug" );
        who.set_all_parts_hp_cur( 1 );
        who.set_dodges_left( 0 );
        return who;
    }
    monster &who = spawn_test_monster( "mon_execution_trigger_test", pos );
    who.set_hp( 1 );
    return who;
}

void equip( Character &who )
{
    item knife( itype_id( "execution_fatal_test_knife" ) );
    REQUIRE( who.wield( knife ) );
    who.martial_arts_data->add_martialart( matype_id( "test_style_execution_fatal" ) );
    who.martial_arts_data->set_style( matype_id( "test_style_execution_fatal" ), false );
    REQUIRE( who.martial_arts_data->selected_style() == matype_id( "test_style_execution_fatal" ) );
}

execution_view view( int64_t id )
{
    const auto result = g->executions().inspect( id );
    REQUIRE( result.has_value() );
    return *result;
}

int64_t pending_id()
{
    const auto report = g->executions().last_trigger_report();
    REQUIRE( report.has_value() );
    REQUIRE( report->instance > 0 );
    return report->instance;
}

/** Drive the actual primary melee/HP path with a fixed torso contact. */
int64_t contact( Creature &source, Creature &target, bool require_reservation = true )
{
    {
        execution_combat::attack_scope root( source, target, source.as_monster() ?
                                             execution_combat::channel::monster_melee : execution_combat::channel::character_melee );
        if( Character *person = source.as_character() ) {
            const item_location weapon = person->get_wielded_item();
            root.set_weapon( weapon ? weapon.get_item() : nullptr, "test_style_execution_fatal", "tec_none" );
            root.set_critical( true );
        }
        dealt_damage_instance dealt;
        const bodypart_id bp = torso.id();
        {
            const execution_combat::primary_hit_scope primary( root );
            target.deal_melee_hit( &source, 1000, true,
                                   damage_instance( damage_type_id( "pure" ), 1000 ), dealt, {}, &bp );
        }
        root.resolve( true, source.as_monster() ? std::nullopt : std::optional<bool>( true ), dealt );
        target.check_dead_state( &get_map() );
        if( source.as_character() && execution_fatal::pending( target ) ) {
            ma_event_context ctx;
            ctx.opponent = &target;
            ctx.weapon = itype_id( "execution_fatal_test_knife" );
            ctx.style = matype_id( "test_style_execution_fatal" );
            ctx.technique = matec_id( "tec_none" );
            ctx.bp = torso;
            ctx.damage = dealt.total_damage();
            ctx.hit = ctx.crit = true;
            ctx.killed = false;
            g->executions().defer_kill( target, ctx );
        }
    }
    if( require_reservation ) {
        return pending_id();
    }
    const auto report = g->executions().last_trigger_report();
    REQUIRE( report );
    return report->instance;
}

void tick( int turns = 1 )
{
    for( int n = 0; n < turns; ++n ) {
        calendar::turn += 1_turns;
        g->executions().process();
    }
}

int corpses( const tripoint_bub_ms &where )
{
    int count = 0;
    for( const item &it : get_map().i_at( where ) ) {
        count += it.is_corpse();
    }
    return count;
}

std::string save_scene()
{
    std::ostringstream text;
    JsonOut out( text );
    g->executions().serialize( out );
    return text.str();
}

std::string save_actor( const Creature &who )
{
    std::ostringstream text;
    JsonOut out( text );
    if( const monster *mon = who.as_monster() ) {
        mon->serialize( out );
    } else {
        who.as_npc()->serialize( out );
    }
    return text.str();
}

struct death_events : event_subscriber {
    using event_subscriber::notify;
    int monster_kills = 0;
    int npc_kills = 0;
    int deaths = 0;
    void notify( const cata::event &e ) override {
        monster_kills += e.type() == event_type::character_kills_monster;
        npc_kills += e.type() == event_type::character_kills_character;
        deaths += e.type() == event_type::character_dies;
    }
};
} // namespace

TEST_CASE( "bulk_health_changes_invalidate_cached_death_without_emitting_limb_events",
           "[executions][fatal_execution][health]" )
{
    fatal_world world;
    avatar &who = get_avatar();
    struct limb_events : event_subscriber {
        using event_subscriber::notify;
        int broken = 0;
        void notify( const cata::event &e ) override {
            broken += e.type() == event_type::broken_bone;
        }
    } events;
    get_event_bus().subscribe( &events );
    REQUIRE_FALSE( who.is_dead_state() );
    who.set_all_parts_hp_cur( 0 );
    CHECK( who.is_dead_state() );
    who.set_all_parts_hp_to_max();
    CHECK_FALSE( who.is_dead_state() );
    CHECK( events.broken == 0 );
}

TEST_CASE( "inherited_core_fatal_knife_executes_the_complete_graph",
           "[executions][fatal_execution][content]" )
{
    fatal_world world;
    REQUIRE( execution_id( "execution_fatal_knife_stagger" ).is_valid() );
    get_globals().set_global_value( "exec_fatal_case", "core" );
    Creature &target = victim( "monster" );
    equip( get_avatar() );
    const int64_t id = contact( get_avatar(), target );
    g->executions().drain_starts();
    tick( 12 );
    CHECK( view( id ).outcome == "killed" );
    CHECK( view( id ).reason != "fatal_timeout" );
    CHECK( execution_fatal::finalized( target ) );
    CHECK( corpses( target.pos_bub() ) == 1 );
}

TEST_CASE( "npc_already_marked_for_death_cannot_reserve_a_fatal_execution",
           "[executions][fatal_execution][npc]" )
{
    fatal_world world;
    npc &target = *victim( "npc" ).as_npc();
    equip( get_avatar() );
    target.marked_for_death = true;
    CHECK( contact( get_avatar(), target, false ) == 0 );
    CHECK_FALSE( execution_fatal::pending( target ) );
    CHECK( target.death_finalized() );
    CHECK( corpses( target.pos_bub() ) == 1 );
}

TEST_CASE( "fatal_execution_preserves_injury_occupancy_and_one_native_death",
           "[executions][fatal_execution]" )
{
    fatal_world world;
    const std::string kind = GENERATE( "monster", "npc" );
    Creature &target = victim( kind );
    const tripoint_bub_ms pos = target.pos_bub();
    equip( get_avatar() );
    death_events events;
    get_event_bus().subscribe( &events );
    const int64_t id = contact( get_avatar(), target );
    CHECK( target.is_dead_state() );
    CHECK_FALSE( execution_fatal::finalized( target ) );
    CHECK( execution_fatal::pending( target ) );
    CHECK( get_creature_tracker().creature_at( pos ) == &target );
    CHECK( creature_ref::bind( target ).resolve() == nullptr );
    CHECK( creature_ref::bind( target ).resolve_for_execution() == &target );
    CHECK( g->get_creatures_if( [&]( const Creature & who ) {
        return &who == &target;
    } ).empty() );
    g->cleanup_dead();
    CHECK( get_creature_tracker().creature_at( pos ) == &target );
    CHECK( corpses( pos ) == 0 );
    CHECK( events.monster_kills + events.npc_kills == 0 );
    g->executions().drain_starts();
    CHECK( view( id ).state == "running" );
    CHECK( g->executions().controls( target ) );
    tick( 2 );
    CHECK( execution_fatal::pending( target ) );
    tick();
    CHECK( view( id ).state == "completed" );
    CHECK( view( id ).outcome == "killed" );
    CHECK( execution_fatal::finalized( target ) );
    CHECK_FALSE( execution_fatal::pending( target ) );
    CHECK( target.get_killer() == &get_avatar() );
    CHECK( events.monster_kills + events.npc_kills == 1 );
    CHECK( corpses( pos ) == 1 );
    target.check_dead_state( &get_map() );
    target.die( &get_map(), &get_avatar() );
    CHECK( events.monster_kills + events.npc_kills == 1 );
    CHECK( corpses( pos ) == 1 );
}

TEST_CASE( "fatal_execution_is_reserved_before_damage_callbacks_but_runs_after_the_attack",
           "[executions][fatal_execution][eoc]" )
{
    fatal_world world;
    const std::string kind = GENERATE( "monster", "npc" );
    Creature &target = victim( kind );
    equip( get_avatar() );
    struct observer : event_subscriber {
        using event_subscriber::notify;
        Creature *target = nullptr;
        bool fired = false;
        bool reserved = false;
        bool stayed_pending = false;
        bool save_refused = false;
        bool transition_refused = false;
        void notify( const cata::event &e ) override {
            if( fired || ( e.type() != event_type::monster_takes_damage &&
                           e.type() != event_type::character_takes_damage ) ) {
                return;
            }
            fired = true;
            reserved = execution_fatal::pending( *target );
            transition_refused = !g->executions().prepare_world_transition();
            target->check_dead_state( &get_map() );
            g->cleanup_dead();
            g->executions().drain_starts();
            stayed_pending = view( pending_id() ).state == "pending" &&
                             !execution_fatal::finalized( *target );
            try {
                save_scene();
            } catch( const JsonError & ) {
                save_refused = true;
            }
        }
    } callback;
    callback.target = &target;
    get_event_bus().subscribe( &callback );
    const int64_t id = contact( get_avatar(), target );
    CHECK( callback.fired );
    CHECK( callback.reserved );
    CHECK( callback.stayed_pending );
    CHECK( callback.save_refused );
    CHECK( callback.transition_refused );
    CHECK( get_globals().get_global_value( "exec_fatal_starts" ).dbl() == 0 );
    g->executions().drain_starts();
    CHECK( view( id ).state == "running" );
    CHECK( get_globals().get_global_value( "exec_fatal_starts" ).dbl() == 1 );
}

TEST_CASE( "fatal_execution_uses_real_character_and_monster_melee_producers",
           "[executions][fatal_execution][melee]" )
{
    fatal_world world;
    const std::string source_kind = GENERATE( "avatar", "npc", "monster" );
    const std::string target_kind = GENERATE( "npc", "monster" );
    Creature &target = victim( target_kind );
    Creature *source = &get_avatar();
    if( source_kind == "npc" ) {
        source = &spawn_npc( get_avatar().pos_bub().xy() + point_rel_ms( 1, 1 ), "thug" );
    } else if( source_kind == "monster" ) {
        source = &spawn_test_monster( "mon_execution_fatal_attacker",
                                      get_avatar().pos_bub() + tripoint_rel_ms( 1, 1, 0 ) );
    }
    if( Character *person = source->as_character() ) {
        equip( *person );
        person->set_str_base( 30 );
        person->set_dex_base( 30 );
        person->reset_stats();
    }
    for( int n = 0; n < 20 && !target.is_dead_state(); ++n ) {
        if( monster *mon = source->as_monster() ) {
            mon->melee_attack( target, 1000.0f );
        } else {
            source->as_character()->melee_attack_abstract( target, false, matec_id( "tec_none" ) );
        }
    }
    REQUIRE( target.is_dead_state() );
    REQUIRE( execution_fatal::pending( target ) );
    const int64_t id = pending_id();
    g->executions().drain_starts();
    tick( 3 );
    CHECK( view( id ).outcome == "killed" );
    CHECK( target.get_killer() == source );
}

// An edged hit can open a bleed whose first tick lands before the hit's own HP loss. When that
// tick takes the last HP, the hit still delivered the death and must reserve the execution.
TEST_CASE( "fatal_execution_survives_the_killing_hit_opening_a_bleed",
           "[executions][fatal_execution][melee]" )
{
    struct fatal_damage : event_subscriber {
        using event_subscriber::notify;
        int damage = -1;
        void notify( const cata::event &e ) override {
            if( e.type() == event_type::monster_takes_damage && e.get<bool>( "dies" ) ) {
                damage = e.get<int>( "damage" );
            }
        }
    } observed;
    get_event_bus().subscribe( &observed );
    bool bleed_killed = false;
    // The bleed and its first tick are both random; stop once the bleed has taken the last HP.
    for( int attempt = 0; attempt < 200 && !bleed_killed; ++attempt ) {
        fatal_world world;
        Creature &target = victim( "monster" );
        avatar &you = get_avatar();
        equip( you );
        you.set_str_base( 30 );
        you.set_dex_base( 30 );
        you.reset_stats();
        observed.damage = -1;
        for( int n = 0; n < 20 && !target.is_dead_state(); ++n ) {
            you.melee_attack_abstract( target, false, matec_id( "tec_none" ) );
        }
        REQUIRE( target.is_dead_state() );
        CAPTURE( attempt, observed.damage );
        REQUIRE( execution_fatal::pending( target ) );
        // The stab itself is far stronger than one bleed tick.
        bleed_killed = observed.damage == 1;
    }
    get_event_bus().unsubscribe( &observed );
    CHECK( bleed_killed );
}

TEST_CASE( "fatal_execution_checked_movement_retains_collision_and_raw_fatal_health",
           "[executions][fatal_execution][map]" )
{
    fatal_world world;
    const std::string kind = GENERATE( "monster", "npc" );
    get_globals().set_global_value( "exec_fatal_case", "walk" );
    Creature &target = victim( kind );
    equip( get_avatar() );
    const tripoint_bub_ms origin = target.pos_bub();
    const int64_t id = contact( get_avatar(), target );
    g->executions().drain_starts();
    tick();
    CHECK( target.pos_bub() == origin + tripoint_rel_ms::east );
    CHECK( get_creature_tracker().creature_at( target.pos_bub() ) == &target );
    CHECK( target.is_dead_state() );
    CHECK_FALSE( execution_fatal::finalized( target ) );
    tick();
    CHECK( target.pos_bub() == origin + tripoint_rel_ms( 2, 0, 0 ) );
    CHECK( execution_fatal::pending( target ) );
    tick();
    CHECK( view( id ).outcome == "killed" );
    CHECK( corpses( target.pos_bub() ) == 1 );
}

TEST_CASE( "fatal_execution_finalizes_after_owned_native_landing",
           "[executions][fatal_execution][map]" )
{
    fatal_world world;
    const std::string kind = GENERATE( "monster", "npc" );
    get_globals().set_global_value( "exec_fatal_case", "fall" );
    Creature &target = victim( kind );
    target.setpos( get_map(), tripoint_bub_ms( 61, 60, 1 ), false );
    get_avatar().setpos( get_map(), tripoint_bub_ms( 60, 60, 1 ), false );
    const tripoint_bub_ms edge( 62, 60, 1 );
    const tripoint_bub_ms landing( 62, 60, 0 );
    get_map().ter_set( target.pos_bub(), ter_str_id( "t_floor" ) );
    get_map().ter_set( edge, ter_str_id( "t_open_air" ) );
    get_map().ter_set( landing, ter_str_id( "t_floor" ) );
    equip( get_avatar() );
    const int64_t id = contact( get_avatar(), target );
    g->executions().drain_starts();
    tick();
    CHECK( target.pos_bub() == landing );
    CHECK( view( id ).outcome == "killed" );
    CHECK( corpses( edge ) == 0 );
    CHECK( corpses( landing ) == 1 );
    CHECK( target.get_killer() == &get_avatar() );
}

TEST_CASE( "fatal_execution_emergency_paths_settle_the_injury",
           "[executions][fatal_execution][lifecycle]" )
{
    fatal_world world;
    const std::string kind = GENERATE( "monster", "npc" );
    const std::string emergency = GENERATE( "cancel", "clear", "direct_death", "damage",
                                            "distributed_damage", "motion", "unload", "removal", "transform" );
    Creature &target = victim( kind );
    const auto target_hold = target.as_monster() ?
                             get_creature_tracker().find_by_uid( target.as_monster()->persistent_id() ) :
                             g->shared_from( target );
    const tripoint_bub_ms origin = target.pos_bub();
    equip( get_avatar() );
    const int64_t id = contact( get_avatar(), target );
    g->executions().drain_starts();
    if( emergency == "cancel" ) {
        REQUIRE( g->executions().cancel( id ) );
        g->executions().drain_starts();
    } else if( emergency == "clear" ) {
        g->executions().clear();
    } else if( emergency == "direct_death" ) {
        target.die( &get_map(), nullptr );
    } else if( emergency == "damage" ) {
        target.apply_damage( nullptr, torso.id(), 1 );
    } else if( emergency == "distributed_damage" ) {
        if( Character *person = target.as_character() ) {
            person->hurtall( 1, nullptr );
        } else {
            target.deal_damage( nullptr, torso.id(), damage_instance( damage_type_id( "pure" ), 1 ) );
        }
    } else if( emergency == "motion" ) {
        target.setpos( get_map(), origin + tripoint_rel_ms::east );
        CHECK( target.pos_bub() == origin );
    } else if( emergency == "unload" ) {
        if( monster *mon = target.as_monster() ) {
            mon->on_unload();
        } else {
            target.as_npc()->on_unload();
        }
    } else if( emergency == "removal" ) {
        if( monster *mon = target.as_monster() ) {
            get_creature_tracker().remove( *mon );
        } else {
            g->remove_npc( target.as_npc()->getID() );
        }
    } else if( monster *mon = target.as_monster() ) {
        mon->poly( mtype_id( "mon_zombie" ) );
        CHECK( mon->type->id == mtype_id( "mon_execution_trigger_test" ) );
    } else {
        get_creature_tracker().clear_npcs();
    }
    CHECK_FALSE( execution_fatal::pending( target ) );
    CHECK( execution_fatal::finalized( target ) );
    CHECK( corpses( origin ) == 1 );
    target.check_dead_state( &get_map() );
    CHECK( corpses( origin ) == 1 );
}

TEST_CASE( "fatal_execution_timeout_settles_with_or_without_a_missed_tick",
           "[executions][fatal_execution]" )
{
    fatal_world world;
    const bool skip = GENERATE( false, true );
    get_globals().set_global_value( "exec_fatal_case", "timeout" );
    Creature &target = victim( "monster" );
    equip( get_avatar() );
    const int64_t id = contact( get_avatar(), target );
    g->executions().drain_starts();
    if( skip ) {
        calendar::turn += 10_turns;
        g->executions().process();
    } else {
        tick( 3 );
    }
    CHECK( execution_fatal::finalized( target ) );
    CHECK( view( id ).outcome == "killed" );
    CHECK_FALSE( g->executions().controls( target ) );
}

TEST_CASE( "genuine_healing_rescues_a_fatal_execution_and_clears_old_attribution",
           "[executions][fatal_execution][healing]" )
{
    fatal_world world;
    const std::string kind = GENERATE( "monster", "npc" );
    Creature &target = victim( kind );
    equip( get_avatar() );
    const int64_t id = contact( get_avatar(), target );
    g->executions().drain_starts();
    if( monster *mon = target.as_monster() ) {
        mon->heal_bp( torso.id(), 10000 );
    } else {
        // Ordinary healing cannot mend a destroyed vital part; restore real health explicitly.
        target.as_character()->set_all_parts_hp_cur( 20 );
    }
    REQUIRE_FALSE( target.is_dead_state() );
    tick();
    CHECK( view( id ).outcome == "rescued" );
    CHECK_FALSE( execution_fatal::finalized( target ) );
    CHECK_FALSE( execution_fatal::pending( target ) );
    CHECK( target.get_killer() == nullptr );
    CHECK( corpses( target.pos_bub() ) == 0 );
    CHECK( get_globals().get_global_value( "exec_fatal_kills" ).dbl() == 0 );
}

TEST_CASE( "npc_fatal_execution_preserves_native_prevention_and_guards_finalizer_reentry",
           "[executions][fatal_execution][npc]" )
{
    fatal_world world;
    npc &target = *victim( "npc" ).as_npc();
    equip( get_avatar() );
    const int64_t id = contact( get_avatar(), target );
    struct prevention : event_subscriber {
        using event_subscriber::notify;
        npc *target = nullptr;
        int calls = 0;
        void notify( const cata::event &e ) override {
            if( e.type() == event_type::character_dies ) {
                ++calls;
                if( calls < 3 ) {
                    target->die( &get_map(), nullptr );
                }
                target->prevent_death();
            }
        }
    } callback;
    callback.target = &target;
    get_event_bus().subscribe( &callback );
    g->executions().drain_starts();
    tick( 3 );
    CHECK( callback.calls == 1 );
    CHECK( view( id ).outcome == "rescued" );
    CHECK_FALSE( target.is_dead_state() );
    CHECK_FALSE( target.death_finalized() );
    CHECK( target.get_killer() == nullptr );
    CHECK( corpses( target.pos_bub() ) == 0 );
}

TEST_CASE( "fatal_execution_delays_original_style_kill_hooks_until_finalization",
           "[executions][fatal_execution][martial_arts]" )
{
    fatal_world world;
    Creature &target = victim( "monster" );
    equip( get_avatar() );
    const int64_t id = contact( get_avatar(), target );
    CHECK( get_globals().get_global_value( "exec_fatal_kills" ).dbl() == 0 );
    get_avatar().martial_arts_data->set_style( matype_id( "style_none" ), false );
    g->executions().drain_starts();
    tick( 3 );
    CHECK( view( id ).outcome == "killed" );
    CHECK( get_globals().get_global_value( "exec_fatal_kills" ).dbl() == 1 );
    CHECK( get_globals().get_global_value( "exec_fatal_killed_seen" ).dbl() == 1 );
    CHECK( get_globals().get_global_value( "exec_fatal_pending_seen" ).dbl() == 0 );
    target.check_dead_state( &get_map() );
    CHECK( get_globals().get_global_value( "exec_fatal_kills" ).dbl() == 1 );
}

TEST_CASE( "saved_fatal_execution_restores_ownership_before_cleanup_and_never_replays_start",
           "[executions][fatal_execution][save]" )
{
    fatal_world world;
    const std::string kind = GENERATE( "monster", "npc" );
    Creature &target = victim( kind );
    equip( get_avatar() );
    const int64_t id = contact( get_avatar(), target );
    g->executions().drain_starts();
    tick();
    const std::string actor_data = save_actor( target );
    const std::string scene_data = save_scene();
    g->executions().reset_for_load();
    if( monster *mon = target.as_monster() ) {
        get_creature_tracker().remove( *mon );
    } else {
        g->remove_npc( target.as_npc()->getID() );
        overmap_buffer.remove_npc( target.as_npc()->getID() );
    }
    g->executions().deserialize( json_loader::from_string( scene_data ) );
    shared_ptr_fast<Creature> restored;
    if( kind == "monster" ) {
        auto mon = make_shared_fast<monster>();
        mon->deserialize( json_loader::from_string( actor_data ) );
        REQUIRE( get_creature_tracker().add( mon ) );
        mon->on_load();
        restored = mon;
    } else {
        auto person = make_shared_fast<npc>();
        person->deserialize( json_loader::from_string( actor_data ) );
        overmap_buffer.insert_npc( person );
        g->load_npcs();
        restored = person;
    }
    g->executions().end_restore();
    REQUIRE( restored );
    CHECK( execution_fatal::pending( *restored ) );
    g->cleanup_dead();
    CHECK_FALSE( execution_fatal::finalized( *restored ) );
    tick( 2 );
    CHECK( view( id ).outcome == "killed" );
    CHECK( get_globals().get_global_value( "exec_fatal_starts" ).dbl() == 1 );
    CHECK( get_globals().get_global_value( "exec_fatal_kills" ).dbl() == 1 );
    CHECK( corpses( restored->pos_bub() ) == 1 );
}

TEST_CASE( "fatal_execution_restored_with_missing_or_expired_content_settles_normally",
           "[executions][fatal_execution][save]" )
{
    fatal_world world;
    const std::string invalidation = GENERATE( "missing", "expired" );
    Creature &target = victim( "monster" );
    equip( get_avatar() );
    const int64_t id = contact( get_avatar(), target );
    g->executions().drain_starts();
    std::string saved = save_scene();
    if( invalidation == "missing" ) {
        const size_t at = saved.find( "test_exec_fatal_hold" );
        REQUIRE( at != std::string::npos );
        saved.replace( at, std::string( "test_exec_fatal_hold" ).size(), "missing_fatal_definition" );
    } else {
        calendar::turn += 31_turns;
    }
    g->executions().deserialize( json_loader::from_string( saved ) );
    g->executions().process();
    CHECK( view( id ).outcome == "killed" );
    CHECK( execution_fatal::finalized( target ) );
    CHECK( corpses( target.pos_bub() ) == 1 );
}

TEST_CASE( "fatal_execution_rejected_roll_preserves_immediate_death",
           "[executions][fatal_execution][melee]" )
{
    fatal_world world;
    get_globals().set_global_value( "exec_fatal_case", "zero" );
    monster &target = *victim( "monster" ).as_monster();
    equip( get_avatar() );
    get_avatar().set_str_base( 30 );
    get_avatar().reset_stats();
    for( int n = 0; n < 20 && !target.is_dead_state(); ++n ) {
        get_avatar().melee_attack_abstract( target, false, matec_id( "tec_none" ) );
    }
    REQUIRE( target.is_dead_state() );
    CHECK_FALSE( execution_fatal::pending( target ) );
    CHECK( target.death_finalized() );
    CHECK( corpses( target.pos_bub() ) == 1 );
    CHECK( get_globals().get_global_value( "exec_fatal_kills" ).dbl() == 1 );
}

TEST_CASE( "fatal_execution_conditions_are_pure_and_avatar_fatal_policy_is_unsupported",
           "[executions][fatal_execution][eoc]" )
{
    fatal_world world;
    Creature &target = victim( "monster" );
    equip( get_avatar() );
    contact( get_avatar(), target );
    dialogue d( get_talker_for( get_avatar() ), get_talker_for( target ) );
    const auto random = rng_get_engine();
    const std::string saved = save_scene();
    CHECK( conditional_t( "npc_death_pending" )( d ) );
    CHECK_FALSE( conditional_t( "u_death_pending" )( d ) );
    dialogue missing( get_talker_for( target ), nullptr );
    CHECK_FALSE( conditional_t( "npc_death_pending" )( missing ) );
    CHECK( rng_get_engine() == random );
    CHECK( save_scene() == saved );
    get_avatar().set_all_parts_hp_cur( 0 );
    dialogue avatar_target( get_talker_for( target ), get_talker_for( get_avatar() ) );
    CHECK( g->executions().check_fatal_start( execution_id( "test_exec_fatal_hold" ), avatar_target ) !=
           "eligible" );
    CHECK_FALSE( execution_fatal::pending( get_avatar() ) );
}

TEST_CASE( "fatal_execution_definitions_require_bounded_target_control",
           "[executions][fatal_execution][json]" )
{
    const std::string unsafe = GENERATE( R"("max_ticks":31)", R"("control":{"target":"free"})" );
    execution_definition def;
    CHECK_THROWS_AS( def.load( json_loader::from_string(
                                   R"({"name":"Unsafe fatal scene","allow_fatal_target":true,)" + unsafe +
                                   R"(,"start":"done","steps":{"done":{"end":"done"}}})" ), "fatal_tests" ), JsonError );
}

TEST_CASE( "fatal_execution_cannot_spend_actions_or_dodge_through_scripted_dispatch",
           "[executions][fatal_execution][combat]" )
{
    fatal_world world;
    const std::string kind = GENERATE( "monster", "npc" );
    Creature &target = victim( kind );
    equip( get_avatar() );
    contact( get_avatar(), target );
    target.set_moves( 1000 );
    const auto before = rng_get_engine();
    CHECK( target.dodge_roll() == 0 );
    CHECK_FALSE( target.dodge_check( -10000.0f, true, 0.0f ) );
    if( monster *mon = target.as_monster() ) {
        CHECK_FALSE( mon->melee_attack( get_avatar(), 1000.0f ) );
        CHECK_FALSE( mon->can_act() );
    } else {
        CHECK_FALSE( target.as_character()->melee_attack_abstract( get_avatar(), false,
                     matec_id( "tec_none" ) ) );
    }
    CHECK( rng_get_engine() == before );
}

TEST_CASE( "fatal_execution_keeps_environmental_damage_active",
           "[executions][fatal_execution][map]" )
{
    fatal_world world;
    const std::string kind = GENERATE( "monster", "npc" );
    Creature &target = victim( kind );
    equip( get_avatar() );
    const int64_t id = contact( get_avatar(), target );
    g->executions().drain_starts();
    get_map().add_field( target.pos_bub(), field_type_str_id( "fd_fire" ).id(), 3 );
    tick();
    CHECK( execution_fatal::finalized( target ) );
    CHECK( view( id ).outcome == "killed" );
    CHECK_FALSE( execution_fatal::pending( target ) );
}

TEST_CASE( "fatal_execution_remains_exposed_to_projectiles_and_explosive_hazards",
           "[executions][fatal_execution][projectile][explosion]" )
{
    fatal_world world;
    const std::string kind = GENERATE( "monster", "npc" );
    const std::string hazard = GENERATE( "projectile", "shrapnel", "blast", "shockwave" );
    CAPTURE( kind, hazard );
    Creature &target = victim( kind );
    equip( get_avatar() );
    const int64_t id = contact( get_avatar(), target );
    g->executions().drain_starts();
    const tripoint_bub_ms position = target.pos_bub();
    if( hazard == "projectile" ) {
        // A guaranteed enchanted evasion must not let a defeated actor avoid the contact.
        target.enchantment_cache->add_value_add( enchant_vals::mod::RANGE_DODGE, 1.0f );
        dealt_projectile_attack attack;
        attack.proj.speed = 1000;
        attack.proj.critical_multiplier = 1.0f;
        attack.proj.impact = damage_instance( damage_type_id( "pure" ), 10 );
        target.deal_projectile_attack( &get_map(), nullptr, attack, 0.1, false, weakpoint_attack() );
    } else if( hazard == "shrapnel" ) {
        // No blast: settlement must come from a real fragment contact.
        explosion_handler::_make_explosion( &get_map(), nullptr, position,
                                            explosion_data( 100, 0, false, shrapnel_data( 100, 0.1f ) ) );
    } else if( hazard == "blast" ) {
        explosion_handler::_make_explosion( &get_map(), nullptr, position, explosion_data( 100 ) );
    } else {
        explosion_handler::shockwave( position + tripoint_rel_ms::west, 2, 5, 0, 1, true );
    }
    CHECK_FALSE( execution_fatal::pending( target ) );
    CHECK( execution_fatal::finalized( target ) );
    CHECK( view( id ).outcome == "killed" );
    CHECK( corpses( position ) == 1 );
}

TEST_CASE( "unconscious_creatures_cannot_use_enchanted_projectile_evasion",
           "[executions][fatal_execution][knockout][projectile]" )
{
    fatal_world world;
    const std::string kind = GENERATE( "avatar", "npc", "monster" );
    Creature &target = kind == "avatar" ? static_cast<Creature &>( get_avatar() ) :
                       kind == "npc" ? victim( kind ) :
                       spawn_test_monster( "mon_execution_knockout_animal", get_avatar().pos_bub() +
                                           tripoint_rel_ms::east );
    if( Character *person = target.as_character() ) {
        person->set_all_parts_hp_to_max();
    }
    REQUIRE( incapacitation::apply( target, nullptr, 20_turns, false ) == "applied" );
    target.enchantment_cache->add_value_add( enchant_vals::mod::RANGE_DODGE, 1.0f );
    dealt_projectile_attack attack;
    attack.proj.speed = 1000;
    attack.proj.critical_multiplier = 1.0f;
    attack.proj.impact = damage_instance( damage_type_id( "pure" ), 10 );
    target.deal_projectile_attack( &get_map(), nullptr, attack, 0.1, false, weakpoint_attack() );
    CHECK( attack.last_hit_critter == &target );
    CHECK( attack.dealt_dam.total_damage() > 0 );
    CHECK( incapacitation::unconscious( target ) );
    CHECK_FALSE( target.is_dead_state() );
}

TEST_CASE( "fatal_execution_source_loss_and_avatar_identity_change_never_credit_a_replacement",
           "[executions][fatal_execution][lifecycle]" )
{
    fatal_world world;
    const bool change_identity = GENERATE( false, true );
    Creature &target = victim( "monster" );
    equip( get_avatar() );
    const int64_t id = contact( get_avatar(), target );
    g->executions().drain_starts();
    const character_id original = get_avatar().getID();
    if( change_identity ) {
        get_avatar().setID( character_id( original.get_value() + 1000 ), true );
    } else {
        get_avatar().set_all_parts_hp_cur( 0 );
    }
    tick();
    CHECK( view( id ).outcome == "killed" );
    CHECK( execution_fatal::finalized( target ) );
    CHECK( target.get_killer() == ( change_identity ? nullptr : &get_avatar() ) );
    get_avatar().setID( original, true );
}

TEST_CASE( "fatal_execution_settles_before_its_source_is_removed",
           "[executions][fatal_execution][lifecycle]" )
{
    fatal_world world;
    const bool monster_source = GENERATE( false, true );
    Creature &target = victim( "monster" );
    Creature *source;
    if( monster_source ) {
        source = &spawn_test_monster( "mon_execution_fatal_attacker",
                                      get_avatar().pos_bub() + tripoint_rel_ms( 1, 1, 0 ) );
    } else {
        source = &spawn_npc( get_avatar().pos_bub().xy() + point_rel_ms( 1, 1 ), "thug" );
        equip( *source->as_character() );
    }
    const auto source_hold = g->shared_from( *source );
    REQUIRE( source_hold );
    const int64_t id = contact( *source, target );
    g->executions().drain_starts();
    if( monster *mon = source->as_monster() ) {
        get_creature_tracker().remove( *mon );
    } else {
        g->remove_npc( source->as_npc()->getID() );
    }
    CHECK( execution_fatal::finalized( target ) );
    CHECK( view( id ).outcome == "killed" );
    CHECK( target.get_killer() == source_hold.get() );
    CHECK( corpses( target.pos_bub() ) == 1 );
}

TEST_CASE( "fatal_execution_rejects_duplicate_saved_owners_without_stranding_the_victim",
           "[executions][fatal_execution][save]" )
{
    fatal_world world;
    Creature &target = victim( "monster" );
    equip( get_avatar() );
    const int64_t id = contact( get_avatar(), target );
    g->executions().drain_starts();
    std::string saved = save_scene();
    const size_t instances = saved.find( "\"instances\"" );
    REQUIRE( instances != std::string::npos );
    const size_t first = saved.find( '{', instances );
    const size_t end = saved.find_last_of( ']' );
    REQUIRE( first < end );
    std::string duplicate = saved.substr( first, end - first );
    const size_t colon = duplicate.find( ':' );
    const size_t comma = duplicate.find( ',' );
    REQUIRE( colon < comma );
    duplicate.replace( colon + 1, comma - colon - 1, std::to_string( id + 1 ) );
    saved.insert( end, "," + duplicate );
    CHECK_THROWS_AS( g->executions().deserialize( json_loader::from_string( saved ) ), JsonError );
    g->executions().clear();
    CHECK( execution_fatal::finalized( target ) );
    CHECK( corpses( target.pos_bub() ) == 1 );
}

TEST_CASE( "fatal_injury_without_a_restored_owner_uses_ordinary_cleanup",
           "[executions][fatal_execution][save]" )
{
    fatal_world world;
    Creature &target = victim( "monster" );
    const auto retained = g->shared_from( target );
    REQUIRE( retained );
    equip( get_avatar() );
    contact( get_avatar(), target );
    g->executions().reset_for_load();
    g->executions().end_restore();
    CHECK_FALSE( execution_fatal::pending( target ) );
    g->cleanup_dead();
    CHECK( execution_fatal::finalized( *retained ) );
    CHECK( corpses( retained->pos_bub() ) == 1 );
}

TEST_CASE( "fatal_execution_settles_before_an_independent_world_transition_snapshot",
           "[executions][fatal_execution][save]" )
{
    fatal_world world;
    Creature &target = victim( "monster" );
    equip( get_avatar() );
    const int64_t id = contact( get_avatar(), target );
    g->executions().drain_starts();
    CHECK( g->executions().prepare_world_transition() );
    CHECK( view( id ).outcome == "killed" );
    CHECK( execution_fatal::finalized( target ) );
    CHECK( corpses( target.pos_bub() ) == 1 );
}

TEST_CASE( "fatal_execution_deadline_also_settles_an_unactivated_attack_reservation",
           "[executions][fatal_execution][lifecycle]" )
{
    fatal_world world;
    Creature &target = victim( "monster" );
    equip( get_avatar() );
    struct clock_jump : event_subscriber {
        using event_subscriber::notify;
        Creature *target = nullptr;
        bool fired = false;
        void notify( const cata::event &e ) override {
            if( !fired && e.type() == event_type::monster_takes_damage &&
                execution_fatal::pending( *target ) ) {
                fired = true;
                calendar::turn += 31_turns;
                g->executions().process();
            }
        }
    } callback;
    callback.target = &target;
    get_event_bus().subscribe( &callback );
    const int64_t id = contact( get_avatar(), target );
    CHECK( callback.fired );
    CHECK( view( id ).reason == "fatal_timeout" );
    CHECK( execution_fatal::finalized( target ) );
    CHECK( get_globals().get_global_value( "exec_fatal_starts" ).dbl() == 0 );
    CHECK( corpses( target.pos_bub() ) == 1 );
}
