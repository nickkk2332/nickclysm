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
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
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

// clear_bionics() zeroed the power capacity it was given but left the power level, so power
// given to a character without bionics outlived clear_avatar() and leaked into later tests.
TEST_CASE( "mut-b09_clear_bionics_cuts_power_to_what_is_left", "[audit][bionics]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    you.set_max_power_level_modifier( 100_kJ );
    you.set_power_level( 50_kJ );
    REQUIRE( you.get_power_level() == 50_kJ );

    you.clear_bionics();
    CHECK( you.get_max_power_level() == 0_kJ );
    CHECK_FALSE( you.has_power() );
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

// MANUAL_CBM_INSTALLATION on the player made every CBM operation succeed, whoever did it:
// nurse bots and NPC surgeons included.
TEST_CASE( "mut-b08_manual_installation_only_helps_whoever_has_it",
           "[audit][bionics][cbm]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    standard_npc surgeon( "Surgeon" );
    you.set_mutation( trait_TEST_MUTB08_MANUAL );
    const int difficulty = 10;

    CHECK( bionic_success_chance( false, -1, difficulty, you ) == 100 );
    CHECK( bionic_success_chance( false, -1, difficulty, surgeon ) < 100 );
    // What a nurse bot uses, with the skill it has.
    CHECK( bionic_manip_cos( 37.0f, difficulty ) < 100 );

    surgeon.set_mutation( trait_TEST_MUTB08_MANUAL );
    CHECK( bionic_success_chance( false, -1, difficulty, surgeon ) == 100 );
    clear_avatar();
}

static const bionic_id test_bio_mutb10_base( "test_bio_mutb10_base" );
static const bionic_id test_bio_mutb10_child( "test_bio_mutb10_child" );
static const bionic_id test_bio_mutb10_parent( "test_bio_mutb10_parent" );
static const bionic_id test_bio_mutb10_teacher( "test_bio_mutb10_teacher" );
static const bionic_id test_bio_mutb10_teacher_b( "test_bio_mutb10_teacher_b" );
static const bionic_id test_bio_mutb10_upgrade( "test_bio_mutb10_upgrade" );

static const npc_class_id npc_class_test_mutb10_class( "test_mutb10_class" );

static const profession_id profession_test_mutb10_profession( "test_mutb10_profession" );

static const proficiency_id proficiency_prof_lockpicking( "prof_lockpicking" );

static const spell_id spell_test_spell_pew( "test_spell_pew" );

static void remove_bionic_of_type( Character &you, const bionic_id &bid )
{
    const std::optional<bionic *> bio = you.find_bionic_by_type( bid );
    REQUIRE( bio );
    you.remove_bionic( **bio );
    REQUIRE_FALSE( you.has_bionic( bid ) );
}

// MUT-B10: removing a CBM took away every proficiency it lists and every spell it lists
// that no other CBM lists, including ones the character had before installing it.
TEST_CASE( "mut-b10_removing_a_cbm_keeps_what_it_did_not_teach",
           "[audit][bionics][cbm][proficiency][magic]" )
{
    clear_avatar();
    avatar &you = get_avatar();

    SECTION( "known before the CBM" ) {
        you.add_proficiency( proficiency_prof_lockpicking );
        you.magic->learn_spell( spell_test_spell_pew, you, true );
        you.magic->get_spell( spell_test_spell_pew ).set_level( you, 5 );
        you.add_bionic( test_bio_mutb10_teacher );
        remove_bionic_of_type( you, test_bio_mutb10_teacher );
        CHECK( you.has_proficiency( proficiency_prof_lockpicking ) );
        REQUIRE( you.magic->knows_spell( spell_test_spell_pew ) );
        CHECK( you.magic->get_spell( spell_test_spell_pew ).get_level() == 5 );
    }
    SECTION( "taught by the first of two CBMs, which is removed first" ) {
        you.add_bionic( test_bio_mutb10_teacher );
        you.add_bionic( test_bio_mutb10_teacher_b );
        remove_bionic_of_type( you, test_bio_mutb10_teacher );
        CHECK( you.has_proficiency( proficiency_prof_lockpicking ) );
        CHECK( you.magic->knows_spell( spell_test_spell_pew ) );
        remove_bionic_of_type( you, test_bio_mutb10_teacher_b );
        CHECK_FALSE( you.has_proficiency( proficiency_prof_lockpicking ) );
        CHECK_FALSE( you.magic->knows_spell( spell_test_spell_pew ) );
    }
    SECTION( "taught by the first of two CBMs, which is removed last" ) {
        you.add_bionic( test_bio_mutb10_teacher );
        you.add_bionic( test_bio_mutb10_teacher_b );
        remove_bionic_of_type( you, test_bio_mutb10_teacher_b );
        CHECK( you.has_proficiency( proficiency_prof_lockpicking ) );
        CHECK( you.magic->knows_spell( spell_test_spell_pew ) );
        remove_bionic_of_type( you, test_bio_mutb10_teacher );
        CHECK_FALSE( you.has_proficiency( proficiency_prof_lockpicking ) );
        CHECK_FALSE( you.magic->knows_spell( spell_test_spell_pew ) );
    }
    SECTION( "installed before CBMs recorded what they taught" ) {
        // Such a CBM is assumed to have taught everything it lists, as before.
        you.add_proficiency( proficiency_prof_lockpicking );
        const bionic_uid uid = you.add_bionic( test_bio_mutb10_teacher );
        ( *you.find_bionic_by_uid( uid ) )->remove_flag( "cbm_grants_recorded" );
        remove_bionic_of_type( you, test_bio_mutb10_teacher );
        CHECK_FALSE( you.has_proficiency( proficiency_prof_lockpicking ) );
        CHECK_FALSE( you.magic->knows_spell( spell_test_spell_pew ) );
    }
    clear_avatar();
}

// MUT-B10: a proficiency a CBM completed lost the character's own practice with it.
TEST_CASE( "mut-b10_removing_a_cbm_gives_back_proficiency_practice",
           "[audit][bionics][cbm][proficiency]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    you.set_proficiency_practiced_time( proficiency_prof_lockpicking, to_turns<int>( 1_hours ) );
    REQUIRE_FALSE( you.has_proficiency( proficiency_prof_lockpicking ) );
    REQUIRE( you.get_proficiency_practiced_time( proficiency_prof_lockpicking ) == 1_hours );

    you.add_bionic( test_bio_mutb10_teacher );
    REQUIRE( you.has_proficiency( proficiency_prof_lockpicking ) );
    remove_bionic_of_type( you, test_bio_mutb10_teacher );
    CHECK_FALSE( you.has_proficiency( proficiency_prof_lockpicking ) );
    CHECK( you.get_proficiency_practiced_time( proficiency_prof_lockpicking ) == 1_hours );
    clear_avatar();
}

// MUT-B10: the proficiencies and spells of a CBM's included bionics stayed after removing it.
TEST_CASE( "mut-b10_removing_a_cbm_takes_back_what_its_included_bionics_taught",
           "[audit][bionics][cbm][proficiency][magic]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    you.add_bionic( test_bio_mutb10_parent );
    REQUIRE( you.has_bionic( test_bio_mutb10_child ) );
    REQUIRE( you.has_proficiency( proficiency_prof_lockpicking ) );
    REQUIRE( you.magic->knows_spell( spell_test_spell_pew ) );

    remove_bionic_of_type( you, test_bio_mutb10_parent );
    REQUIRE_FALSE( you.has_bionic( test_bio_mutb10_child ) );
    CHECK_FALSE( you.has_proficiency( proficiency_prof_lockpicking ) );
    CHECK_FALSE( you.magic->knows_spell( spell_test_spell_pew ) );
    clear_avatar();
}

// Checks that removing the starting CBM leaves what the character's background taught.
static void check_background_outlives_starting_cbm( Character &who )
{
    REQUIRE( who.has_bionic( test_bio_mutb10_teacher ) );
    REQUIRE( who.has_proficiency( proficiency_prof_lockpicking ) );
    REQUIRE( who.magic->knows_spell( spell_test_spell_pew ) );
    REQUIRE( who.magic->get_spell( spell_test_spell_pew ).get_level() == 3 );

    remove_bionic_of_type( who, test_bio_mutb10_teacher );
    CHECK( who.has_proficiency( proficiency_prof_lockpicking ) );
    REQUIRE( who.magic->knows_spell( spell_test_spell_pew ) );
    CHECK( who.magic->get_spell( spell_test_spell_pew ).get_level() == 3 );
}

// MUT-B10: a new character's CBMs went in before the proficiencies and spells of their
// profession, hobbies or NPC class, so the CBMs counted those as their own.
TEST_CASE( "mut-b10_starting_cbms_do_not_claim_what_the_background_teaches",
           "[audit][bionics][cbm][proficiency][magic]" )
{
    SECTION( "a new character's profession" ) {
        standard_npc recruit( "Recruit" );
        recruit.prof = &profession_test_mutb10_profession.obj();
        recruit.initialize( false );
        check_background_outlives_starting_cbm( recruit );
    }
    SECTION( "a new NPC's class" ) {
        npc recruit;
        recruit.randomize( npc_class_test_mutb10_class );
        check_background_outlives_starting_cbm( recruit );
    }
}

// MUT-B10: an upgrade removed the CBM it replaces before installing itself, so a spell both
// teach was forgotten and taught again, losing its experience.
TEST_CASE( "mut-b10_upgrade_takes_over_what_the_base_cbm_taught",
           "[audit][bionics][cbm][proficiency][magic]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    const bionic_uid base_uid = you.add_bionic( test_bio_mutb10_base );
    SECTION( "base CBM that recorded what it taught" ) {
    }
    SECTION( "base CBM installed before that was recorded" ) {
        ( *you.find_bionic_by_uid( base_uid ) )->remove_flag( "cbm_grants_recorded" );
    }
    REQUIRE( you.has_proficiency( proficiency_prof_lockpicking ) );
    REQUIRE( you.magic->knows_spell( spell_test_spell_pew ) );
    you.magic->get_spell( spell_test_spell_pew ).set_level( you, 6 );
    const int experience = you.magic->get_spell( spell_test_spell_pew ).xp();

    you.perform_install( test_bio_mutb10_upgrade, base_uid, 0, 1, 0, "NOT_MED", {},
                         you.pos_bub() );
    REQUIRE_FALSE( you.has_bionic( test_bio_mutb10_base ) );
    REQUIRE( you.has_bionic( test_bio_mutb10_upgrade ) );
    CHECK( you.has_proficiency( proficiency_prof_lockpicking ) );
    REQUIRE( you.magic->knows_spell( spell_test_spell_pew ) );
    CHECK( you.magic->get_spell( spell_test_spell_pew ).xp() == experience );

    // The upgrade now counts as having taught them.
    remove_bionic_of_type( you, test_bio_mutb10_upgrade );
    CHECK_FALSE( you.has_proficiency( proficiency_prof_lockpicking ) );
    CHECK_FALSE( you.magic->knows_spell( spell_test_spell_pew ) );
    clear_avatar();
}

static const bionic_id test_bio_mutb11_arm( "test_bio_mutb11_arm" );

static const efftype_id effect_under_operation( "under_operation" );

static const itype_id itype_anesthetic( "anesthetic" );

static const mtype_id mon_nursebot_defective( "mon_nursebot_defective" );

static const trait_id trait_TEST_MUTB11_SCAR( "TEST_MUTB11_SCAR" );

// Records the CBM removal events sent while it lives.
struct cbm_removal_events : public event_subscriber {
    cbm_removal_events() {
        get_event_bus().subscribe( this );
    }
    ~cbm_removal_events() override {
        get_event_bus().unsubscribe( this );
    }
    cbm_removal_events( const cbm_removal_events & ) = delete;
    cbm_removal_events &operator=( const cbm_removal_events & ) = delete;
    using event_subscriber::notify;
    void notify( const cata::event &e ) override {
        if( e.type() == event_type::removes_cbm ) {
            ++removed;
        } else if( e.type() == event_type::fails_to_remove_cbm ) {
            ++failed;
        }
    }
    int removed = 0;
    int failed = 0;
};

// MUT-B11: a nurse bot's failed removal ran its consequences on the avatar instead of the
// patient, with no part under operation, so they did nothing.
TEST_CASE( "mut-b11_nurse_bot_failed_removal_hurts_the_patient", "[audit][bionics]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms spot = you.pos_bub() + point::east;
    standard_npc patient( "Patient", spot );
    patient.add_bionic( test_bio_mutb11_arm );
    monster bot( mon_nursebot_defective );
    bot.ammo[itype_anesthetic] = 1;
    const bionic &cbm = **patient.find_bionic_by_type( test_bio_mutb11_arm );
    const int arm_hp = patient.get_part_hp_cur( body_part_arm_l );
    const int your_hp = you.get_hp();
    cbm_removal_events events;

    // With almost no skill the removal fails as badly as it can.
    patient.uninstall_bionic( cbm, bot, patient, 0.01f );
    CHECK( events.failed == 1 );
    CHECK( events.removed == 0 );
    CHECK( patient.has_bionic( test_bio_mutb11_arm ) );
    CHECK( patient.get_part_hp_cur( body_part_arm_l ) < arm_hp );
    CHECK_FALSE( patient.has_effect( effect_under_operation ) );
    CHECK( you.get_hp() == your_hp );
    CHECK( you.get_pain() == 0 );
    here.i_clear( spot );
    clear_avatar();
}

// MUT-B11: a nurse bot's successful removal only took the bionic out, skipping what
// removing one normally does, such as giving its give_mut_on_removal traits.
TEST_CASE( "mut-b11_nurse_bot_removal_does_the_usual_cleanup", "[audit][bionics]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms spot = you.pos_bub() + point::east;
    standard_npc patient( "Patient", spot );
    patient.add_bionic( test_bio_mutb11_arm );
    monster bot( mon_nursebot_defective );
    cbm_removal_events events;

    // This much skill nearly always succeeds, and a failure does no harm.
    for( int tries = 0; tries < 10 && patient.has_bionic( test_bio_mutb11_arm ); ++tries ) {
        bot.ammo[itype_anesthetic] = 1;
        patient.uninstall_bionic( **patient.find_bionic_by_type( test_bio_mutb11_arm ), bot, patient,
                                  10000.0f );
    }
    REQUIRE_FALSE( patient.has_bionic( test_bio_mutb11_arm ) );
    CHECK( events.removed == 1 );
    CHECK( patient.has_trait( trait_TEST_MUTB11_SCAR ) );
    CHECK_FALSE( patient.has_effect( effect_under_operation ) );
    here.i_clear( spot );
    clear_avatar();
}
