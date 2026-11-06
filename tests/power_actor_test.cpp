#include <functional>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ability_ai.h"
#include "avatar.h"
#include "bodypart.h"
#include "cata_catch.h"
#include "condition.h"
#include "creature_tracker.h"
#include "damage.h"
#include "dialogue.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "execution_runtime.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "global_vars.h"
#include "incapacitation.h"
#include "item.h"
#include "itype.h"
#include "iuse_actor.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser.h"
#include "monster.h"
#include "npc.h"
#include "options_helpers.h"
#include "player_helpers.h"
#include "power_actor.h"
#include "power_motion.h"
#include "power_runtime.h"
#include "rng.h"
#include "talker.h"

static const itype_id itype_power_demo_familiar_focus( "power_demo_familiar_focus" );

namespace
{
struct actor_world {
    time_point old_time = calendar::turn;
    override_option animations{ "ANIMATIONS", "false" };
    actor_world() {
        g->abilities().clear();
        g->executions().clear();
        clear_avatar();
        clear_map_without_vision( -2, 1 );
        get_globals().clear_global_values();
        calendar::turn = calendar::turn_zero + 12_hours;
        get_avatar().setpos( get_map(), tripoint_bub_ms( 60, 60, 0 ) );
        get_avatar().set_all_parts_hp_cur( 10000 );
        get_avatar().set_moves( 1000 );
    }
    ~actor_world() {
        g->abilities().clear();
        g->executions().clear();
        clear_avatar();
        get_map().vertical_shift( 0 );
        calendar::turn = old_time;
    }
};
monster &victim( int x = 2, int y = 0 )
{
    auto &who = spawn_test_monster( "mon_execution_expanded_victim",
                                    get_avatar().pos_bub() + tripoint_rel_ms( x, y, 0 ) );
    who.set_hp( 10000 );
    who.anger = 100;
    who.morale = 100;
    who.aggro_character = true;
    return who;
}
Creature &actor_kind( const std::string &kind, int x, int y )
{
    if( kind == "npc" ) {
        auto &who = spawn_npc( get_avatar().pos_bub().xy() + point_rel_ms( x, y ), "thug" );
        who.clear_worn();
        who.set_all_parts_hp_cur( 10000 );
        return who;
    }
    if( kind == "monster" ) {
        return victim( x, y );
    }
    return get_avatar();
}
power_summon_admission summon( Creature &owner, const std::string &id = "test_power_familiar",
                               int x = 1, int y = 1 )
{
    auto out = get_power_actors().summon( power_summon_id( id ), creature_ref::bind( owner ),
                                          owner.pos_abs() + tripoint_rel_ms( x, y, 0 ) );
    INFO( out.reason );
    REQUIRE( out.id != 0 );
    REQUIRE( out.summon.resolve() );
    return out;
}
std::string save()
{
    std::ostringstream buffer;
    JsonOut out( buffer );
    g->abilities().serialize( out );
    return buffer.str();
}
void restore( const std::string &text )
{
    g->abilities().reset_for_load();
    g->abilities().deserialize( json_loader::from_string( text ) );
    g->abilities().end_restore();
}
void run_effect( const std::string &text, dialogue &d )
{
    const talk_effect_t operation( json_loader::from_string( text ), "effect", "power_actor_test" );
    finalize_conditions();
    operation.apply( d );
}
double counter( const std::string &key )
{
    const auto *value = get_globals().maybe_get_global_value( key );
    return value ? value->dbl() : 0;
}
struct observer : event_subscriber {
    using event_subscriber::notify;
    std::vector<std::pair<const Creature *, const Creature *>> hits;
    std::vector<const Creature *> kills;
    std::function<void()> callback;
    void notify( const cata::event & ) override {}
    void notify( const cata::event &event, std::unique_ptr<talker> target,
                 std::unique_ptr<talker> source ) override {
        if( event.type() == event_type::character_kills_monster ) {
            kills.push_back( target ? target->get_const_creature() : nullptr );
        }
        if( event.type() == event_type::monster_takes_damage ||
            event.type() == event_type::character_takes_damage ) {
            hits.emplace_back( target ? target->get_const_creature() : nullptr,
                               source ? source->get_const_creature() : nullptr );
            if( callback ) {
                callback();
            }
        }
    }
};
} // namespace

