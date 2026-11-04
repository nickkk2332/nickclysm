#include <algorithm>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>

#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character_martial_arts.h"
#include "creature_ref.h"
#include "creature_tracker.h"
#include "damage.h"
#include "dialogue.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "execution_combat.h"
#include "execution_runtime.h"
#include "execution_trigger.h"
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
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "player_helpers.h"
#include "rng.h"
#include "talker.h"
#include "type_id.h"
#include "weakpoint.h"

namespace
{
struct trigger_world {
    time_point old_time = calendar::turn;
    trigger_world() {
        g->executions().clear();
        clear_avatar();
        clear_map_without_vision( -2, 1 );
        get_globals().clear_global_values();
        get_avatar().setpos( get_map(), tripoint_bub_ms( 60, 60, 0 ) );
    }
    ~trigger_world() {
        g->executions().clear();
        get_globals().remove_global_value( "exec_trigger_case" );
        calendar::turn = old_time;
    }
};

void select_case( const std::string &name )
{
    get_globals().set_global_value( "exec_trigger_case", name );
}

void equip( Character &actor )
{
    item knife( itype_id( "execution_trigger_test_knife" ) );
    REQUIRE( actor.wield( knife ) );
}

execution_combat::attack_snapshot packet( Creature &actor, Creature &target )
{
    execution_combat::attack_snapshot s;
    s.actor = creature_ref::bind( actor );
    s.target = creature_ref::bind( target );
    s.source_kind = actor.is_avatar() ? "avatar" : actor.is_npc() ? "npc" : "monster";
    s.target_kind = target.is_avatar() ? "avatar" : target.is_npc() ? "npc" : "monster";
    if( const monster *mon = actor.as_monster() ) {
        s.source_monster = mon->type->id.str();
    }
    if( const monster *mon = target.as_monster() ) {
        s.target_monster = mon->type->id.str();
    }
    s.weapon = "execution_trigger_test_knife";
    if( const Character *person = actor.as_character() ) {
        if( const item_location weapon = person->get_wielded_item() ) {
            s.weapon_uid = weapon->uid().get_value();
        }
    }
    s.weapon_categories = { "KNIVES" };
    s.weapon_skill = "stabbing";
    s.style = "style_none";
    s.technique = "tec_none";
    s.hit = true;
    s.critical = false;
    s.bodypart = "arm_l";
    s.hp_loss = 3;
    s.damage = {{ "pure", 3 }};
    s.actor_position = actor.pos_abs();
    s.target_position = target.pos_abs();
    s.direction = tripoint_rel_ms::east;
    s.terrain = "t_floor";
    s.furniture = "f_null";
    return s;
}

execution_trigger_report report()
{
    const auto result = g->executions().last_trigger_report();
    REQUIRE( result.has_value() );
    return *result;
}

execution_trigger_report submit( execution_combat::attack_snapshot s )
{
    if( s.id == 0 ) {
        s.id = g->executions().next_attack_id();
    }
    g->executions().on_attack_resolved( s );
    return report();
}

execution_view scene( int64_t id )
{
    const auto result = g->executions().inspect( id );
    REQUIRE( result.has_value() );
    return *result;
}

bool rejected_for( const execution_trigger_report &result, const std::string &reason )
{
    return std::any_of( result.candidates.begin(), result.candidates.end(),
    [&]( const execution_trigger_candidate & entry ) {
        return entry.reason == reason;
    } );
}

std::string save()
{
    std::ostringstream text;
    JsonOut out( text );
    g->executions().serialize( out );
    return text.str();
}

void cancel( int64_t id )
{
    REQUIRE( g->executions().cancel( id ) );
    g->executions().drain_starts();
}

void attack_until_queued( Character &actor, Creature &target )
{
    for( int n = 0; n < 100; ++n ) {
        actor.set_stamina( actor.get_stamina_max() );
        actor.set_moves( 100 );
        actor.melee_attack_abstract( target, false, matec_id( "tec_none" ) );
        if( report().instance != 0 ) {
            return;
        }
    }
    FAIL( "Native attack never selected an execution" );
}
} // namespace

