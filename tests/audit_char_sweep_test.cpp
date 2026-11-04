#include <algorithm>
#include <map>
#include <set>
#include <vector>

#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "coordinates.h"
#include "effect.h"
#include "grab.h"
#include "item.h"
#include "map.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "point.h"
#include "skill.h"
#include "subbodypart.h"
#include "trap.h"
#include "type_id.h"
#include "units.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vpart_position.h"
#include "vpart_range.h"

// ---- CHAR-B05 ----

static const efftype_id effect_downed( "downed" );
static const efftype_id effect_in_pit( "in_pit" );
static const efftype_id effect_webbed( "webbed" );

static const itype_id itype_rock( "rock" );

static const trait_id trait_test_char_b05_no_carry( "test_char_b05_no_carry" );
static const trait_id trait_test_char_b05_wing_left( "test_char_b05_wing_left" );
static const trait_id trait_test_char_b05_wing_right( "test_char_b05_wing_right" );

static void give_wings( avatar &you )
{
    you.set_mutation( trait_test_char_b05_wing_left );
    you.set_mutation( trait_test_char_b05_wing_right );
}

// can_fly() began with move_effects(), so asking whether a character could fly rolled their
// escapes from being downed, webbed, snared, crushed, in a pit or grabbed, and freed them when
// a roll succeeded.
TEST_CASE( "char-b05_asking_whether_one_can_fly_frees_no_one", "[audit][character]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    on_out_of_scope restore( []() {
        clear_avatar();
    } );
    give_wings( you );
    REQUIRE( you.can_fly() );

    for( const efftype_id &held : {
             effect_downed, effect_webbed, effect_in_pit
         } ) {
        CAPTURE( held.str() );
        you.add_effect( held, 1_hours, true );
        int flew = 0;
        for( int i = 0; i < 100; ++i ) {
            if( you.can_fly() ) {
                ++flew;
            }
        }
        CHECK( flew == 0 );
        CHECK( you.has_effect( held ) );
        you.remove_effect( held );
    }
    CHECK( you.can_fly() );
}

// The pit traps put the character in the pit before asking can_fly(), which rolled the pit
// escape: about one fall in five ended out of the pit at once, and wings slowed a fall only
// when that roll succeeded.
TEST_CASE( "char-b05_falling_into_a_pit_rolls_no_escape", "[audit][character]" )
{
    clear_avatar();
    clear_map();
    avatar &you = get_avatar();
    on_out_of_scope restore( []() {
        clear_avatar();
    } );
    const tripoint_bub_ms pos = you.pos_bub();

    SECTION( "without wings, you stay in the pit" ) {
        int climbed_out = 0;
        for( int i = 0; i < 20; ++i ) {
            trapfunc::pit( pos, &you, nullptr );
            if( !you.has_effect( effect_in_pit ) ) {
                ++climbed_out;
            }
            you.remove_effect( effect_in_pit );
            you.set_all_parts_hp_to_max();
        }
        CHECK( climbed_out == 0 );
    }
    SECTION( "wings always slow the fall" ) {
        give_wings( you );
        int hurt = 0;
        for( int i = 0; i < 20; ++i ) {
            const int hp_before = you.get_hp();
            trapfunc::pit( pos, &you, nullptr );
            if( you.get_hp() < hp_before ) {
                ++hurt;
            }
            CHECK( you.has_effect( effect_in_pit ) );
            you.remove_effect( effect_in_pit );
            you.set_all_parts_hp_to_max();
        }
        CHECK( hurt == 0 );
    }
}

// can_fly() worked out the load as 100 * weight_carried() / weight_capacity(), which divided
// by zero for a winged character whose carrying capacity was 0.
TEST_CASE( "char-b05_can_fly_with_no_carrying_capacity", "[audit][character]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    on_out_of_scope restore( []() {
        clear_avatar();
    } );
    give_wings( you );
    you.set_mutation( trait_test_char_b05_no_carry );
    REQUIRE( you.weight_capacity() == 0_gram );

    CHECK( you.can_fly() );
    item rock( itype_rock );
    you.wield( rock );
    REQUIRE( you.weight_carried() > 0_gram );
    CHECK_FALSE( you.can_fly() );
}

