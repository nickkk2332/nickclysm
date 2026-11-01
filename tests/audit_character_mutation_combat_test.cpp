#include <vector>

#include "bodypart.h"
#include "cata_catch.h"
#include "character.h"
#include "creature.h"
#include "player_helpers.h"
#include "type_id.h"

static const json_character_flag json_flag_DISTRIBUTED_DAMAGE( "DISTRIBUTED_DAMAGE" );

static const trait_id trait_SLIMESPAWNER( "SLIMESPAWNER" );

static int total_part_hp( const Character &who, const std::vector<bodypart_id> &parts )
{
    int total = 0;
    for( const bodypart_id &bp : parts ) {
        total += who.get_part_hp_cur( bp );
    }
    return total;
}

// HLTH-B01: DISTRIBUTED_DAMAGE divided the hit by every body entry (12 for a human or
// slime body) with integer division, so hits under 12 did nothing, and handed shares to
// minor parts whose HP never counts. The whole hit must now land on the main parts.
TEST_CASE( "hlth-b01_distributed_damage_spreads_whole_hit_over_main_parts",
           "[audit][health]" )
{
    clear_avatar();
    Character &dummy = get_player_character();
    dummy.set_mutation( trait_SLIMESPAWNER );
    REQUIRE( dummy.has_flag( json_flag_DISTRIBUTED_DAMAGE ) );

    const std::vector<bodypart_id> main_parts =
        dummy.get_all_body_parts( get_body_part_flags::only_main );
    const std::vector<bodypart_id> minor_parts =
        dummy.get_all_body_parts( get_body_part_flags::only_minor );
    REQUIRE( !main_parts.empty() );
    REQUIRE( !minor_parts.empty() );
    const int num_parts = static_cast<int>( dummy.get_all_body_parts().size() );

    // A hit smaller than the number of body parts, and one that is not a multiple of it.
    for( const int dam : {
             num_parts - 1, 2 * num_parts + 5
         } ) {
        CAPTURE( dam );
        const int main_before = total_part_hp( dummy, main_parts );
        const int minor_before = total_part_hp( dummy, minor_parts );
        dummy.apply_damage( nullptr, main_parts.front(), dam );
        CHECK( total_part_hp( dummy, main_parts ) == main_before - dam );
        CHECK( total_part_hp( dummy, minor_parts ) == minor_before );
    }
    clear_avatar();
}