TEST_CASE( "execution_triggers_capture_real_character_contacts",
           "[executions][execution_triggers]" )
{
    trigger_world world;
    select_case( "basic" );
    const bool npc_source = GENERATE( false, true );
    Character &actor = npc_source ? static_cast<Character &>( spawn_npc(
                           get_avatar().pos_bub().xy() + point_rel_ms( 0, 2 ), "thug" ) ) : get_avatar();
    equip( actor );
    const std::string uid = std::to_string( actor.get_wielded_item()->uid().get_value() );
    monster &target = spawn_test_monster( "mon_execution_trigger_test",
                                          actor.pos_bub() + tripoint_rel_ms::east );
    attack_until_queued( actor, target );
    const execution_trigger_report result = report();
    CHECK( result.selected == "test_exec_trigger_basic" );
    const execution_view queued = scene( result.instance );
    CHECK( queued.state == "pending" );
    CHECK( queued.context.at( "weapon_uid" ) == uid );
    CHECK( queued.context.at( "weapon" ) == "execution_trigger_test_knife" );
    CHECK( queued.context.at( "attack_channel" ) == "character_melee" );
    CHECK( queued.context.at( "attack_source_kind" ) == ( npc_source ? "npc" : "avatar" ) );
    CHECK( queued.context.at( "attack_target_monster" ) == "mon_execution_trigger_test" );
    CHECK( queued.context.at( "style" ) == actor.martial_arts_data->selected_style().str() );
    CHECK( queued.context.at( "technique" ) == "tec_none" );
    CHECK( queued.context.at( "hit" ) == 1 );
    CHECK( queued.context.at( "hp_loss" ).dbl() > 0 );
    CHECK( queued.context.at( "weakpoint" ) == "exec_stable_wp" );
    CHECK( queued.context.count( "bp" ) == 1 );
    CHECK( queued.context.at( "attack_target_position" ).tripoint() == target.pos_abs() );
    CHECK( get_globals().get_global_value( "exec_starts" ).dbl() == 0 );
    g->executions().drain_starts();
    CHECK( get_globals().get_global_value( "exec_starts" ).dbl() == 1 );
}

TEST_CASE( "execution_triggers_capture_real_monster_contacts_without_inventing_criticals",
           "[executions][execution_triggers]" )
{
    trigger_world world;
    select_case( "native_monster" );
    const bool npc_target = GENERATE( false, true );
    Creature &target = npc_target ? static_cast<Creature &>( spawn_npc(
                           get_avatar().pos_bub().xy() + point_rel_ms( 0, 2 ), "thug" ) ) : get_avatar();
    monster &actor = spawn_test_monster( "mon_execution_trigger_test",
                                         target.pos_bub() + tripoint_rel_ms::east );
    REQUIRE( actor.melee_attack( target, 1000.0f ) );
    const execution_trigger_report result = report();
    REQUIRE( result.instance > 0 );
    const execution_view queued = scene( result.instance );
    CHECK( queued.context.at( "attack_channel" ) == "monster_melee" );
    CHECK( queued.context.at( "attack_source_monster" ) == "mon_execution_trigger_test" );
    CHECK( queued.context.at( "attack_target_kind" ) == ( npc_target ? "npc" : "avatar" ) );
    CHECK( queued.context.count( "crit" ) == 0 );
    CHECK( queued.context.count( "weapon_uid" ) == 0 );
    CHECK( queued.context.at( "hp_loss" ).dbl() > 0 );
}

