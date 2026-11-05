#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>

#include "ability.h"
#include "ability_runtime.h"
#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "condition.h"
#include "creature_tracker.h"
#include "damage.h"
#include "damage_rule.h"
#include "damage_transaction.h"
#include "dialogue.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "execution_inspector.h"
#include "execution_combat.h"
#include "execution_runtime.h"
#include "execution_trigger.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "global_vars.h"
#include "json.h"
#include "json_loader.h"
#include "magic.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "memory_fast.h"
#include "monster.h"
#include "npc.h"
#include "options_helpers.h"
#include "player_helpers.h"
#include "projectile.h"
#include "rng.h"
#include "talker.h"
#include "type_id.h"

namespace
{
const bodypart_str_id torso( "torso" );

struct damage_world {
    time_point old_time = calendar::turn;
    override_option animations{ "ANIMATION_PROJECTILES", "false" };
    damage_world() {
        g->abilities().clear();
        g->executions().clear();
        clear_avatar();
        clear_map_without_vision( -2, 1 );
        get_damage_transactions().release_settled_sources();
        get_globals().clear_global_values();
        calendar::turn = calendar::turn_zero + 12_hours;
        get_avatar().setpos( get_map(), tripoint_bub_ms( 60, 60, 0 ) );
        get_avatar().set_all_parts_hp_cur( 10000 );
    }
    ~damage_world() {
        g->abilities().clear();
        g->executions().clear();
        clear_avatar();
        calendar::turn = old_time;
    }
};

monster &victim( const std::string &id = "mon_execution_expanded_victim", int offset = 2 )
{
    monster &result = spawn_test_monster( id,
                                          get_avatar().pos_bub() + tripoint_rel_ms( offset, 0, 0 ) );
    result.set_hp( 10000 );
    return result;
}

void enable( Creature &who, const std::string &name )
{
    who.add_effect( efftype_id( "test_damage_" + name ), 1_hours );
}

damage_transaction_view hit( Creature &target, Creature *source, const damage_instance &packet )
{
    const damage_context_scope context;
    target.deal_damage( source, torso.id(), packet );
    const auto result = context.result();
    REQUIRE( result );
    return *result;
}

damage_instance pure( float amount )
{
    return damage_instance( damage_type_id( "pure" ), amount );
}

talk_effect_t effects( const std::string &json )
{
    talk_effect_t result( json_loader::from_string( json ), "effect", "damage_transaction_test" );
    finalize_conditions();
    return result;
}

std::string save()
{
    std::ostringstream buffer;
    JsonOut out( buffer );
    get_damage_transactions().serialize( out );
    return buffer.str();
}

double counter( const std::string &name )
{
    const auto *value = get_globals().maybe_get_global_value( name );
    return value ? value->dbl() : 0;
}

struct events : event_subscriber {
    using event_subscriber::notify;
    std::function<void( const cata::event & )> callback;
    void notify( const cata::event &event ) override {
        callback( event );
    }
};
} // namespace

TEST_CASE( "damage_shield_spends_exact_capacity_and_passes_overflow_for_characters",
           "[damage_transactions][powers]" )
{
    damage_world world;
    const bool npc_target = GENERATE( false, true );
    Character &target = npc_target ? static_cast<Character &>( spawn_npc(
                            get_avatar().pos_bub().xy() + point_rel_ms( 0, -2 ), "thug" ) ) : get_avatar();
    target.set_all_parts_hp_cur( 10000 );
    enable( target, "shield" );
    target.magic->set_mana( 10 );
    const auto result = hit( target, &victim(), pure( 25 ) );
    CHECK( result.incoming == 25 );
    CHECK( result.after_armor == 25 );
    CHECK( result.outgoing == 15 );
    CHECK( result.hp_loss == 15 );
    CHECK( result.absorbed == 10 );
    CHECK( result.hp_committed );
    CHECK( target.get_hp( torso.id() ) == 9985 );
    CHECK( target.magic->available_mana() == 0 );
    REQUIRE( result.rules.size() == 1 );
    CHECK( result.rules.front().payments.at( "mana" ) == 10 );
    const auto second = hit( target, nullptr, pure( 25 ) );
    CHECK( second.hp_loss == 25 );
    CHECK( second.absorbed == 0 );
}

