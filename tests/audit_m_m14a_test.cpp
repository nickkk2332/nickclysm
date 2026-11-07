#include <initializer_list>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "anatomy.h"
#include "avatar.h"
#include "bodypart.h"
#include "bonuses.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "character_attire.h"
#include "debug.h"
#include "effect.h"
#include "flag.h"
#include "flexbuffer_json.h"
#include "item.h"
#include "json_loader.h"
#include "lightmap.h"
#include "map_helpers.h"
#include "material.h"
#include "mutation.h"
#include "player_helpers.h"
#include "ret_val.h"
#include "skill.h"
#include "subbodypart.h"
#include "type_id.h"
#include "units.h"
#include "weather.h"

static const bodypart_str_id body_part_gastropod_tongue( "gastropod_tongue" );
static const bodypart_str_id body_part_hand_slime_complete_l( "hand_slime_complete_l" );
static const bodypart_str_id body_part_hand_slime_complete_r( "hand_slime_complete_r" );
static const bodypart_str_id body_part_hand_slime_l( "hand_slime_l" );
static const bodypart_str_id body_part_hoof_l( "hoof_l" );
static const bodypart_str_id body_part_insect_hand_l( "insect_hand_l" );

static const matec_id TECH_SLIME_PUNCH( "TECH_SLIME_PUNCH" );

static const efftype_id effect_common_cold( "common_cold" );
static const efftype_id effect_flu( "flu" );
static const efftype_id effect_heating_bionic( "heating_bionic" );
static const efftype_id effect_test_m14a_hand_heat( "test_m14a_hand_heat" );
static const efftype_id effect_test_m14a_local_heat( "test_m14a_local_heat" );
static const efftype_id effect_test_m14a_warming_aura( "test_m14a_warming_aura" );

static const itype_id itype_test_m14a_animal_shirt( "test_m14a_animal_shirt" );
static const itype_id itype_test_m14a_cotton_hat( "test_m14a_cotton_hat" );
static const itype_id itype_test_m14a_cotton_shirt( "test_m14a_cotton_shirt" );
static const itype_id itype_test_m14a_horned_helmet( "test_m14a_horned_helmet" );
static const itype_id itype_test_m14a_leather_vest( "test_m14a_leather_vest" );
static const itype_id itype_test_m14a_steel_helmet( "test_m14a_steel_helmet" );
static const itype_id itype_test_m14a_wool_shirt( "test_m14a_wool_shirt" );

static const json_character_flag json_flag_FROSTBITE_VULNERABLE( "FROSTBITE_VULNERABLE" );
static const json_character_flag json_flag_LIMB_LOWER( "LIMB_LOWER" );
static const json_character_flag json_flag_LIMB_UPPER( "LIMB_UPPER" );
static const json_character_flag json_flag_NIGHT_VISION( "NIGHT_VISION" );

static const material_id material_cotton( "cotton" );
static const material_id material_wool( "wool" );
static const material_id material_wool_quilted( "wool_quilted" );

static const skill_id skill_cutting( "cutting" );
static const skill_id skill_driving( "driving" );

static const sub_bodypart_str_id sub_body_part_test_m14a_no_sub_part( "test_m14a_no_sub_part" );
static const sub_bodypart_str_id sub_body_part_test_m14a_sub_part( "test_m14a_sub_part" );
static const sub_bodypart_str_id sub_body_part_torso_upper( "torso_upper" );

static const trait_id trait_FUR( "FUR" );
static const trait_id trait_HORNS_POINTED( "HORNS_POINTED" );
static const trait_id trait_NIGHTVISION( "NIGHTVISION" );
static const trait_id trait_NIGHTVISION2( "NIGHTVISION2" );
static const trait_id trait_NIGHTVISION3( "NIGHTVISION3" );
static const trait_id trait_SLIMY( "SLIMY" );
static const trait_id trait_SMELLY( "SMELLY" );
static const trait_id trait_SQUEAMISH( "SQUEAMISH" );
static const trait_id trait_URSINE_FUR( "URSINE_FUR" );
static const trait_id trait_VEGAN( "VEGAN" );
static const trait_id trait_test_m14a_horns( "test_m14a_horns" );
static const trait_id trait_test_m14a_night_vis( "test_m14a_night_vis" );
static const trait_id trait_test_m14a_no_cotton( "test_m14a_no_cotton" );
static const trait_id trait_test_m14a_nv_flag( "test_m14a_nv_flag" );

