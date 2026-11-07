#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "addiction.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "coordinates.h"
#include "creature.h"
#include "dialogue.h"
#include "effect.h"
#include "effect_on_condition.h"
#include "enums.h"
#include "magic_enchantment.h"
#include "map.h"
#include "map_helpers.h"
#include "math_parser.h"
#include "math_parser_diag_value.h"
#include "messages.h"
#include "mutation.h"
#include "player_helpers.h"
#include "rng.h"
#include "sleep.h"
#include "talker.h"
#include "translation.h"
#include "type_id.h"
#include "uistate.h"

static const addiction_id addiction_opiate( "opiate" );
static const addiction_id addiction_test_audit_m15b_short( "test_audit_m15b_short" );

static const effect_on_condition_id
effect_on_condition_EOC_TRAIT_MOODSWINGS( "EOC_TRAIT_MOODSWINGS" );
static const effect_on_condition_id
effect_on_condition_EOC_TRAIT_NARCOLEPTIC( "EOC_TRAIT_NARCOLEPTIC" );
static const effect_on_condition_id effect_on_condition_EOC_TRAIT_SHOUT1( "EOC_TRAIT_SHOUT1" );
static const effect_on_condition_id effect_on_condition_EOC_TRAIT_SHOUT2( "EOC_TRAIT_SHOUT2" );
static const effect_on_condition_id effect_on_condition_EOC_TRAIT_SHOUT3( "EOC_TRAIT_SHOUT3" );
static const effect_on_condition_id
effect_on_condition_EOC_test_audit_m15b_awake( "EOC_test_audit_m15b_awake" );
static const effect_on_condition_id
effect_on_condition_EOC_test_audit_m15b_shout( "EOC_test_audit_m15b_shout" );

static const efftype_id effect_brainworms( "brainworms" );
static const efftype_id effect_lying_down( "lying_down" );
static const efftype_id effect_pkill1_acetaminophen( "pkill1_acetaminophen" );
static const efftype_id effect_pkill2( "pkill2" );
static const efftype_id effect_sleep( "sleep" );
static const efftype_id effect_tapeworm( "tapeworm" );
static const efftype_id effect_test_audit_m15b_chatty( "test_audit_m15b_chatty" );
static const efftype_id effect_test_audit_m15b_comfy( "test_audit_m15b_comfy" );
static const efftype_id effect_test_audit_m15b_pkill( "test_audit_m15b_pkill" );
static const efftype_id effect_tetanus( "tetanus" );

static const ter_str_id ter_t_floor( "t_floor" );
static const ter_str_id ter_t_water_dp( "t_water_dp" );

static const trait_id trait_ADDICTIVE( "ADDICTIVE" );
static const trait_id trait_HEAVYSLEEPER( "HEAVYSLEEPER" );
static const trait_id trait_HEAVYSLEEPER2( "HEAVYSLEEPER2" );
static const trait_id trait_HIBERNATE( "HIBERNATE" );
static const trait_id trait_MOODSWINGS( "MOODSWINGS" );
static const trait_id trait_NARCOLEPTIC( "NARCOLEPTIC" );
static const trait_id trait_NONADDICTIVE( "NONADDICTIVE" );
static const trait_id trait_SHOUT1( "SHOUT1" );
static const trait_id trait_SHOUT2( "SHOUT2" );
static const trait_id trait_SHOUT3( "SHOUT3" );
static const trait_id trait_TEST_AUDIT_M15B_COMFORT( "TEST_AUDIT_M15B_COMFORT" );
static const trait_id trait_WATERSLEEP( "WATERSLEEP" );

