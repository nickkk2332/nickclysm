#pragma once
#ifndef CATA_SRC_ABILITY_AI_H
#define CATA_SRC_ABILITY_AI_H

#include <optional>
#include <string>
#include <vector>

#include "ability.h"
#include "ability_runtime.h"

struct creature_ref;

struct ability_ai_view {
    ability_id ability;
    std::string intent;
    std::string reason;
    int score = 0;
    int magnitude = 0;
    int energy_cost = 0;
    int move_cost = 0;
    int windup_min = 0;
    int windup_max = 0;
    bool eligible() const;
};
namespace ability_ai
{
/** Declarative estimates only: no EOCs, spell formula evaluation, RNG or mutations. */
ability_ai_view query( const ability_request &request );
std::optional<ability_request> choose( const std::vector<ability_id> &abilities,
                                       const creature_ref &caster, const creature_ref &target );
} // namespace ability_ai

#endif // CATA_SRC_ABILITY_AI_H
