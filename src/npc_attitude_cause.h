#pragma once
#ifndef CATA_SRC_NPC_ATTITUDE_CAUSE_H
#define CATA_SRC_NPC_ATTITUDE_CAUSE_H

// Why an NPC's attitude changes, for the npc_attitude_changed and npc_becomes_hostile events
// (doc/JSON/EFFECT_ON_CONDITION.md).  The code that changes attitudes for one reason opens a
// cause for its scope, and npc::set_attitude() reads the innermost one: mutiny() inside
// make_angry() reports "mutiny", the rest of make_angry() "angered".
class npc_attitude_cause
{
    public:
        // a dialogue response's or an EOC's effects (talk_effect_t::apply())
        static constexpr const char *effect = "effect";
        // the NPC was angered: attacked or robbed by the avatar, a dialogue gone wrong
        static constexpr const char *angered = "angered";
        // the NPC sized the avatar up: on meeting, on being pushed aside, as a conversation starts
        static constexpr const char *opinion = "opinion";
        // the NPC left the avatar's faction (npc::mutiny())
        static constexpr const char *mutiny = "mutiny";
        // the NPC's faction is hostile to the avatar's
        static constexpr const char *faction = "faction";
        // the NPC runs from a fight
        static constexpr const char *panic = "panic";
        // the NPC saw the avatar steal, or answers a call about it
        static constexpr const char *theft = "theft";
        // anything else the NPC decides on its turn (npc::move())
        static constexpr const char *ai = "ai";

        // cause is one of the names above, or another string that outlives the scope
        explicit npc_attitude_cause( const char *cause );
        ~npc_attitude_cause();
        npc_attitude_cause( const npc_attitude_cause & ) = delete;
        npc_attitude_cause &operator=( const npc_attitude_cause & ) = delete;

        // the innermost open cause, "" when none is open
        static const char *current();
    private:
        const char *previous;
};

#endif // CATA_SRC_NPC_ATTITUDE_CAUSE_H
