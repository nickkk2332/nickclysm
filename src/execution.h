#pragma once
#ifndef CATA_SRC_EXECUTION_H
#define CATA_SRC_EXECUTION_H

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "translation.h"
#include "global_vars.h"
#include "type_id.h"

class JsonObject;
struct const_dialogue;

struct execution_range {
    int min = 0;
    int max = 0;
};

struct execution_transition {
    enum class kind { step, condition, chance, weighted, result } type = kind::step;
    std::string step;
    execution_range delay;
    std::function<bool( const_dialogue const & )> condition;
    double chance = 0;
    std::shared_ptr<execution_transition> yes;
    std::shared_ptr<execution_transition> no;
    std::vector<std::pair<int, std::shared_ptr<execution_transition>>> weighted;
    std::string result;
    std::map<std::string, std::shared_ptr<execution_transition>> cases;
};

struct execution_narration {
    enum class audience { visible, audible, visible_or_audible } perception = audience::visible;
    int volume = 0;
    translation audible_text;
    std::string entry;
    std::string append_to;
    std::string separator = " ";
};

struct execution_action {
    enum class kind { none, message, move, eoc, effect, damage, incapacitate } type = kind::none;
    std::string role = "target";
    std::string direction = "backward";
    execution_range tiles{ 1, 1 };
    int ticks_per_tile = 1;
    int first_after = 0;
    bool stop_before_unsupported = true;
    std::string store_result;
    std::vector<std::pair<int, translation>> messages;
    bool avoid_repeat = false;
    execution_narration narration;
    translation on_moved;
    execution_narration on_moved_narration;
    std::vector<effect_on_condition_id> eocs;
    efftype_id effect;
    int duration = 1;
    int intensity = 1;
    damage_type_id damage_type;
    int amount = 0;
    bodypart_str_id bodypart;
    execution_range incapacity_duration{ 10, 10 };
    bool wake_on_damage = true;
    std::string source;
};

struct execution_step {
    execution_action action;
    int repeat_count = 1;
    int interval = 1;
    int first_after = 0;
    bool repeats = false;
    std::shared_ptr<execution_transition> next;
    std::string end;
};

enum class execution_control { free, held, scripted };

struct execution_definition {
    execution_id id;
    std::vector<std::pair<execution_id, mod_id>> src;
    bool was_loaded = false;
    translation name;
    int revision = 1;
    int max_ticks = 30;
    int max_step_entries = 128;
    int max_distance = 12;
    execution_control control_actor = execution_control::free;
    execution_control control_target = execution_control::scripted;
    bool interrupt_damage = true;
    bool interrupt_displacement = true;
    bool require_weapon = false;
    bool allow_fatal_target = false;
    std::string start;
    global_variables::impl_t locals;
    std::map<std::string, execution_step> steps;
    std::vector<effect_on_condition_id> on_start;
    std::vector<effect_on_condition_id> on_tick;
    std::vector<effect_on_condition_id> on_complete;
    std::vector<effect_on_condition_id> on_interrupt;
    std::vector<effect_on_condition_id> on_end;

    void load( const JsonObject &jo, std::string_view source );
    void check() const;
};

namespace executions
{
void load( const JsonObject &jo, const std::string &source );
void reset();
void finalize();
void check();
} // namespace executions

#endif // CATA_SRC_EXECUTION_H