TEST_CASE( "power_fling_observes_native_motion_and_attributes_impacts_for_all_actors",
           "[power_actors][map]" )
{
    actor_world world;
    const std::string kind = GENERATE( "avatar", "npc", "monster" );
    Creature &target = actor_kind( kind, 1, 0 );
    Creature &source = kind == "avatar" ? actor_kind( "npc", -1, 0 ) : get_avatar();
    const auto before = target.pos_abs();
    get_map().ter_set( target.pos_bub() + tripoint_rel_ms( 2, 0, 0 ), ter_id( "t_concrete_wall" ) );
    observer events;
    get_event_bus().subscribe( &events );
    const auto result = power_motion::fling( creature_ref::bind( source ), creature_ref::bind( target ),
                        40 );
    CHECK( result.accepted );
    CHECK( result.reason == "moved" );
    CHECK( result.origin == before );
    CHECK( result.destination == target.pos_abs() );
    CHECK( result.path.size() == 2 );
    CHECK( result.landed );
    CHECK( result.hp_loss > 0 );
    REQUIRE_FALSE( result.impacts.empty() );
    CHECK( result.impacts.front().kind == "terrain" );
    CHECK_FALSE( result.impacts.front().broken );
    REQUIRE_FALSE( events.hits.empty() );
    for( const auto &hit : events.hits ) {
        CHECK( hit.second == &source );
    }
    CHECK_FALSE( power_motion::processing() );
}

TEST_CASE( "power_fling_collateral_keeps_instigator_and_occupancy", "[power_actors]" )
{
    actor_world world;
    const std::string kind = GENERATE( "npc", "monster" );
    Creature &target = victim( 1 );
    Creature &blocker = actor_kind( kind, 2, 0 );
    const int hp = blocker.get_hp();
    observer events;
    get_event_bus().subscribe( &events );
    const auto result = power_motion::fling( creature_ref::bind( get_avatar() ),
                        creature_ref::bind( target ), 40 );
    CHECK( result.reason == "blocked" );
    CHECK( blocker.get_hp() < hp );
    CHECK( get_creature_tracker().creature_at( blocker.pos_bub() ) == &blocker );
    REQUIRE_FALSE( result.impacts.empty() );
    CHECK( result.impacts.front().collider.resolve() == &blocker );
    CHECK( result.impacts.front().collider_hp_loss == hp - blocker.get_hp() );
    for( const auto &hit : events.hits ) {
        CHECK( hit.second == &get_avatar() );
    }
}

TEST_CASE( "power_fling_callbacks_cannot_move_or_replace_the_in_flight_identity", "[power_actors]" )
{
    actor_world world;
    const std::string mode = GENERATE( "source_removed", "target_removed", "displaced" );
    monster &source = victim( -1 );
    monster &target = victim( 1 );
    const auto hold = g->shared_from( target );
    get_map().ter_set( target.pos_bub() + tripoint_rel_ms::east, ter_id( "t_concrete_wall" ) );
    observer events;
    bool changed = false;
    events.callback = [&]() {
        if( changed ) {
            return;
        }
        changed = true;
        CHECK_THROWS_AS( save(), JsonError );
        if( mode == "source_removed" ) {
            get_creature_tracker().remove( source );
        } else if( mode == "target_removed" ) {
            get_creature_tracker().remove( target );
        } else {
            target.setpos( get_map(), target.pos_bub() + tripoint_rel_ms::south );
        }
    };
    get_event_bus().subscribe( &events );
    const auto result = power_motion::fling( creature_ref::bind( source ), creature_ref::bind( target ),
                        40 );
    CHECK( changed );
    CHECK( result.reason == "interrupted" );
    CHECK_FALSE( power_motion::processing() );
    CHECK_NOTHROW( save() );
}

TEST_CASE( "power_fling_does_not_lend_its_source_to_nested_environmental_damage", "[power_actors]" )
{
    actor_world world;
    auto &target = victim( 1 );
    get_map().ter_set( target.pos_bub() + tripoint_rel_ms::east, ter_id( "t_concrete_wall" ) );
    observer events;
    bool nested = false;
    events.callback = [&]() {
        if( !nested ) {
            nested = true;
            target.apply_damage( nullptr, bodypart_id( "torso" ), 1 );
        }
    };
    get_event_bus().subscribe( &events );
    power_motion::fling( creature_ref::bind( get_avatar() ), creature_ref::bind( target ), 40 );
    REQUIRE( events.hits.size() >= 2 );
    CHECK( events.hits[0].second == &get_avatar() );
    CHECK( events.hits[1].second == nullptr );
}

