#include <string>
#include <utility>

#include "avatar.h"
#include "cata_catch.h"
#include "character.h"
#include "item.h"
#include "player_helpers.h"
#include "ret_val.h"
#include "type_id.h"

static const flag_id json_flag_INEDIBLE( "INEDIBLE" );

static const itype_id itype_apple( "apple" );
static const itype_id itype_bone( "bone" );
static const itype_id itype_egg_chicken( "egg_chicken" );
static const itype_id itype_meat( "meat" );

static const morale_type morale_antimeat( "morale_antimeat" );

static const trait_id trait_TEST_EDIBLE_LUPINE_FOOD( "TEST_EDIBLE_LUPINE_FOOD" );
static const trait_id trait_TEST_EGG_ALLERGY( "TEST_EGG_ALLERGY" );
static const trait_id trait_TEST_NO_RAW_FOOD( "TEST_NO_RAW_FOOD" );
static const trait_id trait_THRESH_LUPINE( "THRESH_LUPINE" );
static const trait_id trait_VEGETARIAN( "VEGETARIAN" );

// The animal thresholds' right to eat INEDIBLE food with their flag was hardcoded per trait
TEST_CASE( "hlth-j03_edible_food_flags_let_a_mutation_eat_inedible_food",
           "[audit][food][mutations]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    const item bone( itype_bone );
    REQUIRE( bone.has_flag( json_flag_INEDIBLE ) );
    REQUIRE_FALSE( you.can_eat( bone ).success() );
    REQUIRE( you.fun_for( bone ).first == 0 );

    SECTION( "a JSON mutation with the food's flag can eat it and doesn't mind the taste" ) {
        you.toggle_trait( trait_TEST_EDIBLE_LUPINE_FOOD );
        REQUIRE( you.has_trait( trait_TEST_EDIBLE_LUPINE_FOOD ) );
        CHECK( you.can_eat( bone ).success() );
        CHECK( you.fun_for( bone ).first == 2 );
    }

    SECTION( "the wolf threshold gets the same rule from its JSON" ) {
        you.toggle_trait( trait_THRESH_LUPINE );
        REQUIRE( you.has_trait( trait_THRESH_LUPINE ) );
        CHECK( you.can_eat( bone ).success() );
        CHECK( you.fun_for( bone ).first == 2 );
    }
}

// allergy_type() mapped six hardcoded traits to allergens; a mod could not add an allergy
TEST_CASE( "hlth-j03_food_allergies_come_from_mutation_json", "[audit][food][mutations]" )
{
    clear_avatar();
    avatar &you = get_avatar();

    SECTION( "a JSON-only allergy" ) {
        const item egg( itype_egg_chicken );
        REQUIRE( you.allergy_type( egg ).is_null() );
        you.toggle_trait( trait_TEST_EGG_ALLERGY );
        REQUIRE( you.has_trait( trait_TEST_EGG_ALLERGY ) );
        CHECK( you.allergy_type( egg ) == morale_antimeat );
        CHECK( you.allergy_type( item( itype_apple ) ).is_null() );
    }

    SECTION( "the migrated meat intolerance" ) {
        const item meat( itype_meat );
        REQUIRE( you.allergy_type( meat ).is_null() );
        you.toggle_trait( trait_VEGETARIAN );
        REQUIRE( you.has_trait( trait_VEGETARIAN ) );
        CHECK( you.allergy_type( meat ) == morale_antimeat );
    }
}

// Consumption EOCs run after the food is eaten, so nothing in JSON could refuse food
TEST_CASE( "hlth-j03_eat_condition_vetoes_food_before_eating", "[audit][food][mutations]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    const item meat( itype_meat );
    const item apple( itype_apple );
    REQUIRE( you.can_eat( meat ).success() );
    REQUIRE( you.can_eat( apple ).success() );

    you.toggle_trait( trait_TEST_NO_RAW_FOOD );
    REQUIRE( you.has_trait( trait_TEST_NO_RAW_FOOD ) );

    const ret_val<edible_rating> refused = you.can_eat( meat );
    CHECK_FALSE( refused.success() );
    CHECK( refused.value() == INEDIBLE_MUTATION );
    CHECK( refused.str() == "Test: you won't eat raw food." );
    // Food the condition accepts is still edible
    CHECK( you.can_eat( apple ).success() );
}
