#pragma once
#ifndef CATA_SRC_DIALOGUE_TOPIC_OVERRIDE_H
#define CATA_SRC_DIALOGUE_TOPIC_OVERRIDE_H

#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "type_id.h"

class JsonObject;
struct const_dialogue;

struct dialogue_topic_override;
using dialogue_topic_override_id = string_id<dialogue_topic_override>;

/**
 * MATH-J10: a topic a conversation with an NPC opens on when a condition holds, as the
 * built-in rules do for a deaf avatar or a sedated NPC.  It can also turn built-in rules off.
 */
struct dialogue_topic_override {
    dialogue_topic_override_id id;
    std::vector<std::pair<dialogue_topic_override_id, mod_id>> src;
    bool was_loaded = false;

    // u is the avatar, npc the NPC talked to; without one, always
    std::function<bool( const_dialogue const & )> condition;
    // the topic opened on, or "" to only turn rules off
    std::string topic;
    // the topic of the highest priority comes first; ties go by id
    int priority = 0;
    // built-in rules (see builtin_rules()) that don't apply when the condition holds
    std::set<std::string> replaces;

    void load( const JsonObject &jo, std::string_view src );
    void check() const;

    static void load_override( const JsonObject &jo, const std::string &src );
    static void finalize_all();
    static void check_all();
    static void reset();
    static const std::vector<dialogue_topic_override> &get_all();
};

namespace dialogue_topic_overrides
{
/** The names of the built-in rules of talker_npc::get_topics that an override can replace. */
const std::vector<std::string_view> &builtin_rules();

struct applied {
    // to add after the built-in topics, lowest priority first (the last is shown first)
    std::vector<std::string> topics;
    std::set<std::string> replaced;
};
/** The overrides whose condition holds in @p d. */
applied evaluate( const_dialogue const &d );
} // namespace dialogue_topic_overrides

#endif // CATA_SRC_DIALOGUE_TOPIC_OVERRIDE_H
