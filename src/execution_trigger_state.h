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
        std::vector<execution_trigger_report> history() const;
        void clear();
        void serialize( JsonOut &out ) const;
        void deserialize( const JsonObject &obj );
    private:
        std::map<std::string, time_point> cooldowns;
        std::deque<uint64_t> recent; // NOLINT(cata-serialize): runtime deduplication
        std::optional<execution_trigger_report> last; // NOLINT(cata-serialize): inspector history only
        std::deque<execution_trigger_report> reports; // NOLINT(cata-serialize): inspector history only
        bool selecting = false; // NOLINT(cata-serialize)
        uint64_t next_contact = 1;
        time_point next_sweep = calendar::before_time_starts; // NOLINT(cata-serialize)
};

#endif // CATA_SRC_EXECUTION_TRIGGER_STATE_H
