#include <algorithm>
#include <climits>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>

#include "avatar.h"
#include "ballistics.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "creature_ref.h"
#include "creature_tracker.h"
#include "damage.h"
#include "dispersion.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "execution_combat.h"
#include "execution_fatal.h"
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
#include "projectile.h"
#include "rng.h"
#include "type_id.h"

namespace
{
struct projectile_world {
    time_point old_time = calendar::turn;
    quit_status old_quit = g->uquit;
    override_option animation{ "ANIMATION_PROJECTILES", "false" };
    projectile_world() {
        g->executions().clear();
        clear_avatar();
        clear_map_without_vision( -1, 1 );
        Messages::clear_messages();
        get_globals().clear_global_values();
        get_avatar().setpos( get_map(), tripoint_bub_ms( 60, 60, 0 ) );
        // Shooters must see their targets: at night an NPC with a bow scatters and can miss.
        set_time_to_day();
        g->uquit = QUIT_NO;
        get_globals().set_global_value( "exec_projectile_case", "basic" );
    }
    ~projectile_world() {
        g->executions().clear();
        clear_avatar();
        Messages::clear_messages();
        get_globals().remove_global_value( "exec_projectile_case" );
        g->uquit = old_quit;
        calendar::turn = old_time;
    }
};

void mode( const std::string &name )
{
    get_globals().set_global_value( "exec_projectile_case", name );
}

item &arm( Character &actor, const std::string &weapon = "execution_projectile_test_gun",
           const itype_id &ammunition = itype_id::NULL_ID() )
{
    actor.set_str_base( 40 );
    actor.set_dex_base( 20 );
    actor.reset_stats();
    for( const std::string skill : {
             "gun", "pistol", "shotgun", "archery"
         } ) {
        actor.set_skill_level( skill_id( skill ), 10 );
    }
    arm_shooter( actor, itype_id( weapon ), {}, ammunition );
    REQUIRE( actor.get_wielded_item() );
    return *actor.get_wielded_item();
}

monster &victim( const tripoint_bub_ms &where )
{
    return spawn_test_monster( "mon_execution_trigger_test", where );
}

execution_trigger_report report()
{
    const auto result = g->executions().last_trigger_report();
    REQUIRE( result );
    return *result;
}

execution_view view( int64_t id )
{
    const auto result = g->executions().inspect( id );
    REQUIRE( result );
    return *result;
}

int fire( Character &actor, Creature &target, int rounds = 1 )
{
    actor.recoil = 0;
    actor.set_stamina( actor.get_stamina_max() );
    return actor.fire_gun( get_map(), target.pos_bub(), rounds, *actor.get_wielded_item(), {} );
}

dealt_projectile_attack shot( int damage = 4 )
{
    dealt_projectile_attack result{};
    result.proj.speed = 1000;
    result.proj.range = 60;
    result.proj.critical_multiplier = 2;
    result.proj.impact = damage_instance( damage_type_id( "pure" ), damage );
    result.proj.proj_effects.insert( ammo_effect_str_id( "NO_DAMAGE_SCALING" ) );
    return result;
}

// Precise native impact fixtures exercise producer boundaries without ballistic scatter.
void impact( Character &actor, Creature &target, dealt_projectile_attack &round )
{
    const execution_combat::projectile_target_scope target_scope( target );
    target.deal_projectile_attack( &get_map(), &actor, round, 0, false );
}

void scoped_impact( Character &actor, Creature &target, dealt_projectile_attack &round )
{
    execution_combat::projectile_fire_scope firing( actor, get_map(), *actor.get_wielded_item() );
    firing.begin_round( *actor.get_wielded_item(), round );
    const execution_combat::projectile_flight_scope flight( round, &get_map(), &actor,
            actor.pos_bub() );
    impact( actor, target, round );
}

struct shot_events : event_subscriber {
    using event_subscriber::notify;
    std::function<void( const cata::event & )> action;
    void notify( const cata::event &event ) override {
        if( action ) {
            action( event );
        }
    }
};

std::string save()
{
    std::ostringstream text;
    JsonOut out( text );
    g->executions().serialize( out );
    return text.str();
}
} // namespace

