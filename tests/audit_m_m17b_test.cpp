#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "bionics.h"
#include "bodypart.h"
#include "cata_catch.h"
#include "character.h"
#include "coordinates.h"
#include "damage.h"
#include "magic_enchantment.h"
#include "math_parser_diag_value.h"
#include "mutation.h"
#include "player_helpers.h"
#include "translation.h"
#include "type_id.h"
#include "units.h"

static const bionic_id bio_power_storage( "bio_power_storage" );
static const bionic_id bio_radscrubber( "bio_radscrubber" );
static const bionic_id test_bio_mutj08_extend( "test_bio_mutj08_extend" );
static const bionic_id test_bio_mutj08_parent( "test_bio_mutj08_parent" );
static const bionic_id test_bio_mutj08_restate( "test_bio_mutj08_restate" );
static const bionic_id test_bio_mutj09_faulty( "test_bio_mutj09_faulty" );
static const bionic_id test_bio_mutj09_pooled( "test_bio_mutj09_pooled" );
static const bionic_id test_bio_mutj12_lamp( "test_bio_mutj12_lamp" );
static const bionic_id test_bio_mutj12_scrubber( "test_bio_mutj12_scrubber" );
static const bionic_id test_bio_mutj12_trigger( "test_bio_mutj12_trigger" );

static const damage_type_id damage_bash( "bash" );

