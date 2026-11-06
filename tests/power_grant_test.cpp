#include <functional>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

#include "ability_runtime.h"
#include "avatar.h"
#include "bodypart.h"
#include "cata_catch.h"
#include "character_martial_arts.h"
#include "creature_tracker.h"
#include "damage.h"
#include "effect.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "execution_inspector.h"
#include "execution_runtime.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "global_vars.h"
#include "item.h"
#include "item_location.h"
#include "itype.h"
#include "iuse_actor.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "monster.h"
#include "npc.h"
#include "options_helpers.h"
#include "overmapbuffer.h"
#include "player_helpers.h"
#include "power_grant.h"
#include "power_runtime.h"
#include "rng.h"
#include "talker.h"
#include "units.h"
#include "vehicle.h"
#include "vpart_position.h"
#include "vpart_range.h"

static const itype_id itype_power_demo_form_grant_focus( "power_demo_form_grant_focus" );

namespace
{
const efftype_id boost( "test_owned_power_boost" );
struct grant_world {
    time_point old_time = calendar::turn;
    override_option animations{ "ANIMATIONS", "false" };
    grant_world() {
        g->abilities().clear();
        g->executions().clear();
        clear_avatar();
        clear_map_without_vision( -2, 1 );
        get_globals().clear_global_values();
        calendar::turn = calendar::turn_zero + 12_hours;
        get_avatar().setpos( get_map(), tripoint_bub_ms( 60, 60, 0 ), false );
        get_avatar().clear_worn();
        get_avatar().set_all_parts_hp_cur( 10000 );
        get_avatar().set_moves( 1000 );
    }
    ~grant_world() {
        g->abilities().clear();
        g->executions().clear();
        clear_avatar();
        calendar::turn = old_time;
    }
};
Creature &actor( const std::string &kind )
{
    if( kind == "npc" ) {
        npc &who = spawn_npc( get_avatar().pos_bub().xy() + point_rel_ms( 3, 0 ), "thug" );
        who.clear_worn();
        who.set_wielded_item( item() );
        who.set_all_parts_hp_cur( 10000 );
        return who;
    }
    if( kind == "monster" ) {
        monster &who = spawn_test_monster( "mon_execution_expanded_victim",
                                           get_avatar().pos_bub() + tripoint_rel_ms( 0, 3, 0 ) );
        who.set_hp( 10000 );
        return who;
    }
    return get_avatar();
}
power_application_request request( Creature &owner, Creature &target, const std::string &id )
{
    return { power_application_id( id ), creature_ref::bind( owner ), creature_ref::bind( target ),
             std::nullopt, {} };
}
int64_t apply( Creature &owner, Creature &target, const std::string &id = "test_grant_buff",
               bool begin = true )
{
    const auto out = get_powers().apply( request( owner, target, id ) );
    INFO( out.reason );
    REQUIRE( out.id != 0 );
    if( begin ) {
        g->abilities().drain_starts();
    }
    return out.id;
}
void end( int64_t id )
{
    REQUIRE( get_powers().end( id ) );
    g->abilities().drain_starts();
}
void tick( int count = 1 )
{
    for( int i = 0; i < count; ++i ) {
        calendar::turn += 1_turns;
        g->abilities().process();
    }
}
power_application_view view( int64_t id )
{
    for( const auto &value : get_powers().inspect_applications() ) {
        if( value.id == id ) {
            return value;
        }
    }
    FAIL( "Missing application " << id );
    return {};
}
power_grant_view grant( int64_t id )
{
    for( const auto &value : get_power_grants().inspect() ) {
        if( value.application == id ) {
            return value;
        }
    }
    FAIL( "Missing grant " << id );
    return {};
}
std::string save()
{
    std::ostringstream buffer;
    JsonOut out( buffer );
    g->abilities().serialize( out );
    return buffer.str();
}
std::string save_actor()
{
    std::ostringstream buffer;
    JsonOut out( buffer );
    get_avatar().serialize( out );
    return buffer.str();
}
void restore( const std::string &runtime, const std::string &who = "" )
{
    g->abilities().reset_for_load();
    g->abilities().deserialize( json_loader::from_string( runtime ) );
    if( !who.empty() ) {
        get_avatar().setID( character_id(), true );
        get_avatar().deserialize( json_loader::from_string( who ) );
    }
    g->abilities().end_restore();
}
int64_t shirt( Character &who )
{
    const auto worn = who.wear_item( item( itype_id( "tshirt" ), calendar::turn ), false );
    REQUIRE( worn );
    item &value = **worn;
    value.set_var( "original_gear", "unique" );
    value.set_damage( 1000 );
    return value.uid().get_value();
}
const item *find( const Character &who, int64_t uid )
{
    const auto items = who.items_with( [uid]( const item & value ) {
        return value.uid().get_value() == uid;
    } );
    return items.empty() ? nullptr : items.front();
}
const item *ground( const tripoint_bub_ms &point, int64_t uid )
{
    for( const item &value : get_map().i_at( point ) ) {
        if( value.uid().get_value() == uid ) {
            return &value;
        }
    }
    return nullptr;
}
struct observer : event_subscriber {
    using event_subscriber::notify;
    event_type wanted = event_type::character_gains_effect;
    bool armed = true;
    std::function<void()> callback;
    void notify( const cata::event &event ) override {
        if( armed && event.type() == wanted ) {
            armed = false;
            callback();
        }
    }
    void notify( const cata::event &event, std::unique_ptr<talker>,
                 std::unique_ptr<talker> ) override {
        notify( event );
    }
};
} // namespace

