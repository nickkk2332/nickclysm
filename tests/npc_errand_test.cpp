#include <string>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "coordinates.h"
#include "map.h"
#include "map_helpers.h"
#include "npc.h"
#include "player_helpers.h"
#include "point.h"

// NPC errands from EOCs (src/errand.h) and the NPC movement fixes they rest on.

static constexpr tripoint_bub_ms errand_origin{ 50, 50, 0 };

static npc &errand_npc()
{
    clear_map_without_vision();
    clear_avatar();
    clear_npcs();
    map &here = get_map();
    get_avatar().setpos( here, errand_origin + tripoint( -20, 0, 0 ) );
    npc &guy = spawn_npc( errand_origin.xy(), "test_talker" );
    clear_character( guy, true );
    guy.setpos( here, errand_origin );
    here.build_map_cache( 0 );
    return guy;
}

// NPC-B17: a step to a tile that isn't adjacent used to be a teleport
TEST_CASE( "npc_move_to_does_not_hop_over_tiles", "[npc][errand]" )
{
    npc &guy = errand_npc();
    SECTION( "move_to a tile three steps away stays put" ) {
        guy.set_moves( 100 );
        guy.move_to( errand_origin + tripoint( 3, 0, 0 ) );
        CHECK( guy.pos_bub() == errand_origin );
        CHECK( guy.path.empty() );
    }
    SECTION( "a stale path is routed again from where the NPC stands" ) {
        // as left by a shove: the path starts three tiles away
        guy.path = { errand_origin + tripoint( 3, 0, 0 ), errand_origin + tripoint( 4, 0, 0 ) };
        guy.set_moves( 100 );
        guy.move_to_next();
        CHECK( guy.pos_bub() == errand_origin + tripoint( 1, 0, 0 ) );
        REQUIRE_FALSE( guy.path.empty() );
        CHECK( guy.path.back() == errand_origin + tripoint( 4, 0, 0 ) );
    }
}