static const effect_on_condition_id effect_on_condition_EOC_TEST_MUTJ08_A( "EOC_TEST_MUTJ08_A" );
static const effect_on_condition_id effect_on_condition_EOC_TEST_MUTJ08_B( "EOC_TEST_MUTJ08_B" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_MUTJ08_BIO_A( "EOC_TEST_MUTJ08_BIO_A" );

static const enchantment_id
enchantment_INLINE_ENCH_TEST_MUTJ08_EXTEND_0( "INLINE_ENCH_TEST_MUTJ08_EXTEND_0" );
static const enchantment_id
enchantment_INLINE_ENCH_TEST_MUTJ08_PARENT_0( "INLINE_ENCH_TEST_MUTJ08_PARENT_0" );
static const enchantment_id
enchantment_INLINE_ENCH_TEST_MUTJ08_RESTATE_0( "INLINE_ENCH_TEST_MUTJ08_RESTATE_0" );
static const enchantment_id
enchantment_INLINE_ENCH_test_bio_mutj08_parent_0( "INLINE_ENCH_test_bio_mutj08_parent_0" );
static const enchantment_id
enchantment_INLINE_ENCH_test_bio_mutj08_restate_0( "INLINE_ENCH_test_bio_mutj08_restate_0" );
static const enchantment_id enchantment_TEST_MUTJ08_NAMED_ENCH( "TEST_MUTJ08_NAMED_ENCH" );

static const itype_id itype_integrated_claws( "integrated_claws" );

static const json_character_flag json_flag_SURGICAL_TRAINING( "SURGICAL_TRAINING" );

static const mutation_category_id mutation_category_ALPHA( "ALPHA" );
static const mutation_category_id mutation_category_TEST_MUTJ10_TUNED( "TEST_MUTJ10_TUNED" );

static const species_id species_HUMAN( "HUMAN" );

static const trait_id trait_PROF_MED( "PROF_MED" );
static const trait_id trait_TEST_MUTJ08_DELETE( "TEST_MUTJ08_DELETE" );
static const trait_id trait_TEST_MUTJ08_EXTEND( "TEST_MUTJ08_EXTEND" );
static const trait_id trait_TEST_MUTJ08_PARENT( "TEST_MUTJ08_PARENT" );
static const trait_id trait_TEST_MUTJ08_RESTATE( "TEST_MUTJ08_RESTATE" );
static const trait_id trait_TEST_MUTJ09_SURGEON( "TEST_MUTJ09_SURGEON" );
static const trait_id trait_TEST_MUTJ10_GOOD( "TEST_MUTJ10_GOOD" );
static const trait_id trait_TEST_MUTJ11_COSTS( "TEST_MUTJ11_COSTS" );
static const trait_id trait_TEST_MUTJ11_LEGACY( "TEST_MUTJ11_LEGACY" );
static const trait_id trait_TEST_MUTJ11_POWERED( "TEST_MUTJ11_POWERED" );

using eoc_list = std::vector<effect_on_condition_id>;
using ench_list = std::vector<enchantment_id>;

// A numeric character variable as math sees it (unset reads as 0).
static double char_var( const Character &you, const std::string &name )
{
    const diag_value *val = you.maybe_get_value( name );
    return val == nullptr ? 0.0 : val->dbl();
}

static void give_power_storage( Character &you )
{
    for( int i = 0; i < 5; ++i ) {
        you.add_bionic( bio_power_storage );
    }
    REQUIRE( you.get_max_power_level() >= 100_kJ );
}

static float torso_bash_armor( const mutation_branch &mut )
{
    const auto found = mut.armor.find( body_part_torso );
    return found == mut.armor.end() ? 0.0f : found->second.type_resist( damage_bash );
}

// MUT-J08: copy-from appended a restated list to the parent's (EOCs, enchantments, integrated
// armor, attacks), added restated armor to it and kept the parent's value of a restated key.
// A same-id copy that restated its parent's inline enchantment applied it twice.
TEST_CASE( "mut-j08_restated_trait_lists_replace_the_parents", "[audit][mutation][json]" )
{
    const mutation_branch &parent = trait_TEST_MUTJ08_PARENT.obj();
    REQUIRE( parent.processed_eocs == eoc_list{ effect_on_condition_EOC_TEST_MUTJ08_A } );
    REQUIRE( parent.enchantments == ench_list{ enchantment_INLINE_ENCH_TEST_MUTJ08_PARENT_0 } );
    REQUIRE( parent.attacks_granted.size() == 1 );
    REQUIRE( torso_bash_armor( parent ) == Approx( 2.0f ) );

    const mutation_branch &copy = trait_TEST_MUTJ08_RESTATE.obj();
    CHECK( copy.processed_eocs == eoc_list{ effect_on_condition_EOC_TEST_MUTJ08_B } );
    CHECK( copy.enchantments == ench_list{ enchantment_INLINE_ENCH_TEST_MUTJ08_RESTATE_0 } );
    CHECK( copy.integrated_armor == std::vector<itype_id> { itype_integrated_claws } );
    CHECK( copy.lumination == std::map<bodypart_str_id, float> { { body_part_head, 2.0f } } );
    CHECK( copy.anger_relations == std::map<species_id, int> { { species_HUMAN, 20 } } );
    REQUIRE( copy.attacks_granted.size() == 1 );
    CHECK( copy.attacks_granted.front().chance == 20 );
    CHECK( torso_bash_armor( copy ) == Approx( 5.0f ) );
}

TEST_CASE( "mut-j08_extend_and_delete_change_inherited_trait_lists", "[audit][mutation][json]" )
{
    SECTION( "extend adds, and replaces the value of a key already there" ) {
        const mutation_branch &copy = trait_TEST_MUTJ08_EXTEND.obj();
        CHECK( copy.processed_eocs ==
               eoc_list{ effect_on_condition_EOC_TEST_MUTJ08_A, effect_on_condition_EOC_TEST_MUTJ08_B } );
        // An inline enchantment of the copy is named after the copy, not counted from 0 of
        // the parent's, so it can't replace the parent's
        CHECK( copy.enchantments == ench_list{ enchantment_INLINE_ENCH_TEST_MUTJ08_PARENT_0,
                                               enchantment_TEST_MUTJ08_NAMED_ENCH,
                                               enchantment_INLINE_ENCH_TEST_MUTJ08_EXTEND_0 } );
        // torso extended to 6, head deleted
        CHECK( copy.lumination == std::map<bodypart_str_id, float> { { body_part_torso, 6.0f } } );
        CHECK( copy.anger_relations.empty() );
        CHECK( copy.attacks_granted.size() == 2 );
        CHECK( torso_bash_armor( copy ) == Approx( 5.0f ) );
        CHECK( copy.integrated_armor == std::vector<itype_id> { itype_integrated_claws } );
    }
    SECTION( "delete takes ids out, and an empty list clears" ) {
        const mutation_branch &copy = trait_TEST_MUTJ08_DELETE.obj();
        CHECK( copy.processed_eocs.empty() );
        CHECK( copy.enchantments.empty() );
        CHECK( copy.integrated_armor.empty() );
        CHECK( copy.attacks_granted.empty() );
        // what it doesn't name is inherited as it is
        CHECK( copy.lumination.size() == 2 );
        CHECK( torso_bash_armor( copy ) == Approx( 2.0f ) );
    }
}

// MUT-J08: bionics had the same appending lists, and named their inline enchantments after
// their (translated) name: two bionics of the same name shared, and overwrote, one enchantment.
TEST_CASE( "mut-j08_bionic_lists_and_inline_enchantment_names", "[audit][bionics][json]" )
{
    clear_avatar();
    const avatar &you = get_avatar();
    const bionic_data &parent = test_bio_mutj08_parent.obj();
    const bionic_data &restated = test_bio_mutj08_restate.obj();
    const bionic_data &extended = test_bio_mutj08_extend.obj();
    REQUIRE( parent.name.translated() == restated.name.translated() );

    CHECK( parent.activated_eocs == eoc_list{ effect_on_condition_EOC_TEST_MUTJ08_BIO_A } );
    CHECK( parent.enchantments == ench_list{ enchantment_INLINE_ENCH_test_bio_mutj08_parent_0 } );
    CHECK( restated.activated_eocs == eoc_list{ effect_on_condition_EOC_TEST_MUTJ08_B } );
    CHECK( restated.enchantments == ench_list{ enchantment_INLINE_ENCH_test_bio_mutj08_restate_0 } );
    CHECK( extended.activated_eocs ==
           eoc_list{ effect_on_condition_EOC_TEST_MUTJ08_BIO_A, effect_on_condition_EOC_TEST_MUTJ08_B } );
    CHECK( extended.enchantments == ench_list{ enchantment_INLINE_ENCH_test_bio_mutj08_parent_0,
                                               enchantment_TEST_MUTJ08_NAMED_ENCH } );

    // Each keeps its own enchantment: the copy's didn't overwrite the parent's
    CHECK( enchantment_INLINE_ENCH_test_bio_mutj08_parent_0->get_value_add(
               enchant_vals::mod::SPEED, you ) == Approx( 1.0 ) );
    CHECK( enchantment_INLINE_ENCH_test_bio_mutj08_restate_0->get_value_add(
               enchant_vals::mod::SPEED, you ) == Approx( 3.0 ) );
}

// MUT-J09: the surgery bonus of medical training was hardcoded to two trait ids; it is now the
// BIONIC_SURGERY_SKILL enchantment value and the SURGICAL_TRAINING flag, on those traits in JSON.
TEST_CASE( "mut-j09_surgery_training_comes_from_json", "[audit][bionics][json]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    // A given skill level counts 12 a level, before any bonus
    REQUIRE( you.bionics_pl_skill( false, 5 ) == 60 );
    REQUIRE_FALSE( you.has_flag( json_flag_SURGICAL_TRAINING ) );

    SECTION( "the MD trait still adds 3, now from its JSON" ) {
        you.set_mutation( trait_PROF_MED );
        CHECK( you.has_flag( json_flag_SURGICAL_TRAINING ) );
        CHECK( you.bionics_pl_skill( false, 5 ) == 63 );
        CHECK( you.bionics_pl_skill( true, 5 ) == 63 );
    }
    SECTION( "a modded trait can make a surgeon" ) {
        you.set_mutation( trait_TEST_MUTJ09_SURGEON );
        CHECK( you.has_flag( json_flag_SURGICAL_TRAINING ) );
        CHECK( you.bionics_pl_skill( false, 5 ) == 70 );
    }
    clear_avatar();
}

// MUT-J09: a failed installation could only leave a bionic from the one global faulty pool.
TEST_CASE( "mut-j09_failed_installation_uses_the_bionics_faulty_pool", "[audit][bionics][json]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    REQUIRE( test_bio_mutj09_pooled->faulty_pool ==
             std::vector<bionic_id> { test_bio_mutj09_faulty } );
    REQUIRE_FALSE( you.has_bionic( test_bio_mutj09_faulty ) );
    // 1 short of success, difficulty 2, skill 1: failure level sqrt( 1 * 4 * 2 / 1 ) = 2, a
    // faulty installation, which takes one of the pool (here the only one)
    you.bionics_install_failure( test_bio_mutj09_pooled, "NOT_MED", 2, -1, 1.0f, you.pos_bub() );
    CHECK( you.has_bionic( test_bio_mutj09_faulty ) );
    clear_avatar();
}

