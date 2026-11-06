#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "ability.h"
#include "ability_runtime.h"
#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "condition.h"
#include "coordinates.h"
#include "creature.h"
#include "creature_ref.h"
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
#include "iuse.h"
#include "iuse_actor.h"
#include "json.h"
#include "json_loader.h"
#include "magic.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser.h"
#include "math_parser_diag_value.h"
#include "monster.h"
#include "npc.h"
#include "options_helpers.h"
#include "pimpl.h"
#include "player_helpers.h"
#include "point.h"
#include "power.h"
#include "power_runtime.h"
#include "rng.h"
#include "talker.h"
#include "type_id.h"

static const damage_type_id damage_pure( "pure" );

static const effect_on_condition_id
effect_on_condition_EOC_TEST_POWER_DAMAGE_HOOK( "EOC_TEST_POWER_DAMAGE_HOOK" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_POWER_RECURSIVE( "EOC_TEST_POWER_RECURSIVE" );

static const efftype_id effect_test_damage_observe( "test_damage_observe" );
static const efftype_id effect_test_damage_shield( "test_damage_shield" );
static const efftype_id effect_test_power_resource_shield( "test_power_resource_shield" );

static const itype_id itype_power_demo_mirror_focus( "power_demo_mirror_focus" );
static const itype_id itype_power_demo_ward_focus( "power_demo_ward_focus" );

static const power_application_id
power_application_power_demo_owned_mirror( "power_demo_owned_mirror" );
static const power_application_id
power_application_power_demo_owned_ward( "power_demo_owned_ward" );
static const power_application_id power_application_test_power_hook( "test_power_hook" );
static const power_application_id power_application_test_power_interrupt( "test_power_interrupt" );
static const power_application_id power_application_test_power_mirror( "test_power_mirror" );
static const power_application_id power_application_test_power_recursive( "test_power_recursive" );
static const power_application_id power_application_test_power_ward( "test_power_ward" );

static const power_cooldown_id power_cooldown_power_demo_force_group( "power_demo_force_group" );
static const power_cooldown_id power_cooldown_test_power_group( "test_power_group" );
static const power_cooldown_id power_cooldown_test_power_lock( "test_power_lock" );

namespace
{

struct power_world {
    time_point old_time = calendar::turn;
    override_option animations{ "ANIMATION_PROJECTILES", "false" };
    power_world() {
        g->abilities().clear();
        g->executions().clear();
        clear_avatar();
        clear_map_without_vision( -2, 1 );
        get_damage_transactions().release_settled_sources();
        get_globals().clear_global_values();
        calendar::turn = calendar::turn_zero + 12_hours;
        get_avatar().setpos( get_map(), tripoint_bub_ms( 60, 60, 0 ) );
        get_avatar().set_all_parts_hp_cur( 10000 );
        get_avatar().set_moves( 1000 );
    }
    ~power_world() {
        g->abilities().clear();
        g->executions().clear();
        clear_avatar();
        calendar::turn = old_time;
    }
};

monster &victim( int offset = 2 )
{
    monster &who = spawn_test_monster( "mon_execution_expanded_victim",
                                       get_avatar().pos_bub() + tripoint_rel_ms( offset, 0, 0 ) );
    who.set_hp( 10000 );
    return who;
}

Creature &kind( const std::string &name )
{
    if( name == "npc" ) {
        npc &who = spawn_npc( get_avatar().pos_bub().xy() + point_rel_ms( 0, -2 ), "thug" );
        who.set_all_parts_hp_cur( 10000 );
        return who;
    }
    if( name == "monster" ) {
        return victim( 4 );
    }
    return get_avatar();
}

void tick( int count = 1 )
{
    for( int i = 0; i < count; ++i ) {
        calendar::turn += 1_turns;
        g->abilities().process();
    }
}

power_application_request application( Creature &owner, Creature &target,
                                       const std::string &id = "test_power_ward" )
{
    return { power_application_id( id ), creature_ref::bind( owner ), creature_ref::bind( target ),
             std::nullopt, {} };
}

int64_t apply( Creature &owner, Creature &target, const std::string &id = "test_power_ward" )
{
    const power_application_admission result = get_powers().apply( application( owner, target, id ) );
    INFO( result.reason );
    REQUIRE( result.accepted() );
    return result.id;
}

power_application_view view( int64_t id )
{
    for( power_application_view const &v : get_powers().inspect_applications() ) {
        if( v.id == id ) {
            return v;
        }
    }
    FAIL( "Missing application " << id );
    return {};
}

ability_request request( Creature &source, Creature &target,
                         const std::string &id = "test_power_charged" )
{
    ability_request r;
    r.ability = ability_id( id );
    r.caster = creature_ref::bind( source );
    r.target = creature_ref::bind( target );
    return r;
}

ability_view action_view( int64_t id )
{
    const auto result = g->abilities().inspect( id );
    REQUIRE( result );
    return *result;
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

std::string save_accounts()
{
    std::ostringstream buffer;
    JsonOut out( buffer );
    get_powers().serialize( out );
    return buffer.str();
}

talk_effect_t effects( const std::string &text )
{
    talk_effect_t out( json_loader::from_string( text ), "effect", "power_ownership_test" );
    finalize_conditions();
    return out;
}

double counter( const std::string &name )
{
    const diag_value *value = get_globals().maybe_get_global_value( name );
    return value ? value->dbl() : 0;
}

double query( const std::string_view expression, Creature &alpha, Creature *beta = nullptr )
{
    math_exp math;
    REQUIRE( math.parse( expression ) );
    const_dialogue d( get_const_talker_for( alpha ), beta ? get_const_talker_for( *beta ) : nullptr );
    return math.eval( d );
}

struct events : event_subscriber {
    using event_subscriber::notify;
    std::function<void( const cata::event & )> callback;
    void notify( const cata::event &event ) override {
        callback( event );
    }
};
} // namespace

TEST_CASE( "power_custom_pools_are_real_independent_avatar_NPC_and_monster_accounts",
           "[power_ownership][powers]" )
{
    power_world world;
    const std::string name = GENERATE( "avatar", "npc", "monster" );
    Creature &actor = kind( name );
    CHECK( get_powers().balance( actor, "test_power_focus" ).available() == 100 );
    CHECK( get_powers().spend( actor, "test_power_focus", 35, "fixture" ) );
    CHECK( get_powers().balance( actor, "test_power_focus" ).available() == 65 );
    CHECK_FALSE( get_powers().spend( actor, "test_power_focus", 66, "fixture" ) );
    CHECK( get_powers().modify( actor, "test_power_focus", 10, "fixture" ) == 10 );
    CHECK( get_powers().balance( actor, "test_power_focus" ).available() == 75 );
    if( name != "avatar" ) {
        CHECK( get_powers().balance( get_avatar(), "test_power_focus" ).available() == 100 );
        CHECK( get_powers().balance( actor, "test_power_avatar_only" ).reason == "unsupported_resource" );
    }
}

TEST_CASE( "power_regeneration_has_no_banked_full_pool_credit_and_queries_do_not_materialize",
           "[power_ownership][powers]" )
{
    power_world world;
    avatar &actor = get_avatar();
    const std::string initial = save();
    // NOLINTNEXTLINE(cata-determinism)
    const cata_default_random_engine rng_before = rng_get_engine();
    CHECK( query( "u_power_resource('test_power_regen')", actor ) == 20 );
    CHECK( save() == initial );
    CHECK( rng_get_engine() == rng_before );
    CHECK( get_powers().inspect_pools().empty() );
    CHECK( get_powers().spend( actor, "test_power_regen", 10, "fixture" ) );
    tick();
    CHECK( get_powers().balance( actor, "test_power_regen" ).available() == 10 );
    tick();
    const std::string materialized = save();
    CHECK( get_powers().balance( actor, "test_power_regen" ).available() == 13 );
    CHECK( save() == materialized );
    calendar::turn += 100_turns;
    CHECK( get_powers().balance( actor, "test_power_regen" ).available() == 20 );
    REQUIRE( get_powers().spend( actor, "test_power_regen", 5, "fixture" ) );
    CHECK( get_powers().balance( actor, "test_power_regen" ).available() == 15 );
    tick();
    CHECK( get_powers().balance( actor, "test_power_regen" ).available() == 15 );
    tick();
    CHECK( get_powers().balance( actor, "test_power_regen" ).available() == 18 );
}

TEST_CASE( "power_reserved_custom_energy_is_shared_with_real_damage_defenses",
           "[power_ownership][powers][damage_transactions]" )
{
    power_world world;
    monster &target = victim();
    avatar &source = get_avatar();
    source.add_effect( effect_test_power_resource_shield, 1_hours );
    REQUIRE( get_powers().modify( source, "test_power_focus", -70, "fixture" ) == -70 );
    const ability_admission admitted = g->abilities().request( request( source, target ) );
    REQUIRE( admitted.accepted() );
    CHECK( get_powers().balance( source, "test_power_focus" ).total == 30 );
    CHECK( get_powers().balance( source, "test_power_focus" ).reserved == 20 );
    CHECK( get_powers().balance( source, "test_power_focus" ).available() == 10 );
    g->abilities().drain_starts();
    const damage_context_scope trace;
    source.deal_damage( &target, body_part_torso.id(), damage_instance( damage_pure, 25 ) );
    const auto packet = trace.result();
    REQUIRE( packet );
    CHECK( packet->absorbed == 10 );
    CHECK( packet->hp_loss == 15 );
    CHECK( get_powers().balance( source, "test_power_focus" ).total == 20 );
    tick( 2 );
    CHECK( action_view( admitted.id ).effect_completed );
    CHECK( action_view( admitted.id ).paid_energy == 20 );
    CHECK( action_view( admitted.id ).paid_charges == 1 );
    CHECK( get_powers().balance( source, "test_power_focus" ).total == 0 );
    CHECK( get_powers().inspect_reservations().empty() );
}

TEST_CASE( "power_reservations_for_native_mana_coordinate_shields_without_fake_refunds",
           "[power_ownership][powers]" )
{
    power_world world;
    avatar &source = get_avatar();
    monster &target = victim();
    source.magic->set_mana( 30 );
    source.add_effect( effect_test_damage_shield, 1_hours );
    const ability_admission admitted = g->abilities().request( request( source, target,
                                       "test_power_mana_reserve" ) );
    REQUIRE( admitted.accepted() );
    g->abilities().drain_starts();
    source.deal_damage( nullptr, body_part_torso.id(), damage_instance( damage_pure, 25 ) );
    CHECK( source.magic->available_mana() == 20 );
    CHECK( source.get_hp( body_part_torso.id() ) == 9985 );
    tick( 2 );
    CHECK( action_view( admitted.id ).paid_energy == 20 );
    CHECK( source.magic->available_mana() == 0 );
}

TEST_CASE( "power_cancelled_or_unaffordable_windups_release_claims_without_spending_charges",
           "[power_ownership][powers]" )
{
    power_world world;
    avatar &source = get_avatar();
    monster &target = victim();
    const std::string mode = GENERATE( "cancel", "before_release", "native_drain", "exception" );
    const bool mana = mode == "native_drain";
    if( mana ) {
        source.magic->set_mana( 30 );
    }
    const ability_admission admitted = g->abilities().request( request( source, target,
                                       mana ? "test_power_mana_reserve" : mode == "before_release" ?
                                       "test_power_cancel" : "test_power_charged" ) );
    REQUIRE( admitted.accepted() );
    g->abilities().drain_starts();
    if( mode == "cancel" ) {
        REQUIRE( g->abilities().cancel( admitted.id ) );
    } else if( mana ) {
        source.magic->set_mana( 5 );
    }
    events listener;
    listener.callback = [&]( const cata::event & event ) {
        if( mode == "exception" && event.type() == event_type::character_casts_spell ) {
            throw std::runtime_error( "power payment callback" );
        }
    };
    get_event_bus().subscribe( &listener );
    if( mode == "exception" ) {
        CHECK_THROWS_AS( tick( 2 ), std::runtime_error );
        CHECK( action_view( admitted.id ).paid_energy == 20 );
        CHECK( action_view( admitted.id ).paid_charges == 1 );
    } else {
        tick( 2 );
        CHECK_FALSE( action_view( admitted.id ).costs_committed );
        CHECK( get_powers().charges( source, power_cooldown_test_power_group ).charges == 2 );
        CHECK( get_powers().balance( source, "test_power_focus" ).total == 100 );
    }
    CHECK( get_powers().inspect_reservations().empty() );
    if( mana ) {
        CHECK( source.magic->available_mana() == 5 );
    }
}

TEST_CASE( "power_native_and_custom_costs_share_actor_support_and_reservation_rules",
           "[power_ownership][powers]" )
{
    power_world world;
    const std::string name = GENERATE( "npc", "monster" );
    Creature &actor = kind( name );
    monster &target = victim();
    const ability_admission admitted = g->abilities().request( request( actor, target ) );
    REQUIRE( admitted.accepted() );
    g->abilities().drain_starts();
    tick( 2 );
    CHECK( action_view( admitted.id ).effect_completed );
    CHECK( get_powers().balance( actor, "test_power_focus" ).total == 80 );
    CHECK( get_powers().balance( get_avatar(), "test_power_focus" ).total == 100 );
    CHECK( get_powers().balance( actor, "stamina" ).reason == "unsupported_resource" );
}

TEST_CASE( "power_shared_charge_groups_recharge_sequentially_and_effect_only_casts_remain_free",
           "[power_ownership][powers]" )
{
    power_world world;
    monster &target = victim();
    ability_request r = request( get_avatar(), target, "test_power_instant" );
    for( int i = 0; i < 2; ++i ) {
        REQUIRE( g->abilities().request( r ).accepted() );
        g->abilities().drain_starts();
    }
    CHECK( g->abilities().check( r ) == "no_charges" );
    CHECK( get_powers().charges( get_avatar(), power_cooldown_test_power_group ).charges == 0 );
    ability_request free = r;
    free.invocation = ability_invocation::effect_only;
    const ability_admission proc = g->abilities().request( free );
    REQUIRE( proc.accepted() );
    g->abilities().drain_starts();
    CHECK( action_view( proc.id ).paid_charges == 0 );
    CHECK( get_powers().charges( get_avatar(), power_cooldown_test_power_group ).charges == 0 );
    tick( 3 );
    CHECK( get_powers().charges( get_avatar(), power_cooldown_test_power_group ).charges == 1 );
    const std::string before = save();
    CHECK( query( "u_power_cooldown('test_power_group', 'field': 'recharge')", get_avatar() ) == 3 );
    CHECK( save() == before );
    tick( 3 );
    CHECK( get_powers().charges( get_avatar(), power_cooldown_test_power_group ).charges == 2 );
}

TEST_CASE( "power_group_cooldown_is_independent_of_recovering_a_charge",
           "[power_ownership][powers][eoc]" )
{
    power_world world;
    monster &target = victim();
    ability_request r = request( get_avatar(), target, "test_power_locked" );
    REQUIRE( g->abilities().request( r ).accepted() );
    g->abilities().drain_starts();
    CHECK( g->abilities().check( r ) == "cooldown" );
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    effects( R"({"effect":{"recover_power_charge":{"id":"test_power_lock"}}})" ).apply( d );
    CHECK( d.get_value( "power_charges_recovered" ).dbl() == 1 );
    CHECK( g->abilities().check( r ) == "cooldown" );
    tick( 2 );
    CHECK( g->abilities().check( r ).empty() );
    REQUIRE( g->abilities().request( r ).accepted() );
    g->abilities().drain_starts();
    CHECK( get_powers().charges( get_avatar(),
                                 power_cooldown_test_power_lock ).recharge_remaining == 5 );
}

TEST_CASE( "power_two_maintained_applications_disappear_independently_and_recover_one_charge_after_load",
           "[power_ownership][powers][save]" )
{
    power_world world;
    avatar &owner = get_avatar();
    monster &target = victim();
    const int64_t ward = apply( owner, owner );
    const int64_t mirror = apply( owner, owner, "test_power_mirror" );
    g->abilities().drain_starts();
    REQUIRE( view( ward ).state == "active" );
    REQUIRE( view( mirror ).state == "active" );
    for( int i = 0; i < 2; ++i ) {
        REQUIRE( g->abilities().request( request( owner, target, "test_power_instant" ) ).accepted() );
        g->abilities().drain_starts();
    }
    tick();
    CHECK( get_powers().balance( owner, "test_power_focus" ).total == 97 );
    const double starts = counter( "power_starts" );
    // NOLINTNEXTLINE(cata-determinism)
    const cata_default_random_engine rng_before = rng_get_engine();
    restore( save() );
    CHECK( rng_get_engine() == rng_before );
    CHECK( counter( "power_starts" ) == starts );
    CHECK( get_powers().application_value( owner, power_application_id() ) == 2 );
    REQUIRE( get_powers().end( ward ) );
    CHECK( get_powers().application_value( owner, power_application_test_power_ward ) == 0 );
    CHECK( get_powers().application_value( owner, power_application_test_power_mirror ) == 1 );
    g->abilities().drain_starts();
    CHECK( counter( "power_ends" ) == 1 );
    tick();
    CHECK( get_powers().balance( owner, "test_power_focus" ).total == 95 );
    tick();
    CHECK( get_powers().charges( owner, power_cooldown_test_power_group ).charges == 1 );
    CHECK( view( mirror ).tick_count == 3 );
    CHECK( view( ward ).state == "ended" );
}

TEST_CASE( "power_two_casters_same_ward_preserve_surviving_native_combat_contribution",
           "[power_ownership][powers][damage_transactions]" )
{
    power_world world;
    avatar &a = get_avatar();
    npc &b = spawn_npc( a.pos_bub().xy() + point_rel_ms( 0, -2 ), "thug" );
    monster &target = victim();
    target.add_effect( effect_test_damage_observe, 1_hours );
    const int64_t first = apply( a, target );
    const int64_t second = apply( b, target );
    g->abilities().drain_starts();
    target.deal_damage( &a, body_part_torso.id(), damage_instance( damage_pure, 20 ) );
    CHECK( target.get_hp() == 9994 );
    CHECK( query( "n_power_application('test_power_ward', 'owner': 'u')", a, &target ) == 1 );
    CHECK( get_powers().dispel( creature_ref::bind( target ), power_application_test_power_ward,
                                creature_ref::bind( a ) ) == 1 );
    CHECK( get_powers().application_value( target, power_application_test_power_ward,
                                           true ) == 7 );
    target.deal_damage( &a, body_part_torso.id(), damage_instance( damage_pure, 20 ) );
    CHECK( target.get_hp() == 9981 );
    g->abilities().drain_starts();
    CHECK( view( first ).state == "ended" );
    CHECK( view( second ).state == "active" );
    CHECK( target.has_effect( effect_test_damage_observe ) );
    CHECK( a.get_value( "power_owner_seen" ).dbl() == 1 );
    CHECK( b.get_value( "power_owner_seen" ).dbl() == 1 );
    CHECK( target.get_value( "power_target_seen" ).dbl() == 2 );
}

TEST_CASE( "power_application_start_upkeep_expiry_and_repeated_pumps_are_exactly_once",
           "[power_ownership][powers]" )
{
    power_world world;
    const int64_t id = apply( get_avatar(), get_avatar(), "test_power_short" );
    g->abilities().drain_starts();
    CHECK( get_powers().balance( get_avatar(), "test_power_focus" ).total == 95 );
    for( int i = 0; i < 3; ++i ) {
        g->abilities().process();
    }
    CHECK( counter( "power_starts" ) == 1 );
    CHECK( counter( "power_ticks" ) == 0 );
    tick( 2 );
    CHECK( get_powers().balance( get_avatar(), "test_power_focus" ).total == 93 );
    CHECK( view( id ).context.at( "ticks" ).dbl() == 2 );
    tick();
    CHECK( view( id ).reason == "expired" );
    CHECK( counter( "power_ends" ) == 1 );
    tick( 3 );
    CHECK( counter( "power_ends" ) == 1 );
    CHECK( get_powers().balance( get_avatar(), "test_power_focus" ).total == 93 );
}

TEST_CASE( "power_unaffordable_upkeep_ends_only_the_application_that_cannot_pay",
           "[power_ownership][powers]" )
{
    power_world world;
    const int64_t first = apply( get_avatar(), get_avatar() );
    const int64_t second = apply( get_avatar(), get_avatar(), "test_power_mirror" );
    g->abilities().drain_starts();
    REQUIRE( get_powers().modify( get_avatar(), "test_power_focus", -98, "fixture" ) == -98 );
    tick();
    CHECK( view( first ).state == "active" );
    CHECK( view( second ).reason == "upkeep_unaffordable" );
    CHECK( view( first ).paid_upkeep == 1 );
    CHECK( view( second ).paid_upkeep == 0 );
    CHECK( get_powers().balance( get_avatar(), "test_power_focus" ).total == 1 );
}

TEST_CASE( "power_cancelling_an_application_parent_ends_all_owned_children_without_recreation",
           "[power_ownership][powers]" )
{
    power_world world;
    const int64_t root = apply( get_avatar(), get_avatar(), "test_power_recursive" );
    for( int i = 0; i < 12; ++i ) {
        g->abilities().drain_starts();
    }
    REQUIRE( get_powers().inspect_applications().size() == 9 );
    REQUIRE( get_powers().application_value( get_avatar(),
             power_application_test_power_recursive ) == 9 );
    REQUIRE( get_powers().end( root ) );
    CHECK( get_powers().application_value( get_avatar(),
                                           power_application_test_power_recursive ) == 0 );
    g->abilities().drain_starts();
    for( power_application_view const &v : get_powers().inspect_applications() ) {
        CHECK( v.state == "ended" );
    }
    CHECK( counter( "power_ends" ) == 9 );
    CHECK_NOTHROW( restore( save() ) );
    tick( 2 );
    CHECK( get_powers().inspect_applications().empty() );
}

TEST_CASE( "power_damage_and_displacement_interrupt_only_declared_concentration",
           "[power_ownership][powers]" )
{
    power_world world;
    const bool damage = GENERATE( false, true );
    const int64_t passive = apply( get_avatar(), get_avatar() );
    const int64_t concentrated = apply( get_avatar(), get_avatar(), "test_power_interrupt" );
    g->abilities().drain_starts();
    if( damage ) {
        get_avatar().apply_damage( nullptr, body_part_torso.id(), 1 );
    } else {
        get_avatar().setpos( get_map(), get_avatar().pos_bub() + tripoint_rel_ms::west );
    }
    CHECK( get_powers().application_value( get_avatar(),
                                           power_application_test_power_interrupt ) == 0 );
    tick();
    CHECK( view( passive ).state == "active" );
    CHECK( view( concentrated ).reason == ( damage ? "damaged" : "owner_displaced" ) );
}

TEST_CASE( "power_lost_owner_or_target_does_not_transfer_to_a_replacement_creature",
           "[power_ownership][powers]" )
{
    power_world world;
    monster &owner = victim( 2 );
    monster &target = victim( 4 );
    const bool remove_owner = GENERATE( false, true );
    const int64_t id = apply( owner, target );
    g->abilities().drain_starts();
    monster &removed = remove_owner ? owner : target;
    const tripoint_bub_ms position = removed.pos_bub();
    const creature_ref old_ref = creature_ref::bind( removed );
    get_creature_tracker().remove( removed );
    monster &replacement = spawn_test_monster( "mon_execution_expanded_victim", position );
    CHECK_FALSE( creature_ref::bind( replacement ) == old_ref );
    tick();
    CHECK( view( id ).reason == ( remove_owner ? "owner_unavailable" : "target_unavailable" ) );
    CHECK( get_powers().application_value( replacement, power_application_id() ) == 0 );
    CHECK( get_powers().balance( replacement, "test_power_focus" ).total == 100 );
    CHECK( counter( "power_ends" ) == 1 );
}

TEST_CASE( "power_application_callback_removal_revalidates_held_roles_without_avatar_fallback",
           "[power_ownership][powers]" )
{
    power_world world;
    monster &owner = victim();
    const creature_ref identity = creature_ref::bind( owner );
    const int64_t id = apply( owner, get_avatar(), "test_power_hook" );
    g->abilities().drain_starts();
    events listener;
    listener.callback = [&]( const cata::event & event ) {
        if( event.type() == event_type::monster_takes_damage ) {
            get_creature_tracker().remove( owner );
        }
    };
    get_event_bus().subscribe( &listener );
    tick();
    CHECK( view( id ).reason == "owner_unavailable" );
    CHECK_FALSE( identity.resolve_loaded() );
    CHECK( get_powers().balance( get_avatar(), "test_power_focus" ).total == 100 );
    CHECK( counter( "power_ends" ) == 1 );
}

TEST_CASE( "power_callback_exceptions_keep_committed_costs_and_cleanup_once",
           "[power_ownership][powers]" )
{
    power_world world;
    const int64_t id = apply( get_avatar(), get_avatar(), "test_power_hook" );
    g->abilities().drain_starts();
    events listener;
    listener.callback = [&]( const cata::event & event ) {
        if( event.type() == event_type::character_takes_damage ) {
            CHECK( g->abilities().processing() );
            CHECK_THROWS_AS( save(), JsonError );
            CHECK_THROWS_AS( save_accounts(), JsonError );
            CHECK_THROWS_AS( get_powers().clear(), JsonError );
            throw std::runtime_error( "power hook exception" );
        }
    };
    get_event_bus().subscribe( &listener );
    CHECK_THROWS_AS( tick(), std::runtime_error );
    CHECK_FALSE( g->abilities().processing() );
    CHECK( view( id ).state == "failed" );
    CHECK( view( id ).paid_start == 5 );
    CHECK( view( id ).paid_upkeep == 2 );
    CHECK( get_powers().balance( get_avatar(), "test_power_focus" ).total == 93 );
    CHECK( counter( "power_ends" ) == 1 );
    CHECK( get_powers().application_value( get_avatar(), power_application_id() ) == 0 );
    CHECK_NOTHROW( save() );
    tick( 2 );
    CHECK( counter( "power_ends" ) == 1 );
}

TEST_CASE( "power_time_jumps_end_sustains_without_replaying_upkeep_or_tick_hooks",
           "[power_ownership][powers]" )
{
    power_world world;
    const int64_t id = apply( get_avatar(), get_avatar() );
    g->abilities().drain_starts();
    REQUIRE( get_powers().use_charge( get_avatar(), power_cooldown_test_power_group, 2, "fixture" ) );
    REQUIRE( get_powers().spend( get_avatar(), "test_power_regen", 20, "fixture" ) );
    calendar::turn += 1000_turns;
    g->abilities().process();
    CHECK( view( id ).reason == "time_skipped" );
    CHECK( counter( "power_ticks" ) == 0 );
    CHECK( view( id ).paid_upkeep == 0 );
    CHECK( get_powers().balance( get_avatar(), "test_power_regen" ).total == 20 );
    CHECK( get_powers().charges( get_avatar(), power_cooldown_test_power_group ).charges == 2 );
    CHECK( counter( "power_ends" ) == 1 );
}

TEST_CASE( "power_pending_and_active_save_load_preserve_handles_deadlines_and_no_duplicate_payment",
           "[power_ownership][powers][save]" )
{
    power_world world;
    const bool started = GENERATE( false, true );
    const int64_t id = apply( get_avatar(), get_avatar(), "test_power_short" );
    if( started ) {
        g->abilities().drain_starts();
        tick();
    }
    const power_application_view before = view( id );
    restore( save() );
    CHECK( view( id ).state == before.state );
    CHECK( view( id ).next_tick == before.next_tick );
    CHECK( view( id ).expires == before.expires );
    CHECK( view( id ).paid_start == before.paid_start );
    CHECK( view( id ).paid_upkeep == before.paid_upkeep );
    g->abilities().drain_starts();
    CHECK( counter( "power_starts" ) == 1 );
    CHECK( get_powers().balance( get_avatar(), "test_power_focus" ).total == ( started ? 94 : 95 ) );
    tick();
    CHECK( view( id ).paid_upkeep == ( started ? 2 : 1 ) );
}

TEST_CASE( "power_saved_windup_reservations_are_restored_and_protect_the_same_actor",
           "[power_ownership][powers][save]" )
{
    power_world world;
    monster &target = victim();
    const ability_admission admitted = g->abilities().request( request( get_avatar(), target ) );
    REQUIRE( admitted.accepted() );
    g->abilities().drain_starts();
    tick();
    restore( save() );
    CHECK( action_view( admitted.id ).energy_reserved );
    CHECK( get_powers().balance( get_avatar(), "test_power_focus" ).reserved == 20 );
    CHECK( get_powers().modify( get_avatar(), "test_power_focus", -100, "fixture" ) == -80 );
    CHECK( get_powers().balance( get_avatar(), "test_power_focus" ).total == 20 );
    tick();
    CHECK( action_view( admitted.id ).effect_completed );
    CHECK( get_powers().balance( get_avatar(), "test_power_focus" ).total == 0 );
    CHECK( get_powers().inspect_reservations().empty() );
}

TEST_CASE( "power_application_world_transition_blocks_cross_runtime_terminal_recreation",
           "[power_ownership][powers][executions]" )
{
    power_world world;
    monster &target = victim();
    const int64_t app = apply( get_avatar(), target, "test_power_transition" );
    g->abilities().drain_starts();
    REQUIRE( g->abilities().request( request( get_avatar(), target ) ).accepted() );
    g->abilities().drain_starts();
    g->abilities().cancel_for_world_transition( "dimension_changed" );
    CHECK( view( app ).reason == "dimension_changed" );
    CHECK( get_powers().inspect_applications().size() == 1 );
    CHECK( get_powers().inspect_reservations().empty() );
    CHECK_FALSE( g->abilities().controls( get_avatar() ) );
    for( ability_view const &v : g->abilities().inspect_all() ) {
        CHECK( v.state == "interrupted" );
    }
    for( execution_inspection const &scene : g->executions().inspect_all() ) {
        CHECK( scene.view.reason == "dimension_changed" );
    }
    tick( 2 );
    CHECK( get_powers().inspect_applications().size() == 1 );
    CHECK( target.get_hp() == 10000 );
}

TEST_CASE( "power_EOC_actor_selection_validation_and_source_specific_dispelling_are_explicit",
           "[power_ownership][powers][eoc]" )
{
    power_world world;
    monster &target = victim();
    dialogue d( get_talker_for( get_avatar() ), get_talker_for( target ) );
    effects( R"({"effect":{"apply_power_application":{"id":"test_power_ward","target":"npc","magnitude":9}}})" ).apply(
        d );
    REQUIRE( d.get_value( "reason" ) == "accepted" );
    g->abilities().drain_starts();
    CHECK( query( "n_power_application('test_power_ward', 'field': 'magnitude', 'owner': 'u')",
                  get_avatar(), &target ) == 9 );
    effects( R"({"effect":{"modify_power_resource":{"id":"test_power_focus","actor":"npc","amount":-25}}})" ).apply(
        d );
    CHECK( d.get_value( "power_resource_changed" ).dbl() == -25 );
    CHECK( get_powers().balance( target, "test_power_focus" ).total == 75 );
    CHECK( get_powers().balance( get_avatar(), "test_power_focus" ).total == 100 );
    effects( R"({"effect":{"dispel_power_applications":{"target":"npc","owner":"u","tag":"ward"}}})" ).apply(
        d );
    CHECK( d.get_value( "power_dispelled" ).dbl() == 1 );
    CHECK( get_powers().application_value( target, power_application_id() ) == 0 );
}

TEST_CASE( "power_EOC_refuses_missing_roles_and_nonfinite_fractional_or_out_of_range_mutations",
           "[power_ownership][powers][eoc]" )
{
    power_world world;
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    const std::string mode = GENERATE( "missing", "fractional", "invalid_group", "unknown_resource" );
    if( mode == "missing" ) {
        effects( R"({"effect":{"apply_power_application":{"id":"test_power_ward","owner":"npc"}}})" ).apply(
            d );
        CHECK( d.get_value( "reason" ) == "participant_unavailable" );
    } else if( mode == "fractional" ) {
        effects( R"({"effect":{"modify_power_resource":{"id":"test_power_focus","amount":1.5}}})" ).apply(
            d );
        CHECK( d.get_value( "reason" ) == "invalid_amount" );
    } else if( mode == "invalid_group" ) {
        effects( R"({"effect":{"recover_power_charge":{"id":"missing_group"}}})" ).apply( d );
        CHECK( d.get_value( "reason" ) == "invalid_cooldown" );
    } else {
        effects( R"({"effect":{"modify_power_resource":{"id":"missing_pool","amount":5}}})" ).apply( d );
        CHECK( d.get_value( "reason" ) == "unsupported_resource" );
    }
    CHECK( get_powers().inspect_applications().empty() );
    CHECK( get_powers().inspect_pools().empty() );
    CHECK( get_powers().inspect_cooldowns().empty() );
}

TEST_CASE( "power_queries_and_inspection_do_not_run_hooks_mutate_accounts_or_consume_randomness",
           "[power_ownership][powers]" )
{
    power_world world;
    monster &target = victim();
    const int64_t id = apply( get_avatar(), target );
    const bool active = GENERATE( false, true );
    if( active ) {
        g->abilities().drain_starts();
        get_powers().spend( get_avatar(), "test_power_regen", 10, "fixture" );
        get_powers().use_charge( get_avatar(), power_cooldown_test_power_group, 1, "fixture" );
    }
    const std::string before = save();
    // NOLINTNEXTLINE(cata-determinism)
    const cata_default_random_engine rng_before = rng_get_engine();
    const time_point time_before = calendar::turn;
    const double starts = counter( "power_starts" );
    for( int i = 0; i < 3; ++i ) {
        CHECK_FALSE( execution_inspector::format_application( view( id ) ).empty() );
        CHECK_FALSE( execution_inspector::format_power_accounts( get_powers() ).empty() );
        CHECK( query( "u_power_resource('test_power_regen')", get_avatar() ) == ( active ? 10 : 20 ) );
        CHECK( query( "u_power_cooldown('test_power_group')", get_avatar() ) == ( active ? 1 : 2 ) );
        CHECK( query( "n_power_application('test_power_ward')", get_avatar(),
                      &target ) == ( active ? 1 : 0 ) );
        CHECK( g->abilities().check( request( get_avatar(), target ) ).empty() );
    }
    CHECK( save() == before );
    CHECK( rng_get_engine() == rng_before );
    CHECK( calendar::turn == time_before );
    CHECK( counter( "power_starts" ) == starts );
}

TEST_CASE( "power_actor_accounts_preserve_avatar_capacity_across_dimension_snapshots",
           "[power_ownership][powers][save]" )
{
    power_world world;
    REQUIRE( get_powers().spend( get_avatar(), "test_power_focus", 37, "fixture" ) );
    REQUIRE( get_powers().use_charge( get_avatar(), power_cooldown_test_power_group, 1, "fixture" ) );
    std::string text = save_accounts();
    size_t offset = 0;
    while( ( offset = text.find( "\"dimension\"", offset ) ) != std::string::npos ) {
        const std::size_t colon = text.find( ':', offset );
        const std::size_t start = text.find( '"', colon );
        const std::size_t end = text.find( '"', start + 1 );
        REQUIRE( end != std::string::npos );
        text.replace( start + 1, end - start - 1, "power_test_old_dimension" );
        offset = start + 1 + std::string( "power_test_old_dimension" ).size();
    }
    get_powers().deserialize( json_loader::from_string( text ) );
    CHECK( get_powers().balance( get_avatar(), "test_power_focus" ).total == 63 );
    CHECK( get_powers().charges( get_avatar(), power_cooldown_test_power_group ).charges == 1 );
}

TEST_CASE( "power_ended_history_and_accounting_receipts_are_bounded",
           "[power_ownership][powers]" )
{
    power_world world;
    for( int i = 0; i < 40; ++i ) {
        const int64_t id = apply( get_avatar(), get_avatar() );
        g->abilities().drain_starts();
        REQUIRE( get_powers().end( id ) );
        g->abilities().drain_starts();
        get_powers().modify( get_avatar(), "test_power_focus", -1, "fixture" );
        get_powers().modify( get_avatar(), "test_power_focus", 1, "fixture" );
    }
    CHECK( get_powers().inspect_applications().size() == 32 );
    CHECK( get_powers().history().size() == 64 );
    restore( save() );
    CHECK( get_powers().inspect_applications().empty() );
    CHECK( get_powers().history().empty() );
    CHECK( get_powers().balance( get_avatar(), "test_power_focus" ).total == 100 );
}

TEST_CASE( "power_invalid_serialized_application_states_are_rejected_atomically",
           "[power_ownership][powers][save]" )
{
    power_world world;
    const int64_t id = apply( get_avatar(), get_avatar() );
    g->abilities().drain_starts();
    std::string bad = save_accounts();
    const std::size_t pos = bad.find( "\"active\"" );
    REQUIRE( pos != std::string::npos );
    bad.replace( pos, 8, "\"ending\"" );
    CHECK_THROWS_AS( get_powers().deserialize( json_loader::from_string( bad ) ), JsonError );
    CHECK( view( id ).state == "active" );
    CHECK( get_powers().application_value( get_avatar(), power_application_id() ) == 1 );
}

TEST_CASE( "power_loader_rejects_invalid_intervals_costs_and_unbounded_capacity",
           "[power_ownership][powers][json]" )
{
    const std::string kind = GENERATE( "resource", "cooldown", "application" );
    if( kind == "resource" ) {
        power_resource def;
        CHECK_THROWS_AS( def.load( json_loader::from_string( R"({"name":"invalid","capacity":0})" ),
                                   "test_data" ), JsonError );
    } else if( kind == "cooldown" ) {
        power_cooldown def;
        CHECK_THROWS_AS( def.load( json_loader::from_string( R"({"name":"invalid","recharge_ticks":0})" ),
                                   "test_data" ), JsonError );
    } else {
        power_application def;
        CHECK_THROWS_AS( def.load( json_loader::from_string(
                                       R"({"name":"invalid","upkeep":{"resource":"none","amount":1}})" ), "test_data" ), JsonError );
    }
}

TEST_CASE( "power_concentration_incapatitation_and_definition_changes_end_owned_contributions",
           "[power_ownership][powers]" )
{
    power_world world;
    const bool changed = GENERATE( false, true );
    const int64_t id = apply( get_avatar(), get_avatar(), "test_power_interrupt" );
    g->abilities().drain_starts();
    power_application &definition = const_cast<power_application &>
                                    ( *power_application_test_power_interrupt );
    const int revision = definition.revision;
    const on_out_of_scope reset( [&]() {
        definition.revision = revision;
    } );
    if( changed ) {
        ++definition.revision;
    } else {
        REQUIRE( incapacitation::apply( get_avatar(), nullptr, 10_turns, false ) == "applied" );
    }
    tick();
    CHECK( view( id ).reason == ( changed ? "incompatible_definition" : "owner_incapacitated" ) );
    CHECK( get_powers().application_value( get_avatar(), power_application_id() ) == 0 );
}

TEST_CASE( "power_saved_orphan_reservations_are_rejected_instead_of_locking_an_actor_pool",
           "[power_ownership][powers][save]" )
{
    power_world world;
    monster &target = victim();
    REQUIRE( g->abilities().request( request( get_avatar(), target ) ).accepted() );
    std::string bad = save();
    const std::size_t field = bad.find( "\"energy_reserved\"" );
    REQUIRE( field != std::string::npos );
    const std::size_t value = bad.find( "true", field );
    REQUIRE( value != std::string::npos );
    bad.replace( value, 4, "false" );
    CHECK_THROWS_AS( g->abilities().deserialize( json_loader::from_string( bad ) ), JsonError );
}

TEST_CASE( "power_legacy_ability_save_without_power_accounts_remains_loadable",
           "[power_ownership][powers][save]" )
{
    power_world world;
    monster &target = victim();
    const ability_admission admitted = g->abilities().request( request( get_avatar(), target,
                                       "test_ability_strike" ) );
    REQUIRE( admitted.accepted() );
    std::string text = save();
    const std::size_t field = text.find( "\"powers\"" );
    REQUIRE( field != std::string::npos );
    const std::size_t start = text.find( '{', field );
    int depth = 0;
    bool quoted = false;
    bool escaped = false;
    size_t end = start;
    for( ; end < text.size(); ++end ) {
        const char c = text[end];
        if( quoted ) {
            if( escaped ) {
                escaped = false;
            } else if( c == '\\' ) {
                escaped = true;
            } else if( c == '"' ) {
                quoted = false;
            }
        } else if( c == '"' ) {
            quoted = true;
        } else if( c == '{' ) {
            ++depth;
        } else if( c == '}' && --depth == 0 ) {
            break;
        }
    }
    REQUIRE( end < text.size() );
    const std::size_t comma = text.find( ',', end );
    REQUIRE( comma != std::string::npos );
    text.erase( field, comma - field + 1 );
    restore( text );
    g->abilities().drain_starts();
    tick( 2 );
    CHECK( action_view( admitted.id ).effect_completed );
    CHECK( target.get_hp() == 9988 );
}

TEST_CASE( "power_finalized_native_death_retires_actor_accounts_without_running_cleanup_inside_death",
           "[power_ownership][powers]" )
{
    power_world world;
    const std::string name = GENERATE( "npc", "monster" );
    Creature &actor = kind( name );
    const auto hold = g->shared_from( actor );
    REQUIRE( hold );
    const creature_ref ref = creature_ref::bind( actor );
    REQUIRE( get_powers().spend( actor, "test_power_focus", 25, "fixture" ) );
    REQUIRE( get_powers().use_charge( actor, power_cooldown_test_power_group, 1, "fixture" ) );
    const int64_t id = apply( actor, actor );
    g->abilities().drain_starts();
    actor.apply_damage( &get_avatar(), body_part_torso.id(), 20000 );
    g->cleanup_dead();
    CHECK_FALSE( ref.resolve_loaded() );
    CHECK( get_powers().inspect_pools().empty() );
    CHECK( get_powers().inspect_cooldowns().empty() );
    CHECK( counter( "power_ends" ) == 0 );
    g->abilities().drain_starts();
    CHECK( view( id ).reason == "owner_unavailable" );
    CHECK( counter( "power_ends" ) == 1 );
}

TEST_CASE( "power_upkeep_rejects_unsupported_actor_adapters_before_granting_a_free_interval",
           "[power_ownership][powers]" )
{
    power_world world;
    const std::string name = GENERATE( "avatar", "npc", "monster" );
    Creature &owner = kind( name );
    power_application &definition = const_cast<power_application &>
                                    ( *power_application_test_power_ward );
    const power_cost old_upkeep = definition.upkeep;
    const on_out_of_scope reset( [&]() {
        definition.upkeep = old_upkeep;
    } );
    definition.upkeep.resource = "mana";
    definition.upkeep.amount = 1;
    if( Character *person = owner.as_character() ) {
        person->magic->set_mana( 10 );
    }
    const power_application_admission result = get_powers().apply( application( owner, owner ) );
    if( name == "monster" ) {
        CHECK_FALSE( result.accepted() );
        CHECK( result.reason == "unsupported_resource" );
        CHECK( get_powers().inspect_applications().empty() );
    } else {
        REQUIRE( result.accepted() );
        g->abilities().drain_starts();
        tick();
        CHECK( owner.as_character()->magic->available_mana() == 9 );
        CHECK( view( result.id ).paid_upkeep == 1 );
    }
}

TEST_CASE( "power_demo_native_item_toggles_compose_and_charged_strikes_recover_one_charge",
           "[power_ownership][powers][iuse]" )
{
    // This checks the powers_demo mod's own items, so it runs only with that mod loaded, as in
    // CI's mods job.
    if( !itype_power_demo_ward_focus.is_valid() ) {
        return;
    }
    power_world world;
    map &here = get_map();
    avatar &owner = get_avatar();
    monster &target = victim();
    item &ward = here.add_item( owner.pos_bub(), item( itype_power_demo_ward_focus ) );
    item &mirror = here.add_item( owner.pos_bub() + tripoint_rel_ms::west,
                                  item( itype_power_demo_mirror_focus ) );
    const auto use = [&]( item & focus, const tripoint_bub_ms & position ) {
        const auto *method = focus.type->get_use( "effect_on_conditions" );
        REQUIRE( method );
        const auto *actor = dynamic_cast<const effect_on_conditions_actor *>( method->get_actor_ptr() );
        REQUIRE( actor );
        actor->use( &owner, focus, &here, position );
        g->abilities().drain_starts();
    };
    use( ward, owner.pos_bub() );
    use( mirror, owner.pos_bub() + tripoint_rel_ms::west );
    CHECK( get_powers().application_value( owner,
                                           power_application_power_demo_owned_ward ) == 1 );
    CHECK( get_powers().application_value( owner,
                                           power_application_power_demo_owned_mirror ) == 1 );
    CHECK( get_powers().balance( owner, "power_demo_focus_pool" ).total == 90 );
    use( ward, owner.pos_bub() );
    CHECK( get_powers().application_value( owner,
                                           power_application_power_demo_owned_ward ) == 0 );
    CHECK( get_powers().application_value( owner,
                                           power_application_power_demo_owned_mirror ) == 1 );
    const ability_admission strike = g->abilities().request( request( owner, target,
                                     "power_demo_charged_strike" ) );
    REQUIRE( strike.accepted() );
    const int windup = action_view( strike.id ).windup_ticks;
    g->abilities().drain_starts();
    tick( windup );
    CHECK( action_view( strike.id ).effect_completed );
    CHECK( action_view( strike.id ).paid_energy == 10 );
    CHECK( get_powers().charges( owner, power_cooldown_power_demo_force_group ).charges == 1 );
    tick( 5 );
    CHECK( get_powers().charges( owner, power_cooldown_power_demo_force_group ).charges == 2 );
}

TEST_CASE( "power_reservation_tokens_cannot_release_another_actor_or_partial_cost_claim",
           "[power_ownership][powers]" )
{
    power_world world;
    monster &target = victim();
    npc &other = spawn_npc( get_avatar().pos_bub().xy() + point_rel_ms( 0, -2 ), "thug" );
    const ability_admission admitted = g->abilities().request( request( get_avatar(), target ) );
    REQUIRE( admitted.accepted() );
    CHECK_FALSE( get_powers().spend( other, "test_power_focus", 20, "wrong_actor", admitted.id ) );
    CHECK_FALSE( get_powers().spend( get_avatar(), "test_power_focus", 10, "partial_claim",
                                     admitted.id ) );
    CHECK( get_powers().balance( other, "test_power_focus" ).total == 100 );
    CHECK( get_powers().balance( get_avatar(), "test_power_focus" ).reserved == 20 );
    g->abilities().drain_starts();
    tick( 2 );
    CHECK( action_view( admitted.id ).effect_completed );
    CHECK( get_powers().balance( get_avatar(), "test_power_focus" ).total == 80 );
    CHECK( get_powers().inspect_reservations().empty() );
}

TEST_CASE( "power_parent_callback_failure_removes_child_contributions_and_defers_cleanup_before_save",
           "[power_ownership][powers][save]" )
{
    power_world world;
    power_application &definition = const_cast<power_application &>
                                    ( *power_application_test_power_hook );
    const auto original = definition.on_tick;
    const on_out_of_scope reset( [&]() {
        definition.on_tick = original;
    } );
    definition.on_tick = { effect_on_condition_EOC_TEST_POWER_RECURSIVE,
                           effect_on_condition_EOC_TEST_POWER_DAMAGE_HOOK
                         };
    const int64_t parent = apply( get_avatar(), get_avatar(), "test_power_hook" );
    g->abilities().drain_starts();
    events listener;
    listener.callback = [&]( const cata::event & event ) {
        if( event.type() == event_type::character_takes_damage ) {
            throw std::runtime_error( "parent power callback" );
        }
    };
    get_event_bus().subscribe( &listener );
    CHECK_THROWS_AS( tick(), std::runtime_error );
    CHECK( view( parent ).state == "failed" );
    CHECK( get_powers().application_value( get_avatar(), power_application_id() ) == 0 );
    CHECK_THROWS_AS( save(), JsonError );
    g->abilities().drain_starts();
    CHECK_NOTHROW( restore( save() ) );
    CHECK( get_powers().inspect_applications().empty() );
    CHECK( counter( "power_ends" ) == 2 );
    CHECK( get_powers().balance( get_avatar(), "test_power_focus" ).total == 93 );
}
