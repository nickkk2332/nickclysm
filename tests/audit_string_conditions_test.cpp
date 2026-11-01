#include <string>

#include "avatar.h"
#include "cata_catch.h"
#include "character.h"
#include "condition.h"
#include "dialogue.h"
#include "flexbuffer_json.h"
#include "json_error.h"
#include "json_loader.h"
#include "player_helpers.h"
#include "type_id.h"

static const trait_id trait_NIGHTVISION( "NIGHTVISION" );

static void load_condition( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    jo.allow_omitted_members();
    conditional_t::func condition;
    read_condition( jo, "condition", condition, false );
}

// EOC-B02: a string condition that names no condition used to load as "always false"
// (or "always true" under "not") without any error, so typos in data went unnoticed.
TEST_CASE( "eoc-b02_unknown_string_conditions_fail_to_load", "[audit][condition]" )
{
    SECTION( "plain string" ) {
        CHECK_THROWS_AS( load_condition( R"({ "condition": "u_is_definitely_not_a_condition" })" ),
                         JsonError );
    }
    SECTION( "string under not" ) {
        CHECK_THROWS_AS( load_condition( R"({ "condition": { "not": "npc_see_you" } })" ),
                         JsonError );
    }
    SECTION( "string inside and/or" ) {
        const std::string in_and = R"({ "condition": { "and": [ "u_male", "is_night" ] } })";
        const std::string in_or = R"({ "condition": { "or": [ "has_many_assigned_mission" ] } })";
        CHECK_THROWS_AS( load_condition( in_and ), JsonError );
        CHECK_THROWS_AS( load_condition( in_or ), JsonError );
    }
    SECTION( "known strings still load" ) {
        CHECK_NOTHROW( load_condition( R"({ "condition": "u_male" })" ) );
        CHECK_NOTHROW( load_condition( R"({ "condition": { "not": "npc_see_u" } })" ) );
        CHECK_NOTHROW( load_condition(
                           R"({ "condition": { "and": [ "u_male", { "not": "is_day" } ] } })" ) );
    }
}

// EOC-B02 found an enchantment that wrote { "and": [ "ACTIVE", ... ] }, which can't work:
// "ACTIVE" is only meaningful as the whole enchantment condition. u_has_active_trait lets
// a dialogue condition check whether a mutation is switched on.
TEST_CASE( "u_has_active_trait_checks_the_mutation_is_switched_on", "[audit][condition]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    const JsonObject jo = json_loader::from_string(
                              R"({ "condition": { "u_has_active_trait": "NIGHTVISION" } })" );
    conditional_t::func condition;
    read_condition( jo, "condition", condition, false );
    dialogue d( get_talker_for( u ), nullptr );

    CHECK_FALSE( condition( d ) );
    // NIGHTVISION starts switched on when gained.
    u.set_mutation( trait_NIGHTVISION );
    u.deactivate_mutation( trait_NIGHTVISION );
    REQUIRE( u.has_trait( trait_NIGHTVISION ) );
    CHECK_FALSE( condition( d ) );
    u.activate_mutation( trait_NIGHTVISION );
    REQUIRE( u.has_active_mutation( trait_NIGHTVISION ) );
    CHECK( condition( d ) );
    u.deactivate_mutation( trait_NIGHTVISION );
    CHECK_FALSE( condition( d ) );
}