// MUT-J10: progression tuning was fixed in C++; a category now sets its bad-mutation odds,
// the level needed to try its threshold, and the threshold message.
TEST_CASE( "mut-j10_categories_tune_their_progression", "[audit][mutation][json]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    const mutation_category_trait &alpha =
        mutation_category_trait::get_category( mutation_category_ALPHA );
    CHECK( alpha.bad_mutation_chance_scale == Approx( 0.5f ) );
    CHECK( alpha.bad_mutation_chance_cap == Approx( 0.67f ) );
    CHECK( alpha.threshold_breach_min_level == 30 );
    CHECK( alpha.threshold_message() ==
           "Something strains mightily for a moment… and then… you're… FREE!" );

    const mutation_category_trait &tuned =
        mutation_category_trait::get_category( mutation_category_TEST_MUTJ10_TUNED );
    CHECK( tuned.bad_mutation_chance_cap == Approx( 0.0f ) );
    CHECK( tuned.threshold_breach_min_level == 0 );
    CHECK( tuned.threshold_message() == "You cross the test threshold." );

    SECTION( "a cap of 0 means no bad mutations, however unstable" ) {
        you.set_mutation( trait_TEST_MUTJ10_GOOD );
        REQUIRE( you.get_instability_per_category( mutation_category_TEST_MUTJ10_TUNED ) > 0 );
        int bad = 0;
        for( int i = 0; i < 50; ++i ) {
            if( you.roll_bad_mutation( mutation_category_TEST_MUTJ10_TUNED ) ) {
                ++bad;
            }
        }
        CHECK( bad == 0 );
    }
    SECTION( "the threshold can be tried at the category's own level" ) {
        // Level 0: enough for the tuned category, not for Alpha's 30
        REQUIRE_FALSE( you.crossed_threshold() );
        CHECK( you.can_cross_threshold( mutation_category_TEST_MUTJ10_TUNED ) );
        CHECK_FALSE( you.can_cross_threshold( mutation_category_ALPHA ) );
    }
    clear_avatar();
}

