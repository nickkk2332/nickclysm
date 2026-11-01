#include <memory>

#include "avatar.h"
#include "cata_catch.h"
#include "character.h"
#include "dialogue.h"
#include "enums.h"
#include "math_parser.h"
#include "math_parser_type.h"
#include "player_helpers.h"
#include "talker.h"
#include "type_id.h"

static const limb_score_id limb_score_balance( "balance" );

// MATH-B01: limb_score() documents 'type' as optional, but omitting it made the
// evaluator call io::string_to_enum<bp_type>( "" ), which throws a non-math
// exception that nothing catches and terminates the game.
TEST_CASE( "math-b01_limb_score_without_type_kwarg", "[audit][math_parser]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    dialogue d( get_talker_for( u ), std::make_unique<talker>() );
    math_exp testexp;

    SECTION( "omitted type sums the score over all body part types" ) {
        REQUIRE( testexp.parse( "u_limb_score('balance')" ) );
        const float expected = u.get_limb_score( limb_score_balance );
        REQUIRE( expected > 0.0f );
        double result = 0.0;
        CHECK_NOTHROW( result = testexp.eval( d ) );
        CHECK( result == Approx( expected ) );
    }

    SECTION( "explicit type still selects that body part type" ) {
        REQUIRE( testexp.parse( "u_limb_score('balance', 'type': 'leg')" ) );
        const float expected = u.get_limb_score( limb_score_balance, bp_type::leg );
        CHECK( testexp.eval( d ) == Approx( expected ) );
    }

    SECTION( "unknown type raises a math error instead of a fatal exception" ) {
        REQUIRE( testexp.parse( "u_limb_score('balance', 'type': 'not_a_bp_type')" ) );
        CHECK_THROWS_AS( testexp.eval( d ), math::exception );
    }
}