TEST_CASE( "power_fling_refuses_invalid_requests_without_world_or_rng_changes", "[power_actors]" )
{
    actor_world world;
    auto &target = victim();
    const auto before = target.pos_abs();
    const auto rng = rng_get_engine();
    const auto result = power_motion::fling( creature_ref::bind( get_avatar() ),
                        creature_ref::bind( target ), 301 );
    CHECK( result.reason == "invalid_velocity" );
    CHECK_FALSE( result.accepted );
    CHECK( target.pos_abs() == before );
    CHECK( rng_get_engine() == rng );
}

TEST_CASE( "owned_familiars_separate_two_owners_and_only_dismiss_the_selected_bond",
           "[power_actors][save]" )
{
    actor_world world;
    Creature &other = actor_kind( "npc", -4, 0 );
    const auto a = summon( get_avatar() );
    const auto b = summon( other );
    const auto a2 = summon( get_avatar(), "test_power_familiar", 1, -1 );
    CHECK( a.summon.resolve()->get_summoner() == &get_avatar() );
    CHECK( b.summon.resolve()->get_summoner() == &other );
    CHECK( get_power_actors().command( a.id, creature_ref::bind( other ), "follow" ) == "wrong_owner" );
    restore( save() );
    CHECK( get_power_actors().end( a.id, "dismissed" ) );
    get_power_actors().process();
    CHECK( a.summon.resolve() == nullptr );
    CHECK( a2.summon.resolve() != nullptr );
    CHECK( b.summon.resolve() != nullptr );
    CHECK( counter( "power_familiar_ends" ) == 1 );
    get_power_actors().process();
    CHECK( counter( "power_familiar_ends" ) == 1 );
}

TEST_CASE( "owned_summons_use_typed_monster_owners_and_bound_allegiance", "[power_actors]" )
{
    actor_world world;
    Creature &owner = victim( -3 );
    const auto bond = summon( owner );
    Creature &familiar = *bond.summon.resolve();
    CHECK( familiar.get_summoner() == &owner );
    CHECK( familiar.attitude_to( owner ) == Creature::Attitude::FRIENDLY );
    CHECK( familiar.attitude_to( get_avatar() ) == owner.attitude_to( get_avatar() ) );
    CHECK( get_power_actors().summon( power_summon_id( "test_power_familiar" ),
                                      bond.summon ).reason == "summon_cannot_own" );
}

TEST_CASE( "familiar_commands_drive_native_pathfinding_without_overwriting_other_bonds",
           "[power_actors]" )
{
    actor_world world;
    const auto bond = summon( get_avatar() );
    auto &familiar = *bond.summon.resolve()->as_monster();
    familiar.setpos( get_map(), familiar.pos_bub() + tripoint_rel_ms( 2, 0, 0 ) );
    familiar.plan();
    CHECK( familiar.get_dest() == get_avatar().pos_abs() );
    const auto guard = familiar.pos_abs() + tripoint_rel_ms( 0, 2, 0 );
    CHECK( get_power_actors().command( bond.id, creature_ref::bind( get_avatar() ), "guard", {}, guard )
           == "commanded" );
    familiar.plan();
    CHECK( familiar.get_dest() == guard );
    const auto before = familiar.pos_abs();
    get_map().build_map_cache( 0 );
    familiar.set_moves( 100 );
    familiar.move();
    CHECK( familiar.pos_abs() != before );
    CHECK( familiar.get_moves() < 100 );
    CHECK( counter( "power_familiar_commands" ) == 1 );
}

TEST_CASE( "familiar_attack_tracks_identity_and_executes_the_same_paid_ability", "[power_actors]" )
{
    actor_world world;
    const auto bond = summon( get_avatar(), "test_power_familiar", 1, 0 );
    auto &familiar = *bond.summon.resolve()->as_monster();
    auto &enemy = victim( 2 );
    get_map().build_map_cache( 0 );
    CHECK( get_power_actors().command( bond.id, creature_ref::bind( get_avatar() ), "attack",
                                       creature_ref::bind( enemy ) ) == "commanded" );
    familiar.set_moves( 100 );
    familiar.move();
    REQUIRE( g->abilities().inspect_all().size() == 1 );
    g->abilities().drain_starts();
    CHECK( get_powers().balance( familiar, "test_power_focus" ).total == 95 );
    CHECK( enemy.pos_abs().x() > familiar.pos_abs().x() + 1 );
    CHECK( get_power_actors().motions().back().instigator.resolve() == &familiar );
}