TEST_CASE( "damage_complete_absorption_prevents_type_side_effects_and_HP_writes",
           "[damage_transactions][powers]" )
{
    damage_world world;
    avatar &target = get_avatar();
    enable( target, "shield" );
    target.magic->set_mana( 500 );
    const auto result = hit( target, nullptr, damage_instance( damage_type_id( "heat" ), 200 ) );
    CHECK( result.reason == "blocked" );
    CHECK( result.absorbed == 200 );
    CHECK( result.hp_loss == 0 );
    CHECK_FALSE( result.hp_committed );
    CHECK_FALSE( target.has_effect( efftype_id( "onfire" ) ) );
    CHECK( target.get_pain() == 0 );
    CHECK( target.magic->available_mana() == 300 );
}

TEST_CASE( "damage_mixed_components_share_one_wallet_and_keep_integer_multipliers",
           "[damage_transactions][powers]" )
{
    damage_world world;
    avatar &target = get_avatar();
    enable( target, "shield" );
    target.magic->set_mana( 10 );
    damage_instance packet;
    packet.add_damage( damage_type_id( "bash" ), 10.5f, 0, 1, 1.7f );
    packet.add_damage( damage_type_id( "cut" ), 8.5f, 0, 1, 1.1f );
    const auto result = hit( target, nullptr, packet );
    CHECK( result.incoming == 26 );
    CHECK( result.outgoing == 16 );
    CHECK( result.hp_loss == 16 );
    CHECK( result.absorbed == 10 );
    CHECK( target.magic->available_mana() == 0 );
    REQUIRE( result.outgoing_components.size() == 2 );
    CHECK( result.outgoing_components.front().damage_multiplier == 1.7f );
}

TEST_CASE( "damage_type_filters_leave_other_components_unspent",
           "[damage_transactions][powers]" )
{
    damage_world world;
    avatar &target = get_avatar();
    enable( target, "typed" );
    target.magic->set_mana( 20 );
    damage_instance packet( damage_type_id( "bash" ), 12 );
    packet.add_damage( damage_type_id( "cut" ), 8 );
    const auto result = hit( target, nullptr, packet );
    CHECK( result.absorbed == 8 );
    CHECK( result.hp_loss == 12 );
    CHECK( target.magic->available_mana() == 12 );
}

TEST_CASE( "damage_native_stamina_payment_uses_whole_affordable_points",
           "[damage_transactions][powers]" )
{
    damage_world world;
    const bool npc_target = GENERATE( false, true );
    Character &target = npc_target ? static_cast<Character &>( spawn_npc(
                            get_avatar().pos_bub().xy() + point_rel_ms( 0, -2 ), "thug" ) ) : get_avatar();
    enable( target, "stamina" );
    target.set_stamina( 9 );
    const auto result = hit( target, nullptr, pure( 10 ) );
    CHECK( result.absorbed == ( npc_target ? 0 : 4 ) );
    CHECK( result.hp_loss == ( npc_target ? 10 : 6 ) );
    REQUIRE( result.rules.size() == 1 );
    if( npc_target ) {
        CHECK( result.rules.front().reason == "unsupported_resource" );
        CHECK( result.rules.front().payments.empty() );
    } else {
        CHECK( target.get_stamina() == 1 );
        CHECK( result.rules.front().payments.at( "stamina" ) == 8 );
    }
}

TEST_CASE( "damage_monsters_support_bounded_free_defense_without_avatar_resource_fallback",
           "[damage_transactions][powers]" )
{
    damage_world world;
    monster &target = victim();
    const bool free = GENERATE( false, true );
    enable( target, free ? "shell" : "shield" );
    get_avatar().magic->set_mana( 100 );
    const auto result = hit( target, &get_avatar(), pure( 20 ) );
    CHECK( result.absorbed == ( free ? 7 : 0 ) );
    CHECK( result.hp_loss == ( free ? 13 : 20 ) );
    CHECK( get_avatar().magic->available_mana() == 100 );
    REQUIRE( result.rules.size() == 1 );
    CHECK( result.rules.front().payments.empty() );
    if( !free ) {
        CHECK( result.rules.front().reason == "unsupported_resource" );
    }
}

