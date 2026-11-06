#pragma once
#ifndef CATA_SRC_ABILITY_H
#define CATA_SRC_ABILITY_H

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "translation.h"
#include "power.h"
#include "type_id.h"
#include "global_vars.h"

class JsonObject;
struct ability_definition;
using ability_id = string_id<ability_definition>;

/** Opt-in action policy around existing effect implementations. */
struct ability_definition {
    ability_id id;
    std::vector<std::pair<ability_id, mod_id>> src;
    bool was_loaded = false;
    translation name;
    int revision = 1;
    int windup_min = 0;
    int windup_max = 0;
    int range = 1;
    std::string target = "creature";
    bool line_of_sight = true;
    bool interrupt_damage = true;
    bool interrupt_displacement = true;
    bool allow_effect_only = false;
    int move_cost = 100;
    std::string energy = "none";
    int energy_cost = 0;
    bool reserve_energy = false;
    power_cooldown_id cooldown;
    int charge_cost = 1;
    spell_id spell;
    int spell_level = 0;
    std::vector<effect_on_condition_id> eocs;
    std::vector<effect_on_condition_id> on_start;
    std::vector<effect_on_condition_id> before_release;
    std::vector<effect_on_condition_id> on_complete;
    std::vector<effect_on_condition_id> on_interrupt;
    std::vector<effect_on_condition_id> on_end;
    global_variables::impl_t locals;

    void load( const JsonObject &jo, std::string_view source );
    void check() const;
};

namespace abilities
{
void load( const JsonObject &jo, const std::string &source );
void reset();
void finalize();
void check();
} // namespace abilities

#endif // CATA_SRC_ABILITY_H