TEST_CASE( "owned_native_effects_sum_sources_for_every_real_actor_kind", "[power_grants]" )
{
    grant_world world;
    Creature &target = actor( GENERATE( "avatar", "npc", "monster" ) );
    const auto one = apply( get_avatar(), target );
    const auto two = apply( get_avatar(), target );
    CHECK( target.get_effect_int( boost ) == 4 );
    CHECK( target.get_effect( boost ).get_mod( "STR" ) == 4 );
    end( one );
    CHECK( target.get_effect_int( boost ) == 2 );
    CHECK( view( two ).state == "active" );
    end( two );
    CHECK_FALSE( target.has_effect( boost ) );
}

TEST_CASE( "owned_effects_preserve_natural_effects_and_decay_their_own_timers", "[power_grants]" )
{
    grant_world world;
    auto &who = get_avatar();
    who.add_effect( boost, 10_turns, false, 1 );
    const auto first = apply( who, who );
    const auto second = apply( actor( "npc" ), who );
    CHECK( who.get_effect_int( boost ) == 5 );
    who.process_effects();
    end( first );
    CHECK( who.get_effect_int( boost ) == 3 );
    end( second );
    CHECK( who.get_effect_int( boost ) == 1 );
    CHECK( who.get_effect_dur( boost ) == 9_turns );
    CHECK_FALSE( who.get_effect( boost ).is_permanent() );
}

TEST_CASE( "ordinary_reapplication_under_owned_effects_survives_last_source_removal",
           "[power_grants]" )
{
    grant_world world;
    auto &who = get_avatar();
    const auto id = apply( who, who );
    who.add_effect( boost, 20_turns, false, 3 );
    CHECK( who.get_effect_int( boost ) == 5 );
    who.get_effect( boost ).mod_duration( 2_turns );
    who.get_effect( boost ).set_intensity( 4 );
    CHECK( who.get_effect_int( boost ) == 6 );
    end( id );
    CHECK( who.get_effect_int( boost ) == 4 );
    CHECK( who.get_effect_dur( boost ) == 22_turns );
}

TEST_CASE( "owned_effect_intensity_caps_do_not_destroy_hidden_contributions", "[power_grants]" )
{
    grant_world world;
    auto &who = get_avatar();
    who.add_effect( boost, 30_turns, false, 8 );
    const auto a = apply( who, who );
    const auto b = apply( who, who );
    CHECK( who.get_effect_int( boost ) == 10 );
    end( a );
    CHECK( who.get_effect_int( boost ) == 10 );
    end( b );
    CHECK( who.get_effect_int( boost ) == 8 );
}

TEST_CASE( "a_natural_effect_can_expire_while_an_owned_contribution_remains", "[power_grants]" )
{
    grant_world world;
    auto &who = get_avatar();
    who.add_effect( boost, 1_turns, false, 1 );
    const auto id = apply( who, who );
    who.process_effects();
    who.process_effects();
    CHECK( who.get_effect_int( boost ) == 2 );
    end( id );
    CHECK_FALSE( who.has_effect( boost ) );
}

TEST_CASE( "native_cures_clear_owned_layers_without_sustained_reapplication", "[power_grants]" )
{
    grant_world world;
    auto &who = get_avatar();
    const auto a = apply( who, who );
    const auto b = apply( actor( "npc" ), who );
    REQUIRE( who.remove_effect( boost ) );
    tick();
    CHECK_FALSE( who.has_effect( boost ) );
    CHECK( view( a ).state == "active" );
    end( b );
    CHECK_FALSE( who.has_effect( boost ) );
}

TEST_CASE( "owned_bodypart_contributions_are_independent", "[power_grants]" )
{
    grant_world world;
    auto &who = get_avatar();
    const auto a = apply( who, who, "test_grant_torso" );
    const auto b = apply( who, who, "test_grant_arm" );
    CHECK( who.get_effect_int( boost, bodypart_id( "torso" ) ) == 2 );
    CHECK( who.get_effect_int( boost, bodypart_id( "arm_l" ) ) == 2 );
    end( a );
    CHECK_FALSE( who.has_effect( boost, bodypart_id( "torso" ) ) );
    CHECK( who.get_effect_int( boost, bodypart_id( "arm_l" ) ) == 2 );
    end( b );
}

TEST_CASE( "owned_effect_copies_do_not_alias_live_layer_mutations", "[power_grants]" )
{
    grant_world world;
    auto &who = get_avatar();
    who.add_effect( boost, 10_turns, false, 1 );
    const auto id = apply( who, who );
    effect snapshot = who.get_effect( boost );
    snapshot.set_intensity( 5 );
    CHECK( snapshot.get_intensity() == 7 );
    CHECK( who.get_effect_int( boost ) == 3 );
    CHECK( snapshot.remove_owned( id ) );
    CHECK( snapshot.get_intensity() == 5 );
    CHECK( who.get_effect_int( boost ) == 3 );
}