TEST_CASE( "damage_defense_stages_bracket_real_armor_and_conversion_changes_armor_type",
           "[damage_transactions][powers]" )
{
    damage_world world;
    monster &target = victim( "mon_test_damage_armor" );
    const bool convert = GENERATE( false, true );
    if( convert ) {
        enable( target, "convert" );
    }
    enable( target, "shell" );
    const auto result = hit( target, &get_avatar(), damage_instance( damage_type_id( "bash" ), 30 ) );
    CHECK( result.incoming == 30 );
    CHECK( result.after_armor == ( convert ? 10 : 20 ) );
    CHECK( result.absorbed == 7 );
    CHECK( result.hp_loss == ( convert ? 3 : 13 ) );
}

TEST_CASE( "damage_pre_armor_shield_blocks_before_native_armor_runs",
           "[damage_transactions][powers]" )
{
    damage_world world;
    enable( get_avatar(), "pre" );
    get_avatar().magic->set_mana( 30 );
    const auto result = hit( get_avatar(), nullptr, pure( 30 ) );
    CHECK_FALSE( result.armor_evaluated );
    CHECK( result.absorbed == 30 );
    CHECK( result.hp_loss == 0 );
}

TEST_CASE( "damage_rules_run_in_priority_order_and_mutation_outputs_are_validated",
           "[damage_transactions][powers][eoc]" )
{
    damage_world world;
    monster &target = victim();
    const std::string mode = GENERATE( "ordered", "mutate", "invalid" );
    if( mode == "ordered" ) {
        enable( target, "order_b" );
        enable( target, "order_a" );
    } else {
        enable( target, mode );
    }
    const auto result = hit( target, &get_avatar(), pure( 25 ) );
    CHECK( result.hp_loss == ( mode == "invalid" ? 25 : 8 ) );
    CHECK( result.absorbed == 0 );
    if( mode == "ordered" ) {
        REQUIRE( result.rules.size() == 2 );
        CHECK( result.rules[0].rule == "test_damage_order_a" );
        CHECK( result.rules[0].after == 12 );
    } else if( mode == "invalid" ) {
        CHECK( result.rules.front().reason == "invalid_output" );
    } else {
        CHECK( counter( "damage_mutations" ) == 1 );
    }
}

TEST_CASE( "damage_committed_observers_see_captured_HP_loss_with_defender_as_alpha",
           "[damage_transactions][powers][eoc]" )
{
    damage_world world;
    monster &target = victim();
    target.set_hp( 5 );
    enable( target, "observe" );
    const auto result = hit( target, &get_avatar(), pure( 20 ) );
    CHECK( result.outgoing == 20 );
    CHECK( result.hp_loss == 5 );
    CHECK( counter( "damage_observed_hp" ) == 5 );
    CHECK( target.get_value( "damage_observed" ).dbl() == 1 );
    CHECK( get_avatar().get_value( "damage_observed" ).dbl() == 0 );
    CHECK( target.get_killer() == &get_avatar() );
}

TEST_CASE( "damage_nested_EOC_packets_suppress_ancestor_rules_and_preserve_parent_capture",
           "[damage_transactions][powers][eoc]" )
{
    damage_world world;
    monster &target = victim();
    enable( target, "nested" );
    const auto result = hit( target, &get_avatar(), pure( 20 ) );
    CHECK( result.hp_loss == 8 );
    CHECK( target.get_hp() == 9991 );
    REQUIRE( result.children.size() == 1 );
    const auto rows = get_damage_transactions().inspect_all();
    REQUIRE( rows.size() == 2 );
    CHECK( rows[1].parent == result.id );
    CHECK( rows[1].root == result.id );
    CHECK( rows[1].hp_loss == 1 );
    CHECK( rows[1].rules.front().reason == "ancestry_suppressed" );
}

TEST_CASE( "damage_reflection_preserves_attribution_and_terminates_between_two_defenders",
           "[damage_transactions][powers]" )
{
    damage_world world;
    monster &source = victim( "mon_execution_expanded_victim", 2 );
    monster &target = victim( "mon_execution_expanded_victim", 4 );
    enable( source, "reflect" );
    enable( target, "reflect" );
    const auto result = hit( target, &source, pure( 20 ) );
    const auto rows = get_damage_transactions().inspect_all();
    REQUIRE( rows.size() == 3 );
    CHECK( result.reflected == 10 );
    CHECK( result.hp_loss == 10 );
    CHECK( target.get_hp() == 9985 );
    CHECK( source.get_hp() == 9995 );
    CHECK( rows[1].source == creature_ref::bind( target ) );
    CHECK( rows[1].instigator == creature_ref::bind( target ) );
    CHECK( rows[1].original_source == creature_ref::bind( source ) );
    CHECK( rows[2].rules.front().reason == "ancestry_suppressed" );
    CHECK( rows[2].depth == 2 );
}

