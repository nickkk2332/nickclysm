#include <algorithm>
#include <cstdlib>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#include "avatar.h"
#include "basecamp.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
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
#include "regional_settings.h"
#include "submap.h"
#include "type_id.h"

static const oter_str_id oter_field( "field" );
static const oter_str_id oter_test_forest_very_thick( "test_forest_very_thick" );

static const overmap_special_id overmap_special_test_audit_mansion( "test_audit_mansion" );
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

// OMAP-B03: find_camp scanned an off-by-one [-3, +2] window and returned the first camp it met.
// With two camps side by side (the Isherwood farm places them so under some rotations), looking
// up the second returned the first, and special placement then renamed and moved that one.
TEST_CASE( "omap-b03_find_camp_returns_the_camp_at_the_point", "[audit][overmap][camp]" )
{
    clear_avatar();
    clear_map();
    const point_abs_omt first = get_avatar().pos_abs_omt().xy() + point_rel_omt( 20, 20 );
    const point_abs_omt second = first + point_rel_omt( 1, 0 );
    REQUIRE( !overmap_buffer.find_camp( first ) );
    overmap_buffer.add_camp( basecamp( "first", tripoint_abs_omt( first, 0 ) ) );
    overmap_buffer.add_camp( basecamp( "second", tripoint_abs_omt( second, 0 ) ) );

    std::optional<basecamp *> found = overmap_buffer.find_camp( second );
    REQUIRE( found );
    CHECK( ( *found )->camp_omt_pos().xy() == second );
    found = overmap_buffer.find_camp( first );
    REQUIRE( found );
    CHECK( ( *found )->camp_omt_pos().xy() == first );

    // Off the camps the nearest one wins, and the window reaches 3 OMTs on every side.
    found = overmap_buffer.find_camp( second + point_rel_omt( 2, 0 ) );
    REQUIRE( found );
    CHECK( ( *found )->camp_omt_pos().xy() == second );
    found = overmap_buffer.find_camp( first + point_rel_omt( -3, -3 ) );
    REQUIRE( found );
    CHECK( ( *found )->camp_omt_pos().xy() == first );
    CHECK( !overmap_buffer.find_camp( first + point_rel_omt( -4, 0 ) ) );

    overmap_buffer.get_om_global( first ).om->remove_camp( first );
    overmap_buffer.get_om_global( second ).om->remove_camp( second );
}

// Generates the overmap at `where` without any specials, which is all the city tests need.
static overmap &generate_bare_overmap( const point_abs_om &where )
{
    std::vector<const overmap_special *> no_specials;
    overmap_special_batch batch( where, no_specials );
    overmap_buffer.create_custom_overmap( where, batch );
    overmap *om = overmap_buffer.get_existing( where );
    REQUIRE( om != nullptr );
    return *om;
}

// OMAP-B04: place_cities combined urbanity with std::min instead of std::max and then clamped
// the result back up to the region's city_size, so urbanity never grew a city: every random
// city came from a base size of city_size - 1 or city_size. Far to the east, where the default
// region raises urbanity, other sizes must now show up.
TEST_CASE( "omap-b04_urbanity_grows_cities", "[audit][overmap]" )
{
    overmap &om = generate_bare_overmap( point_abs_om( 20, 0 ) );
    REQUIRE( om.get_urbanity() >= 10 );
    const region_settings_city &city_settings = om.get_settings().get_settings_city();
    REQUIRE( !city_settings.is_megacity );
    REQUIRE( city::get_all().empty() );

    // Every size the old code could produce: the size rolls of place_cities applied to the
    // two possible base sizes.
    std::set<int> old_sizes;
    for( int base = city_settings.city_size - 1; base <= city_settings.city_size; ++base ) {
        for( const int size : {
                 base / 3, base * 2 / 3, base * 3 / 2, base * 2
             } ) {
            old_sizes.insert( std::min( std::max( size, 2 ), 55 ) );
        }
    }
    // About 11 cities, each with roughly a 70% chance of a size the old code couldn't roll.
    REQUIRE( om.cities.size() >= 5 );
    const bool any_new_size = std::any_of( om.cities.begin(), om.cities.end(),
    [&old_sizes]( const city & c ) {
        return old_sizes.count( c.size ) == 0;
    } );
    CHECK( any_new_size );
}

// OMAP-B05: building_bin::pick dereferenced the null pointer an empty bin hands back, and
// pick_random_building_to_place redrew forever when nothing in a bin fit the town. A region whose
// parks bin is empty and whose houses and shops are all too big for any town must still
// generate.
TEST_CASE( "omap-b05_unfillable_building_bins_do_not_hang_or_crash", "[audit][overmap]" )
{
    building_bin empty_bin;
    empty_bin.finalize();
    CHECK( empty_bin.pick().is_null() );

    building_bin too_big;
    too_big.add( overmap_special_test_audit_mansion, 100 );
    too_big.finalize();
    REQUIRE( too_big.pick() == overmap_special_test_audit_mansion );

    const point_abs_om where( 0, 20 );
    const tripoint_abs_omt where_omt = project_to<coords::omt>( tripoint_abs_om( where, 0 ) );
    const region_settings &region = overmap_buffer.get_settings( where_omt );
    region_settings_city &city_spec =
        const_cast<region_settings_city &>( region.get_settings_city() );
    REQUIRE( city_spec.city_size > 0 );
    restore_on_out_of_scope<building_bin> restore_houses( city_spec.houses );
    restore_on_out_of_scope<building_bin> restore_shops( city_spec.shops );
    restore_on_out_of_scope<building_bin> restore_parks( city_spec.parks );
    city_spec.houses = too_big;
    city_spec.shops = too_big;
    city_spec.parks = empty_bin;

    overmap &om = generate_bare_overmap( where );
    CHECK( !om.cities.empty() );
}