TEST_CASE( "execution_trigger_filters_distinguish_missing_facts_and_zero_damage",
           "[executions][execution_triggers][json]" )
{
    execution_trigger rule;
    rule.load( json_loader::from_string( R"({"execution":"test_exec_free","match":{
        "channels":["character_melee"],"source_kind":["avatar"],"target_kind":["monster"],
        "target_monsters":["mon_execution_trigger_test"],"weapons":["execution_trigger_test_knife"],
        "weapon_categories":["KNIVES"],"weapon_skills":["stabbing"],"styles":["style_none"],
        "techniques":["tec_none"],"body_parts":["arm_l"],"weakpoints":["exec_stable_wp"],
        "terrains":["t_floor"],"furniture":["f_null"],"critical":false,
        "hp_loss":{"minimum":1,"maximum":3},"damage":[{"type":"pure","minimum":3}]}})" ),
               "trigger_tests" );
    execution_combat::attack_snapshot s;
    s.source_kind = "avatar";
    s.target_kind = "monster";
    s.target_monster = "mon_execution_trigger_test";
    s.weapon = "execution_trigger_test_knife";
    s.weapon_categories = { "KNIVES" };
    s.weapon_skill = "stabbing";
    s.style = "style_none";
    s.technique = "tec_none";
    s.hit = true;
    s.critical = false;
    s.bodypart = "arm_l";
    s.weakpoint = "exec_stable_wp";
    s.terrain = "t_floor";
    s.furniture = "f_null";
    s.hp_loss = 3;
    s.damage = {{ "pure", 3 }};
    REQUIRE( rule.matches( s ).empty() );
    SECTION( "missing critical is different from false" ) {
        s.critical.reset();
        CHECK( rule.matches( s ) == "critical_unavailable_or_mismatched" );
    }
    SECTION( "reported damage is different from primary HP loss" ) {
        s.hp_loss = 0;
        CHECK( rule.matches( s ) == "hp_loss" );
    }
    SECTION( "missing weakpoint does not match" ) {
        s.weakpoint.reset();
        CHECK( rule.matches( s ) == "filtered" );
    }
    SECTION( "different body part does not match" ) {
        s.bodypart = "head";
        CHECK( rule.matches( s ) == "filtered" );
    }
    SECTION( "different weapon category does not match" ) {
        s.weapon_categories.clear();
        CHECK( rule.matches( s ) == "weapon_category" );
    }
    SECTION( "a missing damage type is zero only on a resolved hit" ) {
        s.damage.clear();
        CHECK( rule.matches( s ) == "damage" );
        rule.damage.at( "pure" ).minimum = 0;
        CHECK( rule.matches( s ).empty() );
        s.hp_loss.reset();
        rule.hp_loss.reset();
        CHECK( rule.matches( s ) == "damage_unavailable" );
    }
}

TEST_CASE( "execution_trigger_arbitration_is_prioritized_bounded_and_deduplicated",
           "[executions][execution_triggers]" )
{
    trigger_world world;
    equip( get_avatar() );
    monster &target = spawn_test_monster( "mon_execution_trigger_test",
                                          get_avatar().pos_bub() + tripoint_rel_ms::east );
    auto s = packet( get_avatar(), target );
    SECTION( "failed high priority falls through and lower tiers are not rolled after a win" ) {
        select_case( "priority" );
        const auto result = submit( s );
        CHECK( result.selected == "test_exec_trigger_lower_passed" );
        CHECK( rejected_for( result, "chance_failed" ) );
        CHECK_FALSE( std::any_of( result.candidates.begin(), result.candidates.end(),
        []( const execution_trigger_candidate & entry ) {
            return entry.rule == "test_exec_trigger_lowest";
        } ) );
    }
    SECTION( "a duplicate contact cannot queue another scene or consume RNG" ) {
        select_case( "basic" );
        s.id = g->executions().next_attack_id();
        REQUIRE( submit( s ).instance > 0 );
        const auto before = rng_get_engine();
        CHECK( submit( s ).reason == "duplicate_contact" );
        CHECK( rng_get_engine() == before );
    }
    SECTION( "zero chance and no matching condition leave RNG untouched" ) {
        const std::string name = GENERATE( "zero", "unconfigured" );
        select_case( name );
        const auto before = rng_get_engine();
        CHECK( submit( s ).instance == 0 );
        CHECK( rng_get_engine() == before );
    }
    SECTION( "an authored miss has no fabricated damage or critical result" ) {
        select_case( "miss" );
        s.hit = false;
        s.hp_loss.reset();
        s.critical.reset();
        s.bodypart.reset();
        s.damage.clear();
        const auto result = submit( s );
        REQUIRE( result.instance > 0 );
        const auto context = scene( result.instance ).context;
        CHECK( context.at( "hit" ) == 0 );
        CHECK( context.count( "damage" ) == 0 );
        CHECK( context.count( "hp_loss" ) == 0 );
        CHECK( context.count( "crit" ) == 0 );
    }
    SECTION( "inspection never rerolls or starts a scene" ) {
        select_case( "half" );
        const auto result = submit( s );
        const auto before = rng_get_engine();
        for( int n = 0; n < 20; ++n ) {
            CHECK( report().selected == result.selected );
            CHECK( report().instance == result.instance );
        }
        CHECK( rng_get_engine() == before );
    }
    SECTION( "one eligible probabilistic rule rolls its chance exactly once" ) {
        select_case( "half" );
        const unsigned int seed = GENERATE( 1u, 17u, 41723u );
        rng_set_engine_seed( seed );
        const auto before = rng_get_engine();
        const bool passed = rng_float( 0, 1 ) < 0.5;
        if( passed ) {
            // An accepted scene seeds its own saved RNG using two ordinary draws.
            rng( 0, std::numeric_limits<int>::max() );
            rng( 0, std::numeric_limits<int>::max() );
        }
        const auto expected = rng_get_engine();
        rng_get_engine() = before;
        CHECK( ( submit( s ).instance > 0 ) == passed );
        CHECK( rng_get_engine() == expected );
    }
    SECTION( "unloaded and dead participants are refused before selection" ) {
        select_case( "basic" );
        target.set_hp( 0 );
        CHECK( submit( s ).reason == "participant_unavailable" );
        CHECK_FALSE( g->executions().contains( get_avatar() ) );
    }
}

