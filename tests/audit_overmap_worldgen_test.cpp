#include <algorithm>
#include <cstdlib>
#include <memory>
#include <vector>

#include "avatar.h"
#include "cata_catch.h"
#include "city.h"
#include "coordinates.h"
#include "map_helpers.h"
#include "map_iterator.h"
#include "mapbuffer.h"
#include "omdata.h"
#include "overmap.h"
#include "overmapbuffer.h"
#include "player_helpers.h"
#include "point.h"
#include "submap.h"
#include "type_id.h"

static const oter_str_id oter_field( "field" );
static const oter_str_id oter_test_forest_very_thick( "test_forest_very_thick" );

static const overmap_special_id overmap_special_test_audit_road_shack( "test_audit_road_shack" );

// An overmap a few overmaps away from the avatar, so these tests leave the map the rest of the
// suite plays on alone.
static point_abs_om audit_overmap_near_avatar( const point_rel_om &offset )
{
    return project_to<coords::om>( get_avatar().pos_abs_omt().xy() ) + offset;
}

// OMAP-B01: place_special_attempt passed must_be_unexplored in place_special's `force` slot, so
// a special placed for a mission or EOC (overmapbuffer::place_special) laid its road across map
// the player had already generated. It also used up a placement the unique-special deck had
// drawn for worldgen.
TEST_CASE( "omap-b01_mission_special_road_avoids_explored_terrain", "[audit][overmap]" )
{
    clear_avatar();
    clear_map();
    overmap &om = overmap_buffer.get( audit_overmap_near_avatar( point_rel_om( 3, 3 ) ) );
    // Flat fields and a single city far to the south, so the special's road heads south.
    for( int x = 0; x < OMAPX; ++x ) {
        for( int y = 0; y < OMAPY; ++y ) {
            om.ter_set( tripoint_om_omt( x, y, 0 ), oter_field.id() );
        }
    }
    om.cities.clear();
    om.cities.emplace_back( "audit town", point_om_omt( 90, 150 ), 4 );

    // A ring of already generated (explored) OMTs three tiles out from the search center. The
    // special lands within one tile of the center and its road starts within two, so a road to
    // the city has to cross the ring.
    const point_om_omt center( 90, 90 );
    std::vector<tripoint_om_omt> ring;
    for( int dx = -3; dx <= 3; ++dx ) {
        for( int dy = -3; dy <= 3; ++dy ) {
            if( std::max( std::abs( dx ), std::abs( dy ) ) != 3 ) {
                continue;
            }
            const tripoint_om_omt p( center + point_rel_omt( dx, dy ), 0 );
            std::unique_ptr<submap> sm = std::make_unique<submap>();
            MAPBUFFER.add_submap( project_to<coords::sm>( project_combine( om.pos(), p ) ), sm );
            REQUIRE( om.is_omt_generated( p ) );
            ring.push_back( p );
        }
    }

    // A worldgen deck draw still waiting to be placed.
    overmap_buffer.get_deck_state( overmap_special_test_audit_road_shack, 1, 1 ).to_place = 1;

    const tripoint_abs_omt center_abs = project_combine( om.pos(), tripoint_om_omt( center, 0 ) );
    REQUIRE( overmap_buffer.place_special( overmap_special_test_audit_road_shack, center_abs, 2 ) );
    bool placed = false;
    for( const tripoint_om_omt &p : points_in_radius( tripoint_om_omt( center, 0 ), 1 ) ) {
        placed = placed || om.overmap_special_at( p ) == overmap_special_test_audit_road_shack;
    }
    REQUIRE( placed );

    for( const tripoint_om_omt &p : ring ) {
        CAPTURE( p );
        CHECK( om.ter( p ) == oter_field.id() );
    }
    CHECK( overmap_buffer.get_deck_state( overmap_special_test_audit_road_shack, 1,
                                          1 ).to_place == 1 );

    MAPBUFFER.clear_outside_reality_bubble();
}

// OMAP-B02 (a): when the terrain branch found a strictly closer match it lowered found_dist but
// kept the farther matches, so find_closest picked at random between a match three z-levels
// down and a closer one on the surface.
TEST_CASE( "omap-b02_find_closest_drops_farther_terrain_matches", "[audit][overmap]" )
{
    clear_avatar();
    clear_map();
    const tripoint_abs_omt origin = get_avatar().pos_abs_omt();
    // 3D distance 3 (one tile over, three levels down); the search meets it first.
    const tripoint_abs_omt deep_match = origin + tripoint_rel_omt( 1, 0, -3 );
    // 3D distance 2 (two tiles over, same level).
    const tripoint_abs_omt close_match = origin + tripoint_rel_omt( 2, 0, 0 );
    const oter_id old_deep = overmap_buffer.ter( deep_match );
    const oter_id old_close = overmap_buffer.ter( close_match );
    overmap_buffer.ter_set( deep_match, oter_test_forest_very_thick.id() );
    overmap_buffer.ter_set( close_match, oter_test_forest_very_thick.id() );

    omt_find_params params;
    params.types = { { "test_forest_very_thick", ot_match_type::type } };
    params.search_range = 10;
    params.existing_only = true;
    for( int i = 0; i < 20; ++i ) {
        CHECK( overmap_buffer.find_closest( origin, params ) == close_match );
    }

    overmap_buffer.ter_set( deep_match, old_deep );
    overmap_buffer.ter_set( close_match, old_close );
}

// OMAP-B02 (b): the om_special branch returned the nearest match from the first overmap ring
// that had any, so a special 150 OMTs away on the origin's own overmap beat one 4 OMTs away just
// across the border. It also ignored the z-range and skipped a match exactly search_range away.
TEST_CASE( "omap-b02_find_closest_special_looks_across_the_border", "[audit][overmap]" )
{
    clear_avatar();
    clear_map();
    const point_abs_om home_pos = audit_overmap_near_avatar( point_rel_om( -3, -3 ) );
    const point_abs_om east_pos = home_pos + point_rel_om( 1, 0 );
    overmap &home = overmap_buffer.get( home_pos );
    overmap &east = overmap_buffer.get( east_pos );
    const tripoint_om_omt far_local( 27, 90, 0 );
    const tripoint_om_omt close_local( 1, 90, 0 );
    home.place_special_forced( overmap_special_test_audit_road_shack, far_local,
                               om_direction::type::north );
    east.place_special_forced( overmap_special_test_audit_road_shack, close_local,
                               om_direction::type::north );

    const tripoint_abs_omt origin = project_combine( home_pos, tripoint_om_omt( 177, 90, 0 ) );
    const tripoint_abs_omt far_match = project_combine( home_pos, far_local );
    const tripoint_abs_omt close_match = project_combine( east_pos, close_local );
    REQUIRE( square_dist( origin, far_match ) == 150 );
    REQUIRE( square_dist( origin, close_match ) == 4 );

    omt_find_params params;
    params.types = { { "cabin", ot_match_type::type } };
    params.om_special = overmap_special_test_audit_road_shack;
    params.existing_only = true;
    params.search_range = 200;
    CHECK( overmap_buffer.find_closest( origin, params ) == close_match );

    // A match exactly search_range away counts, as it does for plain terrain.
    params.search_range = 4;
    CHECK( overmap_buffer.find_closest( origin, params ) == close_match );

    // The z-range applies too: the shack has no tile above the surface.
    params.search_range = 200;
    params.min_z = 1;
    CHECK( overmap_buffer.find_closest( origin, params ).is_invalid() );
}