TEST_CASE( "form_restores_real_garments_and_weapon_without_copying_UIDs", "[power_grants]" )
{
    grant_world world;
    Character &who = *actor( GENERATE( "avatar", "npc" ) ).as_character();
    const auto original = shirt( who );
    item held( itype_id( "bat" ), calendar::turn );
    const auto original_weapon = held.uid().get_value();
    REQUIRE( who.wield_power_item( held ) );
    const auto id = apply( get_avatar(), who, "test_grant_form" );
    CHECK( grant( id ).stowed == 2 );
    CHECK( who.amount_worn( itype_id( "test_power_grant_shell" ) ) == 1 );
    CHECK( who.get_effect_int( boost ) == 3 );
    CHECK_FALSE( find( who, original ) );
    REQUIRE( who.get_wielded_item() );
    CHECK( who.get_wielded_item()->has_var( "power_grant_application" ) );
    end( id );
    const item *restored = find( who, original );
    REQUIRE( restored );
    CHECK( who.is_worn( *restored ) );
    CHECK( restored->damage() == 1000 );
    CHECK( restored->get_var( "original_gear", "" ) == "unique" );
    CHECK( who.get_wielded_item()->uid().get_value() == original_weapon );
    CHECK( grant( id ).restored == 2 );
    CHECK_FALSE( who.has_effect( boost ) );
}

TEST_CASE( "owned_form_escrow_preserves_nested_item_identity_through_a_real_avatar_save",
           "[power_grants][save]" )
{
    grant_world world;
    auto &who = get_avatar();
    item pack( itype_id( "backpack" ), calendar::turn );
    const auto worn = who.wear_item( pack, false );
    REQUIRE( worn );
    item &actual_pack = **worn;
    item stone( itype_id( "rock" ), calendar::turn );
    stone.set_var( "cargo", "original" );
    REQUIRE( actual_pack.put_in( std::move( stone ), pocket_type::CONTAINER ).success() );
    const auto pack_uid = actual_pack.uid().get_value();
    const auto cargo_uid = actual_pack.all_items_top().front()->uid().get_value();
    who.add_effect( boost, 40_turns, false, 1 );
    const auto id = apply( who, who, "test_grant_form" );
    const auto runtime = save();
    const auto person = save_actor();
    restore( runtime, person );
    g->abilities().drain_starts();
    CHECK( who.get_effect_int( boost ) == 4 );
    CHECK( grant( id ).stowed == 1 );
    end( id );
    const item *returned = find( who, pack_uid );
    REQUIRE( returned );
    REQUIRE( returned->all_items_top().size() == 1 );
    CHECK( returned->all_items_top().front()->uid().get_value() == cargo_uid );
    CHECK( returned->all_items_top().front()->get_var( "cargo", "" ) == "original" );
    CHECK( who.get_effect_int( boost ) == 1 );
    CHECK( who.get_effect_dur( boost ) == 40_turns );
}

TEST_CASE( "granted_equipment_uses_native_armor_and_cannot_become_crafting_material",
           "[power_grants]" )
{
    grant_world world;
    auto &who = get_avatar();
    const auto id = apply( who, who, "test_grant_equipment" );
    const auto pieces = who.items_with( []( const item & value ) {
        return value.has_var( "power_grant_application" );
    } );
    REQUIRE( pieces.size() == 1 );
    CHECK_FALSE( is_crafting_component( *pieces.front() ) );
    CHECK_FALSE( pieces.front()->allow_crafting_component() );
    CHECK_FALSE( pieces.front()->is_salvageable() );
    CHECK_FALSE( pieces.front()->is_disassemblable() );
    const int hp = who.get_hp();
    who.deal_damage( nullptr, bodypart_id( "torso" ), damage_instance( damage_type_id( "bash" ), 10 ) );
    CHECK( who.get_hp() == hp );
    end( id );
    CHECK( who.amount_worn( itype_id( "test_power_grant_shell" ) ) == 0 );
}

TEST_CASE( "form_conflicts_and_monster_equipment_refuse_before_payment_or_rng", "[power_grants]" )
{
    grant_world world;
    auto &who = get_avatar();
    apply( who, who, "test_grant_form" );
    const auto r = request( who, who, "test_grant_paid" );
    const auto before = save();
    const auto rng = rng_get_engine();
    CHECK( get_powers().check_application( r ) == "form_conflict" );
    CHECK( get_powers().apply( r ).reason == "form_conflict" );
    CHECK( rng_get_engine() == rng );
    CHECK( save() == before );
    auto &mon = actor( "monster" );
    CHECK( get_powers().apply( request( who, mon,
                                        "test_grant_equipment" ) ).reason == "equipment_requires_character" );
}

