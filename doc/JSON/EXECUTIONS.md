# Executions

An execution is a saved sequence of actions on two particular living creatures. It can
describe a stagger, a brief clash, or an interrupted takedown. Completion does not imply
death. One tick is one game turn (one second), regardless of participant speed.

The current implementation supports explicit EOC starts, bounded step graphs, native movement
and falling, damage/effects, and owned visible/audible narration. The broader design in
[the execution plans](../../Audit/plans/executions/README.md) includes later features;
those proposed schemas are not a list of currently supported JSON.

## Starting and cancelling

An activation EOC with a living creature as each talker can request a sequence:

```json
{
  "type": "effect_on_condition",
  "id": "EOC_START_STAGGER",
  "effect": { "start_execution": "execution_knife_stagger" }
}
```

Alpha (`u_`) becomes `actor`; beta (`npc_`) becomes `target`. Either can be an avatar,
NPC, or monster. They must be distinct and loaded. Missing talkers never fall back to
the avatar. A request reserves its controlled participants immediately, then starts at
the next safe action/turn boundary. It does not run graph actions inside the combat
callback. A target already killed by the triggering hit is refused.

`start_execution` also accepts the usual string variable object and optional
`true_eocs`/`false_eocs` arrays. The requesting dialogue receives `execution_instance`
as a decimal **string** and `reason`. Success means **queued**, not completed. Save
the handle before subsequent requests overwrite it. Refusals return handle `"0"`;
reasons include `invalid_definition`, `missing_participant`, `invalid_participant`,
`participant_unloaded`, `out_of_range`, `participant_busy`, `busy`,
`restrained`, `weapon_lost`, `invalid_weapon`, `invalid_parameter`, and `instance_budget`.

```json
{ "cancel_execution": { "context_val": "execution_instance" } }
```

Cancellation is requested immediately and settled at a safe boundary. Its dialogue
`reason` is `cancelled` or `not_found`. The avatar can cancel its execution activity
through the normal activity cancellation UI. `u_in_execution` and `npc_in_execution`
test participation, including free roles and pending requests.

For weapon/style content, use existing martial-art/technique EOCs and their resolved
hit context. The optional `executions_demo` mod adds a combat knife whose technique
requests the sample after a nonfatal critical hit against a humanoid. Existing core
weapons acquire no new automatic trigger. There is no universal indexed execution
trigger table yet.

## Definition

```json
{
  "type": "execution",
  "id": "example_clash",
  "name": "Brief clash",
  "revision": 1,
  "required_roles": [ "actor", "target" ],
  "control": { "actor": "free", "target": "scripted" },
  "max_ticks": 30,
  "max_step_entries": 128,
  "max_distance": 12,
  "require_weapon": false,
  "interruptions": { "external_damage": "interrupt", "displacement": "interrupt" },
  "locals": { "counter": 0, "tone": "grim" },
  "on_start": [],
  "on_tick": [],
  "on_complete": [],
  "on_interrupt": [],
  "on_end": [],
  "start": "pause",
  "steps": {
    "pause": { "next": { "step": "speak", "after_ticks": [ 1, 3 ] } },
    "speak": {
      "action": { "op": "message", "text": "<target_name> stares at <actor_name>." },
      "end": "survived"
    }
  }
}
```

`name`, `start`, and `steps` are required on a new definition. Other values above show
defaults. `copy-from` is supported; providing `steps` replaces the entire inherited
graph. Bump `revision` when changing a graph that could invalidate saved cursors.
Unknown step references and unsupported action/transition forms are load errors.

Both roles are required. `free` leaves normal actions available; `held` suppresses
voluntary actions; `scripted` also permits typed sequence movement. The world, other
creatures, fields, effects, and damage continue. Controlled creatures do not bank
positive move points for a burst of actions afterward. A character with an existing
activity, mount/vehicle or restraint cannot be claimed for control. Claims cannot
overlap other controlled roles; free roles can participate in multiple scenes.

Damage and displacement policies accept `interrupt` or `continue`. External damage
means any applied HP damage outside this sequence's own action, including bleeding
and environmental damage. `continue` does not prevent damage or death. Displacement
checks concern controlled roles; a free attacker may move within `max_distance`.
Missing/dead/unloaded participants, lost required weapons, incompatible saves,
player cancellation, skipped time, and budgets can still interrupt.

`require_weapon` binds the actor's wielded item by UID, not by item type. In melee
context, `weapon_uid` and `weapon` preserve the weapon that actually triggered the
request. An identical replacement does not satisfy the binding. Without that context,
the current wielded item is captured. Changing selected style does not itself cancel.

## Steps and transitions

A step has an optional `action`, optional `repeat`, and exactly one of `next` or `end`.
`end` is an authored outcome string, not a command to kill or knock out a creature.

`next` accepts the following forms, nested up to eight levels:

```json
"another_step"
```

```json
{ "step": "another_step", "after_ticks": [ 1, 3 ] }
```

```json
{ "if": { "npc_has_effect": "stunned" }, "then": "stunned", "else": "standing" }
```

