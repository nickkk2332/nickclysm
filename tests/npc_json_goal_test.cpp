#include <algorithm>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "avatar.h"
#include "behavior.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "condition.h"
#include "coordinates.h"
#include "creature_tracker.h"
#include "debug.h"
#include "dialogue.h"
#include "errand.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "faction.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "global_vars.h"
#include "item.h"
#include "item_location.h"
#include "json.h"
#include "json_error.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser_diag_value.h"
#include "monster.h"
#include "npc.h"
#include "npc_arbitration.h"
#include "npc_decision_category.h"
#include "npc_goal_state.h"
#include "npc_turn_helpers.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "point.h"
#include "talker.h"
#include "type_id.h"

// JSON goals (doc/JSON/NPC_BEHAVIOR.md): decision categories as JSON, goals defined on behavior
// leaves, what runs them in npc::move(), their saves, what EOCs read about them and the decision
// debug view.  The goals, trees and hooks are in data/mods/TEST_DATA/npc_goals.json; the hooks
// count their runs and record their context values in globals.

static decision_category category( const std::string &id )
{
    return decision_category( npc_decision_category_id( id ) );
}

TEST_CASE( "npc_decision_categories_keep_the_built_in_order", "[npc][npc_goal]" )
{
    // the closed enum this replaced, first to last
    const std::vector<std::string> built_in = {
        "combat", "investigate", "needs", "follow", "order", "duty", "camp_work", "camp_travel",
        "free_time", "idle", "unmodeled"
    };
    std::vector<std::string> resolved;
    for( const npc_decision_category_id &id : npc_decision_category::order() ) {
        if( std::find( built_in.begin(), built_in.end(), id.str() ) != built_in.end() ) {
            resolved.push_back( id.str() );
        }
    }
    CHECK( resolved == built_in );
    const std::vector<const decision_category *> constants = {
        &decision_category::combat, &decision_category::investigate, &decision_category::needs,
        &decision_category::follow, &decision_category::order, &decision_category::duty,
        &decision_category::camp_work, &decision_category::camp_travel,
        &decision_category::free_time, &decision_category::idle, &decision_category::unmodeled
    };
    for( size_t i = 0; i + 1 < constants.size(); ++i ) {
        CAPTURE( category_name( *constants[i] ), category_name( *constants[i + 1] ) );
        CHECK( *constants[i] < *constants[i + 1] );
        CHECK( constants[i]->id().str() == built_in[i] );
    }
    // the built-in goals keep their categories, now read from the JSON
    CHECK( bt_goal_to_category( "flee" ) == decision_category::combat );
    CHECK( bt_goal_to_category( "start_fire" ) == decision_category::needs );
    CHECK( bt_goal_to_category( "follow_embarked" ) == decision_category::follow );
    CHECK( bt_goal_to_category( "hold_position" ) == decision_category::duty );
    CHECK( bt_goal_to_category( "return_to_camp" ) == decision_category::camp_travel );
    CHECK( bt_goal_to_category( "idle" ) == decision_category::idle );
    for( const std::string &goal : builtin_npc_goals() ) {
        CAPTURE( goal );
        CHECK( bt_goal_to_category( goal ) != decision_category::unmodeled );
    }
    CHECK( decision_category::needs.hysteresis() == Approx( 0.1f ) );
}

TEST_CASE( "npc_decision_category_placed_before_and_after", "[npc][npc_goal]" )
{
    // TEST_DATA: test_goal_top before combat, test_goal_early before needs, test_goal_late
    // after free_time
    const std::vector<npc_decision_category_id> &order = npc_decision_category::order();
    const auto at = [&order]( const std::string & id ) {
        return std::find( order.begin(), order.end(), npc_decision_category_id( id ) ) - order.begin();
    };
    CHECK( at( "test_goal_top" ) == 0 );
    CHECK( at( "test_goal_top" ) + 1 == at( "combat" ) );
    CHECK( at( "test_goal_early" ) + 1 == at( "needs" ) );
    CHECK( at( "investigate" ) + 1 == at( "test_goal_early" ) );
    CHECK( at( "free_time" ) + 1 == at( "test_goal_late" ) );
    CHECK( at( "test_goal_late" ) + 1 == at( "idle" ) );
    CHECK( category( "test_goal_early" ).hysteresis() == Approx( 0.5f ) );
    CHECK( category( "test_goal_late" ).hysteresis() == Approx( 0.1f ) );

    SECTION( "resolving an order" ) {
        const auto id = []( const char *name ) {
            return npc_decision_category_id( name );
        };
        std::vector<std::string> errors;
        const std::vector<npc_decision_category_id> resolved = resolve_category_order( {
            { id( "a" ), std::nullopt, std::nullopt },
            { id( "x" ), std::nullopt, id( "a" ) },
            { id( "b" ), std::nullopt, std::nullopt },
            { id( "y" ), std::nullopt, id( "a" ) },
            { id( "z" ), id( "a" ), std::nullopt },
            { id( "w" ), std::nullopt, id( "y" ) },
        }, errors );
        // after a in load order, w after y, z before a
        CHECK( resolved == std::vector<npc_decision_category_id> {
            id( "z" ), id( "a" ), id( "x" ), id( "y" ), id( "w" ), id( "b" )
        } );
        CHECK( errors.empty() );

        resolve_category_order( {
            { id( "a" ), std::nullopt, std::nullopt },
            { id( "u" ), id( "nowhere" ), std::nullopt },
            { id( "s" ), std::nullopt, id( "s" ) },
            { id( "p" ), std::nullopt, id( "q" ) },
            { id( "q" ), id( "p" ), std::nullopt },
        }, errors );
        REQUIRE( errors.size() == 4 );
        CHECK( errors[0].find( "doesn't exist" ) != std::string::npos );
        CHECK( errors[1].find( "itself" ) != std::string::npos );
        CHECK( errors[2].find( "loop" ) != std::string::npos );
    }
    SECTION( "a category goes before or after, not both" ) {
        npc_decision_category cat;
        const JsonObject jo = json_loader::from_string(
                                  R"({ "id": "x", "before": "needs", "after": "combat" })" );
        jo.allow_omitted_members();
        CHECK_THROWS_AS( cat.load( jo, "test" ), JsonError );
    }
}
