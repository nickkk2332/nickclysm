#include "behavior.h"

#include <algorithm>
#include <cstddef>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "behavior_oracle.h"
#include "behavior_strategy.h"
#include "cata_assert.h"
#include "debug.h"
#include "flexbuffer_json.h"
#include "generic_factory.h"
#include "string_formatter.h"

using namespace behavior;

void node_t::set_strategy( const strategy_t *new_strategy )
{
    strategy = new_strategy;
}
void node_t::add_predicate( const
                            std::function<status_t ( const oracle_t *, const std::string & )> &
                            new_predicate, const std::string &argument, const bool &invert_result )
{
    conditions.push_back( { "", new_predicate, argument, invert_result } );
}
void node_t::set_goal( const std::string &new_goal )
{
    _goal = new_goal;
}
void node_t::add_child( const node_t *new_child )
{
    children.push_back( new_child );
}
void node_t::set_score_function( const score_type &func, const std::string &argument )
{
    score_function_.emplace( func, argument );
}

const std::vector<const node_t *> &node_t::get_children() const
{
    return children;
}

status_t node_t::process_predicates( const oracle_t *subject ) const
{
    status_t result = status_t::running;

    for( const condition_t &condition : conditions ) {
        result = condition.func( subject, condition.argument );

        if( condition.invert ) {
            if( result == status_t::running ) {
                result = status_t::failure;
            } else if( result == status_t::failure ) {
                result = status_t::running;
            }
        }

        if( result != status_t::running ) {
            break;
        }
    }

    return result;
}

behavior_return node_t::tick( const oracle_t *subject ) const
{
    if( children.empty() ) {
        status_t result = process_predicates( subject );
        float score = 0.0f;
        if( result == status_t::running && score_function_ ) {
            score = score_function_->first( subject, score_function_->second );
        }
        return { result, this, score };
    } else {
        cata_assert( strategy != nullptr );
        status_t result = process_predicates( subject );
        if( result == status_t::running ) {
            behavior_return child_result = strategy->evaluate( subject, children );
            if( child_result.result == status_t::running && score_function_ ) {
                child_result.score = score_function_->first( subject,
                                     score_function_->second );
            }
            return child_result;
        } else {
            return { result, nullptr, 0.0f };
        }
    }
}

std::string node_t::goal() const
{
    return _goal;
}

std::pair<std::string, float> tree::tick_full( const oracle_t *subject )
{
    behavior_return result = root->tick( subject );
    active_node = result.result == status_t::running ? result.selection : nullptr;
    active_score = result.score;
    return { goal(), active_score };
}

std::string tree::tick( const oracle_t *subject )
{
    auto [g, s] = tick_full( subject );
    return g;
}

std::string tree::goal() const
{
    return active_node == nullptr ? "idle" : active_node->goal();
}

float tree::last_score() const
{
    return active_score;
}

void tree::add( const node_t *new_node )
{
    root = new_node;
}

// Now for the generic_factory definition

namespace
{
generic_factory<behavior::node_t> behavior_factory( "behavior" );
// What link_children() found at finalization, reported by check_consistency().
std::vector<std::string> link_problems;

} // namespace

/** @relates string_id */
template<>
bool string_id<node_t>::is_valid() const
{
    return behavior_factory.is_valid( *this );
}

template<>
const node_t &string_id<node_t>::obj() const
{
    return behavior_factory.obj( *this );
}

void behavior::load_behavior( const JsonObject &jo, const std::string &src )
{
    behavior_factory.load( jo, src );
}

void node_t::load( const JsonObject &jo, std::string_view )
{
    load_children( jo );
    if( jo.has_string( "strategy" ) ) {
        std::unordered_map<std::string, const strategy_t *>::iterator new_strategy =
            behavior::strategy_map.find( jo.get_string( "strategy" ) );
        if( new_strategy != strategy_map.end() ) {
            strategy = new_strategy->second;
        } else {
            debugmsg( "While loading %s, failed to find strategy %s.",
                      id.str(), jo.get_string( "strategy" ) );
            jo.throw_error( "Invalid strategy in behavior." );
        }
    }
    load_conditions( jo );
    optional( jo, was_loaded, "goal", _goal );
    load_score( jo );
}

