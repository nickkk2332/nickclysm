#include "behavior.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "behavior_oracle.h"
#include "behavior_strategy.h"
#include "calendar.h"
#include "cata_assert.h"
#include "condition.h"
#include "creature.h"
#include "debug.h"
#include "dialogue.h"
#include "dialogue_helpers.h"
#include "flexbuffer_json.h"
#include "generic_factory.h"
#include "math_parser_type.h"
#include "string_formatter.h"
#include "talker.h"

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

std::string node_t::summary() const
{
    std::vector<std::string> parts;
    for( const auto &[name, known] : strategy_map ) {
        if( known == strategy && strategy != nullptr ) {
            parts.push_back( "strategy " + name );
        }
    }
    if( !conditions.empty() ) {
        std::string list;
        for( const condition_t &condition : conditions ) {
            list += list.empty() ? "" : ", ";
            list += ( condition.invert ? "!" : "" ) + condition.predicate;
            if( !condition.argument.empty() ) {
                list += "(" + condition.argument + ")";
            }
        }
        parts.push_back( "if " + list );
    }
    if( score_function_ ) {
        parts.push_back( "score " + ( score_label_.empty() ? "code" : score_label_ ) );
    }
    if( evaluate_every_turns > 0 ) {
        parts.push_back( string_format( "every %d", evaluate_every_turns ) );
    }
    if( !_goal.empty() ) {
        parts.push_back( "goal " + _goal );
    }
    std::string out;
    for( const std::string &part : parts ) {
        out += ( out.empty() ? "" : "; " ) + part;
    }
    return out;
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

node_t::cached_evaluation *node_t::cached_for( const oracle_t *subject ) const
{
    if( evaluate_every_turns <= 0 || subject == nullptr ) {
        return nullptr;
    }
    const std::optional<int> key = subject->cache_key();
    if( !key ) {
        return nullptr;
    }
    const int now = to_turn<int>( calendar::turn );
    const auto expired = [this, now]( const cached_evaluation & c ) {
        // time can go back too, when another game is loaded
        return now < c.evaluated_at || now >= c.evaluated_at + evaluate_every_turns;
    };
    // Drop the results of subjects that are gone, so the cache doesn't grow without bound. Each
    // prune waits for the cache to double, so its cost is spread over the inserts.
    if( evaluation_cache.size() >= evaluation_cache_prune_at ) {
        for( auto it = evaluation_cache.begin(); it != evaluation_cache.end(); ) {
            it = expired( it->second ) ? evaluation_cache.erase( it ) : std::next( it );
        }
        static constexpr size_t min_prune_at = 256;
        evaluation_cache_prune_at = std::max( min_prune_at, 2 * evaluation_cache.size() );
    }
    auto [it, inserted] = evaluation_cache.try_emplace( *key );
    cached_evaluation &cached = it->second;
    if( inserted || expired( cached ) ) {
        cached.evaluated_at = now;
        cached.conditions = process_predicates( subject );
        cached.score.reset();
    }
    return &cached;
}

float node_t::evaluate_score( const oracle_t *subject, cached_evaluation *cached ) const
{
    if( cached == nullptr ) {
        return score_function_->first( subject, score_function_->second );
    }
    if( !cached->score ) {
        cached->score = score_function_->first( subject, score_function_->second );
    }
    return *cached->score;
}

behavior_return node_t::tick( const oracle_t *subject ) const
{
    if( children.empty() && !child_ids.empty() ) {
        // every child was unknown or closed a cycle (reported at load): not a leaf
        return { status_t::failure, nullptr, 0.0f };
    }
    cached_evaluation *cached = cached_for( subject );
    const status_t result = cached != nullptr ? cached->conditions : process_predicates( subject );
    if( children.empty() ) {
        float score = 0.0f;
        if( result == status_t::running && score_function_ ) {
            score = evaluate_score( subject, cached );
        }
        return { result, this, score };
    } else {
        cata_assert( strategy != nullptr );
        if( result == status_t::running ) {
            behavior_return child_result = strategy->evaluate( subject, children );
            if( child_result.result == status_t::running && score_function_ ) {
                child_result.score = evaluate_score( subject, cached );
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

// The subject as u. The beta is an empty talker, so beta conditions are false and beta math
// reads 0 (a null beta would crash them).
const_dialogue subject_dialogue( const Creature &subject )
{
    return const_dialogue( get_const_talker_for( subject ), std::make_unique<talker>() );
}

// A problem in a node's JSON that loading can skip: reported with its place in the JSON.
void report_at( const JsonValue &jv, const std::string &message )
{
    try {
        jv.throw_error( message );
    } catch( const JsonError &err ) {
        debugmsg( "%s", err.what() );
    }
}

// A "predicate": "condition" entry: running when the dialogue condition is true, else failure.
std::function<status_t( const oracle_t *, const std::string & )>
read_dialogue_condition( const JsonObject &jo )
{
    if( !jo.has_member( "condition" ) ) {
        jo.throw_error_at( "predicate", "A \"condition\" predicate needs a \"condition\"." );
    }
    std::function<bool( const_dialogue const & )> cond;
    read_condition( jo, "condition", cond, false );
    return [cond]( const oracle_t *subject, const std::string & ) {
        const Creature *who = subject == nullptr ? nullptr : subject->creature();
        if( who == nullptr ) {
            return status_t::failure;
        }
        return cond( subject_dialogue( *who ) ) ? status_t::running : status_t::failure;
    };
}

// A "score": { "math": [ ... ] } expression, evaluated with the subject as u.
node_t::score_type read_math_score( const JsonObject &jo )
{
    eoc_math math;
    math.from_json( jo, "math", math_type_t::ret );
    return [math]( const oracle_t *subject, std::string_view ) {
        const Creature *who = subject == nullptr ? nullptr : subject->creature();
        if( who == nullptr ) {
            return 0.0f;
        }
        const const_dialogue d = subject_dialogue( *who );
        return static_cast<float>( math.act( d ) );
    };
}

// A built-in score by name, with its argument checked.
node_t::score_type read_named_score( const JsonObject &jo, const std::string &name_member,
                                     const std::string &argument_member, const string_id<node_t> &id )
{
    const std::string name = jo.get_string( name_member );
    const auto score = score_predicate_map.find( name );
    if( score == score_predicate_map.end() ) {
        jo.throw_error_at( name_member, string_format( "Behavior \"%s\" names score \"%s\", "
                           "which is not a built-in score.", id.str(), name ) );
    }
    const bool takes_argument = scores_with_argument.count( name ) != 0;
    if( jo.has_member( argument_member ) && !takes_argument ) {
        jo.throw_error_at( argument_member, string_format( "Behavior \"%s\": score \"%s\" takes no "
                           "argument.", id.str(), name ) );
    }
    if( !jo.has_member( argument_member ) && takes_argument ) {
        jo.throw_error_at( name_member, string_format( "Behavior \"%s\": score \"%s\" needs an "
                           "\"%s\".", id.str(), name, argument_member ) );
    }
    return score->second;
}

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
    // read once, so that a member of "extend" or "delete" nothing reads is reported
    const JsonObject extend = jo.get_object( "extend" );
    const JsonObject remove = jo.get_object( "delete" );
    load_children( jo, extend, remove );
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
    load_conditions( jo, extend, remove );
    optional( jo, was_loaded, "goal", _goal );
    load_score( jo );
    time_duration evaluate_every = time_duration::from_turns( evaluate_every_turns );
    optional( jo, was_loaded, "evaluate_every", evaluate_every, 0_turns );
    if( evaluate_every < 0_turns ) {
        jo.throw_error_at( "evaluate_every", "\"evaluate_every\" can't be negative." );
    }
    evaluate_every_turns = to_turns<int>( evaluate_every );
}

// "children" replaces the list. With copy-from, "delete": { "children": [ ids ] } removes
// inherited children, and "extend": { "children": [ ... ] } adds children, each an id (added at
// the end) or { "id": ..., "before": anchor } or { "id": ..., "after": anchor }. Anchors name a
// child of the list (a deleted one too, so a child can be replaced in place) or one inserted
// earlier in the same "extend". Edits that don't apply, such as two mods deleting the same
// child, are reported and skipped; an insert whose anchor is missing goes at the end.
void node_t::load_children( const JsonObject &jo, const JsonObject &extend,
                            const JsonObject &remove )
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
    const bool extending = extend.has_member( "children" );
    const bool deleting = remove.has_member( "children" );
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
    const auto find = [&entries]( const string_id<node_t> &child, bool deleted ) {
        return std::find_if( entries.begin(), entries.end(), [&]( const entry & e ) {
            return e.child == child && e.deleted == deleted;
        } );
    };
    if( deleting ) {
        for( JsonValue jv : remove.get_array( "children" ) ) {
            const std::string child = jv.get_string();
            const auto it = find( string_id<node_t>( child ), false );
            if( it == entries.end() ) {
                report_at( jv, string_format( "Behavior \"%s\" can't delete child \"%s\": it has no "
                                              "such child.  Skipped.", id.str(), child ) );
                continue;
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
                report_at( jv, string_format( "Behavior \"%s\" already has child \"%s\" (to move it, "
                                              "delete it as well).  Skipped.", id.str(), child ) );
                continue;
            }
            auto at = entries.end();
            if( anchor ) {
                // the child in the list, else the place of a deleted one
                const string_id<node_t> anchor_id( anchor->first );
                at = find( anchor_id, false );
                if( at == entries.end() ) {
                    at = find( anchor_id, true );
                }
                if( at == entries.end() ) {
                    report_at( jv, string_format( "Behavior \"%s\" can't insert \"%s\" %s \"%s\": it "
                                                  "has no such child.  Added at the end.", id.str(), child,
                                                  anchor->second ? "after" : "before", anchor->first ) );
                } else if( anchor->second ) {
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
// and "extend": { "conditions": [ ... ] } appends conditions. Deleting a condition the node
// doesn't have is reported and skipped.
void node_t::load_conditions( const JsonObject &jo, const JsonObject &extend,
                              const JsonObject &remove )
{
    const auto read_entry = [this]( const JsonObject & entry ) {
        condition_t condition;
        condition.predicate = entry.get_string( "predicate" );
        condition.invert = entry.get_bool( "invert_result", false );
        if( condition.predicate == "condition" ) {
            if( entry.has_member( "argument" ) ) {
                entry.throw_error_at( "argument", string_format( "Behavior \"%s\": a \"condition\" "
                                      "predicate takes no argument; write the test in \"condition\".", id.str() ) );
            }
            condition.func = read_dialogue_condition( entry );
            return condition;
        }
        const auto predicate = predicate_map.find( condition.predicate );
        if( predicate == predicate_map.end() ) {
            entry.throw_error_at( "predicate", string_format( "Behavior \"%s\" uses predicate \"%s\", "
                                  "which is neither \"condition\" nor a built-in predicate.", id.str(), condition.predicate ) );
        }
        condition.func = predicate->second;
        const bool takes_argument = predicates_with_argument.count( condition.predicate ) != 0;
        if( entry.has_member( "argument" ) && !takes_argument ) {
            entry.throw_error_at( "argument", string_format( "Behavior \"%s\": predicate \"%s\" takes "
                                  "no argument.", id.str(), condition.predicate ) );
        }
        if( !entry.has_member( "argument" ) && takes_argument ) {
            entry.throw_error_at( "predicate", string_format( "Behavior \"%s\": predicate \"%s\" needs "
                                  "an \"argument\".", id.str(), condition.predicate ) );
        }
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
            report_at( jv, string_format( "Behavior \"%s\" can't delete condition \"%s\": it has no "
                                          "such condition.  Skipped.", id.str(), predicate ) );
        }
    }
    for( const JsonObject entry : extend.get_array( "conditions" ) ) {
        conditions.push_back( read_entry( entry ) );
    }
}

// "score" is the name of a built-in score (with "score_argument"),
// { "named": ..., "argument": ..., "mult": ... } or { "math": [ ... ] }.
void node_t::load_score( const JsonObject &jo )
{
    if( jo.has_string( "score" ) ) {
        score_type score = read_named_score( jo, "score", "score_argument", id );
        score_function_.emplace( score, jo.get_string( "score_argument", "" ) );
        score_name_ = jo.get_string( "score" );
        score_label_ = score_name_;
    } else if( jo.has_object( "score" ) ) {
        const JsonObject score = jo.get_object( "score" );
        if( score.has_member( "math" ) ) {
            if( score.has_member( "named" ) ) {
                score.throw_error( string_format( "Behavior \"%s\": a score is either \"named\" or "
                                                  "\"math\", not both.", id.str() ) );
            }
            score_function_.emplace( read_math_score( score ), "" );
            score_name_.clear();
            score_label_ = "math";
        } else {
            score_type named = read_named_score( score, "named", "argument", id );
            score_name_ = score.get_string( "named" );
            score_label_ = score_name_;
            if( score.has_member( "mult" ) ) {
                const float mult = static_cast<float>( score.get_float( "mult" ) );
                named = [named, mult]( const oracle_t *subject, std::string_view argument ) {
                    return named( subject, argument ) * mult;
                };
                score_label_ += string_format( "*%g", mult );
            }
            score_function_.emplace( named, score.get_string( "argument", "" ) );
        }
    } else if( jo.has_member( "score" ) ) {
        jo.throw_error_at( "score", string_format( "Behavior \"%s\": \"score\" is a score's name or "
                           "an object.", id.str() ) );
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
            bool found = n.children.empty() && n.child_ids.empty() && !n._goal.empty();
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
    // A node in the subtree that uses NPC predicates or scores, and one that uses monster ones.
    struct kinds {
        const node_t *character = nullptr;
        const node_t *monster = nullptr;
    };
    std::vector<std::optional<kinds>> subtree_kinds( nodes.size() );
    const std::function<kinds( size_t )> kinds_below = [&]( size_t i ) {
        if( !subtree_kinds[i] ) {
            const node_t &n = nodes[i];
            kinds found;
            const auto note = [&found, &n]( oracle_kind kind ) {
                if( kind == oracle_kind::character && found.character == nullptr ) {
                    found.character = &n;
                } else if( kind == oracle_kind::monster && found.monster == nullptr ) {
                    found.monster = &n;
                }
            };
            for( const node_t::condition_t &condition : n.conditions ) {
                note( predicate_kind( condition.predicate ) );
            }
            note( score_kind( n.score_name_ ) );
            for( const node_t *child : n.children ) {
                const kinds below = kinds_below( static_cast<size_t>( child - nodes.data() ) );
                if( found.character == nullptr ) {
                    found.character = below.character;
                }
                if( found.monster == nullptr ) {
                    found.monster = below.monster;
                }
            }
            subtree_kinds[i] = found;
        }
        return *subtree_kinds[i];
    };
    for( size_t i = 0; i < nodes.size(); ++i ) {
        if( linked[i] ) {
            continue;
        }
        if( !reaches( i ) ) {
            problems.push_back( string_format( "Behavior tree \"%s\" reaches no leaf with a goal, so "
                                               "it always decides \"idle\".", nodes[i].id.str() ) );
        }
        const kinds found = kinds_below( i );
        if( found.character != nullptr && found.monster != nullptr ) {
            problems.push_back( string_format( "Behavior tree \"%s\" uses NPC predicates or scores (in "
                                               "\"%s\") and monster ones (in \"%s\"), so one kind always fails.",
                                               nodes[i].id.str(), found.character->id.str(), found.monster->id.str() ) );
        }
    }
    return problems;
}

void behavior::clear_evaluation_cache()
{
    for( const node_t &n : behavior_factory.get_all() ) {
        n.evaluation_cache.clear();
    }
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
