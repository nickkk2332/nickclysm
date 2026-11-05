#pragma once
#ifndef CATA_SRC_DAMAGE_RULE_H
#define CATA_SRC_DAMAGE_RULE_H

#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dialogue_helpers.h"
#include "type_id.h"
#include "power.h"

class Creature;
class JsonObject;
struct const_dialogue;
struct damage_rule;
using damage_rule_id = string_id<damage_rule>;

struct damage_rule_action {
    std::string operation;
    dbl_or_var value;
    std::string resource = "none";
    int cost_per_damage = 1;
    damage_type_id from;
    damage_type_id to;
};

struct damage_rule {
    damage_rule_id id;
    std::vector<std::pair<damage_rule_id, mod_id>> src;
    bool was_loaded = false;
    std::string stage = "after_armor";
    int priority = 0;
    efftype_id required_effect;
    power_application_id required_application;
    std::set<std::string> defender_kinds;
    std::set<std::string> source_kinds;
    std::set<damage_type_id> damage_types;
    std::set<bodypart_str_id> bodyparts;
    std::set<spell_id> spells;
    bool has_condition = false;
    std::function<bool( const_dialogue const & )> condition;
    std::vector<effect_on_condition_id> mutate;
    std::vector<damage_rule_action> actions;
    std::vector<effect_on_condition_id> on_resolved;

    void load( const JsonObject &jo, std::string_view source );
    void check() const;
};

namespace damage_rules
{
void load( const JsonObject &jo, const std::string &source );
void reset();
void finalize();
void check();
bool may_intercept( const Creature &defender );
const std::vector<const damage_rule *> &ordered();
} // namespace damage_rules

#endif // CATA_SRC_DAMAGE_RULE_H
