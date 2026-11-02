#include "addiction.h"
#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "effect.h"
#include "player_helpers.h"
#include "type_id.h"

static const addiction_id addiction_nicotine( "nicotine" );

static const bodypart_str_id body_part_arm_insect_l( "arm_insect_l" );
static const bodypart_str_id body_part_wing_insect_l( "wing_insect_l" );

static const efftype_id effect_mending( "mending" );

static const trait_id trait_INSECT_ARMS( "INSECT_ARMS" );
static const trait_id trait_REGEN_LIZ( "REGEN_LIZ" );
static const trait_id trait_WINGS_INSECT( "WINGS_INSECT" );

// How many turns of mending the part has accumulated.
static int audit_mend_turns( Character &who, const bodypart_id &bp )
{
    return to_turns<int>( who.get_effect_dur( effect_mending, bp ) );
}

// HLTH-B02: Character::mend multiplied the shared healing factor by each broken limb's
// mend_rate, so every later limb inherited the product of the earlier limbs' rates.
// Whatever order the parts are visited in, the second one got the wrong rate.
TEST_CASE( "hlth-b02_mend_rate_applies_per_limb", "[audit][health]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    u.set_mutation( trait_REGEN_LIZ );
    u.set_mutation( trait_INSECT_ARMS );
    u.set_mutation( trait_WINGS_INSECT );
    u.recalculate_enchantment_cache();
    REQUIRE( u.has_part( body_part_arm_insect_l ) );
    REQUIRE( u.has_part( body_part_wing_insect_l ) );
    REQUIRE( body_part_arm_insect_l->mend_rate == Approx( 0.7f ) );
    REQUIRE( body_part_wing_insect_l->mend_rate == Approx( 0.4f ) );

    u.set_part_hp_cur( body_part_arm_insect_l, 0 );
    u.set_part_hp_cur( body_part_wing_insect_l, 0 );
    REQUIRE( u.is_limb_broken( body_part_arm_insect_l ) );
    REQUIRE( u.is_limb_broken( body_part_wing_insect_l ) );

    u.mend( 1000 );
    const int arm = audit_mend_turns( u, body_part_arm_insect_l );
    const int wing = audit_mend_turns( u, body_part_wing_insect_l );
    REQUIRE( arm > 100 );
    REQUIRE( wing > 100 );
    // Each limb mends at its own rate: 0.7 / 0.4. The old code gave 2.5 or 0.7.
    CHECK( static_cast<double>( arm ) / wing == Approx( 0.7 / 0.4 ).epsilon( 0.02 ) );
    clear_avatar();
}
