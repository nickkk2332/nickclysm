#include <climits>
#include <initializer_list>
#include <string>
#include <string_view>

#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "condition.h"
#include "coordinates.h"
#include "creature.h"
#include "dialogue.h"
#include "enums.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "json_loader.h"
#include "magic.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser.h"
#include "math_parser_type.h"
#include "monster.h"
#include "pimpl.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "point.h"
#include "talker.h"
#include "type_id.h"
#include "units.h"
#include "vehicle.h"

static const activity_id ACT_AUDIT_QF_MATH_A( "ACT_AUDIT_QF_MATH_A" );

static const efftype_id effect_audit_qf_math_effect( "audit_qf_math_effect" );

static const mutation_category_id mutation_category_INST_TEST( "INST_TEST" );
static const mutation_category_id mutation_category_REMOVAL_TEST( "REMOVAL_TEST" );

static const spell_id spell_test_spell_pew( "test_spell_pew" );
static const spell_id spell_test_spell_random_damage( "test_spell_random_damage" );

static const trait_id trait_AUDIT_QF_MATH_ACTIVE( "AUDIT_QF_MATH_ACTIVE" );
static const trait_id trait_TEST_GOOD1( "TEST_GOOD1" );
static const trait_id trait_TEST_GOOD2( "TEST_GOOD2" );

static const vpart_id vpart_audit_qf_math_ballast( "audit_qf_math_ballast" );
static const vpart_id vpart_frame( "frame" );

static const vproto_id vehicle_prototype_none( "none" );

static double eval_math( dialogue &d, const std::string_view expr )
{
    math_exp exp;
    REQUIRE( exp.parse( expr ) );
    return exp.eval( d );
}

static conditional_t::func load_condition( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    conditional_t::func condition;
    read_condition( jo, "condition", condition, false );
    return condition;
}

// MATH-J01: effect_duration() was documented as assignable, and effect_intensity() took a
// bodypart "to get/set", but neither had an assign function, so the parser rejected both.
TEST_CASE( "math-j01_effect_duration_and_intensity_are_assignable", "[audit][math_parser]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    dialogue d( get_talker_for( u ), nullptr );
    REQUIRE_FALSE( u.has_effect( effect_audit_qf_math_effect ) );

    // a missing effect is added, with the duration in seconds or in the unit given
    eval_math( d, "u_effect_duration('audit_qf_math_effect') = 30" );
    CHECK( u.get_effect_dur( effect_audit_qf_math_effect ) == 30_seconds );
    eval_math( d, "u_effect_duration('audit_qf_math_effect', 'unit': 'minutes') = 2" );
    CHECK( u.get_effect_dur( effect_audit_qf_math_effect ) == 2_minutes );
    CHECK( eval_math( d, "u_effect_duration('audit_qf_math_effect')" ) == 120 );

    // intensity, clamped to the effect's max_intensity of 5
    eval_math( d, "u_effect_intensity('audit_qf_math_effect') = 3" );
    CHECK( u.get_effect_int( effect_audit_qf_math_effect ) == 3 );
    eval_math( d, "u_effect_intensity('audit_qf_math_effect') += 10" );
    CHECK( u.get_effect_int( effect_audit_qf_math_effect ) == 5 );

    // the bodypart kwarg picks the effect on that part, and leaves the other alone
    eval_math( d, "u_effect_duration('audit_qf_math_effect', 'bodypart': 'arm_l') = 10" );
    CHECK( u.get_effect_dur( effect_audit_qf_math_effect, body_part_arm_l ) == 10_seconds );
    CHECK( u.get_effect_dur( effect_audit_qf_math_effect ) == 2_minutes );
    eval_math( d, "u_effect_intensity('audit_qf_math_effect', 'bodypart': 'arm_l') = 2" );
    CHECK( u.get_effect_int( effect_audit_qf_math_effect, body_part_arm_l ) == 2 );
    CHECK( u.get_effect_int( effect_audit_qf_math_effect ) == 5 );

    // 0 removes it
    eval_math( d, "u_effect_duration('audit_qf_math_effect', 'bodypart': 'arm_l') = 0" );
    CHECK_FALSE( u.has_effect( effect_audit_qf_math_effect, body_part_arm_l ) );
    CHECK( u.has_effect( effect_audit_qf_math_effect ) );
    eval_math( d, "u_effect_intensity('audit_qf_math_effect') = 0" );
    CHECK_FALSE( u.has_effect( effect_audit_qf_math_effect ) );
    // an intensity has no duration to add a missing effect with
    eval_math( d, "u_effect_intensity('audit_qf_math_effect') = 2" );
    CHECK_FALSE( u.has_effect( effect_audit_qf_math_effect ) );
    CHECK( eval_math( d, "u_effect_intensity('audit_qf_math_effect')" ) == -1 );

    CHECK_THROWS_AS( eval_math( d, "u_effect_duration('audit_no_such_effect') = 5" ),
                     math::runtime_error );
}

