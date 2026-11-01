#include <string>

#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "coordinates.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "monster.h"
#include "npc.h"
#include "options_helpers.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"

static const faction_id faction_your_followers( "your_followers" );

// hates the "player" and "human" monster factions
static const mfaction_str_id monfaction_attack_player_only( "attack_player_only" );
// hates the "player" monster faction, ignores NPCs with PORTAL_STORM_IGNORE_NPC
static const mfaction_str_id monfaction_nether_player_hate( "nether_player_hate" );

static const ter_str_id ter_t_wall( "t_wall" );

// A monster whose faction hates an NPC's faction must not drop the player it can see for
// an NPC it can't see, or for a hated NPC much farther away, or for one it ignores.
TEST_CASE( "monai-b02_hated_npc_needs_to_be_seen_and_near", "[audit][monster]" )
{
    clear_map();
    clear_avatar();
    map &here = get_map();
    Character &player_character = get_player_character();
    const tripoint_bub_ms mon_pos( 50, 50, 0 );

    SECTION( "an NPC behind walls does not replace an adjacent player" ) {
        player_character.setpos( here, mon_pos + tripoint::east );
        monster &zed = spawn_test_monster( "mon_zombie", mon_pos );
        zed.faction = monfaction_attack_player_only.id();
        npc &guy = spawn_npc( ( mon_pos + point( 0, 8 ) ).xy(), "test_talker" );
        for( const tripoint_bub_ms &p : here.points_in_radius( guy.pos_bub(), 1 ) ) {
            if( p != guy.pos_bub() ) {
                here.ter_set( p, ter_t_wall );
            }
        }
        set_time_to_day();
        REQUIRE( zed.sees( here, player_character ) );
        REQUIRE( !zed.sees( here, guy ) );

        zed.unset_dest();
        zed.plan();
        CHECK( zed.get_dest() == player_character.pos_abs() );
    }

    SECTION( "a farther visible NPC does not replace an adjacent player" ) {
        player_character.setpos( here, mon_pos + tripoint::east );
        monster &zed = spawn_test_monster( "mon_zombie", mon_pos );
        zed.faction = monfaction_attack_player_only.id();
        npc &guy = spawn_npc( ( mon_pos + point( 0, 4 ) ).xy(), "test_talker" );
        set_time_to_day();
        REQUIRE( zed.sees( here, player_character ) );
        REQUIRE( zed.sees( here, guy ) );

        zed.unset_dest();
        zed.plan();
        CHECK( zed.get_dest() == player_character.pos_abs() );
    }

    SECTION( "a visible NPC not much farther than the player is preferred" ) {
        player_character.setpos( here, mon_pos + point( 3, 0 ) );
        monster &zed = spawn_test_monster( "mon_zombie", mon_pos );
        zed.faction = monfaction_attack_player_only.id();
        npc &guy = spawn_npc( ( mon_pos + point( 0, 4 ) ).xy(), "test_talker" );
        set_time_to_day();
        REQUIRE( zed.sees( here, player_character ) );
        REQUIRE( zed.sees( here, guy ) );

        zed.unset_dest();
        zed.plan();
        CHECK( zed.get_dest() == guy.pos_abs() );
    }

    SECTION( "an ignored NPC does not replace an adjacent player" ) {
        override_option ignore_npcs( "PORTAL_STORM_IGNORE_NPC", "true" );
        player_character.setpos( here, mon_pos + tripoint::east );
        monster &zed = spawn_test_monster( "mon_zombie", mon_pos );
        zed.faction = monfaction_nether_player_hate.id();
        npc &guy = spawn_npc( ( mon_pos + tripoint::west ).xy(), "test_talker" );
        guy.set_fac( faction_your_followers );
        set_time_to_day();
        REQUIRE( zed.sees( here, player_character ) );
        REQUIRE( zed.sees( here, guy ) );
        REQUIRE( zed.attitude( &guy ) == MATT_FPASSIVE );

        zed.unset_dest();
        zed.plan();
        CHECK( zed.get_dest() == player_character.pos_abs() );
    }
}