TEST_CASE( "projectile_execution_captures_real_guns_and_bows_for_loaded_participant_kinds",
           "[executions][projectile_execution]" )
{
    projectile_world world;
    const std::string pair = GENERATE( "avatar_monster", "avatar_npc", "npc_avatar", "npc_monster" );
    const bool bow = GENERATE( false, true );
    Character *source = &get_avatar();
    if( pair.find( "npc_" ) == 0 ) {
        npc &person = spawn_npc( get_avatar().pos_bub().xy() + point_rel_ms( -3, 0 ), "thug" );
        // Like the avatar: no random traits or worn gear (encumbrance scatters a bow shot).
        // clear_character() also moves it, so it goes back to its spot.
        clear_character( person );
        person.setpos( get_map(), get_avatar().pos_bub() + tripoint_rel_ms( -3, 0, 0 ) );
        person.set_attitude( NPCATT_KILL );
        source = &person;
    }
    item &gun = arm( *source, bow ? "execution_projectile_test_bow" : "execution_projectile_test_gun" );
    const std::string weapon = gun.typeId().str();
    const std::string ammunition = gun.ammo_current().str();
    const std::string uid = std::to_string( gun.uid().get_value() );
    const int before = gun.ammo_remaining();
    Creature *target = nullptr;
    if( pair == "npc_avatar" ) {
        target = &get_avatar();
    } else if( pair == "avatar_npc" ) {
        target = &spawn_npc( source->pos_bub().xy() + point_rel_ms( 2, 0 ), "thug" );
    } else {
        target = &victim( source->pos_bub() + tripoint_rel_ms( 2, 0, 0 ) );
    }
    if( Character *person = target->as_character() ) {
        person->clear_worn();
        person->set_all_parts_hp_cur( 10000 );
    }
    const tripoint_abs_ms origin = source->pos_abs();
    const tripoint_abs_ms position = target->pos_abs();
    bool saw_native_event = false;
    shot_events callbacks;
    callbacks.action = [&]( const cata::event & event ) {
        if( event.type() == event_type::character_ranged_attacks_monster ||
            event.type() == event_type::character_ranged_attacks_character ) {
            saw_native_event = true;
            CHECK( execution_combat::attack_in_progress() );
            CHECK_FALSE( g->executions().last_trigger_report() );
            g->executions().drain_starts();
            CHECK( get_globals().get_global_value( "exec_projectile_starts" ).dbl() == 0 );
            CHECK_FALSE( g->save() );
        }
    };
    get_event_bus().subscribe( &callbacks );
    REQUIRE( fire( *source, *target ) == 1 );
    REQUIRE( saw_native_event );
    CHECK( gun.ammo_remaining() == before - gun.ammo_required() );
    CHECK_FALSE( execution_combat::attack_in_progress() );
    const auto selected = report();
    REQUIRE( selected.instance != 0 );
    const auto scene = view( selected.instance );
    CHECK( scene.state == "pending" );
    CHECK( scene.context.at( "attack_channel" ) == "projectile" );
    CHECK( scene.context.at( "weapon" ) == weapon );
    CHECK( scene.context.at( "weapon_uid" ) == uid );
    CHECK( scene.context.at( "ammunition" ) == ammunition );
    CHECK( scene.context.at( "weapon_skill" ) == ( bow ? "archery" : "pistol" ) );
    CHECK( scene.context.at( "projectile_origin" ).tripoint() == origin );
    CHECK( scene.context.at( "attack_target_position" ).tripoint() == position );
    CHECK( scene.context.at( "hp_loss" ).dbl() > 0 );
    CHECK( scene.context.at( "crit" ).is_dbl() );
    CHECK( scene.context.count( "style" ) == 0 );
    CHECK( scene.context.count( "technique" ) == 0 );
    if( target->as_character() ) {
        CHECK( scene.context.count( "bp" ) == 1 );
    } else {
        CHECK( scene.context.at( "weakpoint" ) == "exec_stable_wp" );
    }
    g->executions().drain_starts();
    CHECK( get_globals().get_global_value( "exec_projectile_starts" ).dbl() == 1 );
}