// MATH-J01: the documented example u_val('strength') = 2 threw "Invalid aspect": only the
// *_base and *_bonus attributes could be set.
TEST_CASE( "math-j01_val_sets_the_current_attributes", "[audit][math_parser]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    dialogue d( get_talker_for( u ), nullptr );
    u.set_str_bonus( 2 );
    u.set_dex_bonus( -1 );

    // the base is set so that base + bonus reads back as set
    eval_math( d, "u_val('strength') = 12" );
    CHECK( u.get_str() == 12 );
    CHECK( u.get_str_base() == 10 );
    CHECK( eval_math( d, "u_val('strength')" ) == 12 );
    eval_math( d, "u_val('dexterity') = 7" );
    CHECK( u.get_dex() == 7 );
    CHECK( u.get_dex_base() == 8 );
    eval_math( d, "u_val('intelligence') = 9" );
    CHECK( u.get_int() == 9 );
    eval_math( d, "u_val('perception') = 11" );
    CHECK( u.get_per() == 11 );
    eval_math( d, "u_val('strength') = 2" );
    CHECK( u.get_str() == 2 );
}

// MATH-J01: spell_level_sum('level': N) ignored the level unless 'school' was given too, and
// the documented example passed the level as a string, which is a type error.
TEST_CASE( "math-j01_spell_level_sum_level_works_without_school", "[audit][math_parser][magic]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    u.magic->learn_spell( spell_test_spell_pew, u, true );
    u.magic->learn_spell( spell_test_spell_random_damage, u, true );
    u.magic->set_spell_level( spell_test_spell_pew, 2, &u );
    u.magic->set_spell_level( spell_test_spell_random_damage, 6, &u );
    dialogue d( get_talker_for( u ), nullptr );

    REQUIRE( eval_math( d, "u_spell_level_sum()" ) == 8 );
    CHECK( eval_math( d, "u_spell_level_sum('level': 4)" ) == 6 );
    CHECK( eval_math( d, "u_spell_level_sum('level': 7)" ) == 0 );
    // both test spells are of the NONE class
    CHECK( eval_math( d, "u_spell_level_sum('school': 'NONE', 'level': 4)" ) == 6 );
    // the corrected spell_difficulty() example
    CHECK( eval_math( d, "u_spell_difficulty('test_spell_pew', 'baseline': true)" ) >= 0 );

    u.magic->forget_spell( spell_test_spell_pew );
    u.magic->forget_spell( spell_test_spell_random_damage );
}

// MUT-J04: math could read none of the mutation internals the game runs on: the instability
// per category and mutation_category_level, which gates thresholds and dreams.
TEST_CASE( "mut-j04_mutation_instability_and_category_level_are_readable",
           "[audit][math_parser][mutations]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    u.set_mutation( trait_TEST_GOOD1 );
    u.set_mutation( trait_TEST_GOOD2 );
    u.calc_mutation_levels();
    dialogue d( get_talker_for( u ), nullptr );

    // mutations of another tree weigh double
    const int in_tree = u.get_instability_per_category( mutation_category_INST_TEST );
    const int out_of_tree = u.get_instability_per_category( mutation_category_REMOVAL_TEST );
    REQUIRE( in_tree > 0 );
    REQUIRE( out_of_tree > in_tree );
    CHECK( eval_math( d, "u_mutation_instability('INST_TEST')" ) == in_tree );
    CHECK( eval_math( d, "u_mutation_instability('REMOVAL_TEST')" ) == out_of_tree );

    const int level = u.mutation_category_level[mutation_category_INST_TEST];
    REQUIRE( level > 0 );
    CHECK( eval_math( d, "u_mutation_category_level('INST_TEST')" ) == level );
    CHECK( eval_math( d, "u_mutation_category_level('REMOVAL_TEST')" ) == 0 );

    CHECK_THROWS_AS( eval_math( d, "u_mutation_instability('AUDIT_NO_SUCH_CATEGORY')" ),
                     math::runtime_error );
}

