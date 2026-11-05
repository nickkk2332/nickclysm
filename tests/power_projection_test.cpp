#include <functional>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ability_runtime.h"
#include "avatar.h"
#include "bodypart.h"
#include "cata_catch.h"
#include "condition.h"
#include "creature_tracker.h"
#include "damage.h"
#include "damage_transaction.h"
#include "dialogue.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "execution_inspector.h"
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
#include "magic.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser.h"
#include "monster.h"
#include "npc.h"
#include "options_helpers.h"
#include "player_helpers.h"
#include "power_projection.h"
#include "power_runtime.h"
#include "rng.h"
#include "talker.h"

namespace
{
struct projection_world {
    time_point old_time = calendar::turn;
    override_option animations{ "ANIMATIONS", "false" };
    projection_world() {
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
    ~projection_world() {
        g->abilities().clear();
        g->executions().clear();
        clear_avatar();
        get_map().vertical_shift( 0 );
        calendar::turn = old_time;
    }
};
monster &enemy( int x = 3, int y = 0 )
{
    auto &mon = spawn_test_monster( "mon_execution_expanded_victim",
                                    get_avatar().pos_bub() + tripoint_rel_ms( x, y, 0 ) );
    mon.set_hp( 10000 );
    mon.anger = 100;
    mon.morale = 100;
    mon.aggro_character = true;
    return mon;
}
Creature &owner_kind( const std::string &kind )
{
    if( kind == "npc" ) {
        auto &who = spawn_npc( get_avatar().pos_bub().xy() + point_rel_ms( 0, -2 ), "thug" );
        who.clear_worn();
        who.set_all_parts_hp_cur( 10000 );
        return who;
    }
    if( kind == "monster" ) {
        return enemy( -4, -2 );
    }
    return get_avatar();
}
power_projection_request request( const std::string &id, Creature &owner,
                                  const std::optional<tripoint_abs_ms> &point = std::nullopt,
                                  Creature *target = nullptr )
{
    power_projection_request r;
    r.definition = power_projection_id( id );
    r.owner = creature_ref::bind( owner );
    r.point = point;
    if( target ) {
        r.target = creature_ref::bind( *target );
    }
    return r;
}
int64_t create( power_projection_request r, bool begin = true )
{
    const auto out = get_projections().create( r );
    INFO( out.reason );
    REQUIRE( out.id != 0 );
    if( begin ) {
        g->abilities().drain_starts();
    }
    return out.id;
}
power_projection_view view( int64_t id )
{
    for( const auto &p : get_projections().inspect() ) {
        if( p.id == id ) {
            return p;
        }
    }
    FAIL( "Missing projection " << id );
    return {};
}
void tick( int count = 1 )
{
    for( int i = 0; i < count; ++i ) {
        calendar::turn += 1_turns;
        g->abilities().process();
    }
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
double counter( const std::string &name )
{
    const auto *value = get_globals().maybe_get_global_value( name );
    return value ? value->dbl() : 0;
}
void run_effect( const std::string &text, dialogue &d )
{
    const talk_effect_t effect( json_loader::from_string( text ), "effect", "projection_test" );
    finalize_conditions();
    effect.apply( d );
}
struct observer : event_subscriber {
    using event_subscriber::notify;
    std::vector<const Creature *> sources;
    std::function<void()> callback;
    void notify( const cata::event & ) override {}
    void notify( const cata::event &event, std::unique_ptr<talker>,
                 std::unique_ptr<talker> source ) override {
        if( event.type() == event_type::monster_takes_damage ||
            event.type() == event_type::character_takes_damage ) {
            sources.push_back( source ? source->get_const_creature() : nullptr );
            if( callback ) {
                callback();
            }
        }
    }
};
} // namespace

TEST_CASE( "power_projectiles_travel_once_per_world_second_then_commit_native_attributed_damage",
           "[power_projections]" )
{
    projection_world world;
    const std::string kind = GENERATE( "avatar", "npc", "monster" );
    Creature &owner = owner_kind( kind );
    const auto point = owner.pos_abs() + tripoint_rel_ms( 3, 0, 0 );
    auto *target = g->place_critter_at( mtype_id( "mon_execution_expanded_victim" ),
                                        get_map().get_bub( point ) );
    REQUIRE( target );
    target->set_hp( 10000 );
    observer events;
    get_event_bus().subscribe( &events );
    const auto id = create( request( "test_projection_bolt", owner, point ) );
    CHECK( view( id ).position == owner.pos_abs() );
    tick();
    CHECK( view( id ).travelled == 1 );
    g->abilities().process();
    g->abilities().drain_starts();
    CHECK( view( id ).travelled == 1 );
    CHECK( target->get_hp() == 10000 );
    tick( 2 );
    CHECK( target->get_hp() == 9980 );
    CHECK( view( id ).state == "ended" );
    CHECK( view( id ).reason == "contact" );
    CHECK( view( id ).hp_loss == 20 );
    REQUIRE( events.sources.size() == 1 );
    CHECK( events.sources.front() == &owner );
    CHECK( counter( "projection_hits" ) == 1 );
    CHECK( counter( "projection_ends" ) == 1 );
}

TEST_CASE( "power_projectile_native_shield_spends_exact_capacity_and_leaks_the_remainder",
           "[power_projections]" )
{
    projection_world world;
    auto &source = enemy( -3 );
    auto &target = get_avatar();
    target.magic->set_mana( 10 );
    target.add_effect( efftype_id( "test_damage_shield" ), 1_minutes );
    const int hp = target.get_hp();
    create( request( "test_projection_fast", source, target.pos_abs() ) );
    tick();
    CHECK( target.magic->available_mana() == 0 );
    CHECK( hp - target.get_hp() == 10 );
    REQUIRE_FALSE( get_damage_transactions().inspect_all().empty() );
    CHECK( get_damage_transactions().inspect_all().front().instigator.resolve() == &source );
}

TEST_CASE( "power_projectile_wall_and_body_blockers_do_not_teleport_or_retarget",
           "[power_projections]" )
{
    projection_world world;
    const bool body = GENERATE( false, true );
    auto &target = enemy( 4 );
    Creature *blocker = nullptr;
    if( body ) {
        blocker = &enemy( 2 );
    } else {
        get_map().ter_set( get_avatar().pos_bub() + tripoint_rel_ms( 2, 0, 0 ),
                           ter_id( "t_concrete_wall" ) );
    }
    const auto id = create( request( "test_projection_fast", get_avatar(), target.pos_abs() ) );
    tick();
    CHECK( target.get_hp() == 10000 );
    CHECK( view( id ).travelled == 2 );
    CHECK( view( id ).reason == ( body ? "contact" : "terrain" ) );
    if( blocker ) {
        CHECK( blocker->get_hp() == 9980 );
    }
}

TEST_CASE( "power_homing_uses_the_saved_identity_after_movement_and_load",
           "[power_projections][save]" )
{
    projection_world world;
    auto &target = enemy( 4 );
    const auto id = create( request( "test_projection_homing", get_avatar(), std::nullopt, &target ) );
    tick();
    restore( save() );
    target.setpos( get_map(), target.pos_bub() + tripoint_rel_ms( 0, 2, 0 ) );
    tick( 5 );
    CHECK( target.get_hp() == 9980 );
    CHECK( view( id ).reason == "contact" );
    CHECK( counter( "projection_starts" ) == 1 );
}

TEST_CASE( "power_homing_target_loss_never_adopts_a_replacement", "[power_projections]" )
{
    projection_world world;
    auto &target = enemy( 3 );
    const auto point = target.pos_bub();
    const auto id = create( request( "test_projection_homing", get_avatar(), std::nullopt, &target ) );
    get_creature_tracker().remove( target );
    auto &replacement = spawn_test_monster( "mon_execution_expanded_victim", point );
    tick( 3 );
    CHECK( replacement.get_hp() == replacement.get_hp_max() );
    CHECK( view( id ).reason == "target_unavailable" );
}

TEST_CASE( "power_beam_telegraphs_charges_and_pulses_without_double_damage_on_repeated_pumps",
           "[power_projections]" )
{
    projection_world world;
    auto &target = enemy( 4 );
    const auto id = create( request( "test_projection_beam", get_avatar(), target.pos_abs() ) );
    CHECK( view( id ).state == "charging" );
    REQUIRE_FALSE( get_projections().tiles().empty() );
    CHECK( get_projections().tiles().front().charging );
    tick();
    CHECK( target.get_hp() == 10000 );
    restore( save() );
    tick();
    CHECK( view( id ).state == "active" );
    CHECK( target.get_hp() == 9990 );
    g->abilities().process();
    CHECK( target.get_hp() == 9990 );
    tick();
    CHECK( target.get_hp() == 9980 );
    CHECK( counter( "projection_starts" ) == 1 );
    CHECK( counter( "projection_releases" ) == 1 );
}

TEST_CASE( "power_charged_beam_damage_interrupt_prevents_release", "[power_projections]" )
{
    projection_world world;
    auto &target = enemy( 4 );
    const auto id = create( request( "test_projection_beam", get_avatar(), target.pos_abs() ) );
    get_avatar().apply_damage( &target, bodypart_id( "torso" ), 1 );
    tick( 2 );
    CHECK( target.get_hp() == 10000 );
    CHECK( view( id ).reason == "damaged" );
    CHECK( counter( "projection_releases" ) == 0 );
}

TEST_CASE( "power_equal_opposing_beams_clash_before_any_damage_and_do_not_roll_rng",
           "[power_projections]" )
{
    projection_world world;
    auto &owner = enemy( 6 );
    const auto a = create( request( "test_projection_ready_beam", get_avatar(), owner.pos_abs() ) );
    const auto b = create( request( "test_projection_ready_beam", owner, get_avatar().pos_abs() ) );
    const auto rng = rng_get_engine();
    const int avatar_hp = get_avatar().get_hp();
    tick();
    CHECK( rng_get_engine() == rng );
    CHECK( owner.get_hp() == 10000 );
    CHECK( get_avatar().get_hp() == avatar_hp );
    CHECK( view( a ).reason == "contested" );
    CHECK( view( b ).reason == "contested" );
    CHECK( counter( "projection_clashes" ) == 2 );
    CHECK( counter( "projection_hits" ) == 0 );
}

TEST_CASE( "power_stronger_beam_keeps_only_residual_strength_and_scaled_damage",
           "[power_projections]" )
{
    projection_world world;
    auto &owner = enemy( 6 );
    auto first = request( "test_projection_ready_beam", get_avatar(), owner.pos_abs() );
    first.strength = 40;
    const auto a = create( first );
    const auto b = create( request( "test_projection_ready_beam", owner, get_avatar().pos_abs() ) );
    tick();
    CHECK( view( a ).strength == 20 );
    CHECK( view( a ).state == "active" );
    CHECK( view( b ).reason == "contested" );
    CHECK( owner.get_hp() == 9995 );
}

TEST_CASE( "power_projectile_interceptors_and_crossing_bolts_stop_at_the_contact_cell",
           "[power_projections]" )
{
    projection_world world;
    auto &owner = enemy( 6 );
    const bool barrier = GENERATE( false, true );
    const auto a = create( request( "test_projection_bolt", get_avatar(), owner.pos_abs() ) );
    const auto b = create( request( barrier ? "test_projection_barrier" : "test_projection_bolt", owner,
                                    barrier ? get_avatar().pos_abs() + tripoint_rel_ms( 2, 0, 0 ) : get_avatar().pos_abs() ) );
    tick( 4 );
    CHECK( view( a ).reason == "contested" );
    CHECK( view( b ).reason == "contested" );
    CHECK( owner.get_hp() == 10000 );
    REQUIRE_FALSE( get_projections().contacts().empty() );
    CHECK( get_projections().contacts().front().reason == "clash" );
}

TEST_CASE( "power_allied_beams_coexist_without_contesting_each_other", "[power_projections]" )
{
    projection_world world;
    auto &target = enemy( 5 );
    const auto a = create( request( "test_projection_ready_beam", get_avatar(), target.pos_abs() ) );
    const auto b = create( request( "test_projection_ready_beam", get_avatar(), target.pos_abs() ) );
    tick();
    CHECK( view( a ).state == "active" );
    CHECK( view( b ).state == "active" );
    CHECK( target.get_hp() == 9980 );
    CHECK( counter( "projection_clashes" ) == 0 );
}

TEST_CASE( "power_area_observes_enter_stay_exit_on_safe_pumps_without_duplicate_hooks",
           "[power_projections]" )
{
    projection_world world;
    auto &target = enemy( 3 );
    create( request( "test_projection_area", get_avatar(), get_avatar().pos_abs() ) );
    CHECK( counter( "projection_enters" ) == 0 );
    target.setpos( get_map(), get_avatar().pos_bub() + tripoint_rel_ms::east );
    g->abilities().drain_starts();
    CHECK( counter( "projection_enters" ) == 1 );
    CHECK( counter( "projection_stays" ) == 0 );
    g->abilities().drain_starts();
    CHECK( counter( "projection_enters" ) == 1 );
    tick();
    CHECK( counter( "projection_stays" ) == 1 );
    target.setpos( get_map(), get_avatar().pos_bub() + tripoint_rel_ms( 4, 0, 0 ) );
    g->abilities().drain_starts();
    CHECK( counter( "projection_exits" ) == 1 );
    CHECK( get_projections().area_value( target, power_projection_id( "test_projection_area" ) ) == 0 );
}

TEST_CASE( "power_area_exit_and_enter_preserve_identity_after_remove_replace_and_load",
           "[power_projections][save]" )
{
    projection_world world;
    auto &target = enemy( 1 );
    const auto id = create( request( "test_projection_area", get_avatar(), get_avatar().pos_abs() ) );
    REQUIRE( view( id ).members.size() == 1 );
    restore( save() );
    g->abilities().drain_starts();
    CHECK( counter( "projection_enters" ) == 1 );
    const auto point = target.pos_bub();
    get_creature_tracker().remove( target );
    auto &replacement = spawn_test_monster( "mon_execution_expanded_victim", point );
    replacement.anger = 100;
    replacement.morale = 100;
    replacement.aggro_character = true;
    g->abilities().drain_starts();
    CHECK( counter( "projection_exits" ) == 1 );
    CHECK( counter( "projection_enters" ) == 2 );
    REQUIRE( view( id ).members.size() == 1 );
    CHECK( view( id ).members.front().resolve() == &replacement );
}

TEST_CASE( "power_overlapping_area_wards_sum_independent_native_damage_contributions",
           "[power_projections]" )
{
    projection_world world;
    auto &owner = owner_kind( "npc" );
    auto &attacker = enemy( 5 );
    const auto point = get_avatar().pos_abs();
    const auto a = create( request( "test_projection_ward", get_avatar(), point ) );
    const auto b = create( request( "test_projection_ward", owner, point ) );
    const auto hit = [&]() {
        const int hp = get_avatar().get_hp();
        get_avatar().deal_damage( &attacker, bodypart_id( "torso" ),
                                  damage_instance( damage_type_id( "pure" ), 25 ) );
        return hp - get_avatar().get_hp();
    };
    CHECK( get_projections().area_value( get_avatar(), power_projection_id( "test_projection_ward" ),
                                         true ) == 10 );
    CHECK( hit() == 15 );
    CHECK( get_projections().end( a ) );
    CHECK( hit() == 20 );
    CHECK( view( b ).state == "active" );
    g->abilities().drain_starts();
    CHECK( view( a ).state == "ended" );
}

TEST_CASE( "power_moving_area_follows_its_owner_and_reconciles_membership", "[power_projections]" )
{
    projection_world world;
    auto &target = enemy( 1 );
    create( request( "test_projection_moving", get_avatar(), get_avatar().pos_abs() ) );
    CHECK( counter( "projection_enters" ) == 1 );
    get_avatar().setpos( get_map(), get_avatar().pos_bub() + tripoint_rel_ms( 5, 0, 0 ) );
    CHECK( get_projections().area_value( target,
                                         power_projection_id( "test_projection_moving" ) ) == 0 );
    g->abilities().drain_starts();
    CHECK( counter( "projection_exits" ) == 1 );
}

TEST_CASE( "power_area_payloads_use_native_damage_and_on_stay_once_per_interval",
           "[power_projections]" )
{
    projection_world world;
    auto &target = enemy( 1 );
    const auto id = create( request( "test_projection_harm_area", get_avatar(),
                                     get_avatar().pos_abs() ) );
    tick();
    CHECK( target.get_hp() == 9993 );
    CHECK( view( id ).hp_loss == 7 );
    g->abilities().drain_starts();
    g->abilities().process();
    CHECK( target.get_hp() == 9993 );
    tick();
    CHECK( target.get_hp() == 9986 );
    CHECK( counter( "projection_stays" ) == 2 );
}

TEST_CASE( "power_area_members_are_removed_before_terminal_exit_hooks_and_end_runs_once",
           "[power_projections]" )
{
    projection_world world;
    enemy( 1 );
    enemy( 0, 1 );
    const auto id = create( request( "test_projection_area", get_avatar(), get_avatar().pos_abs() ) );
    CHECK( counter( "projection_enters" ) == 2 );
    REQUIRE( get_projections().end( id ) );
    CHECK( get_projections().owned_value( get_avatar(),
                                          power_projection_id( "test_projection_area" ) ) == 0 );
    g->abilities().drain_starts();
    CHECK( counter( "projection_exits" ) == 2 );
    CHECK( counter( "projection_ends" ) == 1 );
    g->abilities().drain_starts();
    CHECK( counter( "projection_ends" ) == 1 );
}

TEST_CASE( "power_projection_parent_application_cancels_membership_and_owned_children",
           "[power_projections]" )
{
    projection_world world;
    auto &target = enemy( 1 );
    power_application_request application;
    application.application = power_application_id( "test_power_ward" );
    application.owner = application.target = creature_ref::bind( get_avatar() );
    const auto app = get_powers().apply( application );
    REQUIRE( app.accepted() );
    g->abilities().drain_starts();
    auto r = request( "test_projection_area", get_avatar(), get_avatar().pos_abs() );
    r.application = app.id;
    const auto id = create( r );
    CHECK( get_projections().area_value( target, r.definition ) == 1 );
    REQUIRE( get_powers().end( app.id ) );
    CHECK( get_projections().area_value( target, r.definition ) == 0 );
    tick();
    CHECK( view( id ).reason == "application_ended" );
    CHECK( counter( "projection_exits" ) == 1 );
}

TEST_CASE( "power_projection_upkeep_respects_shared_reservations_and_keeps_committed_start_cost",
           "[power_projections]" )
{
    projection_world world;
    auto &target = enemy( 4 );
    const auto id = create( request( "test_projection_paid", get_avatar(), target.pos_abs() ) );
    CHECK( get_powers().balance( get_avatar(), "test_power_focus" ).total == 95 );
    REQUIRE( get_powers().reserve( get_avatar(), "test_power_focus", 95, 12345 ) );
    tick();
    CHECK( view( id ).reason == "upkeep_unaffordable" );
    CHECK( target.get_hp() == 10000 );
    CHECK( get_powers().balance( get_avatar(), "test_power_focus" ).total == 95 );
    get_powers().release_reservation( 12345 );
}

TEST_CASE( "power_projection_charge_and_paid_deadlines_survive_save_without_replay",
           "[power_projections][save]" )
{
    projection_world world;
    auto &target = enemy( 4 );
    const auto id = create( request( "test_projection_paid", get_avatar(), target.pos_abs() ) );
    tick();
    CHECK( view( id ).paid_upkeep == 5 );
    restore( save() );
    g->abilities().process();
    CHECK( view( id ).paid_start == 5 );
    CHECK( view( id ).paid_upkeep == 5 );
    tick();
    CHECK( view( id ).paid_upkeep == 10 );
}

TEST_CASE( "power_projection_time_jumps_end_without_replaying_hits_costs_or_enters",
           "[power_projections]" )
{
    projection_world world;
    auto &target = enemy( 4 );
    const auto id = create( request( "test_projection_paid", get_avatar(), target.pos_abs() ) );
    calendar::turn += 10_turns;
    g->abilities().process();
    CHECK( view( id ).reason == "time_skipped" );
    CHECK( target.get_hp() == 10000 );
    CHECK( view( id ).paid_upkeep == 0 );
}

TEST_CASE( "power_projection_owner_loss_and_world_transitions_end_exactly_once",
           "[power_projections]" )
{
    projection_world world;
    auto &owner = enemy( -4, -2 );
    const auto id = create( request( "test_projection_bolt", owner,
                                     owner.pos_abs() + tripoint_rel_ms( 4, 0, 0 ) ) );
    const bool transition = GENERATE( false, true );
    if( transition ) {
        g->abilities().cancel_for_world_transition( "dimension_travel" );
    } else {
        get_creature_tracker().remove( owner );
        g->abilities().drain_starts();
    }
    CHECK( view( id ).state == "ended" );
    CHECK( counter( "projection_ends" ) == 1 );
    tick();
    CHECK( counter( "projection_ends" ) == 1 );
}

TEST_CASE( "power_projection_callback_exceptions_keep_costs_and_remove_only_the_failed_area",
           "[power_projections]" )
{
    projection_world world;
    auto &target = enemy( 1 );
    const auto survivor = create( request( "test_projection_area", get_avatar(),
                                           get_avatar().pos_abs() ) );
    observer events;
    events.callback = []() {
        CHECK_THROWS_AS( save(), JsonError );
        CHECK_THROWS_AS( g->abilities().cancel_for_world_transition( "fixture" ), JsonError );
        throw std::runtime_error( "projection callback" );
    };
    get_event_bus().subscribe( &events );
    auto r = request( "test_projection_callback", get_avatar(), get_avatar().pos_abs() );
    const auto out = get_projections().create( r );
    REQUIRE( out.id );
    CHECK_THROWS_AS( g->abilities().drain_starts(), std::runtime_error );
    CHECK( view( out.id ).state == "ended" );
    CHECK( target.get_hp() == 9999 );
    CHECK( view( survivor ).state == "active" );
    CHECK_NOTHROW( save() );
}

TEST_CASE( "power_projection_damage_callback_removal_stops_subsequent_beam_targets",
           "[power_projections]" )
{
    projection_world world;
    auto &owner = enemy( -4, -2 );
    auto &first = enemy( -2, -2 );
    auto &second = enemy( 0, -2 );
    const bool remove = GENERATE( false, true );
    observer events;
    bool ended = false;
    int64_t id = 0;
    events.callback = [&]() {
        if( !ended ) {
            ended = true;
            if( remove ) {
                get_creature_tracker().remove( owner );
            } else {
                CHECK( get_projections().end( id ) );
            }
        }
    };
    get_event_bus().subscribe( &events );
    id = create( request( "test_projection_ready_beam", owner, second.pos_abs() ) );
    tick();
    CHECK( first.get_hp() == 9990 );
    CHECK( second.get_hp() == 10000 );
    CHECK( view( id ).state == "ended" );
}

TEST_CASE( "power_projection_recursive_hooks_are_queued_and_bounded_by_ancestry",
           "[power_projections]" )
{
    projection_world world;
    auto &target = enemy( 1 );
    create( request( "test_projection_recursive", get_avatar(), target.pos_abs() ) );
    CHECK( get_projections().inspect().size() == 2 );
    for( int i = 0; i < 20; ++i ) {
        g->abilities().drain_starts();
    }
    CHECK( get_projections().inspect().size() == 9 );
    for( const auto &p : get_projections().inspect() ) {
        CHECK( p.depth <= 8 );
    }
}

TEST_CASE( "power_projection_terminal_hooks_cannot_recreate_their_family", "[power_projections]" )
{
    projection_world world;
    const auto id = create( request( "test_projection_terminal", get_avatar(),
                                     get_avatar().pos_abs() ) );
    REQUIRE( get_projections().end( id ) );
    g->abilities().drain_starts();
    CHECK( get_projections().inspect().size() == 1 );
    CHECK( view( id ).state == "ended" );
}

TEST_CASE( "power_projection_geometry_math_and_inspector_queries_are_pure", "[power_projections]" )
{
    projection_world world;
    auto &target = enemy( 1 );
    const auto id = create( request( "test_projection_area", get_avatar(), get_avatar().pos_abs() ) );
    dialogue d( get_talker_for( get_avatar() ), get_talker_for( target ) );
    math_exp query;
    REQUIRE( query.parse( "u_power_projection('test_projection_area') + n_power_area('test_projection_area', 'field': 'strength')" ) );
    const auto before = save();
    const auto rng = rng_get_engine();
    for( int i = 0; i < 3; ++i ) {
        CHECK( query.eval( d ) == 6 );
        CHECK_FALSE( execution_inspector::format_projection( view( id ) ).empty() );
        CHECK_FALSE( get_projections().tiles().empty() );
    }
    CHECK( rng_get_engine() == rng );
    CHECK( save() == before );
}

TEST_CASE( "power_projection_EOCs_validate_roles_handles_and_exact_source_dispelling",
           "[power_projections]" )
{
    projection_world world;
    auto &target = enemy( 3 );
    dialogue d( get_talker_for( get_avatar() ), get_talker_for( target ) );
    run_effect( R"({"effect":[{"create_power_projection":{"id":"test_projection_bolt"}}]})", d );
    CHECK( d.get_value( "reason" ) == "accepted" );
    run_effect( R"({"effect":[{"end_power_projection":{"context_val":"power_projection"}}]})", d );
    CHECK( d.get_value( "reason" ) == "dispelled" );
    g->abilities().drain_starts();
    run_effect(
        R"({"effect":[{"create_power_projection":{"id":"test_projection_bolt","strength":1.5}}]})", d );
    CHECK( d.get_value( "reason" ) == "invalid_strength" );
    dialogue absent( nullptr, get_talker_for( target ) );
    run_effect( R"({"effect":[{"create_power_projection":{"id":"test_projection_bolt"}}]})", absent );
    CHECK( absent.get_value( "reason" ) == "owner_unavailable" );
}

TEST_CASE( "power_projection_legacy_saves_without_objects_load_cleanly",
           "[power_projections][save]" )
{
    projection_world world;
    create( request( "test_projection_area", get_avatar(), get_avatar().pos_abs() ) );
    restore( R"({"next_id":1,"roots":[],"instances":[]})" );
    CHECK( get_projections().inspect().empty() );
}

TEST_CASE( "power_projection_malformed_saved_membership_and_counters_are_rejected_atomically",
           "[power_projections][save]" )
{
    projection_world world;
    enemy( 1 );
    const auto id = create( request( "test_projection_area", get_avatar(), get_avatar().pos_abs() ) );
    std::ostringstream buffer;
    JsonOut out( buffer );
    get_projections().serialize( out );
    const auto text = buffer.str();
    auto bad = text;
    const auto found = bad.find( "\"strength\":5" );
    REQUIRE( found != std::string::npos );
    bad.replace( found, std::string( "\"strength\":5" ).size(), "\"strength\":-1" );
    CHECK_THROWS_AS( get_projections().deserialize( json_loader::from_string( bad ) ), JsonError );
    CHECK( view( id ).state == "active" );
    bad = text;
    const std::string marker = "\"members\":[";
    const auto start = bad.find( marker );
    REQUIRE( start != std::string::npos );
    const auto last = bad.find( ']', start );
    REQUIRE( last != std::string::npos );
    bad.insert( last, "," + bad.substr( start + marker.size(), last - start - marker.size() ) );
    CHECK_THROWS_AS( get_projections().deserialize( json_loader::from_string( bad ) ), JsonError );
    CHECK( get_projections().area_value( get_avatar(),
                                         power_projection_id( "test_projection_area" ) ) == 0 );
}

TEST_CASE( "power_projection_invalid_definitions_and_unsupported_geometry_refuse_early",
           "[power_projections]" )
{
    projection_world world;
    power_projection def;
    CHECK_THROWS_AS( def.load( json_loader::from_string(
                                   R"({"name":"bad","kind":"projectile","speed":0})" ), "test" ), JsonError );
    const auto before = save();
    const auto result = get_projections().create( request( "test_projection_bolt", get_avatar(),
                        get_avatar().pos_abs() ) );
    CHECK( result.reason == "invalid_geometry" );
    CHECK( save() == before );
}


TEST_CASE( "power_moving_areas_follow_the_owners_loaded_z_level", "[power_projections]" )
{
    projection_world world;
    auto &owner = enemy( -4, -2 );
    const auto id = create( request( "test_projection_moving", owner, owner.pos_abs() ) );
    const auto upstairs = owner.pos_bub() + tripoint_rel_ms( 0, 0, 1 );
    get_map().ter_set( upstairs, ter_str_id( "t_floor" ) );
    owner.setpos( get_map(), upstairs );
    REQUIRE( owner.pos_abs().z() == 1 );
    g->abilities().drain_starts();
    restore( save() );
    CHECK( view( id ).state == "active" );
    REQUIRE_FALSE( view( id ).cells.empty() );
    for( const auto &point : view( id ).cells ) {
        CHECK( point.z() == 1 );
    }
}

TEST_CASE( "power_projection_damaged_payload_exceptions_keep_actual_health_loss_and_end_safely",
           "[power_projections]" )
{
    projection_world world;
    auto &target = enemy( 3 );
    const auto id = create( request( "test_projection_fast", get_avatar(), target.pos_abs() ) );
    observer events;
    events.callback = []() {
        throw std::runtime_error( "native packet fixture" );
    };
    get_event_bus().subscribe( &events );
    calendar::turn += 1_turns;
    CHECK_THROWS_AS( g->abilities().process(), std::runtime_error );
    CHECK( target.get_hp() == 9980 );
    CHECK( view( id ).state == "ended" );
    CHECK( view( id ).hp_loss == 20 );
    CHECK_NOTHROW( save() );
}

TEST_CASE( "power_projection_demo_items_and_abilities_use_the_native_routes",
           "[power_projections][powers_demo]" )
{
    projection_world world;
    const auto use = []( const std::string & id ) {
        item focus( itype_id( id ), calendar::turn );
        const auto it = focus.type->use_methods.find( "effect_on_conditions" );
        REQUIRE( it != focus.type->use_methods.end() );
        REQUIRE( it->second.get_actor_ptr() );
        it->second.get_actor_ptr()->use( &get_avatar(), focus, &get_map(), get_avatar().pos_bub() );
        g->abilities().drain_starts();
    };
    use( "power_demo_domain_projection_focus" );
    CHECK( get_projections().area_value( get_avatar(),
                                         power_projection_id( "power_demo_projection_aura" ) ) == 1 );
    use( "power_demo_domain_projection_focus" );
    CHECK( get_projections().area_value( get_avatar(),
                                         power_projection_id( "power_demo_projection_aura" ) ) == 0 );
    use( "power_demo_interceptor_projection_focus" );
    CHECK( get_projections().owned_value( get_avatar(),
                                          power_projection_id( "power_demo_projection_barrier" ) ) == 1 );
    use( "power_demo_interceptor_projection_focus" );
    CHECK( get_projections().owned_value( get_avatar(),
                                          power_projection_id( "power_demo_projection_barrier" ) ) == 0 );
    auto &target = enemy( 4 );
    dialogue d( get_talker_for( get_avatar() ), get_talker_for( target ) );
    const auto out = g->abilities().request( ability_id( "power_demo_bolt_projection_cast" ), d );
    INFO( out.reason );
    REQUIRE( out.id != 0 );
    g->abilities().process();
    g->abilities().drain_starts();
    CHECK( get_projections().owned_value( get_avatar(),
                                          power_projection_id( "power_demo_travelling_bolt" ) ) == 1 );
    CHECK( get_powers().balance( get_avatar(), "power_demo_focus_pool" ).total == 85 );
    const int hp = target.get_hp();
    tick( 4 );
    CHECK( target.get_hp() < hp );
}
