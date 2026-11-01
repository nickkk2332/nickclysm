#include <string>

#include "activity_actor_definitions.h"
#include "avatar.h"
#include "cata_catch.h"
#include "character.h"
#include "construction.h"
#include "coordinates.h"
#include "global_vars.h"
#include "map.h"
#include "map_helpers.h"
#include "math_parser_diag_value.h"
#include "npc.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"

static const activity_id ACT_BUILD( "ACT_BUILD" );

static const construction_str_id construction_test_constr_eoc_hooks( "test_constr_eoc_hooks" );

static const ter_str_id ter_t_dirt( "t_dirt" );

// TERR-J01: constructions read no condition or EOC fields, and nothing was sent when one
// was finished, so a mod couldn't gate a construction on the builder or react to it.
TEST_CASE( "terr-j01_construction_condition_and_eoc_hooks", "[audit][construction]" )
{
    clear_map();
    clear_avatar();
    avatar &u = get_avatar();
    map &here = get_map();
    global_variables &globvars = get_globals();
    globvars.clear_global_values();
    const construction &con = construction_test_constr_eoc_hooks.obj();
    const tripoint_bub_ms site = u.pos_bub() + tripoint::east;
    const tripoint_abs_ms site_abs = here.get_abs( site );
    REQUIRE( here.ter( site ) != ter_t_dirt );
    u.remove_value( "test_constr_turns" );
    u.remove_value( "test_constr_done" );
    u.remove_value( "test_constr_done_pos" );

    // The condition is checked with the builder as alpha.
    u.set_value( "test_constr_allowed", 0 );
    CHECK_FALSE( can_construct( con, site ) );
    u.set_value( "test_constr_allowed", 1 );
    CHECK( can_construct( con, site ) );
    npc &guy = spawn_npc( ( u.pos_bub() + tripoint::south ).xy(), "test_talker" );
    CHECK_FALSE( can_construct( con, site, guy ) );
    guy.set_value( "test_constr_allowed", 1 );
    CHECK( can_construct( con, site, guy ) );

    partial_con pc;
    pc.id = construction_test_constr_eoc_hooks.id();
    here.partial_con_set( site, pc );
    u.assign_activity( build_construction_activity_actor( site_abs ) );
    REQUIRE( u.activity.id() == ACT_BUILD );
    // The condition gates starting it, not the work already begun.
    u.set_value( "test_constr_allowed", 0 );
    REQUIRE( process_activity_bounded( u, 1000, 2000 ) );

    CHECK( here.ter( site ) == ter_t_dirt );
    const diag_value *turns = u.maybe_get_value( "test_constr_turns" );
    REQUIRE( turns != nullptr );
    CHECK( turns->dbl() >= 1 );
    const diag_value *done = u.maybe_get_value( "test_constr_done" );
    REQUIRE( done != nullptr );
    CHECK( done->str() == "test_constr_eoc_hooks" );
    const diag_value *done_pos = u.maybe_get_value( "test_constr_done_pos" );
    REQUIRE( done_pos != nullptr );
    REQUIRE( done_pos->is_tripoint() );
    CHECK( done_pos->tripoint() == site_abs );

    // character_finishes_construction carries the construction id and the tile.
    const diag_value *event_id = globvars.maybe_get_global_value( "test_constr_event_id" );
    REQUIRE( event_id != nullptr );
    CHECK( event_id->str() == "test_constr_eoc_hooks" );
    const diag_value *event_pos = globvars.maybe_get_global_value( "test_constr_event_pos" );
    REQUIRE( event_pos != nullptr );
    REQUIRE( event_pos->is_tripoint() );
    CHECK( event_pos->tripoint() == site_abs );
}