// MUT-J04: an active trait's cost timer could not be read or reset, and math could not ask
// whether a trait was switched on.
TEST_CASE( "mut-j04_trait_cooldown_and_active_state", "[audit][math_parser][mutations]" )
{
    clear_avatar();
    clear_map();
    avatar &u = get_avatar();
    dialogue d( get_talker_for( u ), nullptr );

    // a trait the character doesn't have reads 0, and setting it does nothing
    CHECK( eval_math( d, "u_trait_cooldown('AUDIT_QF_MATH_ACTIVE')" ) == 0 );
    eval_math( d, "u_trait_cooldown('AUDIT_QF_MATH_ACTIVE') = 5" );
    CHECK( eval_math( d, "u_has_active_trait('AUDIT_QF_MATH_ACTIVE')" ) == 0 );

    u.set_mutation( trait_AUDIT_QF_MATH_ACTIVE );
    CHECK( eval_math( d, "u_has_active_trait('AUDIT_QF_MATH_ACTIVE')" ) == 0 );
    u.activate_mutation( trait_AUDIT_QF_MATH_ACTIVE );
    REQUIRE( u.has_active_mutation( trait_AUDIT_QF_MATH_ACTIVE ) );
    CHECK( eval_math( d, "u_has_active_trait('AUDIT_QF_MATH_ACTIVE')" ) == 1 );

    // switching it on started its 10 turn timer
    REQUIRE( u.get_cost_timer( trait_AUDIT_QF_MATH_ACTIVE ) == 9_turns );
    CHECK( eval_math( d, "u_trait_cooldown('AUDIT_QF_MATH_ACTIVE')" ) == 9 );
    eval_math( d, "u_trait_cooldown('AUDIT_QF_MATH_ACTIVE') = 0" );
    CHECK( u.get_cost_timer( trait_AUDIT_QF_MATH_ACTIVE ) == 0_turns );
    eval_math( d, "u_trait_cooldown('AUDIT_QF_MATH_ACTIVE') = 25" );
    CHECK( u.get_cost_timer( trait_AUDIT_QF_MATH_ACTIVE ) == 25_turns );
    CHECK( eval_math( d, "u_trait_cooldown('AUDIT_QF_MATH_ACTIVE')" ) == 25 );
    eval_math( d, "u_trait_cooldown('AUDIT_QF_MATH_ACTIVE') = -4" );
    CHECK( u.get_cost_timer( trait_AUDIT_QF_MATH_ACTIVE ) == 0_turns );

    // a monster has no traits: it reads 0, and setting it is an error
    monster &zombie = spawn_test_monster( "mon_zombie", u.pos_bub() + tripoint::east * 2 );
    dialogue with_monster( get_talker_for( u ), get_talker_for( zombie ) );
    CHECK( eval_math( with_monster, "n_trait_cooldown('AUDIT_QF_MATH_ACTIVE')" ) == 0 );
    CHECK( eval_math( with_monster, "n_has_active_trait('AUDIT_QF_MATH_ACTIVE')" ) == 0 );
    CHECK( eval_math( with_monster, "n_mutation_instability('INST_TEST')" ) == 0 );
    CHECK( eval_math( with_monster, "n_mutation_category_level('INST_TEST')" ) == 0 );
    CHECK_THROWS_AS( eval_math( with_monster, "n_trait_cooldown('AUDIT_QF_MATH_ACTIVE') = 3" ),
                     math::runtime_error );
    clear_creatures();
}

