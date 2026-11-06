// Behavior the performance fixes for INV-P02, MGEN-P01, OMAP-P02 and ACT-P03 must keep.
// These tests pass on the code before those fixes too.

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "activity_actor_definitions.h"
#include "activity_handlers.h"
#include "activity_item_handling.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "clzones.h"
#include "coordinates.h"
#include "game.h"
#include "item.h"
#include "item_location.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "map_memory.h"
#include "map_selector.h"
#include "mapbuffer.h"
#include "mapdata.h"
#include "memory_fast.h"
#include "messages.h"
#include "omdata.h"
#include "overmap.h"
#include "overmapbuffer.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "point.h"
#include "requirement_scan_memo.h"
#include "requirements.h"
#include "rng.h"
#include "string_formatter.h"
#include "submap.h"
#include "trap.h"
#include "type_id.h"
#include "units.h"
#include "veh_type.h"
#include "vehicle.h"
#include "visitable.h"
#include "weather.h"
#include "weather_gen.h"

static const activity_id ACT_FETCH_REQUIRED( "ACT_FETCH_REQUIRED" );
static const activity_id ACT_MULTIPLE_FARM( "ACT_MULTIPLE_FARM" );
static const activity_id ACT_VEHICLE_DECONSTRUCTION( "ACT_VEHICLE_DECONSTRUCTION" );

static const faction_id faction_your_followers( "your_followers" );

static const itype_id itype_backpack( "backpack" );
static const itype_id itype_bag_plastic( "bag_plastic" );
static const itype_id itype_hoe( "hoe" );
static const itype_id itype_rock( "rock" );
static const itype_id itype_scrap( "scrap" );
static const itype_id itype_seed_buckwheat( "seed_buckwheat" );

static const oter_str_id oter_test_forest_very_thick( "test_forest_very_thick" );

static const quality_id qual_HAMMER( "HAMMER" );

static const ter_str_id ter_t_dirt( "t_dirt" );
static const ter_str_id ter_t_dirtmound( "t_dirtmound" );
static const ter_str_id ter_t_grass( "t_grass" );
static const ter_str_id ter_t_pseudo_phase( "t_pseudo_phase" );
static const ter_str_id ter_t_water_dp( "t_water_dp" );
static const ter_str_id ter_t_water_moving_sh( "t_water_moving_sh" );
static const ter_str_id ter_t_water_sh( "t_water_sh" );

static const vpart_id vpart_frame( "frame" );
static const vpart_id vpart_test_vp_reserve_welder( "test_vp_reserve_welder" );
static const vpart_id vpart_test_vp_weld_to_remove( "test_vp_weld_to_remove" );

static const vproto_id vehicle_prototype_none( "none" );

static const zone_type_id zone_type_FARM_PLOT( "FARM_PLOT" );
static const zone_type_id zone_type_LOOT_UNSORTED( "LOOT_UNSORTED" );
static const zone_type_id zone_type_VEHICLE_DECONSTRUCT( "VEHICLE_DECONSTRUCT" );

namespace
{

// Restores calendar::turn when it goes out of scope.
struct restore_turn {
    time_point saved = calendar::turn;
    ~restore_turn() {
        calendar::turn = saved;
    }
};

item_location find_carried( const Character &u, const itype_id &type )
{
    item_location found;
    u.visit_items( [&found, &type]( const item_location & loc ) {
        if( loc->typeId() == type ) {
            found = loc;
            return VisitResponse::ABORT;
        }
        return VisitResponse::NEXT;
    } );
    return found;
}

} // namespace

// ---------------------------------------------------------------------------------------
// INV-P02: item_location

TEST_CASE( "inv-p02_location_of_a_destroyed_item_is_invalid", "[audit][item_location]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    u.wear_item( item( itype_backpack ), false );
    item bag_item( itype_bag_plastic );
    REQUIRE( bag_item.put_in( item( itype_rock ), pocket_type::CONTAINER ).success() );
    item_location bag = u.i_add( bag_item );
    REQUIRE( bag );
    item_location rock = find_carried( u, itype_rock );
    REQUIRE( rock );
    REQUIRE( rock.where() == item_location::type::container );
    CHECK( rock.parent_item() == bag );
    const item_location copy = rock;
    REQUIRE( copy );

    // destroying the item invalidates every location of it
    CHECK( bag.remove_items_with( []( const item & it ) {
        return it.typeId() == itype_rock;
    } ).size() == 1 );
    CHECK_FALSE( rock );
    CHECK_FALSE( copy );
    CHECK_FALSE( rock.valid() );
    CHECK( rock.get_item() == nullptr );
    // it still knows what kind of place the item was in
    CHECK( rock.where() == item_location::type::container );
    CHECK( bag );

    // the same on the map
    clear_map();
    map &here = get_map();
    const tripoint_bub_ms spot( 60, 61, 0 );
    item &on_map = here.add_item( spot, item( itype_rock ) );
    const item_location map_loc( map_cursor( here.get_abs( spot ) ), &on_map );
    REQUIRE( map_loc );
    REQUIRE( map_loc.where() == item_location::type::map );
    here.i_clear( spot );
    CHECK_FALSE( map_loc );
    CHECK( map_loc.get_item() == nullptr );
    clear_avatar();
}

TEST_CASE( "inv-p02_empty_locations_are_nowhere_and_independent", "[audit][item_location]" )
{
    const item_location a;
    item_location b;
    CHECK_FALSE( a );
    CHECK_FALSE( a.valid() );
    CHECK( a.where() == item_location::type::invalid );
    CHECK( a.where_recursive() == item_location::type::invalid );
    CHECK( a.get_item() == nullptr );
    CHECK( a.carrier() == nullptr );
    CHECK_FALSE( a.has_parent() );
    CHECK( a == b );
    CHECK( a == item_location::nowhere );
    CHECK_FALSE( item_location::nowhere );

    clear_avatar();
    avatar &u = get_avatar();
    u.wear_item( item( itype_backpack ), false );
    REQUIRE_FALSE( u.get_wielded_item() );
    CHECK( u.get_wielded_item().where() == item_location::type::invalid );
    CHECK( u.get_wielded_item() == item_location::nowhere );

    REQUIRE( u.wield( *u.i_add( item( itype_rock ) ) ) );
    item_location wielded = u.get_wielded_item();
    REQUIRE( wielded );
    CHECK( wielded.where() == item_location::type::character );
    CHECK( wielded->typeId() == itype_rock );
    CHECK( wielded.carrier() == &u );

    // removing the item through a location makes that location nowhere, and leaves other
    // empty locations as they were
    wielded.remove_item();
    CHECK_FALSE( wielded );
    CHECK( wielded.where() == item_location::type::invalid );
    CHECK_FALSE( u.get_wielded_item() );
    CHECK_FALSE( a );
    CHECK( item_location::nowhere.where() == item_location::type::invalid );

    // an empty location can be pointed at an item afterwards
    b = u.i_add( item( itype_scrap ) );
    CHECK( b );
    CHECK( b->typeId() == itype_scrap );
    CHECK_FALSE( a );
    CHECK_FALSE( item_location::nowhere );
    clear_avatar();
}