TEST_CASE( "damage_paid_reflection_spends_once_and_environment_cannot_be_reflected",
           "[damage_transactions][powers]" )
{
    damage_world world;
    const bool environment = GENERATE( false, true );
    monster &source = victim();
    enable( get_avatar(), "paid_reflect" );
    get_avatar().magic->set_mana( 11 );
    const auto result = hit( get_avatar(), environment ? nullptr : &source, pure( 20 ) );
    CHECK( result.reflected == ( environment ? 0 : 5 ) );
    CHECK( result.hp_loss == ( environment ? 20 : 15 ) );
    CHECK( get_avatar().magic->available_mana() == ( environment ? 11 : 1 ) );
    CHECK( source.get_hp() == ( environment ? 10000 : 9995 ) );
}

TEST_CASE( "damage_packet_EOC_uses_explicit_roles_and_returns_the_original_packet",
           "[damage_transactions][powers][eoc]" )
{
    damage_world world;
    monster &target = victim();
    enable( target, "reflect" );
    dialogue d( get_talker_for( get_avatar() ), get_talker_for( target ) );
    effects( R"({"effect":{"deal_damage_packet":{"target":"npc","source":"u","instigator":"npc","cause":"fixture","damage":[{"damage_type":"pure","amount":20}]}}})" ).apply(
        d );
    const auto rows = get_damage_transactions().inspect_all();
    REQUIRE( rows.size() == 2 );
    CHECK( d.get_value( "damage_transaction" ) == std::to_string( rows[0].id ) );
    CHECK( d.get_value( "damage_packet_hp_loss" ).dbl() == 10 );
    CHECK( d.get_value( "damage_packet_reflected" ).dbl() == 10 );
    CHECK( rows[0].source == creature_ref::bind( get_avatar() ) );
    CHECK( rows[0].instigator == creature_ref::bind( target ) );
    CHECK( rows[0].context.at( "damage_cause" ).str() == "fixture" );
}

TEST_CASE( "damage_packet_EOC_rejects_missing_actors_invalid_parts_and_multipliers",
           "[damage_transactions][powers][eoc]" )
{
    damage_world world;
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    const std::string mode = GENERATE( "missing", "part", "multiplier", "environment" );
    std::string args = R"("target":"u","source":"none")";
    if( mode == "missing" ) {
        args = R"("target":"npc","source":"u")";
    } else if( mode == "part" ) {
        args += R"(,"bodypart":"not_a_bodypart")";
    } else if( mode == "multiplier" ) {
        args += R"(,"multiplier":101)";
    }
    effects( "{\"effect\":{\"deal_damage_packet\":{" + args +
             ",\"damage\":[{\"damage_type\":\"pure\",\"amount\":10}]}}}" ).apply( d );
    if( mode == "environment" ) {
        CHECK( d.get_value( "damage_packet_hp_loss" ).dbl() == 10 );
        const auto rows = get_damage_transactions().inspect_all();
        REQUIRE( rows.size() == 1 );
        CHECK( rows[0].source.id == 0 );
    } else {
        CHECK( d.get_value( "damage_transaction" ) == "0" );
        CHECK( get_damage_transactions().inspect_all().empty() );
        CHECK( d.get_value( "reason" ) != "resolved" );
    }
}

TEST_CASE( "damage_native_spell_filter_observes_real_spell_without_tracing_every_legacy_cast",
           "[damage_transactions][powers][magic]" )
{
    damage_world world;
    monster &target = victim();
    enable( target, "spell" );
    spell effect( spell_id( "test_ability_spell" ) );
    effect.cast_all_effects( get_avatar(), target.pos_bub() );
    CHECK( target.get_hp() == 10000 );
    auto rows = get_damage_transactions().inspect_all();
    REQUIRE( rows.size() == 1 );
    CHECK( rows.front().context.at( "damage_spell" ).str() == "test_ability_spell" );
    CHECK( rows.front().reason == "blocked" );
    target.remove_effect( efftype_id( "test_damage_spell" ) );
    get_damage_transactions().clear();
    effect.cast_all_effects( get_avatar(), target.pos_bub() );
    CHECK( target.get_hp() < 10000 );
    CHECK( get_damage_transactions().inspect_all().empty() );
}

