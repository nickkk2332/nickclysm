#include <string>

#include "addiction.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "condition.h"
#include "coordinates.h"
#include "dialogue.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "math_parser.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"

static const addiction_id addiction_alcohol( "alcohol" );
static const addiction_id addiction_caffeine( "caffeine" );

static const ter_str_id ter_t_thconc_floor( "t_thconc_floor" );

static talk_effect_t load_effect( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    talk_effect_t effect( jo, "effect", "audit_eoc_math_test" );
    // Math is parsed after loading; outside data loading that has to be done by hand.
    finalize_conditions();
    return effect;
}

static conditional_t::func load_condition( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    conditional_t::func condition;
    read_condition( jo, "condition", condition, false );
    return condition;
}

// EOC-B03: map_is_outside returned !map::is_outside(), so it was true indoors and false
// outdoors, the opposite of its documentation and of the sibling is_outside condition.
TEST_CASE( "eoc-b03_map_is_outside_matches_its_docs", "[audit][eoc]" )
{
    clear_avatar();
    clear_map();
    avatar &u = get_avatar();
    map &here = get_map();
    const std::string loc_var = "audit_b03_loc";
    const conditional_t::func map_is_outside = load_condition(
                R"({ "condition": { "map_is_outside": { "global_val": "audit_b03_loc" } } })" );
    const conditional_t::func tile_is_outside = load_condition(
                R"({ "condition": { "is_outside": { "global_val": "audit_b03_loc" } } })" );
    dialogue d( get_talker_for( u ), nullptr );

    const tripoint_bub_ms outdoors = u.pos_bub() + tripoint::east * 3;
    const tripoint_bub_ms indoors = u.pos_bub() + tripoint::west * 3;
    REQUIRE( here.ter_set( indoors, ter_t_thconc_floor ) );
    here.set_outside_cache_dirty( indoors.z() );
    here.build_outside_cache( indoors.z() );
    REQUIRE( here.is_outside( outdoors ) );
    REQUIRE_FALSE( here.is_outside( indoors ) );

    get_globals().set_global_value( loc_var, here.get_abs( outdoors ) );
    CHECK( tile_is_outside( d ) );
    CHECK( map_is_outside( d ) );

    get_globals().set_global_value( loc_var, here.get_abs( indoors ) );
    CHECK_FALSE( tile_is_outside( d ) );
    CHECK_FALSE( map_is_outside( d ) );

    get_globals().remove_global_value( loc_var );
}

// Runs a weighted_list_eocs with the given entries. EOC_mess_pass sets weighted_var to 1 and
// EOC_mess_fail sets it to -999; it starts at 5, so 5 means no EOC ran.
static double run_weighted_list( const std::string &entries )
{
    get_globals().set_global_value( "weighted_var", 5 );
    const talk_effect_t effect = load_effect( R"({ "effect": { "weighted_list_eocs": )" + entries +
                                 " } }" );
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    effect.apply( d );
    const double ret = get_globals().get_global_value( "weighted_var" ).dbl();
    get_globals().remove_global_value( "weighted_var" );
    return ret;
}

// EOC-B04: weights went into a weighted_int_list, so 0.5 truncated to 0 and was dropped;
// when every weight was dropped pick() returned null and was dereferenced. Bare entries, as
// in the documentation's example, failed to load.
TEST_CASE( "eoc-b04_weighted_list_eocs_fractional_and_zero_weights", "[audit][eoc]" )
{
    clear_avatar();
    // Fractional weights are kept: the only positive weight is below 1.
    CHECK( run_weighted_list( R"([ [ "EOC_mess_pass", 0.5 ], [ "EOC_mess_fail", 0 ] ])" ) == 1 );
    const std::string math_quarter =
        R"([ [ "EOC_mess_fail", 0 ], [ "EOC_mess_pass", { "math": [ "0.25" ] } ] ])";
    CHECK( run_weighted_list( math_quarter ) == 1 );
    // Every weight is 0 or less: nothing runs, and nothing crashes.
    const std::string all_zero =
        R"([ [ "EOC_mess_pass", 0 ], [ "EOC_mess_fail", { "math": [ "0.4 - 1" ] } ] ])";
    CHECK( run_weighted_list( all_zero ) == 5 );
    // A bare EOC entry has weight 1.
    CHECK( run_weighted_list( R"([ "EOC_mess_pass", [ "EOC_mess_fail", 0 ] ])" ) == 1 );
}