TEST_CASE( "queued_forms_revalidate_the_exclusive_claim_before_spending", "[power_grants]" )
{
    grant_world world;
    auto &who = get_avatar();
    const auto a = apply( who, who, "test_grant_paid", false );
    const auto b = apply( who, who, "test_grant_paid", false );
    g->abilities().drain_starts();
    CHECK( view( a ).state == "active" );
    CHECK( view( b ).reason == "form_conflict" );
    CHECK( get_powers().balance( who, "test_power_focus" ).total == 95 );
}

TEST_CASE( "form_restoration_preserves_a_newly_occupied_weapon_slot", "[power_grants]" )
{
    grant_world world;
    auto &who = get_avatar();
    item old( itype_id( "bat" ), calendar::turn );
    old.set_var( "original_weapon", "kept" );
    REQUIRE( who.wield_power_item( old ) );
    const auto id = apply( who, who, "test_grant_form" );
    who.remove_items_with( []( const item & value ) {
        return value.has_var( "power_grant_application" );
    } );
    item replacement( itype_id( "bat" ), calendar::turn );
    const auto replacement_id = replacement.uid().get_value();
    REQUIRE( who.wield_power_item( replacement ) );
    end( id );
    REQUIRE( who.get_wielded_item() );
    CHECK( who.get_wielded_item()->uid().get_value() == replacement_id );
    int returned = 0;
    for( const item &value : get_map().i_at( who.pos_bub() ) ) {
        returned += value.get_var( "original_weapon", "" ) == "kept" ? 1 : 0;
    }
    CHECK( returned == 1 );
    CHECK( grant( id ).dropped == 1 );
}

TEST_CASE( "ending_one_equipment_source_preserves_another_source", "[power_grants]" )
{
    grant_world world;
    auto &who = get_avatar();
    const auto a = apply( who, who, "test_grant_clothing" );
    const auto b = apply( actor( "npc" ), who, "test_grant_clothing" );
    CHECK( who.amount_worn( itype_id( "tshirt" ) ) == 2 );
    end( a );
    CHECK( who.amount_worn( itype_id( "tshirt" ) ) == 1 );
    CHECK( view( b ).state == "active" );
    end( b );
    CHECK( who.amount_worn( itype_id( "tshirt" ) ) == 0 );
}

TEST_CASE( "a_form_cannot_stow_another_sources_projected_equipment", "[power_grants]" )
{
    grant_world world;
    auto &who = get_avatar();
    apply( actor( "npc" ), who, "test_grant_equipment" );
    CHECK( get_powers().apply( request( who, who,
                                        "test_grant_form" ) ).reason == "borrowed_equipment" );
}

TEST_CASE( "dropped_and_copied_projected_items_end_with_their_actual_source", "[power_grants]" )
{
    grant_world world;
    auto &who = get_avatar();
    const auto id = apply( who, who, "test_grant_weapon" );
    REQUIRE( who.get_wielded_item() );
    item dropped = *who.get_wielded_item();
    const auto point = who.pos_bub() + tripoint_rel_ms( 2, 0, 0 );
    const auto dropped_location = get_map().add_item_or_charges_ret_loc( point, std::move( dropped ) );
    REQUIRE( dropped_location );
    const auto drop_id = dropped_location->uid().get_value();
    REQUIRE( ground( point, drop_id ) );
    end( id );
    CHECK_FALSE( who.has_weapon() );
    CHECK_FALSE( ground( point, drop_id ) );
}

TEST_CASE( "ordinary_contents_of_expiring_projection_equipment_are_preserved", "[power_grants]" )
{
    grant_world world;
    auto &who = get_avatar();
    const auto id = apply( who, who, "test_grant_weapon" );
    item projected = *who.get_wielded_item();
    // A scripted conversion retains its source marker; real inserted cargo is not minted.
    projected.convert( itype_id( "backpack" ) );
    item stone( itype_id( "rock" ), calendar::turn );
    stone.set_var( "cargo", "ordinary" );
    REQUIRE( projected.put_in( std::move( stone ), pocket_type::CONTAINER ).success() );
    get_map().add_item_or_charges( who.pos_bub(), std::move( projected ) );
    end( id );
    int returned = 0;
    for( const item &value : get_map().i_at( who.pos_bub() ) ) {
        returned += value.get_var( "cargo", "" ) == "ordinary" ? 1 : 0;
    }
    CHECK( returned == 1 );
}

TEST_CASE( "detached_projection_processing_discards_orphans_and_preserves_cargo", "[power_grants]" )
{
    grant_world world;
    item orphan( itype_id( "backpack" ), calendar::turn );
    orphan.set_var( "power_grant_application", "999999" );
    orphan.set_var( "power_grant_dimension", g->get_dimension_prefix().str() );
    orphan.ethereal = true;
    item stone( itype_id( "rock" ), calendar::turn );
    stone.set_var( "cargo", "ordinary" );
    REQUIRE( orphan.put_in( std::move( stone ), pocket_type::CONTAINER ).success() );
    CHECK( orphan.process( get_map(), nullptr, get_avatar().pos_bub() ) );
    int returned = 0;
    for( const item &value : get_map().i_at( get_avatar().pos_bub() ) ) {
        returned += value.get_var( "cargo", "" ) == "ordinary" ? 1 : 0;
    }
    CHECK( returned == 1 );
}