TEST_CASE( "damage_actual_HP_capture_precedes_healing_and_raw_secondary_event_writes",
           "[damage_transactions][powers]" )
{
    damage_world world;
    get_avatar().set_part_hp_cur( torso.id(), 50 );
    bool observed = false;
    events listener;
    listener.callback = [&]( const cata::event & event ) {
        if( event.type() == event_type::character_takes_damage && !observed ) {
            observed = true;
            get_avatar().heal_bp( torso.id(), 4 );
            get_avatar().apply_damage( nullptr, torso.id(), 3 );
        }
    };
    get_event_bus().subscribe( &listener );
    const auto result = hit( get_avatar(), nullptr, pure( 10 ) );
    CHECK( observed );
    CHECK( result.hp_loss == 10 );
    CHECK( get_avatar().get_hp( torso.id() ) == 41 );
}

TEST_CASE( "damage_actor_removal_before_commit_rejects_the_remaining_packet",
           "[damage_transactions][powers]" )
{
    damage_world world;
    monster &source = victim();
    enable( get_avatar(), "callback" );
    events listener;
    listener.callback = [&]( const cata::event & event ) {
        if( event.type() == event_type::character_takes_damage ) {
            get_creature_tracker().remove( source );
        }
    };
    get_event_bus().subscribe( &listener );
    const auto result = hit( get_avatar(), &source, pure( 20 ) );
    CHECK( result.reason == "participant_unavailable" );
    CHECK( result.hp_loss == 0 );
    CHECK_FALSE( result.hp_committed );
    CHECK( get_avatar().get_hp( torso.id() ) == 9999 );
    CHECK_FALSE( get_damage_transactions().processing() );
}

TEST_CASE( "damage_removed_killer_is_retained_until_native_fatal_cleanup",
           "[damage_transactions][powers]" )
{
    damage_world world;
    monster &source = victim();
    monster &target = victim( "mon_execution_expanded_victim", 4 );
    target.set_hp( 5 );
    weak_ptr_fast<Creature> lifetime = g->shared_from( source );
    events listener;
    listener.callback = [&]( const cata::event & event ) {
        if( event.type() == event_type::monster_takes_damage ) {
            get_creature_tracker().remove( source );
        }
    };
    get_event_bus().subscribe( &listener );
    const auto result = hit( target, &source, pure( 20 ) );
    CHECK( result.hp_loss == 5 );
    CHECK( result.state == "committed" );
    CHECK_FALSE( lifetime.expired() );
    CHECK( target.get_killer() == &source );
    CHECK_THROWS_AS( save(), JsonError );
    g->cleanup_dead();
    CHECK( lifetime.expired() );
    CHECK_NOTHROW( save() );
}

TEST_CASE( "damage_callback_exceptions_release_guards_and_forbid_partial_saves",
           "[damage_transactions][powers]" )
{
    damage_world world;
    bool save_blocked = false;
    events listener;
    listener.callback = [&]( const cata::event & event ) {
        if( event.type() == event_type::monster_takes_damage ) {
            save_blocked = get_damage_transactions().processing();
            CHECK_THROWS_AS( save(), JsonError );
            throw std::runtime_error( "damage test callback" );
        }
    };
    get_event_bus().subscribe( &listener );
    monster &target = victim();
    CHECK_THROWS_AS( hit( target, &get_avatar(), pure( 10 ) ), std::runtime_error );
    CHECK( save_blocked );
    CHECK_FALSE( get_damage_transactions().processing() );
    const auto rows = get_damage_transactions().inspect_all();
    REQUIRE( rows.size() == 1 );
    CHECK( rows.front().state == "failed" );
    CHECK( rows.front().hp_loss == 10 );
    CHECK_NOTHROW( save() );
}