TEST_CASE( "familiar_attack_rejects_friendlies_and_never_hits_a_replacement_at_old_coordinates",
           "[power_actors]" )
{
    actor_world world;
    const auto bond = summon( get_avatar() );
    auto &familiar = *bond.summon.resolve()->as_monster();
    auto &enemy = victim( 3 );
    get_map().build_map_cache( 0 );
    CHECK( get_power_actors().command( bond.id, creature_ref::bind( get_avatar() ), "attack",
                                       creature_ref::bind( get_avatar() ) ) == "invalid_target" );
    CHECK( get_power_actors().command( bond.id, creature_ref::bind( get_avatar() ), "attack",
                                       creature_ref::bind( enemy ) ) == "commanded" );
    const auto old = enemy.pos_bub();
    get_creature_tracker().remove( enemy );
    auto &replacement = spawn_test_monster( "mon_execution_expanded_victim", old );
    familiar.plan();
    CHECK_FALSE( familiar.has_dest() ); // Already adjacent to its owner: follow fallback.
    familiar.set_moves( 100 );
    familiar.move();
    CHECK( g->abilities().inspect_all().empty() );
    CHECK( replacement.get_hp() == replacement.get_hp_max() );
}

TEST_CASE( "familiar_expiry_leash_and_owner_death_clean_up_once", "[power_actors]" )
{
    actor_world world;
    const std::string cause = GENERATE( "expiry", "leash", "owner_dead" );
    auto &owner = victim( -3 );
    const auto bond = summon( owner );
    if( cause == "expiry" ) {
        calendar::turn += 31_turns;
    } else if( cause == "leash" ) {
        bond.summon.resolve()->setpos( get_map(), tripoint_bub_ms( 80, 80, 0 ) );
    } else {
        owner.set_hp( 0 );
        owner.check_dead_state( &get_map() );
    }
    get_power_actors().process();
    CHECK( bond.summon.resolve() == nullptr );
    CHECK( counter( "power_familiar_ends" ) == 1 );
    get_power_actors().process();
    CHECK( counter( "power_familiar_ends" ) == 1 );
}

TEST_CASE( "familiar_unload_tombstone_removes_only_the_returning_identity", "[power_actors][save]" )
{
    actor_world world;
    const auto bond = summon( get_avatar() );
    auto mon = get_creature_tracker().find_by_uid( bond.summon.id );
    const auto position = mon->pos_bub();
    get_creature_tracker().remove( *mon );
    get_power_actors().process();
    CHECK( get_power_actors().inspect().front().state == "ending" );
    restore( save() );
    auto &replacement = spawn_test_monster( "mon_execution_expanded_victim", position );
    get_power_actors().process();
    CHECK( get_creature_tracker().creature_at( position ) == &replacement );
    get_creature_tracker().remove( replacement );
    REQUIRE( g->place_critter_at( mon, position ) );
    get_power_actors().process();
    CHECK( bond.summon.resolve() == nullptr );
    CHECK( counter( "power_familiar_ends" ) == 1 );
}

TEST_CASE( "familiar_dead_identity_is_retired_without_a_permanent_tombstone", "[power_actors]" )
{
    actor_world world;
    const auto bond = summon( get_avatar() );
    auto &mon = *bond.summon.resolve();
    mon.apply_damage( &get_avatar(), bodypart_id( "torso" ), 100000 );
    mon.check_dead_state( &get_map() );
    get_creature_tracker().remove_dead();
    get_power_actors().process();
    CHECK( get_power_actors().inspect().front().state == "ended" );
    CHECK( counter( "power_familiar_ends" ) == 1 );
}

TEST_CASE( "familiar_callbacks_are_bounded_and_self_dismissal_is_safe", "[power_actors]" )
{
    actor_world world;
    const std::string mode = GENERATE( "test_power_familiar_self_dismiss",
                                       "test_power_familiar_recursive" );
    const auto bond = summon( get_avatar(), mode );
    CHECK( get_power_actors().inspect().size() == 1 );
    if( mode == "test_power_familiar_self_dismiss" ) {
        CHECK( get_power_actors().inspect().front().state == "ending" );
        get_power_actors().process();
        CHECK( bond.summon.resolve() == nullptr );
    } else {
        CHECK( get_power_actors().inspect().front().state == "active" );
    }
}