TEST_CASE( "execution_trigger_weights_choose_one_rule_in_a_priority_tier",
           "[executions][execution_triggers]" )
{
    trigger_world world;
    select_case( "weighted" );
    equip( get_avatar() );
    monster &target = spawn_test_monster( "mon_execution_trigger_test",
                                          get_avatar().pos_bub() + tripoint_rel_ms::east );
    int a = 0;
    int b = 0;
    rng_set_engine_seed( 41723 );
    for( int n = 0; n < 100; ++n ) {
        const auto result = submit( packet( get_avatar(), target ) );
        REQUIRE( result.instance > 0 );
        CHECK( result.candidates.size() <= 64 );
        CHECK( rejected_for( result, "not_selected" ) );
        a += result.selected == "test_exec_trigger_weighted_a";
        b += result.selected == "test_exec_trigger_weighted_b";
        g->executions().clear();
    }
    CHECK( a > 0 );
    CHECK( b > a );
    CHECK( a + b == 100 );
}

TEST_CASE( "execution_trigger_cooldowns_have_explicit_scopes_and_survive_without_active_scenes",
           "[executions][execution_triggers][save]" )
{
    trigger_world world;
    const std::string scope = GENERATE( "actor", "target", "actor_target_pair", "world" );
    select_case( "cooldown_" + scope );
    avatar &one = get_avatar();
    npc &two = spawn_npc( one.pos_bub().xy() + point_rel_ms( 0, 2 ), "thug" );
    equip( one );
    equip( two );
    monster &first = spawn_test_monster( "mon_execution_trigger_test", one.pos_bub() +
                                         tripoint_rel_ms::east );
    monster &second = spawn_test_monster( "mon_execution_trigger_test", two.pos_bub() +
                                          tripoint_rel_ms::east );
    const auto initial = submit( packet( one, first ) );
    REQUIRE( initial.instance > 0 );
    cancel( initial.instance );
    const std::string saved = save();
    const JsonObject saved_object = json_loader::from_string( saved );
    saved_object.allow_omitted_members();
    REQUIRE( saved_object.get_array( "instances" ).empty() );
    const auto before = rng_get_engine();
    g->executions().deserialize( json_loader::from_string( saved ) );
    CHECK( rng_get_engine() == before );
    CHECK_FALSE( g->executions().last_trigger_report().has_value() );
    CHECK( submit( packet( one, first ) ).instance == 0 );
    CHECK( rejected_for( report(), "cooldown" ) );
    const auto changed_target = submit( packet( one, second ) );
    CHECK( ( changed_target.instance == 0 ) == ( scope == "actor" || scope == "world" ) );
    if( changed_target.instance ) {
        cancel( changed_target.instance );
    }
    const auto changed_actor = submit( packet( two, first ) );
    CHECK( ( changed_actor.instance == 0 ) == ( scope == "target" || scope == "world" ) );
    if( changed_actor.instance ) {
        cancel( changed_actor.instance );
    }
    calendar::turn += 20_turns;
    CHECK( submit( packet( one, first ) ).instance > 0 );
    CHECK( report().attack > initial.attack );
}

