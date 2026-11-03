#include "npc_json_goal.h"

#include <algorithm>
#include <memory>
#include <set>
#include <unordered_map>
#include <utility>

#include "behavior.h"
#include "calendar.h"
#include "character_oracle.h"
#include "condition.h"
#include "creature.h"
#include "debug.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "errand.h"
#include "event.h"
#include "event_bus.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "math_parser_diag_value.h"
#include "npc.h"
#include "npc_arbitration.h"
#include "npc_goal_state.h"
#include "string_formatter.h"
#include "talker.h"

static const string_id<behavior::node_t> behavior_node_t_npc_decision( "npc_decision" );

namespace
{
npc_goal_registry loaded_goals;
std::optional<string_id<behavior::node_t>> test_tree_root;

std::vector<effect_on_condition_id> load_hooks( const JsonObject &jo, std::string_view member,
        std::string_view src )
{
    std::vector<effect_on_condition_id> eocs;
    if( jo.has_array( member ) ) {
        for( const JsonValue jv : jo.get_array( member ) ) {
            eocs.push_back( effect_on_conditions::load_inline_eoc( jv, src ) );
        }
    } else if( jo.has_member( member ) ) {
        eocs.push_back( effect_on_conditions::load_inline_eoc( jo.get_member( member ), src ) );
    }
    return eocs;
}

global_variables::impl_t goal_context( const std::string &goal, const int turns,
                                       const std::string &reason )
{
    global_variables::impl_t context;
    context["goal_id"] = diag_value( goal );
    context["turns_active"] = diag_value( static_cast<double>( turns ) );
    if( !reason.empty() ) {
        context["reason"] = diag_value( reason );
    }
    return context;
}

void run_hooks( npc &guy, const std::vector<effect_on_condition_id> &eocs,
                const global_variables::impl_t &context )
{
    if( eocs.empty() ) {
        return;
    }
    // npc is an empty talker: npc_ conditions and n_ math read nothing instead of crashing
    dialogue d( get_talker_for( guy ), std::make_unique<talker>(), {}, context );
    for( const effect_on_condition_id &eoc : eocs ) {
        eoc->activate( d );
    }
}

// started by an EOC of the goal: the walk keeps the context it was sent with
bool walk_is_for( const walk_order &w, const std::string &goal )
{
    const auto it = w.context.find( "goal_id" );
    return it != w.context.end() && it->second.is_str() && it->second.str() == goal;
}

// a leaf under node has a JSON goal of category `cat` or one before it
bool tree_has_goal_at_or_above( const behavior::node_t &node, const decision_category &cat,
                                std::set<const behavior::node_t *> &seen )
{
    if( !seen.insert( &node ).second ) {
        return false;
    }
    if( node.get_children().empty() ) {
        const npc_json_goal *goal = npc_json_goals::find( node.goal() );
        return goal != nullptr && !( cat < decision_category( goal->category ) );
    }
    return std::any_of( node.get_children().begin(), node.get_children().end(),
    [&cat, &seen]( const behavior::node_t *child ) {
        return tree_has_goal_at_or_above( *child, cat, seen );
    } );
}

} // namespace

bool npc_goal_registry::load_node( const JsonObject &jo, const string_id<behavior::node_t> &node,
                                   std::string_view src, std::string &goal_id )
{
    const std::string source = "behavior " + node.str();
    // loaded again (a mod overriding the node): what it said before no longer holds
    claims_.erase( std::remove_if( claims_.begin(), claims_.end(),
    [&source]( const npc_goal_claim & claim ) {
        return claim.source == source;
    } ), claims_.end() );
    for( auto it = goals_.begin(); it != goals_.end(); ) {
        it = it->second.node == node ? goals_.erase( it ) : std::next( it );
    }

    std::optional<npc_decision_category_id> category;
    if( jo.has_member( "category" ) ) {
        category = npc_decision_category_id( jo.get_string( "category" ) );
    }
    if( !jo.has_object( "goal" ) ) {
        if( category ) {
            if( !jo.has_string( "goal" ) ) {
                jo.throw_error_at( "category", "\"category\" goes on a node with a goal" );
            }
            claims_.push_back( { jo.get_string( "goal" ), *category, source } );
        }
        return false;
    }
    const JsonObject jg = jo.get_object( "goal" );
    npc_json_goal goal;
    goal.id = jg.get_string( "id" );
    if( goal.id.empty() ) {
        jg.throw_error_at( "id", "a JSON goal needs an id" );
    }
    if( is_builtin_npc_goal( goal.id ) ) {
        jg.throw_error_at( "id", string_format(
                               "%s is a built-in goal; a JSON goal needs an id of its own", goal.id ) );
    }
    if( !category ) {
        jo.throw_error_at( "goal", "a JSON goal needs a \"category\" next to it" );
    }
    const auto other = goals_.find( goal.id );
    if( other != goals_.end() ) {
        jg.throw_error_at( "id", string_format( "goal %s is defined by behavior %s already", goal.id,
                                                other->second.node.str() ) );
    }
    goal.node = node;
    goal.category = *category;
    goal.on_start = load_hooks( jg, "on_start", src );
    goal.on_turn = load_hooks( jg, "on_turn", src );
    goal.on_end = load_hooks( jg, "on_end", src );
    goal.on_interrupt = load_hooks( jg, "on_interrupt", src );
    goal.on_timeout = load_hooks( jg, "on_timeout", src );
    if( jg.has_member( "until" ) ) {
        read_condition( jg, "until", goal.until, false );
    }
    if( jg.has_member( "timeout" ) ) {
        time_duration timeout;
        jg.read( "timeout", timeout, true );
        if( timeout <= 0_turns ) {
            jg.throw_error_at( "timeout", "timeout must be more than 0 turns" );
        }
        goal.timeout = timeout;
    }
    claims_.push_back( { goal.id, goal.category, source } );
    goal_id = goal.id;
    goals_.emplace( goal_id, std::move( goal ) );
    return true;
}

