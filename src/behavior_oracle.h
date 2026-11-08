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

// Which oracle a built-in predicate or score reads.
enum class oracle_kind : char { any, character, monster };

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
        virtual oracle_kind kind() const {
            return oracle_kind::any;
        }
        // The creature this oracle reads. Dialogue conditions and math scores in the tree see it
        // as u. nullptr when there is none: such nodes then fail, and score 0.
        virtual const Creature *creature() const {
            return nullptr;
        }
        // MONAI-J09: the creature the subject is after, npc in its dialogue conditions and math
        // scores: a monster's attack target. nullptr when there is none (and for NPCs): npc is
        // then an empty talker, as before.
        virtual const Creature *target() const {
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

// The oracle a built-in predicate or score reads; `any` for a name that isn't built in. Given
// another kind of oracle, a built-in fails (predicates) or gives 0 (scores) with a debugmsg.
oracle_kind predicate_kind( std::string_view name );
oracle_kind score_kind( std::string_view name );

} // namespace behavior
#endif // CATA_SRC_BEHAVIOR_ORACLE_H