// MUT-J11: a trait had one cost for every resource it used, and could not use bionic power.
TEST_CASE( "mut-j11_each_resource_has_its_own_cost", "[audit][mutation][json]" )
{
    const mutation_branch &costly = trait_TEST_MUTJ11_COSTS.obj();
    CHECK( costly.hunger );
    CHECK( costly.kcal_cost == 50 );
    CHECK( costly.stamina );
    CHECK( costly.stamina_cost == 300 );
    CHECK( costly.power_cost == 5_kJ );
    CHECK_FALSE( costly.thirst );
    CHECK_FALSE( costly.mana );
    CHECK( costly.has_cost() );

    // The legacy form: one amount for each flagged resource
    const mutation_branch &legacy = trait_TEST_MUTJ11_LEGACY.obj();
    CHECK( legacy.kcal_cost == 7 );
    CHECK( legacy.mana_cost == 7 );
    CHECK( legacy.thirst_cost == 0 );
    CHECK( legacy.stamina_cost == 0 );
    CHECK( legacy.power_cost == 0_kJ );

    clear_avatar();
    avatar &you = get_avatar();
    give_power_storage( you );
    you.set_power_level( 20_kJ );
    REQUIRE( you.get_power_level() == 20_kJ );
    you.set_mutation( trait_TEST_MUTJ11_COSTS );

    SECTION( "activating pays each amount" ) {
        const int kcal = you.get_stored_kcal();
        const int stamina = you.get_stamina();
        REQUIRE( you.can_power_mutation( trait_TEST_MUTJ11_COSTS ) );
        you.activate_mutation( trait_TEST_MUTJ11_COSTS );
        CHECK( you.has_active_mutation( trait_TEST_MUTJ11_COSTS ) );
        CHECK( you.get_stored_kcal() == kcal - 50 );
        CHECK( you.get_stamina() == stamina - 300 );
        CHECK( you.get_power_level() == 15_kJ );
    }
    SECTION( "too little power stops it" ) {
        you.set_power_level( 4_kJ );
        CHECK_FALSE( you.can_power_mutation( trait_TEST_MUTJ11_COSTS ) );
        you.activate_mutation( trait_TEST_MUTJ11_COSTS );
        CHECK_FALSE( you.has_active_mutation( trait_TEST_MUTJ11_COSTS ) );
        CHECK( you.get_power_level() == 4_kJ );
    }
    clear_avatar();
}