TEST_CASE( "damage_history_is_bounded_read_only_and_counter_survives_save",
           "[damage_transactions][powers]" )
{
    damage_world world;
    monster &target = victim();
    for( int i = 0; i < 40; ++i ) {
        hit( target, nullptr, pure( 1 ) );
    }
    const auto rows = get_damage_transactions().inspect_all();
    REQUIRE( rows.size() == 32 );
    const auto rng_before = rng_get_engine();
    const auto time_before = calendar::turn;
    const auto serialized = save();
    for( const auto &row : rows ) {
        CHECK_FALSE( execution_inspector::format_damage( row ).empty() );
    }
    CHECK( rng_get_engine() == rng_before );
    CHECK( calendar::turn == time_before );
    CHECK( save() == serialized );
    get_damage_transactions().deserialize( json_loader::from_string( serialized ) );
    CHECK( get_damage_transactions().inspect_all().empty() );
    const auto result = hit( target, nullptr, pure( 1 ) );
    CHECK( result.id > rows.back().id );
}

TEST_CASE( "damage_malformed_packet_rejects_without_HP_or_resource_spending",
           "[damage_transactions][powers]" )
{
    damage_world world;
    enable( get_avatar(), "shield" );
    get_avatar().magic->set_mana( 10 );
    const auto result = hit( get_avatar(), nullptr, pure( 1000001 ) );
    CHECK( result.state == "rejected" );
    CHECK( result.reason == "invalid_packet" );
    CHECK( result.hp_loss == 0 );
    CHECK( get_avatar().get_hp( torso.id() ) == 10000 );
    CHECK( get_avatar().magic->available_mana() == 10 );
}

TEST_CASE( "damage_ancestry_depth_and_family_limits_bound_callback_packets",
           "[damage_transactions][powers]" )
{
    damage_world world;
    monster &target = victim();
    const bool siblings = GENERATE( false, true );
    bool expanded = false;
    events listener;
    listener.callback = [&]( const cata::event & event ) {
        if( event.type() != event_type::monster_takes_damage ) {
            return;
        }
        if( siblings ) {
            if( !expanded ) {
                expanded = true;
                for( int i = 0; i < 70; ++i ) {
                    target.deal_damage( nullptr, torso.id(), pure( 1 ) );
                }
            }
        } else {
            target.deal_damage( nullptr, torso.id(), pure( 1 ) );
        }
    };
    get_event_bus().subscribe( &listener );
    const auto result = hit( target, nullptr, pure( 1 ) );
    const auto rows = get_damage_transactions().inspect_all();
    CHECK( target.get_hp() == ( siblings ? 9936 : 9991 ) );
    CHECK( result.hp_loss == 1 );
    CHECK( result.id == result.root );
    CHECK( result.children.size() == ( siblings ? 64 : 1 ) );
    CHECK( result.truncated == siblings );
    CHECK( rows.size() == ( siblings ? 32 : 10 ) );
    CHECK( rows.back().reason == "ancestry_budget" );
    CHECK( rows.back().hp_loss == 0 );
}

TEST_CASE( "damage_shield_prevents_fatal_HP_and_reflection_can_kill_with_native_credit",
           "[damage_transactions][powers]" )
{
    damage_world world;
    monster &source = victim();
    source.set_hp( 5 );
    get_avatar().set_part_hp_cur( torso.id(), 5 );
    enable( get_avatar(), "paid_reflect" );
    get_avatar().magic->set_mana( 40 );
    const auto result = hit( get_avatar(), &source, pure( 20 ) );
    CHECK( result.hp_loss == 0 );
    CHECK( result.reflected == 20 );
    CHECK_FALSE( get_avatar().is_dead_state() );
    CHECK( source.is_dead_state() );
    CHECK( source.get_killer() == &get_avatar() );
}

TEST_CASE( "damage_ability_spell_keeps_lifecycle_context_and_reports_defended_contact",
           "[damage_transactions][powers][abilities][magic]" )
{
    damage_world world;
    monster &target = victim();
    enable( target, "shell" );
    ability_request request;
    request.ability = ability_id( "test_ability_strike" );
    request.caster = creature_ref::bind( get_avatar() );
    request.target = creature_ref::bind( target );
    const auto admitted = g->abilities().request( request );
    REQUIRE( admitted.accepted() );
    g->abilities().drain_starts();
    for( int i = 0; i < 2; ++i ) {
        calendar::turn += 1_turns;
        g->abilities().process();
    }
    const auto ability = g->abilities().inspect( admitted.id );
    REQUIRE( ability );
    CHECK( ability->effect_completed );
    CHECK( ability->paid_moves == 80 );
    REQUIRE( ability->contacts.size() == 1 );
    CHECK( ability->contacts.front().facts.at( "hp_loss" ).dbl() == 5 );
    const auto rows = get_damage_transactions().inspect_all();
    REQUIRE( rows.size() == 1 );
    CHECK( rows[0].hp_loss == 5 );
    CHECK( rows[0].absorbed == 7 );
    CHECK( rows[0].context.at( "ability_instance" ).str() == std::to_string( admitted.id ) );
}

