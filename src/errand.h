#pragma once
#ifndef CATA_SRC_ERRAND_H
#define CATA_SRC_ERRAND_H

// Errands a character is sent on: a walk to a spot kept across turns (u_walk_to, a follower's
// "Move there!" order, u_pickup_item with "walk") and moving one item (u_pickup_item,
// u_wield_item, u_wear_item, u_drop_item).  See doc/JSON/EFFECT_ON_CONDITION.md.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "calendar.h"
#include "coordinates.h"
#include "global_vars.h"
#include "item_location.h"
#include "type_id.h"

class Character;
class JsonObject;
class JsonOut;
class avatar;

// what a walk is for
enum class walk_kind : int {
    // u_walk_to / npc_walk_to
    eoc,
    // a follower told "Move there!"
    order,
    // u_pickup_item with "walk": the item is taken on arrival
    pickup
};

// A walk a character is on, kept across turns and saved with it (Character::walk).  An NPC
// walks it as the goal goto_ordered_position; the avatar walks it by auto-move.
struct walk_order {
    walk_kind kind = walk_kind::eoc;
    tripoint_abs_ms target;
    // ends this close to target: square distance, on target's z level
    int range = 0;
    bool run = false;
    // a threat or a more urgent goal pauses the walk; false ends it instead
    bool interruptible = true;
    // may bash through what's in the way, as the follower order always did
    bool bash = false;
    time_point started = calendar::turn_zero;
    // keeps trying until then, even with no way there; unset: gives up once there's none
    std::optional<time_point> give_up_at;
    std::vector<effect_on_condition_id> true_eocs;
    std::vector<effect_on_condition_id> false_eocs;
    // context values of the EOC that sent the character, passed on to true_eocs and false_eocs
    global_variables::impl_t context;
    // walk_kind::pickup: the item, found again after a load by its uid (or its type) where
    // the walk ends, so a vanished item reads as gone without complaint
    item_location item; // NOLINT(cata-serialize): found again from item_uid and item_type
    int64_t item_uid = 0;
    itype_id item_type;
    bool allow_theft = true;
    // last turn the walker got anywhere; a walk stuck for errand::walk_patience is blocked
    time_point last_progress = calendar::turn_zero;
    // the walk switched the walker to running, and switches it back at the end
    bool switched_to_run = false;

    // not saved
    // the avatar's auto-move ends there; unset after a load, so the route is made again
    std::optional<tripoint_abs_ms> auto_move_end; // NOLINT(cata-serialize)
    // last turn the walk took a step; a gap means it was paused
    time_point last_step = calendar::turn_zero; // NOLINT(cata-serialize)
    // tells this walk from one that replaced it
    int serial = 0; // NOLINT(cata-serialize)
    // no way there for now: an NPC told to keep trying looks again at this turn
    time_point retry_at = calendar::turn_zero; // NOLINT(cata-serialize)
    // took a step during the current npc::move()
    bool stepped = false; // NOLINT(cata-serialize)

    void serialize( JsonOut &jsout ) const;
    void deserialize( const JsonObject &jo );
};

namespace errand
{
// why a walk or an item errand ended, passed to the EOCs as the context value "reason"
namespace reason
{
// walks
constexpr const char *arrived = "arrived";
constexpr const char *unreachable = "unreachable";
constexpr const char *blocked = "blocked";
constexpr const char *timeout = "timeout";
constexpr const char *interrupted = "interrupted";
constexpr const char *cancelled = "cancelled";
// item errands
constexpr const char *done = "done";
constexpr const char *gone = "gone";
constexpr const char *held = "held";
constexpr const char *not_carried = "not_carried";
constexpr const char *out_of_reach = "out_of_reach";
constexpr const char *too_heavy = "too_heavy";
constexpr const char *no_room = "no_room";
constexpr const char *liquid = "liquid";
constexpr const char *would_spill = "would_spill";
constexpr const char *owned = "owned";
constexpr const char *cannot_wield = "cannot_wield";
constexpr const char *cannot_wear = "cannot_wear";
constexpr const char *cannot_drop = "cannot_drop";
} // namespace reason

// turns without getting anywhere before a walk with no give_up_at ends as blocked
constexpr int walk_patience = 10;

// the follower "Move there!" order: walk exactly there, bashing allowed, no EOCs
walk_order move_order( const tripoint_abs_ms &where );

// whether who stands where w ends: within range of the target on its z level, or next to a
// target another creature stands on when the range is 0 (NPC-B14)
bool walk_arrived( const Character &who, const walk_order &w );

// A route for who from where it stands to a tile where w ends, or nullopt if there is none;
// empty when it stands there already.  ignore_creatures routes through creatures, so a walk
// can tell a spot it can't get to from one it's only kept from for now.
std::optional<std::vector<tripoint_bub_ms>> walk_route( const Character &who, const walk_order &w,
        bool ignore_creatures );
// whether p ends a walk for who: in range, standable, and nobody else stands there
bool walk_end_tile( const Character &who, const walk_order &w, const tripoint_bub_ms &p );

// Sends who on w.  Another walk is replaced, without its EOCs; a walk to the same target goes
// on, keeping its time limit and progress.  It ends at once when who is there already, can't
// move, or (without give_up_at) has no way there; the avatar also when no route starts its
// auto-move.
void start_walk( Character &who, walk_order w );
// sets who's walk with none of the checks start_walk makes
void set_walk( Character &who, const walk_order &w );
// ends who's walk and runs its EOCs: for a pickup walk that arrived, the pickup first
void end_walk( Character &who, const std::string &why );
// ends who's walk without its EOCs, undoing what it set up (auto-move, running)
void drop_walk( Character &who );
// u_cancel_walk: ends who's walk as cancelled, with its false_eocs unless silent; false if none
bool cancel_walk( Character &who, bool silent );
// the item a pickup walk is for, or an empty location once it's gone
item_location pickup_target( const walk_order &w );
// at the start of an NPC's move: ends a walk that arrived or ran out of time
void review_walk( Character &who );
// the NPC spent its move on something else: a walk that isn't interruptible ends
void walk_passed_over( Character &who );
// before the avatar's action: ends its walk once it arrived, ran out of time or its auto-move
// stopped; makes the route again after a load
void avatar_turn( avatar &u );

// item errands: the item's new place when done, else why not
struct item_result {
    std::string refusal;
    item_location loc;
    bool ok() const {
        return refusal.empty();
    }
};
item_result pickup( Character &who, const item_location &loc, bool allow_theft );
item_result wield( Character &who, const item_location &loc, bool allow_theft );
item_result wear( Character &who, const item_location &loc, bool allow_theft );
item_result drop( Character &who, const item_location &loc );
// runs eocs with who as u, loc (if any) as npc, the context values and "reason"
void run_eocs( Character &who, const item_location &loc,
               const std::vector<effect_on_condition_id> &eocs,
               const global_variables::impl_t &context, const std::string &why );
} // namespace errand

#endif // CATA_SRC_ERRAND_H