```json
{ "chance": 0.5, "then": "tumble", "else": "standing" }
```

```json
{ "weighted": [ { "weight": 3, "to": "standing" }, { "weight": 1, "to": "tumble" } ] }
```

```json
{ "result": "push", "cases": { "moved": "follow_up", "blocked": "stop" }, "otherwise": "stop" }
```

Delays and movement ranges are inclusive; chances are finite numbers from 0 to 1.
Weighted transitions allow zero-weight entries but require a positive total. Delays
attach to a `step` transition, including leaves inside other transition forms.
Conditions use existing dialogue conditions with the bound participants.

```json
"repeat": { "count": 2, "interval_ticks": 1, "first_after_ticks": 1 }
```

This performs the action twice, first at T+1 and then T+2. Without `repeat`, ordinary
actions run on entry. Movement has its own per-tile cursor and cannot use `repeat`.
`on_start` runs once; `on_tick` runs once each subsequent active game turn;
`on_complete` or `on_interrupt` runs on settlement, followed by `on_end` once.
All callback arrays and `eoc` actions accept activation EOC IDs or inline EOCs.
Nested starts wait for a later drain; callbacks cannot recursively pump the runner.

## Actions

All actions accept `role` (`target` by default, or `actor`) and optional `store_result`.

| `op` | Members and behavior |
| --- | --- |
| `message` | Exactly one of translated `text` or `pool`. Optional `entry`, `append_to`, `separator`, and perception fields described below. |
| `move` | `direction`: `backward` (default) or `forward`; `tiles`: integer or range (default 1); `ticks_per_tile`: 1; `first_step_after_ticks`: 1; `stop_before_unsupported`: true; `collision`: `stop`. Requires scripted control of this role. |
| `eoc` | Nonempty `eocs` array. Existing EOCs can manipulate terrain, furniture, items, and other world state. These effects retain their existing limitations and narration rules. |
| `effect` | Required `effect` ID; `duration_ticks`: 1; `intensity`: 1. Applies the existing native effect. |
| `damage` | Required `damage_type`; `amount`: 0; `bodypart`: `torso`. Uses ordinary damage handling and armor; reported actual HP change can differ from the amount. |

A message pool looks like:

```json
"pool": {
  "avoid_immediate_repeat": true,
  "entries": [
    { "weight": 1, "text": "<target_name> coughs up blood." },
    { "weight": 2, "text": "<target_name>'s jaw moves silently." }
  ]
}
```

