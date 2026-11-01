#include <set>
#include <string>
#include <vector>

#include "avatar.h"
#include "cata_catch.h"
#include "character.h"
#include "dialogue.h"
#include "math_parser.h"
#include "player_helpers.h"
#include "proficiency.h"
#include "profession.h"
#include "skill.h"
#include "type_id.h"

static const proficiency_id proficiency_prof_burn_care( "prof_burn_care" );
static const proficiency_id proficiency_prof_frying_dessert( "prof_frying_dessert" );
static const proficiency_id proficiency_prof_wound_care( "prof_wound_care" );

static const profession_id profession_firefighter( "firefighter" );
static const profession_id profession_frying( "frying" );

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

// PROG-B02: profession and hobby proficiencies were granted without their prerequisites, so
// one listed before its prerequisite was silently skipped: firefighter lists Burn Care before
// Wound Care, and the frying hobby lists Dessert Baking without Principles of Baking, which
// also cost it Fried Desserts.
TEST_CASE( "prog-b02_listed_proficiencies_are_granted_regardless_of_order", "[audit][chargen]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    const profession &firefighter = profession_firefighter.obj();
    const profession &frying = profession_frying.obj();
    // The data still lists them in the order that used to fail.
    const std::vector<proficiency_id> ff_profs = firefighter.proficiencies();
    REQUIRE( ff_profs.size() >= 2 );
    REQUIRE( ff_profs.front() == proficiency_prof_burn_care );
    REQUIRE( proficiency_prof_burn_care->required_proficiencies().count(
                 proficiency_prof_wound_care ) == 1 );

    u.prof = &firefighter;
    u.hobbies.insert( &frying );
    Character &you = u;
    you.initialize( false );

    CHECK( u.has_proficiency( proficiency_prof_burn_care ) );
    CHECK( u.has_proficiency( proficiency_prof_frying_dessert ) );
    for( const proficiency_id &prof : firefighter.proficiencies() ) {
        CAPTURE( prof.str() );
        CHECK( u.has_proficiency( prof ) );
    }
    for( const proficiency_id &prof : frying.proficiencies() ) {
        CAPTURE( prof.str() );
        CHECK( u.has_proficiency( prof ) );
    }

    clear_avatar();
}