TEST_CASE( "familiar_world_transition_removes_loaded_bonds_and_preserves_legacy_saves",
           "[power_actors][save]" )
{
    actor_world world;
    const auto bond = summon( get_avatar() );
    g->abilities().cancel_for_world_transition( "dimension_travel" );
    CHECK( bond.summon.resolve() == nullptr );
    restore( R"({"next_id":1,"roots":[],"instances":[]})" );
    CHECK( get_power_actors().inspect().empty() );
}

TEST_CASE( "familiar_load_refuses_duplicate_and_recursive_bonds", "[power_actors][save]" )
{
    actor_world world;
    const auto a = summon( get_avatar() );
    const auto b = summon( get_avatar(), "test_power_familiar", 1, -1 );
    std::ostringstream buffer;
    JsonOut out( buffer );
    get_power_actors().serialize( out );
    const std::string text = buffer.str();
    const std::string duplicate = "\"id\":" + std::to_string( b.id );
    std::string bad = text;
    const auto pos = bad.find( duplicate );
    REQUIRE( pos != std::string::npos );
    bad.replace( pos, duplicate.size(), "\"id\":" + std::to_string( a.id ) );
    CHECK_THROWS_AS( get_power_actors().deserialize( json_loader::from_string( bad ) ), JsonError );
    CHECK( get_power_actors().inspect().size() == 2 );
    const auto ref_json = []( const creature_ref & ref ) {
        std::ostringstream buffer;
        JsonOut out( buffer );
        ref.serialize( out );
        return buffer.str();
    };
    const std::string old_owner = "\"owner\":" + ref_json( creature_ref::bind( get_avatar() ) );
    bad = text;
    const auto owner_pos = bad.find( old_owner );
    REQUIRE( owner_pos != std::string::npos );
    bad.replace( owner_pos, old_owner.size(), "\"owner\":" + ref_json( b.summon ) );
    CHECK_THROWS_AS( get_power_actors().deserialize( json_loader::from_string( bad ) ), JsonError );
    CHECK( get_power_actors().inspect().size() == 2 );
}

TEST_CASE( "ability_AI_queries_are_pure_and_choose_range_and_relation_aware_actions",
           "[power_actors]" )
{
    actor_world world;
    auto &enemy = victim( 3 );
    const auto caster = creature_ref::bind( get_avatar() );
    const auto target = creature_ref::bind( enemy );
    const auto rng = rng_get_engine();
    const auto before = save();
    const std::vector<ability_id> choices{ ability_id( "test_ai_fling" ), ability_id( "test_ai_ranged" ) };
    for( int i = 0; i < 5; ++i ) {
        const auto best = ability_ai::choose( choices, caster, target );
        REQUIRE( best );
        CHECK( best->ability == ability_id( "test_ai_ranged" ) );
    }
    CHECK( save() == before );
    CHECK( rng_get_engine() == rng );
    enemy.setpos( get_map(), get_avatar().pos_bub() + tripoint_rel_ms::east );
    const auto best = ability_ai::choose( choices, caster, target );
    REQUIRE( best );
    CHECK( best->ability == ability_id( "test_ai_fling" ) );
}

TEST_CASE( "ability_AI_respects_payments_charges_busy_state_and_nonopted_in_abilities",
           "[power_actors]" )
{
    actor_world world;
    auto &enemy = victim( 1 );
    ability_request req;
    req.ability = ability_id( "test_ai_fling" );
    req.caster = creature_ref::bind( get_avatar() );
    req.target = creature_ref::bind( enemy );
    CHECK( ability_ai::query( req ).eligible() );
    REQUIRE( get_powers().spend( get_avatar(), "test_power_focus", 100, "fixture" ) );
    CHECK_FALSE( ability_ai::query( req ).eligible() );
    get_powers().modify( get_avatar(), "test_power_focus", 100, "fixture" );
    REQUIRE( g->abilities().request( req ).accepted() );
    CHECK( ability_ai::query( req ).reason == "caster_busy" );
    g->abilities().drain_starts();
    req.ability = ability_id( "test_ability_strike" );
    CHECK( ability_ai::query( req ).reason == "no_ai_intent" );
}