namespace
{

// Calls of addiction::recover until the intensity changes or the addiction is over.
struct recovery_step {
    int turns = 0;
    bool removed = false;
};

recovery_step recover_until_change( addiction &add, const Character &who )
{
    recovery_step step;
    const int start = add.intensity;
    while( step.turns < 100000 ) {
        step.turns++;
        if( add.recover( who ) ) {
            step.removed = true;
            break;
        }
        if( add.intensity != start ) {
            break;
        }
    }
    return step;
}

bool has_message_containing( const std::string &text )
{
    for( const std::pair<std::string, std::string> &msg :
         Messages::recent_messages( Messages::size() ) ) {
        if( msg.second.find( text ) != std::string::npos ) {
            return true;
        }
    }
    return false;
}

double wake_pain_threshold( const Character &who )
{
    return who.enchantment_cache->modify_value( enchant_vals::mod::WAKE_PAIN_THRESHOLD, 0.0 );
}

// The sleepiness after one needs update while asleep, from the same start and the same random
// draws whatever the trait and the terrain.
int sleepiness_after_needs( const bool watersleep, const ter_str_id &ter )
{
    clear_avatar();
    avatar &u = get_avatar();
    map &here = get_map();
    here.ter_set( u.pos_bub( here ), ter );
    if( watersleep ) {
        u.set_mutation( trait_WATERSLEEP );
    }
    u.set_sleepiness( 500 );
    // Deferred, so that the sleep effect itself doesn't run before the needs update.
    u.add_effect( effect_sleep, 2_hours, false, 0, false, true );
    rng_set_engine_seed( 4242 );
    u.update_needs( 1 );
    return u.get_sleepiness();
}

} // namespace

// HLTH-J11: an addiction's lifecycle (how long a dose sates it, how long it takes to lose a
// level, the intensity cap and when it goes away) comes from its addiction_type.
TEST_CASE( "hlth-j11_addiction_lifecycle_comes_from_the_addiction_type", "[audit][health]" )
{
    const add_type &vanilla = addiction_opiate.obj();
    CHECK( vanilla.get_dose_satiation() == 2_hours );
    CHECK( vanilla.get_recovery_time() == 6_hours );
    CHECK( vanilla.get_intensity_decay() == 10_minutes );
    CHECK( vanilla.get_max_intensity() == MAX_ADDICTION_LEVEL );
    CHECK( vanilla.get_remove_below() == MIN_ADDICTION_LEVEL );

    const add_type &custom = addiction_test_audit_m15b_short.obj();
    CHECK( custom.get_dose_satiation() == 30_minutes );
    CHECK( custom.get_recovery_time() == 1_hours );
    CHECK( custom.get_intensity_decay() == 1_minutes );
    CHECK( custom.get_max_intensity() == 5 );
    CHECK( custom.get_remove_below() == 2 );

    clear_avatar();
    avatar &u = get_avatar();
    u.addictions.clear();

    SECTION( "a dose sates for the type's dose_satiation" ) {
        // A strength over 100 always addicts.
        u.add_addiction( addiction_test_audit_m15b_short, 1000 );
        REQUIRE( u.addictions.size() == 1 );
        addiction &add = u.addictions.front();
        REQUIRE( add.intensity == 1 );

        add.sated = -1_turns;
        u.add_addiction( addiction_test_audit_m15b_short, 0 );
        CHECK( add.sated == 30_minutes );
        CHECK( add.intensity == 1 );

        add.sated = 5_minutes;
        u.add_addiction( addiction_test_audit_m15b_short, 0 );
        CHECK( add.sated == 35_minutes );

        u.add_addiction( addiction_test_audit_m15b_short, 0 );
        CHECK( add.sated == 50_minutes );
    }

    SECTION( "doses can't raise the intensity past max_intensity" ) {
        u.addictions.emplace_back( addiction_test_audit_m15b_short, 4 );
        u.add_addiction( addiction_test_audit_m15b_short, 100000 );
        CHECK( u.addictions.front().intensity == 5 );
        u.add_addiction( addiction_test_audit_m15b_short, 100000 );
        CHECK( u.addictions.front().intensity == 5 );
    }

    SECTION( "math sets the intensity, capped at max_intensity" ) {
        const auto set_intensity = [&u]( const std::string & value ) {
            dialogue d( get_talker_for( u ), nullptr );
            const std::string expr = "u_addiction_intensity('test_audit_m15b_short') = " + value;
            math_exp exp;
            REQUIRE( exp.parse( expr ) );
            exp.eval( d );
        };
        set_intensity( "3" );
        REQUIRE( u.addictions.size() == 1 );
        CHECK( u.addictions.front().intensity == 3 );
        set_intensity( "50" );
        CHECK( u.addictions.front().intensity == 5 );
        set_intensity( "0" );
        CHECK( u.addictions.empty() );
    }

    SECTION( "recovery_time, intensity_decay and remove_below set the recovery" ) {
        addiction add( addiction_test_audit_m15b_short, 3 );
        add.sated = 0_turns;
        // A level is lost once the satiation is a recovery_time (1 h) less one intensity_decay
        // (1 m) per level below 0.
        recovery_step step = recover_until_change( add, u );
        CHECK( step.turns == to_turns<int>( 57_minutes ) + 1 );
        CHECK_FALSE( step.removed );
        CHECK( add.intensity == 2 );

        step = recover_until_change( add, u );
        CHECK( step.turns == to_turns<int>( 58_minutes ) + 1 );
        CHECK_FALSE( step.removed );
        CHECK( add.intensity == 1 );

        // Below remove_below (2) the end of the recovery ends the addiction.
        step = recover_until_change( add, u );
        CHECK( step.turns == to_turns<int>( 59_minutes ) + 1 );
        CHECK( step.removed );
    }

    SECTION( "vanilla addictions keep the old lifecycle" ) {
        addiction add( addiction_opiate, 3 );
        add.sated = 0_turns;
        const recovery_step step = recover_until_change( add, u );
        CHECK( step.turns == to_turns<int>( 6_hours - 30_minutes ) + 1 );
        CHECK( add.intensity == 2 );
    }
    u.addictions.clear();
}

