#include <algorithm>

#include "activity_tracker.h"
#include "addiction.h"
#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "damage.h"
#include "effect.h"
#include "effect_source.h"
#include "flag.h"
#include "game_constants.h"
#include "item.h"
#include "map.h"
#include "map_helpers.h"
#include "morale.h"
#include "player_helpers.h"
#include "type_id.h"
#include "wound.h"

static const addiction_id addiction_nicotine( "nicotine" );

static const bodypart_str_id body_part_arm_insect_l( "arm_insect_l" );
static const bodypart_str_id body_part_wing_insect_l( "wing_insect_l" );

static const damage_type_id damage_bash( "bash" );

static const efftype_id effect_debugged( "debugged" );
static const efftype_id effect_incorporeal( "incorporeal" );
static const efftype_id effect_mending( "mending" );
static const efftype_id effect_venom_dmg( "venom_dmg" );

static const itype_id itype_test_longshirt( "test_longshirt" );
static const itype_id itype_test_socks( "test_socks" );

static const morale_type morale_food_good( "morale_food_good" );

static const trait_id trait_INSECT_ARMS( "INSECT_ARMS" );
static const trait_id trait_REGEN_LIZ( "REGEN_LIZ" );
static const trait_id trait_WINGS_INSECT( "WINGS_INSECT" );

static const vitamin_id vitamin_iron( "iron" );

static const wound_type_id wound_test_wound_capped_pain( "test_wound_capped_pain" );
static const wound_type_id wound_test_wound_capped_vitamin( "test_wound_capped_vitamin" );

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
    clear_map();
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

// HLTH-B03: the addiction penalty to fracture healing used lerp( 0.25, 0.75, t ), so a
// level-3 addict healed at 25% speed and a level-20 addict at about 68%, the opposite of
// the documented 25% to 75% slowdown, and t was divided by MAX instead of MAX - MIN.
TEST_CASE( "hlth-b03_addiction_slows_fracture_healing_more_when_heavier", "[audit][health]" )
{
    clear_map();
    clear_avatar();
    avatar &u = get_avatar();
    // MEND_ALL, so no splint is needed.
    u.set_mutation( trait_REGEN_LIZ );
    u.recalculate_enchantment_cache();
    u.addictions.clear();
    u.set_part_hp_cur( body_part_arm_l, 0 );
    REQUIRE( u.is_limb_broken( body_part_arm_l ) );

    const auto mend_with_addiction = [&u]( int level ) {
        u.remove_effect( effect_mending, body_part_arm_l );
        u.addictions.clear();
        if( level > 0 ) {
            u.addictions.emplace_back( addiction_nicotine, level );
        }
        REQUIRE( u.addiction_level( addiction_nicotine ) == level );
        u.mend( 1000 );
        return audit_mend_turns( u, body_part_arm_l );
    };

    const int clean = mend_with_addiction( 0 );
    const int light = mend_with_addiction( MIN_ADDICTION_LEVEL );
    const int heavy = mend_with_addiction( MAX_ADDICTION_LEVEL );
    REQUIRE( clean > 1000 );

    CHECK( static_cast<double>( light ) / clean == Approx( 0.75 ).epsilon( 0.02 ) );
    CHECK( static_cast<double>( heavy ) / clean == Approx( 0.25 ).epsilon( 0.02 ) );
    CHECK( heavy < light );
    u.addictions.clear();
    clear_avatar();
}

// Basal metabolic rate with the activity tracker averaging exactly `level` this period.
static int audit_bmr_at( Character &who, float level )
{
    who.activity_history.reset_activity_level();
    // A new turn after a reset starts a fresh averaging period at rest.
    who.activity_history.new_turn( false );
    who.set_activity_level( level );
    REQUIRE( who.activity_history.average_activity() == Approx( std::max( level, NO_EXERCISE ) ) );
    return who.get_bmr();
}

