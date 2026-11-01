#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

#include "avatar.h"
#include "cata_catch.h"
#include "coordinates.h"
#include "explosion.h"
#include "game.h"
#include "level_cache.h"
#include "line.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "monster.h"
#include "npc.h"
#include "player_helpers.h"
#include "point.h"
#include "sounds.h"
#include "teleport.h"
#include "trap.h"
#include "type_id.h"
#include "veh_appliance.h"
#include "vehicle.h"
#include "vpart_position.h"

static const ter_str_id ter_t_door_c( "t_door_c" );

static const trap_str_id tr_dormant_corpse( "tr_dormant_corpse" );

static const vpart_id vpart_ap_wall_light( "ap_wall_light" );

// With more than ten sounds in a turn the extra ones merge into a cluster. A sound
// no seed was within ~12 tiles of went into the first cluster, wherever it was,
// and dragged that cluster's position (and volume) towards itself.
TEST_CASE( "spat-b03_sound_clusters_merge_into_the_nearest_cluster", "[audit][sound]" )
{
    // At least 16 tiles apart, so no sound is within the old cap of any other.
    const std::vector<tripoint_bub_ms> spots = {
        { 113, 66, 0 }, { 65, 36, 0 }, { 12, 38, 0 }, { 14, 82, 0 }, { 117, 127, 0 },
        { 78, 74, 0 }, { 41, 95, 0 }, { 59, 9, 0 }, { 89, 117, 0 }, { 63, 123, 0 },
        { 26, 10, 0 }
    };
    const auto dist = []( const tripoint_bub_ms & a, const tripoint_bub_ms & b ) {
        return rl_dist( a.xy(), b.xy() );
    };
    const auto nearest = [&]( const tripoint_bub_ms & a ) {
        int best = std::numeric_limits<int>::max();
        for( const tripoint_bub_ms &b : spots ) {
            if( b != a ) {
                best = std::min( best, dist( a, b ) );
            }
        }
        return best;
    };
    // The seeds are random; twenty draws leave the old code no realistic chance.
    for( int i = 0; i < 20; ++i ) {
        sounds::reset_sounds();
        for( const tripoint_bub_ms &p : spots ) {
            sounds::sound( p, 50, sounds::sound_t::combat, "a bang" );
        }
        const std::vector<tripoint_bub_ms> centroids = sounds::get_monster_sounds().second;
        REQUIRE( centroids.size() == 10 );
        // Nine clusters hold one sound each; the two that merged are the ones missing.
        std::vector<tripoint_bub_ms> merged;
        for( const tripoint_bub_ms &p : spots ) {
            if( std::find( centroids.begin(), centroids.end(), p ) == centroids.end() ) {
                merged.push_back( p );
            }
        }
        REQUIRE( merged.size() == 2 );
        const int d = dist( merged[0], merged[1] );
        CAPTURE( merged[0].to_string(), merged[1].to_string(), d );
        // The unseeded one of the two joined its nearest neighbour.
        CHECK( ( d == nearest( merged[0] ) || d == nearest( merged[1] ) ) );
    }
    sounds::reset_sounds();
}

// Triggering a sound trap erases it from the list process_sounds() was walking,
// so the next trap was skipped (and the spell went off at the wrong corpse).
TEST_CASE( "spat-b04_sound_triggers_every_dormant_corpse_in_earshot", "[audit][sound][trap]" )
{
    clear_map();
    clear_avatar();
    sounds::reset_sounds();
    map &here = get_map();
    const std::vector<tripoint_bub_ms> corpses = {
        { 50, 70, 0 }, { 53, 70, 0 }, { 56, 70, 0 }
    };
    for( const tripoint_bub_ms &p : corpses ) {
        here.trap_set( p, tr_dormant_corpse.id() );
    }
    REQUIRE( here.trap_locations( tr_dormant_corpse.id() ).size() == corpses.size() );

    // Loud and close enough to trigger each of them for certain.
    sounds::sound( { 53, 73, 0 }, 100, sounds::sound_t::combat, "a gunshot" );
    sounds::process_sounds();

    for( const tripoint_bub_ms &p : corpses ) {
        CAPTURE( p.to_string() );
        CHECK( here.tr_at( p ).is_null() );
    }
    CHECK( here.trap_locations( tr_dormant_corpse.id() ).empty() );
    // The moans of the woken corpses are left for the next turn, not dropped.
    CHECK_FALSE( sounds::get_monster_sounds().first.empty() );
    sounds::reset_sounds();
}

// A wall light shines from the tile in front of it, and appliances face east, so
// a lamp on the bubble's east edge put its light source one column past the end
// of the light buffers. The write landed in the next cache, outside_cache.
TEST_CASE( "spat-b05_wall_light_on_the_bubble_edge_stays_in_the_light_buffers",
           "[audit][lightmap]" )
{
    clear_map();
    clear_avatar();
    map &here = get_map();
    const tripoint_bub_ms lamp_pos( MAPSIZE_X - 1, 5, 0 );
    REQUIRE( place_appliance( here, lamp_pos, vpart_ap_wall_light, get_avatar() ) );
    const optional_vpart_position vp = here.veh_at( lamp_pos );
    REQUIRE( vp );
    vehicle &lamp = vp->vehicle();
    for( int i = 0; i < lamp.part_count(); ++i ) {
        lamp.part( i ).enabled = true;
    }
    REQUIRE( lamp.lights().size() == 1 );

    here.set_lightmap_cache_dirty( 0 );
    here.build_map_cache( 0 );

    // Every byte of outside_cache must still be a bool's 0 or 1.
    const level_cache &cache = here.get_cache_ref( 0 );
    std::vector<unsigned char> bytes( sizeof( cache.outside_cache ) );
    std::memcpy( bytes.data(), &cache.outside_cache, bytes.size() );
    const auto garbage = std::count_if( bytes.begin(), bytes.end(), []( unsigned char b ) {
        return b > 1;
    } );
    CHECK( garbage == 0 );

    here.destroy_vehicle( &lamp );
    here.set_lightmap_cache_dirty( 0 );
    here.build_map_cache( 0 );
}

