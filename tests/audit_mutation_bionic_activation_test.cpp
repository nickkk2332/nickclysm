#include <optional>
#include <string>

#include "avatar.h"
#include "bionics.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "flag.h"
#include "item.h"
#include "math_parser_diag_value.h"
#include "player_helpers.h"
#include "type_id.h"
#include "units.h"

static const bionic_id bio_adrenaline( "bio_adrenaline" );
static const bionic_id bio_power_storage( "bio_power_storage" );
static const bionic_id test_bio_mutj02_blade( "test_bio_mutj02_blade" );
static const bionic_id test_bio_mutj02_gated( "test_bio_mutj02_gated" );

static const efftype_id effect_adrenaline( "adrenaline" );

static const itype_id itype_bio_blade_weapon( "bio_blade_weapon" );

static const trait_id trait_TEST_MUTJ02_GATED( "TEST_MUTJ02_GATED" );
static const trait_id trait_TEST_MUTJ02_SELF_OFF( "TEST_MUTJ02_SELF_OFF" );

// A numeric character variable as math sees it (unset reads as 0).
static double char_var( const Character &you, const std::string &name )
{
    const diag_value *val = you.maybe_get_value( name );
    return val == nullptr ? 0.0 : val->dbl();
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
