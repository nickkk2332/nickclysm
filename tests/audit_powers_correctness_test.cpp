#include <functional>
#include <memory>
#include <string>

#include "avatar.h"
#include "bodypart.h"
#include "cata_catch.h"
#include "condition.h"
#include "coordinates.h"
#include "creature.h"
#include "debug.h"
#include "dialogue.h"
#include "enums.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "item.h"
#include "json_loader.h"
#include "magic.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser_diag_value.h"
#include "monster.h"
#include "npc.h"
#include "pimpl.h"
#include "player_helpers.h"
#include "point.h"
#include "talker.h"
#include "type_id.h"
#include "units.h"
#include "vehicle.h"

static const itype_id itype_rock( "rock" );

static const spell_id spell_test_twelve_dash( "test_twelve_dash" );
static const spell_id spell_test_twelve_magical( "test_twelve_magical" );
static const spell_id spell_test_twelve_nonmagical( "test_twelve_nonmagical" );
static const spell_id
spell_test_twelve_nonmagical_components( "test_twelve_nonmagical_components" );
static const spell_id spell_test_twelve_nonmagical_condition( "test_twelve_nonmagical_condition" );

static const ter_str_id ter_t_wall( "t_wall" );

static const trait_id trait_test_twelve_no_cast( "test_twelve_no_cast" );

static const trap_str_id test_twelve_dash_trap( "test_twelve_dash_trap" );

static const vpart_id vpart_board( "board" );
static const vpart_id vpart_frame( "frame" );

static const vproto_id vehicle_prototype_none( "none" );

namespace
{
talk_effect_t powers_effect( const std::string &text )
{
    const JsonObject jo = json_loader::from_string( text );
    talk_effect_t result( jo, "effect", "audit_powers_correctness" );
    finalize_conditions();
    return result;
}
} // namespace

TEST_CASE( "powers_damage_keeps_receiver_and_source_separate", "[audit][powers][eoc]" )
{
    clear_avatar();
    clear_map_without_vision();
    get_globals().clear_global_values();
    avatar &attacker = get_avatar();
    attacker.set_value( "twelve_source", "attacker" );
    const int attacker_hp = attacker.get_part_hp_cur( bodypart_id( "torso" ) );
    const talk_effect_t effect = powers_effect( R"({ "effect": {
        "npc_deal_damage": "test_twelve_damage", "amount": 8, "bodypart": "torso", "source": "u"
    } })" );

    SECTION( "a Character receiver" ) {
        npc &victim = spawn_npc( attacker.pos_bub().xy() + point_rel_ms( 2, 0 ), "thug" );
        const int hp = victim.get_part_hp_cur( bodypart_id( "torso" ) );
        dialogue d( get_talker_for( attacker ), get_talker_for( victim ) );
        effect.apply( d );
        CHECK( victim.get_part_hp_cur( bodypart_id( "torso" ) ) == hp - 8 );
    }
    SECTION( "a monster receiver" ) {
        // Its one weakpoint covers it all and changes nothing: a zombie's are rolled at random
        // and some multiply the damage
        monster &victim = spawn_test_monster( "mon_execution_trigger_test",
                                              attacker.pos_bub() + tripoint_rel_ms( 2, 0, 0 ) );
        const int hp = victim.get_hp();
        dialogue d( get_talker_for( attacker ), get_talker_for( victim ) );
        effect.apply( d );
        CHECK( victim.get_hp() == hp - 8 );
    }
    CHECK( attacker.get_part_hp_cur( bodypart_id( "torso" ) ) == attacker_hp );
    CHECK( get_globals().get_global_value( "twelve_damage_source" ) == "attacker" );
}

TEST_CASE( "powers_damage_supports_beta_and_environmental_sources", "[audit][powers][eoc]" )
{
    clear_avatar();
    clear_map_without_vision();
    get_globals().clear_global_values();
    avatar &victim = get_avatar();
    victim.clear_killer();
    monster &attacker = spawn_test_monster( "mon_zombie", victim.pos_bub() + tripoint_rel_ms( 2, 0,
                                            0 ) );
    attacker.set_value( "twelve_source", "monster" );
    const int attacker_hp = attacker.get_hp();
    dialogue d( get_talker_for( victim ), get_talker_for( attacker ) );
    const std::string source = GENERATE( std::string( "npc" ), std::string( "none" ), std::string() );
    const std::string source_member = source.empty() ? "" : R"(, "source": ")" + source + '"';
    powers_effect( R"({ "effect": { "u_deal_damage": "test_twelve_damage", "amount": 1000,
                    "bodypart": "torso")" + source_member + " } }" ).apply( d );
    CHECK( victim.is_dead_state() );
    CHECK( attacker.get_hp() == attacker_hp );
    CHECK( victim.get_killer() == ( source == "npc" ? &attacker : nullptr ) );
    CHECK( get_globals().get_global_value( "twelve_damage_source" ) ==
           ( source == "npc" ? "monster" : "environment" ) );
}

