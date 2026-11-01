#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "coordinates.h"
#include "map_helpers.h"
#include "npc.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"

static const efftype_id effect_shakes( "shakes" );

static npc &audit_effects_spawn_npc()
{
    clear_map();
    clear_avatar();
    npc &guy = spawn_npc( get_player_character().pos_bub().xy() + point( 5, 0 ), "test_talker" );
    clear_character( guy );
    return guy;
}

static void audit_reset_stats( Character &who )
{
    who.reset_bonuses();
    who.reset_stats();
}

// CHAR-B04: effect str/dex/per/int mods, pain, starvation and thirst penalties, trait
// buffs and martial arts static buffs were applied only in avatar::reset_stats. NPCs
// run Character::reset_stats, so a shaking, thirsty or hurting NPC fought at full stats.
TEST_CASE( "char-b04_npc_gets_effect_stat_mods_and_need_penalties", "[audit][character]" )
{
    npc &guy = audit_effects_spawn_npc();
    audit_reset_stats( guy );
    const int str = guy.get_str();
    const int dex = guy.get_dex();
    const int per = guy.get_per();
    const int intl = guy.get_int();
    REQUIRE( str > 4 );
    REQUIRE( dex > 4 );

    SECTION( "effect stat mods" ) {
        // shakes: str_mod -1, dex_mod -4
        guy.add_effect( effect_shakes, 1_hours );
        audit_reset_stats( guy );
        CHECK( guy.get_str() == str - 1 );
        CHECK( guy.get_dex() == dex - 4 );
        CHECK( guy.get_per() == per );
        CHECK( guy.get_int() == intl );
    }

    SECTION( "thirst penalty" ) {
        guy.set_thirst( 600 );
        REQUIRE( guy.get_thirst() >= 200 );
        const int pen = guy.get_thirst() / 200;
        audit_reset_stats( guy );
        CHECK( guy.get_str() == str - pen );
        CHECK( guy.get_dex() == dex - pen );
        CHECK( guy.get_per() == per - pen );
        CHECK( guy.get_int() == intl - pen );
    }

    SECTION( "pain penalty" ) {
        guy.set_pain( 60 );
        REQUIRE( guy.get_perceived_pain() > 0 );
        audit_reset_stats( guy );
        // Any perceived pain costs at least one point of each stat above 2.
        CHECK( guy.get_str() < str );
        CHECK( guy.get_dex() < dex );
    }

    clear_avatar();
}

// The avatar must still get each effect mod exactly once after the move.
TEST_CASE( "char-b04_avatar_gets_effect_stat_mods_once", "[audit][character]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    audit_reset_stats( u );
    const int str = u.get_str();
    const int dex = u.get_dex();

    u.add_effect( effect_shakes, 1_hours );
    audit_reset_stats( u );
    CHECK( u.get_str() == str - 1 );
    CHECK( u.get_dex() == dex - 4 );
    clear_avatar();
}