// HLTH-J11: Addictive Personality and Addiction Resistant are enchantment values now, with the
// numbers the trait checks had.
TEST_CASE( "hlth-j11_addictive_traits_are_enchantment_values_with_the_old_numbers",
           "[audit][health]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    const add_type &opiate = addiction_opiate.obj();

    CHECK( opiate.dose_satiation_for( u ) == 2_hours );
    CHECK( opiate.recovery_time_for( u ) == 6_hours );
    CHECK( addiction_strength_for( u, 15 ) == 15 );

    u.set_mutation( trait_ADDICTIVE );
    u.recalculate_enchantment_cache();
    CHECK( opiate.dose_satiation_for( u ) == 1_hours );
    CHECK( opiate.recovery_time_for( u ) == 10_hours );
    CHECK( addiction_strength_for( u, 15 ) == 30 );
    u.unset_mutation( trait_ADDICTIVE );

    u.set_mutation( trait_NONADDICTIVE );
    u.recalculate_enchantment_cache();
    CHECK( opiate.dose_satiation_for( u ) == 6_hours );
    CHECK( opiate.recovery_time_for( u ) == 3_hours );
    // strength / 2, as the C++ had it
    CHECK( addiction_strength_for( u, 15 ) == 7 );
    u.unset_mutation( trait_NONADDICTIVE );
    u.recalculate_enchantment_cache();

    CHECK( opiate.dose_satiation_for( u ) == 2_hours );
}

// HLTH-J11: the addiction that weakens a painkiller is named in its effect type.
TEST_CASE( "hlth-j11_painkiller_tolerance_names_its_addiction", "[audit][health]" )
{
    clear_avatar();
    avatar &u = get_avatar();

    u.add_effect( effect_test_audit_m15b_pkill, 1_hours );
    REQUIRE( u.has_effect( effect_test_audit_m15b_pkill ) );
    CHECK( u.get_effect( effect_test_audit_m15b_pkill ).get_pkill_addiction() ==
           addiction_test_audit_m15b_short );
    CHECK( u.get_effect( effect_test_audit_m15b_pkill ).get_addict_mod( "PKILL", 3 ) ==
           Approx( 1.0 / 12.0 ) );

    // true still means opiate, at 2 per level
    u.add_effect( effect_pkill2, 1_hours );
    REQUIRE( u.has_effect( effect_pkill2 ) );
    CHECK( u.get_effect( effect_pkill2 ).get_pkill_addiction() == addiction_opiate );
    CHECK( u.get_effect( effect_pkill2 ).get_addict_mod( "PKILL", 4 ) == Approx( 1.0 / 8.0 ) );

    u.add_effect( effect_pkill1_acetaminophen, 1_minutes );
    REQUIRE( u.has_effect( effect_pkill1_acetaminophen ) );
    CHECK( u.get_effect( effect_pkill1_acetaminophen ).get_pkill_addiction().is_null() );
    CHECK( u.get_effect( effect_pkill1_acetaminophen ).get_addict_mod( "PKILL", 4 ) == 1.0 );
    clear_avatar();
}