TEST_CASE( "source_loss_releases_native_grants_and_returns_the_real_wardrobe_once",
           "[power_grants]" )
{
    grant_world world;
    auto &source = *actor( "monster" ).as_monster();
    auto &who = get_avatar();
    const auto original = shirt( who );
    const auto id = apply( source, who, "test_grant_form" );
    get_creature_tracker().remove( source );
    tick();
    CHECK( find( who, original ) );
    CHECK_FALSE( who.has_effect( boost ) );
    CHECK( view( id ).reason == "owner_unavailable" );
    CHECK( grant( id ).restored == 1 );
    tick();
    CHECK( grant( id ).restored == 1 );
}

TEST_CASE( "native_wear_callback_exceptions_keep_payment_and_restore_committed_gear",
           "[power_grants]" )
{
    grant_world world;
    auto &who = get_avatar();
    const auto original = shirt( who );
    const auto id = apply( who, who, "test_grant_paid", false );
    observer events;
    events.wanted = event_type::character_wears_item;
    events.callback = []() {
        CHECK_THROWS_AS( save(), JsonError );
        CHECK_THROWS_AS( g->abilities().cancel_for_world_transition( "fixture" ), JsonError );
        throw std::runtime_error( "native wear fixture" );
    };
    get_event_bus().subscribe( &events );
    CHECK_THROWS_AS( g->abilities().drain_starts(), std::runtime_error );
    CHECK( get_powers().balance( who, "test_power_focus" ).total == 95 );
    CHECK( view( id ).state == "failed" );
    CHECK( find( who, original ) );
    CHECK_FALSE( who.has_effect( boost ) );
    CHECK_FALSE( who.has_weapon() );
    CHECK( who.amount_worn( itype_id( "test_power_grant_shell" ) ) == 0 );
    CHECK_NOTHROW( save() );
}

TEST_CASE( "native_effect_callback_removal_stops_later_equipment_grants", "[power_grants]" )
{
    grant_world world;
    auto &source = *actor( "monster" ).as_monster();
    auto &who = get_avatar();
    const auto original = shirt( who );
    const auto id = apply( source, who, "test_grant_form", false );
    observer events;
    events.callback = [&]() {
        get_creature_tracker().remove( source );
    };
    get_event_bus().subscribe( &events );
    g->abilities().drain_starts();
    CHECK( view( id ).state == "ended" );
    CHECK( find( who, original ) );
    CHECK_FALSE( who.has_effect( boost ) );
    CHECK_FALSE( who.has_weapon() );
    CHECK( grant( id ).equipment == 0 );
}

TEST_CASE( "form_upkeep_failure_and_time_skips_return_equipment_without_catchup", "[power_grants]" )
{
    grant_world world;
    auto &who = get_avatar();
    const auto original = shirt( who );
    const auto id = apply( who, who, "test_grant_paid" );
    const std::string mode = GENERATE( "upkeep", "skipped" );
    if( mode == "upkeep" ) {
        get_powers().modify( who, "test_power_focus", -95, "fixture" );
        tick();
        CHECK( view( id ).reason == "upkeep_unaffordable" );
    } else {
        calendar::turn += 5_turns;
        g->abilities().process();
        CHECK( view( id ).reason == "time_skipped" );
        CHECK( view( id ).paid_upkeep == 0 );
    }
    CHECK( find( who, original ) );
    CHECK_FALSE( who.has_effect( boost ) );
}

TEST_CASE( "world_transition_settles_forms_and_preserves_unowned_effects", "[power_grants]" )
{
    grant_world world;
    auto &who = get_avatar();
    const auto original = shirt( who );
    who.add_effect( boost, 30_turns, false, 1 );
    const auto id = apply( who, who, "test_grant_form" );
    g->abilities().cancel_for_world_transition( "fixture_transition" );
    CHECK( view( id ).reason == "fixture_transition" );
    CHECK( who.get_effect_int( boost ) == 1 );
    CHECK( find( who, original ) );
    CHECK( grant( id ).restored == 1 );
}

TEST_CASE( "stowed_items_still_run_native_processing_once_per_world_second", "[power_grants]" )
{
    grant_world world;
    auto &who = get_avatar();
    item old( itype_id( "bat" ), calendar::turn );
    old.ethereal = true;
    old.set_var( "ethereal", 2 );
    const auto uid = old.uid().get_value();
    REQUIRE( who.wield_power_item( old ) );
    const auto id = apply( who, who, "test_grant_wardrobe" );
    g->abilities().drain_starts();
    CHECK( grant( id ).stowed == 1 );
    tick();
    g->abilities().drain_starts();
    CHECK( grant( id ).stowed == 1 );
    tick();
    CHECK( grant( id ).stowed == 0 );
    end( id );
    CHECK_FALSE( find( who, uid ) );
    CHECK_FALSE( ground( who.pos_bub(), uid ) );
}

