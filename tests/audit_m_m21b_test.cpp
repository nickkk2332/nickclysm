#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "coordinates.h"
#include "creature.h"
#include "creature_tracker.h"
#include "item.h"
#include "item_location.h"
#include "itype.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "line.h"
#include "magic.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "memory_fast.h"
#include "monster.h"
#include "npc.h"
#include "npc_ai_tuning.h"
#include "npc_attack.h"
#include "player_helpers.h"
#include "point.h"
#include "stomach.h"
#include "type_id.h"
#include "units.h"

static const efftype_id effect_pkill_l( "pkill_l" );
static const efftype_id effect_pkill_wintergreen( "pkill_wintergreen" );
static const efftype_id effect_poison( "poison" );

static const faction_id faction_your_followers( "your_followers" );

static const itype_id itype_aspirin( "aspirin" );
static const itype_id itype_codeine( "codeine" );
static const itype_id itype_debug_backpack( "debug_backpack" );
static const itype_id itype_heroin( "heroin" );
static const itype_id itype_syringe( "syringe" );
static const itype_id itype_test_m21b_antidote( "test_m21b_antidote" );
static const itype_id itype_test_m21b_drink( "test_m21b_drink" );
static const itype_id itype_test_m21b_medicine( "test_m21b_medicine" );
static const itype_id itype_test_m21b_painkiller( "test_m21b_painkiller" );

static const spell_id spell_test_m21b_gated_attack( "test_m21b_gated_attack" );
static const spell_id spell_test_m21b_heal_ally( "test_m21b_heal_ally" );
static const spell_id spell_test_m21b_never( "test_m21b_never" );
static const spell_id spell_test_m21b_plain_attack( "test_m21b_plain_attack" );
static const spell_id spell_test_m21b_self_buff( "test_m21b_self_buff" );
static const spell_id spell_test_m21b_summon( "test_m21b_summon" );

static const ter_str_id ter_t_swater_sh( "t_swater_sh" );
static const ter_str_id ter_t_test_m21b_drink_source( "t_test_m21b_drink_source" );
static const ter_str_id ter_t_water_sh( "t_water_sh" );

// An NPC on a clear, lit map, with the avatar out of its sight underground; nothing else.
static npc &m21b_lone_npc()
{
    clear_map_without_vision();
    clear_avatar();
    clear_npcs();
    clear_creatures();
    set_time_to_day();
    avatar &u = get_avatar();
    npc &guy = spawn_npc( u.pos_bub().xy() + point::east * 5, "test_talker" );
    clear_character( guy );
    u.setpos( get_map(), tripoint_bub_ms( 0, 0, -2 ) );
    get_map().build_map_cache( 0 );
    return guy;
}

// NPC-B16: assess_danger() put every monster that wasn't hostile to an is_enemy() NPC (a deer
// for a bandit) in its hostile list and counted its threat, though attitude_to() rates no attack
// on it; NPCs that weren't NEUTRAL, FRIENDLY ones too, counted as hostile. The never-read
// neutral list is gone.
TEST_CASE( "npc-b16_hostile_npc_does_not_count_neutral_monsters_as_threats", "[audit][npc]" )
{
    on_out_of_scope cleanup( []() {
        clear_creatures();
        clear_npcs();
    } );
    npc &bandit = m21b_lone_npc();
    bandit.set_attitude( NPCATT_KILL );
    REQUIRE( bandit.is_enemy() );
    map &here = get_map();

    monster &calm = spawn_test_monster( "mon_zombie", bandit.pos_bub() + tripoint::east * 2 );
    calm.aggro_character = false;
    REQUIRE( calm.attitude_to( bandit ) == Creature::Attitude::NEUTRAL );
    REQUIRE( bandit.attitude_to( calm ) == Creature::Attitude::NEUTRAL );
    REQUIRE( bandit.sees( here, calm ) );

    bandit.regen_ai_cache();
    CHECK( bandit.mem_combat.assess_enemy == 0.0f );
    CHECK( bandit.current_target() == nullptr );

    monster &angry = spawn_test_monster( "mon_zombie", bandit.pos_bub() + tripoint::south * 2 );
    angry.anger = 100;
    angry.morale = 100;
    angry.aggro_character = true;
    REQUIRE( angry.attitude_to( bandit ) == Creature::Attitude::HOSTILE );
    REQUIRE( bandit.sees( here, angry ) );

    bandit.regen_ai_cache();
    CHECK( bandit.mem_combat.assess_enemy > 0.0f );
    CHECK( bandit.current_target() != &calm );
}

