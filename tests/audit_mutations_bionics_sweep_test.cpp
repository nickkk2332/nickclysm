#include <array>
#include <optional>
#include <string>

#include "avatar.h"
#include "bionics.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "coordinates.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "item.h"
#include "item_location.h"
#include "itype.h"
#include "iuse.h"
#include "magic.h"
#include "map.h"
#include "math_parser_diag_value.h"
#include "monster.h"
#include "npc.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "point.h"
#include "talker.h"
#include "type_id.h"
#include "units.h"

static const json_character_flag json_flag_TEST_MUTB06_OFF( "TEST_MUTB06_OFF" );
static const json_character_flag json_flag_TEST_MUTB06_ON( "TEST_MUTB06_ON" );

static const trait_id trait_TEST_MUTB06_TOGGLE( "TEST_MUTB06_TOGGLE" );

// MUT-B06: has_trait_flag() and count_trait_flag() read the avatar's activation state for a
// trait's active_flags and inactive_flags, whoever they were asked about.
TEST_CASE( "mut-b06_active_flags_follow_the_characters_own_trait_state",
           "[audit][mutation]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    standard_npc guy( "Toggler" );
    you.set_mutation( trait_TEST_MUTB06_TOGGLE );
    guy.set_mutation( trait_TEST_MUTB06_TOGGLE );
    REQUIRE( you.can_power_mutation( trait_TEST_MUTB06_TOGGLE ) );
    REQUIRE( guy.can_power_mutation( trait_TEST_MUTB06_TOGGLE ) );

    SECTION( "only the NPC has it on" ) {
        guy.activate_mutation( trait_TEST_MUTB06_TOGGLE );
        REQUIRE( guy.has_active_mutation( trait_TEST_MUTB06_TOGGLE ) );
        REQUIRE_FALSE( you.has_active_mutation( trait_TEST_MUTB06_TOGGLE ) );
        CHECK( guy.has_trait_flag( json_flag_TEST_MUTB06_ON ) );
        CHECK_FALSE( guy.has_trait_flag( json_flag_TEST_MUTB06_OFF ) );
        CHECK( guy.count_trait_flag( json_flag_TEST_MUTB06_ON ) == 1 );
        CHECK( guy.count_trait_flag( json_flag_TEST_MUTB06_OFF ) == 0 );
        CHECK_FALSE( you.has_trait_flag( json_flag_TEST_MUTB06_ON ) );
        CHECK( you.has_trait_flag( json_flag_TEST_MUTB06_OFF ) );
    }
    SECTION( "only the avatar has it on" ) {
        you.activate_mutation( trait_TEST_MUTB06_TOGGLE );
        REQUIRE( you.has_active_mutation( trait_TEST_MUTB06_TOGGLE ) );
        REQUIRE_FALSE( guy.has_active_mutation( trait_TEST_MUTB06_TOGGLE ) );
        CHECK_FALSE( guy.has_trait_flag( json_flag_TEST_MUTB06_ON ) );
        CHECK( guy.has_trait_flag( json_flag_TEST_MUTB06_OFF ) );
        CHECK( guy.count_trait_flag( json_flag_TEST_MUTB06_ON ) == 0 );
        CHECK( guy.count_trait_flag( json_flag_TEST_MUTB06_OFF ) == 1 );
        CHECK( you.has_trait_flag( json_flag_TEST_MUTB06_ON ) );
        CHECK_FALSE( you.has_trait_flag( json_flag_TEST_MUTB06_OFF ) );
    }
    clear_avatar();
}