TEST_CASE( "execution_trigger_refusals_do_not_charge_cooldowns_and_dry_runs_are_pure",
           "[executions][execution_triggers]" )
{
    trigger_world world;
    select_case( "claim" );
    avatar &actor = get_avatar();
    equip( actor );
    monster &target = spawn_test_monster( "mon_execution_trigger_test", actor.pos_bub() +
                                          tripoint_rel_ms::east );
    dialogue d( get_talker_for( actor ), get_talker_for( target ) );
    const auto before = rng_get_engine();
    const std::string saved = save();
    for( int n = 0; n < 10; ++n ) {
        CHECK( g->executions().check_start( execution_id( "test_exec_clock" ), d ) == "eligible" );
    }
    CHECK( rng_get_engine() == before );
    CHECK( save() == saved );
    CHECK_FALSE( g->executions().contains( target ) );
    const auto manual = g->executions().start( execution_id( "test_exec_clock" ), d, 7 );
    REQUIRE( manual.id > 0 );
    CHECK( submit( packet( actor, target ) ).instance == 0 );
    CHECK( rejected_for( report(), "participant_busy" ) );
    cancel( manual.id );
    CHECK( submit( packet( actor, target ) ).instance > 0 );
}

TEST_CASE( "execution_trigger_primary_damage_excludes_nested_damage_healing_and_attacks",
           "[executions][execution_triggers]" )
{
    trigger_world world;
    select_case( "basic" );
    avatar &actor = get_avatar();
    equip( actor );
    monster &target = spawn_test_monster( "mon_execution_trigger_test", actor.pos_bub() +
                                          tripoint_rel_ms::east );
    monster &child = spawn_test_monster( "mon_execution_trigger_test", actor.pos_bub() +
                                         tripoint_rel_ms::north );
    struct nested_callback : event_subscriber {
        using event_subscriber::notify;
        Character *actor = nullptr;
        monster *target = nullptr;
        monster *child = nullptr;
        bool fired = false;
        int primary_damage = 0;
        void notify( const cata::event &e ) override {
            if( fired || e.type() != event_type::monster_takes_damage ) {
                return;
            }
            fired = true;
            primary_damage = e.get<int>( "damage" );
            target->apply_damage( actor, bodypart_id( "torso" ), 17 );
            target->heal( 11 );
            actor->melee_attack_abstract( *child, false, matec_id( "tec_none" ) );
        }
    } callback;
    callback.actor = &actor;
    callback.target = &target;
    callback.child = &child;
    get_event_bus().subscribe( &callback );
    const int hp_before = target.get_hp();
    attack_until_queued( actor, target );
    REQUIRE( callback.fired );
    const auto result = report();
    const auto queued = scene( result.instance );
    CHECK( queued.context.at( "hp_loss" ) == callback.primary_damage );
    CHECK( hp_before - target.get_hp() == callback.primary_damage + 6 );
    CHECK( queued.context.at( "attack_target_position" ).tripoint() == target.pos_abs() );
    CHECK( g->executions().next_attack_id() == result.attack + 1 );
}