TEST_CASE( "grant_inspection_and_admission_queries_are_pure", "[power_grants]" )
{
    grant_world world;
    auto &who = get_avatar();
    const auto id = apply( who, who, "test_grant_buff" );
    const auto r = request( who, who, "test_grant_equipment" );
    const auto before = save();
    const auto person = save_actor();
    const auto rng = rng_get_engine();
    for( int n = 0; n < 3; ++n ) {
        CHECK( get_powers().check_application( r ).empty() );
        CHECK_FALSE( execution_inspector::format_grant( grant( id ) ).empty() );
        CHECK( who.get_effect_int( boost ) == 2 );
    }
    CHECK( save() == before );
    CHECK( save_actor() == person );
    CHECK( rng_get_engine() == rng );
}

TEST_CASE( "orphaned_native_effect_layers_are_removed_without_erasing_the_natural_base",
           "[power_grants]" )
{
    grant_world world;
    auto &who = get_avatar();
    who.add_effect( boost, 15_turns, false, 1 );
    REQUIRE( who.add_effect_grant( 999999, effect_source::empty(), boost,
                                   bodypart_str_id::NULL_ID(), 2 ) );
    CHECK( who.get_effect_int( boost ) == 3 );
    g->abilities().drain_starts();
    CHECK( who.get_effect_int( boost ) == 1 );
    CHECK( who.get_effect_dur( boost ) == 15_turns );
}

TEST_CASE( "malformed_owned_layer_saves_are_rejected_before_nested_recursion",
           "[power_grants][save]" )
{
    grant_world world;
    auto &who = get_avatar();
    apply( who, who );
    std::ostringstream buffer;
    JsonOut out( buffer );
    who.get_effect( boost ).serialize( out );
    auto text = buffer.str();
    const auto found = text.find( "\"intensity\":2", text.find( "owned_sources" ) );
    REQUIRE( found != std::string::npos );
    text.replace( found, std::string( "\"intensity\":2" ).size(), "\"intensity\":-1" );
    effect temporary;
    CHECK_THROWS_AS( temporary.deserialize( json_loader::from_string( text ) ), JsonError );
    CHECK( who.get_effect_int( boost ) == 2 );
}

TEST_CASE( "malformed_wardrobe_saves_leave_existing_owned_state_intact", "[power_grants][save]" )
{
    grant_world world;
    auto &who = get_avatar();
    shirt( who );
    const auto id = apply( who, who, "test_grant_form" );
    std::ostringstream buffer;
    JsonOut out( buffer );
    get_power_grants().serialize( out );
    auto text = buffer.str();
    const auto pos = text.find( "\"effects\":1" );
    REQUIRE( pos != std::string::npos );
    text.replace( pos, std::string( "\"effects\":1" ).size(), "\"effects\":100" );
    CHECK_THROWS_AS( get_power_grants().deserialize( json_loader::from_string( text ) ), JsonError );
    CHECK( grant( id ).stowed == 1 );
    CHECK( who.get_effect_int( boost ) == 3 );
}

TEST_CASE( "invalid_grant_definitions_and_unavailable_bodyparts_refuse", "[power_grants]" )
{
    grant_world world;
    power_grant_definition spec;
    CHECK_THROWS_AS( spec.load( json_loader::from_string(
                                    R"({"equipment":[{"item":"bat","slot":"wielded"},{"item":"bat","slot":"wielded"}]})" ) ),
                     JsonError );
    CHECK_THROWS_AS( spec.load( json_loader::from_string(
                                    R"({"effects":[{"id":"test_owned_power_boost","intensity":0}]})" ) ), JsonError );
    CHECK_FALSE( get_avatar().check_effect_grant( efftype_id( "knocked_out" ),
                 bodypart_str_id::NULL_ID(), 1 ).empty() );
    CHECK_FALSE( get_avatar().check_effect_grant( boost, bodypart_id( "head_dragonfly" ), 1 ).empty() );
}

TEST_CASE( "lost_form_targets_do_not_give_their_wardrobe_to_replacement_residents",
           "[power_grants]" )
{
    grant_world world;
    auto &who = *actor( "npc" ).as_character();
    const auto original = shirt( who );
    const auto point = who.pos_bub();
    const auto identity = who.getID();
    const auto id = apply( get_avatar(), who, "test_grant_form" );
    g->remove_npc( identity );
    REQUIRE( overmap_buffer.remove_npc( identity ) );
    npc &replacement = spawn_npc( point.xy(), "thug" );
    replacement.clear_worn();
    REQUIRE( replacement.getID() != identity );
    tick();
    CHECK( view( id ).reason == "target_unavailable" );
    CHECK_FALSE( find( replacement, original ) );
    CHECK( replacement.items_with( []( const item & value ) {
        return value.get_var( "original_gear", "" ) == "unique";
    } ).empty() );
    int returned = 0;
    for( const item &value : get_map().i_at( point ) ) {
        returned += value.get_var( "original_gear", "" ) == "unique" ? 1 : 0;
    }
    CHECK( returned == 1 );
    CHECK( grant( id ).dropped == 1 );
}

