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
