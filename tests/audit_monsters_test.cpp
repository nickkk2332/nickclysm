#include <map>
#include <string>
#include <utility>
#include <vector>

#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "coordinates.h"
#include "flexbuffer_json.h"
#include "item.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "messages.h"
#include "monattack.h"
#include "mongroup.h"
#include "monster.h"
#include "npc.h"
#include "options_helpers.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"

static const faction_id faction_your_followers( "your_followers" );

static const itype_id itype_meat( "meat" );

// hates the "player" and "human" monster factions
static const mfaction_str_id monfaction_attack_player_only( "attack_player_only" );
// hates the "player" monster faction, ignores NPCs with PORTAL_STORM_IGNORE_NPC
static const mfaction_str_id monfaction_nether_player_hate( "nether_player_hate" );

static const mongroup_id GROUP_AUDIT_MONDT_B01( "GROUP_AUDIT_MONDT_B01" );

static const mtype_id mon_null( "mon_null" );
static const mtype_id mon_zombie( "mon_zombie" );

static const ter_str_id ter_t_wall( "t_wall" );

static bool has_message_containing( const std::string &text )
{
    for( const std::pair<std::string, std::string> &msg :
         Messages::recent_messages( Messages::size() ) ) {
        if( msg.second.find( text ) != std::string::npos ) {
            return true;
        }
    }
    return false;
}

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

// absorb_meat must name the meat it absorbed (it used to read the name from the item it had
// just destroyed), must keep looking past meat too big to absorb, and must only tell the
// player about meat they can see being quivered at.
TEST_CASE( "monai-b03_absorb_meat_names_meat_and_skips_oversized_meat", "[audit][monster]" )
{
    clear_map();
    clear_avatar();
    map &here = get_map();
    Character &you = get_player_character();
    const tripoint_bub_ms mon_pos( 50, 50, 0 );
    // the tiles around the monster are searched starting north-west, ending south-east
    const tripoint_bub_ms first_tile = mon_pos + tripoint( -1, -1, 0 );
    const tripoint_bub_ms last_tile = mon_pos + tripoint( 1, 1, 0 );
    you.setpos( here, mon_pos + point( 5, 0 ) );
    // a zombie (62 L) absorbs meat of up to 3 L a piece
    monster &zed = spawn_test_monster( "mon_zombie", mon_pos );

    SECTION( "the message names the meat that was used up" ) {
        here.add_item( last_tile, item( itype_meat ) );
        set_time_to_day();
        REQUIRE( you.sees( here, zed ) );

        Messages::clear_messages();
        CHECK( mattack::absorb_meat( &zed ) );
        CHECK( here.i_at( last_tile ).empty() );
        CHECK( has_message_containing( "absorbs the chunk of meat" ) );
    }

    SECTION( "meat too big to absorb doesn't stop the search" ) {
        here.add_item( first_tile, item::make_corpse( mon_zombie ) );
        here.add_item( last_tile, item( itype_meat ) );
        set_time_to_day();
        REQUIRE( you.sees( here, zed ) );

        Messages::clear_messages();
        CHECK( mattack::absorb_meat( &zed ) );
        CHECK( here.i_at( first_tile ).size() == 1 );
        CHECK( here.i_at( last_tile ).empty() );
        CHECK( has_message_containing( "absorbs the chunk of meat" ) );
        CHECK( !has_message_containing( "quivers hungrily" ) );
    }

    SECTION( "quivering at meat too big to absorb is only shown when seen" ) {
        here.add_item( first_tile, item::make_corpse( mon_zombie ) );
        player_wear_blindfold();
        set_time_to_day();
        REQUIRE( !you.sees( here, zed ) );

        Messages::clear_messages();
        CHECK( !mattack::absorb_meat( &zed ) );
        CHECK( here.i_at( first_tile ).size() == 1 );
        CHECK( !has_message_containing( "quivers hungrily" ) );
    }
}

// Rolls a throwaway group whose only entry spawns at NIGHT, at the given time past midnight.
static mtype_id roll_night_only_group( const time_duration &time_of_day )
{
    calendar::turn = calendar::turn_zero + 10_days + time_of_day;
    const std::vector<MonsterGroupResult> res =
        MonsterGroupManager::GetResultFromGroup( GROUP_AUDIT_MONDT_B01 );
    return res.empty() ? mtype_id::NULL_ID() : res.front().id;
}

// The NIGHT spawn window runs from sunset to the next sunrise, but both were taken from the
// same day, so it was empty and NIGHT-only entries never spawned.
TEST_CASE( "mondt-b01_night_only_entries_spawn_at_night", "[audit][mongroup]" )
{
    const time_point old_turn = calendar::turn;
    const time_point old_start = calendar::start_of_cataclysm;
    calendar::start_of_cataclysm = calendar::turn_zero;

    const std::string json = R"({
        "id": "GROUP_AUDIT_MONDT_B01",
        "default": "mon_null",
        "monsters": [ { "monster": "mon_zombie", "weight": 1, "conditions": [ "NIGHT" ] } ]
    })";
    JsonObject jo = json_loader::from_string( json );
    MonsterGroupManager::LoadMonsterGroup( jo );

    // a group with a single entry of weight 1 always picks it when it is valid
    CHECK( roll_night_only_group( 2_hours ) == mon_zombie );
    CHECK( roll_night_only_group( 23_hours ) == mon_zombie );
    CHECK( roll_night_only_group( 12_hours ) == mon_null );

    MonsterGroupManager::Get_all_Groups().erase( GROUP_AUDIT_MONDT_B01 );
    calendar::turn = old_turn;
    calendar::start_of_cataclysm = old_start;
}