// ---- CHAR-J02 ----

// Wool allergy, vegans and the squeamish were hardcoded in can_wear; the traits say what they
// refuse in JSON now, and an ANIMAL_PRODUCT flag on the item's type counts too.
TEST_CASE( "char-j02_traits_refuse_to_wear_what_their_json_lists", "[audit][character][armor]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    const item cotton_shirt( itype_test_m14a_cotton_shirt );

    SECTION( "vegan" ) {
        you.set_mutation( trait_VEGAN );
        const ret_val<void> leather = you.can_wear( item( itype_test_m14a_leather_vest ), true );
        CHECK_FALSE( leather.success() );
        CHECK( leather.str() == "Can't wear that, it's made from an animal!" );
        CHECK( you.can_wear( cotton_shirt, true ).success() );
        // Only an ANIMAL_PRODUCT flag set on the item itself counted before
        CHECK_FALSE( you.can_wear( item( itype_test_m14a_animal_shirt ), true ).success() );
    }
    SECTION( "squeamish" ) {
        you.set_mutation( trait_SQUEAMISH );
        CHECK( you.can_wear( cotton_shirt, true ).success() );
        item filthy_shirt( itype_test_m14a_cotton_shirt );
        filthy_shirt.set_flag( flag_FILTHY );
        const ret_val<void> filthy = you.can_wear( filthy_shirt, true );
        CHECK_FALSE( filthy.success() );
        CHECK( filthy.str() == "Can't wear that, it's filthy!" );
    }
    SECTION( "a mod's trait without a message of its own" ) {
        you.set_mutation( trait_test_m14a_no_cotton );
        const ret_val<void> refused = you.can_wear( cotton_shirt, true );
        CHECK_FALSE( refused.success() );
        CHECK_THAT( refused.str(), Catch::Matchers::Contains( "prevents you from wearing" ) );
        CHECK( you.can_wear( item( itype_test_m14a_leather_vest ), true ).success() );
    }
    clear_avatar();
}

// Pointed horns, antennae and antlers had a hardcoded "no rigid headwear" check that ran after
// the JSON one and so overrode allowed_items.  Their JSON restricts_gear does the job now.
TEST_CASE( "char-j02_horns_restrict_headwear_through_their_json", "[audit][character][armor]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    const item steel_helmet( itype_test_m14a_steel_helmet );
    const item horned_helmet( itype_test_m14a_horned_helmet );
    const item cotton_hat( itype_test_m14a_cotton_hat );
    REQUIRE( steel_helmet.is_rigid() );
    REQUIRE( cotton_hat.is_soft() );

    SECTION( "pointed horns" ) {
        you.set_mutation( trait_HORNS_POINTED );
        CHECK_FALSE( you.can_wear( steel_helmet, true ).success() );
        CHECK( you.can_wear( cotton_hat, true ).success() );
    }
    SECTION( "horns a helmet makes room for" ) {
        you.set_mutation( trait_test_m14a_horns );
        CHECK_FALSE( you.can_wear( steel_helmet, true ).success() );
        CHECK( you.can_wear( horned_helmet, true ).success() );
        CHECK( you.can_wear( cotton_hat, true ).success() );
    }
    clear_avatar();
}

// Only the material id "wool" kept its warmth when wet; materials say so in JSON now.
TEST_CASE( "char-j02_wet_wool_stays_warm", "[audit][character][temperature]" )
{
    CHECK( material_wool->wet_warmth_retention() == 1.0f );
    CHECK( material_cotton->wet_warmth_retention() == 0.0f );
    // It copies from wool but didn't keep its warmth, and still doesn't
    CHECK( material_wool_quilted->wet_warmth_retention() == 0.0f );

    clear_avatar();
    avatar &you = get_avatar();
    const bodypart_id torso = body_part_torso.id();
    const auto warmth_dry_and_wet = [&you, &torso]( const itype_id & shirt ) {
        you.clear_worn();
        you.wear_item( item( shirt ), false );
        you.set_part_wetness( torso, 0 );
        const int dry = you.worn.warmth( you )[torso];
        you.set_part_wetness( torso, you.get_part_drench_capacity( torso ) );
        const int wet = you.worn.warmth( you )[torso];
        return std::make_pair( dry, wet );
    };
    const std::pair<int, int> wool = warmth_dry_and_wet( itype_test_m14a_wool_shirt );
    CHECK( wool.first > 0 );
    CHECK( wool.second == wool.first );
    const std::pair<int, int> cotton = warmth_dry_and_wet( itype_test_m14a_cotton_shirt );
    CHECK( cotton.first > 0 );
    CHECK( cotton.second < cotton.first );
    clear_avatar();
}

