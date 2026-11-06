#include <string>

#include "calendar.h"
#include "cata_catch.h"
#include "coordinates.h"
#include "magic_enchantment.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "monster.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"

// "base_mods": { "speed_mod": [ -80 ] }, no other effect on monsters
static const efftype_id effect_infection( "infection" );
// enchantment: SPEED multiply -0.5
static const efftype_id effect_photophobia( "photophobia" );
// no modifiers and no enchantments
static const efftype_id effect_spooked_recent( "spooked_recent" );
// "base_mods": { "speed_mod": [ -20 ] }
static const efftype_id effect_staggered( "staggered" );

// The speed_mod of an effect must reach the monster's speed (and so the moves it gets each
// turn) instead of being overwritten by the speed computed from its enchantments.
TEST_CASE( "monai-b01_effect_speed_mod_applies_to_monsters", "[audit]" )
{
    clear_map();
    clear_avatar();
    // pseudo_debug_mon has a base speed of 100 and is IMMOBILE
    monster &mon = spawn_test_monster( "pseudo_debug_mon", tripoint_bub_ms( 10, 10, 0 ) );
    const int base_speed = mon.get_speed_base();
    REQUIRE( base_speed == 100 );
    REQUIRE( mon.get_speed() == base_speed );

    SECTION( "speed_mod lowers speed and moves, and a new effect keeps it" ) {
        mon.add_effect( effect_staggered, 5_turns );
        mon.set_moves( 0 );
        mon.process_turn();
        CHECK( mon.get_speed() == base_speed - 20 );
        CHECK( mon.get_moves() == base_speed - 20 );

        // Gaining an unrelated effect must not wipe the speed modifier.
        mon.add_effect( effect_spooked_recent, 5_turns );
        CHECK( mon.get_speed() == base_speed - 20 );
    }

    SECTION( "speed_mod stacks with enchanted speed" ) {
        mon.add_effect( effect_staggered, 5_turns );
        mon.add_effect( effect_photophobia, 5_turns );
        mon.set_moves( 0 );
        mon.process_turn();
        // -20 from staggered and -50% of the base speed from photophobia
        CHECK( mon.get_speed() == base_speed - 20 - base_speed / 2 );
        CHECK( mon.get_moves() == base_speed - 20 - base_speed / 2 );

        // Once the effects are gone, their enchantments are gone too.
        mon.remove_effect( effect_staggered );
        mon.remove_effect( effect_photophobia );
        mon.process_turn();
        CHECK( mon.get_speed() == base_speed );
        CHECK( mon.calculate_by_enchantment( base_speed, enchant_vals::mod::SPEED,
                                             true ) == base_speed );
    }

    SECTION( "speed stays above a quarter of the base speed" ) {
        mon.add_effect( effect_staggered, 5_turns );
        mon.add_effect( effect_infection, 5_turns );
        mon.set_moves( 0 );
        mon.process_turn();
        // -20 - 80 is clamped to -75% of the base speed
        CHECK( mon.get_speed() == base_speed / 4 );
        CHECK( mon.get_moves() == base_speed / 4 );
    }
}
