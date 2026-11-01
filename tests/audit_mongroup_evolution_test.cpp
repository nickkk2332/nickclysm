#include <map>
#include <string>

#include "calendar.h"
#include "cata_catch.h"
#include "flexbuffer_json.h"
#include "json_loader.h"
#include "mongroup.h"
#include "options_helpers.h"
#include "type_id.h"

static const mongroup_id GROUP_AUDIT_MONDT_B02( "GROUP_AUDIT_MONDT_B02" );

// Loads a throwaway monster group with the given evolution multiplier and returns its
// entries (by value), removing the group again so it cannot leak into other tests.
static FreqDef load_evolution_test_group( const std::string &multiplier )
{
    override_option evo( "EVOLUTION_INVERSE_MULTIPLIER", multiplier );
    const std::string json = R"({
        "id": "GROUP_AUDIT_MONDT_B02",
        "default": "mon_null",
        "monsters": [
            { "monster": "mon_zombie", "weight": 10, "starts": "4 hours", "ends": "10 days" },
            { "monster": "mon_zombie", "weight": 10, "ends": "1 turns" },
            { "monster": "mon_zombie", "weight": 10 }
        ]
    })";
    JsonObject jo = json_loader::from_string( json );
    MonsterGroupManager::LoadMonsterGroup( jo );
    std::map<mongroup_id, MonsterGroup> &groups = MonsterGroupManager::Get_all_Groups();
    const FreqDef entries = groups[GROUP_AUDIT_MONDT_B02].monsters;
    groups.erase( GROUP_AUDIT_MONDT_B02 );
    return entries;
}

TEST_CASE( "mondt-b02_fractional_evolution_multiplier_scales_spawn_window", "[audit]" )
{
    SECTION( "hard preset 0.75 shortens the window instead of removing it" ) {
        const FreqDef entries = load_evolution_test_group( "0.75" );
        REQUIRE( entries.size() == 3 );
        // 4 hours * 0.75 = 3 hours; truncating the multiplier to 0 gave 0 turns.
        CHECK( to_turns<int>( entries[0].starts ) == to_turns<int>( 3_hours ) );
        // 10 days * 0.75 = 7.5 days; truncating to 0 made the entry last forever.
        CHECK( to_turns<int>( entries[0].ends ) == to_turns<int>( 180_hours ) );
        CHECK_FALSE( entries[0].lasts_forever() );
        // A tiny non-zero end must not round down to 0 (= lasts forever).
        CHECK( to_turns<int>( entries[1].starts ) == 0 );
        CHECK( to_turns<int>( entries[1].ends ) == 1 );
        CHECK_FALSE( entries[1].lasts_forever() );
        // Unset start/end stays unset.
        CHECK( to_turns<int>( entries[2].starts ) == 0 );
        CHECK( entries[2].lasts_forever() );
    }
    SECTION( "easy preset 1.5 lengthens the window instead of being ignored" ) {
        const FreqDef entries = load_evolution_test_group( "1.5" );
        REQUIRE( entries.size() == 3 );
        // Truncating 1.5 to 1 left these at 4 hours / 10 days.
        CHECK( to_turns<int>( entries[0].starts ) == to_turns<int>( 6_hours ) );
        CHECK( to_turns<int>( entries[0].ends ) == to_turns<int>( 15_days ) );
        CHECK( to_turns<int>( entries[1].ends ) == 1 );
    }
    SECTION( "multiplier 0 (evolution disabled) leaves the window unscaled" ) {
        const FreqDef entries = load_evolution_test_group( "0" );
        REQUIRE( entries.size() == 3 );
        CHECK( to_turns<int>( entries[0].starts ) == to_turns<int>( 4_hours ) );
        CHECK( to_turns<int>( entries[0].ends ) == to_turns<int>( 10_days ) );
    }
}
