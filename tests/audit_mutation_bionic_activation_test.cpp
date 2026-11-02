#include <optional>
#include <string>

#include "avatar.h"
#include "bionics.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "flag.h"
#include "item.h"
#include "math_parser_diag_value.h"
#include "npc.h"
#include "player_helpers.h"
#include "talker.h"
#include "type_id.h"
#include "units.h"

static const bionic_id bio_adrenaline( "bio_adrenaline" );
static const bionic_id bio_ads( "bio_ads" );
static const bionic_id bio_laser( "bio_laser" );
static const bionic_id bio_painkiller( "bio_painkiller" );
static const bionic_id bio_power_storage( "bio_power_storage" );
static const bionic_id test_bio_mutj02_blade( "test_bio_mutj02_blade" );
static const bionic_id test_bio_mutj02_gated( "test_bio_mutj02_gated" );
static const bionic_id test_bio_mutj03_off_self_remove( "test_bio_mutj03_off_self_remove" );
static const bionic_id test_bio_mutj03_passive( "test_bio_mutj03_passive" );
static const bionic_id
test_bio_mutj03_passive_self_remove( "test_bio_mutj03_passive_self_remove" );
static const bionic_id test_bio_mutj03_toggle( "test_bio_mutj03_toggle" );
static const bionic_id
test_bio_mutj03_toggle_self_remove( "test_bio_mutj03_toggle_self_remove" );
static const bionic_id test_bio_npcj01_blade( "test_bio_npcj01_blade" );
static const bionic_id test_bio_npcj01_shield( "test_bio_npcj01_shield" );

static const efftype_id effect_adrenaline( "adrenaline" );