TEST_CASE( "execution_trigger_damage_callbacks_and_scripted_actions_cannot_create_roots",
           "[executions][execution_triggers]" )
{
    trigger_world world;
    select_case( "basic" );
    avatar &actor = get_avatar();
    equip( actor );
    monster &target = spawn_test_monster( "mon_execution_trigger_test", actor.pos_bub() +
                                          tripoint_rel_ms::east );
    struct attack_callback : event_subscriber {
        using event_subscriber::notify;
        Character *actor = nullptr;
        monster *target = nullptr;
        bool fired = false;
        void notify( const cata::event &e ) override {
            if( !fired && e.type() == event_type::monster_takes_damage ) {
                fired = true;
                actor->melee_attack_abstract( *target, false, matec_id( "tec_none" ) );
            }
        }
    } callback;
    callback.actor = &actor;
    callback.target = &target;
    get_event_bus().subscribe( &callback );
    SECTION( "a standalone damage event is a barrier" ) {
        target.apply_damage( &actor, bodypart_id( "torso" ), 1 );
    }
    SECTION( "an execution action is a barrier" ) {
        dialogue d( get_talker_for( actor ), get_talker_for( target ) );
        REQUIRE( g->executions().start( execution_id( "test_exec_strike" ), d, 7 ).id > 0 );
        g->executions().drain_starts();
    }
    REQUIRE( callback.fired );
    CHECK_FALSE( g->executions().last_trigger_report().has_value() );
}

TEST_CASE( "execution_trigger_weapon_binding_uses_the_contact_item_not_a_later_replacement",
           "[executions][execution_triggers]" )
{
    trigger_world world;
    const bool required = GENERATE( false, true );
    select_case( required ? "weapon_loss" : "basic" );
    avatar &actor = get_avatar();
    equip( actor );
    const std::string original = std::to_string( actor.get_wielded_item()->uid().get_value() );
    monster &target = spawn_test_monster( "mon_execution_trigger_test", actor.pos_bub() +
                                          tripoint_rel_ms::east );
    struct disarmer : event_subscriber {
        using event_subscriber::notify;
        Character *actor = nullptr;
        bool fired = false;
        void notify( const cata::event &e ) override {
            if( !fired && e.type() == event_type::monster_takes_damage ) {
                fired = true;
                actor->i_rem( &*actor->get_wielded_item() );
                item replacement( itype_id( "knife_combat" ) );
                REQUIRE( actor->wield( replacement ) );
            }
        }
    } callback;
    callback.actor = &actor;
    get_event_bus().subscribe( &callback );
    for( int n = 0; n < 100 && !callback.fired; ++n ) {
        actor.set_stamina( actor.get_stamina_max() );
        actor.melee_attack_abstract( target, false, matec_id( "tec_none" ) );
    }
    REQUIRE( callback.fired );
    if( required ) {
        CHECK( report().instance == 0 );
        CHECK( rejected_for( report(), "weapon_lost" ) );
    } else {
        REQUIRE( report().instance > 0 );
        CHECK( scene( report().instance ).context.at( "weapon_uid" ) == original );
    }
}

TEST_CASE( "execution_trigger_defensive_counters_from_direct_hits_are_not_new_roots",
           "[executions][execution_triggers][martial_arts]" )
{
    trigger_world world;
    select_case( "basic" );
    avatar &target = get_avatar();
    equip( target );
    const matype_id style( "test_style_execution_trigger_counter" );
    target.martial_arts_data->add_martialart( style );
    target.martial_arts_data->set_style( style, false );
    target.set_skill_level( skill_id( "dodge" ), 10 );
    target.set_dex_base( 20 );
    target.set_dodges_left( 1 );
    monster &source = spawn_test_monster( "mon_execution_trigger_test", target.pos_bub() +
                                          tripoint_rel_ms::east );
    SECTION( "direct hit invokes a defensive EOC before primary damage" ) {
        dealt_damage_instance dealt;
        const bodypart_id bp( "arm_l" );
        target.deal_melee_hit( &source, 1000, false,
                               damage_instance( damage_type_id( "pure" ), 1 ), dealt, {}, &bp );
    }
    SECTION( "direct dodge resolution invokes a defensive EOC before any damage" ) {
        REQUIRE( target.deal_melee_attack( &source, -1000 ) < 0 );
    }
    REQUIRE( get_globals().get_global_value( "exec_trigger_counters" ).dbl() > 0 );
    CHECK_FALSE( g->executions().last_trigger_report().has_value() );
}