// ---- CHAR-B06 ----

static const skill_id skill_dodge( "dodge" );

// The fall damage multiplier dropped 4% for each point of DEX / 2 + dodge (times the limbs'
// landing score) and was clamped at 0, which a character reached at 25 points (DEX 18 and
// dodge 8 with Feline Eyes, or DEX 30 and dodge 10 where a mod raises the DEX cap): falls from
// any height did nothing.  Dexterity and dodge now cut the damage to 10% at most.
TEST_CASE( "char-b06_no_dexterity_or_dodge_makes_falls_harmless", "[audit][character]" )
{
    clear_avatar();
    clear_map();
    avatar &you = get_avatar();
    on_out_of_scope restore( []() {
        clear_avatar();
    } );

    SECTION( "an ordinary survivor is unchanged" ) {
        REQUIRE( you.get_dex() == 8 );
        REQUIRE( static_cast<int>( you.get_skill_level( skill_dodge ) ) == 0 );
        CHECK( you.fall_damage_mod() == Approx( 0.84f ) );
    }
    SECTION( "an acrobat still gets hurt by a long fall" ) {
        restore_on_out_of_scope restore_max_dex( character_max_dex );
        character_max_dex = 30;
        you.set_dex_base( 30 );
        you.set_skill_level( skill_dodge, 10 );
        REQUIRE( you.get_dex() == 30 );
        REQUIRE( static_cast<int>( you.get_skill_level( skill_dodge ) ) == 10 );
        CHECK( you.fall_damage_mod() == Approx( 0.1f ) );
        // One story still makes a perfect landing, three don't.
        CHECK( you.impact( 30, you.pos_bub() ) == 0 );
        CHECK( you.impact( 90, you.pos_bub() ) > 0 );
    }
}

// ---- CHAR-B07 ----

// The lists of similar (sub) body parts were static maps that finalize() appended to and that
// reset() never cleared, so each data reload repeated their entries, and a modded world's parts
// stayed listed after loading a world without the mod.
TEST_CASE( "char-b07_similar_sub_body_part_lists_are_rebuilt_not_appended", "[audit][bodypart]" )
{
    std::map<sub_bodypart_str_id, std::vector<sub_bodypart_str_id>> before;
    for( const body_part_type &bp : body_part_type::get_all() ) {
        for( const sub_bodypart_str_id &sbp : bp.sub_parts ) {
            before[sbp] = sbp->get_all_combined_similar_sub_bodyparts();
        }
    }
    REQUIRE( std::any_of( before.begin(), before.end(),
    []( const std::pair<const sub_bodypart_str_id, std::vector<sub_bodypart_str_id>> &entry ) {
        return !entry.second.empty();
    } ) );

    // A reload finalizes them again.
    sub_body_part_type::finalize_all();

    for( const std::pair<const sub_bodypart_str_id, std::vector<sub_bodypart_str_id>> &entry :
         before ) {
        CAPTURE( entry.first.str() );
        const std::vector<sub_bodypart_str_id> after =
            entry.first->get_all_combined_similar_sub_bodyparts();
        CHECK( after == entry.second );
        CHECK( std::set<sub_bodypart_str_id>( after.begin(), after.end() ).size() == after.size() );
    }
}

// ---- CHAR-B08 ----

static const efftype_id effect_test_char_b08_impeding( "test_char_b08_impeding" );

