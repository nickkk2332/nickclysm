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
#include "magic.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "mattack_common.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "options_helpers.h"
#include "player_helpers.h"
#include "rng.h"
#include "talker.h"
#include "type_id.h"
#include "iuse.h"

namespace
{
struct ability_world {
    time_point old_time = calendar::turn;
    override_option animations{ "ANIMATION_PROJECTILES", "false" };
    ability_world() {
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
    ~ability_world() {
        g->abilities().clear();
        g->executions().clear();
        clear_avatar();
        calendar::turn = old_time;
    }
};

monster &victim( const Creature &source )
{
    monster &target = spawn_test_monster( "mon_execution_expanded_victim",
                                          source.pos_bub() + tripoint_rel_ms( 2, 0, 0 ) );
    target.set_hp( 10000 );
    return target;
}

ability_request request_for( Creature &source, Creature &target,
                             const std::string &id = "test_ability_strike" )
{
    ability_request request;
    request.ability = ability_id( id );
    request.caster = creature_ref::bind( source );
    request.target = creature_ref::bind( target );
    return request;
}

int64_t start( Creature &source, Creature &target, const std::string &id = "test_ability_strike" )
{
    const auto result = g->abilities().request( request_for( source, target, id ) );
    INFO( result.reason );
    REQUIRE( result.accepted() );
    return result.id;
}

ability_view view( int64_t id )
{
    const auto result = g->abilities().inspect( id );
    REQUIRE( result );
    return *result;
}

void tick( int count = 1 )
{
    for( int i = 0; i < count; ++i ) {
        calendar::turn += 1_turns;
        g->executions().process();
        g->abilities().process();
    }
}

double counter( const std::string &name )
{
    const diag_value *value = get_globals().maybe_get_global_value( name );
    return value ? value->dbl() : 0;
}

std::string save()
{
    std::ostringstream text;
    JsonOut out( text );
    g->abilities().serialize( out );
    return text.str();
}

talk_effect_t effects( const std::string &json )
{
    talk_effect_t result( json_loader::from_string( json ), "effect", "ability_lifecycle_test" );
    finalize_conditions();
    return result;
}

struct events : event_subscriber {
    using event_subscriber::notify;
    std::function<void( const cata::event & )> callback;
    void notify( const cata::event &e ) override {
        if( callback ) {
            callback( e );
        }
    }
};
} // namespace

TEST_CASE( "ability_one_charged_strike_runs_through_avatar_npc_and_native_monster_special",
           "[abilities][powers][magic][eoc]" )
{
    ability_world world;
    const std::string kind = GENERATE( "avatar", "npc", "monster" );
    Creature *source = &get_avatar();
    if( kind == "npc" ) {
        source = &spawn_npc( get_avatar().pos_bub().xy() + point_rel_ms( 0, -4 ), "thug" );
    } else if( kind == "monster" ) {
        source = &spawn_test_monster( "mon_test_ability_caster",
                                      get_avatar().pos_bub() + tripoint_rel_ms( 2, 0, 0 ) );
    }
    source->set_moves( 1000 );
    Creature &target = kind == "monster" ? static_cast<Creature &>( get_avatar() ) : victim( *source );
    const int hp = target.get_hp();
    if( monster *mon = source->as_monster() ) {
        set_time( calendar::turn );
        mon->set_dest( target.pos_abs() );
        INFO( "can_act=" << mon->can_act() << ", sees=" << mon->sees( get_map(), target ) <<
              ", attitude=" << static_cast<int>( mon->attitude_to( target ) ) );
        REQUIRE( mon->type->special_attacks.at( "test_ability_monster_request" )->call( *mon ) );
    } else {
        dialogue d( get_talker_for( source ), get_talker_for( target ) );
        effects( R"({"effect":{"request_ability":{"id":"test_ability_strike"}}})" ).apply( d );
        CHECK( d.get_value( "reason" ) == "accepted" );
    }
    const auto rows = g->abilities().inspect_all();
    REQUIRE( rows.size() == 1 );
    const int64_t id = rows.front().id;
    CHECK( view( id ).state == "pending" );
    CHECK( source->get_moves() == 1000 );
    CHECK( target.get_hp() == hp );
    CHECK( counter( "ability_on_start" ) == 0 );
    g->abilities().drain_starts();
    CHECK( view( id ).state == "windup" );
    CHECK( g->abilities().controls( *source ) );
    CHECK_FALSE( g->abilities().controls( target ) );
    CHECK( source->get_moves() == 0 );
    tick();
    CHECK( target.get_hp() == hp );
    source->set_moves( 200 );
    tick();
    const auto result = view( id );
    CHECK( result.state == "completed" );
    CHECK( result.reason == "released" );
    CHECK( result.costs_committed );
    CHECK( result.paid_moves == 80 );
    CHECK( result.paid_energy == 0 );
    CHECK( result.effect_completed );
    CHECK( result.spell_level == 1 );
    CHECK( source->get_moves() == 120 );
    CHECK( target.get_hp() == hp - 12 );
    REQUIRE( result.contacts.size() == 1 );
    CHECK( result.contacts.front().target == creature_ref::bind( target ) );
    CHECK( result.contacts.front().facts.at( "hp_loss" ).dbl() == 12 );
    CHECK( result.contacts.front().facts.at( "spell" ) == "test_ability_spell" );
    CHECK_FALSE( g->abilities().controls( *source ) );
    if( source->is_avatar() ) {
        CHECK( get_avatar().activity.is_null() );
    }
    g->abilities().process();
    g->abilities().drain_starts();
    tick();
    CHECK( source->get_moves() == 120 );
    CHECK( target.get_hp() == hp - 12 );
    CHECK( counter( "ability_on_start" ) == 1 );
    CHECK( counter( "ability_on_complete" ) == 1 );
    CHECK( counter( "ability_on_interrupt" ) == 0 );
    CHECK( counter( "ability_on_end" ) == 1 );
}

TEST_CASE( "ability_charge_save_restores_sampled_deadline_identity_and_activity_without_replay",
           "[abilities][powers][save]" )
{
    ability_world world;
    avatar &source = get_avatar();
    monster &target = victim( source );
    const tripoint_abs_ms selected = target.pos_abs();
    const creature_ref identity = creature_ref::bind( target );
    const int64_t id = start( source, target, "test_ability_random" );
    g->abilities().drain_starts();
    tick();
    const auto charged = view( id );
    REQUIRE( charged.windup_ticks >= 2 );
    REQUIRE( charged.windup_ticks <= 4 );
    target.setpos( get_map(), target.pos_bub() + tripoint_rel_ms::north );
    monster &replacement = spawn_test_monster( "mon_execution_expanded_victim",
                           get_map().get_bub( selected ) );
    replacement.set_hp( 10000 );
    const std::string saved = save();
    std::ostringstream activity;
    JsonOut out( activity );
    source.activity.serialize( out );
    const auto rng_before = rng_get_engine();
    g->abilities().reset_for_load();
    CHECK( source.activity.is_null() );
    source.activity.deserialize( json_loader::from_string( activity.str() ) );
    g->abilities().deserialize( json_loader::from_string( saved ) );
    g->abilities().end_restore();
    CHECK( rng_get_engine() == rng_before );
    CHECK( save() == saved );
    CHECK( source.activity.id() == activity_id( "ACT_ABILITY" ) );
    CHECK( g->abilities().owns_avatar_activity( id ) );
    g->abilities().process();
    CHECK( counter( "ability_on_start" ) == 1 );
    CHECK( view( id ).due == charged.due );
    CHECK( view( id ).target == identity );
    while( calendar::turn < charged.due ) {
        tick();
    }
    CHECK( target.get_hp() == 9988 );
    CHECK( replacement.get_hp() == 10000 );
    CHECK( view( id ).selected == selected );
    CHECK( view( id ).released_at == target.pos_abs() );
    CHECK( view( id ).paid_moves == 80 );
    CHECK( counter( "ability_on_end" ) == 1 );
    CHECK( source.activity.is_null() );
}

TEST_CASE( "ability_native_damage_displacement_and_incapacity_interrupt_without_payment",
           "[abilities][powers]" )
{
    ability_world world;
    avatar &source = get_avatar();
    monster &target = victim( source );
    const int64_t id = start( source, target, "test_ability_stamina" );
    g->abilities().drain_starts();
    const int stamina = source.get_stamina();
    std::string reason;
    SECTION( "real HP loss" ) {
        source.apply_damage( nullptr, bodypart_id( "torso" ), 1 );
        reason = "damaged";
    }
    SECTION( "external displacement" ) {
        source.setpos( get_map(), source.pos_bub() + tripoint_rel_ms::north );
        reason = "displaced";
    }
    SECTION( "stun" ) {
        source.add_effect( efftype_id( "stunned" ), 10_turns );
        reason = "caster_incapacitated";
    }
    SECTION( "unconsciousness" ) {
        REQUIRE( incapacitation::apply( source, &target, 10_turns, true ) == "applied" );
        reason = "caster_incapacitated";
    }
    tick();
    CHECK( view( id ).state == "interrupted" );
    CHECK( view( id ).reason == reason );
    CHECK_FALSE( view( id ).costs_committed );
    CHECK( source.get_stamina() == stamina );
    CHECK( target.get_hp() == 10000 );
    CHECK( counter( "ability_on_interrupt" ) == 1 );
    CHECK( counter( "ability_on_end" ) == 1 );
    CHECK( source.activity.is_null() );
    tick( 3 );
    CHECK( counter( "ability_on_end" ) == 1 );
}

TEST_CASE( "ability_target_loss_range_and_obstruction_refuse_release_and_do_not_retarget",
           "[abilities][powers]" )
{
    ability_world world;
    monster &target = victim( get_avatar() );
    const creature_ref selected = creature_ref::bind( target );
    const int64_t id = start( get_avatar(), target );
    g->abilities().drain_starts();
    std::string reason;
    SECTION( "target removed and replaced on its old tile" ) {
        const auto position = target.pos_bub();
        get_creature_tracker().remove( target );
        monster &replacement = spawn_test_monster( "mon_execution_expanded_victim", position );
        REQUIRE_FALSE( selected == creature_ref::bind( replacement ) );
        reason = "target_unavailable";
    }
    SECTION( "target moved out of range" ) {
        target.setpos( get_map(), target.pos_bub() + tripoint_rel_ms( 10, 0, 0 ) );
        reason = "out_of_range";
    }
    SECTION( "wall appeared during windup" ) {
        get_map().ter_set( get_avatar().pos_bub() + tripoint_rel_ms::east, ter_id( "t_wall" ) );
        reason = "blocked";
    }
    tick( 2 );
    CHECK( view( id ).state == "interrupted" );
    CHECK( view( id ).reason == reason );
    CHECK( view( id ).target == selected );
    CHECK_FALSE( view( id ).costs_committed );
    CHECK( view( id ).contacts.empty() );
}

TEST_CASE( "ability_character_resources_commit_once_and_never_use_native_spell_cost",
           "[abilities][powers][magic]" )
{
    ability_world world;
    avatar &source = get_avatar();
    monster &target = victim( source );
    const bool mana = GENERATE( false, true );
    source.magic->set_mana( 200 );
    const int stamina = source.get_stamina();
    const int64_t id = start( source, target, mana ? "test_ability_mana" : "test_ability_stamina" );
    g->abilities().drain_starts();
    tick( 2 );
    REQUIRE( view( id ).effect_completed );
    CHECK( view( id ).paid_energy == ( mana ? 20 : 25 ) );
    CHECK( source.magic->available_mana() == ( mana ? 180 : 200 ) );
    CHECK( source.get_stamina() == stamina - ( mana ? 0 : 25 ) );
    g->abilities().process();
    tick();
    CHECK( source.magic->available_mana() == ( mana ? 180 : 200 ) );
    CHECK( source.get_stamina() == stamina - ( mana ? 0 : 25 ) );
}

TEST_CASE( "ability_passive_drain_can_refuse_release_and_monster_resources_are_explicit",
           "[abilities][powers]" )
{
    ability_world world;
    monster &target = victim( get_avatar() );
    SECTION( "drain during a charge" ) {
        get_avatar().magic->set_mana( 30 );
        const int64_t id = start( get_avatar(), target, "test_ability_mana" );
        g->abilities().drain_starts();
        get_avatar().magic->set_mana( 10 );
        tick( 2 );
        CHECK( view( id ).reason == "insufficient_energy" );
        CHECK_FALSE( view( id ).costs_committed );
        CHECK( get_avatar().magic->available_mana() == 10 );
    }
    SECTION( "no Character mana fallback for a monster" ) {
        auto request = request_for( target, get_avatar(), "test_ability_mana" );
        const int mana = get_avatar().magic->available_mana();
        CHECK( g->abilities().check( request ) == "unsupported_resource" );
        CHECK( g->abilities().request( request ).reason == "unsupported_resource" );
        CHECK( get_avatar().magic->available_mana() == mana );
    }
    CHECK( target.get_hp() == 10000 );
}

TEST_CASE( "ability_effect_only_is_explicit_opt_in_and_skips_charge_and_payment",
           "[abilities][powers][eoc]" )
{
    ability_world world;
    monster &target = victim( get_avatar() );
    auto request = request_for( get_avatar(), target );
    request.invocation = ability_invocation::effect_only;
    const auto before = rng_get_engine();
    CHECK( g->abilities().request( request ).reason == "effect_only_forbidden" );
    CHECK( rng_get_engine() == before );
    request.ability = ability_id( "test_ability_free" );
    const auto admitted = g->abilities().request( request );
    REQUIRE( admitted.accepted() );
    g->abilities().drain_starts();
    const auto result = view( admitted.id );
    CHECK( result.state == "completed" );
    CHECK( result.windup_ticks == 0 );
    CHECK_FALSE( result.costs_committed );
    CHECK( result.paid_moves == 0 );
    CHECK( result.effect_completed );
    CHECK( get_avatar().get_moves() == 1000 );
    CHECK( target.get_hp() == 9988 );
    CHECK( get_avatar().activity.is_null() );
}

TEST_CASE( "ability_player_and_hook_cancellation_are_terminal_before_payment",
           "[abilities][powers][eoc]" )
{
    ability_world world;
    monster &target = victim( get_avatar() );
    int64_t id = 0;
    SECTION( "cancel a pending request through EOC" ) {
        id = start( get_avatar(), target );
        dialogue d( get_talker_for( get_avatar() ), get_talker_for( target ) );
        d.set_value( "handle", std::to_string( id ) );
        effects( R"({"effect":{"cancel_ability":{"context_val":"handle"}}})" ).apply( d );
        CHECK( d.get_value( "reason" ) == "cancelled" );
        g->abilities().drain_starts();
        CHECK( counter( "ability_on_start" ) == 0 );
    }
    SECTION( "cancel the avatar activity" ) {
        id = start( get_avatar(), target );
        g->abilities().drain_starts();
        get_avatar().cancel_activity();
        g->abilities().drain_starts();
        CHECK( view( id ).reason == "player_cancelled" );
    }
    SECTION( "before-release hook cancels its own handle" ) {
        id = start( get_avatar(), target, "test_ability_before_cancel" );
        g->abilities().drain_starts();
        tick( 2 );
        CHECK( view( id ).reason == "cancelled" );
    }
    CHECK( view( id ).state == "interrupted" );
    CHECK_FALSE( view( id ).costs_committed );
    CHECK( target.get_hp() == 10000 );
    CHECK_FALSE( g->abilities().cancel( id ) );
    tick( 3 );
    CHECK( counter( "ability_on_complete" ) == 0 );
    CHECK( counter( "ability_on_interrupt" ) == 1 );
    CHECK( counter( "ability_on_end" ) == 1 );
}

TEST_CASE( "ability_callbacks_revalidate_participants_before_committing",
           "[abilities][powers][eoc]" )
{
    ability_world world;
    monster &target = victim( get_avatar() );
    const int64_t id = start( get_avatar(), target, "test_ability_before_kill" );
    g->abilities().drain_starts();
    tick( 2 );
    CHECK( target.is_dead_state() );
    CHECK( view( id ).reason == "target_unavailable" );
    CHECK_FALSE( view( id ).costs_committed );
    CHECK_FALSE( view( id ).effect_completed );
}

TEST_CASE( "ability_queries_and_inspection_do_not_run_spell_conditions_or_change_state",
           "[abilities][powers][execution_inspector]" )
{
    ability_world world;
    monster &target = victim( get_avatar() );
    auto request = request_for( get_avatar(), target, "test_ability_random" );
    auto &definition = const_cast<spell_type &>( *spell_id( "test_ability_spell" ) );
    const auto old_condition = definition.caster_condition;
    const bool old_has = definition.has_caster_condition;
    const on_out_of_scope restore( [&]() {
        definition.caster_condition = old_condition;
        definition.has_caster_condition = old_has;
    } );
    int calls = 0;
    definition.has_caster_condition = true;
    definition.caster_condition = [&]( const_dialogue const & ) {
        ++calls;
        return true;
    };
    const std::string before = save();
    const auto rng_before = rng_get_engine();
    const conditional_t available( json_loader::from_string(
                                       R"({"can_request_ability":{"id":"test_ability_random"}})" ) );
    const_dialogue d( get_const_talker_for( get_avatar() ), get_const_talker_for( target ) );
    for( int i = 0; i < 3; ++i ) {
        CHECK( g->abilities().check( request ).empty() );
        CHECK( available( d ) );
        CHECK( g->abilities().inspect_all().empty() );
    }
    CHECK( calls == 0 );
    CHECK( save() == before );
    CHECK( rng_get_engine() == rng_before );
    const int64_t id = start( get_avatar(), target, "test_ability_random" );
    CHECK_FALSE( available( d ) );
    g->abilities().drain_starts();
    const std::string active = save();
    const auto charged_rng = rng_get_engine();
    for( int i = 0; i < 3; ++i ) {
        const auto result = view( id );
        const std::string rendered = execution_inspector::format_ability( result );
        CHECK( rendered.find( "windup" ) != std::string::npos );
        CHECK( rendered.find( "test_ability_random" ) != std::string::npos );
    }
    CHECK( calls == 0 );
    CHECK( save() == active );
    CHECK( rng_get_engine() == charged_rng );
    tick( 4 );
    CHECK( calls == 1 );
    CHECK( view( id ).effect_completed );
}

TEST_CASE( "ability_point_self_and_item_invocation_use_explicit_target_contracts",
           "[abilities][powers][eoc]" )
{
    ability_world world;
    monster &target = victim( get_avatar() );
    int64_t id = 0;
    SECTION( "a fixed point does not follow an occupant" ) {
        auto request = request_for( get_avatar(), target, "test_ability_point" );
        request.point = target.pos_abs();
        const auto admitted = g->abilities().request( request );
        REQUIRE( admitted.accepted() );
        id = admitted.id;
        g->abilities().drain_starts();
        target.setpos( get_map(), target.pos_bub() + tripoint_rel_ms::north );
        tick( 2 );
        CHECK( target.get_hp() == 10000 );
        CHECK( view( id ).released_at == *request.point );
        CHECK( view( id ).contacts.empty() );
    }
    SECTION( "self resolves to the real caster" ) {
        const int hp = get_avatar().get_hp();
        id = start( get_avatar(), target, "test_ability_self" );
        g->abilities().drain_starts();
        tick( 2 );
        CHECK( get_avatar().get_hp() == hp - 12 );
        CHECK( target.get_hp() == 10000 );
        CHECK( view( id ).target == creature_ref::bind( get_avatar() ) );
    }
    SECTION( "native item EOC supplies a point instead of treating the item as a creature" ) {
        map &here = get_map();
        item &token = here.add_item( target.pos_bub(), item( itype_id( "test_ability_token" ) ) );
        const auto *use = token.type->get_use( "effect_on_conditions" );
        REQUIRE( use );
        const auto *actor = dynamic_cast<const effect_on_conditions_actor *>( use->get_actor_ptr() );
        REQUIRE( actor );
        actor->use( &get_avatar(), token, &here, target.pos_bub() );
        const auto rows = g->abilities().inspect_all();
        REQUIRE( rows.size() == 1 );
        id = rows.front().id;
        CHECK( rows.front().selected == target.pos_abs() );
        CHECK( rows.front().context.at( "id" ) == "test_ability_token" );
        CHECK( rows.front().context.at( "ability_source_item" ) == "test_ability_token" );
        CHECK( rows.front().context.at( "ability_source_item_uid" ) == std::to_string(
                   token.uid().get_value() ) );
        g->abilities().drain_starts();
        tick( 2 );
        CHECK( target.get_hp() == 9988 );
    }
    CHECK( view( id ).effect_completed );
}

TEST_CASE( "ability_EOC_release_reports_dispatch_without_inventing_native_contacts",
           "[abilities][powers][eoc]" )
{
    ability_world world;
    monster &target = victim( get_avatar() );
    const int64_t id = start( get_avatar(), target, "test_ability_eoc" );
    g->abilities().drain_starts();
    tick( 2 );
    CHECK( target.get_hp() == 9988 );
    CHECK( target.get_killer() == nullptr );
    CHECK( view( id ).effect_completed );
    CHECK( view( id ).contacts.empty() );
    CHECK( view( id ).costs_committed );
}

TEST_CASE( "ability_callback_children_keep_ancestry_are_deferred_and_bounded",
           "[abilities][powers][eoc]" )
{
    ability_world world;
    monster &target = victim( get_avatar() );
    const int64_t root = start( get_avatar(), target, "test_ability_recursive" );
    g->abilities().drain_starts();
    REQUIRE( g->abilities().inspect_all().size() == 2 );
    CHECK( view( root ).state == "completed" );
    CHECK( g->abilities().inspect_all().back().state == "pending" );
    CHECK( get_avatar().activity.id() == activity_id( "ACT_ABILITY" ) );
    CHECK( g->abilities().owns_avatar_activity( g->abilities().inspect_all().back().id ) );
    // Advance the world between pumps, including a save/restore with the root
    // already completed. The child still inherits the original root budget.
    const std::string saved = save();
    g->abilities().reset_for_load();
    g->abilities().deserialize( json_loader::from_string( saved ) );
    g->abilities().end_restore();
    for( int i = 0; i < 12; ++i ) {
        tick();
    }
    const auto rows = g->abilities().inspect_all();
    REQUIRE( rows.size() == 8 );
    int depth = 1;
    for( const auto &row : rows ) {
        CHECK( row.root == root );
        CHECK( row.parent < row.id );
        CHECK( row.depth == depth++ );
        CHECK( row.state == "completed" );
        CHECK( row.paid_moves == 0 );
    }
    CHECK( get_avatar().activity.is_null() );
    CHECK_FALSE( g->abilities().controls( get_avatar() ) );
}

TEST_CASE( "ability_unwind_blocks_mid_release_save_and_never_repeats_committed_costs",
           "[abilities][powers][magic]" )
{
    ability_world world;
    monster &target = victim( get_avatar() );
    const int64_t id = start( get_avatar(), target, "test_ability_instant" );
    events listener;
    listener.callback = [&]( const cata::event & event ) {
        if( event.type() == event_type::character_casts_spell ) {
            CHECK( view( id ).costs_committed );
            CHECK_THROWS_AS( save(), JsonError );
            CHECK_FALSE( g->save() );
            CHECK_FALSE( g->executions().prepare_world_transition() );
            g->abilities().process();
            g->abilities().drain_starts();
            throw std::runtime_error( "ability fixture interruption" );
        }
    };
    get_event_bus().subscribe( &listener );
    CHECK_THROWS_AS( g->abilities().drain_starts(), std::runtime_error );
    CHECK_FALSE( g->abilities().processing() );
    CHECK( view( id ).state == "failed" );
    CHECK( view( id ).reason == "callback_failed" );
    CHECK( view( id ).paid_moves == 80 );
    CHECK( get_avatar().get_moves() == 920 );
    CHECK( target.get_hp() == 10000 );
    tick( 3 );
    CHECK( get_avatar().get_moves() == 920 );
    CHECK( target.get_hp() == 10000 );
}

TEST_CASE( "ability_world_cancellation_and_time_skip_never_catch_up_attacks",
           "[abilities][powers][save]" )
{
    ability_world world;
    monster &target = victim( get_avatar() );
    const int64_t id = start( get_avatar(), target );
    g->abilities().drain_starts();
    SECTION( "session transition" ) {
        g->abilities().cancel_for_world_transition( "dimension_changed" );
        CHECK( view( id ).reason == "dimension_changed" );
    }
    SECTION( "time jump" ) {
        calendar::turn += 10_turns;
        g->abilities().process();
        CHECK( view( id ).reason == "time_skipped" );
    }
    tick();
    CHECK_FALSE( view( id ).costs_committed );
    CHECK( target.get_hp() == 10000 );
    CHECK( get_avatar().activity.is_null() );
    CHECK( counter( "ability_on_end" ) == 1 );
}

TEST_CASE( "ability_simultaneous_casters_own_their_charge_and_cancel_independently",
           "[abilities][powers]" )
{
    ability_world world;
    monster &target = victim( get_avatar() );
    npc &other = spawn_npc( get_avatar().pos_bub().xy() + point_rel_ms( 0, -2 ), "thug" );
    const int64_t first = start( get_avatar(), target );
    const int64_t second = start( other, target );
    CHECK( g->abilities().request( request_for( get_avatar(), target ) ).reason == "caster_busy" );
    g->abilities().drain_starts();
    other.set_moves( 100 );
    other.move();
    CHECK( other.get_moves() == 0 );
    CHECK( g->abilities().cancel( first ) );
    tick( 2 );
    CHECK_FALSE( view( first ).costs_committed );
    CHECK( view( second ).costs_committed );
    CHECK( target.get_hp() == 9988 );
    CHECK( other.get_moves() == -80 );
}

TEST_CASE( "ability_loader_rejects_invalid_policy_and_saved_transaction_states",
           "[abilities][powers][save]" )
{
    ability_world world;
    SECTION( "reversed windup" ) {
        ability_definition def;
        CHECK_THROWS_AS( def.load( json_loader::from_string( R"({"name":"invalid","windup_ticks":[3,1]})" ),
                                   "test_data" ), JsonError );
    }
    SECTION( "nonzero none cost" ) {
        ability_definition def;
        CHECK_THROWS_AS( def.load( json_loader::from_string(
                                       R"({"name":"invalid","energy_cost":{"resource":"none","amount":5}})" ),
                                   "test_data" ), JsonError );
    }
    SECTION( "both release adapters" ) {
        ability_definition def;
        CHECK_THROWS_AS( def.load( json_loader::from_string(
                                       R"({"name":"invalid","spell":{"id":"test_ability_spell"},"eocs":[]})" ),
                                   "test_data" ), JsonError );
    }
    SECTION( "releasing records may never be reloaded" ) {
        monster &target = victim( get_avatar() );
        start( get_avatar(), target );
        std::string bad = save();
        const auto at = bad.find( "\"pending\"" );
        REQUIRE( at != std::string::npos );
        bad.replace( at, 9, "\"releasing\"" );
        CHECK_THROWS_AS( g->abilities().deserialize( json_loader::from_string( bad ) ), JsonError );
    }
}

TEST_CASE( "ability_spell_formulas_use_request_context_and_unrelated_procs_do_not_inherit_it",
           "[abilities][powers][magic]" )
{
    ability_world world;
    monster &target = victim( get_avatar() );
    auto request = request_for( get_avatar(), target, "test_ability_formula" );
    request.context["power_amount"] = diag_value{ 18.0 };
    const auto admitted = g->abilities().request( request );
    REQUIRE( admitted.accepted() );
    request.context["power_amount"] = diag_value{ 99.0 };
    events listener;
    bool child_cast = false;
    listener.callback = [&]( const cata::event & event ) {
        if( event.type() == event_type::character_casts_spell && !child_cast ) {
            child_cast = true;
            spell unrelated( spell_id( "test_ability_context_spell" ) );
            unrelated.cast_all_effects( get_avatar(), target.pos_bub() );
        }
    };
    get_event_bus().subscribe( &listener );
    g->abilities().drain_starts();
    tick( 2 );
    CHECK( child_cast );
    CHECK( view( admitted.id ).effect_completed );
    CHECK( target.get_hp() == 9982 );
    CHECK( counter( "ability_spell_context" ) == 0 );
    CHECK( counter( "ability_spell_paid" ) == 0 );
}

TEST_CASE( "ability_spell_EOCs_and_declared_extras_keep_context_and_legacy_actor_order",
           "[abilities][powers][magic][eoc]" )
{
    ability_world world;
    monster &target = victim( get_avatar() );
    auto request = request_for( get_avatar(), target, "test_ability_spell_context" );
    request.context["power_amount"] = diag_value{ 18.0 };
    request.context["child_level_limit"] = diag_value{ 3.0 };
    const auto admitted = g->abilities().request( request );
    REQUIRE( admitted.accepted() );
    g->abilities().drain_starts();
    tick( 2 );
    CHECK( view( admitted.id ).effect_completed );
    CHECK( counter( "ability_spell_context" ) == 36 );
    CHECK( counter( "ability_spell_paid" ) == 160 );
    CHECK( counter( "ability_spell_level_sum" ) == 3 );
    CHECK( target.get_value( "ability_target_seen" ).dbl() == 2 );
    CHECK( get_avatar().get_value( "ability_caster_seen" ).dbl() == 2 );
    CHECK( target.get_value( "ability_caster_seen" ).dbl() == 0 );
    CHECK( get_avatar().get_value( "ability_target_seen" ).dbl() == 0 );
    spell later( spell_id( "test_ability_context_spell" ) );
    later.cast_all_effects( get_avatar(), target.pos_bub() );
    CHECK( counter( "ability_spell_context" ) == 36 );
    CHECK( counter( "ability_spell_paid" ) == 160 );
}

TEST_CASE( "ability_area_release_records_each_observed_native_victim",
           "[abilities][powers][magic]" )
{
    ability_world world;
    monster &target = victim( get_avatar() );
    monster &other = spawn_test_monster( "mon_execution_expanded_victim",
                                         target.pos_bub() + tripoint_rel_ms::east );
    other.set_hp( 10000 );
    const int64_t id = start( get_avatar(), target, "test_ability_area" );
    g->abilities().drain_starts();
    tick( 2 );
    const auto result = view( id );
    REQUIRE( result.contacts.size() == 2 );
    CHECK( target.get_hp() == 9988 );
    CHECK( other.get_hp() == 9988 );
    CHECK( result.effect_completed );
    CHECK( result.paid_moves == 80 );
    for( const auto &contact : result.contacts ) {
        CHECK( contact.facts.at( "hp_loss" ).dbl() == 12 );
    }
}

TEST_CASE( "ability_interrupt_policy_applies_to_real_monster_damage",
           "[abilities][powers]" )
{
    ability_world world;
    monster &source = spawn_test_monster( "mon_test_ability_caster",
                                          get_avatar().pos_bub() + tripoint_rel_ms::east );
    const bool interruptible = GENERATE( false, true );
    const int64_t id = start( source, get_avatar(), interruptible ?
                              "test_ability_strike" : "test_ability_uninterruptible" );
    g->abilities().drain_starts();
    source.apply_damage( nullptr, bodypart_id( "torso" ), 1 );
    tick( 2 );
    CHECK( view( id ).costs_committed == !interruptible );
    CHECK( view( id ).state == ( interruptible ? "interrupted" : "completed" ) );
    if( interruptible ) {
        CHECK( view( id ).reason == "damaged" );
    }
}

TEST_CASE( "ability_fatal_primary_contact_is_recorded_once_without_promising_final_death",
           "[abilities][powers][magic]" )
{
    ability_world world;
    monster &target = victim( get_avatar() );
    target.set_hp( 5 );
    const creature_ref identity = creature_ref::bind( target );
    const int64_t id = start( get_avatar(), target );
    g->abilities().drain_starts();
    tick( 2 );
    const auto result = view( id );
    CHECK( result.effect_completed );
    CHECK( result.costs_committed );
    REQUIRE( result.contacts.size() == 1 );
    CHECK( result.contacts.front().target == identity );
    CHECK( result.contacts.front().facts.at( "attack_fatal" ).dbl() == 1 );
    CHECK( result.contacts.front().facts.at( "hp_loss" ).dbl() > 0 );
}

TEST_CASE( "ability_world_transition_also_settles_scenes_queued_by_terminal_hooks",
           "[abilities][powers][executions]" )
{
    ability_world world;
    monster &target = victim( get_avatar() );
    const int64_t id = start( get_avatar(), target, "test_ability_transition" );
    g->abilities().drain_starts();
    g->abilities().cancel_for_world_transition( "dimension_changed" );
    CHECK( view( id ).reason == "dimension_changed" );
    REQUIRE( g->abilities().inspect_all().size() == 1 );
    const auto scenes = g->executions().inspect_all();
    REQUIRE( scenes.size() == 1 );
    CHECK( scenes.front().view.reason == "dimension_changed" );
    CHECK_FALSE( g->executions().controls( get_avatar() ) );
    CHECK_FALSE( g->executions().controls( target ) );
    CHECK_FALSE( g->abilities().controls( get_avatar() ) );
    CHECK( get_avatar().activity.is_null() );
    tick( 3 );
    CHECK( target.get_hp() == 10000 );
    CHECK( g->abilities().inspect_all().size() == 1 );
}
