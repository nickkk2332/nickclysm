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

static const effect_on_condition_id
effect_on_condition_EOC_TEST_MUTB07_ACTIVATE( "EOC_TEST_MUTB07_ACTIVATE" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_MUTB07_DEACTIVATE( "EOC_TEST_MUTB07_DEACTIVATE" );

static const trait_id trait_TEST_MUTB07_FORM( "TEST_MUTB07_FORM" );
static const trait_id trait_TEST_MUTB07_FORM_ALT( "TEST_MUTB07_FORM_ALT" );
static const trait_id trait_TEST_MUTB07_TOGGLE( "TEST_MUTB07_TOGGLE" );

// A numeric character variable as math sees it (unset reads as 0).
static double char_var( const Character &you, const std::string &name )
{
    const diag_value *val = you.maybe_get_value( name );
    return val == nullptr ? 0.0 : val->dbl();
}

// MUT-B07: switching a trait the character lacked on or off made a cache entry for it, so
// has_trait() reported it. Activation also paid for it and ran its EOCs; deactivation ran
// its EOCs and its transform, which granted the transform's target.
TEST_CASE( "mut-b07_switching_a_trait_you_lack_does_nothing", "[audit][mutation][eoc]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    REQUIRE_FALSE( you.has_trait( trait_TEST_MUTB07_TOGGLE ) );
    REQUIRE_FALSE( you.has_trait( trait_TEST_MUTB07_FORM ) );
    REQUIRE_FALSE( you.has_trait( trait_TEST_MUTB07_FORM_ALT ) );

    SECTION( "from code" ) {
        you.activate_mutation( trait_TEST_MUTB07_TOGGLE );
        you.deactivate_mutation( trait_TEST_MUTB07_TOGGLE );
        you.deactivate_mutation( trait_TEST_MUTB07_FORM );
    }
    SECTION( "from EOCs" ) {
        dialogue d( get_talker_for( you ), nullptr );
        effect_on_condition_EOC_TEST_MUTB07_ACTIVATE->activate( d );
        effect_on_condition_EOC_TEST_MUTB07_DEACTIVATE->activate( d );
    }
    CHECK_FALSE( you.has_trait( trait_TEST_MUTB07_TOGGLE ) );
    CHECK_FALSE( you.has_active_mutation( trait_TEST_MUTB07_TOGGLE ) );
    CHECK_FALSE( you.has_trait( trait_TEST_MUTB07_FORM ) );
    CHECK_FALSE( you.has_trait( trait_TEST_MUTB07_FORM_ALT ) );
    CHECK( char_var( you, "mutb07_on" ) == 0 );
    CHECK( char_var( you, "mutb07_off" ) == 0 );
    clear_avatar();
}

TEST_CASE( "mut-b07_switching_a_trait_you_have_still_works", "[audit][mutation][eoc]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    you.set_mutation( trait_TEST_MUTB07_TOGGLE );
    you.set_mutation( trait_TEST_MUTB07_FORM );
    dialogue d( get_talker_for( you ), nullptr );

    effect_on_condition_EOC_TEST_MUTB07_ACTIVATE->activate( d );
    CHECK( you.has_active_mutation( trait_TEST_MUTB07_TOGGLE ) );
    CHECK( char_var( you, "mutb07_on" ) == 1 );

    effect_on_condition_EOC_TEST_MUTB07_DEACTIVATE->activate( d );
    CHECK( you.has_trait( trait_TEST_MUTB07_TOGGLE ) );
    CHECK_FALSE( you.has_active_mutation( trait_TEST_MUTB07_TOGGLE ) );
    CHECK( char_var( you, "mutb07_off" ) == 1 );
    CHECK_FALSE( you.has_trait( trait_TEST_MUTB07_FORM ) );
    CHECK( you.has_trait( trait_TEST_MUTB07_FORM_ALT ) );
    clear_avatar();
}