TEST_CASE( "projectile_execution_deduplicates_native_bursts_and_both_pellet_paths",
           "[executions][projectile_execution]" )
{
    projectile_world world;
    const int range = GENERATE( 1, 5 );
    const bool pellets = GENERATE( false, true );
    avatar &source = get_avatar();
    item &gun = arm( source, pellets ? "execution_projectile_test_shotgun" :
                     "execution_projectile_test_gun" );
    monster &target = victim( source.pos_bub() + tripoint_rel_ms( range, 0, 0 ) );
    const int before = gun.ammo_remaining();
    const int count = pellets ? 1 : 3;
    const int hp = target.get_hp();
    REQUIRE( fire( source, target, count ) == count );
    REQUIRE( report().instance != 0 );
    CHECK( gun.ammo_remaining() == before - count * gun.ammo_required() );
    CHECK( view( report().instance ).context.at( "hp_loss" ).dbl() <= hp - target.get_hp() );
    g->executions().drain_starts();
    CHECK( get_globals().get_global_value( "exec_projectile_starts" ).dbl() == 1 );
}

TEST_CASE( "projectile_execution_first_contact_is_not_replaced_by_a_later_pellet",
           "[executions][projectile_execution]" )
{
    projectile_world world;
    avatar &source = get_avatar();
    arm( source );
    monster &target = victim( source.pos_bub() + tripoint_rel_ms::east );
    const int hp = target.get_hp();
    {
        execution_combat::projectile_fire_scope firing( source, get_map(), *source.get_wielded_item() );
        for( int damage : {
                 0, 9
             } ) {
            auto round = shot( damage );
            firing.begin_round( *source.get_wielded_item(), round );
            const execution_combat::projectile_flight_scope flight( round, &get_map(), &source,
                    source.pos_bub() );
            impact( source, target, round );
        }
    }
    REQUIRE( report().instance != 0 );
    CHECK( view( report().instance ).context.at( "hp_loss" ).dbl() == 0 );
    CHECK( hp - target.get_hp() == 9 );
    g->executions().drain_starts();
    CHECK( get_globals().get_global_value( "exec_projectile_starts" ).dbl() == 1 );
}

TEST_CASE( "projectile_execution_primary_hp_snapshot_excludes_callback_damage_and_healing",
           "[executions][projectile_execution]" )
{
    projectile_world world;
    avatar &source = get_avatar();
    arm( source );
    monster &target = victim( source.pos_bub() + tripoint_rel_ms::east );
    const int hp = target.get_hp();
    bool changed = false;
    shot_events callbacks;
    callbacks.action = [&]( const cata::event & event ) {
        if( event.type() == event_type::monster_takes_damage && !changed ) {
            changed = true;
            target.apply_damage( &source, bodypart_id( "torso" ), 20 );
            target.set_hp( target.get_hp() + 2 );
        }
    };
    get_event_bus().subscribe( &callbacks );
    auto round = shot();
    scoped_impact( source, target, round );
    REQUIRE( changed );
    REQUIRE( report().instance != 0 );
    const auto context = view( report().instance ).context;
    CHECK( context.at( "hp_loss" ).dbl() == 4 );
    CHECK( context.at( "damage_pure" ).dbl() == 4 );
    CHECK( hp - target.get_hp() == 22 );
}