TEST_CASE( "powers_damage_rejects_an_unavailable_explicit_source", "[audit][powers][eoc]" )
{
    clear_avatar();
    clear_map_without_vision();
    avatar &victim = get_avatar();
    const int hp = victim.get_part_hp_cur( bodypart_id( "torso" ) );
    dialogue d( get_talker_for( victim ), nullptr );
    const talk_effect_t effect = powers_effect( R"({ "effect": { "u_deal_damage": "pure",
        "amount": 10, "bodypart": "torso", "source": "npc" } })" );
    CHECK( capture_debugmsg_during( [&]() {
        effect.apply( d );
    } ).find( "is not a creature" ) != std::string::npos );
    CHECK( victim.get_part_hp_cur( bodypart_id( "torso" ) ) == hp );
}

TEST_CASE( "powers_targeted_cast_cancellation_does_not_run_success_callbacks",
           "[audit][powers][magic][eoc]" )
{
    clear_avatar();
    clear_map_without_vision();
    get_globals().clear_global_values();
    avatar &u = get_avatar();
    const bool cancelled = GENERATE( false, true );
    const std::string id = cancelled ? "test_twelve_target_cast" : "test_twelve_self_cast";
    dialogue d( get_talker_for( u ), nullptr );
    powers_effect( R"({ "effect": { "u_cast_spell": { "id": ")" + id + R"(" }, "targeted": true,
        "true_eocs": [ "EOC_TEST_TWELVE_CAST_OK" ],
        "false_eocs": [ "EOC_TEST_TWELVE_CAST_CANCELLED" ]
    } })" ).apply( d );
    if( cancelled ) {
        CHECK_FALSE( get_globals().maybe_get_global_value( "twelve_cast_ok" ) );
        CHECK_FALSE( get_globals().maybe_get_global_value( "twelve_cast_effect" ) );
        CHECK( get_globals().get_global_value( "twelve_cast_cancelled" ) == 1 );
        CHECK( d.get_value( "reason" ) == "target_cancelled" );
    } else {
        CHECK( get_globals().get_global_value( "twelve_cast_ok" ) == 1 );
        CHECK( get_globals().get_global_value( "twelve_cast_effect" ) == 1 );
        CHECK_FALSE( get_globals().maybe_get_global_value( "twelve_cast_cancelled" ) );
    }
}

TEST_CASE( "powers_nonmagical_casts_still_require_energy_condition_and_components",
           "[audit][powers][magic]" )
{
    clear_avatar();
    clear_map_without_vision();
    avatar &u = get_avatar();
    const spell ordinary( spell_test_twelve_nonmagical );
    u.magic->set_mana( 0 );
    CHECK_FALSE( ordinary.can_cast( u ) );
    u.magic->set_mana( 1000 );
    CHECK( ordinary.can_cast( u ) );
    u.set_mutation( trait_test_twelve_no_cast );
    CHECK( ordinary.can_cast( u ) );
    CHECK_FALSE( spell( spell_test_twelve_magical ).can_cast( u ) );
    CHECK_FALSE( spell( spell_test_twelve_nonmagical_condition ).can_cast( u ) );
    const spell components( spell_test_twelve_nonmagical_components );
    CHECK_FALSE( components.can_cast( u ) );
    u.i_add( item( itype_rock ) );
    u.invalidate_crafting_inventory();
    CHECK( components.can_cast( u ) );
    // Check the NPC eligibility path too; it uses the same ordinary requirements.
    npc &other = spawn_npc( u.pos_bub().xy() + point_rel_ms( 3, 0 ), "thug" );
    other.magic->set_mana( 0 );
    CHECK_FALSE( ordinary.can_cast( other ) );
    other.magic->set_mana( 1000 );
    CHECK( ordinary.can_cast( other ) );
}

