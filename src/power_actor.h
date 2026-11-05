#pragma once
#ifndef CATA_SRC_POWER_ACTOR_H
#define CATA_SRC_POWER_ACTOR_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ability.h"
#include "calendar.h"
#include "creature.h"
#include "power_motion.h"

class JsonObject;
class JsonOut;
class monster;

struct power_summon;
using power_summon_id = string_id<power_summon>;
struct power_summon {
    power_summon_id id;
    std::vector<std::pair<power_summon_id, mod_id>> src;
    bool was_loaded = false;
    translation name;
    int revision = 1;
    mtype_id monster;
    int duration_ticks = 60;
    int max_per_owner = 2;
    int range = 5;
    int leash = 30;
    std::vector<ability_id> abilities;
    std::vector<effect_on_condition_id> on_spawn;
    std::vector<effect_on_condition_id> on_command;
    std::vector<effect_on_condition_id> on_end;
    void load( const JsonObject &jo, std::string_view source );
    void check() const;
};
namespace power_summons
{
void load( const JsonObject &jo, const std::string &source );
void reset();
void finalize();
void check();
}

struct power_summon_view {
    int64_t id = 0;
    power_summon_id definition;
    creature_ref owner;
    creature_ref summon;
    creature_ref target;
    std::string command = "follow";
    tripoint_abs_ms guard;
    time_point expires = calendar::before_time_starts;
    std::string state = "active";
    std::string reason;
};
struct power_summon_admission {
    int64_t id = 0;
    std::string reason;
    creature_ref summon;
};

/** Owned spawned monsters only. This never adopts or deletes an unrelated actor. */
class power_actor_manager
{
    public:
        power_actor_manager();
        ~power_actor_manager();
        power_summon_admission summon( const power_summon_id &id, const creature_ref &owner,
                                       std::optional<tripoint_abs_ms> point = std::nullopt );
        std::string command( int64_t id, const creature_ref &owner, const std::string &command,
                             const creature_ref &target = {}, std::optional<tripoint_abs_ms> point = std::nullopt );
        int dismiss( const creature_ref &owner, const power_summon_id &filter = {} );
        bool end( int64_t id, const std::string &reason );
        void process();
        void cancel_all( const std::string &reason );
        void clear();
        bool processing() const;
        void retire_actor( const Creature &actor );
        std::optional<creature_ref> owner_of( const Creature &summon ) const;
        std::optional<Creature::Attitude> attitude( const Creature &summon, const Creature &other ) const;
        bool plan( monster &summon );
        bool take_turn( monster &summon );
        void record_motion( const power_motion_result &result );
        std::vector<power_motion_result> motions() const;
        std::vector<power_summon_view> inspect() const;
        void serialize( JsonOut &out ) const;
        void deserialize( const JsonObject &jo );
    private:
        struct impl;
        std::unique_ptr<impl> data;
};
power_actor_manager &get_power_actors();

#endif // CATA_SRC_POWER_ACTOR_H
