#include "activity_type.h"
#include "avatar.h"
#include "cata_catch.h"
#include "character.h"
#include "game_constants.h"
#include "global_vars.h"
#include "map_helpers.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "translation.h"
#include "type_id.h"

static const activity_id ACT_MEDITATE( "ACT_MEDITATE" );
static const activity_id ACT_TEST_COPY_FROM_ABSTRACT( "ACT_TEST_COPY_FROM_ABSTRACT" );

static const effect_on_condition_id
effect_on_condition_EOC_TEST_ACT_INLINE_COMPLETE( "EOC_TEST_ACT_INLINE_COMPLETE" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_ACT_MEDITATE_TICK( "EOC_TEST_ACT_MEDITATE_TICK" );

// ACT-J01: activity types were loaded into a plain map that rejected a second definition,
// so a mod couldn't add EOCs to a vanilla activity, and copy-from, abstracts and inline EOCs
// weren't supported. TEST_DATA overrides ACT_MEDITATE and copies from an abstract.
TEST_CASE( "act-j01_mods_can_extend_activity_types", "[audit][activity]" )
{
    SECTION( "copy-from the same id keeps the vanilla fields and adds the EOC" ) {
        const activity_type &meditate = ACT_MEDITATE.obj();
        CHECK( meditate.verb().translated() == "meditating" );
        CHECK( meditate.rooted() );
        CHECK( meditate.based_on() == based_on_type::SPEED );
        CHECK( meditate.exertion_level() == NO_EXERCISE );
        CHECK( meditate.do_turn_EOC == effect_on_condition_EOC_TEST_ACT_MEDITATE_TICK );
        CHECK( meditate.do_turn_EOC.is_valid() );
    }
    SECTION( "copy-from an abstract, with an inline completion EOC" ) {
        const activity_type &act = ACT_TEST_COPY_FROM_ABSTRACT.obj();
        CHECK( act.verb().translated() == "testing inheritance" );
        CHECK( act.rooted() );
        CHECK( act.based_on() == based_on_type::TIME );
        CHECK( act.exertion_level() == LIGHT_EXERCISE );
        CHECK( act.completion_EOC == effect_on_condition_EOC_TEST_ACT_INLINE_COMPLETE );
        REQUIRE( act.completion_EOC.is_valid() );

        clear_avatar();
        clear_map_without_vision();
        global_variables &globvars = get_globals();
        globvars.clear_global_values();
        Character &u = get_avatar();
        u.assign_activity( ACT_TEST_COPY_FROM_ABSTRACT, 10 );
        while( !u.activity.is_null() ) {
            u.set_moves( u.get_speed() );
            u.activity.do_turn( u );
        }
        CHECK( globvars.get_global_value( "act_inline_complete" ) == 1 );
    }
}