const npc_json_goal *npc_goal_registry::find( std::string_view id ) const
{
    const auto it = goals_.find( id );
    return it == goals_.end() ? nullptr : &it->second;
}

std::vector<std::string> npc_goal_registry::check() const
{
    std::vector<std::string> errors = check_goal_claims( claims_ );
    for( const auto &[id, goal] : goals_ ) {
        for( const std::vector<effect_on_condition_id> *hooks : {
                 &goal.on_start, &goal.on_turn, &goal.on_end, &goal.on_interrupt, &goal.on_timeout
             } ) {
            for( const effect_on_condition_id &eoc : *hooks ) {
                if( !eoc.is_valid() ) {
                    errors.push_back( string_format( "goal %s (behavior %s) runs effect_on_condition %s, "
                                                     "which doesn't exist", id, goal.node.str(), eoc.str() ) );
                }
            }
        }
    }
    return errors;
}

void npc_goal_registry::clear()
{
    goals_.clear();
    claims_.clear();
}

npc_goal_registry &npc_json_goals::registry()
{
    return loaded_goals;
}

const npc_json_goal *npc_json_goals::find( std::string_view id )
{
    if( id.empty() ) {
        return nullptr;
    }
    return loaded_goals.find( id );
}

void npc_json_goals::reset()
{
    loaded_goals.clear();
}

const behavior::node_t &npc_decision_tree()
{
    return ( test_tree_root ? *test_tree_root : behavior_node_t_npc_decision ).obj();
}

void set_npc_decision_tree_for_test( const std::optional<string_id<behavior::node_t>> &root )
{
    test_tree_root = root;
}

const std::string &npc_goal_runtime::current_goal( const npc &guy )
{
    static const std::string idle = "idle";
    const std::string &committed = guy.ai_cache.intention.goal;
    if( !committed.empty() ) {
        return committed;
    }
    if( guy.json_goal->active() ) {
        return guy.json_goal->goal;
    }
    return idle;
}

int npc_goal_runtime::turns_on_goal( const npc &guy )
{
    const npc_intention &in = guy.ai_cache.intention;
    if( !in.goal.empty() ) {
        return to_turns<int>( calendar::turn - in.since );
    }
    if( guy.json_goal->active() ) {
        return to_turns<int>( calendar::turn - guy.json_goal->started );
    }
    return 0;
}

void npc_goal_runtime::sync( npc &guy )
{
    npc_goal_state &state = *guy.json_goal;
    if( !state.active() ) {
        return;
    }
    npc_intention &in = guy.ai_cache.intention;
    if( npc_json_goals::find( state.goal ) == nullptr ) {
        // the data that defined it is gone: it ends without its hooks
        if( in.goal == state.goal ) {
            in = {};
        }
        state.goal.clear();
        state.turns_active = 0;
        return;
    }
    if( in.goal.empty() ) {
        // loaded from a save: the goal goes on where it was
        in = { state.goal, state.started, {}, npc_permission::allowed };
    } else if( in.goal != state.goal ) {
        end( guy, npc_goal_reason::interrupted );
    }
}

std::optional<std::string> npc_goal_runtime::finish_reason( const npc &guy )
{
    const npc_goal_state &state = *guy.json_goal;
    const npc_json_goal *goal = npc_json_goals::find( state.goal );
    if( goal == nullptr ) {
        return std::nullopt;
    }
    if( goal->until ) {
        const const_dialogue d( get_const_talker_for( guy ), std::make_unique<const_talker>(), {},
                                goal_context( state.goal, state.turns_active, std::string() ) );
        if( goal->until( d ) ) {
            return npc_goal_reason::done;
        }
    }
    if( goal->timeout && calendar::turn >= state.started + *goal->timeout ) {
        return npc_goal_reason::timeout;
    }
    return std::nullopt;
}

