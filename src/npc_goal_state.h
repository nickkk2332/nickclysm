#pragma once
#ifndef CATA_SRC_NPC_GOAL_STATE_H
#define CATA_SRC_NPC_GOAL_STATE_H

#include <optional>
#include <string>

#include "calendar.h"

class JsonObject;
class JsonOut;

// how hostile npc_becomes_hostile last reported an NPC to be (npc.cpp)
enum class npc_hostility : int {
    // neutral or friendly
    none,
    // mugging the avatar, or waiting for it to leave
    hostile,
    // out to kill it
    kill
};

// The JSON goal an NPC is on (doc/JSON/NPC_BEHAVIOR.md), saved with it (npc::json_goal).  On
// load the goal resumes without running its on_start again.  Built-in goals keep their state in
// npc_short_term_cache and aren't saved.  Also what the NPC's goal and attitude events last
// reported.
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

    // how hostile npc_becomes_hostile last reported the NPC: a flight keeps it, a neutral or
    // friendly attitude ends it.  nullopt until first needed, when it is read off the attitude
    // the NPC had.  npc::store() saves it.
    std::optional<npc_hostility> hostility; // NOLINT(cata-serialize)
    // the NPC's attitude events are going out: an attitude their EOCs set goes unreported, and
    // the first such is an error
    bool reporting_attitude = false; // NOLINT(cata-serialize)
    bool warned_nested_attitude = false; // NOLINT(cata-serialize)

    bool active() const {
        return !goal.empty();
    }

    void serialize( JsonOut &jsout ) const;
    void deserialize( const JsonObject &jo );
};

#endif // CATA_SRC_NPC_GOAL_STATE_H