TEST_CASE( "execution_triggers_keep_removed_native_sources_alive_but_refuse_their_scenes",
           "[executions][execution_triggers]" )
{
    trigger_world world;
    select_case( "native_monster" );
    monster &actor = spawn_test_monster( "mon_execution_trigger_test", get_avatar().pos_bub() +
                                         tripoint_rel_ms::east );
    struct source_remover : event_subscriber {
        using event_subscriber::notify;
        monster *actor = nullptr;
        weak_ptr_fast<monster> lifetime;
        bool fired = false;
        bool held = false;
        void notify( const cata::event &e ) override {
            if( !fired && e.type() == event_type::character_takes_damage ) {
                fired = true;
                get_creature_tracker().remove( *actor );
                get_creature_tracker().remove_dead();
                held = !lifetime.expired();
            }
        }
    } callback;
    callback.actor = &actor;
    callback.lifetime = get_creature_tracker().find_by_uid( actor.persistent_id() );
    get_event_bus().subscribe( &callback );
    REQUIRE( actor.melee_attack( get_avatar(), 1000.0f ) );
    REQUIRE( callback.fired );
    CHECK( callback.held );
    CHECK( callback.lifetime.expired() );
    CHECK( report().reason == "participant_unavailable" );
    CHECK( report().instance == 0 );
}

TEST_CASE( "execution_triggers_preserve_impact_position_and_direction_after_displacement",
           "[executions][execution_triggers][map]" )
{
    trigger_world world;
    select_case( "direction" );
    avatar &actor = get_avatar();
    equip( actor );
    monster &target = spawn_test_monster( "mon_execution_trigger_test", actor.pos_bub() +
                                          tripoint_rel_ms::east );
    const tripoint_abs_ms impact = target.pos_abs();
    const tripoint_bub_ms displaced = actor.pos_bub() + tripoint_rel_ms::north;
    struct mover : event_subscriber {
        using event_subscriber::notify;
        monster *target = nullptr;
        tripoint_bub_ms destination;
        bool fired = false;
        void notify( const cata::event &e ) override {
            if( !fired && e.type() == event_type::monster_takes_damage ) {
                fired = true;
                target->setpos( get_map(), destination );
            }
        }
    } callback;
    callback.target = &target;
    callback.destination = displaced;
    get_event_bus().subscribe( &callback );
    attack_until_queued( actor, target );
    REQUIRE( callback.fired );
    const auto queued = scene( report().instance );
    CHECK( queued.context.at( "attack_target_position" ).tripoint() == impact );
    CHECK( target.pos_bub() == displaced );
    g->executions().drain_starts();
    calendar::turn += 1_turns;
    g->executions().process();
    CHECK( target.pos_bub() == displaced + tripoint_rel_ms::east );
}

TEST_CASE( "execution_trigger_definitions_reject_unsupported_producers_and_invalid_limits",
           "[executions][execution_triggers][json]" )
{
    const std::string field = GENERATE(
                                  R"("event":"fatal_hit")",
                                  R"("match":{"channels":["projectile"]})",
                                  R"("match":{"fatal_state":"pending"})",
                                  R"("match":{"crit":true})",
                                  R"("match":{"weapons":[]})",
                                  R"("match":{"hp_loss":{"minimum":5,"maximum":2}})",
                                  R"("chance":1.1)", R"("weight":0)",
                                  R"("cooldown":{"ticks":-1})",
                                  R"("cooldown":{"scope":"unknown"})" );
    execution_trigger rule;
    CHECK_THROWS_AS( rule.load( json_loader::from_string(
                                    R"({"execution":"test_exec_free",)" + field + "}" ), "trigger_tests" ), JsonError );
}

TEST_CASE( "execution_trigger_array_ids_cannot_bypass_the_definition_budget",
           "[executions][execution_triggers][json]" )
{
    std::ostringstream text;
    JsonOut out( text );
    out.start_object();
    out.member( "id" );
    out.start_array();
    for( int n = 0; n < 2049; ++n ) {
        out.write( "over_budget_trigger_" + std::to_string( n ) );
    }
    out.end_array();
    out.end_object();
    CHECK_THROWS_WITH( execution_triggers::load( json_loader::from_string( text.str() ),
                       "trigger_tests" ), Catch::Matchers::Contains( "at most 2048" ) );
    CHECK( execution_trigger_id( "test_exec_trigger_basic" ).is_valid() );
}
