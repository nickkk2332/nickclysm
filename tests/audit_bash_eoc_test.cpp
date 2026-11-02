#include <string>

#include "avatar.h"
#include "cata_catch.h"
#include "coordinates.h"
#include "global_vars.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser_diag_value.h"
#include "monster.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"

static const field_type_str_id field_test_fd_bash_eoc_web( "test_fd_bash_eoc_web" );

static const furn_str_id furn_test_f_bash_eoc_altar( "test_f_bash_eoc_altar" );
static const furn_str_id furn_test_f_bash_eoc_crate( "test_f_bash_eoc_crate" );

static const ter_str_id ter_t_floor( "t_floor" );
static const ter_str_id ter_test_t_bash_eoc_wall( "test_t_bash_eoc_wall" );

// A global set by the test EOCs, or nullptr if they never ran
static const diag_value *bash_global( const std::string &name )
{
    return get_globals().maybe_get_global_value( name );
}

// MAP-J01: nothing ran when terrain, furniture or fields were hit or broken. The bash object
// now takes "eoc_on_hit" and "eoc_on_destroy", run with the basher (if any) as alpha and the
// tile and what was there before and after as context.
TEST_CASE( "map-j01_bash_eocs_run_when_a_tile_is_hit_or_broken", "[audit][map][eoc]" )
{
    clear_map_without_vision();
    clear_avatar();
    get_globals().clear_global_values();
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms target = u.pos_bub() + tripoint_rel_ms::east;

    SECTION( "the avatar's smash hits furniture without breaking it" ) {
        here.furn_set( target, furn_test_f_bash_eoc_altar );
        tripoint_bub_ms smashp = target;
        u.smash( smashp );
        REQUIRE( here.furn( target ) == furn_test_f_bash_eoc_altar );

        const diag_value *by_avatar = bash_global( "test_bash_hit_by_avatar" );
        REQUIRE( by_avatar != nullptr );
        CHECK( by_avatar->dbl() == 1 );
        const diag_value *loc = bash_global( "test_bash_hit_loc" );
        REQUIRE( loc != nullptr );
        CHECK( loc->tripoint() == here.get_abs( target ) );
        const diag_value *new_furn = bash_global( "test_bash_hit_new_furn" );
        REQUIRE( new_furn != nullptr );
        CHECK( new_furn->str() == furn_test_f_bash_eoc_altar.str() );
    }

    SECTION( "a monster's bash breaks furniture" ) {
        here.furn_set( target, furn_test_f_bash_eoc_crate );
        monster &zombie = spawn_test_monster( "mon_test_zombie", target + tripoint_rel_ms::east );
        REQUIRE( zombie.bash_at( target ) );
        REQUIRE( here.furn( target ) == furn_str_id::NULL_ID() );

        const diag_value *by_monster = bash_global( "test_bash_crate_by_monster" );
        REQUIRE( by_monster != nullptr );
        CHECK( by_monster->dbl() == 1 );
        const diag_value *old_furn = bash_global( "test_bash_crate_old_furn" );
        REQUIRE( old_furn != nullptr );
        CHECK( old_furn->str() == furn_test_f_bash_eoc_crate.str() );
        const diag_value *new_furn = bash_global( "test_bash_crate_new_furn" );
        REQUIRE( new_furn != nullptr );
        CHECK( new_furn->str() == furn_str_id::NULL_ID().str() );
    }

    SECTION( "terrain broken with no basher" ) {
        here.ter_set( target, ter_test_t_bash_eoc_wall );
        here.bash( target, 50 );
        REQUIRE( here.ter( target ) == ter_t_floor );

        const diag_value *alpha = bash_global( "test_bash_wall_alpha" );
        REQUIRE( alpha != nullptr );
        CHECK( alpha->dbl() == 0 );
        const diag_value *old_ter = bash_global( "test_bash_wall_old_ter" );
        REQUIRE( old_ter != nullptr );
        CHECK( old_ter->str() == ter_test_t_bash_eoc_wall.str() );
        const diag_value *new_ter = bash_global( "test_bash_wall_new_ter" );
        REQUIRE( new_ter != nullptr );
        CHECK( new_ter->str() == ter_t_floor.str() );
    }

    SECTION( "the avatar's smash clears a field" ) {
        REQUIRE( here.add_field( target, field_test_fd_bash_eoc_web, 1 ) );
        tripoint_bub_ms smashp = target;
        u.smash( smashp );
        REQUIRE( here.get_field( target, field_test_fd_bash_eoc_web ) == nullptr );

        const diag_value *by_avatar = bash_global( "test_bash_web_by_avatar" );
        REQUIRE( by_avatar != nullptr );
        CHECK( by_avatar->dbl() == 1 );
        const diag_value *field = bash_global( "test_bash_web_field" );
        REQUIRE( field != nullptr );
        CHECK( field->str() == field_test_fd_bash_eoc_web.str() );
    }

    SECTION( "bashing a field away with map::bash" ) {
        REQUIRE( here.add_field( target, field_test_fd_bash_eoc_web, 1 ) );
        here.bash( target, 10 );
        REQUIRE( here.get_field( target, field_test_fd_bash_eoc_web ) == nullptr );

        const diag_value *field = bash_global( "test_bash_web_field" );
        REQUIRE( field != nullptr );
        CHECK( field->str() == field_test_fd_bash_eoc_web.str() );
        CHECK( bash_global( "test_bash_web_by_avatar" ) == nullptr );
    }
}