// try_remove_impeding_effect() took the difficulty from the whole-body instance of the effect,
// which a limb's effect doesn't have, so every escape from one succeeded; and a success removed
// the effect from every body part while the loop still walked them.
TEST_CASE( "char-b08_escaping_an_impeding_effect_on_a_limb", "[audit][character][effect]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    on_out_of_scope restore( []() {
        clear_avatar();
    } );
    you.set_str_base( 10 );
    // Difficulty 6 per intensity: an intensity 1 hold always breaks, an intensity 1000 one
    // practically never does.
    you.add_effect( effect_test_char_b08_impeding, 1_hours, body_part_arm_l, true, 1 );
    you.add_effect( effect_test_char_b08_impeding, 1_hours, body_part_arm_r, true, 1000 );
    REQUIRE( you.get_effect_int( effect_test_char_b08_impeding, body_part_arm_r ) == 1000 );

    you.try_remove_impeding_effect();

    CHECK_FALSE( you.has_effect( effect_test_char_b08_impeding, body_part_arm_l ) );
    CHECK( you.has_effect( effect_test_char_b08_impeding, body_part_arm_r ) );

    SECTION( "two weak holds both break" ) {
        you.add_effect( effect_test_char_b08_impeding, 1_hours, body_part_leg_l, true, 1 );
        you.remove_effect( effect_test_char_b08_impeding, body_part_arm_r );
        you.add_effect( effect_test_char_b08_impeding, 1_hours, body_part_leg_r, true, 1 );
        you.try_remove_impeding_effect();
        CHECK_FALSE( you.has_effect( effect_test_char_b08_impeding ) );
    }
}

// ---- CHAR-B10 ----

static const ter_str_id ter_t_floor( "t_floor" );
static const ter_str_id ter_t_wall( "t_wall" );

static const vproto_id vehicle_prototype_hospital_bed_small( "hospital_bed_small" );

static vehicle *empty_vehicle_at( map &here, const vproto_id &type, const tripoint_bub_ms &p )
{
    vehicle *veh = here.add_vehicle( type, p, 0_degrees, 0, veh_spawn_status::UNDAMAGED );
    REQUIRE( veh != nullptr );
    for( const vpart_reference &vp : veh->get_all_parts() ) {
        veh->get_items( vp.part() ).clear();
    }
    veh->invalidate_mass();
    return veh;
}

// Taking the stairs while holding a vehicle put the vehicle beside the avatar on the new level
// with none of the checks dragging makes: it went into walls, furniture, vehicles or
// creatures, cost no moves or stamina, ignored a harnessed animal, and anyone could take any
// vehicle down the stairs.
TEST_CASE( "char-b10_a_vehicle_taken_across_stairs_is_dragged_by_the_rules",
           "[audit][vehicle][grab]" )
{
    clear_avatar();
    clear_map( -2, 1 );
    clear_vehicles();
    map &here = get_map();
    avatar &you = get_avatar();
    on_out_of_scope restore( [&here]() {
        get_avatar().grab( object_type::NONE );
        clear_vehicles( &here );
        clear_map( -2, 1 );
        clear_avatar();
    } );
    // The direction the player picks after taking the stairs
    const tripoint_rel_ms east( 1, 0, 0 );

    SECTION( "up the stairs" ) {
        // The avatar has just come up to the upper floor; the bed is still below.
        const tripoint_bub_ms top( 60, 60, 1 );
        for( const tripoint_bub_ms &p : here.points_in_radius( top, 2 ) ) {
            here.ter_set( p, ter_t_floor );
        }
        you.setpos( here, top );
        const tripoint_rel_ms grab_point( 1, 0, -1 );
        const vehicle *bed = empty_vehicle_at( here, vehicle_prototype_hospital_bed_small,
                                               top + grab_point );
        REQUIRE( bed->total_mass( here ) >= 10_kilogram );
        here.rebuild_vehicle_level_caches();
        you.grab( object_type::VEHICLE, grab_point );
        // As game::vertical_move works it out: from the bed to the tile the player picked
        const tripoint_rel_ms dp = -grab_point + east;

        SECTION( "a wall in the way blocks it" ) {
            here.ter_set( top + east, ter_t_wall );
            CHECK_FALSE( drag_vehicle_across_stairs( here, you, dp ) );
            CHECK( here.veh_at( top + grab_point ) );
            CHECK_FALSE( here.veh_at( top + east ) );
        }
        SECTION( "it takes moves and stamina" ) {
            you.set_moves( 100 );
            const int stamina = you.get_stamina();
            CHECK( drag_vehicle_across_stairs( here, you, dp ) );
            CHECK( here.veh_at( top + east ) );
            CHECK( you.get_moves() < 100 );
            CHECK( you.get_stamina() < stamina );
        }
    }
    SECTION( "down the stairs" ) {
        // The avatar has just come down; the bed is still on the floor above.
        const tripoint_bub_ms bottom( 60, 60, 0 );
        const tripoint_rel_ms grab_point( 1, 0, 1 );
        here.ter_set( bottom + grab_point, ter_t_floor );
        you.setpos( here, bottom );
        vehicle *bed = empty_vehicle_at( here, vehicle_prototype_hospital_bed_small,
                                         bottom + grab_point );
        here.rebuild_vehicle_level_caches();
        you.grab( object_type::VEHICLE, grab_point );
        const tripoint_rel_ms dp = -grab_point + east;
        const int str_req = bed->total_mass( here ) / 10_kilogram;
        REQUIRE( str_req >= 2 );
        you.set_str_base( str_req - 1 );
        REQUIRE( you.get_arm_str() < str_req );

        CHECK_FALSE( drag_vehicle_across_stairs( here, you, dp ) );
        CHECK( here.veh_at( bottom + grab_point ) );
        CHECK_FALSE( here.veh_at( bottom + east ) );
    }
}