static double eval_math( dialogue &d, const std::string &expr )
{
    math_exp exp;
    REQUIRE( exp.parse( expr ) );
    return exp.eval( d );
}

static int caffeine_turns( const Character &you )
{
    for( const addiction &add : you.addictions ) {
        if( add.type == addiction_caffeine ) {
            return to_turns<int>( add.sated );
        }
    }
    return 0;
}

// MATH-B02: math passes an assign function the new value (for += and ++ that is old + rhs),
// but several setters added it to the current value, so = didn't set and += doubled.
TEST_CASE( "math-b02_assignable_math_functions_set_their_value", "[audit][math_parser]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    dialogue d( get_talker_for( u ), nullptr );

    SECTION( "addiction_turns" ) {
        u.addictions.clear();
        u.addictions.emplace_back( addiction_caffeine, 5 );
        u.addictions.back().sated = 1_hours;
        REQUIRE( eval_math( d, "u_addiction_turns('caffeine')" ) == 3600 );

        eval_math( d, "u_addiction_turns('caffeine') = 0" );
        CHECK( caffeine_turns( u ) == 0 );
        eval_math( d, "u_addiction_turns('caffeine') = -50" );
        CHECK( caffeine_turns( u ) == -50 );
        eval_math( d, "u_addiction_turns('caffeine')++" );
        CHECK( caffeine_turns( u ) == -49 );
        eval_math( d, "u_addiction_turns('caffeine') += 100" );
        CHECK( caffeine_turns( u ) == 51 );

        // A missing addiction can be given a duration.
        eval_math( d, "u_addiction_turns('alcohol') = 600" );
        CHECK( eval_math( d, "u_addiction_turns('alcohol')" ) == 600 );
        CHECK( u.addiction_level( addiction_alcohol ) == 1 );
    }

    SECTION( "perceived pain" ) {
        u.set_pain( 0 );
        u.set_painkiller( 0 );
        eval_math( d, "u_pain('type': 'perceived') = 10" );
        REQUIRE( u.get_perceived_pain() == 10 );
        eval_math( d, "u_pain('type': 'perceived') = 10" );
        CHECK( u.get_perceived_pain() == 10 );
        eval_math( d, "u_pain('type': 'perceived') += 5" );
        CHECK( u.get_perceived_pain() == 15 );
        eval_math( d, "u_pain('type': 'perceived') = 4" );
        CHECK( u.get_perceived_pain() == 4 );
    }

    SECTION( "stat bonuses" ) {
        u.reset_bonuses();
        eval_math( d, "u_val('strength_bonus') = 3" );
        REQUIRE( u.get_str_bonus() == 3 );
        eval_math( d, "u_val('strength_bonus') = 3" );
        CHECK( u.get_str_bonus() == 3 );
        eval_math( d, "u_val('strength_bonus') += 1" );
        CHECK( u.get_str_bonus() == 4 );
        eval_math( d, "u_val('dexterity_bonus') = -2" );
        eval_math( d, "u_val('dexterity_bonus') = -2" );
        CHECK( u.get_dex_bonus() == -2 );
        eval_math( d, "u_val('intelligence_bonus') = 1" );
        eval_math( d, "u_val('intelligence_bonus') = 1" );
        CHECK( u.get_int_bonus() == 1 );
        eval_math( d, "u_val('perception_bonus') = 2" );
        eval_math( d, "u_val('perception_bonus') = 2" );
        CHECK( u.get_per_bonus() == 2 );
        u.reset_bonuses();
    }
}