static const effect_on_condition_id
effect_on_condition_EOC_TEST_MUTJ03_ACTIVATE( "EOC_TEST_MUTJ03_ACTIVATE" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_MUTJ03_CHECK( "EOC_TEST_MUTJ03_CHECK" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_MUTJ03_DEACTIVATE( "EOC_TEST_MUTJ03_DEACTIVATE" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_MUTJ03_NPC_GUN( "EOC_TEST_MUTJ03_NPC_GUN" );

static const itype_id itype_bio_blade_weapon( "bio_blade_weapon" );

static const trait_id trait_TEST_MUTJ02_GATED( "TEST_MUTJ02_GATED" );
static const trait_id trait_TEST_MUTJ02_SELF_OFF( "TEST_MUTJ02_SELF_OFF" );

// A numeric character variable as math sees it (unset reads as 0).
static double char_var( const Character &you, const std::string &name )
{
    const diag_value *val = you.maybe_get_value( name );
    return val == nullptr ? 0.0 : val->dbl();
}

// A string character variable (unset reads as "").
static std::string char_str( const Character &you, const std::string &name )
{
    const diag_value *val = you.maybe_get_value( name );
    return val == nullptr ? std::string() : val->str();
}

static bionic &give_powered_bionic( Character &you, const bionic_id &bio_id )
{
    you.add_bionic( bio_power_storage );
    you.set_power_level( you.get_max_power_level() );
    REQUIRE( you.get_power_level() >= 10_kJ );
    you.add_bionic( bio_id );
    const std::optional<bionic *> bio = you.find_bionic_by_type( bio_id );
    REQUIRE( bio );
    return **bio;
}

// MUT-J02: activate_cached_mutation() forced `powered = activated_is_setup` after the
// activation EOCs, so an EOC that turned its own mutation off was overridden.
TEST_CASE( "mut-j02_activation_eoc_can_turn_its_own_trait_off", "[audit][mutations]" )
{
    clear_avatar();
    avatar &me = get_avatar();
    me.set_mutation( trait_TEST_MUTJ02_SELF_OFF );
    REQUIRE( me.has_trait( trait_TEST_MUTJ02_SELF_OFF ) );

    // While the EOC lets it, the setup mutation stays on, as before.
    me.activate_mutation( trait_TEST_MUTJ02_SELF_OFF );
    REQUIRE( me.has_active_mutation( trait_TEST_MUTJ02_SELF_OFF ) );
    me.deactivate_mutation( trait_TEST_MUTJ02_SELF_OFF );
    REQUIRE_FALSE( me.has_active_mutation( trait_TEST_MUTJ02_SELF_OFF ) );
    REQUIRE( char_var( me, "mutj02_deactivated" ) == 1 );

    // Now the activation EOC runs u_deactivate_trait on itself: it must stay off.
    me.set_value( "mutj02_refuse", 1 );
    me.activate_mutation( trait_TEST_MUTJ02_SELF_OFF );
    CHECK_FALSE( me.has_active_mutation( trait_TEST_MUTJ02_SELF_OFF ) );
    // The deactivation went through deactivate_mutation, so its EOCs ran.
    CHECK( char_var( me, "mutj02_deactivated" ) == 2 );
    CHECK( me.has_trait( trait_TEST_MUTJ02_SELF_OFF ) );
    clear_avatar();
}

// MUT-J02: "activation_condition" vetoes a mutation activation before any cost is paid.
TEST_CASE( "mut-j02_trait_activation_condition_vetoes_before_any_cost", "[audit][mutations]" )
{
    clear_avatar();
    avatar &me = get_avatar();
    me.set_mutation( trait_TEST_MUTJ02_GATED );
    REQUIRE( me.can_power_mutation( trait_TEST_MUTJ02_GATED ) );
    const int kcal_before = me.get_stored_kcal();

    // The gate variable is unset: nothing happens, nothing is paid.
    me.activate_mutation( trait_TEST_MUTJ02_GATED );
    CHECK_FALSE( me.has_active_mutation( trait_TEST_MUTJ02_GATED ) );
    CHECK( me.get_stored_kcal() == kcal_before );
    CHECK( char_var( me, "mutj02_gated_ran" ) == 0 );

    // The condition also sees `this`; with the gate open the activation goes through.
    me.set_value( "mutj02_gate", 1 );
    me.activate_mutation( trait_TEST_MUTJ02_GATED );
    CHECK( me.has_active_mutation( trait_TEST_MUTJ02_GATED ) );
    CHECK( me.get_stored_kcal() == kcal_before - 100 );
    CHECK( char_var( me, "mutj02_gated_ran" ) == 1 );
    clear_avatar();
}

// MUT-J02: "activation_condition" vetoes a bionic activation before any power is spent.
TEST_CASE( "mut-j02_bionic_activation_condition_vetoes_before_any_cost", "[audit][bionics]" )
{
    clear_avatar();
    avatar &me = get_avatar();
    bionic &bio = give_powered_bionic( me, test_bio_mutj02_gated );
    const units::energy power_before = me.get_power_level();

    CHECK_FALSE( me.activate_bionic( bio ) );
    CHECK_FALSE( me.has_active_bionic( test_bio_mutj02_gated ) );
    CHECK( me.get_power_level() == power_before );
    CHECK( char_var( me, "mutj02_bio_ran" ) == 0 );

    me.set_value( "mutj02_gate", 1 );
    CHECK( me.activate_bionic( bio ) );
    CHECK( me.has_active_bionic( test_bio_mutj02_gated ) );
    CHECK( me.get_power_level() == power_before - 1_kJ );
    CHECK( char_var( me, "mutj02_bio_ran" ) == 1 );
    clear_avatar();
}

// MUT-J02: bionic activated_eocs ran before the hardcoded checks that can still abort the
// activation, so a failed activation had the EOC's side effects anyway.
TEST_CASE( "mut-j02_failed_bionic_activation_runs_no_activation_eoc", "[audit][bionics]" )
{
    clear_avatar();
    avatar &me = get_avatar();
    bionic &bio = give_powered_bionic( me, test_bio_mutj02_blade );

    // Something NO_UNWIELD that no bionic owns is stuck in the character's hands, so the
    // weapon bionic refuses to extend ("Deactivate your ... first!").
    item stuck( itype_bio_blade_weapon );
    stuck.set_flag( flag_NO_UNWIELD );
    me.set_wielded_item( stuck );
    REQUIRE( me.get_weapon_bionic_uid() == 0 );
    const units::energy power_before = me.get_power_level();

    CHECK_FALSE( me.activate_bionic( bio ) );
    CHECK_FALSE( bio.powered );
    CHECK( me.get_power_level() == power_before );
    CHECK( char_var( me, "mutj02_blade_ran" ) == 0 );

    // With free hands it extends, and its activation EOC runs once.
    me.set_wielded_item( item() );
    CHECK( me.activate_bionic( bio ) );
    CHECK( me.is_using_bionic_weapon() );
    CHECK( char_var( me, "mutj02_blade_ran" ) == 1 );
    clear_avatar();
}

// MUT-J02: bio_adrenaline used an activation EOC to refund its power and "refuse" a second
// dose, but the activation itself still reported success.  It now uses activation_condition.
TEST_CASE( "mut-j02_bio_adrenaline_refuses_a_second_dose_for_free", "[audit][bionics]" )
{
    clear_avatar();
    avatar &me = get_avatar();
    bionic &bio = give_powered_bionic( me, bio_adrenaline );

    me.add_effect( effect_adrenaline, 10_minutes );
    const units::energy power_before = me.get_power_level();
    CHECK_FALSE( me.activate_bionic( bio ) );
    CHECK( me.get_power_level() == power_before );

    me.remove_effect( effect_adrenaline );
    CHECK( me.activate_bionic( bio ) );
    CHECK( me.has_effect( effect_adrenaline ) );
    CHECK( me.get_power_level() < power_before );
    clear_avatar();
}

// MUT-J03: EOCs could add and remove bionics but not read or switch their powered state.
TEST_CASE( "mut-j03_eocs_read_and_switch_bionic_state", "[audit][bionics][eoc]" )
{
    clear_avatar();
    avatar &me = get_avatar();
    give_powered_bionic( me, test_bio_mutj03_toggle );
    const units::energy power_before = me.get_power_level();
    dialogue d( get_talker_for( me ), nullptr );

    effect_on_condition_EOC_TEST_MUTJ03_CHECK->activate( d );
    CHECK( char_var( me, "mutj03_seen_active" ) == 0 );
    CHECK( char_var( me, "mutj03_seen_any" ) == 0 );

    // u_activate_bionic goes through activate_bionic(): the power is paid and the
    // activation EOCs run, with `this` set to the bionic id.
    effect_on_condition_EOC_TEST_MUTJ03_ACTIVATE->activate( d );
    CHECK( me.has_active_bionic( test_bio_mutj03_toggle ) );
    CHECK( me.get_power_level() == power_before - 1_kJ );
    CHECK( char_var( me, "mutj03_on_true" ) == 1 );
    CHECK( char_var( me, "mutj03_on_false" ) == 0 );
    CHECK( char_str( me, "mutj03_activated_this" ) == test_bio_mutj03_toggle.str() );

    effect_on_condition_EOC_TEST_MUTJ03_CHECK->activate( d );
    CHECK( char_var( me, "mutj03_seen_active" ) == 1 );
    CHECK( char_var( me, "mutj03_seen_any" ) == 1 );

    // Nothing left to switch on: false_eocs, and nothing paid.
    effect_on_condition_EOC_TEST_MUTJ03_ACTIVATE->activate( d );
    CHECK( char_var( me, "mutj03_on_true" ) == 1 );
    CHECK( char_var( me, "mutj03_on_false" ) == 1 );
    CHECK( me.get_power_level() == power_before - 1_kJ );

    // processed_eocs get `this` too.
    const std::optional<bionic *> bio = me.find_bionic_by_type( test_bio_mutj03_toggle );
    REQUIRE( bio );
    me.process_bionic( **bio );
    CHECK( char_str( me, "mutj03_processed_this" ) == test_bio_mutj03_toggle.str() );

    // u_deactivate_bionic, here with the id in a variable; deactivated_eocs get `this`.
    me.set_value( "mutj03_target", std::string( test_bio_mutj03_toggle.str() ) );
    effect_on_condition_EOC_TEST_MUTJ03_DEACTIVATE->activate( d );
    CHECK_FALSE( me.has_active_bionic( test_bio_mutj03_toggle ) );
    CHECK( char_var( me, "mutj03_off_true" ) == 1 );
    CHECK( char_var( me, "mutj03_off_false" ) == 0 );
    CHECK( char_str( me, "mutj03_deactivated_this" ) == test_bio_mutj03_toggle.str() );

    effect_on_condition_EOC_TEST_MUTJ03_CHECK->activate( d );
    CHECK( char_var( me, "mutj03_seen_active" ) == 0 );
    CHECK( char_var( me, "mutj03_seen_any" ) == 0 );

    effect_on_condition_EOC_TEST_MUTJ03_DEACTIVATE->activate( d );
    CHECK( char_var( me, "mutj03_off_false" ) == 1 );
    clear_avatar();
}

// MUT-J03: process_bionic() returned early for unpowered bionics, so a passive CBM's
// processed_eocs never ran (passive mutations do run theirs).
TEST_CASE( "mut-j03_passive_bionic_runs_processed_eocs", "[audit][bionics]" )
{
    clear_avatar();
    avatar &me = get_avatar();
    me.add_bionic( test_bio_mutj03_passive );
    const std::optional<bionic *> bio = me.find_bionic_by_type( test_bio_mutj03_passive );
    REQUIRE( bio );
    REQUIRE_FALSE( ( *bio )->info().activated );
    REQUIRE_FALSE( ( *bio )->powered );

    me.process_bionic( **bio );
    CHECK( char_var( me, "mutj03_passive_ticks" ) == 1 );
    CHECK( char_str( me, "mutj03_passive_this" ) == test_bio_mutj03_passive.str() );
    me.process_bionic( **bio );
    CHECK( char_var( me, "mutj03_passive_ticks" ) == 2 );
    clear_avatar();
}

// MUT-J03: an NPC extending a weapon CBM set bio.powered directly, skipping the activation
// EOCs (and the enchantment recalculation) that the player's activation runs.
TEST_CASE( "mut-j03_npc_weapon_cbm_runs_activation_eocs", "[audit][bionics][npc]" )
{
    standard_npc guy( "Blade" );
    bionic &bio = give_powered_bionic( guy, test_bio_mutj02_blade );
    REQUIRE_FALSE( guy.get_wielded_item() );

    guy.check_or_use_weapon_cbm( test_bio_mutj02_blade );
    CHECK( guy.is_using_bionic_weapon() );
    CHECK( bio.powered );
    CHECK( char_var( guy, "mutj02_blade_ran" ) == 1 );
}

// MUT-J03 review: activate_bionic() fired a gun CBM through the avatar whoever activated it,
// so npc_activate_bionic on an NPC's gun CBM put the player into aim mode with the NPC's gun.
// Interactive CBMs now refuse non-avatars, and the effect runs its false_eocs.
TEST_CASE( "mut-j03_npc_activate_bionic_refuses_an_npc_gun_cbm", "[audit][bionics][eoc][npc]" )
{
    clear_avatar();
    avatar &me = get_avatar();
    standard_npc guy( "Gunner" );
    bionic &bio = give_powered_bionic( guy, bio_laser );
    REQUIRE( bio.has_weapon() );
    // Enough power to fire it, so nothing but the avatar check stops the activation.
    REQUIRE( guy.get_power_level() >= bio.get_weapon().get_gun_bionic_drain() );
    REQUIRE_FALSE( me.activity );
    const units::energy power_before = guy.get_power_level();

    dialogue d( get_talker_for( me ), get_talker_for( guy ) );
    effect_on_condition_EOC_TEST_MUTJ03_NPC_GUN->activate( d );
    CHECK_FALSE( me.activity );
    CHECK( char_var( me, "mutj03_gun_true" ) == 0 );
    CHECK( char_var( me, "mutj03_gun_false" ) == 1 );
    CHECK( guy.get_power_level() == power_before );
    clear_avatar();
}

// MUT-J03 review: Character::suffer() walked my_bionics with a range-for while bionic EOCs
// ran, so a passive bionic that uninstalled itself shifted the next bionic into the slot
// already processed, and that one was skipped (and the loop read past the new end).
TEST_CASE( "mut-j03_bionic_removing_itself_does_not_skip_the_next_one", "[audit][bionics]" )
{
    clear_avatar();
    avatar &me = get_avatar();
    me.add_bionic( test_bio_mutj03_passive_self_remove );
    me.add_bionic( test_bio_mutj03_passive );
    me.add_bionic( bio_power_storage );
    REQUIRE( me.my_bionics->size() == 3 );
    REQUIRE( ( *me.my_bionics )[0].id == test_bio_mutj03_passive_self_remove );
    REQUIRE( ( *me.my_bionics )[1].id == test_bio_mutj03_passive );

    me.suffer();
    CHECK_FALSE( me.has_bionic( test_bio_mutj03_passive_self_remove ) );
    CHECK( me.has_bionic( test_bio_mutj03_passive ) );
    CHECK( char_var( me, "mutj03_passive_ticks" ) == 1 );
    clear_avatar();
}

// MUT-J03 review: process_bionic() kept using `bio` after its processed_eocs, which may have
// uninstalled it; the next bionic slid into its slot and got the per-turn effects instead.
TEST_CASE( "mut-j03_processed_eoc_removing_its_bionic_ends_its_turn", "[audit][bionics]" )
{
    clear_avatar();
    avatar &me = get_avatar();
    me.add_bionic( test_bio_mutj03_toggle_self_remove );
    me.add_bionic( bio_painkiller );
    REQUIRE( ( *me.my_bionics )[0].id == test_bio_mutj03_toggle_self_remove );
    REQUIRE( ( *me.my_bionics )[1].id == bio_painkiller );
    bionic &bio = ( *me.my_bionics )[0];
    bio.powered = true;
    me.set_pain( 20 );
    REQUIRE( me.get_pain() == 20 );
    REQUIRE( me.get_painkiller() == 0 );

    me.process_bionic( bio );
    CHECK_FALSE( me.has_bionic( test_bio_mutj03_toggle_self_remove ) );
    // The painkiller CBM is off, so its per-turn effect must not have run.
    CHECK_FALSE( me.has_active_bionic( bio_painkiller ) );
    CHECK( me.get_painkiller() == 0 );
    clear_avatar();
}

// MUT-J03 review: deactivate_bionic() read `this` from `bio` for each deactivated EOC, so an
// EOC after one that uninstalled the bionic got the id of the bionic that slid into its slot.
TEST_CASE( "mut-j03_deactivation_eocs_keep_this_after_removal", "[audit][bionics][eoc]" )
{
    clear_avatar();
    avatar &me = get_avatar();
    me.add_bionic( test_bio_mutj03_off_self_remove );
    me.add_bionic( bio_power_storage );
    REQUIRE( ( *me.my_bionics )[0].id == test_bio_mutj03_off_self_remove );
    bionic &bio = ( *me.my_bionics )[0];
    bio.powered = true;

    CHECK( me.deactivate_bionic( bio ) );
    CHECK_FALSE( me.has_bionic( test_bio_mutj03_off_self_remove ) );
    CHECK( char_str( me, "mutj03_removed_off_this" ) == test_bio_mutj03_off_self_remove.str() );
    clear_avatar();
}

// NPC-J01: NPCs only used weapon CBMs from a fixed list of four ids, so a mod's
// BIONIC_NPC_USABLE weapon CBM was never extended.
TEST_CASE( "npc-j01_npc_extends_any_npc_usable_weapon_cbm", "[audit][bionics][npc]" )
{
    standard_npc guy( "Blade" );
    bionic &bio = give_powered_bionic( guy, test_bio_npcj01_blade );
    REQUIRE_FALSE( guy.get_wielded_item() );
    REQUIRE( guy.can_use_offensive_cbm() );

    guy.activate_combat_cbms();
    CHECK( guy.is_using_bionic_weapon() );
    CHECK( bio.powered );

    guy.deactivate_combat_cbms();
    CHECK_FALSE( guy.is_using_bionic_weapon() );
    CHECK_FALSE( bio.powered );
}

// NPC-J01: the CBMs NPCs switch on for a fight came from a hardcoded list; they now come
// from the bionic's "npc_use".  bio_ads checks that the vanilla data was migrated.
TEST_CASE( "npc-j01_npc_switches_cbms_by_their_npc_use", "[audit][bionics][npc]" )
{
    standard_npc guy( "Shield" );
    give_powered_bionic( guy, test_bio_npcj01_shield );
    guy.add_bionic( bio_ads );
    REQUIRE( test_bio_npcj01_shield->npc_use == bionic_npc_use::combat );
    REQUIRE( bio_ads->npc_use == bionic_npc_use::combat );

    guy.activate_combat_cbms();
    CHECK( guy.has_active_bionic( test_bio_npcj01_shield ) );
    CHECK( guy.has_active_bionic( bio_ads ) );

    guy.deactivate_combat_cbms();
    CHECK_FALSE( guy.has_active_bionic( test_bio_npcj01_shield ) );
    CHECK_FALSE( guy.has_active_bionic( bio_ads ) );
}
