#pragma once
#ifndef CATA_SRC_GRAPPLE_H
#define CATA_SRC_GRAPPLE_H

#include "type_id.h"

class Character;
class Creature;
class monster;

/**
 * Grabs by and on characters, ported from Cataclysm: The Last Generation.
 *
 * Tuning formulas are jmath functions in data/json/grappling.json, evaluated with u = grabber
 * and n = target.
 */
namespace grapple
{

// Finding a monster grabber, and how strong a character grabs

/** The longest range of @p mon's grab attacks; 0 if it has none. */
int monster_grab_reach( const monster &mon );

/**
 * The monster holding @p victim through a GRAB effect on @p bp, searched within each monster's
 * grab reach. For a character victim the monster must list @p bp among its grabbed limbs. Prefers
 * a monster attacking the victim, then the nearest one. nullptr if none.
 */
monster *monster_grabber( const Creature &victim, const bodypart_id &bp );

/** The strength of a grab by @p grabber on @p target (pass the grabber itself if none). 1-100. */
int grab_strength( const Character &grabber, const Creature &target );

} // namespace grapple

#endif // CATA_SRC_GRAPPLE_H
