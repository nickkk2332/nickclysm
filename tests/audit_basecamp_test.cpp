#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "basecamp.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "clzones.h"
#include "coordinates.h"
#include "faction.h"
#include "game_constants.h"
#include "map.h"
#include "map_helpers.h"
#include "map_scale_constants.h"
#include "memory_fast.h"
#include "mission_companion.h"
#include "npc.h"
#include "overmapbuffer.h"
#include "player_helpers.h"
#include "point.h"
#include "stomach.h"
#include "type_id.h"

static const faction_id faction_your_followers( "your_followers" );

static const skill_id skill_menial( "menial" );
static const skill_id skill_survival( "survival" );

static constexpr tripoint_bub_ms camp_mid{ MAPSIZE_X / 2, MAPSIZE_Y / 2, 0 };

// player-owned camp at mid-map; caller removes it with clear_camps
static basecamp &place_audit_camp( std::string_view camp_type, tripoint_abs_omt &camp_omt )
{
    map &here = get_map();
    camp_omt = project_to<coords::omt>( here.get_abs( camp_mid ) );
    here.add_camp( camp_omt, "faction_camp" );
    std::optional<basecamp *> bcp = overmap_buffer.find_camp( camp_omt.xy() );
    REQUIRE( bcp );
    basecamp &camp = **bcp;
    camp.set_owner( your_fac );
    camp.define_camp( camp_omt, camp_type, false );
    return camp;
}

static void stock_audit_larder( faction &fac, const int kcal )
{
    fac.empty_food_supply();
    nutrients stock;
    stock.calories = kcal * 1000;
    fac.debug_food_supply().emplace_back( calendar::turn_zero, stock );
    REQUIRE( fac.food_supply().kcal() == kcal );
}

// follower who can be sent on camp missions, as the overmap buffer holds it
static npc_ptr spawn_camp_worker( const point_bub_ms &where )
{
    npc &guy = spawn_npc( where, "test_talker" );
    clear_character( guy, true );
    guy.set_fac( faction_your_followers );
    guy.stomach.empty();
    guy.guts.empty();
    const npc_ptr comp = overmap_buffer.find_npc( guy.getID() );
    REQUIRE( comp );
    return comp;
}

// CAMP-B01: missions paid for their whole planned time when they set off, then finish_return
// billed the elapsed time again for every mission that is not fixed_time (gathering,
// crafting, upgrades, emergency recall).  Only time past the planned time may be billed.
TEST_CASE( "camp-b01_camp_mission_food_is_billed_once", "[audit][camp]" )
{
    clear_avatar();
    clear_map_without_vision();
    get_player_character().camps.clear();
    restore_on_out_of_scope restore_turn( calendar::turn );
    tripoint_abs_omt camp_omt;
    basecamp &camp = place_audit_camp( "faction_base_bare_bones_NPC_camp_0", camp_omt );
    faction &fac = *get_player_character().get_faction();
    on_out_of_scope cleanup( [&camp_omt, &fac]() {
        overmap_buffer.clear_camps( camp_omt.xy() );
        fac.empty_food_supply();
    } );
    stock_audit_larder( fac, 10000 );
    const npc_ptr comp = spawn_camp_worker( camp_mid.xy() );

    const mission_id gather{ Camp_Gather_Materials, "", {}, base_camps::base_dir };
    const int prepaid = camp.time_to_food( 3_hours, LIGHT_EXERCISE );
    REQUIRE( prepaid > 0 );
    REQUIRE( camp.start_mission( gather, 3_hours, true, "", false, {},
                                 std::map<skill_id, int>(), LIGHT_EXERCISE, 0_hours, comp ) );
    REQUIRE( fac.food_supply().kcal() == 10000 - prepaid );

    WHEN( "worker is back when the trip was planned to end" ) {
        calendar::turn += 3_hours;
        camp.finish_return( *comp, false, "returns.", skill_survival.str(), 1 );
        THEN( "larder paid only when the worker set off" ) {
            CHECK( fac.food_supply().kcal() == 10000 - prepaid );
        }
    }
    WHEN( "worker is back three hours after the trip was planned to end" ) {
        calendar::turn += 6_hours;
        camp.finish_return( *comp, false, "returns.", skill_survival.str(), 1 );
        THEN( "larder paid for the six hours, no more" ) {
            const int six_hours = camp.time_to_food( 6_hours, LIGHT_EXERCISE );
            CHECK( fac.food_supply().kcal() == 10000 - six_hours );
        }
    }
    WHEN( "worker is recalled an hour in" ) {
        calendar::turn += 1_hours;
        camp.finish_return( *comp, false, "responds.", skill_menial.str(), 0, true );
        THEN( "nothing more is taken from the larder" ) {
            CHECK( fac.food_supply().kcal() == 10000 - prepaid );
        }
    }
}
