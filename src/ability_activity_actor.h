#pragma once
#ifndef CATA_SRC_ABILITY_ACTIVITY_ACTOR_H
#define CATA_SRC_ABILITY_ACTIVITY_ACTOR_H

#include <cstdint>
#include "activity_actor.h"

class ability_activity_actor : public activity_actor
{
    public:
        int64_t instance = 0;
        explicit ability_activity_actor( int64_t id ) : instance( id ) {}
        const activity_id &get_type() const override;
        void start( player_activity &act, Character &who ) override;
        void do_turn( player_activity &act, Character &who ) override;
        void finish( player_activity &act, Character &who ) override;
        void canceled( player_activity &act, Character &who ) override;
        std::unique_ptr<activity_actor> clone() const override;
        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonValue &jsin );
};

#endif // CATA_SRC_ABILITY_ACTIVITY_ACTOR_H