TEST_CASE( "damage_rule_schema_rejects_unknown_stages_unbounded_free_shields_and_late_edits",
           "[damage_transactions][powers][json]" )
{
    const std::string json = GENERATE(
                                 R"({"stage":"late","actions":[{"operation":"cancel"}]})",
                                 R"({"actions":[{"operation":"absorb","resource":"none"}]})",
                                 R"({"stage":"committed","actions":[{"operation":"cancel"}]})",
                                 R"({"actions":[{"operation":"convert","from":"bash","to":"cut"}]})",
                                 R"({"actions":[{"operation":"absorb","resource":""}]})" );
    damage_rule candidate;
    CHECK_THROWS_AS( candidate.load( json_loader::from_string( json ), "test_data" ), JsonError );
}

TEST_CASE( "damage_native_projectile_impact_encounters_the_same_defense",
           "[damage_transactions][powers][projectile]" )
{
    damage_world world;
    monster &target = victim();
    enable( target, "shell" );
    dealt_projectile_attack shot{};
    shot.proj.speed = 1000;
    shot.proj.range = 60;
    shot.proj.impact = pure( 25 );
    shot.proj.proj_effects.insert( ammo_effect_str_id( "NO_DAMAGE_SCALING" ) );
    const damage_context_scope trace;
    const execution_combat::projectile_target_scope contact( target );
    target.deal_projectile_attack( &get_map(), &get_avatar(), shot, 0, false );
    const auto result = trace.result();
    REQUIRE( result );
    CHECK( result->incoming == 25 );
    CHECK( result->absorbed == 7 );
    CHECK( result->hp_loss == 18 );
    CHECK( target.get_hp() == 9982 );
}

TEST_CASE( "damage_reflected_packet_encounters_its_recipients_armor",
           "[damage_transactions][powers]" )
{
    damage_world world;
    monster &source = victim( "mon_test_damage_armor" );
    monster &target = victim( "mon_execution_expanded_victim", 4 );
    enable( target, "reflect" );
    const auto result = hit( target, &source, damage_instance( damage_type_id( "bash" ), 20 ) );
    CHECK( result.reflected == 10 );
    CHECK( result.hp_loss == 10 );
    CHECK( source.get_hp() == 10000 );
    const auto rows = get_damage_transactions().inspect_all();
    REQUIRE( rows.size() == 2 );
    CHECK( rows[1].incoming == 10 );
    CHECK( rows[1].after_armor == 0 );
    CHECK( rows[1].hp_loss == 0 );
}

TEST_CASE( "damage_inspection_does_not_rerun_stateful_rule_conditions",
           "[damage_transactions][powers]" )
{
    damage_world world;
    monster &target = victim();
    enable( target, "shell" );
    auto &definition = const_cast<damage_rule &>( *damage_rule_id( "test_damage_shell" ) );
    const auto old_condition = definition.condition;
    const bool old_has = definition.has_condition;
    const on_out_of_scope restore( [&]() {
        definition.condition = old_condition;
        definition.has_condition = old_has;
    } );
    int calls = 0;
    definition.has_condition = true;
    definition.condition = [&]( const_dialogue const & ) {
        ++calls;
        return true;
    };
    hit( target, nullptr, pure( 10 ) );
    REQUIRE( calls == 1 );
    const auto rng_before = rng_get_engine();
    for( int i = 0; i < 3; ++i ) {
        for( const auto &row : get_damage_transactions().inspect_all() ) {
            CHECK_FALSE( execution_inspector::format_damage( row ).empty() );
        }
    }
    CHECK( calls == 1 );
    CHECK( rng_get_engine() == rng_before );
}