// ---- HLTH-B11 ----

static const efftype_id effect_hallu( "hallu" );
static const efftype_id effect_visuals( "visuals" );

// Runs the avatar's effects for the turn when a hallucination of the given length has the
// given time left.
static void hallucinate( avatar &you, const time_duration &dose, const time_duration &left )
{
    you.remove_effect( effect_hallu );
    you.remove_effect( effect_visuals );
    you.add_effect( effect_hallu, dose );
    calendar::turn += dose - left;
    you.get_effect( effect_hallu ).set_duration( left );
    you.process_effects();
}

// The hallucination stages were timed for a 6-hour dose: visuals began with exactly 4.8 h left
// and the symptoms ran from 4.8 h to 1.8 h left, so a dose under 1.8 h, like LSD's hour, a
// stare's half hour or sleep deprivation's minutes, did nothing.  The stages are now fractions
// of the dose.
TEST_CASE( "hlth-b11_a_short_hallucination_goes_through_its_stages", "[audit][effect]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    restore_on_out_of_scope restore_turn( calendar::turn );
    on_out_of_scope restore( []() {
        clear_avatar();
        clear_creatures();
    } );

    SECTION( "an hour" ) {
        hallucinate( you, 1_hours, 48_minutes );
        CHECK( you.has_effect( effect_visuals ) );
        hallucinate( you, 1_hours, 30_minutes );
        CHECK( you.per_bonus_hardcoded == -2 );
        CHECK( you.dex_bonus_hardcoded == -2 );
        hallucinate( you, 1_hours, 10_minutes );
        CHECK( you.per_bonus_hardcoded == 0 );
    }
    SECTION( "five minutes" ) {
        hallucinate( you, 5_minutes, 4_minutes );
        CHECK( you.has_effect( effect_visuals ) );
        hallucinate( you, 5_minutes, 2_minutes );
        CHECK( you.per_bonus_hardcoded == -2 );
    }
    SECTION( "six hours, as before" ) {
        hallucinate( you, 6_hours, 288_minutes );
        CHECK( you.has_effect( effect_visuals ) );
        hallucinate( you, 6_hours, 289_minutes );
        CHECK_FALSE( you.has_effect( effect_visuals ) );
        hallucinate( you, 6_hours, 3_hours );
        CHECK( you.per_bonus_hardcoded == -2 );
        hallucinate( you, 6_hours, 100_minutes );
        CHECK( you.per_bonus_hardcoded == 0 );
    }
}