TEST_CASE( "inv-p02_inventory_queries_see_nested_items", "[audit][visitable]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    u.wear_item( item( itype_backpack ), false );
    for( int i = 0; i < 5; ++i ) {
        u.i_add( item( itype_rock ) );
    }
    for( int i = 0; i < 2; ++i ) {
        item bag( itype_bag_plastic );
        REQUIRE( bag.put_in( item( itype_rock ), pocket_type::CONTAINER ).success() );
        u.i_add( bag );
    }
    REQUIRE( item( itype_rock ).get_quality( qual_HAMMER ) == 1 );

    // the same counts, walked item by item from the top-level items without locations
    int rocks = 0;
    int nodes = 0;
    int nested_rocks = 0;
    for( const item *top : static_cast<const Character &>( u ).inv_dump() ) {
        top->visit_items( [&]( const item * it, const item * parent ) {
            ++nodes;
            if( it->typeId() == itype_rock ) {
                ++rocks;
                nested_rocks += parent != nullptr && parent->typeId() == itype_bag_plastic ? 1 : 0;
            }
            return VisitResponse::NEXT;
        } );
    }
    REQUIRE( rocks > 2 );
    REQUIRE( nested_rocks > 0 );

    CHECK( u.amount_of( itype_rock ) == rocks );
    CHECK( u.amount_of( itype_rock, true, 2 ) == 2 );
    CHECK( u.has_amount( itype_rock, rocks ) );
    CHECK_FALSE( u.has_amount( itype_rock, rocks + 1 ) );
    CHECK( u.has_quality( qual_HAMMER ) );
    CHECK( u.has_quality( qual_HAMMER, 1, rocks ) );
    CHECK( u.max_quality( qual_HAMMER ) == 1 );
    CHECK( static_cast<int>( u.items_with( []( const item & it ) {
        return it.typeId() == itype_rock;
    } ).size() ) == rocks );
    CHECK( u.has_item_with( []( const item & it ) {
        return it.typeId() == itype_bag_plastic;
    } ) );
    CHECK_FALSE( u.has_item_with( []( const item & it ) {
        return it.typeId() == itype_scrap;
    } ) );

    // every visited location is valid and knows its container
    int visited = 0;
    int in_bags = 0;
    u.visit_items( [&]( const item_location & loc ) {
        ++visited;
        CHECK( loc );
        if( loc.where() == item_location::type::container ) {
            const item_location parent = loc.parent_item();
            CHECK( parent );
            CHECK( parent->has_item( *loc ) );
            in_bags += parent->typeId() == itype_bag_plastic ? 1 : 0;
        } else {
            CHECK( loc.where() == item_location::type::character );
        }
        CHECK( loc.carrier() == &u );
        return VisitResponse::NEXT;
    } );
    CHECK( visited == nodes );
    CHECK( in_bags == nested_rocks );
    clear_avatar();
}

// ---------------------------------------------------------------------------------------
// MGEN-P01: phase change on submap load

namespace
{

// What the phase change pass leaves on a tile: its terrain, and the terrain recorded under it.
struct phase_outcome {
    ter_id terrain;
    std::optional<ter_id> original;
};

// The outcome on one tile, worked out as the pass before MGEN-P01 did it: revert to the
// recorded original terrain (which drops the record unless the tile already showed it), then
// freeze or thaw by the 10-day average temperature at 08:00.
phase_outcome old_phase_outcome( map &here, const tripoint_bub_ms &p,
                                 const weather_generator &wgen )
{
    const ter_id cur = here.ter( p );
    const bool had_original = here.has_original_terrain_at( p );
    const ter_id base = had_original ? here.get_original_terrain_at( p ) : cur;
    std::optional<ter_id> original;
    if( had_original && base == cur ) {
        original = base;
    }
    const ter_t &tt = base.obj();
    if( tt.phase_method != "water_freeze" ) {
        return { base, original };
    }
    const tripoint_abs_ms abs_p = here.get_abs( p );
    const unsigned seed = static_cast<unsigned>( g->get_seed() ) ^
                          static_cast<unsigned>( abs_p.x() * 1103515245u + abs_p.y() );
    units::temperature sum = 0_K;
    for( int d = 0; d < 10; d++ ) {
        const time_point base_date = calendar::turn - ( d + 1 ) * 1_days;
        const time_duration time_of_day = ( base_date - calendar::turn_zero ) % 1_days;
        const time_point t = base_date - time_of_day + 8_hours;
        sum += wgen.get_weather_temperature( abs_p, t, seed );
    }
    const units::temperature standard = sum / 10.0;

    ter_str_id chosen = ter_t_pseudo_phase;
    const size_t targ_n = tt.phase_targets.size();
    const size_t temp_n = tt.phase_temps.size();
    if( targ_n > 0 && temp_n + 1 == targ_n ) {
        std::vector<ter_str_id> targets;
        targets.reserve( tt.phase_targets.size() );
        for( const ter_str_id &target : tt.phase_targets ) {
            targets.push_back( target == ter_t_pseudo_phase ? base.id() : target );
        }
        if( standard < tt.phase_temps[0] ) {
            chosen = targets[0];
        } else {
            bool found = false;
            for( size_t i = 0; i + 1 < temp_n; ++i ) {
                if( standard >= tt.phase_temps[i] && standard <= tt.phase_temps[i + 1] ) {
                    chosen = targets[i + 1];
                    found = true;
                    break;
                }
            }
            if( !found && standard > tt.phase_temps.back() ) {
                chosen = targets.back();
            }
        }
    }
    if( chosen != ter_t_pseudo_phase && chosen != base.id() ) {
        return { chosen.id(), base };
    }
    return { base, original };
}

std::optional<ter_id> recorded_original( map &here, const tripoint_bub_ms &p )
{
    if( here.has_original_terrain_at( p ) ) {
        return here.get_original_terrain_at( p );
    }
    return std::nullopt;
}

std::vector<tripoint_bub_ms> omt_surface( const tripoint_abs_omt &omt )
{
    map &here = get_map();
    const tripoint_abs_ms origin = project_to<coords::ms>( omt );
    std::vector<tripoint_bub_ms> tiles;
    for( int y = 0; y < 2 * SEEY; ++y ) {
        for( int x = 0; x < 2 * SEEX; ++x ) {
            tiles.push_back( here.get_bub( origin + tripoint_rel_ms( x, y, 0 ) ) );
        }
    }
    return tiles;
}

void load_smallmap( const tripoint_abs_omt &omt )
{
    smallmap sm;
    sm.load( omt, false );
}

time_point noon_in_season( season_type season, int days_in )
{
    time_point t = calendar::turn_zero + 12_hours;
    while( season_of_year( t ) != season ) {
        t += 1_days;
    }
    return t + days_in * 1_days;
}

// Two overmap tiles of the reality bubble: one of deep lake, one mixing shallow, moving and
// deep water with dry land.
std::vector<tripoint_abs_omt> lake_setup()
{
    map &here = get_map();
    const tripoint_abs_omt base = project_to<coords::omt>( here.get_abs_sub() );
    const tripoint_abs_omt lake = base + tripoint_rel_omt( 2, 2, 0 );
    const tripoint_abs_omt shore = base + tripoint_rel_omt( 3, 2, 0 );
    for( const tripoint_bub_ms &p : omt_surface( lake ) ) {
        here.ter_set( p, ter_t_water_dp );
    }
    int i = 0;
    for( const tripoint_bub_ms &p : omt_surface( shore ) ) {
        const ter_str_id kinds[] = { ter_t_water_sh, ter_t_water_moving_sh, ter_t_water_dp, ter_t_grass };
        here.ter_set( p, kinds[( i++ / 5 ) % 4] );
    }
    return { lake, shore };
}

} // namespace