void npc_goal_runtime::end( npc &guy, const std::string &reason )
{
    npc_goal_state &state = *guy.json_goal;
    if( !state.active() ) {
        return;
    }
    // cleared first: the hooks may look at the NPC's goal, or end it again
    const std::string goal_id = state.goal;
    const int turns = state.turns_active;
    state.goal.clear();
    state.turns_active = 0;
    state.last_turn = calendar::before_time_starts;
    // the goal's errand ends with it, without the walk's own EOCs
    if( guy.walk && walk_is_for( *guy.walk, goal_id ) ) {
        errand::drop_walk( guy );
    }
    add_msg_debug( debugmode::DF_NPC_NEEDS, "NPC %s: goal %s ends: %s", guy.get_name(), goal_id,
                   reason );
    const npc_json_goal *goal = npc_json_goals::find( goal_id );
    if( goal == nullptr ) {
        return;
    }
    const global_variables::impl_t context = goal_context( goal_id, turns, reason );
    if( reason == npc_goal_reason::timeout ) {
        run_hooks( guy, goal->on_timeout, context );
    } else if( reason == npc_goal_reason::interrupted ) {
        run_hooks( guy, goal->on_interrupt, context );
    }
    run_hooks( guy, goal->on_end, context );
}

npc_goal_runtime::turn_result npc_goal_runtime::take_turn( npc &guy, const npc_json_goal &goal,
        const int moves_at_decision )
{
    npc_goal_state &state = *guy.json_goal;
    if( state.goal != goal.id ) {
        // another JSON goal still on record was replaced without being ended
        end( guy, npc_goal_reason::interrupted );
        state.goal = goal.id;
        state.started = calendar::turn;
        state.turns_active = 0;
        state.last_turn = calendar::before_time_starts;
        add_msg_debug( debugmode::DF_NPC_NEEDS, "NPC %s: starts goal %s", guy.get_name(), goal.id );
        run_hooks( guy, goal.on_start, goal_context( goal.id, 0, std::string() ) );
        if( state.goal != goal.id || guy.is_dead() ) {
            return turn_result::ended;
        }
    }
    if( state.last_turn != calendar::turn ) {
        state.last_turn = calendar::turn;
        ++state.turns_active;
        run_hooks( guy, goal.on_turn, goal_context( goal.id, state.turns_active, std::string() ) );
        if( state.goal != goal.id || guy.is_dead() ) {
            return turn_result::ended;
        }
    }
    if( owns_walk( guy ) && !errand::walk_arrived( guy, *guy.walk ) ) {
        return turn_result::walk;
    }
    if( guy.activity ) {
        return turn_result::activity;
    }
    if( guy.get_attitude() == NPCATT_ACTIVITY && ( guy.has_stashed_activity() ||
            guy.has_destination_activity() || !guy.get_auto_move_route().empty() ) ) {
        return turn_result::task;
    }
    if( guy.get_moves() < moves_at_decision ) {
        return turn_result::spent;
    }
    return turn_result::wait;
}

bool npc_goal_runtime::owns_walk( const npc &guy )
{
    return guy.walk && guy.json_goal->active() && walk_is_for( *guy.walk, guy.json_goal->goal );
}

float npc_goal_runtime::score( const npc &guy, const npc_json_goal &goal )
{
    if( !goal.node.is_valid() ) {
        return 0.0f;
    }
    const behavior::character_oracle_t oracle( &guy );
    const behavior::behavior_return picked = goal.node.obj().tick( &oracle );
    return picked.result == behavior::status_t::running ? picked.score : 0.0f;
}

bool npc_goal_runtime::holds_against( npc &guy, const decision_category &reaction )
{
    sync( guy );
    if( guy.json_goal->active() ) {
        if( outranks( guy, reaction ) ) {
            return true;
        }
        end( guy, npc_goal_reason::interrupted );
        guy.ai_cache.intention = {};
        report_change( guy );
        return false;
    }
    // only a goal at or above the reaction can keep the NPC from it: ask the tree only when
    // it has one
    if( npc_json_goals::registry().all().empty() ) {
        return false;
    }
    std::set<const behavior::node_t *> seen;
    if( !tree_has_goal_at_or_above( npc_decision_tree(), reaction, seen ) ) {
        return false;
    }
    const behavior::character_oracle_t oracle( &guy );
    behavior::tree tree;
    tree.add( &npc_decision_tree() );
    const npc_json_goal *picked = npc_json_goals::find( tree.tick( &oracle ) );
    return picked != nullptr && !( reaction < decision_category( picked->category ) );
}

bool npc_goal_runtime::outranks( const npc &guy, const decision_category &cat )
{
    const npc_json_goal *goal = npc_json_goals::find( guy.json_goal->goal );
    return goal != nullptr && !( cat < decision_category( goal->category ) );
}

void npc_goal_runtime::report_change( npc &guy )
{
    npc_goal_state &state = *guy.json_goal;
    const std::string &now = current_goal( guy );
    if( now == state.reported ) {
        return;
    }
    std::string old = std::exchange( state.reported, now );
    get_event_bus().send<event_type::npc_goal_changed>( guy.getID(), std::move( old ),
            std::string( now ) );
}
