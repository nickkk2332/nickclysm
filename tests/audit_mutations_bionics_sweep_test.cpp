#include <array>
#include <optional>
#include <string>

#include "avatar.h"
#include "bionics.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "coordinates.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "item.h"
#include "item_location.h"
#include "itype.h"
#include "iuse.h"
#include "magic.h"
#include "map.h"
#include "math_parser_diag_value.h"
#include "monster.h"
#include "npc.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "point.h"
#include "talker.h"
#include "type_id.h"
#include "units.h"

static const json_character_flag json_flag_TEST_MUTB06_OFF( "TEST_MUTB06_OFF" );
static const json_character_flag json_flag_TEST_MUTB06_ON( "TEST_MUTB06_ON" );

static const trait_id trait_TEST_MUTB06_TOGGLE( "TEST_MUTB06_TOGGLE" );

// MUT-B06: has_trait_flag() and count_trait_flag() read the avatar's activation state for a
// trait's active_flags and inactive_flags, whoever they were asked about.
TEST_CASE( "mut-b06_active_flags_follow_the_characters_own_trait_state",
           "[audit][mutation]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    standard_npc guy( "Toggler" );
    you.set_mutation( trait_TEST_MUTB06_TOGGLE );
    guy.set_mutation( trait_TEST_MUTB06_TOGGLE );
    REQUIRE( you.can_power_mutation( trait_TEST_MUTB06_TOGGLE ) );
    REQUIRE( guy.can_power_mutation( trait_TEST_MUTB06_TOGGLE ) );

    SECTION( "only the NPC has it on" ) {
        guy.activate_mutation( trait_TEST_MUTB06_TOGGLE );
        REQUIRE( guy.has_active_mutation( trait_TEST_MUTB06_TOGGLE ) );
        REQUIRE_FALSE( you.has_active_mutation( trait_TEST_MUTB06_TOGGLE ) );
        CHECK( guy.has_trait_flag( json_flag_TEST_MUTB06_ON ) );
        CHECK_FALSE( guy.has_trait_flag( json_flag_TEST_MUTB06_OFF ) );
        CHECK( guy.count_trait_flag( json_flag_TEST_MUTB06_ON ) == 1 );
        CHECK( guy.count_trait_flag( json_flag_TEST_MUTB06_OFF ) == 0 );
        CHECK_FALSE( you.has_trait_flag( json_flag_TEST_MUTB06_ON ) );
        CHECK( you.has_trait_flag( json_flag_TEST_MUTB06_OFF ) );
    }
    SECTION( "only the avatar has it on" ) {
        you.activate_mutation( trait_TEST_MUTB06_TOGGLE );
        REQUIRE( you.has_active_mutation( trait_TEST_MUTB06_TOGGLE ) );
        REQUIRE_FALSE( guy.has_active_mutation( trait_TEST_MUTB06_TOGGLE ) );
        CHECK_FALSE( guy.has_trait_flag( json_flag_TEST_MUTB06_ON ) );
        CHECK( guy.has_trait_flag( json_flag_TEST_MUTB06_OFF ) );
        CHECK( guy.count_trait_flag( json_flag_TEST_MUTB06_ON ) == 0 );
        CHECK( guy.count_trait_flag( json_flag_TEST_MUTB06_OFF ) == 1 );
        CHECK( you.has_trait_flag( json_flag_TEST_MUTB06_ON ) );
        CHECK_FALSE( you.has_trait_flag( json_flag_TEST_MUTB06_OFF ) );
    }
    clear_avatar();
}

