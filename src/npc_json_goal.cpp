#include "npc_json_goal.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <set>
#include <tuple>
#include <unordered_map>
#include <utility>

#include "avatar.h"
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
#include "line.h"
#include "global_vars.h"
#include "math_parser_diag_value.h"
#include "npc.h"
#include "npc_ai_tuning.h"
#include "npc_arbitration.h"
#include "npc_goal_state.h"
#include "string_formatter.h"
#include "talker.h"

static const efftype_id effect_sleep( "sleep" );

static const json_character_flag json_flag_CANNOT_SLEEP( "CANNOT_SLEEP" );

static const string_id<behavior::node_t> behavior_node_t_npc_decision( "npc_decision" );

int npc_goal_runtime::reaction_tree_walks = 0;

namespace
{
npc_goal_registry loaded_goals;
int loaded_best_rank = std::numeric_limits<int>::max();
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

const char *status_name( const behavior::status_t status )
{
    switch( status ) {
        case behavior::status_t::running:
            return "running";
        case behavior::status_t::success:
            return "success";
        case behavior::status_t::failure:
            return "failure";
    }
    return "failure";
}
} // namespace

bool npc_goal_registry::load_node( const JsonObject &jo, const string_id<behavior::node_t> &node,
                                   std::string_view src, std::string &goal_id )
{
    if( !jo.has_member( "goal" ) && !jo.has_member( "category" ) ) {
        // no goal of its own, or an override that keeps the one it has
        return false;
    }
    const std::string source = "behavior " + node.str();
    // loaded again with a goal or a category (a mod overriding the node): what it said before
    // no longer holds
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
    std::vector<std::string> errors = check_goal_claims( category_goal_claims(), claims_ );
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
    loaded_best_rank = std::numeric_limits<int>::max();
}

int npc_json_goals::best_rank()
{
    return loaded_best_rank;
}

void npc_json_goals::refresh_ranks()
{
    loaded_best_rank = std::numeric_limits<int>::max();
    for( const auto &entry : loaded_goals.all() ) {
        loaded_best_rank = std::min( loaded_best_rank, decision_category( entry.second.category ).rank() );
    }
}

void npc_json_goals::swap_registry_for_test( npc_goal_registry &other )
{
    std::swap( loaded_goals, other );
    refresh_ranks();
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
    const behavior::character_oracle_t oracle( &guy );
    return score( goal, oracle );
}

float npc_goal_runtime::score( const npc_json_goal &goal, const behavior::oracle_t &oracle )
{
    if( !goal.node.is_valid() ) {
        return 0.0f;
    }
    const behavior::behavior_return picked = goal.node.obj().tick( &oracle );
    return picked.result == behavior::status_t::running ? picked.score : 0.0f;
}

bool npc_goal_runtime::node_applies( const npc_json_goal &goal, const behavior::oracle_t &oracle )
{
    return goal.node.is_valid() &&
           goal.node.obj().tick( &oracle ).result == behavior::status_t::running;
}

namespace
{
// a built-in goal the NPC is committed to is over by its own rule, `picked` being the tree's
// pick; keyed by the goal, whatever its category
bool builtin_goal_over( const npc &guy, const std::string &committed, const std::string &picked )
{
    if( committed == "return_to_guard_pos" ) {
        const std::optional<tripoint_abs_ms> gp = guy.get_effective_guard_pos();
        return gp && guy.pos_abs() == *gp;
    }
    if( committed == "hold_position" ) {
        // persists only while the tree keeps picking it: off shift or displaced, the fresh
        // goal takes over
        return picked != "hold_position";
    }
    if( committed == "follow_player" ) {
        const Character &pc = get_player_character();
        return ( rl_dist( guy.pos_bub(), pc.pos_bub() ) <= guy.follow_distance() &&
                 guy.posz() == pc.posz() ) || picked != "follow_player";
    }
    if( committed == "fight" || committed == "flee" ) {
        // only as long as the tree keeps picking it
        return picked != committed;
    }
    if( committed == "follow_embarked" ) {
        return picked != "follow_embarked";
    }
    if( committed == "goto_ordered_position" ) {
        return !guy.walk || errand::walk_arrived( guy, *guy.walk ) ||
               picked != "goto_ordered_position";
    }
    if( const std::optional<npc::need_goal_id> gid = npc::goal_id_for( committed ) ) {
        const behavior::character_oracle_t oracle( &guy );
        bool satisfied = false;
        switch( *gid ) {
            case npc::need_goal_id::eat_food:
            case npc::need_goal_id::drink_water:
                // a meal ends when it's enough, not when the body has caught up
                satisfied = !guy.intake_useful( *gid );
                break;
            case npc::need_goal_id::regulate_temperature:
                satisfied = oracle.needs_temperature_regulation( "" ) != behavior::status_t::running;
                break;
            case npc::need_goal_id::go_to_sleep:
                // leaving for another goal goes through judge_interrupt(); woken half way to
                // tired, it lies back down first
                satisfied = guy.has_effect( effect_sleep ) ||
                            ( oracle.needs_sleep_badly( "" ) != behavior::status_t::running &&
                              !guy.get_sleep_session().wants_resume( guy.get_sleepiness(),
                                      ai_tuning_of( guy ) ) );
                break;
        }
        return satisfied || guy.plan_for( *gid ).last_result == npc::need_result::impossible;
    }
    if( committed == "start_fire" ) {
        // legacy warmth fallback with no executor: ends with the cold, or when the tree stops
        // picking it
        const behavior::character_oracle_t oracle( &guy );
        return oracle.needs_temperature_regulation( "" ) != behavior::status_t::running ||
               picked != committed;
    }
    if( committed == "camp_work" || committed == "return_to_camp" || committed == "free_time" ) {
        return picked != committed;
    }
    return false;
}
} // namespace

