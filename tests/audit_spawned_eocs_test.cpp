#include <string>
#include <vector>

#include "avatar.h"
#include "cata_catch.h"
#include "character.h"
#include "creature.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "game.h"
#include "item.h"
#include "item_location.h"
#include "map.h"
#include "map_helpers.h"
#include "math_parser_diag_value.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "player_helpers.h"
#include "type_id.h"

static const effect_on_condition_id
effect_on_condition_EOC_TEST_SPAWNED_EOCS( "EOC_TEST_SPAWNED_EOCS" );

static const itype_id itype_rock( "rock" );

static const mtype_id mon_zombie( "mon_zombie" );

// EOC-J01: spawn effects ran their EOCs with the caller's dialogue, so nothing could act on
// the spawned monster or NPC, and spawned NPCs and items couldn't be given variables.
TEST_CASE( "eoc-j01_spawn_effects_can_act_on_what_they_spawn", "[audit][eoc]" )
{
    clear_avatar();
    clear_map();
    avatar &u = get_avatar();

    dialogue d( get_talker_for( u ), nullptr );
    effect_on_condition_EOC_TEST_SPAWNED_EOCS->activate( d );

    int zombies = 0;
    int tagged_zombies = 0;
    for( monster &critter : g->all_monsters() ) {
        if( critter.type->id == mon_zombie ) {
            zombies++;
            const diag_value *marker = critter.maybe_get_value( "spawned_marker" );
            if( marker != nullptr && marker->dbl() == 1 ) {
                tagged_zombies++;
            }
        }
    }
    CHECK( zombies == 2 );
    CHECK( tagged_zombies == 2 );
    // The spawner is beta inside spawned_eocs.
    const diag_value *tagged_count = u.maybe_get_value( "monsters_tagged" );
    REQUIRE( tagged_count != nullptr );
    CHECK( tagged_count->dbl() == 2 );

    int npcs = 0;
    for( npc &guy : g->all_npcs() ) {
        npcs++;
        const diag_value *tag = guy.maybe_get_value( "quest_tag" );
        REQUIRE( tag != nullptr );
        CHECK( tag->str() == "bandit" );
        const diag_value *marker = guy.maybe_get_value( "spawned_marker" );
        REQUIRE( marker != nullptr );
        CHECK( marker->dbl() == 7 );
    }
    CHECK( npcs == 1 );

    // The cleared avatar has no pocket for the rock, so it is dropped at their feet.
    std::vector<const item *> rocks;
    for( const item *it : u.items_with( []( const item & i ) {
    return i.typeId() == itype_rock;
    } ) ) {
        rocks.push_back( it );
    }
    for( const item &it : get_map().i_at( u.pos_bub() ) ) {
        if( it.typeId() == itype_rock ) {
            rocks.push_back( &it );
        }
    }
    REQUIRE( rocks.size() == 1 );
    CHECK( rocks.front()->get_var( "quest_tag" ) == "loot" );
}