TEST_CASE( "ability_AI_ties_and_area_safety_are_deterministic", "[power_actors]" )
{
    actor_world world;
    auto &enemy = victim( 3 );
    const auto caster = creature_ref::bind( get_avatar() );
    const auto target = creature_ref::bind( enemy );
    const auto a = ability_ai::choose( { ability_id( "test_ai_tie" ), ability_id( "test_ai_ranged" ) },
                                       caster, target );
    REQUIRE( a );
    CHECK( a->ability == ability_id( "test_ai_ranged" ) );
    ability_request request = *a;
    request.ability = ability_id( "test_ai_safe" );
    CHECK( ability_ai::query( request ).eligible() );
    auto &ally = victim( 3, 1 );
    ally.friendly = -1;
    CHECK( ability_ai::query( request ).reason == "unsafe_area" );
    request.ability = ability_id( "test_ai_heal" );
    get_avatar().set_all_parts_hp_cur( get_avatar().get_hp_max() );
    CHECK( ability_ai::query( request ).reason == "target_healthy" );
}

TEST_CASE( "power_actor_EOCs_and_formula_queries_use_real_roles_and_handles", "[power_actors]" )
{
    actor_world world;
    auto &enemy = victim( 1 );
    dialogue d( get_talker_for( get_avatar() ), get_talker_for( enemy ) );
    run_effect( R"({"effect":[{"spawn_power_summon":{"id":"test_power_familiar"}}]})", d );
    CHECK( d.get_value( "reason" ) == "accepted" );
    const auto handle = d.get_value( "power_bond" );
    run_effect(
        R"({"effect":[{"command_power_summon":{"bond":{"context_val":"power_bond"},"command":"guard"}}]})",
        d );
    CHECK( d.get_value( "reason" ) == "commanded" );
    const auto rng = rng_get_engine();
    const auto before = save();
    math_exp query;
    REQUIRE( query.parse( "u_ability_query('test_ai_fling') + u_power_summons('test_power_familiar')" ) );
    CHECK( query.eval( d ) == 2 );
    CHECK( save() == before );
    CHECK( rng_get_engine() == rng );
    run_effect( R"({"effect":[{"use_best_ability":{"abilities":["test_ai_fling","test_ai_ranged"]}}]})",
                d );
    CHECK( d.get_value( "reason" ) == "accepted" );
    g->abilities().drain_starts();
    CHECK( enemy.pos_abs().x() > get_avatar().pos_abs().x() + 1 );
    CHECK( get_powers().balance( get_avatar(), "test_power_focus" ).total == 95 );
    CHECK( handle != "0" );
    run_effect( R"({"effect":[{"dismiss_power_summons":{"id":"test_power_familiar"}}]})", d );
    CHECK( d.get_value( "power_summons_dismissed" ).dbl() == 1 );
    get_power_actors().process();
    CHECK( counter( "power_familiar_ends" ) == 1 );
}


TEST_CASE( "power_fling_breaks_native_walls_and_reports_the_real_fall", "[power_actors][map]" )
{
    actor_world world;
    auto &target = victim( 1 );
    const bool fall = GENERATE( false, true );
    if( fall ) {
        for( int x = 1; x <= 5; ++x ) {
            const auto p = target.pos_bub() + tripoint_rel_ms( x, 0, 0 );
            get_map().ter_set( p, ter_id( "t_open_air" ) );
            get_map().ter_set( p + tripoint_rel_ms( 0, 0, -1 ), ter_id( "t_floor" ) );
        }
    } else {
        get_map().ter_set( target.pos_bub() + tripoint_rel_ms::east, ter_id( "t_single_pane_glass" ) );
    }
    observer events;
    get_event_bus().subscribe( &events );
    const auto out = power_motion::fling( creature_ref::bind( get_avatar() ),
                                          creature_ref::bind( target ), 40 );
    CHECK( out.accepted );
    if( fall ) {
        CHECK( out.fell );
        CHECK( target.pos_abs().z() == -1 );
        CHECK( out.path.back() == target.pos_abs() );
    } else {
        REQUIRE_FALSE( out.impacts.empty() );
        CHECK( out.impacts.front().broken );
        CHECK( out.path.size() > 1 );
    }
    REQUIRE_FALSE( events.hits.empty() );
    for( const auto &hit : events.hits ) {
        CHECK( hit.second == &get_avatar() );
    }
}