npc_goal_choice npc_goal_runtime::choose( npc &guy, const behavior::oracle_t &oracle )
{
    npc_goal_choice c;
    behavior::tree tree;
    tree.add( &npc_decision_tree() );
    std::tie( c.picked, c.picked_score ) = tree.tick_full( &oracle );
    const npc_ai_tuning &tuning = ai_tuning_of( guy );
    if( guy.goal_is_idle( c.picked ) && !guy.sleep_on_hold() &&
        !guy.has_flag( json_flag_CANNOT_SLEEP ) &&
        ( ( guy.sleep_stalled() && guy.get_sleepiness() >= tuning.tired_level() ) ||
          guy.get_sleep_session().wants_resume( guy.get_sleepiness(), tuning ) ) &&
        guy.best_need_view( npc::need_goal_id::go_to_sleep ).may_consider() ) {
        // a sleep that stepped aside for work, or was woken early, comes back when nothing
        // else is on offer
        c.picked = "go_to_sleep";
        c.picked_score = behavior::character_oracle_t( &guy ).sleepiness_urgency( "" );
    }
    c.goal = c.picked;
    c.score = c.picked_score;
    const npc_intention &in = guy.ai_cache.intention;
    // a JSON goal loaded from a save is the commitment, as sync() makes it
    c.committed = in.goal.empty() && guy.json_goal->active() ? guy.json_goal->goal : in.goal;
    if( c.committed.empty() ) {
        return c;
    }
    npc_pursuit current;
    if( const npc_json_goal *json = npc_json_goals::find( c.committed ) ) {
        current.goal = c.committed;
        current.selection = score( *json, oracle );
        c.committed_score = current.selection;
        if( const std::optional<std::string> why = finish_reason( guy ) ) {
            c.completed = true;
            c.json_end = *why;
        } else {
            c.own_walk = c.picked == "goto_ordered_position" && owns_walk( guy );
            // a goal with no end of its own lasts while its node applies or the tree picks it
            if( !c.own_walk && c.picked != c.committed && !json->until && !json->timeout &&
                !node_applies( *json, oracle ) ) {
                c.completed = true;
                c.json_end = npc_goal_reason::interrupted;
            }
        }
        if( c.completed ) {
            if( c.picked == "goto_ordered_position" && owns_walk( guy ) ) {
                // the goal's walk ends with it
                c.goal = "idle";
                c.score = 0.0f;
            }
            return c;
        }
        if( c.own_walk ) {
            c.goal = c.committed;
            c.score = current.selection;
            c.verdict = { false, "the tree's pick is the goal's own walk" };
            return c;
        }
    } else if( builtin_goal_over( guy, c.committed, c.picked ) ) {
        c.completed = true;
        return c;
    } else {
        current = guy.committed_pursuit();
    }
    if( npc::goal_id_for( c.committed ) && c.picked == "start_fire" ) {
        c.verdict = { false, "start_fire has no executor and doesn't displace a need that has one" };
    } else {
        c.verdict = judge_interrupt( current, { c.picked, c.picked_score, guy.goal_is_idle( c.picked ) },
                                     tuning.need_critical_urgency );
    }
    c.committed_score = current.selection;
    c.interrupted = c.verdict.interrupt;
    if( !c.interrupted ) {
        c.goal = c.committed;
        c.score = current.selection;
    }
    return c;
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
    // only a JSON goal at or above the reaction can keep the NPC from it: look at the tree
    // only when some category that early holds one, and it has one
    if( npc_json_goals::best_rank() > reaction.rank() ) {
        return false;
    }
    ++reaction_tree_walks;
    std::set<const behavior::node_t *> seen;
    if( !tree_has_goal_at_or_above( npc_decision_tree(), reaction, seen ) ) {
        return false;
    }
    // and only when it would win this turn's decision
    const behavior::character_oracle_t oracle( &guy );
    const npc_goal_choice choice = choose( guy, oracle );
    const npc_json_goal *goal = npc_json_goals::find( choice.goal );
    return goal != nullptr && choice.goal != choice.committed &&
           !( reaction < decision_category( goal->category ) );
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

namespace
{
// depth first from node; true when it's on the path to `picked`
bool describe_node( const behavior::node_t &node, const behavior::oracle_t *oracle,
                    const behavior::node_t *picked, const int depth,
                    std::set<const behavior::node_t *> &on_path_to_here,
                    std::vector<npc_decision_report::node_line> &lines )
{
    npc_decision_report::node_line line;
    line.depth = depth;
    line.id = node.id.str();
    for( const behavior::node_t::condition_trace &trace : node.trace_conditions( oracle ) ) {
        line.conditions.push_back( { trace.label, trace.result == behavior::status_t::running } );
    }
    const behavior::behavior_return result = node.tick( oracle );
    line.result = status_name( result.result );
    if( node.has_score() && result.result == behavior::status_t::running ) {
        line.score = result.score;
    }
    if( node.get_children().empty() ) {
        line.goal = node.goal();
        line.category = category_name( bt_goal_to_category( line.goal ) );
    }
    const size_t at = lines.size();
    lines.push_back( line );
    bool picked_here = &node == picked;
    // a tree that loops back on itself is shown once
    if( on_path_to_here.insert( &node ).second ) {
        for( const behavior::node_t *child : node.get_children() ) {
            picked_here |= describe_node( *child, oracle, picked, depth + 1, on_path_to_here, lines );
        }
        on_path_to_here.erase( &node );
    }
    lines[at].picked = picked_here;
    return picked_here;
}
} // namespace

npc_decision_report npc_goal_runtime::report( const npc &guy )
{
    npc_decision_report rep;
    // no evaluate_every cache: what the view shows must not change what the NPC's walk sees
    const behavior::character_oracle_t oracle( &guy, false );
    const behavior::node_t &root = npc_decision_tree();
    const behavior::behavior_return picked = root.tick( &oracle );
    const behavior::node_t *picked_node = picked.result == behavior::status_t::running ?
                                          picked.selection : nullptr;
    std::set<const behavior::node_t *> on_path;
    describe_node( root, &oracle, picked_node, 0, on_path, rep.nodes );

    for( const npc_decision_category_id &id : npc_decision_category::order() ) {
        rep.categories.push_back( { id.str(), id.obj().rank, id.obj().hysteresis } );
    }

    // the decision npc::move() makes, the same way
    const npc_goal_choice c = choose( const_cast<npc &>( guy ), oracle );
    rep.picked_goal = c.picked;
    rep.picked_score = c.picked_score;
    rep.picked_category = category_name( bt_goal_to_category( c.picked ) );
    rep.committed_goal = c.committed.empty() ? "idle" : c.committed;
    rep.committed_category = category_name( bt_goal_to_category( rep.committed_goal ) );
    rep.committed_turns = turns_on_goal( guy );
    rep.committed_score = c.committed_score;
    if( c.committed.empty() ) {
        rep.verdict = string_format( "nothing is committed: %s is taken up", c.goal );
    } else if( c.completed ) {
        rep.verdict = c.json_end.empty() ?
                      string_format( "%s is over: %s is taken up", c.committed, c.goal ) :
                      string_format( "%s ends this turn (%s): %s is taken up", c.committed, c.json_end,
                                     c.goal );
    } else {
        if( c.own_walk ) {
            rep.verdict = string_format( "%s goes on: %s", c.committed, c.verdict.why );
        } else if( c.picked == c.committed ) {
            rep.verdict = string_format( "the tree picks %s again", c.committed );
        } else {
            rep.verdict = string_format( "%s %s %s: %s", c.picked,
                                         c.interrupted ? "takes over from" : "doesn't take over from",
                                         c.committed, c.verdict.why );
        }
    }
    return rep;
}
