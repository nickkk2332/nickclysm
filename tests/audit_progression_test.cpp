#include <string>

#include "avatar.h"
#include "cata_catch.h"
#include "character.h"
#include "dialogue.h"
#include "math_parser.h"
#include "player_helpers.h"
#include "skill.h"
#include "type_id.h"

static const skill_id skill_melee( "melee" );

static double eval_math( dialogue &d, const std::string &expr )
{
    math_exp exp;
    REQUIRE( exp.parse( expr ) );
    return exp.eval( d );
}

// PROG-B01: nothing kept EOCs from storing a skill level of -1, after which
// SkillLevel::exercise() divided by 100 * ( level + 1 )^2 == 0 on every skill read.
TEST_CASE( "prog-b01_eoc_cannot_push_a_skill_below_level_0", "[audit][skill]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    dialogue d( get_talker_for( u ), nullptr );
    u.set_skill_level( skill_melee, 0 );
    u.get_skill_level_object( skill_melee ).set_exercise( 0, true );
    const SkillLevel &melee = u.get_skill_level_object( skill_melee );
    REQUIRE( melee.level() == 0 );

    SECTION( "lowering the level below 0" ) {
        eval_math( d, "u_skill('melee') -= 1" );
        CHECK( melee.level() == 0 );
        CHECK( u.get_skill_level( skill_melee ) == 0.0f );
    }

    SECTION( "negative experience at level 0" ) {
        eval_math( d, "u_skill_exp('melee') = -5" );
        CHECK( melee.level() == 0 );
        CHECK( melee.exercise( true ) == 0 );
        CHECK( u.get_skill_level( skill_melee ) == 0.0f );
    }

    SECTION( "negative experience above level 0 still drops a level" ) {
        u.set_skill_level( skill_melee, 2 );
        eval_math( d, "u_skill_exp('melee') = -5" );
        CHECK( melee.level() == 1 );
        CHECK( melee.exercise( true ) >= 0 );
        eval_math( d, "u_skill('melee') -= 1" );
        CHECK( melee.level() == 0 );
    }

    SECTION( "knowledge level" ) {
        u.set_knowledge_level( skill_melee, -1 );
        CHECK( melee.knowledgeLevel() == 0 );
        u.mod_knowledge_level( skill_melee, -3 );
        CHECK( melee.knowledgeLevel() == 0 );
    }
}
