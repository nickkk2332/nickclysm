<!-- START doctoc generated TOC please keep comment here to allow auto update -->
<!-- DON'T EDIT THIS SECTION, INSTEAD RE-RUN doctoc TO UPDATE -->
*Contents*

- [Activities](#activities)
  - [Adding new activities](#adding-new-activities)
  - [JSON Properties](#json-properties)
  - [Termination](#termination)
  - [Notes](#notes)
    - [`activity_actor::start`](#activity_actorstart)
    - [`activity_actor::do_turn`](#activity_actordo_turn)
    - [`activity_actor::finish`](#activity_actorfinish)

<!-- END doctoc generated TOC please keep comment here to allow auto update -->

# Activities

Activities are long-term actions, that can be interrupted and (optionally)
continued. This allows the avatar or an NPC to react to events (like
monsters appearing, getting hurt) while doing something that takes more
than just one turn.

## Adding new activities

1. `player_activities.json` Define properties that apply to all instances
of the activity in question.

2. `activity_actor_definitions.h` Create a new subclass of `activity_actor`
(e.g., move_items_activity_actor) to store state and handle turns of the
new activity.

3. `activity_actor.cpp` Define the `start`, `do_turn`, and `finish`
functions needed for the new actor as well as the required serialization
functions. Don't forget to add the deserialization function of your new
activity actor to the `deserialize_functions` map towards the bottom of
`activity_actor.cpp`. Define the `canceled` function if the activity modifies
some complex state that should be restored upon cancellation / interruption.

4. If this activity is resumable, `override`
`activity_actor::can_resume_with_internal`

5. Construct your activity actor and then pass it to the constructor for
`player_activity`. The newly constructed activity can then be assigned
to the character and started using `Character::assign_activity`.

6. If the actor needs nothing but an optional duration, add it to
`eoc_starters` next to `deserialize_functions` so `u_assign_activity` can
start it from an EOC.

7. If it works on a target (an item, a location, a recipe), it can be started
from an EOC through `target_starters` in `activity_actor_eoc.cpp`: add an entry
with the fields it takes ("target_item", "target_loc", "recipe",
"butcher_type") and a function that starts it. That function must ask the
player nothing, for the avatar or an NPC, and return why it can't start
instead: check before starting whatever the actor's `start()` or first
`do_turn()` would refuse or ask about (look for `query_yn`, `uilist`,
`popup`, `get_avatar()` and `is_avatar()` in them and in what they call),
and leave things as they were when it refuses. The refusal reaches the
effect's `false_eocs` as the context value `reason`. Activities without an
entry can't be given a target: the effect fails to load, naming the
activity. Document the activity in the table of activities with a target
under `u_assign_activity` in `doc/JSON/EFFECT_ON_CONDITION.md`.

## JSON Properties

* verb: A descriptive term to describe the activity to be used in the
query to stop the activity, and strings that describe it, for example:
`"verb": "mining"` or
`"verb": { "ctxt": "instrument", "str": "playing" }`.

* activity_level: Activity level of the activity, harder activities consume more calories over time. Valid values are, from easiest to most demanding of the body: `NO_EXERCISE`, `LIGHT_EXERCISE`, `MODERATE_EXERCISE`, `BRISK_EXERCISE`, `ACTIVE_EXERCISE`, `EXTRA_EXERCISE`, `EXPLOSIVE_EXERCISE`.

* interruptable (true): Can this be interrupted. If false, then popups related
to e.g. pain or seeing monsters will be suppressed.

* interruptable_with_kb (true): Can this be interrupted by a key press.

* can_resume (true): If true, the activity can be resumed after an interruption,
letting you continue from where you left off rather than from scratch.

* based_on: Can be 'time', 'speed', or 'neither'.

    * time: The amount that `player_activity::moves_left` is
    decremented by is independent of the character's speed.

    * speed: `player_activity::moves_left` may be decremented faster
    or slower, depending on the character's speed.

    * neither: `moves_left` will not be decremented. Thus you must
    define a do_turn function; otherwise, the activity will never end!

* rooted (false): If true, then during the activity, recoil is reduced,
and plant mutants sink their roots into the ground. Should be true if the
activity lasts longer than a few minutes, and can always be accomplished
without moving your feet.

* refuel_fires (false): If true, the character will automatically refuel
fires during the long activity.

* auto_needs (false): If true, the character will automatically eat and
drink from specific auto_consume zones during long activities.

* multi_activity (false): This activity will repeat until it cannot do
any more work, used for NPC and avatar zone activities.

* fetch_items_to_zone (true): For multi-activities, whether the character
fetches the tools and components the job needs from nearby zones.

* mute_npc_completion (false): If true, no message is shown when an NPC
finishes the activity.

* ignored_distractions: A list of distractions that don't interrupt the
activity, for example `[ "hunger", "thirst" ]`. Valid values: `noise`, `pain`,
`attacked`, `hostile_spotted_far`, `hostile_spotted_near`, `talked_to`,
`asthma`, `motion_alarm`, `weather_change`, `portal_storm_popup`, `eoc`,
`dangerous_field`, `hunger`, `thirst`, `temperature`, `mutation`, `oxygen`,
`withdrawal`, `craft_step_complete`.

* start_eoc: an EOC that is run when this activity starts or resumes.

* completion_eoc: an EOC that is run when this activity completes, after it
has ended, so it sees the result.

* cancel_eoc: an EOC that is run when this activity is canceled or
interrupted. A cancel through `Character::cancel_activity()` runs it after
the activity has been cleared, but code that calls
`player_activity::canceled()` directly (an NPC putting its task aside, a
stamina break, unfolding a vehicle) runs it while the activity is still the
character's current one, so don't rely on either.

* do_turn_eoc: an EOC that is run when this activity performs a turn.

Each of the four is an EOC id or an inline EOC object. They run with the
character doing the activity as the alpha talker (`u_`), and with the item
the activity works on, if it has one and it still exists, as the beta talker
(`npc_`). That is the activity's first target, for example the in-progress
craft for `ACT_CRAFT` or the book for `ACT_READ`; for `ACT_DISASSEMBLE` it is
the in-progress disassembly, not the items still queued. An actor chooses it
with `activity_actor::hook_target()`. Context values:

| Context value | Value |
| --- | --- |
| `_activity` | the activity id |
| `_moves_total` | the moves the activity takes in all |
| `_moves_left` | the moves still left; for completion_eoc and cancel_eoc, what was left when it ended |
| `_resume` | start_eoc only: 1 when the activity resumes, 0 when it starts |

An activity completes when it runs out of moves, and also when its actor ends
it after finishing its work: a craft that is done, each item disassembled, a
drop that dropped everything, and a zone job (`ACT_MULTIPLE_*`, `ACT_MOVE_LOOT`,
`ACT_UNLOAD_LOOT`, ...) that ran out of work. completion_eoc runs once per
completion and cancel_eoc once per cancel, never both. A zone job that gives
up for another reason (it is too dark, a fetch failed, a work spot needs items
or tools that can't be found) counts as a cancel. A trip to another work spot
is not an end. The
`character_finished_activity` event is sent right after completion_eoc or
cancel_eoc, with `canceled` false for a completion, so an event EOC sees what
they did.

```json
{
  "type": "activity_type",
  "id": "ACT_CRAFT",
  "copy-from": "ACT_CRAFT",
  "start_eoc": {
    "id": "EOC_MY_MOD_CRAFT_START",
    "condition": { "math": [ "_resume == 0" ] },
    "effect": [ { "math": [ "u_crafts_started++" ] } ]
  },
  "cancel_eoc": {
    "id": "EOC_MY_MOD_CRAFT_ABANDONED",
    "effect": [ { "math": [ "u_crafts_abandoned++" ] } ]
  }
}
```

Activity types support `copy-from`, `abstract`, `extend` and `delete`, so a
mod can change a vanilla activity without redefining it. For example, to run
an EOC on every turn of reading:

```json
{
  "type": "activity_type",
  "id": "ACT_READ",
  "copy-from": "ACT_READ",
  "do_turn_eoc": { "id": "EOC_MY_MOD_READ_TICK", "effect": [ { "math": [ "u_pages_read++" ] } ] }
}
```

## Termination

There are several ways an activity can be ended:

1. Call `player_activity::set_to_null()`

    This can be called if it finished early or something went wrong,
    such as corrupt data, disappearing items, etc. The activity will
    not be put into the backlog.

2. `moves_left` <= 0

    Once this condition is true, the finish function, if there is one,
    is called. The finish function should call `set_to_null()`. If
    there isn't a finish function, `set_to_null()` will be called
    for you (from activity_actor::do_turn).

3. `Character::cancel_activity`

    Canceling an activity prevents the `activity_actor::finish`
    function from running, and the activity does therefore not yield a
    result. Instead, `activity_actor::canceled` is called. If the activity is
    resumable, a copy of it is written to `Character::backlog`.

An actor that finishes its work some other way than running out of moves
(and then calls `set_to_null()` or `Character::cancel_activity()`) reports
the completion: it takes the hooks with `player_activity::take_end_hooks()`
before ending the activity, then calls `run( who, false )` on them once the
result exists. Otherwise the end looks like a cancel (with
`cancel_activity()`) or isn't reported at all (with `set_to_null()`).

## Notes

While the character performs the activity,
`activity_actor::do_turn` is called on each turn. Depending on the
JSON properties, this will do some stuff. It will also call the do_turn
function, and if `moves_left` is non-positive, the finish function.

Some activities (like playing music on a mp3 player) don't have an end
result but instead do something each turn (playing music on the mp3
player decreases its batteries and gives a morale bonus).

If the activity needs any information during its execution or when
it's finished (like *where* to do something, *which* item to use to do
something, ...), simply add data members to your activity actor as well
as serialization functions for the data so that the activity can persist
across save/load cycles.

Be careful when storing coordinates as the activity may be carried out
by NPCS. If it is, the coordinates must be absolute, not local as local
coordinates are based on the avatar position.

If in the `activity_actor::finish`, `player_activity::set_to_null()` is not called, the `activity_actor::finish` will be called again, or if you add more moves to `activity_actor::moves_left`, the `activity_actor::do_turn` function will also be called.

### `activity_actor::start`

This function is called exactly once when the activity
is assigned to a character. It is useful for setting
`player_activity::moves_left`/`player_activity::moves_total` in the case
of an activity based on time or speed.

### `activity_actor::do_turn`

To prevent an infinite loop, ensure that one of the following is
satisfied:

- The `based_on` JSON property is `speed` or `time`

- The `player_activity::moves_left` is decreased in `do_turn`

- The activity is stopped in `do_turn` (see 'Termination' above)

For example, `move_items_activity_actor::do_turn` will either move as
many items as possible given the character's current moves or, if there
are no target items remaining, terminate the activity.

### `activity_actor::finish`

This function is called when the activity has been completed
(`moves_left` <= 0). It must call `player_activity::set_to_null()` or
assign a new activity. One should not call `Character::cancel_activity`
for the finished activity, as this could make a copy of the activity in
`Character::backlog`, which allows resuming an already finished activity.