// HLTH-J12: heavy sleepers are enchantment values now, with the numbers the trait checks had.
TEST_CASE( "hlth-j12_heavy_sleepers_are_enchantment_values_with_the_old_numbers",
           "[audit][health]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    CHECK( u.wake_dice() == 2 );
    CHECK( u.sleep_light_tolerance() == Approx( 1.0 ) );
    CHECK( wake_pain_threshold( u ) == Approx( 0.0 ) );

    u.set_mutation( trait_HEAVYSLEEPER );
    u.recalculate_enchantment_cache();
    CHECK( u.wake_dice() == 3 );
    CHECK( u.sleep_light_tolerance() == Approx( 1.0 ) );
    CHECK( wake_pain_threshold( u ) == Approx( 2.0 ) );
    u.unset_mutation( trait_HEAVYSLEEPER );

    u.set_mutation( trait_HEAVYSLEEPER2 );
    u.recalculate_enchantment_cache();
    CHECK( u.wake_dice() == 6 );
    CHECK( u.sleep_light_tolerance() == Approx( 1.25 ) );
    CHECK( wake_pain_threshold( u ) == Approx( 5.0 ) );

    // Hibernation needs Very Heavy Sleeper; the two make the old 5.
    u.set_mutation( trait_HIBERNATE );
    u.recalculate_enchantment_cache();
    CHECK( u.wake_dice() == 6 );
    CHECK( u.sleep_light_tolerance() == Approx( 5.0 ) );
    u.unset_mutation( trait_HIBERNATE );
    u.unset_mutation( trait_HEAVYSLEEPER2 );
    clear_avatar();
}

// HLTH-J12: a sudden pain wakes a Very Heavy Sleeper only at 3 to 5 plus 5.
TEST_CASE( "hlth-j12_very_heavy_sleepers_sleep_through_more_pain", "[audit][health]" )
{
    restore_on_out_of_scope restore_distraction( uistate.distraction_pain );
    uistate.distraction_pain = false;
    clear_avatar();
    avatar &u = get_avatar();
    u.set_mutation( trait_HEAVYSLEEPER2 );
    u.recalculate_enchantment_cache();
    u.add_effect( effect_sleep, 1_hours, false, 0, false, true );
    REQUIRE( u.get_effect( effect_sleep ).get_duration() > 0_turns );

    u.react_to_felt_pain( 7 );
    CHECK( u.get_effect( effect_sleep ).get_duration() > 0_turns );
    u.react_to_felt_pain( 10 );
    CHECK( u.get_effect( effect_sleep ).get_duration() == 0_turns );
    clear_avatar();
}

// HLTH-J12: Aqueous Repose took 3 sleepiness off twice (the sleep effect and the needs update)
// and wherever its sleeper lay; it does it once, in the needs update, and only in water.
TEST_CASE( "hlth-j12_aqueous_repose_works_once_and_only_in_water", "[audit][health]" )
{
    restore_on_out_of_scope restore_turn( calendar::turn );
    clear_map();
    // On a 10 minute mark
    calendar::turn = calendar::turn_zero + 1_days;

    const int dry_plain = sleepiness_after_needs( false, ter_t_floor );
    const int dry_trait = sleepiness_after_needs( true, ter_t_floor );
    CHECK( dry_trait == dry_plain );

    const int wet_plain = sleepiness_after_needs( false, ter_t_water_dp );
    const int wet_trait = sleepiness_after_needs( true, ter_t_water_dp );
    CHECK( wet_trait == wet_plain - 3 );

    clear_avatar();
    clear_map();
}

// HLTH-J12: a comfort condition can be any dialogue condition.
TEST_CASE( "hlth-j12_comfort_condition_reads_a_dialogue_condition", "[audit][health]" )
{
    clear_avatar();
    clear_map();
    avatar &u = get_avatar();
    u.set_mutation( trait_TEST_AUDIT_M15B_COMFORT );
    const tripoint_bub_ms pos = u.pos_bub();

    CHECK( u.get_comfort_data_for( pos ).human_or_impossible() );

    u.add_effect( effect_test_audit_m15b_comfy, 1_hours );
    REQUIRE( u.has_effect( effect_test_audit_m15b_comfy ) );
    const comfort_data &comfy = u.get_comfort_data_for( pos );
    CHECK_FALSE( comfy.human_or_impossible() );
    // a copy: CHECK takes a reference, and the constant has no definition to bind it to
    CHECK( comfy.base_comfort == static_cast<int>( comfort_data::COMFORT_VERY_COMFORTABLE ) );
    clear_avatar();
}

