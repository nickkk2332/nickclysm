#include "avatar.h"
#include "cata_catch.h"
#include "character.h"
#include "player_helpers.h"
#include "type_id.h"

static const mutation_category_id mutation_category_REMOVAL_TEST( "REMOVAL_TEST" );

static const trait_id trait_THRESH_TEST_REMOVAL( "THRESH_TEST_REMOVAL" );
static const trait_id trait_TEST_MUTB05_LOW( "TEST_MUTB05_LOW" );
static const trait_id trait_TEST_MUTB05_POST_THRESH( "TEST_MUTB05_POST_THRESH" );
static const trait_id trait_TEST_MUTB05_PRICEY( "TEST_MUTB05_PRICEY" );
static const trait_id trait_TEST_REMOVAL_0( "TEST_REMOVAL_0" );
static const trait_id trait_TEST_REMOVAL_2( "TEST_REMOVAL_2" );

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