// HLTH-B04: get_bmr rounded the activity multiplier up, so one step of walking in an
// otherwise idle five-minute window (average about 1.01) burned twice the resting rate.
TEST_CASE( "hlth-b04_bmr_scales_continuously_with_activity", "[audit][health]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    const int rest = audit_bmr_at( u, NO_EXERCISE );
    REQUIRE( rest == u.base_bmr() );
    REQUIRE( rest > 1000 );

    CHECK( audit_bmr_at( u, 1.01f ) == Approx( rest * 1.01 ).margin( 1 ) );
    CHECK( audit_bmr_at( u, 1.5f ) == Approx( rest * 1.5 ).margin( 1 ) );
    CHECK( audit_bmr_at( u, 2.5f ) == Approx( rest * 2.5 ).margin( 1 ) );
    // Whole exertion levels are unchanged.
    CHECK( audit_bmr_at( u, MODERATE_EXERCISE ) == rest * 4 );
    CHECK( audit_bmr_at( u, SLEEP_EXERCISE ) == rest );
    clear_avatar();
}

// HLTH-B05: get_mod and friends stop scaling at max_effective_intensity, but activated()
// and get_percentage() used the raw intensity. venom_dmg (max_effective_intensity 100,
// max_intensity 125) has hurt_chance one_in( 13 - 0.11 * ( intensity - 1 ) ), which
// truncates to 0 from intensity 111, and a chance of 0 fires on every tick.
TEST_CASE( "hlth-b05_effect_chances_stop_scaling_at_max_effective_intensity",
           "[audit][health]" )
{
    const effect_type *venom = &effect_venom_dmg.obj();
    const effect capped( effect_source::empty(), venom, 1_hours, body_part_arm_l, false, 100,
                         calendar::turn );
    const effect over( effect_source::empty(), venom, 1_hours, body_part_arm_l, false, 120,
                       calendar::turn );
    REQUIRE( over.get_intensity() == 120 );
    REQUIRE( over.get_max_effective_intensity() == 100 );
    REQUIRE( over.get_effective_intensity() == 100 );

    // At the cap: one_in( 2 ) every 36 turns.
    const double capped_hurt = capped.get_percentage( "HURT", 1, false );
    REQUIRE( capped_hurt == Approx( 50.0 / 36.0 ) );
    CHECK( over.get_percentage( "HURT", 1, false ) == Approx( capped_hurt ) );
    // pain_chance one_in( 20 - 0.15 * ( intensity - 1 ) ): 5 at the cap, 2 at 120.
    CHECK( over.get_percentage( "PAIN", 1, false ) ==
           Approx( capped.get_percentage( "PAIN", 1, false ) ) );
    CHECK( over.get_percentage( "PAIN", 1, false ) == Approx( 20.0 ) );

    int fired = 0;
    const int rolls = 200;
    for( int i = 1; i <= rolls; ++i ) {
        if( over.activated( calendar::turn_zero + time_duration::from_turns( 36 * i ), "HURT", 1,
                            false, 1.0 ) ) {
            fired++;
        }
    }
    // A coin flip per tick, not a hit on every tick.
    CHECK( fired > 0 );
    CHECK( fired < rolls );
}

// HLTH-B06: morale_point::add decided "same sign" by comparing the current bonus with the
// new max_bonus. With max_bonus 0 (uncapped), positive adds stacked linearly without limit,
// and a positive add to a negative point took the square-root branch and made it worse.
TEST_CASE( "hlth-b06_morale_stacking_uses_sign_of_new_bonus", "[audit][health]" )
{
    player_morale m;

    SECTION( "uncapped positive adds stack with the square-root nerf" ) {
        m.add( morale_food_good, 10, 0, 1_hours, 30_minutes );
        REQUIRE( m.has( morale_food_good ) == 10 );
        m.add( morale_food_good, 10, 0, 1_hours, 30_minutes );
        // sqrt( 10^2 + 10^2 ) = 14; the old code added them up to 20.
        CHECK( m.has( morale_food_good ) == 14 );
    }

    SECTION( "a positive add to a negative point lessens it" ) {
        m.add( morale_food_good, -10, -20, 1_hours, 30_minutes );
        REQUIRE( m.has( morale_food_good ) == -10 );
        m.add( morale_food_good, 5, 0, 1_hours, 30_minutes );
        // The old code gave -sqrt( 10^2 + 5^2 ) = -11.
        CHECK( m.has( morale_food_good ) == -5 );
    }

    SECTION( "capped positive adds are unchanged" ) {
        m.add( morale_food_good, 10, 40, 1_hours, 30_minutes );
        m.add( morale_food_good, 10, 40, 1_hours, 30_minutes );
        CHECK( m.has( morale_food_good ) == 14 );
    }
}

