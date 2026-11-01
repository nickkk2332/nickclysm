#include <string>

#include "avatar.h"
#include "cata_catch.h"
#include "condition.h"
#include "coordinates.h"
#include "dialogue.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"

static const ter_str_id ter_t_thconc_floor( "t_thconc_floor" );

static talk_effect_t load_effect( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    return talk_effect_t( jo, "effect", "audit_eoc_math_test" );
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