`<actor_name>` and `<target_name>` expand to names after translation (including the
avatar's name, so third-person verbs remain grammatical). Unseen other participants
are called “someone.” Unobserved messages still consume the same random choices but
are not displayed or retained in the observed transcript.
EOCs authoring their own messages must handle their own visibility and privacy.

### Named entries and append

```json
"open": {
  "action": { "op": "message", "entry": "wounded", "text": "<target_name> staggers." },
  "next": { "step": "continue", "after_ticks": 1 }
},
"continue": {
  "repeat": { "count": 2, "interval_ticks": 1, "first_after_ticks": 0 },
  "action": {
    "op": "message",
    "entry": "continuation",
    "append_to": "wounded",
    "text": "<target_name> struggles to stay upright."
  },
  "end": "survived"
}
```

`entry` names this contribution; `append_to` joins the group currently bound to a
named entry **in the same instance**. Every append has its own immutable ID and game
timestamp. Repeats select and append once per occurrence. Both names become aliases
of the resulting group. A new `entry` without `append_to` begins a new group and
rebinds that name. Entry names must be nonempty, at most 64 bytes; at most 64 names are
supported per definition. Append targets must be declared by an `entry` somewhere
in the definition, including the same action for a self-appending repeat.

The sidebar joins adjacent segments from the same group. Other intervening messages
keep their position: an append after unrelated combat appears at its actual later
time, rather than being moved back before that combat. Full message history retains
each segment and timestamp. Ordinary message coalescing/cooldowns remain independent;
two scenes cannot append into each other's entries, even when their names/text match.

`separator` is a space by default; an empty string or newline is also accepted. It is
used only when joining adjacent segments for compact presentation. Chronological
history keeps each segment's original text.

An anchor never observed, cleared from history, or fully removed by the message limit
creates a fresh visible group for the current segment. Earlier hidden/pruned text is
never reconstructed. Aliases then point at the fresh group, allowing later appends.
Group bindings and observed segment metadata save with the execution; message IDs and
segments save with player messages. Legacy saves without these fields still load.

### Seeing, hearing, and presentation

| `audience` | Required fields | Observation |
| --- | --- | --- |
| `visible` (default), `visible_action` | None | The action role is visible or is the avatar. |
| `audible` | `volume`: integer 1–200 | The avatar can hear the action role at that volume, using native distance, weather, and hearing/deafness rules. `text`/the selected pool line must describe audible information. |
| `visible_or_audible` | `volume`: integer 1–200; translated `audible_text` | Use the selected visual text when visible; otherwise use only `audible_text` if heard. Missing hearing fallback is a load error. |

```json
{
  "op": "message",
  "audience": "visible_or_audible",
  "volume": 12,
  "text": "<target_name> stares at <actor_name>, wide-eyed.",
  "audible_text": "You hear <target_name> rasping weakly."
}
```

Hearing alone does not reveal a participant's identity: unseen name placeholders
still become “someone.” Explicit names/actions written literally in authored audible
text are the author's responsibility. A deaf observer can still see a visual beat;
an observer who can neither see nor hear receives no entry. Narration itself does not
emit an AI-attracting sound or wake sleepers; use existing sound EOCs for physical noise.

The interface option **Show execution narration** (`EXECUTION_NARRATION`, default true)
hides these entries without changing movement, damage, effects, selected sequence
choices, or timing. Perception, message identity generation, and compact rendering do
not consume gameplay RNG. Hidden narration does not create anchors or observed segments.
The same entry/perception fields are available inside movement's `narration.on_moved`.

Movement uses the captured initial horizontal direction between participants. It
checks loaded bounds, occupancy, walls, solid diagonal corners, restraints, and
support at every tile. It does not tunnel, swap occupants, or push a third creature.
It rejects deep-water entry pending a swimming/drowning action contract. It invokes
native position updates, traps, gravity, fields, and death handling. With
`stop_before_unsupported: false`, eligible grounded creatures can enter open air and
fall normally; flight/levitation, supporting vehicles, or a blocked landing can refuse
the step. A fall is not guaranteed to kill. Native fall damage is attributed to the
sequence actor when it otherwise has no source.

Movement can include:

```json
"narration": {
  "on_moved": { "text": "<target_name> tumbles backward!", "audience": "visible_action" }
}
```

This is emitted only after an accepted move and before gravity. The condition
`{ "execution_can_fall": { "role": "target", "direction": "backward" } }`
checks the next captured-direction tile for the active instance supplied by context.
This milestone supports only the backward probe; an action revalidates the tile.

## Context and results

Caller context is copied as a typed snapshot. `locals` declare up to 64 string or
numeric values, initialized from matching caller values or their defaults. Only these
declared locals are read back from callback EOCs. A wrong caller type refuses the
start. Context fields beginning `execution_` are reserved and cannot be locals.

Lifecycle/action EOCs receive the real actor/target and these runtime values:

- Strings: `execution_instance`, `execution_definition`, `execution_step`,
  `execution_state`, `execution_reason`, `execution_outcome`.
- Numbers: `execution_elapsed`, `execution_entry`, `execution_occurrence`,
  `execution_target_dead` (actual death, independent of the outcome label).
- Positions: `execution_actor_origin`, `execution_target_origin`,
  `execution_actor_position`, `execution_target_position`, when available.
- Bound weapon: `execution_weapon_uid` (string) and `execution_weapon_type`, when present.

Each action sets `execution_result`; `store_result: "push"` also sets
`execution_result_push` and records a status for result transitions. Movement supplies
`execution_moved_push`, `execution_requested_push`, `execution_fell`, and
`execution_position`. Statuses include `moved`, `blocked`, `unsupported`, `displaced`,
and `unavailable`. Message selection reports `selected`; effects/damage report
`applied` (or `immune` for an unapplied effect); EOC actions report `dispatched`. Damage also supplies `execution_damage`
and signed `execution_hp_change`.

Cleanup callbacks may have one or both talkers absent. Check `has_alpha`/`has_beta`
before actor-specific effects. They are never rebound to the avatar or a replacement.

## Persistence and limits

Instances save participant IDs, dimension, weapon binding, typed locals, graph cursor,
RNG state, sampled choices, next deadline, movement progress, and observed transcript.
Saving/loading at an ordinary game save boundary resumes without replaying starts,
messages, moves, or rolls. Avatar control uses a saved, cancellable activity. Monster
IDs survive saves, polymorphs, and offscreen storage; cloning gets a fresh identity.
Required participants must remain in the active area. Dimension travel cancels scenes;
skipped game turns interrupt instead of replaying offscreen actions. Saving from
inside a partially committed execution action is rejected.

Limits: 64 active instances, 128 steps per definition, 256 total step entries at most,
600 ticks per scene, 60 tiles maximum participant distance, 16 tiles per move, 32
repeats, 600 ticks per delay/interval, 64 entries per message/weighted pool, total
weight 1,000,000, and 256 context values. A pump yields after 32 immediate graph
operations. Scene time/entry budgets eventually settle cycles. Terminal diagnostics
retain at most 64 scenes and are not saved; observed transcripts and timestamped
narration each retain 128 segments. Native message-history limits still apply.

The reference content is [knife_stagger.json](../../data/json/executions/knife_stagger.json).
It rolls 1–4 backward tiles, stops before a ledge, tries a 50% native tumble, or emits
two wounded beats. It can end with survival. Fatal-hit reservation, explicit knockout
physiology, indexed automatic triggers, offscreen simulation, a dedicated transcript
inspector UI, and additional typed physical actions remain later milestones.
