#pragma once
#ifndef CATA_SRC_MAGIC_SPELL_EFFECT_HELPERS_H
#define CATA_SRC_MAGIC_SPELL_EFFECT_HELPERS_H

#include <functional>
#include <memory>
#include <set>
#include <vector>

#include "coords_fwd.h"

class Creature;
class spell;

// where the spell actually lands when cast from source at target: a blast spell's
// projectile stops on the last tile before a wall. Teleports, and casters that aren't
// on the map (trap and timed event dummies), land on the target.
tripoint_bub_ms spell_effect_epicenter( const spell &sp, const tripoint_bub_ms &source,
                                        const tripoint_bub_ms &target );

// spells do not reduce in damage the further away from the epicenter the targets are
// rather they do their full damage in the entire area of effect
std::set<tripoint_bub_ms> calculate_spell_effect_area( const spell &sp,
        const tripoint_bub_ms &target, const Creature &caster );

// calculate_spell_effect_area() for many targets of one spell and caster, as an NPC rating
// its attack spells needs it: the map and creature questions are answered once per tile,
// lines are walked without building vectors, and a cone's rays reuse their trigonometry and
// the lines to their ends. The answers are only right while the map, the creatures and the
// spell stay as they were when it was made.
class spell_area_calculator
{
    public:
        spell_area_calculator( const spell &sp, const Creature &caster );
        ~spell_area_calculator();
        spell_area_calculator( const spell_area_calculator & ) = delete;
        spell_area_calculator &operator=( const spell_area_calculator & ) = delete;

        // False when the spell's area can't be worked out once for all targets: its size or
        // range drew random numbers (which are put back), or its targets have a condition.
        // Then only calculate_spell_effect_area() gives the right areas.
        bool usable() const;
        // The spell's aoe() and range() for the caster.
        int aoe() const;
        int range() const;
        // The tiles of calculate_spell_effect_area( sp, target, caster ), in the same order.
        const std::vector<tripoint_bub_ms> &area( const tripoint_bub_ms &target );
        // map::passable(), creature_tracker::creature_at() and spell::is_valid_target(),
        // remembered per tile.
        bool passable( const tripoint_bub_ms &p );
        // Whether every tile of line_to( from, to ) is passable.
        bool passable_line( const tripoint_bub_ms &from, const tripoint_bub_ms &to );
        Creature *creature_at( const tripoint_bub_ms &p );
        bool valid_target( const tripoint_bub_ms &p );

    private:
        struct impl;
        std::unique_ptr<impl> impl_;
};

#endif // CATA_SRC_MAGIC_SPELL_EFFECT_HELPERS_H
