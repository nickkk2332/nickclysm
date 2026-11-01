#include <algorithm>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_utility.h"
#include "character.h"
#include "item.h"
#include "item_components.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "recipe.h"
#include "requirements.h"
#include "rng.h"
#include "type_id.h"

static const itype_id itype_2x4( "2x4" );

static const recipe_id recipe_bp_40fmj_test_no_tools( "bp_40fmj_test_no_tools" );
static const recipe_id recipe_cudgel_test_no_tools( "cudgel_test_no_tools" );

static const trait_id trait_DEBUG_CNF( "DEBUG_CNF" );

// A crafter with no skills but every proficiency the recipe lists, so the failure roll
// sits well under the recipe difficulty without proficiency penalties muddying it.
// The map is cleared so no leftover NPC helps with the craft.
static avatar &prepare_unskilled_crafter( const recipe &making )
{
    clear_map();
    clear_avatar();
    avatar &u = get_avatar();
    for( const recipe_proficiency &rprof : making.get_proficiencies() ) {
        u.add_proficiency( rprof.id, true );
    }
    REQUIRE_FALSE( u.has_trait( trait_DEBUG_CNF ) );
    return u;
}

// CRAFT-B01: crafting_failure_roll() returned the raw skill roll and never subtracted the
// recipe difficulty, while item_destruction_chance() (the "Catastrophic Failure Chance" in
// the crafting menu) does.  A failure roll under 1 is what puts components at risk in
// item::handle_craft_failure(), so how often that happens must match the displayed chance.
TEST_CASE( "craft-b01_failure_roll_matches_displayed_destruction_chance", "[audit]" )
{
    const std::vector<recipe_id> recipes = {
        recipe_cudgel_test_no_tools, recipe_bp_40fmj_test_no_tools
    };
    for( const recipe_id &rid : recipes ) {
        INFO( rid.str() );
        const recipe &making = rid.obj();
        avatar &u = prepare_unskilled_crafter( making );

        const Character::craft_roll_data data = u.recipe_failure_roll_data( making );
        const float displayed = u.item_destruction_chance( making );
        CAPTURE( data.center, data.stddev, data.final_difficulty, displayed );
        // The difficulty term must matter and the chance must be far from certain either way,
        // otherwise this scenario cannot tell the two rolls apart.
        REQUIRE( data.final_difficulty > 0.25f );
        REQUIRE( displayed > 0.1f );
        REQUIRE( displayed < 0.9f );

        const int iterations = 50000;
        int at_risk = 0;
        for( int i = 0; i < iterations; ++i ) {
            if( u.crafting_failure_roll( making ) < 1.0f ) {
                ++at_risk;
            }
        }
        const float observed = static_cast<float>( at_risk ) / iterations;
        CHECK( observed == Approx( displayed ).margin( 0.02 ) );
    }
}

// CRAFT-B01: handle_craft_failure() destroys each at-risk component with probability
// 1 - failure roll.  With a single component, the rate it is lost at must follow the
// difficulty-adjusted roll, not the raw skill roll (which made losses almost never happen).
TEST_CASE( "craft-b01_craft_failure_destroys_components_per_difficulty", "[audit]" )
{
    const recipe &making = recipe_bp_40fmj_test_no_tools.obj();
    avatar &u = prepare_unskilled_crafter( making );

    const Character::craft_roll_data data = u.recipe_failure_roll_data( making );
    REQUIRE( data.final_difficulty > 0.25f );

    // Chance to lose the only component, integrated over the difficulty-adjusted roll
    const int samples = 200000;
    double expected = 0.0;
    for( int i = 0; i < samples; ++i ) {
        const double roll = std::max( normal_roll( data.center, data.stddev ), 0.0 ) -
                            data.final_difficulty;
        expected += 1.0 - clamp( roll, 0.0, 1.0 );
    }
    expected /= samples;
    CAPTURE( data.center, data.stddev, data.final_difficulty, expected );
    REQUIRE( expected > 0.2 );

    REQUIRE( item( &making, 1, item_components(), std::vector<item_comp>() ).is_craft() );
    const int trials = 10000;
    int destroyed = 0;
    for( int i = 0; i < trials; ++i ) {
        item component( itype_2x4, calendar::turn );
        item_components comps;
        comps.add( component );
        item craft( &making, 1, comps, std::vector<item_comp>() );
        // Keep can_continue_craft() away from the map: no tools to re-check
        craft.set_tools_to_continue( true );
        if( craft.handle_craft_failure( u ) ) {
            ++destroyed;
        }
    }
    const double observed = static_cast<double>( destroyed ) / trials;
    CHECK( observed == Approx( expected ).margin( 0.03 ) );
}