TEST_CASE( "mgen-p01_submap_loads_freeze_and_thaw_as_before", "[audit][mapgen]" )
{
    restore_turn turn_guard;
    clear_avatar();
    clear_map();
    get_avatar().setpos( get_map(), tripoint_bub_ms( 6, 6, 0 ) );
    map &here = get_map();
    const weather_generator &wgen = get_weather().get_cur_weather_gen();
    const std::vector<tripoint_abs_omt> omts = lake_setup();

    // a year of loads: the lake freezes over the winter and thaws again, the state carrying
    // over from one load to the next
    std::vector<time_point> days;
    for( int d = 0; d < 91; d += 6 ) {
        days.push_back( noon_in_season( WINTER, d ) );
        // reload the same day too
        days.push_back( noon_in_season( WINTER, d ) );
    }
    days.push_back( noon_in_season( SPRING, 40 ) );
    days.push_back( noon_in_season( SUMMER, 40 ) );
    int frozen_loads = 0;
    int thin = 0;
    for( const time_point &day : days ) {
        calendar::turn = day;
        for( const tripoint_abs_omt &omt : omts ) {
            const std::vector<tripoint_bub_ms> tiles = omt_surface( omt );
            std::vector<phase_outcome> expected;
            expected.reserve( tiles.size() );
            for( const tripoint_bub_ms &p : tiles ) {
                expected.push_back( old_phase_outcome( here, p, wgen ) );
            }
            load_smallmap( omt );
            int mismatches = 0;
            bool any_frozen = false;
            for( size_t i = 0; i < tiles.size(); ++i ) {
                const ter_id now = here.ter( tiles[i] );
                if( now != expected[i].terrain ||
                    recorded_original( here, tiles[i] ) != expected[i].original ) {
                    ++mismatches;
                }
                if( expected[i].original ) {
                    any_frozen = true;
                    thin += now.id().str().find( "thin" ) != std::string::npos;
                }
            }
            CAPTURE( to_string( day ), omt.to_string() );
            CHECK( mismatches == 0 );
            frozen_loads += any_frozen;
        }
    }
    // the year did freeze the lake (this climate makes thin ice; thick ice needs colder)
    CHECK( frozen_loads > 0 );
    CHECK( thin > 0 );
    // and thawed it again
    for( const tripoint_bub_ms &p : omt_surface( omts.front() ) ) {
        CHECK( here.ter( p ) == ter_t_water_dp );
        CHECK_FALSE( here.has_original_terrain_at( p ) );
    }
    clear_map();
}