// ---- CHAR-J03 ----

static float vision_threshold( avatar &you )
{
    you.recalculate_enchantment_cache();
    you.recalc_sight_limits();
    return you.get_vision_threshold( LIGHT_AMBIENT_MINIMAL );
}

// With night vision goggles or the NIGHT_VISION flag, every NIGHT_VIS enchantment was ignored.
TEST_CASE( "char-j03_night_vis_adds_to_night_vision_goggles", "[audit][character][vision]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    you.set_mutation( trait_test_m14a_nv_flag );
    REQUIRE( you.has_flag( json_flag_NIGHT_VISION ) );
    const float goggles = vision_threshold( you );

    you.set_mutation( trait_test_m14a_night_vis );
    // A lower light threshold is better night vision
    CHECK( vision_threshold( you ) < goggles );
    clear_avatar();
}

// The Night Vision traits' ranges are their JSON night_vision_range now; as before, only the
// best of them counts, and only while it is switched on.
TEST_CASE( "char-j03_night_vision_traits_keep_their_ranges", "[audit][character][vision]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    const float none = vision_threshold( you );

    you.set_mutation( trait_NIGHTVISION );
    const float low = vision_threshold( you );
    you.set_mutation( trait_NIGHTVISION2 );
    const float high = vision_threshold( you );
    you.set_mutation( trait_NIGHTVISION3 );
    const float full = vision_threshold( you );
    CHECK( low < none );
    CHECK( high < low );
    CHECK( full < high );

    clear_avatar();
    you.set_mutation( trait_NIGHTVISION3 );
    REQUIRE( you.has_active_mutation( trait_NIGHTVISION3 ) );
    CHECK( vision_threshold( you ) == Approx( full ).epsilon( 1e-6 ) );

    you.deactivate_mutation( trait_NIGHTVISION3 );
    REQUIRE_FALSE( you.has_active_mutation( trait_NIGHTVISION3 ) );
    CHECK( vision_threshold( you ) == Approx( none ).epsilon( 1e-6 ) );
    clear_avatar();
}

// ---- CHAR-J04 ----

// scaling-stat knew 4 stats and 20 skills by short names; anything else silently made the
// bonus a constant.  Any skill id works now, the old names still do, and an unknown one is
// reported.
TEST_CASE( "char-j04_scaling_stat_takes_any_skill", "[audit][martial_arts]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    you.set_skill_level( skill_cutting, 3 );
    you.set_skill_level( skill_driving, 2 );
    const auto scaled = [&you]( const std::string & json ) {
        const JsonObject jo = json_loader::from_string( json );
        return effect_scaling( jo ).get( you );
    };
    CHECK( scaled( R"({ "scaling-stat": "cutting", "scale": 2 })" ) == Approx( 6.0f ) );
    CHECK( scaled( R"({ "scaling-stat": "cut", "scale": 2 })" ) == Approx( 6.0f ) );
    // "drive" named the skill "drive", which doesn't exist; the skill is "driving"
    CHECK( scaled( R"({ "scaling-stat": "drive", "scale": 1 })" ) == Approx( 2.0f ) );
    CHECK( scaled( R"({ "scaling-stat": "str", "scale": 0.5 })" ) ==
           Approx( 0.5f * you.get_str() ) );
    CHECK( scaled( R"({ "scale": 4 })" ) == Approx( 4.0f ) );

    const JsonObject good = json_loader::from_string(
                                R"({ "flat_bonuses": [ { "stat": "hit", "scaling-stat": "cutting", "scale": 1 } ] })" );
    bonus_container good_bonuses;
    good_bonuses.load( good );
    CHECK( capture_debugmsg_during( [&good_bonuses]() {
        good_bonuses.check( "test buff" );
    } ).empty() );
    CHECK_THAT( good_bonuses.get_description(), Catch::Matchers::Contains( skill_cutting->name() ) );

    const JsonObject typo = json_loader::from_string(
                                R"({ "flat_bonuses": [ { "stat": "hit", "scaling-stat": "cuttting", "scale": 1 } ] })" );
    bonus_container typo_bonuses;
    typo_bonuses.load( typo );
    const std::string msg = capture_debugmsg_during( [&typo_bonuses]() {
        typo_bonuses.check( "test buff" );
    } );
    CHECK_THAT( msg, Catch::Matchers::Contains( "cuttting" ) );
    // And it adds nothing, instead of the bare scale
    CHECK( typo_bonuses.get_flat( you, affected_stat::HIT ) == Approx( 0.0f ) );
    clear_avatar();
}

