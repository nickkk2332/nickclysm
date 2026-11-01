#include <string>

#include "cata_catch.h"
#include "condition.h"
#include "dialogue.h"
#include "flexbuffer_json.h"
#include "json_error.h"
#include "json_loader.h"

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