TEST_CASE( "loaned_projection_equipment_is_removed_from_the_actual_carrier", "[power_grants]" )
{
    grant_world world;
    auto &who = get_avatar();
    auto &borrower = *actor( "npc" ).as_character();
    const auto id = apply( who, who, "test_grant_weapon" );
    item loan = who.remove_weapon();
    REQUIRE( borrower.wield_power_item( loan ) );
    CHECK( get_power_grants().item_active( *borrower.get_wielded_item() ) );
    end( id );
    CHECK_FALSE( borrower.has_weapon() );
}

TEST_CASE( "form_native_removal_locks_are_honored_before_admission", "[power_grants]" )
{
    grant_world world;
    auto &who = get_avatar();
    const auto original = shirt( who );
    const auto value = who.items_with( [original]( const item & it ) {
        return it.uid().get_value() == original;
    } );
    REQUIRE( value.size() == 1 );
    value.front()->set_flag( flag_id( "NO_TAKEOFF" ) );
    const auto before = save();
    CHECK( get_powers().apply( request( who, who, "test_grant_paid" ) ).reason == "clothing_locked" );
    CHECK( save() == before );
    CHECK( find( who, original ) );
}

TEST_CASE( "incompatible_application_revisions_release_saved_native_grants",
           "[power_grants][save]" )
{
    grant_world world;
    auto &who = get_avatar();
    const auto original = shirt( who );
    const auto id = apply( who, who, "test_grant_form" );
    auto text = save();
    const auto pos = text.find( "\"revision\":1" );
    REQUIRE( pos != std::string::npos );
    text.replace( pos, std::string( "\"revision\":1" ).size(), "\"revision\":2" );
    restore( text );
    tick();
    CHECK( view( id ).reason == "incompatible_definition" );
    CHECK( find( who, original ) );
    CHECK_FALSE( who.has_effect( boost ) );
}

TEST_CASE( "armored_form_demo_item_uses_the_paid_ability_and_returns_original_gear",
           "[power_grants][powers_demo]" )
{
    // This checks the powers_demo mod's own items, so it runs only with that mod loaded, as in
    // CI's mods job.
    if( !itype_power_demo_form_grant_focus.is_valid() ) {
        return;
    }
    grant_world world;
    auto &who = get_avatar();
    const auto original = shirt( who );
    const auto use = [&]() {
        item focus( itype_power_demo_form_grant_focus, calendar::turn );
        const auto found = focus.type->use_methods.find( "effect_on_conditions" );
        REQUIRE( found != focus.type->use_methods.end() );
        REQUIRE( found->second.get_actor_ptr() );
        found->second.get_actor_ptr()->use( &who, focus, &get_map(), who.pos_bub() );
        g->abilities().drain_starts();
    };
    use();
    CHECK( get_power_grants().inspect().empty() );
    tick();
    g->abilities().drain_starts();
    CHECK( who.amount_worn( itype_id( "power_demo_armor_shell" ) ) == 1 );
    CHECK( who.get_effect_int( efftype_id( "power_demo_might" ) ) == 2 );
    CHECK( get_powers().balance( who, "power_demo_focus_pool" ).total == 90 );
    int paid = 0;
    for( const auto &ability : g->abilities().inspect_all() ) {
        if( ability.ability == ability_id( "power_demo_form_grant_cast" ) ) {
            CHECK( ability.paid_moves == 100 );
            ++paid;
        }
    }
    CHECK( paid == 1 );
    use();
    CHECK( find( who, original ) );
    CHECK_FALSE( who.has_effect( efftype_id( "power_demo_might" ) ) );
    CHECK_FALSE( who.has_weapon() );
}

TEST_CASE( "parent_end_removes_child_native_grants_before_terminal_observers", "[power_grants]" )
{
    grant_world world;
    auto &who = get_avatar();
    const auto id = apply( who, who, "test_grant_parent" );
    g->abilities().drain_starts();
    CHECK( who.get_effect_int( boost ) == 3 );
    end( id );
    CHECK_FALSE( who.has_effect( boost ) );
    const auto *leftover = get_globals().maybe_get_global_value( "grant_parent_leftover" );
    REQUIRE( leftover );
    CHECK( leftover->dbl() == 0 );
}

TEST_CASE( "partial_native_outfit_refusal_keeps_costs_and_returns_original_gear", "[power_grants]" )
{
    grant_world world;
    auto &who = get_avatar();
    const auto original = shirt( who );
    const auto id = apply( who, who, "test_grant_partial_failure" );
    CHECK( view( id ).reason == "equipment_refused" );
    CHECK( get_powers().balance( who, "test_power_focus" ).total == 95 );
    CHECK( find( who, original ) );
    CHECK( grant( id ).equipment == 1 );
    CHECK_FALSE( who.has_effect( boost ) );
    CHECK( who.amount_worn( itype_id( "test_power_grant_shell" ) ) == 0 );
}