// Reloading a frozen tile thawed it and froze it again through two full terrain changes. What
// that left behind besides the same terrain is kept: the map damage reset, the submap marked as
// changed by the player, and a trap entry for the ice from the refreeze (besides the one the
// load registers for every terrain trap). The tile is no longer actually thawed and refrozen,
// which shows in the avatar's memory of it: a terrain change used to forget it.
TEST_CASE( "mgen-p01_reloading_frozen_ice_keeps_what_refreezing_left", "[audit][mapgen]" )
{
    restore_turn turn_guard;
    clear_avatar();
    clear_map();
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_abs_omt lake = lake_setup().front();
    const std::vector<tripoint_bub_ms> tiles = omt_surface( lake );
    // the corner of the lake, the avatar just outside it
    const tripoint_bub_ms corner = tiles.front();
    u.setpos( here, corner + tripoint_rel_ms( -1, -1, 0 ) );
    REQUIRE( here.ter( u.pos_bub() ) == ter_t_grass );

    // the first winter day that freezes the corner tile
    bool frozen = false;
    for( int d = 0; d < 91 && !frozen; ++d ) {
        calendar::turn = noon_in_season( WINTER, d );
        for( const tripoint_bub_ms &p : tiles ) {
            here.ter_set( p, ter_t_water_dp );
        }
        load_smallmap( lake );
        frozen = here.ter( corner ) != ter_t_water_dp;
    }
    REQUIRE( frozen );
    const ter_id ice = here.ter( corner );
    REQUIRE( here.get_original_terrain_at( corner ) == ter_t_water_dp );
    const trap_id ice_trap = ice->trap;
    REQUIRE( ice_trap != tr_null );
    REQUIRE( ter_t_water_dp->trap == tr_null );

    const tripoint_abs_ms abs_corner = here.get_abs( corner );
    const tripoint_abs_sm corner_sm = project_to<coords::sm>( abs_corner );
    submap *sm = MAPBUFFER.lookup_submap( corner_sm );
    REQUIRE( sm != nullptr );
    here.set_map_damage( corner, 7 );
    sm->player_adjusted_map = false;
    set_time( calendar::turn );
    REQUIRE( u.sees( here, corner ) );
    u.prepare_map_memory_region( abs_corner + tripoint_rel_ms( -2, -2, 0 ),
                                 abs_corner + tripoint_rel_ms( 2, 2, 0 ) );
    u.memorize_decoration( abs_corner, ice.id().str(), 0, 0 );
    REQUIRE( u.get_memorized_tile( abs_corner ).get_dec_id() == ice.id().str() );

    {
        smallmap reloaded;
        reloaded.load( lake, false );
        map &m = *reloaded.cast_to_map();
        const tripoint_bub_ms at = m.get_bub( abs_corner );
        CHECK( m.ter( at ) == ice );
        CHECK( m.get_original_terrain_at( at ) == ter_t_water_dp );
        CHECK( m.tr_at( at ).loadid == ice_trap );
        // one entry from refreezing the tile, one from the load registering terrain traps
        const std::vector<tripoint_bub_ms> &trap_locs = m.trap_locations( ice_trap );
        CHECK( std::count( trap_locs.begin(), trap_locs.end(), at ) == 2 );
    }
    CHECK( here.get_map_damage( corner ) == 0 );
    CHECK( sm->player_adjusted_map );
    // the avatar saw the tile, but it never changed, so the memory of it stays
    CHECK( u.get_memorized_tile( abs_corner ).get_dec_id() == ice.id().str() );

    // open water that stays open is left alone
    calendar::turn = noon_in_season( SUMMER, 40 );
    load_smallmap( lake );
    REQUIRE( here.ter( corner ) == ter_t_water_dp );
    REQUIRE_FALSE( here.has_original_terrain_at( corner ) );
    here.set_map_damage( corner, 3 );
    sm->player_adjusted_map = false;
    load_smallmap( lake );
    CHECK( here.ter( corner ) == ter_t_water_dp );
    CHECK( here.get_map_damage( corner ) == 3 );
    CHECK_FALSE( sm->player_adjusted_map );

    // and a uniform submap of open water stays uniform
    const tripoint_abs_sm far_sm = project_to<coords::sm>( here.get_abs( tiles.back() ) );
    submap *open_sm = MAPBUFFER.lookup_submap( far_sm );
    REQUIRE( open_sm != nullptr );
    submap open_water;
    open_water.set_all_ter( ter_t_water_dp.id(), true );
    *open_sm = std::move( open_water );
    REQUIRE( open_sm->is_uniform() );
    load_smallmap( lake );
    CHECK( open_sm->is_uniform() );
    CHECK( open_sm->get_ter( point_sm_ms( 5, 5 ) ) == ter_t_water_dp );
    clear_map();
    clear_avatar();
}

// ---------------------------------------------------------------------------------------
// OMAP-P02: find_closest, find_all and find_random give the same answers as before

namespace
{
namespace old_omt_search
{
// A copy of the searches before OMAP-P02 (with OMAP-B02's fix), for the terrain branch.

constexpr int default_search_range = OMAPX * 5;

bool is_findable_location( const tripoint_abs_omt &location, const omt_find_params &params )
{
    bool type_matches = false;
    if( params.existing_only ) {
        for( const std::pair<std::string, ot_match_type> &elem : params.types ) {
            type_matches = overmap_buffer.check_ot_existing( elem.first, elem.second, location );
            if( type_matches ) {
                break;
            }
        }
    } else {
        for( const std::pair<std::string, ot_match_type> &elem : params.types ) {
            type_matches = overmap_buffer.check_ot( elem.first, elem.second, location );
            if( type_matches ) {
                break;
            }
        }
    }
    if( !type_matches ) {
        return false;
    }

    if( params.must_see && overmap_buffer.seen( location ) == om_vision_level::unseen ) {
        return false;
    }
    if( params.cant_see && overmap_buffer.seen( location ) != om_vision_level::unseen ) {
        return false;
    }

    if( params.om_special ) {
        bool meets_om_special = false;
        if( params.existing_only ) {
            meets_om_special = overmap_buffer.check_overmap_special_type_existing( *params.om_special,
                               location );
        } else {
            meets_om_special = overmap_buffer.check_overmap_special_type( *params.om_special, location );
        }
        if( !meets_om_special ) {
            return false;
        }
    }

    return true;
}

tripoint_abs_omt find_closest( const tripoint_abs_omt &origin, const omt_find_params &params )
{
    if( params.min_distance == 0 && is_findable_location( origin, params ) ) {
        return origin;
    }
    const int min_dist = params.min_distance;
    const int max_dist = params.search_range ? params.search_range : default_search_range;

    std::vector<tripoint_abs_omt> result;
    int found_dist = std::numeric_limits<int>::max();
    for( const point_abs_omt &loc_xy : closest_points_first( origin.xy(), min_dist, max_dist ) ) {
        const int dist_xy = square_dist( origin.xy(), loc_xy );

        if( found_dist < dist_xy ) {
            break;
        }

        for( int z = params.min_z; z <= params.max_z; z++ ) {
            const tripoint_abs_omt loc( loc_xy, z );
            const int dist = square_dist( origin, loc );

            if( found_dist < dist ) {
                continue;
            }

            if( is_findable_location( loc, params ) ) {
                if( dist < found_dist ) {
                    result.clear();
                }
                found_dist = dist;
                result.push_back( loc );
            }
        }
    }
    return random_entry( result, tripoint_abs_omt::invalid );
}

std::vector<tripoint_abs_omt> find_all( const tripoint_abs_omt &origin,
                                        const omt_find_params &params )
{
    std::vector<tripoint_abs_omt> result;
    const int min_dist = params.min_distance;
    const int max_dist = params.search_range ? params.search_range : OMAPX;
    for( const tripoint_abs_omt &loc : closest_points_first( origin, min_dist, max_dist ) ) {
        if( is_findable_location( loc, params ) ) {
            result.push_back( loc );
        }
    }
    return result;
}

} // namespace old_omt_search
} // namespace

