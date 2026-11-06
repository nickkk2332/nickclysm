#pragma once
#ifndef CATA_SRC_ABILITY_RUNTIME_H
#define CATA_SRC_ABILITY_RUNTIME_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ability.h"
#include "calendar.h"
#include "coordinates.h"
#include "creature_ref.h"
#include "global_vars.h"

class Creature;
class damage_transaction_manager;
class JsonObject;
class JsonOut;
struct dialogue;
namespace execution_combat
{
struct attack_snapshot;
} // namespace execution_combat

enum class ability_invocation { action, effect_only };

struct ability_request {
    ability_id ability;
    creature_ref caster;
    creature_ref target;
    std::optional<tripoint_abs_ms> point;
    ability_invocation invocation = ability_invocation::action;
    global_variables::impl_t context;
};

struct ability_admission {
    int64_t id = 0;
    std::string reason;
    bool accepted() const {
        return id != 0;
    }
};

/** Observed primary native contacts, not an assertion that every effect is a hit. */
struct ability_contact {
    creature_ref target;
    global_variables::impl_t facts;
};

struct ability_view {
    int64_t id = 0;
    int64_t root = 0;
    int64_t parent = 0;
    int depth = 0;
    ability_id ability;
    creature_ref caster;
    creature_ref target;
    ability_invocation invocation = ability_invocation::action;
    std::string state;
    std::string reason;
    tripoint_abs_ms origin;
    tripoint_abs_ms selected;
    std::optional<tripoint_abs_ms> released_at;
    time_point started = calendar::before_time_starts;
    time_point due = calendar::before_time_starts;
    int windup_ticks = 0;
    int spell_level = 0;
    bool costs_committed = false;
    int paid_moves = 0;
    int paid_energy = 0;
    bool effect_completed = false;
    bool contacts_truncated = false;
    std::vector<ability_contact> contacts;
    global_variables::impl_t context;
};

/** Session-owned scheduler. Requests and callbacks never recursively pump it. */
class ability_manager
{
    public:
        ability_manager();
        ~ability_manager();
        ability_manager( const ability_manager & ) = delete;
        ability_manager &operator=( const ability_manager & ) = delete;

        // Pure native admission check: no EOCs, spell formulas, RNG or payment.
        std::string check( const ability_request &request ) const;
        ability_admission request( const ability_request &request );
        ability_admission request( const ability_id &id, const dialogue &d,
                                   ability_invocation mode = ability_invocation::action,
                                   std::optional<tripoint_abs_ms> point = std::nullopt );
        bool cancel( int64_t id, const std::string &reason = "cancelled" );
        void cancel_all( const std::string &reason );
        void cancel_for_world_transition( const std::string &reason );
        void clear();
        void reset_for_load();
        void end_restore();
        bool restoring() const;
        bool processing() const;
        damage_transaction_manager &damage_transactions();
        void drain_starts();
        void process();
        bool controls( const Creature &who ) const;
        bool owns_avatar_activity( int64_t id ) const;
        void notify_damage( const Creature &who, int amount );
        void record_contact( const execution_combat::attack_snapshot &contact );
        std::optional<ability_view> inspect( int64_t id ) const;
        std::vector<ability_view> inspect_all() const;
        void serialize( JsonOut &jsout ) const;
        void deserialize( const JsonObject &jo );

    private:
        struct impl;
        std::unique_ptr<impl> data;
};

#endif // CATA_SRC_ABILITY_RUNTIME_H