TEST_CASE( "restoration_callback_failure_keeps_the_committed_transfer_and_never_duplicates_gear",
           "[power_grants]" )
{
    grant_world world;
    auto &who = get_avatar();
    const auto original = shirt( who );
    const auto id = apply( who, who, "test_grant_form" );
    observer events;
    events.wanted = event_type::character_wears_item;
    events.callback = []() {
        throw std::runtime_error( "restore fixture" );
    };
    get_event_bus().subscribe( &events );
    CHECK_THROWS_AS( end( id ), std::runtime_error );
    CHECK( find( who, original ) );
    CHECK( grant( id ).stowed == 0 );
    CHECK( grant( id ).restored == 1 );
    g->abilities().drain_starts();
    CHECK( grant( id ).state == "ended" );
    CHECK( who.items_with( [original]( const item & value ) {
        return value.uid().get_value() == original;
    } ).size() == 1 );
    const auto *ends = get_globals().maybe_get_global_value( "form_ends" );
    REQUIRE( ends );
    CHECK( ends->dbl() == 1 );
}

TEST_CASE( "unloaded_wardrobe_cleanup_is_saved_without_adopting_a_new_target",
           "[power_grants][save]" )
{
    grant_world world;
    auto &who = *actor( "npc" ).as_character();
    shirt( who );
    const auto identity = who.getID();
    const auto id = apply( get_avatar(), who, "test_grant_form" );
    auto far = who.pos_abs() + tripoint_rel_ms( 500, 0, 0 );
    if( project_to<coords::om>( far ) != project_to<coords::om>( who.pos_abs() ) ) {
        far = who.pos_abs() + tripoint_rel_ms( -500, 0, 0 );
    }
    who.setpos( far, false );
    get_powers().retire_actor( who );
    g->remove_npc( identity );
    tick();
    CHECK( grant( id ).state == "cleanup" );
    CHECK( grant( id ).stowed == 1 );
    restore( save() );
    tick();
    CHECK( grant( id ).state == "cleanup" );
    CHECK( grant( id ).stowed == 1 );
    auto &replacement = *actor( "npc" ).as_character();
    const auto other = apply( get_avatar(), replacement, "test_grant_form" );
    CHECK( view( other ).state == "active" );
    CHECK( grant( id ).stowed == 1 );
}

TEST_CASE( "projected_equipment_cleanup_reaches_loaded_vehicle_cargo", "[power_grants]" )
{
    grant_world world;
    auto &who = get_avatar();
    const auto id = apply( who, who, "test_grant_weapon" );
    vehicle *cart = get_map().add_vehicle( vproto_id( "shopping_cart" ),
                                           who.pos_bub() + tripoint_rel_ms( 4, 0, 0 ), 0_degrees );
    REQUIRE( cart );
    const auto cargo = cart->get_any_parts( "CARGO" );
    REQUIRE( cargo.begin() != cargo.end() );
    auto &part = cargo.begin()->part();
    REQUIRE( cart->add_item( get_map(), part, *who.get_wielded_item() ) );
    REQUIRE( cart->add_item( get_map(), part, item( itype_id( "rock" ), calendar::turn ) ) );
    const auto before_end = cart->get_items( part ).size();
    end( id );
    const auto &items = cart->get_items( part );
    CHECK( items.size() == before_end - 1 );
    for( const item &value : items ) {
        CHECK_FALSE( value.has_var( "power_grant_application" ) );
    }
}

TEST_CASE( "forms_preserve_wielded_items_when_a_style_forces_unarmed_combat", "[power_grants]" )
{
    grant_world world;
    auto &who = get_avatar();
    item original( itype_id( "knuckle_brass" ), calendar::turn );
    const auto uid = original.uid().get_value();
    REQUIRE( who.wield_power_item( original ) );
    who.martial_arts_data->set_style( matype_id( "style_kicks" ), true );
    REQUIRE_FALSE( who.has_weapon() );
    REQUIRE( who.get_wielded_item() );
    CHECK( get_powers().apply( request( who, who, "test_grant_weapon" ) ).reason == "weapon_occupied" );
    const auto id = apply( who, who, "test_grant_form" );
    CHECK( grant( id ).stowed == 1 );
    end( id );
    REQUIRE( who.get_wielded_item() );
    CHECK( who.get_wielded_item()->uid().get_value() == uid );
}

TEST_CASE( "restoration_rechecks_a_target_unloaded_by_its_first_wear_callback", "[power_grants]" )
{
    grant_world world;
    auto &who = *actor( "npc" ).as_character();
    shirt( who );
    REQUIRE( who.wear_item( item( itype_id( "backpack" ), calendar::turn ), false ) );
    const auto identity = who.getID();
    const auto id = apply( get_avatar(), who, "test_grant_form" );
    observer events;
    events.wanted = event_type::character_wears_item;
    events.callback = [&]() {
        auto far = who.pos_abs() + tripoint_rel_ms( 500, 0, 0 );
        if( project_to<coords::om>( far ) != project_to<coords::om>( who.pos_abs() ) ) {
            far = who.pos_abs() + tripoint_rel_ms( -500, 0, 0 );
        }
        who.setpos( far, false );
        get_powers().retire_actor( who );
        g->remove_npc( identity );
    };
    get_event_bus().subscribe( &events );
    end( id );
    CHECK( grant( id ).state == "cleanup" );
    CHECK( grant( id ).restored == 1 );
    CHECK( grant( id ).stowed == 1 );
    restore( save() );
    g->abilities().drain_starts();
    CHECK( grant( id ).state == "cleanup" );
    CHECK( grant( id ).stowed == 1 );
}
