#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "cata_variant.h"
#include "character.h"
#include "character_id.h"
#include "condition.h"
#include "coordinates.h"
#include "debug.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "faction.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "global_vars.h"
#include "item.h"
#include "item_location.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "mapbuffer.h"
#include "mapgendata.h"
#include "math_parser_diag_value.h"
#include "monster.h"
#include "npc.h"
#include "omdata.h"
#include "overmap.h"
#include "overmapbuffer.h"
#include "player_helpers.h"
#include "point.h"
#include "talker.h"
#include "timed_event.h"
#include "type_id.h"
#include "viewer.h"

static conditional_t::func load_condition( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    conditional_t::func condition;
    read_condition( jo, "condition", condition, false );
    finalize_conditions();
    return condition;
}

// EOC-B06: test_eoc called the EOC's condition directly, and an EOC without one has an empty
// std::function, so the call threw std::bad_function_call.
TEST_CASE( "eoc-b06_test_eoc_on_an_eoc_without_a_condition", "[audit][eoc]" )
{
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    const conditional_t::func no_condition = load_condition(
                R"({ "condition": { "test_eoc": "EOC_AUDIT_B06_NO_CONDITION" } })" );
    const conditional_t::func false_condition = load_condition(
                R"({ "condition": { "test_eoc": "EOC_AUDIT_B06_FALSE_CONDITION" } })" );
    CHECK( no_condition( d ) );
    CHECK_FALSE( false_condition( d ) );
    // Testing runs no effects
    CHECK_FALSE( get_globals().maybe_get_global_value( "audit_b06_ran" ) );
}
