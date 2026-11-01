#include <algorithm>
#include <limits>
#include <vector>

#include "cata_catch.h"
#include "coordinates.h"
#include "line.h"
#include "map.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "point.h"
#include "sounds.h"
#include "trap.h"
#include "type_id.h"

static const trap_str_id tr_dormant_corpse( "tr_dormant_corpse" );

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
