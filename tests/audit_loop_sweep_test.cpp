#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "avatar.h"
#include "bionics.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character_id.h"
#include "coordinates.h"
#include "creature_tracker.h"
#include "debug.h"
#include "do_turn.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "item.h"
#include "item_location.h"
#include "json_loader.h"
#include "line.h"
#include "magic_enchantment.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "map_selector.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "overmapbuffer.h"
#include "path_info.h"
#include "player_helpers.h"
#include "point.h"
#include "submap.h"
#include "type_id.h"
#include "units.h"
#include "vehicle.h"
#include "vpart_position.h"
#include "weather.h"
#include "weather_gen.h"
#include "weather_type.h"
#include "worldfactory.h"

// ---- LOOP-B06 ----

static const itype_id itype_bucket( "bucket" );
static const itype_id itype_maple_sap( "maple_sap" );

static const ter_str_id ter_t_tree_maple_tapped( "t_tree_maple_tapped" );

// The water temperature curve and the maple sap season count days from the start of spring
// (calendar::turn_zero), but were given days from Jan 1, 81 days later in the year. Water was
// warmest in mid-May and coldest in mid-November, and sap ran from Dec 7 to Feb 14.
static units::temperature water_temperature_at( const time_duration &since_spring )
{
    calendar::turn = calendar::turn_zero + calendar::year_length() + since_spring;
    // At midnight the daily swing adds nothing
    calendar::turn -= time_past_midnight( calendar::turn );
    return get_weather().get_cur_weather_gen().get_water_temperature();
}

TEST_CASE( "loop-b06_water_is_warmest_in_midsummer_and_coldest_in_midwinter",
           "[audit][weather][temperature]" )
{
    restore_on_out_of_scope restore_turn( calendar::turn );
    const time_duration season = calendar::season_length();
    // The yearly curve runs from 33.8 F to 75.2 F
    CHECK( units::to_fahrenheit( water_temperature_at( season * 3 / 2 ) ) ==
           Approx( 75.2 ).margin( 0.5 ) );
    CHECK( units::to_fahrenheit( water_temperature_at( season * 7 / 2 ) ) ==
           Approx( 33.8 ).margin( 0.5 ) );
}

// Sap a tapped maple makes in the day before the given time of year
static int sap_made_in_the_day_before( const time_duration &since_spring )
{
    clear_map();
    map &here = get_map();
    const time_point start = calendar::turn;
    // The next such time at least a day ahead, so every submap the load catches up moves forwards
    time_point when = calendar::turn_zero + since_spring;
    while( when < start + 1_days ) {
        when += calendar::year_length();
    }
    calendar::turn = when;

    const tripoint_bub_ms tree( 60, 60, 0 );
    here.ter_set( tree, ter_t_tree_maple_tapped );
    here.add_item( tree, item( itype_bucket ) );
    const tripoint_abs_omt tree_omt = project_to<coords::omt>( here.get_abs( tree ) );
    // The submaps of the tree's map square, which the load below catches up
    std::vector<submap *> submaps;
    for( int dx = 0; dx < SEEX * 2; dx += SEEX ) {
        for( int dy = 0; dy < SEEY * 2; dy += SEEY ) {
            const tripoint_abs_ms corner = project_to<coords::ms>( tree_omt ) + tripoint_rel_ms( dx, dy, 0 );
            tripoint_bub_ms p = here.get_bub( corner );
            point_sm_ms l;
            submaps.push_back( map_meddler::unsafe_get_submap_at( p, l ) );
        }
    }
    tripoint_bub_ms tree_sm_pos = tree;
    point_sm_ms l;
    map_meddler::unsafe_get_submap_at( tree_sm_pos, l )->last_touched = calendar::turn - 1_days;

    // Loading a map over the tree catches its submap up on the missed day, which makes the sap
    tinymap tm;
    tm.load( tree_omt, false );

    int sap = 0;
    for( const item &container : here.i_at( tree ) ) {
        for( const item *it : container.all_items_top() ) {
            if( it->typeId() == itype_maple_sap ) {
                sap += it->charges;
            }
        }
    }
    calendar::turn = start;
    for( submap *sm : submaps ) {
        sm->last_touched = start;
    }
    clear_map();
    return sap;
}

TEST_CASE( "loop-b06_maple_sap_runs_in_late_winter_and_early_spring", "[audit][map]" )
{
    const time_duration season = calendar::season_length();
    // Ten days into spring (around Mar 31) is in the season
    CHECK( sap_made_in_the_day_before( 10_days ) > 0 );
    // Ten days into winter (around Dec 31) is not
    CHECK( sap_made_in_the_day_before( season * 3 + 10_days ) == 0 );
}
