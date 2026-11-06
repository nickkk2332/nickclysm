#pragma once
#ifndef CATA_SRC_POWER_H
#define CATA_SRC_POWER_H

#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "global_vars.h"
#include "translation.h"
#include "type_id.h"

class Creature;
class JsonObject;
struct power_resource;
struct power_cooldown;
struct power_application;
using power_resource_id = string_id<power_resource>;
using power_cooldown_id = string_id<power_cooldown>;
using power_application_id = string_id<power_application>;

struct power_cost {
    std::string resource = "none";
    int amount = 0;
    void load( const JsonObject &jo );
};

struct power_effect_grant {
    efftype_id effect;
    bodypart_str_id bodypart = bodypart_str_id::NULL_ID();
    int intensity = 1;
};
struct power_equipment_grant {
    itype_id item;
    std::string slot = "worn";
};
struct power_grant_definition {
    std::vector<power_effect_grant> effects;
    std::vector<power_equipment_grant> equipment;
    bool form = false;
    bool stow_worn = false;
    bool stow_weapon = false;
    bool empty() const;
    void load( const JsonObject &jo );
    void check( const std::string &id ) const;
};

struct power_resource {
    power_resource_id id;
    std::vector<std::pair<power_resource_id, mod_id>> src;
    bool was_loaded = false;
    translation name;
    int capacity = 100;
    int initial = 100;
    int regen_amount = 0;
    int regen_ticks = 1;
    std::set<std::string> actor_kinds;
    void load( const JsonObject &jo, std::string_view source );
    void check() const;
};

struct power_cooldown {
    power_cooldown_id id;
    std::vector<std::pair<power_cooldown_id, mod_id>> src;
    bool was_loaded = false;
    translation name;
    int max_charges = 1;
    int initial_charges = 1;
    int recharge_ticks = 1;
    int cooldown_ticks = 0;
    void load( const JsonObject &jo, std::string_view source );
    void check() const;
};

struct power_application {
    power_application_id id;
    std::vector<std::pair<power_application_id, mod_id>> src;
    bool was_loaded = false;
    translation name;
    int revision = 1;
    int duration_ticks = 0;
    int interval_ticks = 1;
    int max_per_owner = 8;
    double magnitude = 1;
    std::set<std::string> tags;
    bool interrupt_damage = false;
    bool interrupt_incapacitated = false;
    bool interrupt_displacement = false;
    power_cost start_cost;
    power_cost upkeep;
    std::vector<effect_on_condition_id> on_start;
    std::vector<effect_on_condition_id> on_tick;
    std::vector<effect_on_condition_id> on_end;
    global_variables::impl_t locals;
    power_grant_definition grants;
    void load( const JsonObject &jo, std::string_view source );
    void check() const;
};

namespace powers
{
void load_resource( const JsonObject &jo, const std::string &source );
void load_cooldown( const JsonObject &jo, const std::string &source );
void load_application( const JsonObject &jo, const std::string &source );
void reset();
void finalize();
void check();
bool valid_resource( const std::string &resource );
std::string actor_kind( const Creature &who );
} // namespace powers

#endif // CATA_SRC_POWER_H
