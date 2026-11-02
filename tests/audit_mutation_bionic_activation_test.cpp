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
static const bionic_id bio_power_storage( "bio_power_storage" );
static const bionic_id test_bio_mutj02_blade( "test_bio_mutj02_blade" );
static const bionic_id test_bio_mutj02_gated( "test_bio_mutj02_gated" );
static const bionic_id test_bio_mutj03_passive( "test_bio_mutj03_passive" );
static const bionic_id test_bio_mutj03_toggle( "test_bio_mutj03_toggle" );

static const efftype_id effect_adrenaline( "adrenaline" );

static const effect_on_condition_id
effect_on_condition_EOC_TEST_MUTJ03_ACTIVATE( "EOC_TEST_MUTJ03_ACTIVATE" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_MUTJ03_CHECK( "EOC_TEST_MUTJ03_CHECK" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_MUTJ03_DEACTIVATE( "EOC_TEST_MUTJ03_DEACTIVATE" );

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
