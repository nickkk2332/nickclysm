#pragma once
#ifndef CATA_SRC_ACTIVITY_ACTOR_EOC_H
#define CATA_SRC_ACTIVITY_ACTOR_EOC_H

#include <memory>
#include <optional>
#include <string>

#include "calendar.h"
#include "type_id.h"

class Character;
class activity_actor;

// Starting activities that have an activity actor from EOCs (u_assign_activity).
namespace activity_actors
{

/** Whether @p type is run by an activity actor (it has an entry in deserialize_functions). */
bool has_actor( const activity_id &type );

/** Whether u_assign_activity can start @p type, which has an actor. */
bool can_start_from_eoc( const activity_id &type );

/** Whether the actor of @p type, started from an EOC, uses the effect's "duration". */
bool eoc_start_takes_duration( const activity_id &type );

/**
 * Builds the actor of @p type as u_assign_activity starts it, for the activities whose actor
 * needs nothing but an optional duration. Returns nullptr when @p type can't be started that way.
 * @param who the character that will do the activity
 * @param other_name the name of the dialogue's other talker, empty if there is none
 * @param duration the effect's "duration", if it has one
 */
std::unique_ptr<activity_actor> make_for_eoc( const activity_id &type, const Character &who,
        const std::string &other_name, const std::optional<time_duration> &duration );

} // namespace activity_actors

#endif // CATA_SRC_ACTIVITY_ACTOR_EOC_H
