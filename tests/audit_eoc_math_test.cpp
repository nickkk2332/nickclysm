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
