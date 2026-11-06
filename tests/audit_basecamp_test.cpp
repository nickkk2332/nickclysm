#include <list>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "basecamp.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "clzones.h"
#include "coordinates.h"
#include "faction.h"
#include "faction_camp.h"
#include "game_constants.h"
#include "item.h"
#include "item_location.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_scale_constants.h"
#include "map_selector.h"
#include "memory_fast.h"
#include "mission_companion.h"
#include "npc.h"
#include "overmapbuffer.h"
#include "player_helpers.h"
#include "point.h"
#include "stomach.h"
#include "type_id.h"
#include "visitable.h"

static const faction_id faction_your_followers( "your_followers" );

static const furn_str_id furn_f_fireplace( "f_fireplace" );
static const furn_str_id furn_f_plant_seed( "f_plant_seed" );

static const itype_id itype_fake_fireplace( "fake_fireplace" );
static const itype_id itype_seed_hops( "seed_hops" );
static const itype_id itype_tinder( "tinder" );

static const skill_id skill_menial( "menial" );
static const skill_id skill_survival( "survival" );

static const ter_str_id ter_t_dirtmound( "t_dirtmound" );

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

static int count_at( const tripoint_bub_ms &p, const itype_id &type )
{
    int found = 0;
    for( const item &it : get_map().i_at( p ) ) {
        if( it.typeId() == type ) {
            found += it.count();
        }
    }
    return found;
}

static int count_carried( const Character &who, const itype_id &type )
{
    int found = 0;
    who.visit_items( [&found, &type]( const item_location & it ) {
        if( it->typeId() == type ) {
            found += it->count();
        }
        return VisitResponse::NEXT;
    } );
    return found;
}

static int count_on_mission( npc &comp, const itype_id &type )
{
    int found = 0;
    for( const item &it : comp.companion_mission_inv ) {
        if( it.typeId() == type ) {
            found += it.count();
        }
    }
    return found;
}

