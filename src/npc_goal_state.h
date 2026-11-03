#pragma once
#ifndef CATA_SRC_NPC_GOAL_STATE_H
#define CATA_SRC_NPC_GOAL_STATE_H

#include <string>

#include "calendar.h"

class JsonObject;
class JsonOut;

// The JSON goal an NPC is on (doc/JSON/NPC_BEHAVIOR.md), saved with it (npc::json_goal).  On
// load the goal resumes without running its on_start again.  Built-in goals keep their state in
// npc_short_term_cache and aren't saved.
struct npc_goal_state {
    // the goal's id; empty while the NPC is on no JSON goal
    std::string goal;
    time_point started = calendar::turn_zero;
    // goal-active turns so far: on_turn ran on each of them
    int turns_active = 0;
    // on_turn runs once a turn, however often the NPC moves in it
    time_point last_turn = calendar::before_time_starts;

    // the goal npc_goal_changed last reported for this NPC, "idle" for none; after a load,
    // the goal the NPC was on
    std::string reported = "idle"; // NOLINT(cata-serialize)

    bool active() const {
        return !goal.empty();
    }

    void serialize( JsonOut &jsout ) const;
    void deserialize( const JsonObject &jo );
};

#endif // CATA_SRC_NPC_GOAL_STATE_H
