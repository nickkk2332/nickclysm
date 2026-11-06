#pragma once
#ifndef CATA_SRC_EXECUTION_RUNTIME_H
#define CATA_SRC_EXECUTION_RUNTIME_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "calendar.h"
#include "creature_ref.h"
#include "global_vars.h"
#include "type_id.h"

class Creature;
class JsonObject;
class JsonOut;
struct dialogue;
struct ma_event_context;

namespace execution_combat
{
struct attack_snapshot;
} // namespace execution_combat

struct execution_trigger_candidate {
    std::string rule;
    std::string reason;
    int priority = 0;
    int weight = 1;
    double chance = 1;
    std::optional<double> roll;
};

struct execution_trigger_report {
    uint64_t attack = 0;
    int considered = 0;
    int64_t instance = 0;
    std::string selected;
    std::string reason;
    std::vector<execution_trigger_candidate> candidates;
    time_point when = calendar::before_time_starts;
    creature_ref actor;
    creature_ref target;
    global_variables::impl_t facts;
    int total_weight = 0;
    std::optional<int> weighted_roll;
};

struct execution_random_draw {
    std::string step;
    int entry = 0;
    std::string purpose;
    double minimum = 0;
    double maximum = 0;
    double value = 0;
    bool sampled = false;
    void serialize( JsonOut &out ) const;
    void deserialize( const JsonObject &obj );
};

struct execution_start_result {
    int64_t id = 0;
    std::string reason;
};

struct execution_view {
    int64_t id = 0;
    execution_id definition;
    std::string step;
    std::string state;
    std::string reason;
    std::string outcome;
    time_point due = calendar::before_time_starts;
    global_variables::impl_t context;
    std::vector<std::string> transcript;
};

struct execution_narration_segment {
    std::string id;
    std::string group;
    std::string entry;
    std::string role;
    std::string audience;
    time_point when = calendar::turn_zero;
    std::string text;
    void serialize( JsonOut &out ) const;
    void deserialize( const JsonObject &obj );
};

/** Detached, read-only diagnostic snapshot; never evaluates author conditions. */
struct execution_inspection {
    execution_view view;
    global_variables::impl_t details;
    std::vector<execution_random_draw> draws;
    std::vector<execution_narration_segment> narration;
};

/** Saved, bounded, game-turn state machines. No graph work runs inside start/cancel. */
class execution_manager
{
    public:
        execution_manager();
        ~execution_manager();
        execution_manager( const execution_manager & ) = delete;
        execution_manager &operator=( const execution_manager & ) = delete;
        execution_start_result start( const execution_id &definition, const dialogue &d,
                                      std::optional<uint64_t> seed = std::nullopt );
        std::string check_start( const execution_id &definition, const dialogue &d );
        std::string check_fatal_start( const execution_id &definition, const dialogue &d );
        execution_start_result start_fatal( const execution_id &definition, const dialogue &d );
        int64_t on_fatal_hit( const execution_combat::attack_snapshot &snapshot );
        void release_fatal_attack( int64_t id, bool successful );
        bool death_pending( const Creature &who ) const;
        bool owns_fatal_motion( const Creature &who ) const;
        bool owns_fatal_gravity( const Creature &who ) const;
        bool settle_fatal( const Creature &who, const std::string &reason );
        void settle_for_removal( const Creature &who, const std::string &reason );
        void defer_kill( const Creature &target, const ma_event_context &ctx );
        void reset_for_load();
        void end_restore();
        bool restoring() const;
        /** Settle fatal owners before writing an independent dimension snapshot. */
        bool prepare_world_transition();
        bool processing_actions() const;
        uint64_t next_attack_id();
        void on_attack_resolved( const execution_combat::attack_snapshot &snapshot );
        std::optional<execution_trigger_report> last_trigger_report() const;
        std::vector<execution_trigger_report> trigger_history() const;
        bool cancel( int64_t id, const std::string &reason = "cancelled" );
        void cancel_all( const std::string &reason );
        void clear();
        void process();
        void drain_starts();
        bool controls( const Creature &who ) const;
        bool contains( const Creature &who ) const;
        bool owns_avatar_activity( int64_t id ) const;
        bool can_fall( int64_t id, const std::string &role ) const;
        void notify_damage( const Creature &who, int amount );
        Creature *secondary_damage_source( const Creature *receiver = nullptr ) const;
        bool claim_physical_flight( const Creature &target );
        bool physical_action_valid() const;
        Creature *physical_instigator() const;
        std::optional<execution_view> inspect( int64_t id ) const;
        std::vector<execution_inspection> inspect_all() const;
        std::vector<execution_narration_segment> narration( int64_t id ) const;
        void serialize( JsonOut &jsout ) const;
        void deserialize( const JsonObject &jo );
    private:
        execution_start_result start_impl( const execution_id &definition, const dialogue &d,
                                           std::optional<uint64_t> seed, bool check_only, bool fatal = false );
        struct impl;

        std::unique_ptr<impl> data;
};

#endif // CATA_SRC_EXECUTION_RUNTIME_H
