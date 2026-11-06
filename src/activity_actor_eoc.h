#pragma once
#ifndef CATA_SRC_ACTIVITY_ACTOR_EOC_H
#define CATA_SRC_ACTIVITY_ACTOR_EOC_H

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "coordinates.h"
#include "item_location.h"
#include "type_id.h"

class Character;
class activity_actor;
class time_duration;

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
 * Whether the actor of @p type, started from an EOC, works only for the avatar: it asks the
 * player through menus. An NPC can't be given it.
 */
bool eoc_start_needs_avatar( const activity_id &type );

/**
 * Builds the actor of @p type as u_assign_activity starts it, for the activities whose actor
 * needs nothing but an optional duration. Returns nullptr when @p type can't be started that way,
 * or can't be started for @p who (see eoc_start_needs_avatar).
 * @param who the character that will do the activity
 * @param other_name the name of the dialogue's other talker, empty if there is none
 * @param duration the effect's "duration", if it has one
 */
std::unique_ptr<activity_actor> make_for_eoc( const activity_id &type, const Character &who,
        std::string_view other_name, const std::optional<time_duration> &duration );

/** The targets u_assign_activity gives an activity (see doc/JSON/EFFECT_ON_CONDITION.md). */
struct eoc_activity_target {
    /** "target_item": the item of the other talker */
    item_location item;
    /** "target_loc" */
    std::optional<tripoint_abs_ms> loc;
    /** "recipe" */
    std::optional<recipe_id> recipe;
    /** "butcher_type" */
    std::optional<std::string> butcher_type;
};

/** Which target fields a u_assign_activity effect has. */
struct eoc_target_fields {
    bool item = false;
    bool loc = false;
    bool recipe = false;
    bool butcher_type = false;

    bool any() const {
        return item || loc || recipe || butcher_type;
    }
};

/** Whether u_assign_activity can start @p type with a target. */
bool takes_eoc_target( const activity_id &type );

/**
 * Why u_assign_activity can't start @p type with the target fields @p given: a field the
 * activity needs is missing, or one it doesn't take is there (any field, for an activity that
 * takes no target). nullopt if it can.
 */
std::optional<std::string> eoc_target_fields_error( const activity_id &type,
        const eoc_target_fields &given );

/** Whether @p name is a "butcher_type" u_assign_activity knows ("quick", "full"...). */
bool is_eoc_butcher_type( const std::string &name );

/**
 * Starts @p type for @p who with @p target, as u_assign_activity does. Nothing asks the player,
 * whether @p who is the avatar or an NPC (@p type must take a target).
 * @returns why it didn't start (nothing was done then), or nullopt once it started
 */
std::optional<std::string> start_with_target( const activity_id &type, Character &who,
        const eoc_activity_target &target );

} // namespace activity_actors

#endif // CATA_SRC_ACTIVITY_ACTOR_EOC_H