// ACT-J04: { "u_has_activity": "ACT_READ" } loaded, but the id was thrown away and the
// condition was true for any activity at all.
TEST_CASE( "act-j04_has_activity_honours_its_ids", "[audit][eoc][activity]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    dialogue d( get_talker_for( u ), nullptr );
    const auto has_activity = []( const std::string & arg ) {
        return load_condition( R"({ "condition": { "u_has_activity": )" + arg + " } }" );
    };
    const conditional_t::func bare = load_condition( R"({ "condition": "u_has_activity" })" );
    const conditional_t::func any = has_activity( R"("any")" );
    const conditional_t::func is_a = has_activity( R"("ACT_AUDIT_QF_MATH_A")" );
    const conditional_t::func is_b = has_activity( R"("ACT_AUDIT_QF_MATH_B")" );
    const conditional_t::func either =
        has_activity( R"([ "ACT_AUDIT_QF_MATH_B", "ACT_AUDIT_QF_MATH_A" ])" );
    const conditional_t::func by_var = has_activity( R"({ "global_val": "audit_qf_math_act" })" );
    get_globals().set_global_value( "audit_qf_math_act", std::string( "ACT_AUDIT_QF_MATH_A" ) );

    for( const conditional_t::func *cond : {
             &bare, &any, &is_a, &is_b, &either, &by_var
         } ) {
        CHECK_FALSE( ( *cond )( d ) );
    }

    u.assign_activity( ACT_AUDIT_QF_MATH_A, 1000 );
    REQUIRE( u.activity.id() == ACT_AUDIT_QF_MATH_A );
    CHECK( bare( d ) );
    CHECK( any( d ) );
    CHECK( is_a( d ) );
    CHECK_FALSE( is_b( d ) );
    CHECK( either( d ) );
    CHECK( by_var( d ) );

    u.cancel_activity();
    get_globals().remove_global_value( "audit_qf_math_act" );
}

// ACT-J04: no math function read the current activity, so an EOC could not see how much of it
// was left or cut it short.
TEST_CASE( "act-j04_activity_moves_left_reads_and_sets", "[audit][math_parser][activity]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    dialogue d( get_talker_for( u ), nullptr );
    CHECK( eval_math( d, "u_activity_moves_left()" ) == 0 );
    // nothing to set without an activity
    eval_math( d, "u_activity_moves_left() = 500" );
    CHECK( u.activity.is_null() );

    u.assign_activity( ACT_AUDIT_QF_MATH_A, 1000 );
    REQUIRE( u.activity.id() == ACT_AUDIT_QF_MATH_A );
    CHECK( eval_math( d, "u_activity_moves_left()" ) == 1000 );
    eval_math( d, "u_activity_moves_left() -= 400" );
    CHECK( u.activity.moves_left == 600 );
    CHECK( u.activity.moves_total == 1000 );
    // more than the total raises the total, so the progress stays within 0-100%
    eval_math( d, "u_activity_moves_left() = 3000" );
    CHECK( u.activity.moves_left == 3000 );
    CHECK( u.activity.moves_total == 3000 );
    eval_math( d, "u_activity_moves_left() = -5" );
    CHECK( u.activity.moves_left == 0 );
    u.cancel_activity();
}

// VINT-J04: weight() threw for a vehicle talker, and val('unloaded_weight') narrowed the
// vehicle's mass in milligrams to an int, which wraps past 2147 kg.
TEST_CASE( "vint-j04_weight_reads_heavy_vehicles", "[audit][math_parser][vehicle]" )
{
    clear_avatar();
    clear_map();
    clear_vehicles();
    map &here = get_map();
    avatar &u = get_avatar();
    vehicle *veh = here.add_vehicle( vehicle_prototype_none, u.pos_bub() + tripoint::east * 5,
                                     0_degrees, 0, veh_spawn_status::UNDAMAGED );
    REQUIRE( veh != nullptr );
    REQUIRE( veh->install_part( here, point_rel_ms::zero, vpart_frame ) != -1 );
    REQUIRE( veh->install_part( here, point_rel_ms::zero, vpart_audit_qf_math_ballast ) != -1 );
    veh->refresh();
    here.add_vehicle_to_cache( veh );

    const double unloaded = static_cast<double>( units::to_milligram( veh->unloaded_mass() ) );
    const double total = static_cast<double>( units::to_milligram( veh->total_mass( here ) ) );
    REQUIRE( unloaded > static_cast<double>( INT_MAX ) );
    REQUIRE( total >= unloaded );
    dialogue d( get_talker_for( *veh ), nullptr );
    CHECK( eval_math( d, "u_weight()" ) == Approx( total ) );
    CHECK( eval_math( d, "u_val('unloaded_weight')" ) == Approx( unloaded ) );

    // a character's weight is unchanged
    dialogue by_avatar( get_talker_for( u ), nullptr );
    CHECK( eval_math( by_avatar, "u_weight()" ) ==
           Approx( static_cast<double>( units::to_milligram( u.get_weight() ) ) ) );
    clear_vehicles();
}
