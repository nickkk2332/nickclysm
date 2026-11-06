#pragma once
#ifndef CATA_SRC_EXECUTION_FATAL_H
#define CATA_SRC_EXECUTION_FATAL_H

#include <string>

class Creature;
class map;

/** Narrow adapters for reserved fatal injuries. Raw fatal health remains unchanged. */
namespace execution_fatal
{
bool pending( const Creature &who );
bool finalized( const Creature &who );
bool before_death( Creature &who, map *here, Creature *source );
void before_damage( Creature &who, bool harmful );
bool before_motion( Creature &who );
bool before_gravity( Creature &who );
void before_removal( const Creature &who, const std::string &reason );

/** NPC death-prevention callbacks run before its native finalized flag is set. */
class death_guard
{
    public:
        explicit death_guard( const Creature &who );
        ~death_guard();
        bool entered() const;
    private:
        const Creature &who;
        bool first;
};
} // namespace execution_fatal
#endif // CATA_SRC_EXECUTION_FATAL_H
