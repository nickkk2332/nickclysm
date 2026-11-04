#pragma once
#ifndef CATA_SRC_MESSAGE_LOG_H
#define CATA_SRC_MESSAGE_LOG_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "calendar.h"
#include "color.h"
#include "enums.h"

class JsonObject;
class JsonOut;

struct game_message {
    std::string message;
    time_point timestamp_in_turns = calendar::turn_zero;
    int timestamp_in_user_actions = 0; // NOLINT(cata-serialize)
    int count = 1;
    unsigned cooldown_seen = 1; // NOLINT(cata-serialize)
    bool cooldown_hidden = false; // NOLINT(cata-serialize)
    game_message_type type = m_neutral;
    std::string owner;
    std::string group;
    std::string segment;
    std::string separator = " ";

    game_message() = default;
    game_message( std::string &&msg, game_message_type type );
    const time_point &turn() const;
    std::string get_with_count() const;
    bool is_in_cooldown() const;
    bool is_new( const time_point &current ) const;
    bool is_recent( const time_point &current ) const;
    nc_color get_color( const time_point &current ) const;
    void deserialize( const JsonObject &obj );
    void serialize( JsonOut &jsout ) const;
};

namespace Messages
{
struct owned_message_result;
} // namespace Messages

/** Non-UI history shared by the game and its test message sink. */
class message_log
{
    public:
        std::deque<game_message> messages;
        time_point curmes = calendar::turn_zero;
        bool active = true;

        Messages::owned_message_result add_owned( const std::string &owner, const std::string &anchor,
                std::string text, const std::string &separator, size_t limit );
        std::vector<game_message> compact_messages( size_t count ) const;
        std::vector<std::pair<std::string, std::string>> recent( size_t count,
                bool compact = false ) const;
        void clear();
        void serialize( JsonOut &jsout ) const;
        void deserialize( const JsonObject &obj );
    private:
        std::string origin;
        uint64_t next_id = 1;
        std::string new_id();
};

#endif // CATA_SRC_MESSAGE_LOG_H