// NPC-J06: every spell was rated as an attack by its damage, so NPCs never cast buffs, heals
// that aren't negative damage, or summons. A spell's "npc_use" now gives it a role, a condition
// and a priority.
TEST_CASE( "npc-j06_npc_spell_roles_from_npc_use", "[audit][npc]" )
{
    on_out_of_scope cleanup( []() {
        clear_creatures();
        clear_npcs();
    } );
    npc &guy = m21b_lone_npc();
    monster &zombie = spawn_test_monster( "mon_zombie", guy.pos_bub() + tripoint::east );

    SECTION( "never: NPCs don't cast it, a plain copy is rated" ) {
        guy.magic->learn_spell( spell_test_m21b_plain_attack, guy, true );
        guy.magic->learn_spell( spell_test_m21b_never, guy, true );
        CHECK( npc_attack_spell( spell_test_m21b_plain_attack ).evaluate( guy, &zombie ).value() );
        CHECK_FALSE( npc_attack_spell( spell_test_m21b_never ).evaluate( guy, &zombie ).value() );
    }

    SECTION( "an attack's condition gates it" ) {
        guy.magic->learn_spell( spell_test_m21b_gated_attack, guy, true );
        const npc_attack_spell gated( spell_test_m21b_gated_attack );
        guy.set_value( "test_m21b_allow", 0 );
        CHECK_FALSE( gated.evaluate( guy, &zombie ).value() );
        CHECK( gated.all_evaluations( guy, &zombie ).empty() );
        guy.set_value( "test_m21b_allow", 1 );
        CHECK( gated.evaluate( guy, &zombie ).value() );
    }

    SECTION( "self: worth its priority, cast on the caster while the condition holds" ) {
        guy.magic->learn_spell( spell_test_m21b_self_buff, guy, true );
        const npc_attack_spell buff( spell_test_m21b_self_buff );
        guy.set_value( "test_m21b_buffed", 0 );
        const npc_attack_rating rating = buff.evaluate( guy, &zombie );
        REQUIRE( rating.value() );
        // 30, less a point a second of casting
        CHECK( *rating.value() > 20 );
        CHECK( *rating.value() <= 30 );
        CHECK( rating.target() == guy.pos_bub() );
        guy.set_value( "test_m21b_buffed", 1 );
        CHECK_FALSE( buff.evaluate( guy, &zombie ).value() );
    }

    SECTION( "self, cast on the ground: next to the caster" ) {
        guy.magic->learn_spell( spell_test_m21b_summon, guy, true );
        const npc_attack_rating rating = npc_attack_spell( spell_test_m21b_summon ).evaluate( guy,
                                         &zombie );
        REQUIRE( rating.value() );
        CHECK( rl_dist( rating.target(), guy.pos_bub() ) == 1 );
        CHECK( get_creature_tracker().creature_at( rating.target() ) == nullptr );
    }

    SECTION( "ally: on the caster when the condition picks it" ) {
        guy.magic->learn_spell( spell_test_m21b_heal_ally, guy, true );
        const npc_attack_spell heal( spell_test_m21b_heal_ally );
        guy.set_value( "test_m21b_hurt", 0 );
        CHECK_FALSE( heal.evaluate( guy, &zombie ).value() );
        guy.set_value( "test_m21b_hurt", 1 );
        const npc_attack_rating rating = heal.evaluate( guy, &zombie );
        REQUIRE( rating.value() );
        CHECK( rating.target() == guy.pos_bub() );
    }
}

TEST_CASE( "npc-j06_follower_casts_an_ally_spell_on_the_avatar", "[audit][npc]" )
{
    on_out_of_scope cleanup( []() {
        get_avatar().remove_value( "test_m21b_hurt" );
        clear_creatures();
        clear_npcs();
    } );
    clear_map_without_vision();
    clear_avatar();
    clear_npcs();
    clear_creatures();
    set_time_to_day();
    avatar &u = get_avatar();
    npc &guy = spawn_npc( u.pos_bub().xy() + point::east * 3, "test_talker" );
    clear_character( guy );
    guy.set_fac( faction_your_followers );
    REQUIRE( guy.is_player_ally() );
    map &here = get_map();
    here.build_map_cache( 0 );
    REQUIRE( guy.sees( here, u ) );
    guy.regen_ai_cache();
    bool avatar_is_friend = false;
    for( const weak_ptr_fast<Creature> &buddy : guy.get_cached_friends() ) {
        avatar_is_friend |= buddy.lock().get() == &u;
    }
    REQUIRE( avatar_is_friend );

    guy.magic->learn_spell( spell_test_m21b_heal_ally, guy, true );
    guy.set_value( "test_m21b_hurt", 0 );
    u.set_value( "test_m21b_hurt", 1 );
    const npc_attack_rating rating = npc_attack_spell( spell_test_m21b_heal_ally ).evaluate( guy,
                                     nullptr );
    REQUIRE( rating.value() );
    CHECK( rating.target() == u.pos_bub() );
}