TEST_CASE( "projectile_execution_ranged_callbacks_cannot_spawn_automatic_child_attacks",
           "[executions][projectile_execution]" )
{
    projectile_world world;
    avatar &source = get_avatar();
    arm( source );
    monster &primary = victim( source.pos_bub() + tripoint_rel_ms::east );
    monster &secondary = victim( source.pos_bub() + tripoint_rel_ms::north );
    const bool native_gun = GENERATE( false, true );
    bool nested = false;
    shot_events callbacks;
    callbacks.action = [&]( const cata::event & event ) {
        if( event.type() == event_type::character_ranged_attacks_monster && !nested ) {
            nested = true;
            if( native_gun ) {
                REQUIRE( fire( source, secondary ) == 1 );
            } else {
                auto round = shot();
                projectile_attack( round, round.proj, source.pos_bub(), secondary.pos_bub(),
                                   dispersion_sources(), &source );
            }
            source.melee_attack_abstract( secondary, false, matec_id( "tec_none" ) );
            CHECK_FALSE( g->executions().last_trigger_report() );
        }
    };
    get_event_bus().subscribe( &callbacks );
    REQUIRE( fire( source, primary ) == 1 );
    REQUIRE( nested );
    REQUIRE( report().instance != 0 );
    CHECK( view( report().instance ).context.at( "attack_target_position" ).tripoint() ==
           primary.pos_abs() );
    g->executions().drain_starts();
    CHECK( get_globals().get_global_value( "exec_projectile_starts" ).dbl() == 1 );
}

TEST_CASE( "projectile_execution_filters_real_ammunition_and_keeps_legacy_channels",
           "[executions][projectile_execution][json]" )
{
    projectile_world world;
    mode( "ammunition" );
    arm( get_avatar(), "execution_projectile_test_gun", itype_id( "execution_projectile_test_round" ) );
    monster &target = victim( get_avatar().pos_bub() + tripoint_rel_ms::east );
    REQUIRE( fire( get_avatar(), target ) == 1 );
    REQUIRE( report().instance != 0 );
    CHECK( report().selected == "test_exec_projectile_ammunition" );
    CHECK( view( report().instance ).context.at( "ammunition" ) == "execution_projectile_test_round" );
    execution_trigger legacy;
    legacy.load( json_loader::from_string( R"({"execution":"test_exec_projectile_hold"})" ), "tests" );
    CHECK( legacy.channels.count( execution_combat::channel::projectile ) == 0 );
    execution_trigger filtered;
    filtered.load( json_loader::from_string(
                       R"({"execution":"test_exec_projectile_hold","match":{"channels":["projectile"],"ammunition":["execution_projectile_test_round"],"critical":false}})" ),
                   "tests" );
    execution_combat::attack_snapshot facts;
    facts.producer = execution_combat::channel::projectile;
    facts.hit = true;
    CHECK_FALSE( filtered.matches( facts ).empty() );
    facts.ammunition = "execution_projectile_test_round";
    CHECK_FALSE( filtered.matches( facts ).empty() );
    facts.critical = false;
    CHECK( filtered.matches( facts ).empty() );
}

TEST_CASE( "projectile_execution_refuses_fatal_hits_and_victims_killed_later_in_the_burst",
           "[executions][projectile_execution]" )
{
    projectile_world world;
    const bool later = GENERATE( false, true );
    avatar &source = get_avatar();
    arm( source );
    monster &target = victim( source.pos_bub() + tripoint_rel_ms::east );
    target.set_hp( later ? 5 : 1 );
    {
        execution_combat::projectile_fire_scope firing( source, get_map(), *source.get_wielded_item() );
        for( int n = 0; n < ( later ? 2 : 1 ); ++n ) {
            auto round = shot();
            firing.begin_round( *source.get_wielded_item(), round );
            const execution_combat::projectile_flight_scope flight( round, &get_map(), &source,
                    source.pos_bub() );
            impact( source, target, round );
        }
    }
    CHECK( target.death_finalized() );
    CHECK_FALSE( execution_fatal::pending( target ) );
    if( later ) {
        CHECK( report().reason == "participant_unavailable" );
    } else {
        CHECK_FALSE( g->executions().last_trigger_report() );
    }
    g->executions().drain_starts();
    CHECK( get_globals().get_global_value( "exec_projectile_starts" ).dbl() == 0 );
}

