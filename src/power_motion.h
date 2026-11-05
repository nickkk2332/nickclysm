#pragma once
#ifndef CATA_SRC_POWER_MOTION_H
#define CATA_SRC_POWER_MOTION_H

#include <string>
#include <vector>

#include "coordinates.h"
#include "creature_ref.h"

class Creature;

struct power_motion_impact {
    tripoint_abs_ms position;
    std::string kind;
    creature_ref collider;
    float force = 0;
    int moved_hp_loss = 0;
    int collider_hp_loss = 0;
    bool broken = false;
};

/** A native flight receipt. Paths and contacts are observations, never predictions. */
struct power_motion_result {
    creature_ref instigator;
    creature_ref target;
    std::string cause;
    std::string reason;
    bool accepted = false;
    tripoint_abs_ms origin;
    tripoint_abs_ms destination;
    float velocity = 0;
    float remaining_velocity = 0;
    int hp_loss = 0;
    bool landed = false;
    bool fell = false;
    std::vector<tripoint_abs_ms> path;
    std::vector<power_motion_impact> impacts;
};

namespace power_motion
{
power_motion_result fling( const creature_ref &source, const creature_ref &target,
                           int velocity, bool controlled = false, bool hit_characters = true,
                           const std::string &cause = "power" );

/** A nested native flight is a barrier, even when it has no authored source. */
class flight_scope
{
    public:
        flight_scope( Creature &target, power_motion_result *result );
        ~flight_scope();
        flight_scope( const flight_scope & ) = delete;
        flight_scope &operator=( const flight_scope & ) = delete;
    private:
        friend bool active( const Creature & );
        friend bool processing();
        friend Creature *instigator( const Creature * );
        flight_scope *previous;
        Creature &target;
        power_motion_result *result;
};
bool active( const Creature &target );
bool processing();
Creature *instigator( const Creature *target );
} // namespace power_motion

#endif // CATA_SRC_POWER_MOTION_H