// Sounds wait in queues until monsters and the player hear them. Kept in bubble
// coordinates, they moved along with the map when it shifted in between, as it
// does when the noise of a step across a submap boundary is made before the shift.
TEST_CASE( "spat-b06_queued_sounds_stay_put_when_the_map_shifts", "[audit][sound]" )
{
    clear_map();
    clear_avatar();
    sounds::reset_sounds();
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms src = u.pos_bub() + tripoint_rel_ms( 5, 3, 0 );
    const tripoint_abs_ms src_abs = here.get_abs( src );
    sounds::sound( src, 40, sounds::sound_t::combat, "a crash" );

    const auto heard_at_source = [&]() {
        const std::vector<tripoint_bub_ms> queued = sounds::get_monster_sounds().first;
        return std::any_of( queued.begin(), queued.end(), [&]( const tripoint_bub_ms & p ) {
            return here.get_abs( p ) == src_abs;
        } );
    };
    REQUIRE( heard_at_source() );

    // Two submaps east and back again, recentring the map each time.
    u.setpos( here, u.pos_bub() + tripoint_rel_ms( 2 * SEEX, 0, 0 ) );
    REQUIRE( g->update_map( u ) != point_rel_sm::zero );
    CHECK( heard_at_source() );

    u.setpos( here, u.pos_bub() - tripoint_rel_ms( 2 * SEEX, 0, 0 ) );
    REQUIRE( g->update_map( u ) != point_rel_sm::zero );
    CHECK( heard_at_source() );
    sounds::reset_sounds();
}

// The telefrag (both knocked away, the occupant hurt) ran only for monsters
// teleporting. A character got the collision message five times, then "flickered"
// and stayed where it was.
TEST_CASE( "spat-b07_teleporting_npc_telefrags_the_monster_in_its_way", "[audit][teleport]" )
{
    clear_map();
    clear_avatar();
    const tripoint_bub_ms start( 50, 60, 0 );
    const tripoint_bub_ms target( 50, 72, 0 );
    npc &guy = spawn_npc( start.xy(), "test_talker" );
    monster &zed = spawn_test_monster( "mon_test_zombie", target );
    const int zed_hp = zed.get_hp();

    CHECK( teleport::teleport_to_point( guy, target, false, false, false ) );
    CHECK( guy.pos_bub() != start );
    CHECK( zed.get_hp() < zed_hp );
    // the cosmetic blast is queued; let it go off while its source still exists
    explosion_handler::process_explosions();
}

// The angle was integer degrees handed to cos/sin as radians and the offset was
// truncated, so a 1 tile shift nearly always landed back on the origin (and the
// teleport failed), and short teleports often fell short of their minimum.
TEST_CASE( "spat-b08_short_teleports_move_at_least_the_minimum_distance", "[audit][teleport]" )
{
    clear_map();
    clear_avatar();
    map &here = get_map();
    const tripoint_bub_ms home( 40, 40, 0 );
    monster &zed = spawn_test_monster( "mon_test_zombie", home );
    for( int i = 0; i < 20; ++i ) {
        zed.setpos( here, home );
        CHECK( teleport::teleport_creature( zed, 1, 1, true, false ) );
        CHECK( square_dist( home, zed.pos_bub() ) == 1 );
    }
    for( int i = 0; i < 20; ++i ) {
        zed.setpos( here, home );
        CHECK( teleport::teleport_creature( zed, 3, 5, true, false ) );
        const int moved = square_dist( home, zed.pos_bub() );
        CHECK( moved >= 3 );
        CHECK( moved <= 5 );
    }
}

// get_path_avoid() closed the tiles a monster can't enter when it *could* bash
// them, so bashers with path_settings never routed through a closed door.
TEST_CASE( "spat-b09_bashing_monsters_may_path_through_bashable_obstacles",
           "[audit][pathfinding][monster]" )
{
    clear_map();
    clear_avatar();
    map &here = get_map();
    const tripoint_bub_ms door( 40, 40, 0 );
    here.ter_set( door, ter_t_door_c );
    REQUIRE( here.is_bashable( door ) );

    monster &zed = spawn_test_monster( "mon_test_zombie", door + tripoint_rel_ms::west );
    REQUIRE_FALSE( zed.bash_skill().empty() );
    REQUIRE_FALSE( zed.can_move_to( door ) );
    CHECK_FALSE( zed.get_path_avoid()( door ) );

    // One that can't bash still keeps out.
    monster &blob = spawn_test_monster( "mon_test_base", door + tripoint_rel_ms::east );
    REQUIRE( blob.bash_skill().empty() );
    CHECK( blob.get_path_avoid()( door ) );
}