TEST_CASE( "projectile_execution_does_not_reclassify_a_rescued_fatal_primary_hit",
           "[executions][projectile_execution]" )
{
    projectile_world world;
    avatar &source = get_avatar();
    arm( source );
    monster &target = victim( source.pos_bub() + tripoint_rel_ms::east );
    target.set_hp( 1 );
    shot_events callbacks;
    callbacks.action = [&]( const cata::event & event ) {
        if( event.type() == event_type::monster_takes_damage ) {
            target.set_hp( 100 );
        }
    };
    get_event_bus().subscribe( &callbacks );
    auto round = shot();
    scoped_impact( source, target, round );
    CHECK_FALSE( target.is_dead_state() );
    CHECK_FALSE( g->executions().last_trigger_report() );
}

TEST_CASE( "projectile_execution_saved_progress_context_and_cooldowns_do_not_replay",
           "[executions][projectile_execution][save]" )
{
    projectile_world world;
    mode( "cooldown" );
    avatar &source = get_avatar();
    arm( source );
    monster &target = victim( source.pos_bub() + tripoint_rel_ms::east );
    REQUIRE( fire( source, target ) == 1 );
    const int64_t id = report().instance;
    REQUIRE( id != 0 );
    g->executions().drain_starts();
    const auto before = view( id );
    const std::string saved = save();
    g->executions().reset_for_load();
    g->executions().deserialize( json_loader::from_string( saved ) );
    g->executions().end_restore();
    CHECK( view( id ).due == before.due );
    CHECK( view( id ).context.at( "ammunition" ) == before.context.at( "ammunition" ) );
    CHECK( view( id ).context.at( "projectile_origin" ) == before.context.at( "projectile_origin" ) );
    for( int n = 0; n < 4; ++n ) {
        calendar::turn += 1_turns;
        g->executions().process();
    }
    CHECK( view( id ).outcome == "complete" );
    CHECK( get_globals().get_global_value( "exec_projectile_starts" ).dbl() == 1 );
    REQUIRE( fire( source, target ) == 1 );
    const auto cooled = report();
    CHECK( cooled.instance == 0 );
    CHECK( std::any_of( cooled.candidates.begin(), cooled.candidates.end(),
    []( const execution_trigger_candidate & candidate ) {
        return candidate.reason == "cooldown";
    } ) );
    calendar::turn += 21_turns;
    REQUIRE( fire( source, target ) == 1 );
    REQUIRE( report().instance != 0 );
    CHECK( report().instance != id );
}

TEST_CASE( "projectile_execution_uses_impact_direction_after_a_ranged_callback_moves_the_shooter",
           "[executions][projectile_execution][map]" )
{
    projectile_world world;
    mode( "stagger" );
    avatar &source = get_avatar();
    arm( source );
    monster &target = victim( source.pos_bub() + tripoint_rel_ms( 2, 0, 0 ) );
    const tripoint_bub_ms before = target.pos_bub();
    const tripoint_abs_ms origin = source.pos_abs();
    shot_events callbacks;
    callbacks.action = [&]( const cata::event & event ) {
        if( event.type() == event_type::character_ranged_attacks_monster ) {
            source.setpos( get_map(), before + tripoint_rel_ms( 4, 0, 0 ) );
        }
    };
    get_event_bus().subscribe( &callbacks );
    REQUIRE( fire( source, target ) == 1 );
    REQUIRE( report().instance != 0 );
    const int64_t id = report().instance;
    CHECK( view( id ).context.at( "projectile_origin" ).tripoint() == origin );
    g->executions().drain_starts();
    calendar::turn += 1_turns;
    g->executions().process();
    CHECK( target.pos_bub() == before + tripoint_rel_ms::east );
    CHECK_FALSE( target.death_finalized() );
}

