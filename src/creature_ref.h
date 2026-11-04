#pragma once
#ifndef CATA_SRC_CREATURE_REF_H
#define CATA_SRC_CREATURE_REF_H

#include <cstdint>

#include "character_id.h"
#include "type_id.h"

class Creature;
class JsonObject;
class JsonOut;

// A monster copy is a new creature; moving or loading one preserves its identity.
class monster_uid
{
    public:
        monster_uid() = default;
        monster_uid( const monster_uid & );
        monster_uid &operator=( const monster_uid & );
        monster_uid( monster_uid && ) noexcept;
        monster_uid &operator=( monster_uid && ) noexcept;
        int64_t get() const;
        void load( int64_t saved );
    private:
        mutable int64_t value = 0;
};

/** A dimension-bound identity, never a position or a tracker vector index. */
struct creature_ref {
    enum class kind { none, character, monster } type = kind::none;
    int64_t id = 0;
    dimension_id dimension;

    static creature_ref bind( const Creature &who );
    Creature *resolve() const;
    bool operator==( const creature_ref &other ) const;
    void serialize( JsonOut &jsout ) const;
    void deserialize( const JsonObject &jo );
};

#endif // CATA_SRC_CREATURE_REF_H