// "children" replaces the list. With copy-from, "delete": { "children": [ ids ] } removes
// inherited children, and "extend": { "children": [ ... ] } adds children, each an id (added at
// the end) or { "id": ..., "before": anchor } or { "id": ..., "after": anchor }. Anchors name a
// child of the inherited list (deleted ones too, so a child can be replaced in place) or one
// inserted earlier in the same "extend".
void node_t::load_children( const JsonObject &jo )
{
    if( jo.has_array( "children" ) ) {
        child_ids.clear();
        for( const std::string &child : jo.get_string_array( "children" ) ) {
            child_ids.emplace_back( child );
        }
    }
    if( !was_loaded ) {
        warn_disabled_feature( jo, "extend", "children", "no copy-from" );
        warn_disabled_feature( jo, "delete", "children", "no copy-from" );
        return;
    }
    JsonObject extend;
    JsonObject remove;
    if( jo.has_object( "extend" ) ) {
        extend = jo.get_object( "extend" );
        extend.allow_omitted_members();
    }
    if( jo.has_object( "delete" ) ) {
        remove = jo.get_object( "delete" );
        remove.allow_omitted_members();
    }
    const bool extending = jo.has_object( "extend" ) && extend.has_member( "children" );
    const bool deleting = jo.has_object( "delete" ) && remove.has_member( "children" );
    if( !extending && !deleting ) {
        return;
    }
    struct entry {
        string_id<node_t> child;
        bool deleted;
    };
    std::vector<entry> entries;
    entries.reserve( child_ids.size() );
    for( const string_id<node_t> &child : child_ids ) {
        entries.push_back( { child, false } );
    }
    const auto find = [&entries]( const string_id<node_t> &child, bool include_deleted ) {
        return std::find_if( entries.begin(), entries.end(), [&]( const entry & e ) {
            return e.child == child && ( include_deleted || !e.deleted );
        } );
    };
    if( deleting ) {
        for( const std::string &child : remove.get_as_string_array( "children" ) ) {
            const auto it = find( string_id<node_t>( child ), false );
            if( it == entries.end() ) {
                remove.throw_error_at( "children", string_format( "Behavior \"%s\" can't delete "
                                       "child \"%s\": it has no such child.", id.str(), child ) );
            }
            it->deleted = true;
        }
    }
    if( extending ) {
        for( JsonValue jv : extend.get_array( "children" ) ) {
            std::string child;
            std::optional<std::pair<std::string, bool>> anchor; // id, after
            if( jv.test_string() ) {
                child = jv.get_string();
            } else {
                JsonObject insert = jv.get_object();
                child = insert.get_string( "id" );
                if( insert.has_member( "before" ) && insert.has_member( "after" ) ) {
                    insert.throw_error( string_format( "Behavior \"%s\": insert child \"%s\" either "
                                                       "\"before\" or \"after\" another, not both.", id.str(), child ) );
                }
                if( insert.has_member( "before" ) ) {
                    anchor.emplace( insert.get_string( "before" ), false );
                } else if( insert.has_member( "after" ) ) {
                    anchor.emplace( insert.get_string( "after" ), true );
                }
            }
            if( find( string_id<node_t>( child ), false ) != entries.end() ) {
                jv.throw_error( string_format( "Behavior \"%s\" already has child \"%s\".  To move "
                                               "it, delete it as well.", id.str(), child ) );
            }
            auto at = entries.end();
            if( anchor ) {
                at = find( string_id<node_t>( anchor->first ), true );
                if( at == entries.end() ) {
                    jv.throw_error( string_format( "Behavior \"%s\" can't insert \"%s\" %s \"%s\": it "
                                                   "has no such child.", id.str(), child, anchor->second ? "after" : "before",
                                                   anchor->first ) );
                }
                if( anchor->second ) {
                    ++at;
                }
            }
            entries.insert( at, { string_id<node_t>( child ), false } );
        }
    }
    child_ids.clear();
    for( const entry &e : entries ) {
        if( !e.deleted ) {
            child_ids.push_back( e.child );
        }
    }
}

