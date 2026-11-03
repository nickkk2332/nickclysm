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

static const effect_on_condition_id
effect_on_condition_EOC_AUDIT_B07_REQUEUE( "EOC_AUDIT_B07_REQUEUE" );

static conditional_t::func load_condition( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    conditional_t::func condition;
    read_condition( jo, "condition", condition, false );
    finalize_conditions();
    return condition;
}

namespace
{
// Holds the avatar's EOC queue and the global one aside, empty, while a test runs EOC queues,
// and puts them back afterwards.
class eoc_queues_aside
{
    public:
        eoc_queues_aside() {
            avatar &u = get_avatar();
            avatar_queue = std::move( u.queued_effect_on_conditions );
            u.queued_effect_on_conditions = queued_eocs();
            avatar_inactive.swap( u.inactive_effect_on_condition_vector );
            global_queue = std::move( g->queued_global_effect_on_conditions );
            g->queued_global_effect_on_conditions = queued_eocs();
            global_inactive.swap( g->inactive_global_effect_on_condition_vector );
        }
        ~eoc_queues_aside() {
            avatar &u = get_avatar();
            u.queued_effect_on_conditions = std::move( avatar_queue );
            u.inactive_effect_on_condition_vector = std::move( avatar_inactive );
            g->queued_global_effect_on_conditions = std::move( global_queue );
            g->inactive_global_effect_on_condition_vector = std::move( global_inactive );
        }
        eoc_queues_aside( const eoc_queues_aside & ) = delete;
        eoc_queues_aside &operator=( const eoc_queues_aside & ) = delete;
    private:
        queued_eocs avatar_queue;
        std::vector<effect_on_condition_id> avatar_inactive;
        queued_eocs global_queue;
        std::vector<effect_on_condition_id> global_inactive;
};

int count_in( const queued_eocs &queue, const effect_on_condition_id &eoc )
{
    return std::count_if( queue.list.begin(), queue.list.end(), [&eoc]( const queued_eoc & q ) {
        return q.eoc == eoc;
    } );
}

// The list's own count agrees with its nodes, and every node is in the priority queue once.
bool queue_is_whole( const queued_eocs &queue )
{
    return queue.list.size() == static_cast<size_t>( std::distance( queue.list.begin(),
            queue.list.end() ) ) && queue.list.size() == queue.queue.size();
}

double global_number( const std::string &name )
{
    const diag_value *value = get_globals().maybe_get_global_value( name );
    return value ? value->dbl() : 0.0;
}
} // namespace

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

// EOC-B07: a queued EOC that queued itself again with no delay was popped again by the same
// pass of the queue, forever.  Here it stops itself after 50 runs; it should run once a pass.
TEST_CASE( "eoc-b07_requeue_with_no_delay_waits_for_the_next_pass", "[audit][eoc]" )
{
    const eoc_queues_aside aside;
    avatar &u = get_avatar();
    global_variables &globals = get_globals();
    globals.remove_global_value( "audit_b07_runs" );
    on_out_of_scope forget( [&globals]() {
        globals.remove_global_value( "audit_b07_runs" );
    } );

    effect_on_conditions::queue_effect_on_condition( 0_turns, effect_on_condition_EOC_AUDIT_B07_REQUEUE,
            u, {} );
    effect_on_conditions::process_effect_on_conditions( u );
    CHECK( global_number( "audit_b07_runs" ) == 1 );
    effect_on_conditions::process_effect_on_conditions( u );
    CHECK( global_number( "audit_b07_runs" ) == 2 );
    // The next run is waiting in the queue
    CHECK( count_in( u.queued_effect_on_conditions, effect_on_condition_EOC_AUDIT_B07_REQUEUE ) == 1 );
    CHECK( queue_is_whole( u.queued_effect_on_conditions ) );
}