// ---- CHAR-J05 ----

// anatomy.parts was mandatory, so "extend" and "delete" didn't work on it.
TEST_CASE( "char-j05_anatomy_parts_can_be_extended", "[audit][bodypart]" )
{
    anatomy plan;
    plan.load( json_loader::from_string(
                   R"({ "id": "test_m14a_anatomy", "parts": [ "torso", "head" ] })" ),
               "test" );
    plan.was_loaded = true;
    plan.load( json_loader::from_string(
                   R"({ "id": "test_m14a_anatomy", "extend": { "parts": [ "arm_l" ] }, "delete": { "parts": [ "head" ] } })" ),
               "test" );
    plan.finalize();
    const std::vector<bodypart_id> parts = plan.get_bodyparts();
    CHECK( parts == std::vector<bodypart_id> { body_part_torso.id(), body_part_arm_l.id() } );

    // Still required the first time
    anatomy no_parts;
    CHECK_THROWS( no_parts.load( json_loader::from_string( R"({ "id": "test_m14a_anatomy" })" ),
                                 "test" ) );

    anatomy twice;
    twice.load( json_loader::from_string(
                    R"({ "id": "test_m14a_anatomy", "parts": [ "head", "head" ] })" ),
                "test" );
    twice.finalize();
    CHECK_THAT( capture_debugmsg_during( [&twice]() {
        twice.check();
    } ), Catch::Matchers::Contains( "more than once" ) );
}

// Sub body parts had no check at all.
TEST_CASE( "char-j05_sub_body_parts_are_checked", "[audit][bodypart]" )
{
    sub_body_part_type sbp;
    sbp.id = sub_body_part_test_m14a_sub_part;
    sbp.parent = body_part_torso;
    sbp.opposite = sub_body_part_torso_upper;
    sbp.locations_under = { sub_body_part_torso_upper };
    CHECK( capture_debugmsg_during( [&sbp]() {
        sbp.check();
    } ).empty() );

    sbp.similar_bodypart = sub_body_part_test_m14a_no_sub_part;
    CHECK_THAT( capture_debugmsg_during( [&sbp]() {
        sbp.check();
    } ), Catch::Matchers::Contains( "invalid similar_bodypart test_m14a_no_sub_part" ) );
}

// ---- CHAR-J06 ----

// The flu's fever, the common cold's chill and the heat of bionics were hardcoded by effect id.
TEST_CASE( "char-j06_temperature_effects_are_json", "[audit][character][temperature]" )
{
    const std::vector<effect_body_temperature> &flu = effect_flu->get_body_temperature();
    REQUIRE( flu.size() == 1 );
    REQUIRE( flu[0].body_part.has_value() );
    CHECK( *flu[0].body_part == body_part_head );
    CHECK( units::to_celsius_delta( flu[0].add ) == 3.0f );

    const std::vector<effect_body_temperature> &cold = effect_common_cold->get_body_temperature();
    REQUIRE( cold.size() == 1 );
    CHECK_FALSE( cold[0].body_part.has_value() );
    CHECK( units::to_celsius_delta( cold[0].add ) == -1.5f );

    CHECK( effect_heating_bionic->get_warmth_per_intensity() == 1 );
    CHECK( units::to_celsius_delta( effect_test_m14a_local_heat->get_body_temperature().at(
                                        0 ).add ) == 10.0f );
}

TEST_CASE( "char-j06_effects_warm_like_clothing", "[audit][character][temperature]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    const bodypart_id torso = body_part_torso.id();
    const bodypart_id head = body_part_head.id();

    std::map<bodypart_id, int> base = you.worn.warmth( you );

    you.add_effect( effect_heating_bionic, 1_hours, torso, false, 7 );
    std::map<bodypart_id, int> warmth = you.worn.warmth( you );
    CHECK( warmth[torso] == base[torso] + 7 );
    CHECK( warmth[head] == base[head] );

    // A whole-body effect warms every part
    you.add_effect( effect_test_m14a_warming_aura, 1_hours, false, 2 );
    warmth = you.worn.warmth( you );
    CHECK( warmth[torso] == base[torso] + 13 );
    CHECK( warmth[head] == base[head] + 6 );
    clear_avatar();
}