namespace
{

struct omt_query {
    tripoint_rel_omt offset;
    std::vector<std::pair<std::string, ot_match_type>> types;
    int range = 0;
    int min_distance = 0;
    bool must_see = false;
    bool cant_see = false;
    bool existing_only = true;
    int min_z = -OVERMAP_DEPTH;
    int max_z = OVERMAP_HEIGHT;
};

omt_find_params params_for( const omt_query &q )
{
    omt_find_params params;
    params.types = q.types;
    params.search_range = q.range;
    params.min_distance = q.min_distance;
    params.must_see = q.must_see;
    params.cant_see = q.cant_see;
    params.existing_only = q.existing_only;
    params.min_z = q.min_z;
    params.max_z = q.max_z;
    return params;
}

std::string describe( const omt_query &q )
{
    std::string types;
    for( const std::pair<std::string, ot_match_type> &t : q.types ) {
        types += string_format( "%s/%d ", t.first, static_cast<int>( t.second ) );
    }
    return string_format( "offset %s types %s range %d min %d see %d/%d existing %d z %d..%d",
                          q.offset.to_string(), types, q.range, q.min_distance, q.must_see, q.cant_see,
                          q.existing_only, q.min_z, q.max_z );
}

std::vector<omt_query> omt_queries()
{
    using types_t = std::vector<std::pair<std::string, ot_match_type>>;
    const std::vector<types_t> type_sets = {
        { { "field", ot_match_type::type } },
        { { "forest", ot_match_type::type } },
        { { "road", ot_match_type::type } },
        { { "road", ot_match_type::prefix } },
        { { "open_air", ot_match_type::exact } },
        { { "empty_rock", ot_match_type::exact } },
        { { "roof", ot_match_type::contains } },
        { { "house", ot_match_type::prefix }, { "s_", ot_match_type::prefix }, { "lake", ot_match_type::contains } },
        { { "road_end", ot_match_type::subtype }, { "road_straight", ot_match_type::subtype } },
        { { "bench_no_such_terrain", ot_match_type::type } },
        {},
    };
    const std::vector<tripoint_rel_omt> offsets = {
        tripoint_rel_omt( 0, 0, 0 ), tripoint_rel_omt( 17, -9, 0 ), tripoint_rel_omt( -40, 25, -1 ),
        tripoint_rel_omt( 5, 5, 2 ), tripoint_rel_omt( 80, -80, 0 )
    };
    std::vector<omt_query> queries;
    int n = 0;
    for( const types_t &types : type_sets ) {
        for( const tripoint_rel_omt &offset : offsets ) {
            omt_query q;
            q.offset = offset;
            q.types = types;
            // vary the other parameters with the query number
            const int ranges[] = { 1, 4, 12, 30 };
            q.range = ranges[n % 4];
            q.min_distance = n % 7 == 3 ? 2 : 0;
            q.must_see = n % 5 == 1;
            q.cant_see = n % 9 == 4;
            // near the overmap edge, only existing overmaps (no new ones generated)
            q.existing_only = std::abs( offset.x() ) > 60 || n % 2 == 0;
            switch( n % 4 ) {
                case 1:
                    q.min_z = offset.z();
                    q.max_z = offset.z();
                    break;
                case 2:
                    q.min_z = -2;
                    q.max_z = 1;
                    break;
                default:
                    break;
            }
            queries.push_back( q );
            ++n;
        }
    }
    // longer searches: a miss over a whole overmap width on the surface, a miss on every
    // level and the default range
    omt_query wide;
    wide.types = { { "bench_no_such_terrain", ot_match_type::prefix } };
    wide.range = OMAPX;
    wide.min_z = 0;
    wide.max_z = 0;
    queries.push_back( wide );
    wide.offset = tripoint_rel_omt( 30, -20, 0 );
    wide.range = 60;
    wide.min_z = -OVERMAP_DEPTH;
    wide.max_z = OVERMAP_HEIGHT;
    queries.push_back( wide );
    omt_query common;
    common.types = { { "field", ot_match_type::type } };
    common.offset = tripoint_rel_omt( 3, 3, 0 );
    queries.push_back( common );
    common.min_distance = 5;
    common.range = 70;
    common.existing_only = false;
    queries.push_back( common );
    return queries;
}

tripoint_abs_omt overmap_middle()
{
    const point_abs_om om = project_to<coords::om>( get_avatar().pos_abs_omt().xy() );
    return project_combine( om, tripoint_om_omt( OMAPX / 2, OMAPY / 2, 0 ) );
}

// Lets the player see a patch of the surface and one level down, and puts the old vision back
// on destruction.
struct revealed_patch {
    std::vector<std::pair<tripoint_abs_omt, om_vision_level>> saved;
    explicit revealed_patch( const tripoint_abs_omt &center ) {
        for( int z = -1; z <= 0; ++z ) {
            for( int dy = -6; dy <= 6; ++dy ) {
                for( int dx = -6; dx <= 6; ++dx ) {
                    const tripoint_abs_omt p( center.xy() + point_rel_omt( dx, dy ), z );
                    saved.emplace_back( p, overmap_buffer.seen( p ) );
                    if( ( dx + dy ) % 3 != 0 ) {
                        overmap_buffer.set_seen( p, om_vision_level::full );
                    }
                }
            }
        }
    }
    ~revealed_patch() {
        for( const std::pair<tripoint_abs_omt, om_vision_level> &s : saved ) {
            overmap_buffer.set_seen( s.first, s.second );
        }
    }
};

} // namespace

TEST_CASE( "omap-p02_terrain_searches_give_the_same_answers", "[audit][overmap]" )
{
    clear_avatar();
    const tripoint_abs_omt middle = overmap_middle();
    const revealed_patch patch( middle );
    const int overmaps_before = overmap_buffer.get_overmap_count();
    int found = 0;
    unsigned int seed = 11;
    for( const omt_query &q : omt_queries() ) {
        CAPTURE( describe( q ) );
        const tripoint_abs_omt origin = middle + q.offset;
        const omt_find_params params = params_for( q );

        rng_set_engine_seed( ++seed );
        const tripoint_abs_omt old_closest = old_omt_search::find_closest( origin, params );
        // NOLINTNEXTLINE(cata-determinism)
        const cata_default_random_engine old_rng = rng_get_engine();
        rng_set_engine_seed( seed );
        const tripoint_abs_omt new_closest = overmap_buffer.find_closest( origin, params );
        CHECK( new_closest == old_closest );
        CHECK( rng_get_engine() == old_rng );
        found += !old_closest.is_invalid();

        rng_set_engine_seed( ++seed );
        const std::vector<tripoint_abs_omt> old_all = old_omt_search::find_all( origin, params );
        // NOLINTNEXTLINE(cata-determinism)
        const cata_default_random_engine old_all_rng = rng_get_engine();
        rng_set_engine_seed( seed );
        const std::vector<tripoint_abs_omt> new_all = overmap_buffer.find_all( origin, params );
        CHECK( new_all == old_all );
        CHECK( rng_get_engine() == old_all_rng );

        rng_set_engine_seed( ++seed );
        const tripoint_abs_omt old_random = random_entry( old_all, tripoint_abs_omt::invalid );
        rng_set_engine_seed( seed );
        CHECK( overmap_buffer.find_random( origin, params ) == old_random );
    }
    // the queries did find things
    CHECK( found > 10 );
    // and generated no overmap
    CHECK( overmap_buffer.get_overmap_count() == overmaps_before );
}

