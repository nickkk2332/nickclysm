#include <functional>
#include <memory>
#include <string>
#include <unordered_set>

#include "avatar.h"
#include "cata_catch.h"
#include "character.h"
#include "condition.h"
#include "debug.h"
#include "dialogue.h"
#include "flexbuffer_json.h"
#include "json_loader.h"
#include "mutation.h"
#include "player_helpers.h"
#include "talker.h"
#include "type_id.h"

static const mutation_category_id mutation_category_AUDIT_NOT_A_CATEGORY( "AUDIT_NOT_A_CATEGORY" );
static const mutation_category_id mutation_category_HUMAN( "HUMAN" );
static const mutation_category_id mutation_category_REMOVAL_TEST( "REMOVAL_TEST" );

static const trait_id trait_TEST_DUMMY( "TEST_DUMMY" );
static const trait_id trait_TEST_GOOD1( "TEST_GOOD1" );
static const trait_id trait_TEST_MUTB05_LOW( "TEST_MUTB05_LOW" );
static const trait_id trait_TEST_MUTB05_POST_THRESH( "TEST_MUTB05_POST_THRESH" );
static const trait_id trait_TEST_MUTB05_PRICEY( "TEST_MUTB05_PRICEY" );
static const trait_id trait_TEST_REMOVAL_0( "TEST_REMOVAL_0" );
static const trait_id trait_TEST_REMOVAL_1( "TEST_REMOVAL_1" );
static const trait_id trait_TEST_REMOVAL_2( "TEST_REMOVAL_2" );
static const trait_id trait_THRESH_TEST_REMOVAL( "THRESH_TEST_REMOVAL" );

static const vitamin_id vitamin_mutagen_test_removal( "mutagen_test_removal" );

// MUT-B05: mutate_towards removed same-type and cancelled traits first, then ran the
// threshold, bionic and vitamin checks that could still fail. A pre-threshold mutant lost
// a trait to a post-threshold one and gained nothing.
TEST_CASE( "mut-b05_failed_threshold_check_keeps_conflicting_trait", "[audit][mutation]" )
{
    clear_avatar();
    Character &dummy = get_player_character();
    dummy.set_mutation( trait_TEST_MUTB05_LOW );
    REQUIRE( dummy.has_trait( trait_TEST_MUTB05_LOW ) );
    REQUIRE( !dummy.has_trait( trait_THRESH_TEST_REMOVAL ) );

    CHECK( !dummy.mutate_towards( trait_TEST_MUTB05_POST_THRESH, mutation_category_REMOVAL_TEST,
                                  nullptr, false ) );
    CHECK( dummy.has_trait( trait_TEST_MUTB05_LOW ) );
    CHECK( !dummy.has_trait( trait_TEST_MUTB05_POST_THRESH ) );
    clear_avatar();
}

// MUT-B05: replacing a starting trait multiplies the vitamin cost (x2 in REMOVAL_TEST), but
// the starting trait was erased before that cost was checked, so an unaffordable mutation
// still destroyed it for good.
TEST_CASE( "mut-b05_unaffordable_mutation_keeps_starting_trait", "[audit][mutation]" )
{
    clear_avatar();
    Character &dummy = get_player_character();
    dummy.my_traits.insert( trait_TEST_MUTB05_LOW );
    dummy.set_mutation( trait_TEST_MUTB05_LOW );
    REQUIRE( dummy.has_base_trait( trait_TEST_MUTB05_LOW ) );

    // TEST_MUTB05_PRICEY costs 200 alone and 400 when it replaces a starting trait.
    dummy.vitamin_set( vitamin_mutagen_test_removal, 300 );
    CHECK( !dummy.mutate_towards( trait_TEST_MUTB05_PRICEY, mutation_category_REMOVAL_TEST ) );
    CHECK( dummy.has_trait( trait_TEST_MUTB05_LOW ) );
    CHECK( dummy.has_base_trait( trait_TEST_MUTB05_LOW ) );
    CHECK( !dummy.has_trait( trait_TEST_MUTB05_PRICEY ) );
    CHECK( dummy.vitamin_get( vitamin_mutagen_test_removal ) == 300 );

    // With enough mutagen the starting trait is replaced at the multiplied cost.
    dummy.vitamin_set( vitamin_mutagen_test_removal, 1000 );
    CHECK( dummy.mutate_towards( trait_TEST_MUTB05_PRICEY, mutation_category_REMOVAL_TEST ) );
    CHECK( !dummy.has_trait( trait_TEST_MUTB05_LOW ) );
    CHECK( !dummy.has_base_trait( trait_TEST_MUTB05_LOW ) );
    CHECK( dummy.has_trait( trait_TEST_MUTB05_PRICEY ) );
    CHECK( dummy.vitamin_get( vitamin_mutagen_test_removal ) == 600 );
    clear_avatar();
}