TEST_CASE( "char-j06_effects_shift_body_temperature", "[audit][character][temperature]" )
{
    clear_map();
    clear_avatar();
    avatar &you = get_avatar();
    weather_manager &weather = get_weather();
    on_out_of_scope clear_cache( [&weather]() {
        weather.clear_temp_cache();
    } );
    restore_on_out_of_scope restore_temperature( weather.temperature );
    weather.temperature = units::from_celsius( 20 );
    weather.clear_temp_cache();

    const bodypart_id hand_l = body_part_hand_l.id();
    const bodypart_id hand_r = body_part_hand_r.id();
    const auto one_update = [&you]() {
        you.set_all_parts_temp_cur( BODYTEMP_NORM );
        you.set_all_parts_temp_conv( BODYTEMP_NORM );
        you.update_bodytemp();
    };
    you.clear_effects();
    one_update();
    const float plain_left = units::to_kelvin( you.get_part_temp_conv( hand_l ) );
    const float plain_right = units::to_kelvin( you.get_part_temp_conv( hand_r ) );

    you.clear_effects();
    // Warms the left hand, wherever it is
    you.add_effect( effect_test_m14a_hand_heat, 1_hours );
    // Warms the part it is on
    you.add_effect( effect_test_m14a_local_heat, 1_hours, hand_r );
    one_update();
    CHECK( units::to_kelvin( you.get_part_temp_conv( hand_l ) ) > plain_left );
    CHECK( units::to_kelvin( you.get_part_temp_conv( hand_r ) ) > plain_right );
    you.clear_effects();
    clear_avatar();
}

// Frostbite was limited to five hardcoded part ids, fur and slime drying to nine trait ids.
TEST_CASE( "char-j06_frostbite_parts_and_drying_traits_are_json",
           "[audit][character][temperature]" )
{
    for( const bodypart_str_id &bp : {
             body_part_mouth, body_part_hand_l, body_part_hand_r, body_part_foot_l, body_part_foot_r
         } ) {
        CAPTURE( bp.str() );
        CHECK( bp->has_flag( json_flag_FROSTBITE_VULNERABLE ) );
    }
    // Mutant parts made from them were immune, and still are, with the flags they had
    for( const bodypart_str_id &bp : {
             body_part_hoof_l, body_part_insect_hand_l, body_part_hand_slime_l, body_part_gastropod_tongue
         } ) {
        CAPTURE( bp.str() );
        CHECK_FALSE( bp->has_flag( json_flag_FROSTBITE_VULNERABLE ) );
    }
    CHECK( body_part_hoof_l->has_flag( json_flag_LIMB_LOWER ) );
    CHECK( body_part_gastropod_tongue->has_flag( json_flag_LIMB_UPPER ) );
    CHECK_FALSE( body_part_torso->has_flag( json_flag_FROSTBITE_VULNERABLE ) );

    CHECK( trait_FUR->drying_time_mult == 2.0f );
    CHECK( trait_URSINE_FUR->drying_time_mult == 4.0f );
    CHECK( trait_SLIMY->drying_time_mult == 4.0f );
    CHECK( trait_SMELLY->drying_time_mult == 1.0f );
}

// The fully slimed hands named TECH_SLIME_COMPLETE_PUNCH, which nothing defines: a full slime
// fighting unarmed got an error.  They use the slime punch.  (Body part techniques aren't checked
// at load: vanilla's wing_l names tech_wing_slap, which only Magiclysm and Limb_WIP define.)
TEST_CASE( "char-j05_slime_hands_use_a_defined_technique", "[audit][character]" )
{
    for( const bodypart_str_id &hand : {
             body_part_hand_slime_complete_l, body_part_hand_slime_complete_r
         } ) {
        CAPTURE( hand.str() );
        CHECK( hand->techniques == std::set<matec_id> { TECH_SLIME_PUNCH } );
        CHECK( TECH_SLIME_PUNCH.is_valid() );
    }
}