// HLTH-B07: becoming incorporeal dropped only the items that can be taken off, but then
// removed every item in inv_dump(), so worn NO_TAKEOFF items were destroyed.
TEST_CASE( "hlth-b07_incorporeal_keeps_worn_no_takeoff_items", "[audit][health]" )
{
    clear_map();
    clear_avatar();
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms pos = u.pos_bub();

    item shirt( itype_test_longshirt );
    shirt.set_flag( flag_NO_TAKEOFF );
    REQUIRE( u.wear_item( shirt, false ) );
    REQUIRE( u.wear_item( item( itype_test_socks ), false ) );
    REQUIRE( u.is_wearing( itype_test_longshirt ) );
    REQUIRE( u.is_wearing( itype_test_socks ) );
    REQUIRE( here.i_at( pos ).empty() );

    u.add_effect( effect_incorporeal, 1_hours );
    u.suffer();

    // The socks fall through; the shirt that can't be taken off stays on.
    CHECK( !u.is_wearing( itype_test_socks ) );
    CHECK( u.is_wearing( itype_test_longshirt ) );
    int socks_on_ground = 0;
    int shirts_on_ground = 0;
    for( const item &it : here.i_at( pos ) ) {
        if( it.typeId() == itype_test_socks ) {
            socks_on_ground++;
        } else if( it.typeId() == itype_test_longshirt ) {
            shirts_on_ground++;
        }
    }
    CHECK( socks_on_ground == 1 );
    CHECK( shirts_on_ground == 0 );

    u.remove_effect( effect_incorporeal );
    clear_avatar();
    clear_map();
}

// Hits the torso hard enough for any on-hit effect with a threshold of 0.
static void audit_bash_torso( Character &who )
{
    int damage = 0;
    int pain = 0;
    who.deal_damage_handle_type( effect_source::empty(), damage_unit( damage_bash, 50.0f ),
                                 body_part_torso, damage, pain );
}

// HLTH-B08: on-hit effect caps read the current effect from the hit part, but global
// effects live on the NULL part, so the lookup found nothing (reported as intensity 1,
// duration 0): global effects ran past max_duration and new effects got one intensity
// level less than the cap. A value already over the cap passed a negative duration to
// add_effect, which shortened the effect.
TEST_CASE( "hlth-b08_onhit_effect_caps_use_the_effect_they_add_to", "[audit][health]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    std::vector<bp_onhit_effect> &onhit =
        const_cast<body_part_type &>( body_part_torso.obj() ).effects_on_hit;
    restore_on_out_of_scope<std::vector<bp_onhit_effect>> restore_onhit( onhit );
    onhit.clear();
    bp_onhit_effect hit;
    hit.id = effect_debugged;
    hit.dtype = damage_bash;
    hit.dmg_threshold = 0;
    hit.scale_increment = 1.0f;
    hit.chance = 100;

    SECTION( "global effect respects max_intensity and max_duration" ) {
        hit.global = true;
        hit.intensity = 3;
        hit.max_intensity = 3;
        hit.duration = 10;
        hit.max_duration = 20;
        onhit.push_back( hit );

        audit_bash_torso( u );
        REQUIRE( u.has_effect( effect_debugged ) );
        // The old code counted the missing effect as intensity 1 and capped the add at 2.
        CHECK( u.get_effect( effect_debugged ).get_intensity() == 3 );
        CHECK( u.get_effect_dur( effect_debugged ) == 10_turns );

        audit_bash_torso( u );
        audit_bash_torso( u );
        // The old code compared against the torso, found nothing and reached 30.
        CHECK( u.get_effect_dur( effect_debugged ) == 20_turns );
        CHECK( u.get_effect( effect_debugged ).get_intensity() == 3 );
    }

    SECTION( "an effect already over the cap is not shortened" ) {
        hit.duration = 1;
        hit.max_duration = 15;
        onhit.push_back( hit );
        u.add_effect( effect_debugged, 25_turns, body_part_torso );
        REQUIRE( u.get_effect_dur( effect_debugged, body_part_torso ) == 25_turns );

        audit_bash_torso( u );
        // The old code added 15 - 25 = -10 turns.
        CHECK( u.get_effect_dur( effect_debugged, body_part_torso ) == 25_turns );
    }

    SECTION( "an effect already over the intensity cap is not lowered" ) {
        hit.intensity = 1;
        hit.max_intensity = 2;
        hit.duration = 5;
        onhit.push_back( hit );
        u.add_effect( effect_debugged, 10_turns, body_part_torso, false, 4 );
        REQUIRE( u.get_effect( effect_debugged, body_part_torso ).get_intensity() == 4 );

        audit_bash_torso( u );
        // At the intensity cap the hit keeps the intensity it has and still adds duration.
        CHECK( u.get_effect( effect_debugged, body_part_torso ).get_intensity() == 4 );
        CHECK( u.get_effect_dur( effect_debugged, body_part_torso ) == 15_turns );
    }
    clear_avatar();
}

