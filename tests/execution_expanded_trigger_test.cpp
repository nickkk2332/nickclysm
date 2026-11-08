#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "coordinates.h"
#include "creature.h"
#include "creature_ref.h"
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
#include "item.h"
#include "item_uid.h"
#include "json.h"
#include "json_loader.h"
#include "magic.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser_diag_value.h"
#include "mattack_common.h"
#include "messages.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "options_helpers.h"
#include "player_helpers.h"
#include "point.h"
#include "projectile.h"
#include "type_id.h"

static const itype_id itype_execution_expanded_throw( "execution_expanded_throw" );

static const skill_id skill_throw( "throw" );

static const spell_id spell_execution_expanded_spell( "execution_expanded_spell" );
static const spell_id spell_execution_expanded_spell_area( "execution_expanded_spell_area" );

namespace
{
struct expanded_world {
    time_point old_time = calendar::turn;
    quit_status old_quit = g->uquit;
    override_option animations{ "ANIMATION_PROJECTILES", "false" };
    override_option deathcam{ "DEATHCAM", "never" };
    expanded_world() {
        g->executions().clear();
        clear_avatar();
        clear_map_without_vision( -2, 1 );
        get_globals().clear_global_values();
        Messages::clear_messages();
        calendar::turn = calendar::turn_zero + 12_hours;
        get_avatar().setpos( get_map(), tripoint_bub_ms( 60, 60, 0 ) );
        get_avatar().set_all_parts_hp_cur( 10000 );
        g->uquit = QUIT_NO;
        get_globals().set_global_value( "exec_expanded_case", "living" );
    }
    ~expanded_world() {
        g->executions().clear();
        clear_avatar();
        Messages::clear_messages();
        calendar::turn = old_time;
        g->uquit = old_quit;
    }
};

monster &victim( const Creature &source, int hp = 10000 )
{
    monster &target = spawn_test_monster( "mon_execution_expanded_victim",
                                          source.pos_bub() + tripoint_rel_ms( 2, 0, 0 ) );
    target.set_hp( hp );
    return target;
}

void equip_thrower( Character &source )
{
    source.set_str_base( 40 );
    source.set_dex_base( 30 );
    source.reset_stats();
    source.set_skill_level( skill_throw, 10 );
    source.set_moves( 1000 );
}

void throw_at( Character &source, Creature &target )
{
    equip_thrower( source );
    const item knife( itype_execution_expanded_throw );
    source.throw_item( target.pos_bub(), knife );
}

execution_trigger_report report()
{
    const auto result = g->executions().last_trigger_report();
    REQUIRE( result );
    INFO( result->reason );
    return *result;
}

execution_view selected()
{
    const execution_trigger_report decision = report();
    REQUIRE( decision.instance != 0 );
    const auto scene = g->executions().inspect( decision.instance );
    REQUIRE( scene );
    return *scene;
}

void tick( int count = 1 )
{
    for( int i = 0; i < count; ++i ) {
        calendar::turn += 1_turns;
        g->executions().process();
    }
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

TEST_CASE( "execution_thrown_native_avatar_and_npc_contacts_keep_original_item_and_costs",
           "[executions][expanded_execution]" )
{
    expanded_world world;
    const bool npc_source = GENERATE( false, true );
    Character &source = npc_source ? static_cast<Character &>( spawn_npc(
                            get_avatar().pos_bub().xy() + point_rel_ms( 0, -4 ), "thug" ) ) :
                        static_cast<Character &>( get_avatar() );
    equip_thrower( source );
    Creature &target = victim( source );
    const int hp = target.get_hp();
    const item knife( itype_execution_expanded_throw );
    const std::string uid = std::to_string( knife.uid().get_value() );
    source.throw_item( target.pos_bub(), knife );
    const execution_view scene = selected();
    CHECK( scene.state == "pending" );
    CHECK( scene.context.at( "attack_channel" ) == "thrown" );
    CHECK( scene.context.at( "weapon" ) == "execution_expanded_throw" );
    CHECK( scene.context.at( "weapon_uid" ) == uid );
    CHECK( scene.context.at( "weapon_skill" ) == "throw" );
    CHECK( scene.context.count( "ammunition" ) == 0 );
    CHECK( scene.context.count( "style" ) == 0 );
    CHECK( scene.context.at( "hp_loss" ).dbl() == hp - target.get_hp() );
    CHECK( source.get_moves() < 1000 );
    CHECK( scene.context.at( "projectile_origin" ).tripoint() == source.pos_abs() );
    g->executions().drain_starts();
    CHECK( g->executions().inspect( scene.id )->state == "running" );
}

TEST_CASE( "execution_spell_native_casters_capture_exact_effect_without_weapon_facts",
           "[executions][expanded_execution]" )
{
    expanded_world world;
    const std::string kind = GENERATE( "avatar", "npc", "monster" );
    Creature *source = &get_avatar();
    if( kind == "npc" ) {
        source = &spawn_npc( get_avatar().pos_bub().xy() + point_rel_ms( 0, -4 ), "thug" );
    } else if( kind == "monster" ) {
        source = &spawn_test_monster( "mon_execution_expanded_attacker",
                                      get_avatar().pos_bub() + tripoint_rel_ms( 0, -4, 0 ) );
    }
    Creature &target = victim( *source );
    const int hp = target.get_hp();
    spell effect( spell_execution_expanded_spell, 1 );
    effect.set_level( get_avatar(), 1 );
    effect.cast_all_effects( *source, target.pos_bub() );
    const execution_view scene = selected();
    CHECK( scene.context.at( "attack_channel" ) == "spell" );
    CHECK( scene.context.at( "spell" ) == effect.id().str() );
    CHECK( scene.context.at( "spell_level" ).dbl() == 1 );
    CHECK( scene.context.at( "hp_loss" ).dbl() == hp - target.get_hp() );
    CHECK( scene.context.at( "damage_pure" ).dbl() > 0 );
    CHECK( scene.context.count( "weapon" ) == 0 );
    CHECK( scene.context.count( "ammunition" ) == 0 );
    CHECK( scene.context.count( "projectile_origin" ) == 0 );
}

TEST_CASE( "execution_spell_aoe_offers_one_packet_for_each_real_victim",
           "[executions][expanded_execution]" )
{
    expanded_world world;
    Creature &first = victim( get_avatar() );
    Creature &second = spawn_test_monster( "mon_execution_expanded_victim",
                                           first.pos_bub() + tripoint_rel_ms( 0, 1, 0 ) );
    spell effect( spell_execution_expanded_spell_area, 1 );
    effect.cast_all_effects( get_avatar(), first.pos_bub() );
    const auto history = g->executions().trigger_history();
    REQUIRE( history.size() == 2 );
    CHECK( history[0].instance != 0 );
    CHECK( history[1].instance != 0 );
    CHECK_FALSE( history[0].target == history[1].target );
    CHECK( first.get_hp() < first.get_hp_max() );
    CHECK( second.get_hp() < second.get_hp_max() );
}

TEST_CASE( "execution_spell_split_percentage_and_multishot_keep_one_first_packet",
           "[executions][expanded_execution]" )
{
    expanded_world world;
    const std::string suffix = GENERATE( "split", "percentage", "multi" );
    npc &target = spawn_npc( get_avatar().pos_bub().xy() + point_rel_ms( 2, 0 ), "thug" );
    target.clear_worn();
    target.set_all_parts_hp_cur( 10000 );
    const int hp = target.get_hp();
    spell effect( spell_id( "execution_expanded_spell_" + suffix ), 1 );
    effect.cast_all_effects( get_avatar(), target.pos_bub() );
    const execution_view scene = selected();
    CHECK( g->executions().trigger_history().size() == 1 );
    CHECK( scene.context.at( "hp_loss" ).dbl() > 0 );
    CHECK( scene.context.at( "hp_loss" ).dbl() < hp - target.get_hp() );
    CHECK( scene.context.count( "bp" ) == 1 );
}

TEST_CASE( "execution_spell_extra_effects_are_children_of_the_original_cast",
           "[executions][expanded_execution]" )
{
    expanded_world world;
    Creature &target = victim( get_avatar() );
    const int hp = target.get_hp();
    const std::string suffix = GENERATE( "extra", "extra_first", "wonder" );
    spell effect( spell_id( "execution_expanded_spell_" + suffix ), 1 );
    effect.cast_all_effects( get_avatar(), target.pos_bub() );
    if( suffix == "wonder" ) {
        CHECK( target.get_hp() < hp );
        CHECK_FALSE( g->executions().last_trigger_report() );
        return;
    }
    const execution_view scene = selected();
    CHECK( scene.context.at( "spell" ) == effect.id().str() );
    CHECK( g->executions().trigger_history().size() == 1 );
    CHECK( hp - target.get_hp() > scene.context.at( "hp_loss" ).dbl() );
}

TEST_CASE( "execution_spell_cast_event_children_do_not_acquire_an_automatic_root",
           "[executions][expanded_execution]" )
{
    expanded_world world;
    Creature &target = victim( get_avatar() );
    spell effect( spell_execution_expanded_spell, 1 );
    events observed;
    bool child = false;
    observed.callback = [&]( const cata::event & e ) {
        if( e.type() == event_type::character_casts_spell && !child ) {
            child = true;
            throw_at( get_avatar(), target );
        }
    };
    get_event_bus().subscribe( &observed );
    effect.cast_all_effects( get_avatar(), target.pos_bub() );
    CHECK( child );
    CHECK( selected().context.at( "attack_channel" ) == "spell" );
    CHECK( g->executions().trigger_history().size() == 1 );
}

TEST_CASE( "execution_spell_source_less_and_unwrapped_effects_remain_outside_the_producer",
           "[executions][expanded_execution]" )
{
    expanded_world world;
    const bool no_source = GENERATE( false, true );
    Creature &target = victim( get_avatar() );
    const int hp = target.get_hp();
    spell effect( spell_execution_expanded_spell, 1 );
    if( no_source ) {
        effect.cast_all_effects( target.pos_bub() );
    } else {
        spell_effect::attack( effect, get_avatar(), target.pos_bub() );
    }
    CHECK( target.get_hp() < hp );
    CHECK_FALSE( g->executions().last_trigger_report() );
}

TEST_CASE( "execution_json_monster_melee_special_uses_its_own_id_and_primary_damage",
           "[executions][expanded_execution]" )
{
    expanded_world world;
    monster &source = spawn_test_monster( "mon_execution_expanded_attacker",
                                          get_avatar().pos_bub() + tripoint_rel_ms::east );
    source.set_dest( get_avatar().pos_abs() );
    source.set_moves( 1000 );
    const int hp = get_avatar().get_hp();
    REQUIRE( source.type->special_attacks.at( "execution_expanded_special" )->call( source ) );
    const execution_view scene = selected();
    CHECK( scene.context.at( "attack_channel" ) == "monster_special" );
    CHECK( scene.context.at( "monster_attack" ) == "execution_expanded_special" );
    CHECK( scene.context.at( "bp" ) == "torso" );
    CHECK( scene.context.at( "hp_loss" ).dbl() == hp - get_avatar().get_hp() );
    CHECK( scene.context.count( "weapon" ) == 0 );
    CHECK( scene.context.count( "technique" ) == 0 );
    CHECK( scene.context.count( "crit" ) == 0 );
    CHECK( source.get_moves() == 900 );
}

TEST_CASE( "execution_additional_producers_reserve_fatal_primary_commits_and_resume",
           "[executions][expanded_execution][save]" )
{
    expanded_world world;
    get_globals().set_global_value( "exec_expanded_case", "fatal" );
    const std::string producer = GENERATE( "thrown", "spell", "monster_special" );
    Creature *target = nullptr;
    if( producer == "monster_special" ) {
        monster &source = spawn_test_monster( "mon_execution_expanded_attacker",
                                              get_avatar().pos_bub() + tripoint_rel_ms::east );
        source.set_dest( get_avatar().pos_abs() );
        get_avatar().set_all_parts_hp_cur( 1 );
        target = &get_avatar();
        REQUIRE( source.type->special_attacks.at( "execution_expanded_special" )->call( source ) );
    } else {
        target = &victim( get_avatar(), 1 );
        if( producer == "thrown" ) {
            throw_at( get_avatar(), *target );
        } else {
            spell effect( spell_execution_expanded_spell, 1 );
            effect.cast_all_effects( get_avatar(), target->pos_bub() );
        }
    }
    const execution_view scene = selected();
    CHECK( scene.context.at( "attack_channel" ) == producer );
    CHECK( scene.context.at( "attack_event" ) == "fatal_hit" );
    CHECK( execution_fatal::pending( *target ) );
    CHECK( target->is_dead_state() );
    CHECK( g->executions().inspect_all().back().details.at( "fatal_ready" ).dbl() == 1 );
    g->executions().drain_starts();
    std::ostringstream text;
    JsonOut out( text );
    g->executions().serialize( out );
    g->executions().reset_for_load();
    g->executions().deserialize( json_loader::from_string( text.str() ) );
    g->executions().end_restore();
    CHECK( execution_fatal::pending( *target ) );
    tick( 3 );
    CHECK_FALSE( execution_fatal::pending( *target ) );
    CHECK( execution_fatal::finalized( *target ) );
    CHECK( g->executions().inspect( scene.id )->outcome == "killed" );
}

TEST_CASE( "execution_additional_producer_exception_settles_real_injury_and_unwinds_guards",
           "[executions][expanded_execution]" )
{
    expanded_world world;
    const bool throwing = GENERATE( false, true );
    get_globals().set_global_value( "exec_expanded_case", "fatal" );
    Creature &target = victim( get_avatar(), 1 );
    events observed;
    bool threw = false;
    observed.callback = [&]( const cata::event & e ) {
        if( e.type() == event_type::monster_takes_damage && !threw ) {
            threw = true;
            throw std::runtime_error( "Abort native producer callback" );
        }
    };
    get_event_bus().subscribe( &observed );
    if( throwing ) {
        CHECK_THROWS_AS( throw_at( get_avatar(), target ), std::runtime_error );
    } else {
        spell effect( spell_execution_expanded_spell, 1 );
        CHECK_THROWS_AS( effect.cast_all_effects( get_avatar(), target.pos_bub() ), std::runtime_error );
    }
    CHECK( threw );
    CHECK_FALSE( execution_combat::attack_in_progress() );
    CHECK_FALSE( execution_fatal::pending( target ) );
    CHECK( execution_fatal::finalized( target ) );
}

TEST_CASE( "execution_expanded_schema_requires_explicit_channels_and_valid_hit_events",
           "[executions][expanded_execution]" )
{
    expanded_world world;
    const std::string channel = GENERATE( "thrown", "spell", "monster_special" );
    execution_trigger rule;
    const std::string text = R"({"execution":"test_exec_expanded_hold","match":{"channels":[")" +
                             channel + R"("]}})";
    CHECK_NOTHROW( rule.load( json_loader::from_string( text ), "test_data" ) );
    CHECK( rule.channels.size() == 1 );
    rule.was_loaded = true;
    CHECK_THROWS_AS( rule.load( json_loader::from_string( R"({"match":{"hit":false}})" ),
                                "test_data" ), JsonError );
}