TEST_CASE( "mut-j11_a_trait_can_run_on_bionic_power", "[audit][mutation][json]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    give_power_storage( you );
    you.set_power_level( 12_kJ );
    REQUIRE( you.get_power_level() == 12_kJ );
    you.set_mutation( trait_TEST_MUTJ11_POWERED );
    you.activate_mutation( trait_TEST_MUTJ11_POWERED );
    REQUIRE( you.has_active_mutation( trait_TEST_MUTJ11_POWERED ) );
    REQUIRE( you.get_power_level() == 7_kJ );

    // No "time": the upkeep is paid each turn
    you.suffer();
    CHECK( you.has_active_mutation( trait_TEST_MUTJ11_POWERED ) );
    CHECK( you.get_power_level() == 2_kJ );
    // Not enough left: it switches off and takes nothing
    you.suffer();
    CHECK_FALSE( you.has_active_mutation( trait_TEST_MUTJ11_POWERED ) );
    CHECK( you.get_power_level() == 2_kJ );
    clear_avatar();
}

// MUT-J12: a bionic copied from one with coded effects lost them under its new id, and
// auto_deactivates only worked for bionics without coded effects.
TEST_CASE( "mut-j12_copied_bionic_keeps_its_coded_effects", "[audit][bionics][json]" )
{
    CHECK( test_bio_mutj12_scrubber->hardcoded_behavior == bio_radscrubber );
    CHECK( test_bio_mutj12_lamp->hardcoded_behavior == test_bio_mutj12_lamp );
    CHECK( bio_radscrubber->hardcoded_behavior == bio_radscrubber );

    clear_avatar();
    avatar &you = get_avatar();
    give_power_storage( you );
    you.set_power_level( you.get_max_power_level() );
    const bionic_uid lamp_uid = you.add_bionic( test_bio_mutj12_lamp );
    const bionic_uid scrubber_uid = you.add_bionic( test_bio_mutj12_scrubber );
    REQUIRE( you.activate_bionic( **you.find_bionic_by_uid( lamp_uid ) ) );
    REQUIRE( ( *you.find_bionic_by_uid( lamp_uid ) )->powered );

    you.set_rad( 12 );
    REQUIRE( you.activate_bionic( **you.find_bionic_by_uid( scrubber_uid ) ) );
    // The scrubber's coded effect takes 5 off
    CHECK( you.get_rad() == 7 );
    // and its auto_deactivates switched the lamp off
    CHECK_FALSE( ( *you.find_bionic_by_uid( lamp_uid ) )->powered );
    you.set_rad( 0 );
    clear_avatar();
}

// MUT-J12: trigger_cost was only charged by some coded bionics; processed_eocs get it now.
TEST_CASE( "mut-j12_processed_eocs_get_the_trigger_cost", "[audit][bionics][eoc]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    const bionic_uid uid = you.add_bionic( test_bio_mutj12_trigger );
    bionic &bio = **you.find_bionic_by_uid( uid );
    REQUIRE_FALSE( bio.info().activated );
    you.process_bionic( bio );
    // 3 kJ, in mJ
    CHECK( char_var( you, "mutj12_trigger" ) == Approx( 3000000.0 ) );
    clear_avatar();
}