// NPC-J07: painkillers were 7 hardcoded item ids (heroin scored but never a reason to take one),
// took_painkiller() 6 effect ids, and no other medicine was ever taken. Now the comestible's
// "npc_use" and the effect flag PAINKILLER say so.
TEST_CASE( "npc-j07_npc_painkillers_and_medicine_come_from_json", "[audit][npc]" )
{
    on_out_of_scope cleanup( []() {
        clear_npcs();
    } );
    npc &guy = m21b_lone_npc();
    guy.wear_item( item( itype_debug_backpack ) );
    guy.set_pain( 0 );

    SECTION( "vanilla painkillers keep their pain ranges" ) {
        REQUIRE( guy.i_add( item( itype_aspirin ) ) );
        guy.set_pain( 20 );
        CHECK( guy.has_painkiller() );
        CHECK( guy.most_appropriate_painkiller()->typeId() == itype_aspirin );
        guy.set_pain( 40 );
        CHECK_FALSE( guy.has_painkiller() );
        REQUIRE( guy.i_add( item( itype_codeine ) ) );
        CHECK( guy.has_painkiller() );
        CHECK( guy.most_appropriate_painkiller()->typeId() == itype_codeine );
    }

    SECTION( "heroin is a painkiller, taken only with a syringe" ) {
        REQUIRE( guy.i_add( item( itype_heroin ) ) );
        guy.set_pain( 80 );
        CHECK_FALSE( guy.has_painkiller() );
        CHECK( guy.most_appropriate_painkiller()->is_null() );
        REQUIRE( guy.i_add( item( itype_syringe ) ) );
        CHECK( guy.has_painkiller() );
        CHECK( guy.most_appropriate_painkiller()->typeId() == itype_heroin );
    }

    SECTION( "a modded painkiller is picked by its pain" ) {
        REQUIRE( guy.i_add( item( itype_codeine ) ) );
        REQUIRE( guy.i_add( item( itype_test_m21b_painkiller ) ) );
        guy.set_pain( 70 );
        CHECK( guy.has_painkiller() );
        CHECK( guy.most_appropriate_painkiller()->typeId() == itype_test_m21b_painkiller );
    }

    SECTION( "an effect flagged PAINKILLER counts as having taken one" ) {
        CHECK_FALSE( guy.took_painkiller() );
        guy.add_effect( effect_pkill_l, 1_hours );
        CHECK( guy.took_painkiller() );
        guy.remove_effect( effect_pkill_l );
        // not on the old list, and not flagged
        guy.add_effect( effect_pkill_wintergreen, 1_hours );
        CHECK_FALSE( guy.took_painkiller() );
    }

    SECTION( "other medicine is taken while its condition holds" ) {
        REQUIRE( guy.i_add( item( itype_test_m21b_medicine ) ) );
        guy.set_value( "test_m21b_ill", 0 );
        CHECK( guy.medicine_to_take() == nullptr );
        CHECK_FALSE( guy.take_medicine() );
        guy.set_value( "test_m21b_ill", 1 );
        REQUIRE( guy.medicine_to_take() != nullptr );
        CHECK( guy.medicine_to_take()->typeId() == itype_test_m21b_medicine );
        CHECK( guy.take_medicine() );
        CHECK_FALSE( guy.has_amount( itype_test_m21b_medicine, 1 ) );
        // its consumption EOC cured it
        CHECK( guy.get_value( "test_m21b_ill" ).dbl() == 0 );
    }
}