TEST_CASE( "powers_dash_moves_each_caster_kind_and_stops_at_blockers", "[audit][powers][magic]" )
{
    clear_avatar();
    clear_map_without_vision();
    get_globals().clear_global_values();
    map &here = get_map();
    const tripoint_bub_ms start( 55, 55, 0 );
    const tripoint_abs_ms start_abs = here.get_abs( start );
    get_avatar().setpos( here, tripoint_bub_ms( 60, 60, 0 ) );
    const int caster_kind = GENERATE( 0, 1, 2 );
    Creature *caster;
    if( caster_kind == 1 ) {
        caster = &spawn_npc( start.xy(), "thug" );
    } else if( caster_kind == 0 ) {
        caster = &spawn_test_monster( "mon_zombie", start );
    } else {
        get_avatar().setpos( here, start );
        caster = &get_avatar();
    }
    const spell sp( spell_test_twelve_dash );
    const int moves = caster->get_moves();
    SECTION( "clear path" ) {
        spell_effect::dash( sp, *caster, start + tripoint_rel_ms( 4, 0, 0 ) );
        CHECK( caster->pos_abs() == start_abs + tripoint_rel_ms( 4, 0, 0 ) );
    }
    SECTION( "wall" ) {
        here.ter_set( start + tripoint_rel_ms( 2, 0, 0 ), ter_t_wall );
        spell_effect::dash( sp, *caster, start + tripoint_rel_ms( 4, 0, 0 ) );
        CHECK( caster->pos_abs() == start_abs + tripoint_rel_ms( 1, 0, 0 ) );
    }
    SECTION( "occupied tile" ) {
        monster &blocker = spawn_test_monster( "mon_zombie", start + tripoint_rel_ms( 2, 0, 0 ) );
        spell_effect::dash( sp, *caster, start + tripoint_rel_ms( 4, 0, 0 ) );
        CHECK( caster->pos_abs() == start_abs + tripoint_rel_ms( 1, 0, 0 ) );
        CHECK( blocker.pos_abs() == start_abs + tripoint_rel_ms( 2, 0, 0 ) );
    }
    SECTION( "a blocking vehicle part" ) {
        vehicle *veh = here.add_vehicle( vehicle_prototype_none, start + tripoint_rel_ms( 2, 0, 0 ),
                                         0_degrees, 0, veh_spawn_status::UNDAMAGED );
        REQUIRE( veh != nullptr );
        REQUIRE( veh->install_part( here, point_rel_ms::zero, vpart_frame ) != -1 );
        REQUIRE( veh->install_part( here, point_rel_ms::zero, vpart_board ) != -1 );
        here.add_vehicle_to_cache( veh );
        REQUIRE( here.impassable( start + tripoint_rel_ms( 2, 0, 0 ) ) );
        spell_effect::dash( sp, *caster, start + tripoint_rel_ms( 4, 0, 0 ) );
        CHECK( caster->pos_abs() == start_abs + tripoint_rel_ms( 1, 0, 0 ) );
    }
    SECTION( "a trap displaces the caster and interrupts the trajectory" ) {
        here.trap_set( start + tripoint_rel_ms( 1, 0, 0 ), test_twelve_dash_trap.id() );
        spell_effect::dash( sp, *caster, start + tripoint_rel_ms( 4, 0, 0 ) );
        CHECK( get_globals().get_global_value( "twelve_dash_trap" ) == 1 );
        CHECK( caster->pos_abs() == start_abs + tripoint_rel_ms( 1, 1, 0 ) );
    }
    SECTION( "the attack starts at the reached location" ) {
        monster &target = spawn_test_monster( "mon_zombie", start + tripoint_rel_ms( 6, 0, 0 ) );
        const int hp = target.get_hp();
        spell_effect::dash( sp, *caster, start + tripoint_rel_ms( 4, 0, 0 ) );
        CHECK( caster->pos_abs() == start_abs + tripoint_rel_ms( 4, 0, 0 ) );
        if( caster_kind != 0 ) {
            CHECK( target.get_hp() < hp );
        }
    }
    SECTION( "self target" ) {
        spell_effect::dash( sp, *caster, start );
        CHECK( caster->pos_abs() == start_abs );
    }
    CHECK( caster->get_moves() == moves );
}
