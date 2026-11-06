#include "npc_decision_category.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>

#include "debug.h"
#include "flexbuffer_json.h"
#include "generic_factory.h"
#include "npc_json_goal.h"
#include "string_formatter.h"

static const npc_decision_category_id npc_decision_category_camp_travel( "camp_travel" );
static const npc_decision_category_id npc_decision_category_camp_work( "camp_work" );
static const npc_decision_category_id npc_decision_category_combat( "combat" );
static const npc_decision_category_id npc_decision_category_duty( "duty" );
static const npc_decision_category_id npc_decision_category_follow( "follow" );
static const npc_decision_category_id npc_decision_category_free_time( "free_time" );
static const npc_decision_category_id npc_decision_category_idle( "idle" );
static const npc_decision_category_id npc_decision_category_investigate( "investigate" );
static const npc_decision_category_id npc_decision_category_needs( "needs" );
static const npc_decision_category_id npc_decision_category_order( "order" );
static const npc_decision_category_id npc_decision_category_unmodeled( "unmodeled" );

namespace
{
generic_factory<npc_decision_category> category_factory( "npc_decision_category" );

// resolved by finalize_all()
std::vector<npc_decision_category_id> category_order;
// every goal with a category, built-in and JSON
std::unordered_map<std::string, npc_decision_category_id> goal_categories;
} // namespace

/** @relates string_id */
template<>
bool npc_decision_category_id::is_valid() const
{
    return category_factory.is_valid( *this );
}

/** @relates string_id */
template<>
const npc_decision_category &npc_decision_category_id::obj() const
{
    return category_factory.obj( *this );
}

void npc_decision_category::load_category( const JsonObject &jo, const std::string &src )
{
    category_factory.load( jo, src );
}

void npc_decision_category::load( const JsonObject &jo, std::string_view )
{
    optional( jo, was_loaded, "goals", goals, string_reader{} );
    if( jo.has_member( "before" ) && jo.has_member( "after" ) ) {
        jo.throw_error( "a category goes either before or after another one, not both" );
    }
    // a mod that overrides a category places it anew, or keeps it where it was
    if( jo.has_member( "before" ) || jo.has_member( "after" ) ) {
        before.reset();
        after.reset();
    }
    if( jo.has_string( "before" ) ) {
        before = npc_decision_category_id( jo.get_string( "before" ) );
    }
    if( jo.has_string( "after" ) ) {
        after = npc_decision_category_id( jo.get_string( "after" ) );
    }
    optional( jo, was_loaded, "hysteresis", hysteresis, 0.1f );
    if( hysteresis < 0.0f ) {
        jo.throw_error_at( "hysteresis", "hysteresis can't be negative" );
    }
}

void npc_decision_category::reset()
{
    category_factory.reset();
    category_order.clear();
    goal_categories.clear();
}

const std::vector<npc_decision_category> &npc_decision_category::get_all()
{
    return category_factory.get_all();
}

const std::vector<npc_decision_category_id> &npc_decision_category::order()
{
    return category_order;
}

std::vector<npc_decision_category_id> resolve_category_order(
    const std::vector<category_placement> &placements, std::vector<std::string> &errors )
{
    std::vector<npc_decision_category_id> placed;
    std::vector<const category_placement *> pending;
    for( const category_placement &p : placements ) {
        if( p.before || p.after ) {
            pending.push_back( &p );
        } else {
            placed.push_back( p.id );
        }
    }
    // each pass places what names a placed category; the rest waits for its neighbour
    bool progress = true;
    while( !pending.empty() && progress ) {
        progress = false;
        for( auto it = pending.begin(); it != pending.end(); ) {
            const category_placement &p = **it;
            const npc_decision_category_id &anchor = p.before ? *p.before : *p.after;
            auto at = std::find( placed.begin(), placed.end(), anchor );
            if( at == placed.end() ) {
                ++it;
                continue;
            }
            if( p.after ) {
                ++at;
                // after the ones placed after the same neighbour, in load order
                while( at != placed.end() ) {
                    const auto earlier = std::find_if( placements.begin(), placements.end(),
                    [&at]( const category_placement & q ) {
                        return q.id == *at;
                    } );
                    if( earlier == placements.end() || earlier->after != p.after ) {
                        break;
                    }
                    ++at;
                }
            }
            placed.insert( at, p.id );
            it = pending.erase( it );
            progress = true;
        }
    }
    std::set<npc_decision_category_id> ids;
    for( const category_placement &p : placements ) {
        ids.insert( p.id );
    }
    for( const category_placement *p : pending ) {
        const npc_decision_category_id &anchor = p->before ? *p->before : *p->after;
        const char *where = p->before ? "before" : "after";
        if( anchor == p->id ) {
            errors.push_back( string_format( "npc_decision_category %s can't go %s itself", p->id.str(),
                                             where ) );
        } else if( ids.count( anchor ) == 0 ) {
            errors.push_back( string_format( "npc_decision_category %s goes %s %s, which doesn't exist",
                                             p->id.str(), where, anchor.str() ) );
        } else {
            errors.push_back( string_format(
                                  "npc_decision_category %s goes %s %s, which is placed in a loop with it",
                                  p->id.str(), where, anchor.str() ) );
        }
    }
    return placed;
}