// A search with several equally close matches picks one at random, from the same list in
// the same order as before.
TEST_CASE( "omap-p02_ties_are_broken_the_same_way", "[audit][overmap]" )
{
    clear_avatar();
    const tripoint_abs_omt middle = overmap_middle() + tripoint_rel_omt( 11, 7, 0 );
    // all three tiles away, on and off the origin's level
    const std::vector<tripoint_rel_omt> spots = {
        tripoint_rel_omt( 3, 0, 0 ), tripoint_rel_omt( -3, 2, 0 ), tripoint_rel_omt( 1, -3, 0 ),
        tripoint_rel_omt( 0, 0, 3 ), tripoint_rel_omt( 2, 3, -3 ), tripoint_rel_omt( -3, -3, 1 )
    };
    std::vector<std::pair<tripoint_abs_omt, oter_id>> saved;
    for( const tripoint_rel_omt &d : spots ) {
        saved.emplace_back( middle + d, overmap_buffer.ter( middle + d ) );
        overmap_buffer.ter_set( middle + d, oter_test_forest_very_thick.id() );
    }
    omt_find_params params;
    params.types = { { "test_forest_very_thick", ot_match_type::exact } };
    params.search_range = 6;
    params.existing_only = true;
    std::vector<tripoint_abs_omt> picks;
    for( unsigned int i = 1; i <= 40; ++i ) {
        // seeds far apart: the first number from nearby small seeds is nearly the same
        const unsigned int seed = 1000003u * i + 12345u;
        rng_set_engine_seed( seed );
        const tripoint_abs_omt old_pick = old_omt_search::find_closest( middle, params );
        rng_set_engine_seed( seed );
        const tripoint_abs_omt new_pick = overmap_buffer.find_closest( middle, params );
        CHECK( new_pick == old_pick );
        CHECK( square_dist( middle, new_pick ) == 3 );
        picks.push_back( new_pick );
    }
    std::sort( picks.begin(), picks.end() );
    picks.erase( std::unique( picks.begin(), picks.end() ), picks.end() );
    // several of the tied spots came up
    CHECK( picks.size() > 2 );
    for( std::pair<tripoint_abs_omt, oter_id> &s : saved ) {
        overmap_buffer.ter_set( s.first, s.second );
    }
}

namespace
{

// What a search did on a fresh overmap buffer.
// NOLINTNEXTLINE(cata-determinism,cert-msc32-c,cert-msc51-cpp): holds a copy of the game's RNG
struct fresh_search {
    tripoint_abs_omt closest = tripoint_abs_omt::invalid;
    std::vector<tripoint_abs_omt> all;
    // the overmaps around the origin that exist afterwards
    std::vector<point_abs_om> overmaps;
    int generated = 0;
    // A copy of the search's RNG state afterwards, assigned before use.
    cata_default_random_engine rng_after;
};

// Runs a search (the old copy or the real one; find_closest or find_all) on an empty overmap
// buffer that starts with the real buffer's region layout and world state and the given RNG
// state, then puts the real buffer back as it was.
fresh_search search_on_fresh_buffer( bool old_search, bool all, const tripoint_abs_omt &origin,
                                     const omt_find_params &params, const cata_default_random_engine &rng )
{
    fresh_search out;
    overmapbuffer real;
    std::swap( overmap_buffer, real );
    overmap_buffer.regions = real.regions;
    overmap_buffer.global_state = real.global_state;
    overmap_buffer.externally_set_args = real.externally_set_args;
    const int before = overmap_buffer.get_overmap_count();
    rng_get_engine() = rng;
    if( all ) {
        out.all = old_search ? old_omt_search::find_all( origin, params ) :
                  overmap_buffer.find_all( origin, params );
    } else {
        out.closest = old_search ? old_omt_search::find_closest( origin, params ) :
                      overmap_buffer.find_closest( origin, params );
    }
    out.rng_after = rng_get_engine();
    out.generated = overmap_buffer.get_overmap_count() - before;
    for( const point_abs_om &p : closest_points_first( project_to<coords::om>( origin.xy() ), 2 ) ) {
        if( overmap_buffer.has( p ) ) {
            out.overmaps.push_back( p );
        }
    }
    std::swap( overmap_buffer, real );
    return out;
}

} // namespace

