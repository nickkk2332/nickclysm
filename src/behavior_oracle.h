#pragma once
#ifndef CATA_SRC_BEHAVIOR_ORACLE_H
#define CATA_SRC_BEHAVIOR_ORACLE_H

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

class Creature;

namespace behavior
{
enum class status_t : char;

/**
 * An oracle is a class in charge of introspecting into a game entity in a particular way.
 * The base class has trivial functionality, but provides a mechanism for aggregating oracle
 * instances regardless of type.
 * Specialized oracles, such as a character_oracle_t, provide some number of predicates to retrieve
 * information about the referenced entity.
 */
class oracle_t
{
    public:
        virtual ~oracle_t() = default;
        // The creature this oracle reads. Dialogue conditions and math scores in the tree see it
        // as u. nullptr when there is none: such nodes then fail, and score 0.
        virtual const Creature *creature() const {
            return nullptr;
        }
        // Tells subjects apart from one turn to the next, so that "evaluate_every" keeps its
        // results per subject. Without a key, every node is evaluated every time.
        virtual std::optional<int> cache_key() const {
            return std::nullopt;
        }
};

status_t return_running( const oracle_t *, std::string_view );

extern std::unordered_map<std::string, std::function<status_t( const oracle_t *, std::string_view )>>
        predicate_map;

extern std::unordered_map<std::string, std::function<float( const oracle_t *, std::string_view )>>
        score_predicate_map;

// The predicates and named scores above that read their "argument". The others are always given
// "", and loading a node that gives them an argument is an error, so none is silently ignored.
extern std::unordered_set<std::string> predicates_with_argument;
extern std::unordered_set<std::string> scores_with_argument;

} // namespace behavior
#endif // CATA_SRC_BEHAVIOR_ORACLE_H