// MUT-B05: a conflict with an unpurifiable trait went to canceltrait, which the
// cancel_recheck loop still saw, so mutate_towards reported success without mutating.
TEST_CASE( "mut-b05_unpurifiable_conflict_reports_failure", "[audit][mutation]" )
{
    clear_avatar();
    Character &dummy = get_player_character();
    dummy.set_mutation( trait_TEST_REMOVAL_0 );
    REQUIRE( dummy.has_trait( trait_TEST_REMOVAL_0 ) );
    REQUIRE( !dummy.purifiable( trait_TEST_REMOVAL_0 ) );

    CHECK( !dummy.mutate_towards( trait_TEST_REMOVAL_2 ) );
    CHECK( dummy.has_trait( trait_TEST_REMOVAL_0 ) );
    CHECK( !dummy.has_trait( trait_TEST_REMOVAL_2 ) );
    clear_avatar();
}

// MUT-B05 review: purifiers work through dummy traits (HUMAN_EYES, ...), whose conflicts
// usually include a starting trait that HUMAN (base_removal_chance 0) never removes. The
// dummy must still purge the removable conflicts and keep the protected one, instead of
// failing on the protected trait and purging nothing.
TEST_CASE( "mut-b05_dummy_trait_purges_removable_conflicts_past_starting_trait",
           "[audit][mutation]" )
{
    clear_avatar();
    Character &dummy = get_player_character();
    dummy.my_traits.insert( trait_TEST_REMOVAL_1 );
    dummy.set_mutation( trait_TEST_REMOVAL_1 );
    dummy.set_mutation( trait_TEST_GOOD1 );
    REQUIRE( trait_TEST_DUMMY->dummy );
    REQUIRE( mutation_category_trait::get_category( mutation_category_HUMAN ).base_removal_chance ==
             0 );
    REQUIRE( dummy.has_base_trait( trait_TEST_REMOVAL_1 ) );
    REQUIRE( dummy.has_trait( trait_TEST_GOOD1 ) );

    // Both traits are in TEST_DUMMY's cancels: TEST_GOOD1 goes, the starting trait stays.
    CHECK( dummy.mutate_towards( trait_TEST_DUMMY, mutation_category_HUMAN, nullptr, false ) );
    CHECK( !dummy.has_trait( trait_TEST_GOOD1 ) );
    CHECK( dummy.has_trait( trait_TEST_REMOVAL_1 ) );
    CHECK( dummy.has_base_trait( trait_TEST_REMOVAL_1 ) );
    CHECK( !dummy.has_trait( trait_TEST_DUMMY ) );
    clear_avatar();
}

static talk_effect_t load_mutation_effect( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    talk_effect_t effect( jo, "effect", "audit_mutation_towards_test" );
    finalize_conditions();
    return effect;
}

// MUT-J01: u_mutate_towards read "category" into a default str_or_var, so leaving it out
// (the documented default is ANY) gave "", and get_category("") dereferenced map::end().
TEST_CASE( "mut-j01_mutate_towards_without_category_defaults_to_any", "[audit][mutation]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    REQUIRE( !u.has_trait( trait_TEST_MUTB05_LOW ) );

    const talk_effect_t effect = load_mutation_effect(
                                     R"({ "effect": { "u_mutate_towards": "TEST_MUTB05_LOW" } })" );
    dialogue d( get_talker_for( u ), nullptr );
    effect.apply( d );
    CHECK( u.has_trait( trait_TEST_MUTB05_LOW ) );
    clear_avatar();
}

// MUT-J01: an unknown category id reached get_category() through the EOC effects and
// dereferenced map::end(). The effects now report it and do nothing, and get_category()
// itself returns an empty category instead.
TEST_CASE( "mut-j01_unknown_mutation_category_is_reported", "[audit][mutation]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    REQUIRE( !mutation_category_is_valid( mutation_category_AUDIT_NOT_A_CATEGORY ) );

    const std::string get_msg = capture_debugmsg_during( []() {
        const mutation_category_trait &cat = mutation_category_trait::get_category(
                mutation_category_AUDIT_NOT_A_CATEGORY );
        CHECK( cat.vitamin.is_null() );
        CHECK( cat.threshold_mut.is_empty() );
    } );
    CHECK( get_msg.find( "invalid mutation category" ) != std::string::npos );

    const talk_effect_t effect = load_mutation_effect(
                                     R"({ "effect": { "u_mutate_towards": "TEST_MUTB05_LOW", )"
                                     R"("category": "AUDIT_NOT_A_CATEGORY" } })" );
    dialogue d( get_talker_for( u ), nullptr );
    const std::string eoc_msg = capture_debugmsg_during( [&effect, &d]() {
        effect.apply( d );
    } );
    CHECK( eoc_msg.find( "invalid mutation category" ) != std::string::npos );
    CHECK( !u.has_trait( trait_TEST_MUTB05_LOW ) );
    clear_avatar();
}