TEST_CASE( "projectile_execution_knockout_uses_native_incapacity_without_death",
           "[executions][projectile_execution][knockout]" )
{
    projectile_world world;
    mode( "knockout" );
    avatar &source = get_avatar();
    arm( source );
    npc &target = spawn_npc( source.pos_bub().xy() + point_rel_ms( 2, 0 ), "thug" );
    target.clear_worn();
    target.set_all_parts_hp_cur( 10000 );
    REQUIRE( fire( source, target ) == 1 );
    REQUIRE( report().instance != 0 );
    g->executions().drain_starts();
    CHECK( incapacitation::unconscious( target ) );
    CHECK_FALSE( target.is_dead_state() );
    CHECK_FALSE( target.death_finalized() );
    CHECK( target.dodge_roll() == 0 );
}

TEST_CASE( "projectile_execution_removed_victims_and_lost_weapon_identity_are_rejected",
           "[executions][projectile_execution]" )
{
    projectile_world world;
    const bool remove = GENERATE( false, true );
    mode( remove ? "basic" : "weapon" );
    avatar &source = get_avatar();
    item &gun = arm( source );
    const int64_t original_uid = gun.uid().get_value();
    monster &target = victim( source.pos_bub() + tripoint_rel_ms::east );
    shot_events callbacks;
    callbacks.action = [&]( const cata::event & event ) {
        if( event.type() == event_type::character_ranged_attacks_monster ) {
            if( remove ) {
                get_creature_tracker().remove( target );
            } else {
                gun = item( itype_id( "knife_combat" ) );
                CHECK( gun.uid().get_value() != original_uid );
            }
        }
    };
    get_event_bus().subscribe( &callbacks );
    REQUIRE( fire( source, target ) == 1 );
    CHECK( report().instance == 0 );
    if( remove ) {
        CHECK( report().reason == "participant_unavailable" );
    } else {
        const auto result = report();
        CHECK( std::any_of( result.candidates.begin(), result.candidates.end(),
        []( const execution_trigger_candidate & candidate ) {
            return candidate.reason == "weapon_lost";
        } ) );
    }
}

TEST_CASE( "projectile_execution_excludes_unowned_secondary_magic_and_shrapnel_contacts",
           "[executions][projectile_execution]" )
{
    projectile_world world;
    const std::string kind = GENERATE( "unowned", "secondary", "magic", "shrapnel", "redirect",
                                       "bounce", "unwielded", "null_source", "no_permit" );
    avatar &source = get_avatar();
    arm( source );
    monster &target = victim( source.pos_bub() + tripoint_rel_ms::east );
    auto round = shot();
    if( kind == "magic" ) {
        round.proj.proj_effects.insert( ammo_effect_str_id( "MAGIC" ) );
    }
    round.shrapnel = kind == "shrapnel";
    if( kind == "unowned" ) {
        impact( source, target, round );
    } else {
        item detached( *source.get_wielded_item() );
        execution_combat::projectile_fire_scope firing( source, get_map(), kind == "unwielded" ?
                detached : *source.get_wielded_item() );
        auto expected = shot();
        firing.begin_round( *source.get_wielded_item(), kind == "secondary" ? expected : round );
        const execution_combat::projectile_flight_scope flight( round, &get_map(), &source,
                source.pos_bub() );
        if( kind == "bounce" ) {
            const execution_combat::projectile_flight_scope bounce( round, &get_map(), &source,
                    source.pos_bub() );
            impact( source, target, round );
        } else if( kind == "redirect" ) {
            const execution_combat::projectile_target_scope original_target( source );
            target.deal_projectile_attack( &get_map(), &source, round, 0, false );
        } else if( kind == "null_source" ) {
            const execution_combat::projectile_target_scope original_target( target );
            target.deal_projectile_attack( &get_map(), nullptr, round, 0, false );
        } else if( kind == "no_permit" ) {
            target.deal_projectile_attack( &get_map(), &source, round, 0, false );
        } else {
            impact( source, target, round );
        }
    }
    CHECK_FALSE( g->executions().last_trigger_report() );
}

