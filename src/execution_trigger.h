#pragma once
#ifndef CATA_SRC_EXECUTION_TRIGGER_H
#define CATA_SRC_EXECUTION_TRIGGER_H

#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "execution_combat.h"
#include "type_id.h"

class JsonObject;
struct const_dialogue;
struct execution_trigger;
using execution_trigger_id = string_id<execution_trigger>;

struct execution_trigger_range {
    int minimum = 0;
    int maximum = std::numeric_limits<int>::max();
    bool contains( int value ) const;
};

struct execution_trigger {
    execution_trigger_id id;
    std::vector<std::pair<execution_trigger_id, mod_id>> src;
    bool was_loaded = false;
    execution_id execution;
    bool fatal = false;
    int priority = 0;
    int weight = 1;
    double chance = 1;
    int cooldown_ticks = 0;
    std::string cooldown_scope = "actor_target_pair";
    std::set<execution_combat::channel> channels;
    std::set<std::string> source_kinds;
    std::set<std::string> target_kinds;
    std::set<std::string> source_monsters;
    std::set<std::string> target_monsters;
    std::set<std::string> weapons;
    std::set<std::string> ammunition;
    std::set<std::string> weapon_categories;
    std::set<std::string> weapon_skills;
    std::set<std::string> styles;
    std::set<std::string> techniques;
    std::set<std::string> bodyparts;
    std::set<std::string> weakpoints;
    std::set<std::string> terrains;
    std::set<std::string> furniture;
    bool hit = true;
    std::optional<bool> critical;
    std::optional<execution_trigger_range> hp_loss;
    std::map<std::string, execution_trigger_range> damage;
    std::function<bool( const const_dialogue & )> condition;

    void load( const JsonObject &jo, std::string_view source );
    void check() const;
    std::string matches( const execution_combat::attack_snapshot &s ) const;
    std::string cooldown_key( const execution_combat::attack_snapshot &s ) const;
};

namespace execution_triggers
{
void load( const JsonObject &jo, const std::string &source );
void reset();
void finalize();
void check();
bool has_rules( execution_combat::channel channel );
bool has_fatal_rules( execution_combat::channel channel );
std::vector<const execution_trigger *> candidates( const execution_combat::attack_snapshot
        &snapshot );
} // namespace execution_triggers

#endif // CATA_SRC_EXECUTION_TRIGGER_H
