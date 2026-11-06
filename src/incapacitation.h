#pragma once
#ifndef CATA_SRC_INCAPACITATION_H
#define CATA_SRC_INCAPACITATION_H

#include <string>

#include "calendar.h"

class Creature;

/** A physiological state independent of execution ownership and ordinary sleep. */
namespace incapacitation
{
bool unconscious( const Creature &who );
bool supported_physiology( const Creature &who );
bool immune( const Creature &who );
/** Read-only admission check shared by the action and author-facing EOC conditions. */
std::string eligibility( const Creature &target );
std::string apply( Creature &target, const Creature *source, time_duration duration,
                   bool wake_on_damage );
/** Safe during effect iteration: expires the effect instead of erasing the map entry. */
void wake_on_damage( Creature &target, int hp_lost );
/** Suppress positive move banking; preserve existing action debt. */
void suppress_moves( Creature &who );
} // namespace incapacitation

#endif // CATA_SRC_INCAPACITATION_H