static int audit_wound_pain( const Character &who, const bodypart_id &bp,
                             const wound_type_id &type )
{
    for( const wound &wd : who.get_part( bp )->get_wounds() ) {
        if( wd.type == type ) {
            return wd.get_pain();
        }
    }
    return -1;
}

// HLTH-B09: VITAMIN mod_values skipped only when the vitamin was both <= cap_min and
// >= cap_max, which never happens, so their caps did nothing. PAIN mod_values grew the
// wound only while the character's total pain was strictly between the caps, so a wound
// with cap_min above its own pain never grew. Character::is_bleeding returned true for
// every part.
TEST_CASE( "hlth-b09_wound_mod_values_respect_caps", "[audit][health]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    bodypart *arm = u.get_part( body_part_arm_l );
    arm->get_wounds().clear();

    SECTION( "pain grows up to cap_max and stops there" ) {
        arm->add_wound( wound_test_wound_capped_pain );
        REQUIRE( audit_wound_pain( u, body_part_arm_l, wound_test_wound_capped_pain ) == 2 );
        // +3 per second, cap_min 5 and cap_max 6.
        u.update_wounds( 1_turns );
        CHECK( audit_wound_pain( u, body_part_arm_l, wound_test_wound_capped_pain ) == 5 );
        u.update_wounds( 1_turns );
        CHECK( audit_wound_pain( u, body_part_arm_l, wound_test_wound_capped_pain ) == 6 );
        u.update_wounds( 1_turns );
        CHECK( audit_wound_pain( u, body_part_arm_l, wound_test_wound_capped_pain ) == 6 );
    }

    SECTION( "vitamin drain stops at cap_min" ) {
        arm->add_wound( wound_test_wound_capped_vitamin );
        u.vitamin_set( vitamin_iron, 15 );
        REQUIRE( u.vitamin_get( vitamin_iron ) == 15 );
        // -3 per second, cap_min 10.
        u.update_wounds( 1_turns );
        CHECK( u.vitamin_get( vitamin_iron ) == 12 );
        u.update_wounds( 1_turns );
        CHECK( u.vitamin_get( vitamin_iron ) == 10 );
        u.update_wounds( 1_turns );
        CHECK( u.vitamin_get( vitamin_iron ) == 10 );
    }

    SECTION( "a part without a bleeding wound is not bleeding" ) {
        arm->add_wound( wound_test_wound_capped_pain );
        CHECK_FALSE( u.is_bleeding( body_part_arm_l ) );
        CHECK_FALSE( u.is_bleeding( body_part_arm_r ) );
    }

    arm->get_wounds().clear();
    clear_avatar();
}