static const effect_on_condition_id
effect_on_condition_EOC_TEST_MUTB07_ACTIVATE( "EOC_TEST_MUTB07_ACTIVATE" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_MUTB07_DEACTIVATE( "EOC_TEST_MUTB07_DEACTIVATE" );

static const trait_id trait_TEST_MUTB07_FORM( "TEST_MUTB07_FORM" );
static const trait_id trait_TEST_MUTB07_FORM_ALT( "TEST_MUTB07_FORM_ALT" );
static const trait_id trait_TEST_MUTB07_TOGGLE( "TEST_MUTB07_TOGGLE" );

// A numeric character variable as math sees it (unset reads as 0).
static double char_var( const Character &you, const std::string &name )
{
    const diag_value *val = you.maybe_get_value( name );
    return val == nullptr ? 0.0 : val->dbl();
}

// MUT-B07: switching a trait the character lacked on or off made a cache entry for it, so
// has_trait() reported it. Activation also paid for it and ran its EOCs; deactivation ran
// its EOCs and its transform, which granted the transform's target.
TEST_CASE( "mut-b07_switching_a_trait_you_lack_does_nothing", "[audit][mutation][eoc]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    REQUIRE_FALSE( you.has_trait( trait_TEST_MUTB07_TOGGLE ) );
    REQUIRE_FALSE( you.has_trait( trait_TEST_MUTB07_FORM ) );
    REQUIRE_FALSE( you.has_trait( trait_TEST_MUTB07_FORM_ALT ) );

    SECTION( "from code" ) {
        you.activate_mutation( trait_TEST_MUTB07_TOGGLE );
        you.deactivate_mutation( trait_TEST_MUTB07_TOGGLE );
        you.deactivate_mutation( trait_TEST_MUTB07_FORM );
    }
    SECTION( "from EOCs" ) {
        dialogue d( get_talker_for( you ), nullptr );
        effect_on_condition_EOC_TEST_MUTB07_ACTIVATE->activate( d );
        effect_on_condition_EOC_TEST_MUTB07_DEACTIVATE->activate( d );
    }
    CHECK_FALSE( you.has_trait( trait_TEST_MUTB07_TOGGLE ) );
    CHECK_FALSE( you.has_active_mutation( trait_TEST_MUTB07_TOGGLE ) );
    CHECK_FALSE( you.has_trait( trait_TEST_MUTB07_FORM ) );
    CHECK_FALSE( you.has_trait( trait_TEST_MUTB07_FORM_ALT ) );
    CHECK( char_var( you, "mutb07_on" ) == 0 );
    CHECK( char_var( you, "mutb07_off" ) == 0 );
    clear_avatar();
}

TEST_CASE( "mut-b07_switching_a_trait_you_have_still_works", "[audit][mutation][eoc]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    you.set_mutation( trait_TEST_MUTB07_TOGGLE );
    you.set_mutation( trait_TEST_MUTB07_FORM );
    dialogue d( get_talker_for( you ), nullptr );

    effect_on_condition_EOC_TEST_MUTB07_ACTIVATE->activate( d );
    CHECK( you.has_active_mutation( trait_TEST_MUTB07_TOGGLE ) );
    CHECK( char_var( you, "mutb07_on" ) == 1 );

    effect_on_condition_EOC_TEST_MUTB07_DEACTIVATE->activate( d );
    CHECK( you.has_trait( trait_TEST_MUTB07_TOGGLE ) );
    CHECK_FALSE( you.has_active_mutation( trait_TEST_MUTB07_TOGGLE ) );
    CHECK( char_var( you, "mutb07_off" ) == 1 );
    CHECK_FALSE( you.has_trait( trait_TEST_MUTB07_FORM ) );
    CHECK( you.has_trait( trait_TEST_MUTB07_FORM_ALT ) );
    clear_avatar();
}

static const bionic_id bio_fuel_cell_gasoline( "bio_fuel_cell_gasoline" );
static const bionic_id bio_power_storage( "bio_power_storage" );
static const bionic_id test_bio_mutb09_mixed_burner( "test_bio_mutb09_mixed_burner" );
static const bionic_id test_bio_mutb09_spare_tank( "test_bio_mutb09_spare_tank" );

static const itype_id itype_gasoline( "gasoline" );
static const itype_id itype_home_picture( "home_picture" );
static const itype_id itype_internal_gasoline_tank( "internal_gasoline_tank" );
static const itype_id itype_test_mutb09_gasoline_tank( "test_mutb09_gasoline_tank" );
static const itype_id itype_test_mutb09_hopper( "test_mutb09_hopper" );

static item_location worn_item_of_type( Character &you, const itype_id &type )
{
    for( const item_location &loc : you.top_items_loc() ) {
        if( loc->typeId() == type ) {
            return loc;
        }
    }
    return item_location();
}

static void give_power_storage( Character &you )
{
    for( int i = 0; i < 5; ++i ) {
        you.add_bionic( bio_power_storage );
    }
    REQUIRE( !you.has_power() );
    REQUIRE( you.has_max_power() );
}

// MUT-B09: burn_fuel() took a unit of fuel from every source a bionic could reach each turn
// but credited only the last one.
TEST_CASE( "mut-b09_fuel_bionic_burns_from_one_source_per_turn", "[audit][bionics]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    give_power_storage( you );
    const bionic_uid cell_uid = you.add_bionic( bio_fuel_cell_gasoline );
    you.add_bionic( test_bio_mutb09_spare_tank );
    item_location own_tank = worn_item_of_type( you, itype_internal_gasoline_tank );
    item_location spare_tank = worn_item_of_type( you, itype_test_mutb09_gasoline_tank );
    REQUIRE( own_tank );
    REQUIRE( spare_tank );
    const std::array<item_location *, 2> tanks = { &own_tank, &spare_tank };
    for( item_location *tank : tanks ) {
        item gasoline( itype_gasoline );
        gasoline.charges = 2;
        ( *tank )->put_in( gasoline, pocket_type::CONTAINER );
        REQUIRE( ( *tank )->only_item().charges == 2 );
    }
    bionic &cell = **you.find_bionic_by_uid( cell_uid );
    REQUIRE( you.get_bionic_fuels( cell.id ).size() == 2 );
    REQUIRE( you.activate_bionic( cell ) );

    you.suffer();
    // One unit of gasoline gives this cell 8550 J.
    CHECK( units::to_joule( you.get_power_level() ) == 8550 );
    CHECK( own_tank->only_item().charges + spare_tank->only_item().charges == 3 );
    clear_avatar();
}

// MUT-B09: get_bionic_fuels() listed a source once for each of the bionic's fuels it held, so
// burn_fuel() drew from it that many times.
TEST_CASE( "mut-b09_source_holding_two_fuels_is_listed_once", "[audit][bionics]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    give_power_storage( you );
    const bionic_uid burner_uid = you.add_bionic( test_bio_mutb09_mixed_burner );
    item_location hopper = worn_item_of_type( you, itype_test_mutb09_hopper );
    REQUIRE( hopper );
    // A picture is made of paper and wood, both of them fuel for this burner.
    for( int i = 0; i < 2; ++i ) {
        hopper->put_in( item( itype_home_picture ), pocket_type::CONTAINER );
    }
    REQUIRE( hopper->all_items_top().size() == 2 );
    bionic &burner = **you.find_bionic_by_uid( burner_uid );
    burner.set_safe_fuel_thresh( -1.0f );

    CHECK( you.get_bionic_fuels( burner.id ).size() == 1 );
    REQUIRE( you.activate_bionic( burner ) );
    you.suffer();
    CHECK( hopper->all_items_top().size() == 1 );
    clear_avatar();
}

static const bionic_id test_bio_mutb08_big( "test_bio_mutb08_big" );

static const itype_id itype_hammer( "hammer" );
static const itype_id itype_test_bio_mutb08_cancels( "test_bio_mutb08_cancels" );
static const itype_id itype_test_bio_mutb08_conflict( "test_bio_mutb08_conflict" );
static const itype_id itype_test_bio_mutb08_small( "test_bio_mutb08_small" );

static const trait_id trait_NOPAIN( "NOPAIN" );
static const trait_id trait_TEST_MUTB08_MANUAL( "TEST_MUTB08_MANUAL" );
static const trait_id trait_TEST_MUTB08_TRAIT( "TEST_MUTB08_TRAIT" );

// The MUT-B08 test CBMs need a hammer to self-install, and Deadened stands in for anesthetic.
static void prepare_self_install( avatar &you )
{
    you.set_mutation( trait_NOPAIN );
    you.set_wielded_item( item( itype_hammer ) );
    you.invalidate_crafting_inventory();
}

// MUT-B08: a CBM you installed yourself skipped the bionic slot limit, the body compatibility
// checks and the question about traits the CBM removes. So did any install for a patient
// with MANUAL_CBM_INSTALLATION.
TEST_CASE( "mut-b08_self_installed_cbm_needs_free_slots", "[audit][bionics][cbm]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    prepare_self_install( you );
    const item small( itype_test_bio_mutb08_small );

    SECTION( "self-install" ) {
    }
    SECTION( "self-install without help (MANUAL_CBM_INSTALLATION)" ) {
        you.set_mutation( trait_TEST_MUTB08_MANUAL );
    }
    REQUIRE( you.can_install_bionics( *small.type, you, false ) );
    you.add_bionic( test_bio_mutb08_big );
    REQUIRE_FALSE( you.bionic_installation_issues( small.type->bionic->id ).empty() );
    CHECK_FALSE( you.can_install_bionics( *small.type, you, false ) );
    clear_avatar();
}

TEST_CASE( "mut-b08_self_installed_cbm_must_suit_the_body", "[audit][bionics][cbm]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    prepare_self_install( you );
    const item cbm( itype_test_bio_mutb08_conflict );
    const use_function *install = cbm.type->get_use( "install_bionic" );
    REQUIRE( install != nullptr );
    REQUIRE( install->can_call( you, cbm, you.pos_bub() ).success() );

    you.set_mutation( trait_TEST_MUTB08_TRAIT );
    CHECK_FALSE( install->can_call( you, cbm, you.pos_bub() ).success() );
    clear_avatar();
}

// Tests answer every query_yn with no, so being asked shows up as a refused install.
TEST_CASE( "mut-b08_self_install_asks_before_removing_traits", "[audit][bionics][cbm]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    prepare_self_install( you );
    const item cbm( itype_test_bio_mutb08_cancels );
    REQUIRE( you.can_install_bionics( *cbm.type, you, false ) );

    you.set_mutation( trait_TEST_MUTB08_TRAIT );
    CHECK_FALSE( you.can_install_bionics( *cbm.type, you, false ) );
    clear_avatar();
}