// "conditions" replaces the list. With copy-from, "delete": { "conditions": [ ... ] } removes
// every condition using a predicate, given by name or as { "predicate": ..., "argument": ... },
// and "extend": { "conditions": [ ... ] } appends conditions.
void node_t::load_conditions( const JsonObject &jo )
{
    const auto read_entry = [this]( const JsonObject & entry ) {
        condition_t condition;
        condition.predicate = entry.get_string( "predicate" );
        condition.invert = entry.get_bool( "invert_result", false );
        const auto predicate = predicate_map.find( condition.predicate );
        if( predicate == predicate_map.end() ) {
            entry.throw_error_at( "predicate", string_format( "Behavior \"%s\" uses predicate \"%s\", "
                                  "which is not a built-in predicate.", id.str(), condition.predicate ) );
        }
        condition.func = predicate->second;
        condition.argument = entry.get_string( "argument", "" );
        return condition;
    };

    if( jo.has_array( "conditions" ) ) {
        conditions.clear();
        for( const JsonObject entry : jo.get_array( "conditions" ) ) {
            conditions.push_back( read_entry( entry ) );
        }
    }
    if( !was_loaded ) {
        warn_disabled_feature( jo, "extend", "conditions", "no copy-from" );
        warn_disabled_feature( jo, "delete", "conditions", "no copy-from" );
        return;
    }
    if( jo.has_object( "delete" ) ) {
        JsonObject remove = jo.get_object( "delete" );
        remove.allow_omitted_members();
        for( JsonValue jv : remove.get_array( "conditions" ) ) {
            std::string predicate;
            std::optional<std::string> argument;
            if( jv.test_string() ) {
                predicate = jv.get_string();
            } else {
                JsonObject match = jv.get_object();
                predicate = match.get_string( "predicate" );
                if( match.has_member( "argument" ) ) {
                    argument = match.get_string( "argument" );
                }
            }
            const size_t before = conditions.size();
            conditions.erase( std::remove_if( conditions.begin(), conditions.end(),
            [&]( const condition_t &c ) {
                return c.predicate == predicate && ( !argument || c.argument == *argument );
            } ), conditions.end() );
            if( conditions.size() == before ) {
                jv.throw_error( string_format( "Behavior \"%s\" can't delete condition \"%s\": it has "
                                               "no such condition.", id.str(), predicate ) );
            }
        }
    }
    if( jo.has_object( "extend" ) ) {
        JsonObject extend = jo.get_object( "extend" );
        extend.allow_omitted_members();
        for( const JsonObject entry : extend.get_array( "conditions" ) ) {
            conditions.push_back( read_entry( entry ) );
        }
    }
}

// "score" is the name of a built-in score, with "score_argument".
void node_t::load_score( const JsonObject &jo )
{
    if( jo.has_string( "score" ) ) {
        const std::string score_id = jo.get_string( "score" );
        const std::string score_arg = jo.get_string( "score_argument", "" );
        auto new_score = score_predicate_map.find( score_id );
        if( new_score != score_predicate_map.end() ) {
            score_function_.emplace( new_score->second, score_arg );
        } else {
            debugmsg( "While loading %s, failed to find score predicate %s.",
                      id.str(), score_id );
            jo.throw_error( "Invalid score predicate in behavior." );
        }
    }
}

