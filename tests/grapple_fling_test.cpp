#include <string>

#include "avatar.h"
#include "cata_catch.h"
#include "coordinates.h"
#include "creature.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "global_vars.h"
#include "magic.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser_diag_value.h"
#include "monster.h"
#include "options_helpers.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"

// The throw engine without grappling (game::fling_creature, from Cataclysm: The Last
// Generation): the u_fling / npc_fling EOC effects and the fling spell effect.

static const effect_on_condition_id
effect_on_condition_EOC_TEST_FLING_EAST( "EOC_TEST_FLING_EAST" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_FLING_TOWARD( "EOC_TEST_FLING_TOWARD" );

static const efftype_id effect_airborne( "airborne" );

static const spell_id spell_test_spell_fling( "test_spell_fling" );

static constexpr tripoint_bub_ms flinger_pos{ 60, 60, 0 };

static avatar &setup_fling_scene()
{
    clear_map_without_vision();
    clear_creatures();
    clear_avatar();
    avatar &you = get_avatar();
    you.setpos( get_map(), flinger_pos );
    return you;
}

TEST_CASE( "fling_eoc_throws_its_talker", "[grapple][eoc]" )
{
    avatar &you = setup_fling_scene();
    override_option no_animations( "ANIMATIONS", "false" );
    map &here = get_map();

    SECTION( "npc_fling: the beta talker, by direction" ) {
        monster &zed = spawn_test_monster( "mon_zombie", you.pos_bub() + tripoint( 0, 3, 0 ) );
        const tripoint_bub_ms start = zed.pos_bub();
        dialogue d( get_talker_for( you ), get_talker_for( zed ) );
        // Force 30, direction 0 (east): three tiles.
        CHECK( effect_on_condition_EOC_TEST_FLING_EAST->activate( d ) );
        CHECK( zed.pos_bub() == start + tripoint( 3, 0, 0 ) );
        CHECK( you.pos_bub() == flinger_pos );
        CHECK( !zed.has_effect( effect_airborne ) );
    }
    SECTION( "u_fling: the alpha talker, toward a location in a variable" ) {
        monster &zed = spawn_test_monster( "mon_zombie", you.pos_bub() + tripoint( 0, 3, 0 ) );
        const tripoint_bub_ms start = zed.pos_bub();
        global_variables &globals = get_globals();
        globals.set_global_value( "test_fling_force", diag_value( 20.0 ) );
        globals.set_global_value( "test_fling_to",
                                  diag_value( here.get_abs( start + tripoint( 0, 10, 0 ) ) ) );
        dialogue d( get_talker_for( zed ), get_talker_for( you ) );
        CHECK( effect_on_condition_EOC_TEST_FLING_TOWARD->activate( d ) );
        // Force 20: two tiles south.
        CHECK( zed.pos_bub() == start + tripoint( 0, 2, 0 ) );
        globals.clear_global_values();
    }
}

TEST_CASE( "fling_spell_throws_its_target_away_from_the_caster", "[grapple][magic]" )
{
    avatar &you = setup_fling_scene();
    override_option no_animations( "ANIMATIONS", "false" );
    monster &zed = spawn_test_monster( "mon_zombie", you.pos_bub() + tripoint( 2, 0, 0 ) );
    const tripoint_bub_ms start = zed.pos_bub();

    spell sp( spell_test_spell_fling );
    REQUIRE( sp.damage( you ) == 30 );
    sp.cast_spell_effect( you, start );
    // Away from the caster, damage / 10 tiles.
    CHECK( zed.pos_bub() == start + tripoint( 3, 0, 0 ) );
    CHECK( you.pos_bub() == flinger_pos );
}
