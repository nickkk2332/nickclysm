#pragma once
#ifndef CATA_SRC_GRAPPLE_H
#define CATA_SRC_GRAPPLE_H

#include <optional>
#include <string>

#include "type_id.h"

class Character;
class Creature;
class effect;
class monster;

/**
 * Grabs started by characters (grappling), ported from Cataclysm: The Last Generation.
 *
 * Ownership model: the victim's GRAB-flagged effect carries effect_source( grabber ), and the
 * grabber carries a GRAB_FILTER effect. Each side finds the other by adjacency plus that
 * source. No pointer is stored and nothing new is saved: a hold survives save and load through
 * the effects alone. A hold whose other side is gone, dead or out of reach counts as no hold
 * and is cleaned up by whichever side notices first.
 *
 * Tuning formulas are jmath functions in data/json/grappling.json, evaluated with u = grabber
 * and n = target. Thresholds are EXTERNAL_OPTIONs in data/core/external_options.json.
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

// Holding a creature

/** Close enough to hold: one tile apart horizontally and at most one z-level apart. */
bool in_reach( const Creature &grabber, const Creature &victim );

/** The GRAB effect on @p victim that @p grabber holds, or nullptr. */
const effect *hold_effect( const Character &grabber, const Creature &victim );

/**
 * The character holding @p victim through @p eff, one of the victim's GRAB effects.
 * nullptr if that effect has no character source, or the character is gone, dead, out of reach
 * or no longer grabbing.
 */
Character *character_grabber( const Creature &victim, const effect &eff );

/**
 * Establish the hold right away, with no roll and no cost: the victim gets a GRAB effect with
 * source @p grabber on @p bp (the null part for a monster), and the grabber a GRAB_FILTER effect.
 * Returns false (and changes nothing) if the victim can't take the effect.
 */
bool start_hold( Character &grabber, Creature &victim, int strength, const bodypart_id &bp );

/**
 * End @p grabber's hold: remove the GRAB effects it holds on @p victim (if given) and every
 * GRAB_FILTER effect on @p grabber. Prints nothing.
 */
void end_hold( Character &grabber, Creature *victim );

/** End every character's hold on @p victim (it dies, teleports, is captured). Prints nothing. */
void release_holds_on( Creature &victim );

/** End every character hold that involves @p c, as grabber or as victim. Prints nothing. */
void release_all( Creature &c );

/** Stamina needed to start a grab or to keep holding one. */
int stamina_floor();

/** Per-turn upkeep for a grabber. Called from Character::process_turn. */
void process_turn( Character &grabber );

// The grab action

/** Arm stamina spent on a grab attempt. */
int arm_stamina_cost();

/** A body part of @p target that nobody holds yet, weighted by hit size; nullopt if none. */
std::optional<bodypart_id> free_grab_part( const Creature &target );

/**
 * Why @p grabber can't try to grab @p target now, or nullopt if it can. Checked before any cost
 * is paid, so a refusal changes nothing.
 */
std::optional<std::string> grab_refusal( const Character &grabber, const Creature &target );

/** Whether @p target accepts the grab: no hit roll, and no anger. */
bool grab_tolerated( const Character &grabber, const Creature &target );

enum class grab_confirmation : int {
    none,           // just do it
    attack_npc,     // the avatar is about to attack a neutral NPC
    anger_faction   // the avatar is grabbing a non-hostile monster
};
/** What the avatar must confirm before grabbing @p target. */
grab_confirmation needs_confirmation( const Character &grabber, const Creature &target );

/**
 * Pay the cost and roll the grab (a hit roll the target can dodge, unless it tolerates the
 * grab). Returns true when the hold is established. Call grab_refusal() first.
 */
bool attempt_grab( Character &grabber, Creature &target );

} // namespace grapple

#endif // CATA_SRC_GRAPPLE_H