// A search that may generate overmaps (existing_only false) generates the same ones, from the
// same RNG state, and finds the same things, as the old search did, starting from no overmaps.
TEST_CASE( "omap-p02_generating_searches_match_the_old_search", "[audit][overmap]" )
{
    clear_avatar();
    // NOLINTNEXTLINE(cata-determinism)
    const cata_default_random_engine saved_rng = rng_get_engine();
    const int overmaps_before = overmap_buffer.get_overmap_count();
    // far from anything the other tests generate
    const point_abs_om far_om = project_to<coords::om>( get_avatar().pos_abs_omt().xy() ) +
                                point_rel_om( 23, -17 );
    struct generating_query {
        tripoint_om_omt local;
        std::vector<std::pair<std::string, ot_match_type>> types;
        int range;
        int min_z;
        int max_z;
        bool all;
    };
    const std::vector<generating_query> queries = {
        // misses on every level, reaching the overmap to the east
        { tripoint_om_omt( OMAPX - 10, 90, 0 ), { { "bench_no_such_terrain", ot_match_type::type } }, 20, -OVERMAP_DEPTH, OVERMAP_HEIGHT, false },
        { tripoint_om_omt( OMAPX - 10, 90, 0 ), { { "bench_no_such_terrain", ot_match_type::type } }, 20, 0, 0, true },
        // fields by the border, on the surface
        { tripoint_om_omt( OMAPX - 2, 90, 0 ), { { "field", ot_match_type::type }, { "forest", ot_match_type::type } }, 30, 0, 0, false },
        { tripoint_om_omt( OMAPX - 2, 90, 0 ), { { "field", ot_match_type::type } }, 12, 0, 0, true },
    };
    rng_set_engine_seed( 7349 );
    // NOLINTNEXTLINE(cata-determinism)
    const cata_default_random_engine rng = rng_get_engine();
    bool warmed_up = false;
    for( const generating_query &q : queries ) {
        omt_find_params params;
        params.types = q.types;
        params.search_range = q.range;
        params.existing_only = false;
        params.min_z = q.min_z;
        params.max_z = q.max_z;
        const tripoint_abs_omt origin = project_combine( far_om, q.local );
        CAPTURE( origin.to_string(), q.all, q.range );
        if( !warmed_up ) {
            // The first overmap generated on a fresh buffer draws random numbers that later ones
            // don't (whichever search runs it), so warm up before comparing.
            search_on_fresh_buffer( true, q.all, origin, params, rng );
            warmed_up = true;
        }
        const fresh_search old_run = search_on_fresh_buffer( true, q.all, origin, params, rng );
        const fresh_search new_run = search_on_fresh_buffer( false, q.all, origin, params, rng );
        // the search went past the origin's own overmap
        CHECK( old_run.generated >= 2 );
        CHECK( new_run.generated == old_run.generated );
        CHECK( new_run.overmaps == old_run.overmaps );
        CHECK( new_run.closest == old_run.closest );
        CHECK( new_run.all == old_run.all );
        CHECK( new_run.rng_after == old_run.rng_after );
    }
    // the real buffer is as it was
    CHECK( overmap_buffer.get_overmap_count() == overmaps_before );
    CHECK_FALSE( overmap_buffer.has( far_om ) );
    rng_get_engine() = saved_rng;
}

// ---------------------------------------------------------------------------------------
// ACT-P03: zone jobs check the same tiles and fetch the same items

namespace
{

const tripoint_bub_ms farm_avatar_pos( 60, 60, 0 );

// A 4x4 plot that needs tilling east of the avatar, and a loot zone of junk to the west.
std::vector<tripoint_bub_ms> farm_job_setup()
{
    clear_avatar();
    clear_map();
    zone_manager &zm = zone_manager::get_manager();
    zm.clear();
    // daylight, and the light caches to go with it
    set_time( noon_in_season( SUMMER, 10 ) );
    avatar &u = get_avatar();
    map &here = get_map();
    u.setpos( here, farm_avatar_pos );
    u.wear_item( item( itype_backpack ), false );
    const tripoint_bub_ms loot_corner = farm_avatar_pos + tripoint_rel_ms( -12, -4, 0 );
    for( int dy = 0; dy < 8; ++dy ) {
        for( int dx = 0; dx < 8; ++dx ) {
            here.add_item_or_charges( loot_corner + tripoint_rel_ms( dx, dy, 0 ),
                                      item( ( dx + dy ) % 2 ? itype_rock : itype_scrap ) );
        }
    }
    zm.add( "loot", zone_type_LOOT_UNSORTED, faction_your_followers, false, true,
            here.get_abs( loot_corner ), here.get_abs( loot_corner + tripoint_rel_ms( 7, 7, 0 ) ) );
    std::vector<tripoint_bub_ms> plot;
    const tripoint_bub_ms plot_corner = farm_avatar_pos + tripoint_rel_ms( 4, -2, 0 );
    for( int dy = 0; dy < 4; ++dy ) {
        for( int dx = 0; dx < 4; ++dx ) {
            plot.push_back( plot_corner + tripoint_rel_ms( dx, dy, 0 ) );
            here.ter_set( plot.back(), ter_t_dirt );
        }
    }
    zm.add( "plot", zone_type_FARM_PLOT, faction_your_followers, false, true,
            here.get_abs( plot_corner ), here.get_abs( plot_corner + tripoint_rel_ms( 3, 3, 0 ) ) );
    here.build_map_cache( 0 );
    return plot;
}

void farm_job_teardown()
{
    avatar &u = get_avatar();
    u.cancel_activity();
    u.backlog.clear();
    zone_manager::get_manager().clear();
    clear_map();
    clear_avatar();
}

int count_messages( const std::string &text )
{
    int n = 0;
    for( const std::pair<std::string, std::string> &m : Messages::recent_messages( 0 ) ) {
        n += m.second == text ? 1 : 0;
    }
    return n;
}

} // namespace

TEST_CASE( "act-p03_farm_scan_without_a_tool_checks_every_tile_once", "[audit][activity]" )
{
    restore_turn turn_guard;
    const std::vector<tripoint_bub_ms> plot = farm_job_setup();
    avatar &u = get_avatar();
    Messages::clear_messages();
    u.assign_activity( multi_farm_activity_actor() );
    u.set_moves( 1000 );
    u.activity.do_turn( u );
    // the scan went over every tile: a move for the scan and one per tile skipped
    CHECK( u.get_moves() == 1000 - 1 - static_cast<int>( plot.size() ) );
    CHECK_FALSE( u.activity );
    CHECK( u.backlog.empty() );
    const std::string missing = string_format(
                                    "The required items are not available to complete the \"%1$s\" task at zone %2$s.",
                                    "farming", "plot" );
    CHECK( count_messages( missing ) == 1 );
    for( const tripoint_bub_ms &p : plot ) {
        CHECK( get_map().ter( p ) == ter_t_dirt );
    }

    // with a hoe in the loot zone the next scan fetches it, for the nearest tile
    map &here = get_map();
    const tripoint_bub_ms loot_spot = farm_avatar_pos + tripoint_rel_ms( -8, 0, 0 );
    here.add_item_or_charges( loot_spot, item( itype_hoe ) );
    u.assign_activity( multi_farm_activity_actor() );
    u.set_moves( 1000 );
    u.activity.do_turn( u );
    CHECK( u.activity.id() == ACT_FETCH_REQUIRED );
    REQUIRE_FALSE( u.backlog.empty() );
    CHECK( u.backlog.front().id() == ACT_MULTIPLE_FARM );
    // no tile was skipped before the first one asked for the hoe
    CHECK( u.get_moves() == 999 );
    farm_job_teardown();
}

