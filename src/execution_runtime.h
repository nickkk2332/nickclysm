#pragma once
#ifndef CATA_SRC_EXECUTION_RUNTIME_H
#define CATA_SRC_EXECUTION_RUNTIME_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "calendar.h"
#include "global_vars.h"
#include "type_id.h"

class Creature;
class JsonObject;
class JsonOut;
struct dialogue;
namespace execution_combat
{
struct attack_snapshot;
} // namespace execution_combat

struct execution_trigger_candidate {
    std::string rule;
    std::string reason;
};

struct execution_trigger_report {
    uint64_t attack = 0;
    int considered = 0;
    int64_t instance = 0;
    std::string selected;
    std::string reason;
    std::vector<execution_trigger_candidate> candidates;
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

/** Saved, bounded, game-turn state machines. No graph work runs inside start/cancel. */
class execution_manager
{
    public:
        execution_manager();
        ~execution_manager();
        execution_manager( const execution_manager & ) = delete;
        execution_manager &operator=( const execution_manager & ) = delete;
        execution_start_result start( const execution_id &definition, const dialogue &context,
                                      std::optional<uint64_t> seed = std::nullopt );
        std::string check_start( const execution_id &definition, const dialogue &context );
        bool processing_actions() const;
        uint64_t next_attack_id();
        void on_attack_resolved( const execution_combat::attack_snapshot &snapshot );
        std::optional<execution_trigger_report> last_trigger_report() const;
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
        Creature *secondary_damage_source() const;
        std::optional<execution_view> inspect( int64_t id ) const;
        std::vector<execution_narration_segment> narration( int64_t id ) const;
        void serialize( JsonOut &jsout ) const;
        void deserialize( const JsonObject &jo );
    private:
        execution_start_result start_impl( const execution_id &definition, const dialogue &context,
                                           std::optional<uint64_t> seed, bool check_only );
        struct impl;
        std::unique_ptr<impl> data;
};

#endif // CATA_SRC_EXECUTION_RUNTIME_H
