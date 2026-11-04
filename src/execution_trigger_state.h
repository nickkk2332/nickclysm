#pragma once
#ifndef CATA_SRC_EXECUTION_TRIGGER_STATE_H
#define CATA_SRC_EXECUTION_TRIGGER_STATE_H

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>

#include "calendar.h"
#include "execution_runtime.h"

class JsonObject;
class JsonOut;
namespace execution_combat
{
struct attack_snapshot;
} // namespace execution_combat

/** Per-game cooldowns and bounded diagnostics, independent of active scene lifetimes. */
class execution_trigger_state
{
    public:
        execution_trigger_report consider( execution_manager &manager,
                                           const execution_combat::attack_snapshot &snapshot );
        bool is_selecting() const;
        uint64_t allocate_contact();
        std::optional<execution_trigger_report> inspect() const;
        void clear();
        void serialize( JsonOut &out ) const;
        void deserialize( const JsonObject &obj );
    private:
        std::map<std::string, time_point> cooldowns;
        std::deque<uint64_t> recent;
        std::optional<execution_trigger_report> last;
        bool selecting = false;
        uint64_t next_contact = 1;
        time_point next_sweep = calendar::before_time_starts;
};

#endif // CATA_SRC_EXECUTION_TRIGGER_STATE_H
