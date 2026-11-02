#pragma once
#ifndef CATA_SRC_GRAPPLE_H
#define CATA_SRC_GRAPPLE_H

#include <optional>
#include <string>
#include <vector>

#include "coords_fwd.h"
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
 * Returns false (and changes nothing) if the victim can't take the effect. Then the grabber's
 * ongrab martial-art buffs and EOCs fire, and a character victim reacts (Character::on_grabbed):
 * its ongrabbed hooks fire and a grab counter may end the hold at once. The result is still true.
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

/**
 * Break the hold that the GRAB effect @p id on @p bp of @p victim stands for, on both sides: a
 * character grabber's hold ends, a monster grabber lets go of that limb. Prints nothing.
 */
void break_hold( Creature &victim, const efftype_id &id, const bodypart_id &bp );

/** End every grab the monster @p mon holds on others, on both sides. Prints nothing. */
void release_grabs_by( monster &mon );

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

// Struggling free

/**
 * The stamina @p grabber spends when @p target tries to break a hold of strength
 * @p hold_strength. @p escape_skill is the target's skill at escaping.
 */
int escape_stamina_cost( const Character &grabber, const Creature &target, int hold_strength,
                         int escape_skill );

/** Turns @p target must stay in @p grabber's hold before it may try to break free. */
int min_hold_turns( const Character &grabber, const Creature &target );

/** Whether the hold @p eff by @p grabber on @p target is still inside its minimum hold time. */
bool within_min_hold( const Character &grabber, const Creature &target, const effect &eff );

// Exertion

/**
 * The activity level of all grappling work by @p who: grabbing, holding on against a struggle,
 * throwing. The same as melee: EXTRA_EXERCISE, or EXPLOSIVE_EXERCISE with the
 * EXPLOSIVE_COMBAT_EXERTION world option on (Character::melee_exertion_level).
 */
float exertion_level( const Character &who );

// Dragging

/**
 * Chance, 0 to 1, that @p grabber drags @p victim along one step (grapple_drag_odds). Weight
 * against grab strength and size; an ally that walks along, or a pet, is easier.
 */
double drag_chance( const Character &grabber, const Creature &victim );

/**
 * Why @p grabber, holding @p victim, can't step from @p from to @p to and drag the victim into
 * @p from; nullopt if it can. Checked before anything is spent, so a refusal changes nothing.
 */
std::optional<std::string> drag_refusal( const Character &grabber, const Creature &victim,
        const tripoint_bub_ms &from, const tripoint_bub_ms &to );

/**
 * Move @p victim to @p dest the way it moves itself: a monster through monster::move_to (forced,
 * as it goes where it is pulled, and keeping its moves), an NPC through the vehicle boarding,
 * trap and field steps of its own move. Gravity applies. Returns whether it got there.
 */
bool drag_to( Creature &victim, const tripoint_abs_ms &dest );

/**
 * After @p grabber stepped off @p from, drag the creature it held at @p victim_at into @p from.
 * If the hold ended during the step nothing happens; if the creature can't follow, the hold ends
 * with a message instead of lingering until the next turn.
 */
void drag_after_step( Character &grabber, const tripoint_abs_ms &victim_at,
                      const tripoint_abs_ms &from );

// Throwing a held creature

/**
 * The full force of a throw of @p victim by @p thrower (grapple_throw_force), before it is
 * scaled to the distance chosen. A creature thrown with force F flies F / 10 tiles.
 */
float throw_force( const Character &thrower, const Creature &victim );

/** How many tiles a throw of @p force carries a creature: force / 10, as fling_creature. */
int throw_range( float force );

/**
 * The force of a throw @p distance tiles far, with @p full_force at full range: scaled by
 * distance / range, so a short throw is a gentle shove and lands where it was aimed.
 */
float throw_force_at( float full_force, int distance );

/** Stamina @p thrower spends to throw @p victim with @p force (grapple_throw_stamina_cost). */
int throw_stamina_cost( const Character &thrower, const Creature &victim, float force );

/**
 * Stamina @p thrower spends to throw @p victim @p distance tiles (clamped to the throw's range):
 * the cost of the force for that distance, less the share a bio_railgun takes against machines.
 * What a throw actually costs.
 */
int throw_stamina_cost_at( const Character &thrower, const Creature &victim, int distance );

/** Moves a throw costs @p thrower. */
int throw_move_cost( const Character &thrower );

/** How hard a throw of @p force is, from 0 (a shove) up (grapple_throw_verb). */
int throw_verb_tier( const Character &thrower, const Creature &victim, float force );

/**
 * The word for a throw of @p force: shove, toss, throw, hurl or launch, from the snippet
 * category grapple_throw_verb_<tier>. A @p seed picks the same word every time.
 */
std::string throw_verb( const Character &thrower, const Creature &victim, float force,
                        std::optional<unsigned int> seed = std::nullopt );

/** The tiles a creature thrown @p distance tiles from @p from toward @p target flies over. */
std::vector<tripoint_bub_ms> throw_path( const tripoint_bub_ms &from,
        const tripoint_bub_ms &target, int distance );

/**
 * Why @p thrower can't throw @p victim, the creature it holds, at all; nullopt if it can.
 * Checked before targeting, so a refusal changes nothing. The throw must be able to carry past
 * the next tile, and @p thrower needs the stamina for the shortest throw, one tile; whether it
 * can afford a longer one is up to throw_target_refusal.
 */
std::optional<std::string> throw_refusal( const Character &thrower, const Creature &victim );

/**
 * Why @p victim can't be thrown toward @p target, whatever it costs; nullopt if it can: it goes
 * across (not up or down), and never through @p thrower.
 */
std::optional<std::string> throw_direction_refusal( const Character &thrower,
        const Creature &victim, const tripoint_bub_ms &target );

/**
 * Why @p victim can't be thrown toward @p target; nullopt if it can. Besides where it may go
 * (throw_direction_refusal), @p thrower must have the stamina for a throw that far
 * (throw_stamina_cost_at).
 */
std::optional<std::string> throw_target_refusal( const Character &thrower,
        const Creature &victim, const tripoint_bub_ms &target );

/** Whether a throw of @p force makes the held NPC @p victim angry (so the avatar is asked). */
bool throw_angers( const Character &thrower, const Creature &victim, float force );

/**
 * If @p victim holds @p thrower as well, @p thrower must break free before throwing it.
 * Returns whether @p thrower is free; a failed attempt costs it moves.
 */
bool break_free_to_throw( Character &thrower, const Creature &victim );

/**
 * Throw @p victim, which @p thrower holds, toward @p target (bubble coordinates; clamped to the
 * throw's range): the hold ends, @p thrower pays moves and stamina, the victim reacts, and it is
 * flung from its tile toward @p target with the force for that distance. No prompts: the caller
 * asks first. Returns whether the throw was made.
 */
bool throw_held_creature( Character &thrower, Creature &victim, const tripoint_bub_ms &target );

} // namespace grapple

#endif // CATA_SRC_GRAPPLE_H