void npc_decision_category::finalize_all()
{
    category_factory.finalize();
    std::vector<category_placement> placements;
    for( const npc_decision_category &cat : category_factory.get_all() ) {
        placements.push_back( { cat.id, cat.before, cat.after } );
    }
    // the errors are reported by check_consistency()
    std::vector<std::string> errors;
    category_order = resolve_category_order( placements, errors );
    int rank = 0;
    for( const npc_decision_category_id &id : category_order ) {
        const_cast<npc_decision_category &>( id.obj() ).rank = rank++;
    }
    // one left out of the order goes last
    for( const npc_decision_category &cat : category_factory.get_all() ) {
        if( std::find( category_order.begin(), category_order.end(), cat.id ) == category_order.end() ) {
            const_cast<npc_decision_category &>( cat ).rank = rank++;
        }
    }
    goal_categories.clear();
    for( const npc_decision_category &cat : category_factory.get_all() ) {
        for( const std::string &goal : cat.goals ) {
            goal_categories.emplace( goal, cat.id );
        }
    }
    // a JSON goal's own category comes first; a disagreeing list is a load error
    for( const auto &[id, goal] : npc_json_goals::registry().all() ) {
        goal_categories[id] = goal.category;
    }
    // a built-in goal no list names may still have one from a node
    for( const npc_goal_claim &claim : npc_json_goals::registry().claims() ) {
        if( is_builtin_npc_goal( claim.goal ) && claim.category.is_valid() ) {
            goal_categories.emplace( claim.goal, claim.category );
        }
    }
    npc_json_goals::refresh_ranks();
}

std::optional<npc_decision_category_id> npc_decision_category::set_goal_category_for_test(
    const std::string &goal, const std::optional<npc_decision_category_id> &cat )
{
    std::optional<npc_decision_category_id> was;
    const auto it = goal_categories.find( goal );
    if( it != goal_categories.end() ) {
        was = it->second;
        goal_categories.erase( it );
    }
    if( cat ) {
        goal_categories.emplace( goal, *cat );
    }
    return was;
}

std::vector<npc_goal_claim> category_goal_claims()
{
    std::vector<npc_goal_claim> claims;
    for( const npc_decision_category &cat : category_factory.get_all() ) {
        for( const std::string &goal : cat.goals ) {
            claims.push_back( { goal, cat.id, "npc_decision_category " + cat.id.str() } );
        }
    }
    return claims;
}

std::vector<std::string> check_goal_claims( const std::vector<npc_goal_claim> &list_claims,
        const std::vector<npc_goal_claim> &node_claims )
{
    std::vector<npc_goal_claim> claims = list_claims;
    claims.insert( claims.end(), node_claims.begin(), node_claims.end() );
    std::vector<std::string> errors;
    std::map<std::string, const npc_goal_claim *> first;
    for( const npc_goal_claim &claim : claims ) {
        if( !claim.category.is_valid() ) {
            errors.push_back( string_format( "%s puts goal %s in npc_decision_category %s, which doesn't exist",
                                             claim.source, claim.goal, claim.category.str() ) );
            continue;
        }
        const auto [it, added] = first.emplace( claim.goal, &claim );
        if( !added && it->second->category != claim.category ) {
            errors.push_back( string_format( "goal %s is given two categories: %s by %s and %s by %s",
                                             claim.goal, it->second->category.str(), it->second->source,
                                             claim.category.str(), claim.source ) );
        }
    }
    // the C++ behind a built-in goal still runs it, but nothing would rank it
    for( const std::string &goal : builtin_npc_goals() ) {
        if( first.count( goal ) == 0 ) {
            errors.push_back( string_format( "built-in goal %s has no npc_decision_category; list it in "
                                             "one's \"goals\"", goal ) );
        }
    }
    return errors;
}

