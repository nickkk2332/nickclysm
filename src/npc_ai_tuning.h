#pragma once
#ifndef CATA_SRC_NPC_AI_TUNING_H
#define CATA_SRC_NPC_AI_TUNING_H

#include <string>
#include <string_view>

#include "calendar.h"

class Character;
class JsonObject;
class npc;
struct npc_permission_verdict;

// the scales the need urgencies are on (character_oracle.cpp): thirst 1200 is dying of thirst,
// sleepiness 1000 is passing out
constexpr float npc_thirst_scale = 1200.0f;
constexpr float npc_sleepiness_scale = 1000.0f;

// How urgent a need must be before an NPC acts on it, from 0 to 1 on the need's urgency scale.
struct npc_need_levels {
    // where the need's own predicate fires, and down to where a discouraged option's floor
    // relaxes
    float act = 0.0f;
    // an option its obligations discourage waits for this
    float insist = 0.0f;
    // an option they allow only in an emergency waits for this
    float emergency = 0.0f;
};

// The numbers an NPC's needs, its arbitration between needs and obligations, and its sleep run
// on.  Read from its npc_class's "ai_tuning" (doc/JSON/NPCs.md, doc/JSON/NPC_BEHAVIOR.md); a
// class that gives none has these defaults, today's constants.
struct npc_ai_tuning {
    npc_need_levels drink_water;
    npc_need_levels eat_food;
    npc_need_levels go_to_sleep;
    npc_need_levels regulate_temperature;
    // a need more urgent than this takes over from another need whatever the margin
    float need_critical_urgency;

    // the built-in scores of the obligations
    // on shift at its post, and the least while away from it
    float duty;
    // on shift away from its post, at the most
    float duty_max;
    // following the avatar from far behind, or from another level
    float follow_max;
    // walking where it was ordered to
    float goto_order;
    float camp_work;
    float return_to_camp;
    float free_time;

    // hunger from which an NPC that burns stored calories counts as starving
    int starvation_hunger;
    // the least a camp meal serves an NPC that is merely hungry
    int camp_meal_kcal;

    // a settling NPC tries to fall asleep this often
    time_duration settle_interval;
    // and gives the spot up after this long
    time_duration settle_timeout;
    // woken this many times within stall_window, its sleep stalls
    int stall_wakes;
    time_duration stall_window;
    // a stalled sleep steps aside for other work this long
    time_duration stall_duration;

    // the defaults
    npc_ai_tuning();

    // the thirst above which it wants water, and the sleepiness at which it is tired enough to
    // sleep, in the units the character has them in
    int thirst_act_level() const;
    int tired_level() const;

    // the levels of a need by its goal ("drink_water"...), nullptr for another name
    const npc_need_levels *need( std::string_view goal ) const;

    // changes what the "ai_tuning" object gives; throws when the result is out of range
    void load( const JsonObject &jo );
    // what is out of range, "" when nothing is
    std::string problems() const;

    static const npc_ai_tuning &defaults();
};

// the tuning of the NPC's class; the defaults for one without a class, or a character that isn't
// an NPC
const npc_ai_tuning &ai_tuning_of( const npc &guy );
const npc_ai_tuning &ai_tuning_of( const Character &who );

// raw level a need with these levels must reach before the NPC acts on an option judged v
float npc_need_floor( const npc_need_levels &levels, const npc_permission_verdict &v );

#endif // CATA_SRC_NPC_AI_TUNING_H