TEST_CASE( "power_fling_exceptions_preserve_committed_results_and_release_save_guard",
           "[power_actors]" )
{
    actor_world world;
    auto &target = victim( 1 );
    get_map().ter_set( target.pos_bub() + tripoint_rel_ms::east, ter_id( "t_concrete_wall" ) );
    observer events;
    events.callback = []() {
        throw std::runtime_error( "impact fixture" );
    };
    get_event_bus().subscribe( &events );
    const int hp = target.get_hp();
    CHECK_THROWS_AS( power_motion::fling( creature_ref::bind( get_avatar() ),
                                          creature_ref::bind( target ), 40 ), std::runtime_error );
    CHECK( target.get_hp() < hp );
    CHECK( get_power_actors().motions().back().reason == "callback_exception" );
    CHECK_FALSE( power_motion::processing() );
    CHECK_NOTHROW( save() );
}

TEST_CASE( "familiar_callback_exceptions_remove_only_the_failed_owned_actor", "[power_actors]" )
{
    actor_world world;
    const auto surviving = summon( get_avatar() );
    observer events;
    events.callback = []() {
        CHECK_THROWS_AS( save(), JsonError );
        CHECK_THROWS_AS( g->abilities().cancel_for_world_transition( "fixture" ), JsonError );
        throw std::runtime_error( "summon fixture" );
    };
    get_event_bus().subscribe( &events );
    CHECK_THROWS_AS( get_power_actors().summon( power_summon_id( "test_power_familiar_callbacks" ),
                     creature_ref::bind( get_avatar() ) ), std::runtime_error );
    CHECK( surviving.summon.resolve() );
    CHECK( get_power_actors().inspect().back().state == "ended" );
    CHECK( get_power_actors().inspect().back().reason == "callback_exception" );
    CHECK( counter( "power_familiar_ends" ) == 1 );
    CHECK_NOTHROW( save() );
}

TEST_CASE( "owned_summon_limits_and_occupied_tiles_refuse_without_extra_spawns", "[power_actors]" )
{
    actor_world world;
    const auto owner = creature_ref::bind( get_avatar() );
    CHECK( get_power_actors().summon( power_summon_id( "test_power_familiar" ), owner,
                                      get_avatar().pos_abs() ).reason == "invalid_spawn_tile" );
    summon( get_avatar() );
    summon( get_avatar(), "test_power_familiar", 1, -1 );
    CHECK( get_power_actors().summon( power_summon_id( "test_power_familiar" ),
                                      owner ).reason == "summon_budget" );
    CHECK( get_power_actors().inspect().size() == 2 );
}

TEST_CASE( "ability_AI_NPC_and_monster_entry_points_use_native_action_accounting",
           "[power_actors]" )
{
    actor_world world;
    const std::string kind = GENERATE( "npc", "monster" );
    Creature &source = actor_kind( kind, -1, 0 );
    if( npc *person = source.as_npc() ) {
        person->set_attitude( NPCATT_KILL );
    }
    REQUIRE( source.attitude_to( get_avatar() ) == Creature::Attitude::HOSTILE );
    const auto selected = ability_ai::choose( { ability_id( "test_ai_fling" ) },
                          creature_ref::bind( source ), creature_ref::bind( get_avatar() ) );
    REQUIRE( selected );
    const auto rng = rng_get_engine();
    CHECK( ability_ai::query( *selected ).eligible() );
    CHECK( rng_get_engine() == rng );
    const auto admission = g->abilities().request( *selected );
    REQUIRE( admission.accepted() );
    g->abilities().drain_starts();
    CHECK( get_powers().balance( source, "test_power_focus" ).total == 95 );
    const auto view = g->abilities().inspect( admission.id );
    REQUIRE( view );
    CHECK( view->paid_energy == 5 );
    CHECK( view->paid_charges == 1 );
    CHECK( view->effect_completed );
    CHECK( get_power_actors().motions().back().cause == "ability:" + std::to_string( admission.id ) );
}

TEST_CASE( "familiar_demo_item_route_saves_a_string_handle_and_issues_orders",
           "[power_actors][powers_demo]" )
{
    // This checks the powers_demo mod's own items, so it runs only with that mod loaded, as in
    // CI's mods job.
    if( !itype_power_demo_familiar_focus.is_valid() ) {
        return;
    }
    actor_world world;
    const auto use = []( const std::string & id ) {
        item focus( itype_id( id ), calendar::turn );
        const auto it = focus.type->use_methods.find( "effect_on_conditions" );
        REQUIRE( it != focus.type->use_methods.end() );
        REQUIRE( it->second.get_actor_ptr() );
        it->second.get_actor_ptr()->use( &get_avatar(), focus, &get_map(), get_avatar().pos_bub() );
    };
    use( "power_demo_familiar_focus" );
    REQUIRE( get_power_actors().inspect().size() == 1 );
    const auto bond = get_power_actors().inspect().front();
    CHECK( bond.definition == power_summon_id( "power_demo_familiar" ) );
    use( "power_demo_familiar_guard_focus" );
    CHECK( get_power_actors().inspect().front().command == "guard" );
    use( "power_demo_familiar_follow_focus" );
    CHECK( get_power_actors().inspect().front().command == "follow" );
    restore( save() );
    use( "power_demo_familiar_focus" );
    get_power_actors().process();
    CHECK( bond.summon.resolve() == nullptr );
}