TEST_CASE( "projectile_execution_no_match_and_zero_chance_preserve_native_rng",
           "[executions][projectile_execution]" )
{
    projectile_world world;
    avatar &source = get_avatar();
    arm( source );
    monster &target = victim( source.pos_bub() + tripoint_rel_ms::east );
    int baseline = 0;
    for( const std::string choice : {
             "unmatched", "zero"
         } ) {
        mode( choice );
        rng_set_engine_seed( 8913 );
        auto round = shot();
        scoped_impact( source, target, round );
        const int after = rng( 0, INT_MAX );
        CHECK( report().instance == 0 );
        if( choice == "unmatched" ) {
            baseline = after;
        } else {
            CHECK( after == baseline );
        }
    }
}

TEST_CASE( "projectile_execution_cleans_combat_guards_when_native_flight_unwinds",
           "[executions][projectile_execution]" )
{
    projectile_world world;
    avatar &source = get_avatar();
    arm( source );
    monster &target = victim( source.pos_bub() + tripoint_rel_ms::east );
    try {
        execution_combat::projectile_fire_scope firing( source, get_map(), *source.get_wielded_item() );
        auto round = shot();
        firing.begin_round( *source.get_wielded_item(), round );
        const execution_combat::projectile_flight_scope flight( round, &get_map(), &source,
                source.pos_bub() );
        impact( source, target, round );
        throw std::runtime_error( "Interrupted native flight fixture" );
    } catch( const std::runtime_error & ) {
    }
    CHECK_FALSE( execution_combat::attack_in_progress() );
    CHECK_FALSE( g->executions().last_trigger_report() );
    auto round = shot();
    scoped_impact( source, target, round );
    CHECK( report().instance != 0 );
}

TEST_CASE( "projectile_execution_bounds_a_firing_actions_victim_candidates",
           "[executions][projectile_execution]" )
{
    projectile_world world;
    avatar &source = get_avatar();
    arm( source );
    {
        execution_combat::projectile_fire_scope firing( source, get_map(), *source.get_wielded_item() );
        for( int n = 0; n < 65; ++n ) {
            monster &target = victim( tripoint_bub_ms( 40 + n % 8, 40 + n / 8, 0 ) );
            auto round = shot();
            firing.begin_round( *source.get_wielded_item(), round );
            const execution_combat::projectile_flight_scope flight( round, &get_map(), &source,
                    source.pos_bub() );
            impact( source, target, round );
        }
    }
    g->executions().drain_starts();
    CHECK( get_globals().get_global_value( "exec_projectile_starts" ).dbl() == 64 );
}

TEST_CASE( "projectile_execution_schema_supports_fatal_events_and_rejects_inherited_misses",
           "[executions][projectile_execution][json]" )
{
    execution_trigger rule;
    const std::string field = GENERATE(
                                  R"("match":{"channels":["projectile"],"hit":false})",
                                  R"("match":{"channels":["projectile"],"ammunition":[]})" );
    CHECK_THROWS_AS( rule.load( json_loader::from_string(
                                    R"({"execution":"test_exec_projectile_hold",)" + field + "}" ), "tests" ), JsonError );
    execution_trigger inherited;
    inherited.load( json_loader::from_string(
                        R"({"execution":"test_exec_projectile_hold","match":{"channels":["projectile"]}})" ),
                    "tests" );
    inherited.was_loaded = true;
    CHECK_NOTHROW( inherited.load( json_loader::from_string(
                                       R"({"execution":"test_exec_fatal_hold","event":"fatal_hit"})" ), "tests" ) );
    CHECK( inherited.fatal );
    CHECK_THROWS_AS( inherited.load( json_loader::from_string( R"({"match":{"hit":false}})" ),
                                     "tests" ),
                     JsonError );
}