// NPC-J08: NPC triage knew only bleed, bite and infection, and heal_actor couldn't cure any other
// effect. A heal action's "cures" removes effects, and NPCs treat them. The HP thresholds are the
// healer's ai_tuning, defaulting to what the capped code did.
TEST_CASE( "npc-j08_npc_treats_effects_its_healing_items_cure", "[audit][npc]" )
{
    on_out_of_scope cleanup( []() {
        clear_npcs();
    } );
    npc &guy = m21b_lone_npc();
    guy.wear_item( item( itype_debug_backpack ) );
    REQUIRE( heal_actor::curable_effects().count( effect_poison ) == 1 );

    SECTION( "a poisoned NPC finds and uses its antidote" ) {
        item_location antidote = guy.i_add( item( itype_test_m21b_antidote ) );
        REQUIRE( antidote );
        CHECK( guy.patient_assessment( guy ).cure.empty() );

        guy.add_effect( effect_poison, 10_minutes );
        healing_options need = guy.patient_assessment( guy );
        CHECK( need.cure.count( effect_poison ) == 1 );
        CHECK( need.any_true() );
        CHECK_FALSE( need.any_wound() );
        const healing_options can = guy.has_healing_options( need );
        CHECK( can.cure.count( effect_poison ) == 1 );
        CHECK( &guy.get_healing_item( can ) == antidote.get_item() );

        const use_function *use = antidote->type->get_use( "heal" );
        REQUIRE( use != nullptr );
        const heal_actor &actor = dynamic_cast<const heal_actor &>( *use->get_actor_ptr() );
        // poison is on the whole body: the cure goes on through the torso
        const bodypart_id part = actor.use_healing_item( guy, guy, *antidote, false );
        CHECK( part == bodypart_id( "torso" ) );
        actor.finish_using( guy, guy, *antidote, part );
        CHECK_FALSE( guy.has_effect( effect_poison ) );
        CHECK( guy.patient_assessment( guy ).cure.empty() );
    }

    SECTION( "first aid thresholds keep today's numbers" ) {
        const npc_ai_tuning &defaults = npc_ai_tuning::defaults();
        CHECK( defaults.first_aid_limb == 75 );
        CHECK( defaults.first_aid_torso == 80 );
        CHECK( defaults.first_aid_head == 80 );
        const bodypart_id head( "head" );
        const int max_hp = guy.get_part_hp_max( head );
        REQUIRE( max_hp >= 20 );
        guy.set_part_hp_cur( head, max_hp * 85 / 100 );
        CHECK_FALSE( guy.patient_assessment( guy ).bandage );
        guy.set_part_hp_cur( head, max_hp * 80 / 100 );
        CHECK( guy.patient_assessment( guy ).bandage );
    }
}

// NPC-J09: NPCs drank only "water" and "water_clean" from terrain, straight into the stomach,
// skipping the liquid's nutrients, consumption EOCs and events. Now any liquid flagged
// NPC_DRINKABLE that the NPC can drink, with its EOCs once a charge.
TEST_CASE( "npc-j09_npc_drinks_a_modded_liquid_from_terrain", "[audit][npc]" )
{
    on_out_of_scope cleanup( []() {
        clear_npcs();
    } );
    npc &guy = m21b_lone_npc();
    clear_character( guy, true );
    guy.stomach.empty();
    map &here = get_map();
    const tripoint_bub_ms spring = guy.pos_bub() + tripoint::east;
    const tripoint_bub_ms sea = guy.pos_bub() + tripoint::west;
    here.ter_set( spring, ter_t_test_m21b_drink_source );
    here.ter_set( sea, ter_t_swater_sh );
    here.build_map_cache( 0 );

    // salt water isn't NPC_DRINKABLE; the modded drink is
    const std::vector<npc::scored_water_source> sources = guy.find_nearby_water_sources();
    REQUIRE( sources.size() == 1 );
    CHECK( sources[0].pos == spring );

    guy.set_thirst( 200 );
    guy.set_value( "test_m21b_drinks", 0 );
    const units::volume sip = guy.sip_of_water();
    REQUIRE( sip > 0_ml );
    REQUIRE( guy.drink_from_water_source( spring ) );
    CHECK( guy.stomach.get_water() == sip );
    CHECK( guy.stomach.get_calories() > 0 );
    CHECK( guy.get_value( "test_m21b_drinks" ).dbl() == 1 );

    // the other sips of that charge don't run its EOC again; the next charge does
    const int charge_ml = units::to_milliliter( item( itype_test_m21b_drink ).volume() );
    const int sip_ml = units::to_milliliter( sip );
    const int sips_per_charge = ( charge_ml + sip_ml - 1 ) / sip_ml;
    for( int i = 1; i < sips_per_charge; ++i ) {
        REQUIRE( guy.drink_from_water_source( spring ) );
    }
    CHECK( guy.get_value( "test_m21b_drinks" ).dbl() == 1 );
    REQUIRE( guy.drink_from_water_source( spring ) );
    CHECK( guy.get_value( "test_m21b_drinks" ).dbl() == 2 );

    SECTION( "vanilla water is still drunk, and only water" ) {
        here.ter_set( spring, ter_t_water_sh );
        here.build_map_cache( 0 );
        guy.stomach.empty();
        REQUIRE( guy.drink_from_water_source( spring ) );
        CHECK( guy.stomach.get_water() == sip );
        CHECK( guy.stomach.get_calories() == 0 );
        CHECK_FALSE( guy.drink_from_water_source( sea ) );
    }
}