TEST_CASE( "familiar_native_kill_credit_resolves_the_actual_character_owner", "[power_actors]" )
{
    actor_world world;
    const std::string kind = GENERATE( "avatar", "npc" );
    Creature &owner = actor_kind( kind, -4, 0 );
    const auto bond = summon( owner );
    auto &enemy = victim( 3 );
    observer events;
    get_event_bus().subscribe( &events );
    enemy.apply_damage( bond.summon.resolve(), bodypart_id( "torso" ), 100000 );
    enemy.check_dead_state( &get_map() );
    CHECK( enemy.is_dead_state() );
    CHECK( enemy.get_killer() == bond.summon.resolve() );
    REQUIRE( events.kills.size() == 1 );
    CHECK( events.kills.front() == &owner );
}


TEST_CASE( "power_fling_does_not_damage_a_collider_removed_by_an_impact_callback",
           "[power_actors]" )
{
    actor_world world;
    auto &target = victim( 1 );
    auto &blocker = victim( 2 );
    const auto hold = g->shared_from( blocker );
    const int hp = blocker.get_hp();
    observer events;
    bool changed = false;
    events.callback = [&]() {
        if( !changed ) {
            changed = true;
            get_creature_tracker().remove( blocker );
        }
    };
    get_event_bus().subscribe( &events );
    const auto out = power_motion::fling( creature_ref::bind( get_avatar() ),
                                          creature_ref::bind( target ), 40 );
    CHECK( out.reason == "interrupted" );
    CHECK( blocker.get_hp() == hp );
    CHECK_FALSE( out.landed );
}


TEST_CASE( "ability_AI_familiar_lost_vision_follows_instead_of_casting_at_the_old_target",
           "[power_actors][powers_demo]" )
{
    actor_world world;
    const auto bond = summon( get_avatar(), "power_demo_familiar", 3, 0 );
    auto &familiar = *bond.summon.resolve()->as_monster();
    auto &enemy = victim( 6 );
    get_map().build_map_cache( 0 );
    REQUIRE( get_power_actors().command( bond.id, creature_ref::bind( get_avatar() ), "attack",
                                         creature_ref::bind( enemy ) ) == "commanded" );
    familiar.add_effect( efftype_id( "blind" ), 10_turns );
    familiar.process_effects();
    REQUIRE_FALSE( familiar.sees( get_map(), enemy ) );
    familiar.set_moves( 100 );
    familiar.move();
    CHECK( g->abilities().inspect_all().empty() );
    CHECK( get_powers().balance( familiar, "power_demo_focus_pool" ).total == 100 );
    CHECK( familiar.get_dest() == get_avatar().pos_abs() );
}


TEST_CASE( "familiar_cleanup_waits_until_a_legacy_native_flight_finishes", "[power_actors]" )
{
    actor_world world;
    const auto bond = summon( get_avatar(), "test_power_familiar", 1, 0 );
    Creature *target = bond.summon.resolve();
    get_map().ter_set( target->pos_bub() + tripoint_rel_ms::east, ter_id( "t_concrete_wall" ) );
    observer events;
    bool marked = false;
    events.callback = [&]() {
        if( !marked ) {
            marked = true;
            CHECK( get_power_actors().end( bond.id, "dismissed" ) );
            get_power_actors().process();
            CHECK( bond.summon.resolve() == target );
            CHECK_THROWS_AS( save(), JsonError );
        }
    };
    get_event_bus().subscribe( &events );
    CHECK( g->fling_creature( target, 0_degrees, 40 ) );
    CHECK( marked );
    CHECK_FALSE( power_motion::processing() );
    get_power_actors().process();
    CHECK( bond.summon.resolve() == nullptr );
    CHECK( counter( "power_familiar_ends" ) == 1 );
}