// Tiles whose requirements differ get their own answers within one scan: the tiles to till
// (no hoe anywhere) are skipped, and the scan goes on to fetch seeds for the tiles to plant.
TEST_CASE( "act-p03_one_scan_answers_each_requirement_separately", "[audit][activity]" )
{
    restore_turn turn_guard;
    const std::vector<tripoint_bub_ms> plot = farm_job_setup();
    map &here = get_map();
    zone_manager &zm = zone_manager::get_manager();
    // a second plot, tilled, for buckwheat, farther away than the first
    const tripoint_bub_ms seed_corner = farm_avatar_pos + tripoint_rel_ms( 10, 4, 0 );
    for( int dy = 0; dy < 2; ++dy ) {
        for( int dx = 0; dx < 2; ++dx ) {
            here.ter_set( seed_corner + tripoint_rel_ms( dx, dy, 0 ), ter_t_dirtmound );
        }
    }
    shared_ptr_fast<plot_options> options = make_shared_fast<plot_options>();
    options->deserialize(
        json_loader::from_string( R"({ "seed": "seed_buckwheat" })" ).get_object() );
    zm.add( "seeds", zone_type_FARM_PLOT, faction_your_followers, false, true,
            here.get_abs( seed_corner ), here.get_abs( seed_corner + tripoint_rel_ms( 1, 1, 0 ) ),
            options );
    for( int i = 0; i < 4; ++i ) {
        here.add_item_or_charges( farm_avatar_pos + tripoint_rel_ms( -8, 0, 0 ),
                                  item( itype_seed_buckwheat ) );
    }
    here.build_map_cache( 0 );

    avatar &u = get_avatar();
    u.assign_activity( multi_farm_activity_actor() );
    u.set_moves( 1000 );
    u.activity.do_turn( u );
    CHECK( u.activity.id() == ACT_FETCH_REQUIRED );
    REQUIRE_FALSE( u.backlog.empty() );
    CHECK( u.backlog.front().id() == ACT_MULTIPLE_FARM );
    // every tile to till was skipped first
    CHECK( u.get_moves() == 1000 - 1 - static_cast<int>( plot.size() ) );
    farm_job_teardown();
}

// The scan memo keeps an answer until forget(). A scan calls forget() before it acts on a work
// spot, so what it does there can't leave a later spot an answer from before.
TEST_CASE( "act-p03_scan_memo_keeps_answers_until_forgotten", "[audit][activity]" )
{
    restore_turn turn_guard;
    const std::vector<tripoint_bub_ms> plot = farm_job_setup();
    avatar &u = get_avatar();
    map &here = get_map();
    multi_farm_activity_actor farm;
    activity_reason_info till = activity_reason_info::fail( do_activity_reason::NEEDS_TILLING );
    const std::optional<requirement_id> tilling = farm.multi_activity_requirements( u, till,
            plot.front(), nullptr );
    REQUIRE( tilling.has_value() );
    std::vector<tripoint_bub_ms> loot_spots;
    for( const tripoint_bub_ms &p : zone_manager::get_manager().get_point_set_loot( u.pos_abs(),
            MAX_VIEW_DISTANCE, false, faction_your_followers ) ) {
        loot_spots.push_back( p );
    }
    REQUIRE_FALSE( loot_spots.empty() );
    const auto nearby = [&]() {
        return multi_activity_actor::are_requirements_nearby( loot_spots, *tilling, u,
                ACT_MULTIPLE_FARM, true, plot.front() );
    };
    {
        scoped_requirement_scan_memo memo;
        CHECK_FALSE( nearby() );
        // a hoe turns up in the loot zone
        here.add_item_or_charges( loot_spots.front(), item( itype_hoe ) );
        // still the remembered answer
        CHECK_FALSE( nearby() );
        memo.forget();
        CHECK( nearby() );
    }
    // outside a scan, nothing is remembered
    CHECK( nearby() );
    farm_job_teardown();
}

// The welding rigs in reach of a work spot are part of its answer: a deconstruction spot with
// a rig beside it can be worked although one without could not, in the same scan.
TEST_CASE( "act-p03_a_welding_rig_counts_only_where_it_reaches", "[audit][activity]" )
{
    restore_turn turn_guard;
    clear_avatar();
    clear_map();
    zone_manager &zm = zone_manager::get_manager();
    zm.clear();
    set_time( noon_in_season( SUMMER, 10 ) );
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms start( 60, 60, 0 );
    u.setpos( here, start );
    // both out of reach of the rig from where the avatar stands; only the far spot has it in
    // reach (PICKUP_RANGE - 1 tiles)
    const tripoint_bub_ms near_spot = start + tripoint_rel_ms( 4, 0, 0 );
    const tripoint_bub_ms far_spot = start + tripoint_rel_ms( 16, 0, 0 );
    const tripoint_bub_ms rig_spot = far_spot + tripoint_rel_ms( 3, 0, 0 );
    const auto place = [&here]( const tripoint_bub_ms & where, const vpart_id & part ) {
        vehicle *veh = here.add_vehicle( vehicle_prototype_none, where, 0_degrees, 0,
                                         veh_spawn_status::UNDAMAGED );
        REQUIRE( veh != nullptr );
        REQUIRE( veh->install_part( here, point_rel_ms::zero, vpart_frame ) != -1 );
        REQUIRE( veh->install_part( here, point_rel_ms::zero, part ) != -1 );
        veh->refresh();
        here.add_vehicle_to_cache( veh );
    };
    place( near_spot, vpart_test_vp_weld_to_remove );
    place( far_spot, vpart_test_vp_weld_to_remove );
    place( rig_spot, vpart_test_vp_reserve_welder );
    for( const tripoint_bub_ms &spot : {
             near_spot, far_spot
         } ) {
        zm.add( "deconstruct", zone_type_VEHICLE_DECONSTRUCT, faction_your_followers, false, true,
                here.get_abs( spot ), here.get_abs( spot ) );
    }
    here.build_map_cache( 0 );

    u.assign_activity( multi_vehicle_deconstruct_activity_actor() );
    REQUIRE( u.activity.id() == ACT_VEHICLE_DECONSTRUCTION );
    u.set_moves( 1000 );
    u.activity.do_turn( u );
    // the near spot was skipped for a move; the far one, with the rig in reach, went on to a
    // fetch for the rest of what removing the part needs
    CHECK( u.get_moves() == 1000 - 1 - 1 );
    CHECK( u.activity.id() == ACT_FETCH_REQUIRED );
    REQUIRE_FALSE( u.backlog.empty() );
    CHECK( u.backlog.front().id() == ACT_VEHICLE_DECONSTRUCTION );

    u.cancel_activity();
    u.backlog.clear();
    u.activity_vehicle_part_index = -1;
    zm.clear();
    clear_map();
    clear_avatar();
}