void node_t::check() const
{
    // Invariants
    if( child_ids.empty() ) {
        if( _goal.empty() ) {
            debugmsg( "Behavior %s must have either children or a goal.",
                      id.str() );
        }
    } else {
        if( strategy == nullptr ) {
            debugmsg( "Behavior %s has children but no strategy.", id.str() );
        }
        if( !_goal.empty() ) {
            debugmsg( "Behavior %s has both children and a goal.", id.str() );
        }
    }
}

std::vector<std::string> behavior::link_children( std::vector<node_t> &nodes )
{
    std::vector<std::string> problems;
    std::unordered_map<string_id<node_t>, size_t> index;
    for( size_t i = 0; i < nodes.size(); ++i ) {
        index.emplace( nodes[i].id, i );
        nodes[i].children.clear();
    }
    enum class mark : char { unvisited, visiting, done };
    std::vector<mark> marks( nodes.size(), mark::unvisited );
    // whether some node links to it
    std::vector<bool> linked( nodes.size(), false );
    // the nodes being visited, from the first one down
    std::vector<size_t> path;
    const std::function<void( size_t )> visit = [&]( size_t i ) {
        marks[i] = mark::visiting;
        path.push_back( i );
        node_t &parent = nodes[i];
        std::unordered_set<string_id<node_t>> seen;
        for( const string_id<node_t> &child : parent.child_ids ) {
            if( !seen.insert( child ).second ) {
                problems.push_back( string_format( "Behavior \"%s\" lists child \"%s\" twice.",
                                                   parent.id.str(), child.str() ) );
                continue;
            }
            const auto found = index.find( child );
            if( found == index.end() ) {
                problems.push_back( string_format( "Behavior \"%s\" lists child \"%s\", which is not a "
                                                   "behavior.", parent.id.str(), child.str() ) );
                continue;
            }
            const size_t c = found->second;
            if( marks[c] == mark::visiting ) {
                std::string cycle;
                for( auto it = std::find( path.begin(), path.end(), c ); it != path.end(); ++it ) {
                    cycle += nodes[*it].id.str() + " -> ";
                }
                cycle += child.str();
                problems.push_back( string_format( "Behavior \"%s\" lists child \"%s\", which closes a "
                                                   "cycle: %s.  The link is ignored.", parent.id.str(), child.str(), cycle ) );
                continue;
            }
            if( marks[c] == mark::unvisited ) {
                visit( c );
            }
            linked[c] = true;
            parent.children.push_back( &nodes[c] );
        }
        path.pop_back();
        marks[i] = mark::done;
    };
    for( size_t i = 0; i < nodes.size(); ++i ) {
        if( marks[i] == mark::unvisited ) {
            visit( i );
        }
    }

    // The links now form no cycle, so this ends.
    std::vector<std::optional<bool>> reaches_leaf( nodes.size() );
    const std::function<bool( size_t )> reaches = [&]( size_t i ) {
        if( !reaches_leaf[i] ) {
            const node_t &n = nodes[i];
            bool found = n.children.empty() && !n._goal.empty();
            for( const node_t *child : n.children ) {
                if( reaches( static_cast<size_t>( child - nodes.data() ) ) ) {
                    found = true;
                    break;
                }
            }
            reaches_leaf[i] = found;
        }
        return *reaches_leaf[i];
    };
    for( size_t i = 0; i < nodes.size(); ++i ) {
        if( !linked[i] && !reaches( i ) ) {
            problems.push_back( string_format( "Behavior tree \"%s\" reaches no leaf with a goal, so "
                                               "it always decides \"idle\".", nodes[i].id.str() ) );
        }
    }
    return problems;
}

void behavior::reset()
{
    link_problems.clear();
    behavior_factory.reset();
}

void behavior::finalize()
{
    behavior_factory.finalize();
    link_problems = link_children( behavior_factory.get_all_mod() );
}

void behavior::check_consistency()
{
    for( const std::string &problem : link_problems ) {
        debugmsg( "%s", problem );
    }
    for( const node_t &checked_node : behavior_factory.get_all() ) {
        checked_node.check();
    }
}
