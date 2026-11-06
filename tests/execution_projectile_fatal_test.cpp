#include <sstream>
#include <stdexcept>
#include <string>

#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "creature_ref.h"
#include "creature_tracker.h"
#include "damage.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
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
#include "messages.h"
#include "monster.h"
#include "npc.h"
#include "npc_turn_helpers.h"
#include "options_helpers.h"
#include "player_helpers.h"
#include "projectile.h"
#include "type_id.h"

static const damage_type_id damage_pure( "pure" );

namespace
{
struct fatal_projectile_world {
    time_point old_time = calendar::turn;
    quit_status old_quit = g->uquit;
    override_option animation{ "ANIMATION_PROJECTILES", "false" };
    override_option deathcam{ "DEATHCAM", "never" };
    fatal_projectile_world() {
        g->executions().clear();
        clear_avatar();
        clear_map_without_vision( -2, 1 );
        Messages::clear_messages();
        get_globals().clear_global_values();
        get_avatar().setpos( get_map(), tripoint_bub_ms( 60, 60, 0 ) );
        // Shooters must see their targets: at night an NPC with a bow scatters and can miss.
        set_time_to_day();
        g->uquit = QUIT_NO;
        get_globals().set_global_value( "exec_projectile_fatal_case", "hold" );
    }
    ~fatal_projectile_world() {
        g->executions().clear();
        clear_avatar();
        get_map().vertical_shift( 0 );
        get_avatar().setpos( get_map(), tripoint_bub_ms( 60, 60, 0 ), false );
        Messages::clear_messages();
        get_globals().remove_global_value( "exec_projectile_fatal_case" );
        g->uquit = old_quit;
        calendar::turn = old_time;
    }
};

item &arm( Character &source, const std::string &kind = "gun" )
{
    if( source.is_npc() ) {
        // Like the avatar: no random traits or worn gear (encumbrance scatters a bow shot).
        // clear_character() also moves it, so it goes back to its spot.
        const tripoint_bub_ms where = source.pos_bub();
        clear_character( source );
        source.setpos( get_map(), where );
    }
    source.set_str_base( 40 );
    source.set_dex_base( 20 );
    source.reset_stats();
    for( const std::string skill : {
             "gun", "pistol", "shotgun", "archery"
         } ) {
        source.set_skill_level( skill_id( skill ), 10 );
    }
    arm_shooter( source, itype_id( "execution_projectile_fatal_test_" + kind ) );
    REQUIRE( source.get_wielded_item() );
    return *source.get_wielded_item();
}

Creature &victim( const std::string &kind, int hp = 1 )
{
    if( kind == "avatar" ) {
        get_avatar().set_all_parts_hp_cur( hp );
        return get_avatar();
    }
    const tripoint_bub_ms position = get_avatar().pos_bub() + tripoint_rel_ms( 2, 0, 0 );
    if( kind == "npc" ) {
        npc &who = spawn_npc( position.xy(), "thug" );
        who.clear_worn();
        who.set_all_parts_hp_cur( hp );
        return who;
    }
    monster &who = spawn_test_monster( "mon_execution_trigger_test", position );
    who.set_hp( hp );
    return who;
}

execution_view view( int64_t id )
{
    const auto result = g->executions().inspect( id );
    REQUIRE( result );
    return *result;
}

int64_t pending_id()
{
    const auto result = g->executions().last_trigger_report();
    REQUIRE( result );
    REQUIRE( result->instance != 0 );
    return result->instance;
}

// Fixed torso contacts use the actual native damage/HP-write producer and firing guards.
void hit( Character &source, Creature &target, execution_combat::projectile_fire_scope &firing,
          int amount = 4 )
{
    dealt_projectile_attack round{};
    round.proj.speed = 1000;
    firing.begin_round( *source.get_wielded_item(), round );
    const execution_combat::projectile_flight_scope flight( round, &get_map(), &source,
            source.pos_bub() );
    const execution_combat::projectile_target_scope permit( target );
    execution_combat::projectile_hit_scope contact( target, &source, round, &get_map(), true );
    dealt_damage_instance dealt;
    {
        const execution_combat::direct_damage_scope capture = contact.capture_damage();
        dealt = target.deal_damage( &source, bodypart_id( "torso" ),
                                    damage_instance( damage_pure, amount ) );
    }
    dealt.bp_hit = bodypart_id( "torso" );
    contact.resolve( dealt );
    target.check_dead_state( &get_map() );
}

int64_t contact( Character &source, Creature &target )
{
    {
        execution_combat::projectile_fire_scope firing( source, get_map(), *source.get_wielded_item() );
        hit( source, target, firing );
    }
    return pending_id();
}

void tick( int count = 1 )
{
    for( int n = 0; n < count; ++n ) {
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

struct death_events : event_subscriber {
    using event_subscriber::notify;
    int kills = 0;
    int avatar_deaths = 0;
    void notify( const cata::event &event ) override {
        kills += event.type() == event_type::character_kills_character ||
                 event.type() == event_type::character_kills_monster;
        avatar_deaths += event.type() == event_type::avatar_dies;
    }
};
} // namespace

TEST_CASE( "fatal_projectile_reserves_real_injury_and_settles_each_actor_kind_once",
           "[executions][fatal_projectile]" )
{
    fatal_projectile_world world;
    const std::string kind = GENERATE( "monster", "npc", "avatar" );
    Character *source = &get_avatar();
    if( kind == "avatar" ) {
        source = &spawn_npc( get_avatar().pos_bub().xy() + point_rel_ms( -2, 0 ), "thug" );
    }
    arm( *source );
    Creature &target = victim( kind );
    const tripoint_bub_ms position = target.pos_bub();
    death_events events;
    get_event_bus().subscribe( &events );
    const int64_t id = contact( *source, target );
    CHECK( target.is_dead_state() );
    CHECK( execution_fatal::pending( target ) );
    CHECK_FALSE( execution_fatal::finalized( target ) );
    CHECK( get_creature_tracker().creature_at( position ) == &target );
    CHECK( view( id ).context.at( "attack_channel" ) == "projectile" );
    CHECK( view( id ).context.at( "attack_event" ) == "fatal_hit" );
    CHECK( view( id ).context.at( "hp_loss" ).dbl() == 1 );
    CHECK( view( id ).context.at( "damage_pure" ).dbl() == 4 );
    CHECK( view( id ).context.at( "bp" ) == "torso" );
    CHECK( events.kills == 0 );
    CHECK( corpses( position ) == 0 );
    g->executions().drain_starts();
    CHECK( get_globals().get_global_value( "exec_fatal_starts" ).dbl() == 1 );
    tick( 3 );
    CHECK_FALSE( execution_fatal::pending( target ) );
    CHECK( execution_fatal::finalized( target ) );
    CHECK( view( id ).outcome == "killed" );
    CHECK( events.kills == 1 );
    if( kind == "avatar" ) {
        CHECK( g->uquit == QUIT_NO );
        CHECK( g->is_game_over() );
        CHECK( events.avatar_deaths == 1 );
    }
    CHECK( corpses( position ) == 1 );
    target.die( &get_map(), source );
    CHECK( corpses( position ) == 1 );
    CHECK( events.kills == 1 );
}

TEST_CASE( "fatal_projectile_native_guns_and_bows_complete_firing_before_start",
           "[executions][fatal_projectile]" )
{
    fatal_projectile_world world;
    const bool npc_source = GENERATE( false, true );
    const std::string weapon = GENERATE( "gun", "bow" );
    Character &source = npc_source ? static_cast<Character &>( spawn_npc(
                            get_avatar().pos_bub().xy() + point_rel_ms( 0, -3 ), "thug" ) ) :
                        static_cast<Character &>( get_avatar() );
    item &gun = arm( source, weapon );
    monster &target = spawn_test_monster( "mon_execution_trigger_test",
                                          source.pos_bub() + tripoint_rel_ms( 2, 0, 0 ) );
    // Native bleeding applies its initial point before the primary HP commit.
    // Keep the victim alive through that side effect so the actual shot is lethal.
    target.set_hp( 5 );
    const std::string ammunition = gun.ammo_current().str();
    const int before = gun.ammo_remaining();
    struct hp_observer : event_subscriber {
        using event_subscriber::notify;
        std::string diagnostic;
        void notify( const cata::event &event ) override {
            if( event.type() == event_type::monster_takes_damage ) {
                diagnostic += " / damage " + std::to_string( event.get<int>( "damage" ) );
                if( const auto report = g->executions().last_trigger_report() ) {
                    diagnostic += " / HP report " + report->reason + " / " + report->selected;
                }
            }
        }
    } hp_event;
    get_event_bus().subscribe( &hp_event );
    source.recoil = 0;
    REQUIRE( source.fire_gun( get_map(), target.pos_bub(), 1, gun, {} ) == 1 );
    CAPTURE( npc_source, weapon, target.get_hp(), target.death_finalized() );
    std::string native_diagnostic;
    if( const auto report = g->executions().last_trigger_report() ) {
        native_diagnostic = report->reason + " / " + report->selected;
        if( report->instance ) {
            const execution_view diagnostic = view( report->instance );
            native_diagnostic += " / " + diagnostic.state + " / " + diagnostic.reason;
        }
    }
    INFO( native_diagnostic );
    INFO( hp_event.diagnostic );
    REQUIRE( execution_fatal::pending( target ) );
    const int64_t id = pending_id();
    CHECK( view( id ).state == "pending" );
    CHECK( view( id ).context.at( "ammunition" ) == ammunition );
    CHECK( gun.ammo_remaining() == before - gun.ammo_required() );
    CHECK( get_globals().get_global_value( "exec_fatal_starts" ).dbl() == 0 );
    g->executions().drain_starts();
    CHECK( view( id ).state == "running" );
}

TEST_CASE( "fatal_projectile_later_lethal_round_uses_its_own_packet",
           "[executions][fatal_projectile]" )
{
    fatal_projectile_world world;
    avatar &source = get_avatar();
    arm( source );
    Creature &target = victim( "monster", 6 );
    int64_t id = 0;
    {
        execution_combat::projectile_fire_scope firing( source, get_map(), *source.get_wielded_item() );
        hit( source, target, firing, 2 );
        CHECK_FALSE( execution_fatal::pending( target ) );
        hit( source, target, firing, 7 );
        REQUIRE( execution_fatal::pending( target ) );
        id = pending_id();
        CHECK( view( id ).context.at( "hp_loss" ).dbl() == 4 );
        CHECK( view( id ).context.at( "damage_pure" ).dbl() == 7 );
        g->executions().drain_starts();
        CHECK( view( id ).state == "pending" );
    }
    g->executions().drain_starts();
    CHECK( view( id ).state == "running" );
    CHECK( get_globals().get_global_value( "exec_fatal_starts" ).dbl() == 1 );
}

TEST_CASE( "fatal_projectile_subsequent_harm_settles_without_protecting_the_victim",
           "[executions][fatal_projectile]" )
{
    fatal_projectile_world world;
    const bool native_pellets = GENERATE( false, true );
    avatar &source = get_avatar();
    item &gun = arm( source, native_pellets ? "shotgun" : "gun" );
    Creature &target = victim( "monster" );
    death_events events;
    get_event_bus().subscribe( &events );
    if( native_pellets ) {
        source.recoil = 0;
        REQUIRE( source.fire_gun( get_map(), target.pos_bub(), 1, gun, {} ) == 1 );
    } else {
        execution_combat::projectile_fire_scope firing( source, get_map(), gun );
        hit( source, target, firing );
        REQUIRE( execution_fatal::pending( target ) );
        hit( source, target, firing );
    }
    CHECK_FALSE( execution_fatal::pending( target ) );
    CHECK( execution_fatal::finalized( target ) );
    CHECK( events.kills == 1 );
    CHECK( corpses( target.pos_bub() ) == 1 );
    CHECK( get_globals().get_global_value( "exec_fatal_starts" ).dbl() == 0 );
}

TEST_CASE( "fatal_projectile_zero_unmatched_and_player_opt_in_keep_ordinary_death",
           "[executions][fatal_projectile]" )
{
    fatal_projectile_world world;
    const std::string mode = GENERATE( "zero", "unmatched", "no_avatar" );
    get_globals().set_global_value( "exec_projectile_fatal_case", mode );
    Character *source = &get_avatar();
    if( mode == "no_avatar" ) {
        source = &spawn_npc( get_avatar().pos_bub().xy() + point_rel_ms( -2, 0 ), "thug" );
    }
    arm( *source );
    Creature &target = victim( mode == "no_avatar" ? "avatar" : "monster" );
    {
        execution_combat::projectile_fire_scope firing( *source, get_map(), *source->get_wielded_item() );
        hit( *source, target, firing );
    }
    CHECK_FALSE( execution_fatal::pending( target ) );
    if( mode == "no_avatar" ) {
        CHECK( g->is_game_over() );
    }
    CHECK( execution_fatal::finalized( target ) );
    CHECK( corpses( target.pos_bub() ) == 1 );
}

TEST_CASE( "fatal_projectile_rescue_and_native_character_prevention_award_no_kill",
           "[executions][fatal_projectile]" )
{
    fatal_projectile_world world;
    const std::string kind = GENERATE( "monster", "npc", "avatar" );
    Character *source = &get_avatar();
    if( kind == "avatar" ) {
        source = &spawn_npc( get_avatar().pos_bub().xy() + point_rel_ms( -2, 0 ), "thug" );
    }
    arm( *source );
    Creature &target = victim( kind );
    struct prevention : event_subscriber {
        using event_subscriber::notify;
        Character *target = nullptr;
        void notify( const cata::event &event ) override {
            if( target && event.type() == event_type::character_dies ) {
                target->prevent_death();
            }
        }
    } rescue;
    death_events events;
    get_event_bus().subscribe( &events );
    rescue.target = target.as_character();
    get_event_bus().subscribe( &rescue );
    const int64_t id = contact( *source, target );
    g->executions().drain_starts();
    if( kind == "monster" ) {
        target.as_monster()->set_hp( 100 );
    }
    tick( 3 );
    CHECK( view( id ).outcome == "rescued" );
    CHECK_FALSE( target.is_dead_state() );
    CHECK_FALSE( execution_fatal::pending( target ) );
    CHECK( events.kills == 0 );
    CHECK( events.avatar_deaths == 0 );
    CHECK( corpses( target.pos_bub() ) == 0 );
}

TEST_CASE( "fatal_projectile_exception_cancellation_timeout_and_transition_settle",
           "[executions][fatal_projectile]" )
{
    fatal_projectile_world world;
    const std::string mode = GENERATE( "exception", "cancel", "timeout", "transition",
                                       "source_died" );
    if( mode == "timeout" ) {
        get_globals().set_global_value( "exec_projectile_fatal_case", "timeout" );
    }
    avatar &source = get_avatar();
    arm( source );
    Creature &target = victim( "monster" );
    int64_t id = 0;
    if( mode == "exception" ) {
        try {
            execution_combat::projectile_fire_scope firing( source, get_map(), *source.get_wielded_item() );
            hit( source, target, firing );
            id = pending_id();
            throw std::runtime_error( "Abort firing after a lethal contact" );
        } catch( const std::runtime_error & ) {
        }
    } else {
        id = contact( source, target );
        g->executions().drain_starts();
        if( mode == "cancel" ) {
            g->executions().cancel( id );
            g->executions().drain_starts();
        } else if( mode == "timeout" ) {
            tick( 2 );
        } else if( mode == "source_died" ) {
            source.set_all_parts_hp_cur( 0 );
            source.check_dead_state( &get_map() );
            tick();
        } else {
            CHECK( g->executions().prepare_world_transition() );
        }
    }
    CHECK_FALSE( execution_combat::attack_in_progress() );
    CHECK_FALSE( execution_fatal::pending( target ) );
    CHECK( execution_fatal::finalized( target ) );
    CHECK( view( id ).outcome == "killed" );
    CHECK( corpses( target.pos_bub() ) == 1 );
}

TEST_CASE( "fatal_projectile_saved_reservation_preserves_context_deadline_and_start",
           "[executions][fatal_projectile][save]" )
{
    fatal_projectile_world world;
    avatar &source = get_avatar();
    arm( source );
    Creature &target = victim( "monster" );
    const int64_t id = contact( source, target );
    g->executions().drain_starts();
    tick();
    const execution_view before = view( id );
    std::ostringstream text;
    JsonOut out( text );
    g->executions().serialize( out );
    g->executions().reset_for_load();
    g->executions().deserialize( json_loader::from_string( text.str() ) );
    g->executions().end_restore();
    CHECK( execution_fatal::pending( target ) );
    CHECK( view( id ).due == before.due );
    CHECK( view( id ).context.at( "ammunition" ) == before.context.at( "ammunition" ) );
    CHECK( view( id ).context.at( "projectile_origin" ) == before.context.at( "projectile_origin" ) );
    tick( 2 );
    CHECK( view( id ).outcome == "killed" );
    CHECK( get_globals().get_global_value( "exec_fatal_starts" ).dbl() == 1 );
    CHECK( corpses( target.pos_bub() ) == 1 );
}

TEST_CASE( "fatal_projectile_supports_checked_movement_without_fake_healing",
           "[executions][fatal_projectile][map]" )
{
    fatal_projectile_world world;
    get_globals().set_global_value( "exec_projectile_fatal_case", "walk" );
    avatar &source = get_avatar();
    arm( source );
    Creature &target = victim( "monster" );
    const tripoint_bub_ms origin = target.pos_bub();
    const int64_t id = contact( source, target );
    g->executions().drain_starts();
    tick();
    CHECK( target.pos_bub() == origin + tripoint_rel_ms::east );
    CHECK( target.is_dead_state() );
    CHECK( execution_fatal::pending( target ) );
    tick( 3 );
    CHECK( view( id ).outcome == "killed" );
    CHECK( target.pos_bub() == origin + tripoint_rel_ms( 2, 0, 0 ) );
    CHECK( corpses( target.pos_bub() ) == 1 );
}