// HLTH-J13: an effect's flavour messages are JSON (periodic_messages), and the parasites and
// tetanus have theirs there now.
TEST_CASE( "hlth-j13_periodic_messages_come_from_the_effect_json", "[audit][health]" )
{
    clear_avatar();
    avatar &u = get_avatar();

    const auto check_message = [&u]( const efftype_id & id, const std::string & text, int chance ) {
        CAPTURE( id.str() );
        u.add_effect( id, 1_hours );
        REQUIRE( u.has_effect( id ) );
        const std::vector<effect_periodic_message> &msgs = u.get_effect( id ).get_periodic_msgs();
        REQUIRE( msgs.size() == 1 );
        CHECK( msgs.front().text.translated() == text );
        CHECK( msgs.front().chance == chance );
        CHECK( msgs.front().type == m_bad );
        u.remove_effect( id );
    };
    check_message( effect_tapeworm, "Your bowels ache.", 3072 );
    check_message( effect_brainworms, "Your head aches faintly.", 1536 );
    check_message( effect_tetanus, "Your muscles are tight and sore.", 1536 );

    clear_avatar();
    Messages::clear_messages();
    u.add_effect( effect_test_audit_m15b_chatty, 1_hours );
    REQUIRE( u.has_effect( effect_test_audit_m15b_chatty ) );
    u.process_effects();
    CHECK( has_message_containing( "Test periodic message that always shows." ) );
    CHECK_FALSE( has_message_containing( "Test periodic message for intensity 2 and up." ) );

    Messages::clear_messages();
    u.get_effect( effect_test_audit_m15b_chatty ).set_intensity( 2 );
    u.process_effects();
    CHECK( has_message_containing( "Test periodic message that always shows." ) );
    CHECK( has_message_containing( "Test periodic message for intensity 2 and up." ) );
    Messages::clear_messages();
    clear_avatar();
}

// HLTH-J14: Narcoleptic, Mood Swings and the Shouter traits run their side effects as
// processed_eocs, which need u_in_sleep_state and u_shout.
TEST_CASE( "hlth-j14_trait_side_effects_are_processed_eocs", "[audit][health]" )
{
    const std::vector<std::pair<trait_id, effect_on_condition_id>> moved = {
        { trait_NARCOLEPTIC, effect_on_condition_EOC_TRAIT_NARCOLEPTIC },
        { trait_MOODSWINGS, effect_on_condition_EOC_TRAIT_MOODSWINGS },
        { trait_SHOUT1, effect_on_condition_EOC_TRAIT_SHOUT1 },
        { trait_SHOUT2, effect_on_condition_EOC_TRAIT_SHOUT2 },
        { trait_SHOUT3, effect_on_condition_EOC_TRAIT_SHOUT3 },
    };
    for( const std::pair<trait_id, effect_on_condition_id> &entry : moved ) {
        CAPTURE( entry.first.str() );
        const mutation_branch &mut = entry.first.obj();
        // processed_eocs of an activated trait run only while it's on
        CHECK_FALSE( mut.activated );
        CHECK( std::find( mut.processed_eocs.begin(), mut.processed_eocs.end(),
                          entry.second ) != mut.processed_eocs.end() );
    }

    clear_avatar();
    clear_map();
    avatar &u = get_avatar();
    const auto awake = [&u]() {
        dialogue d( get_talker_for( u ), nullptr );
        effect_on_condition_EOC_test_audit_m15b_awake->activate( d );
        const diag_value *value = u.maybe_get_value( "audit_m15b_awake" );
        REQUIRE( value != nullptr );
        return *value == 1.0;
    };
    CHECK( awake() );
    u.add_effect( effect_lying_down, 1_hours, false, 0, false, true );
    CHECK_FALSE( awake() );
    u.remove_effect( effect_lying_down );
    u.add_effect( effect_sleep, 1_hours, false, 0, false, true );
    CHECK_FALSE( awake() );
    u.remove_effect( effect_sleep );
    CHECK( awake() );

    dialogue d( get_talker_for( u ), nullptr );
    effect_on_condition_EOC_test_audit_m15b_shout->activate( d );
    const diag_value *shouted = u.maybe_get_value( "audit_m15b_shouted" );
    REQUIRE( shouted != nullptr );
    CHECK( *shouted == 1.0 );
    clear_avatar();
}