void npc_decision_category::check_consistency()
{
    std::vector<category_placement> placements;
    for( const npc_decision_category &cat : category_factory.get_all() ) {
        placements.push_back( { cat.id, cat.before, cat.after } );
    }
    std::vector<std::string> errors;
    resolve_category_order( placements, errors );
    for( const decision_category *builtin : {
             &decision_category::combat, &decision_category::investigate, &decision_category::needs,
             &decision_category::follow, &decision_category::order, &decision_category::duty,
             &decision_category::camp_work, &decision_category::camp_travel,
             &decision_category::free_time, &decision_category::idle, &decision_category::unmodeled
         } ) {
        if( !builtin->id().is_valid() ) {
            errors.push_back( string_format( "npc_decision_category %s is missing; the game's code acts on it",
                                             builtin->id().str() ) );
        }
    }
    for( const npc_decision_category &cat : category_factory.get_all() ) {
        for( const std::string &goal : cat.goals ) {
            if( !is_builtin_npc_goal( goal ) && npc_json_goals::find( goal ) == nullptr ) {
                errors.push_back( string_format( "npc_decision_category %s lists goal %s, which doesn't exist",
                                                 cat.id.str(), goal ) );
            }
        }
    }
    const std::vector<std::string> claim_errors = npc_json_goals::registry().check();
    errors.insert( errors.end(), claim_errors.begin(), claim_errors.end() );
    for( const std::string &error : errors ) {
        debugmsg( "%s", error );
    }
}

const decision_category decision_category::combat( npc_decision_category_combat );
const decision_category decision_category::investigate( npc_decision_category_investigate );
const decision_category decision_category::needs( npc_decision_category_needs );
const decision_category decision_category::follow( npc_decision_category_follow );
const decision_category decision_category::order( npc_decision_category_order );
const decision_category decision_category::duty( npc_decision_category_duty );
const decision_category decision_category::camp_work( npc_decision_category_camp_work );
const decision_category decision_category::camp_travel( npc_decision_category_camp_travel );
const decision_category decision_category::free_time( npc_decision_category_free_time );
const decision_category decision_category::idle( npc_decision_category_idle );
const decision_category decision_category::unmodeled( npc_decision_category_unmodeled );

decision_category::decision_category() : id_( npc_decision_category_unmodeled ) {}

decision_category::decision_category( const npc_decision_category_id &id ) : id_( id ) {}

int decision_category::rank() const
{
    if( id_.is_valid() ) {
        return id_.obj().rank;
    }
    if( unmodeled.id_.is_valid() ) {
        return unmodeled.id_.obj().rank;
    }
    return std::numeric_limits<int>::max();
}

float decision_category::hysteresis() const
{
    return id_.is_valid() ? id_.obj().hysteresis : 0.1f;
}

const std::vector<std::string> &builtin_npc_goals()
{
    static const std::vector<std::string> goals = {
        "fight", "flee", "investigate_sound", "drink_water", "eat_food", "go_to_sleep", "start_fire",
        "regulate_temperature", "follow_player", "follow_embarked", "goto_ordered_position",
        "return_to_guard_pos", "hold_position", "camp_work", "return_to_camp", "free_time", "idle"
    };
    return goals;
}

bool is_builtin_npc_goal( std::string_view goal )
{
    const std::vector<std::string> &goals = builtin_npc_goals();
    return std::find( goals.begin(), goals.end(), goal ) != goals.end();
}

bool is_builtin_need_goal( std::string_view goal )
{
    return goal == "drink_water" || goal == "eat_food" || goal == "go_to_sleep" ||
           goal == "start_fire" || goal == "regulate_temperature";
}

const char *category_name( const decision_category &cat )
{
    return cat.id().c_str();
}

decision_category bt_goal_to_category( const std::string &goal )
{
    const auto it = goal_categories.find( goal );
    if( it == goal_categories.end() ) {
        return decision_category::unmodeled;
    }
    return decision_category( it->second );
}

const char *classify_comparison( const decision_category &bt, const decision_category &cascade )
{
    if( bt == decision_category::unmodeled || cascade == decision_category::unmodeled ) {
        return "unmodeled";
    }
    return bt == cascade ? "converged" : "DIVERGED";
}
