#include "ability_ai.h"

#include <algorithm>
#include <cstddef>
#include <memory>

#include "coordinates.h"
#include "creature.h"
#include "creature_ref.h"
#include "game.h"
#include "string_id.h"

bool ability_ai_view::eligible() const
{
    return reason.empty();
}

ability_ai_view ability_ai::query( const ability_request &request )
{
    ability_ai_view out;
    out.ability = request.ability;
    if( !request.ability.is_valid() ) {
        out.reason = "invalid_ability";
        return out;
    }
    const ability_definition &def = *request.ability;
    out.intent = def.ai_intent;
    out.magnitude = def.ai_magnitude;
    out.energy_cost = def.energy_cost;
    out.move_cost = def.move_cost;
    out.windup_min = def.windup_min;
    out.windup_max = def.windup_max;
    if( def.ai_intent.empty() ) {
        out.reason = "no_ai_intent";
        return out;
    }
    if( request.invocation != ability_invocation::action ) {
        out.reason = "ai_requires_action";
        return out;
    }
    out.reason = g->abilities().check( request );
    if( !out.reason.empty() ) {
        return out;
    }
    const Creature &caster = *request.caster.resolve();
    const Creature *target = def.target == "self" ? &caster : request.target.resolve();
    if( !target ) {
        out.reason = "ai_requires_creature_target";
        return out;
    }
    const int distance = rl_dist( caster.pos_abs(), target->pos_abs() );
    if( distance < def.ai_min_range ) {
        out.reason = "too_close";
        return out;
    }
    const Creature::Attitude relation = caster.attitude_to( *target );
    if( ( def.ai_relation == "hostile" && ( target == &caster ||
                                            relation != Creature::Attitude::HOSTILE ) ) ||
        ( def.ai_relation == "friendly" && target != &caster &&
          relation != Creature::Attitude::FRIENDLY ) ||
        ( def.ai_relation == "self" && target != &caster ) ) {
        out.reason = "target_relation";
        return out;
    }
    if( def.ai_intent == "heal" && target->get_hp() >= target->get_hp_max() ) {
        out.reason = "target_healthy";
        return out;
    }
    if( def.ai_safe_radius > 0 ) {
        for( const Creature &other : g->all_creatures() ) {
            if( &other != target && !other.is_dead_state() &&
                rl_dist( other.pos_abs(), target->pos_abs() ) <= def.ai_safe_radius &&
                ( &other == &caster || caster.attitude_to( other ) != Creature::Attitude::HOSTILE ) ) {
                out.reason = "unsafe_area";
                return out;
            }
        }
    }
    out.score = def.ai_priority + def.ai_magnitude;
    return out;
}

std::optional<ability_request> ability_ai::choose( const std::vector<ability_id> &abilities,
        const creature_ref &caster, const creature_ref &target )
{
    std::optional<ability_request> best;
    int score = 0;
    // Bound work even for native callers; authoring lists are limited to 16.
    for( size_t i = 0; i < std::min<size_t>( abilities.size(), 16 ); ++i ) {
        ability_request request;
        request.ability = abilities[i];
        request.caster = caster;
        request.target = request.ability.is_valid() && request.ability->target == "self" ? caster : target;
        const ability_ai_view candidate = query( request );
        // NOLINTBEGIN(cata-use-localized-sorting)
        if( candidate.eligible() && ( !best || candidate.score > score ||
                                      ( candidate.score == score &&
                                        request.ability.str() < best->ability.str() ) ) ) {
            // NOLINTEND(cata-use-localized-sorting)
            score = candidate.score;
            best = request;
        }
    }
    return best;
}