TEST_CASE( "damage_condition_killing_defender_stops_payment_for_the_original_packet",
           "[damage_transactions][powers]" )
{
    damage_world world;
    enable( get_avatar(), "shield" );
    get_avatar().magic->set_mana( 10 );
    auto &definition = const_cast<damage_rule &>( *damage_rule_id( "test_damage_shield" ) );
    const auto old_condition = definition.condition;
    const bool old_has = definition.has_condition;
    const on_out_of_scope restore( [&]() {
        definition.condition = old_condition;
        definition.has_condition = old_has;
    } );
    definition.has_condition = true;
    definition.condition = [&]( const_dialogue const & ) {
        get_avatar().set_part_hp_cur( torso.id(), 0 );
        return true;
    };
    const auto result = hit( get_avatar(), nullptr, pure( 20 ) );
    CHECK( result.reason == "defender_dead_before_commit" );
    CHECK( result.hp_loss == 0 );
    CHECK( result.absorbed == 0 );
    CHECK( get_avatar().magic->available_mana() == 10 );
}

TEST_CASE( "damage_HP_capture_precedes_stateful_native_fatal_execution_conditions",
           "[damage_transactions][powers][executions]" )
{
    damage_world world;
    monster &target = victim();
    target.set_hp( 5 );
    auto &definition = const_cast<execution_trigger &>( *execution_trigger_id( "test_fatal_hold" ) );
    const auto old_condition = definition.condition;
    const on_out_of_scope restore( [&]() {
        definition.condition = old_condition;
    } );
    bool called = false;
    definition.condition = [&]( const_dialogue const & ) {
        called = true;
        target.set_hp( 50 );
        target.apply_damage( nullptr, torso.id(), 3 );
        return false;
    };
    const damage_context_scope trace;
    {
        execution_combat::attack_scope root( get_avatar(), target,
                                             execution_combat::channel::character_melee );
        dealt_damage_instance dealt;
        const bodypart_id bp = torso.id();
        {
            const execution_combat::primary_hit_scope primary( root );
            target.deal_melee_hit( &get_avatar(), 1000, false, pure( 20 ), dealt, {}, &bp );
        }
        root.resolve( true, std::nullopt, dealt );
    }
    CHECK( called );
    const auto result = trace.result();
    REQUIRE( result );
    CHECK( result->hp_loss == 5 );
    CHECK( target.get_hp() == 47 );
}

TEST_CASE( "damage_payment_does_not_clamp_away_unspent_mana_above_current_maximum",
           "[damage_transactions][powers]" )
{
    damage_world world;
    enable( get_avatar(), "shield" );
    const int initial = get_avatar().magic->max_mana( get_avatar() ) + 100;
    get_avatar().magic->set_mana( initial );
    const auto result = hit( get_avatar(), nullptr, pure( 25 ) );
    CHECK( result.absorbed == 25 );
    CHECK( result.hp_loss == 0 );
    CHECK( get_avatar().magic->available_mana() == initial - 25 );
    CHECK( result.rules.front().payments.at( "mana" ) == 25 );
}

TEST_CASE( "ability_NPC_costs_use_real_mana_and_refuse_synthetic_stamina",
           "[damage_transactions][powers][abilities]" )
{
    damage_world world;
    npc &source = spawn_npc( get_avatar().pos_bub().xy() + point_rel_ms( 0, -2 ), "thug" );
    monster &target = victim();
    const bool mana = GENERATE( false, true );
    const int initial = ( mana ? source.magic->max_mana( source ) : source.get_stamina_max() ) + 100;
    if( mana ) {
        source.magic->set_mana( initial );
    } else {
        source.set_stamina( initial );
    }
    ability_request request;
    request.ability = ability_id( mana ? "test_ability_mana" : "test_ability_stamina" );
    request.caster = creature_ref::bind( source );
    request.target = creature_ref::bind( target );
    const auto admitted = g->abilities().request( request );
    if( !mana ) {
        CHECK_FALSE( admitted.accepted() );
        CHECK( admitted.reason == "unsupported_resource" );
        CHECK( g->abilities().inspect_all().empty() );
        return;
    }
    REQUIRE( admitted.accepted() );
    g->abilities().drain_starts();
    for( int i = 0; i < 2; ++i ) {
        calendar::turn += 1_turns;
        g->abilities().process();
    }
    const auto result = g->abilities().inspect( admitted.id );
    REQUIRE( result );
    CHECK( result->effect_completed );
    CHECK( result->paid_energy == ( mana ? 20 : 25 ) );
    CHECK( ( mana ? source.magic->available_mana() : source.get_stamina() ) ==
           initial - result->paid_energy );
}