// ends the worker's mission, so later tests don't find it waiting at the same camp
static void call_off_mission( npc &comp )
{
    comp.reset_companion_mission();
    comp.companion_mission_inv.clear();
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

// CAMP-B02: every opening of the camp board re-ran reset_camp_resources, and add_resource
// appended a new entry each time.  Each entry is a pseudo tool loaded with the whole fuel
// stock, so after K openings a camp tool could draw K times the fuel in storage.
TEST_CASE( "camp-b02_camp_board_openings_do_not_multiply_camp_fuel", "[audit][camp]" )
{
    clear_avatar();
    clear_map_without_vision();
    get_player_character().camps.clear();
    map &here = get_map();
    tripoint_abs_omt camp_omt;
    basecamp &camp = place_audit_camp( "faction_base_bare_bones_NPC_camp_0", camp_omt );
    on_out_of_scope cleanup( [&camp_omt]() {
        overmap_buffer.clear_camps( camp_omt.xy() );
    } );
    const tripoint_bub_ms storage = camp_mid + tripoint::east;
    here.i_clear( storage );
    here.add_item_or_charges( storage, item( itype_tinder, calendar::turn, 100 ) );
    camp.set_storage_tiles( { here.get_abs( storage ) } );
    // a fireplace in the camp gives it a camp fireplace that burns tinder
    here.furn_set( camp_mid + tripoint::west * 2, furn_f_fireplace );
    camp.scan_pseudo_items();

    for( int opened = 0; opened < 3; ++opened ) {
        mission_data missions;
        camp.get_available_missions( missions, here );
    }

    int wanted = 300;
    int drawn = 0;
    for( const item &fuel : camp.use_charges( itype_fake_fireplace, wanted ) ) {
        drawn += fuel.charges;
    }
    CHECK( drawn == 100 );
    CHECK( wanted == 200 );
}

// CAMP-B02: a legacy expansion (a save with cur_level and no provides) gets its levels'
// provides from reset_camp_resources.  They must be added once, not again on every opening,
// and the loader's own entry for level 0 must not hide the rest of what level 0 provides.
TEST_CASE( "camp-b02_legacy_camp_levels_are_counted_once", "[audit][camp]" )
{
    clear_avatar();
    clear_map_without_vision();
    get_player_character().camps.clear();
    map &here = get_map();
    tripoint_abs_omt camp_omt;
    basecamp &camp = place_audit_camp( "faction_base_bare_bones_NPC_camp_0", camp_omt );
    on_out_of_scope cleanup( [&camp_omt]() {
        overmap_buffer.clear_camps( camp_omt.xy() );
    } );
    std::ostringstream os;
    JsonOut jsout( os );
    jsout.start_object();
    jsout.member( "expansions" );
    jsout.start_array();
    jsout.start_object();
    jsout.member( "dir", base_camps::base_dir );
    jsout.member( "type", "bare_bones_NPC_camp" );
    jsout.member( "cur_level", 0 );
    jsout.member( "pos", camp_omt );
    jsout.end_object();
    jsout.end_array();
    jsout.end_object();
    camp.deserialize( json_loader::from_string( os.str() ).get_object() );
    // the loader adds the level-0 blueprint itself and nothing else
    REQUIRE( camp.has_provides( "faction_base_bare_bones_NPC_camp_0", base_camps::base_dir ) );
    REQUIRE_FALSE( camp.has_provides( "gathering", base_camps::base_dir ) );

    for( int opened = 0; opened < 3; ++opened ) {
        mission_data missions;
        camp.get_available_missions( missions, here );
    }

    CHECK( camp.has_provides( "gathering", base_camps::base_dir ) );
    CHECK_FALSE( camp.has_provides( "gathering", base_camps::base_dir, 1 ) );
}

// CAMP-B03: start_farm_op cut the selected seed stack down to the chosen count before anyone
// set off, so a cancelled or unfed mission destroyed the rest.  When a companion did go,
// individual_mission also used up the companion's own seeds of that type, and start_mission
// took the same count again by type from a tile list that began with the bubble's corner
// tile.  Only the chosen seeds may leave storage, and only once a companion sets off.
TEST_CASE( "camp-b03_camp_planting_takes_only_the_chosen_seeds", "[audit][camp]" )
{
    clear_avatar();
    clear_map_without_vision();
    get_player_character().camps.clear();
    map &here = get_map();
    tripoint_abs_omt camp_omt;
    basecamp &camp = place_audit_camp( "faction_base_bare_bones_NPC_camp_0", camp_omt );
    faction &fac = *get_player_character().get_faction();
    const npc_ptr comp = spawn_camp_worker( camp_mid.xy() + point::south );
    on_out_of_scope cleanup( [&camp_omt, &fac, &comp]() {
        call_off_mission( *comp );
        overmap_buffer.clear_camps( camp_omt.xy() );
        fac.empty_food_supply();
    } );
    stock_audit_larder( fac, 10000 );
    const tripoint_bub_ms storage = camp_mid + tripoint::east;
    const tripoint_bub_ms corner{ 0, 0, 0 };
    here.i_clear( storage );
    here.i_clear( corner );
    std::vector<item *> stored;
    stored.reserve( 5 );
    for( int i = 0; i < 5; ++i ) {
        stored.push_back( &here.add_item( storage, item( itype_seed_hops ) ) );
    }
    here.add_item( corner, item( itype_seed_hops ) );
    camp.set_storage_tiles( { here.get_abs( storage ) } );
    comp->i_add( item( itype_seed_hops ) );
    REQUIRE( count_at( storage, itype_seed_hops ) == 5 );
    REQUIRE( count_at( corner, itype_seed_hops ) == 1 );
    REQUIRE( count_carried( *comp, itype_seed_hops ) == 1 );
    const mission_id plant{ Camp_Plant, "", {}, base_camps::base_dir };
    const drop_locations two_stored{
        { item_location( map_cursor( storage ), stored[0] ), 1 },
        { item_location( map_cursor( storage ), stored[1] ), 1 }
    };

    WHEN( "a camp mission is handed copies of two seeds" ) {
        item seed_copy( itype_seed_hops );
        const std::vector<item *> equipment{ &seed_copy, &seed_copy };
        REQUIRE( camp.start_mission( plant, 2_minutes, true, "", false, equipment,
                                     std::map<skill_id, int>(), LIGHT_EXERCISE, 0_hours, comp ) );
        THEN( "companion carries the copies and nothing else is taken" ) {
            CHECK( count_on_mission( *comp, itype_seed_hops ) == 2 );
            CHECK( count_carried( *comp, itype_seed_hops ) == 1 );
            CHECK( count_at( storage, itype_seed_hops ) == 5 );
            CHECK( count_at( corner, itype_seed_hops ) == 1 );
        }
    }
    WHEN( "two of the stored seeds are sent to be planted" ) {
        REQUIRE( talk_function::camp_start_planting( camp, plant, two_stored, LIGHT_EXERCISE,
                 comp ) );
        THEN( "just those two leave storage, with the companion" ) {
            CHECK( count_on_mission( *comp, itype_seed_hops ) == 2 );
            CHECK( count_carried( *comp, itype_seed_hops ) == 1 );
            CHECK( count_at( storage, itype_seed_hops ) == 3 );
            CHECK( count_at( corner, itype_seed_hops ) == 1 );
        }
    }
    WHEN( "there is no food to send a planter" ) {
        fac.empty_food_supply();
        CHECK_FALSE( talk_function::camp_start_planting( camp, plant, two_stored, LIGHT_EXERCISE,
                     comp ) );
        THEN( "every seed stays where it was" ) {
            CHECK( count_on_mission( *comp, itype_seed_hops ) == 0 );
            CHECK( count_carried( *comp, itype_seed_hops ) == 1 );
            CHECK( count_at( storage, itype_seed_hops ) == 5 );
            CHECK( count_at( corner, itype_seed_hops ) == 1 );
        }
    }
}

// CAMP-B03, stacks: no core seed counts by charges, so a stack of tinder stands in for a
// stack of seeds.  The old code set the stack to the chosen count before the mission started.
TEST_CASE( "camp-b03_camp_planting_leaves_the_rest_of_a_stack", "[audit][camp]" )
{
    clear_avatar();
    clear_map_without_vision();
    get_player_character().camps.clear();
    map &here = get_map();
    tripoint_abs_omt camp_omt;
    basecamp &camp = place_audit_camp( "faction_base_bare_bones_NPC_camp_0", camp_omt );
    faction &fac = *get_player_character().get_faction();
    const npc_ptr comp = spawn_camp_worker( camp_mid.xy() + point::south );
    on_out_of_scope cleanup( [&camp_omt, &fac, &comp]() {
        call_off_mission( *comp );
        overmap_buffer.clear_camps( camp_omt.xy() );
        fac.empty_food_supply();
    } );
    stock_audit_larder( fac, 10000 );
    const tripoint_bub_ms storage = camp_mid + tripoint::east;
    here.i_clear( storage );
    item &stack = here.add_item( storage, item( itype_tinder, calendar::turn, 50 ) );
    camp.set_storage_tiles( { here.get_abs( storage ) } );
    REQUIRE( stack.count_by_charges() );
    REQUIRE( count_at( storage, itype_tinder ) == 50 );
    const mission_id plant{ Camp_Plant, "", {}, base_camps::base_dir };
    const drop_locations ten_of_fifty{ { item_location( map_cursor( storage ), &stack ), 10 } };

    WHEN( "10 of the 50 are sent with a companion" ) {
        REQUIRE( talk_function::camp_start_planting( camp, plant, ten_of_fifty, LIGHT_EXERCISE,
                 comp ) );
        THEN( "10 go and 40 stay" ) {
            CHECK( count_on_mission( *comp, itype_tinder ) == 10 );
            CHECK( count_at( storage, itype_tinder ) == 40 );
        }
    }
    WHEN( "there is no food to send a planter" ) {
        fac.empty_food_supply();
        CHECK_FALSE( talk_function::camp_start_planting( camp, plant, ten_of_fifty,
                     LIGHT_EXERCISE, comp ) );
        THEN( "all 50 stay" ) {
            CHECK( count_at( storage, itype_tinder ) == 50 );
        }
    }
}

// CAMP-B04: farm_return gave every unplanted item with charges to the player, then
// finish_return also put everything still in the companion's mission inventory in camp
// storage, so each leftover stack came back twice.  Leftovers must come back once, to camp.
TEST_CASE( "camp-b04_unplanted_seeds_come_back_once", "[audit][camp]" )
{
    clear_avatar();
    clear_map_without_vision();
    get_player_character().camps.clear();
    restore_on_out_of_scope restore_turn( calendar::turn );
    map &here = get_map();
    tripoint_abs_omt camp_omt;
    basecamp &camp = place_audit_camp( "faction_base_bare_bones_NPC_camp_0", camp_omt );
    faction &fac = *get_player_character().get_faction();
    const npc_ptr comp = spawn_camp_worker( camp_mid.xy() + point::south );
    on_out_of_scope cleanup( [&camp_omt, &fac, &comp]() {
        call_off_mission( *comp );
        overmap_buffer.clear_camps( camp_omt.xy() );
        fac.empty_food_supply();
    } );
    stock_audit_larder( fac, 10000 );
    // one plot ready for planting, in the camp's own tile
    const tripoint_bub_ms plot = camp_mid + tripoint::west * 2;
    here.ter_set( plot, ter_t_dirtmound );
    const mission_id plant{ Camp_Plant, "", {}, base_camps::base_dir };
    // two seeds for the one plot, and a stack that counts by charges that is left over (no
    // core seed counts by charges; tinder stands in for such a stack of seeds)
    item seed( itype_seed_hops );
    item stack( itype_tinder, calendar::turn, 10 );
    const std::vector<item *> equipment{ &seed, &seed, &stack };
    REQUIRE( camp.start_mission( plant, 12_minutes, true, "", false, equipment,
                                 std::map<skill_id, int>(), LIGHT_EXERCISE, 0_hours, comp ) );
    calendar::turn += 12_minutes;
    REQUIRE( camp.farm_return( plant, base_camps::base_dir ) );

    // carried by the player or anywhere on the map except the plot
    const auto count_back = [&here, &plot]( const itype_id & type ) {
        int found = count_carried( get_player_character(), type );
        for( const tripoint_bub_ms &p : here.points_on_zlevel( 0 ) ) {
            if( p != plot ) {
                found += count_at( p, type );
            }
        }
        return found;
    };
    CHECK( here.furn( plot ) == furn_f_plant_seed );
    CHECK( count_back( itype_seed_hops ) == 1 );
    CHECK( count_back( itype_tinder ) == 10 );
    CHECK( count_carried( get_player_character(), itype_tinder ) == 0 );
    CHECK( comp->companion_mission_inv.empty() );
}
