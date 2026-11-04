# Executions

An execution is a saved sequence of actions on two particular living creatures. It can
describe a stagger, a brief clash, or an interrupted takedown. Completion does not imply
death. One tick is one game turn (one second), regardless of participant speed.

The current implementation supports EOC starts and automatic living-actor melee rules,
bounded step graphs, native movement and falling, knockouts, damage/effects, and owned visible/audible
narration. The broader design in
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

Existing martial-art/technique EOCs can still request scenes directly. Alternatively,
use the indexed rules below. The optional `executions_demo` mod supplies an automatic
rule for its demonstration knife and critical technique against humanoids. Existing
core weapons acquire no new automatic trigger.

## Automatic melee rules

```json
{
  "type": "execution_trigger",
  "id": "example_knife_stagger",
  "execution": "execution_knife_stagger",
  "event": "attack_resolved",
  "priority": 10,
  "weight": 1,
  "chance": 0.25,
  "cooldown": { "ticks": 30, "scope": "actor_target_pair" },
  "match": {
    "channels": [ "character_melee" ],
    "weapons": [ "knife_combat" ],
    "critical": true,
    "hp_loss": { "minimum": 1 },
    "damage": [ { "type": "stab", "minimum": 1 } ]
  },
  "condition": { "npc_bodytype": "human" }
}
```

`execution` is required. Defaults are `attack_resolved`, priority 0, weight 1, chance
1, no cooldown, both melee channels, and `hit: true`. `copy-from` is supported;
provided filter lists replace inherited lists, while omitted filters remain inherited.
Lists contain 1–64 nonempty identifiers. Multiple values within a list are alternatives;
different filters must all pass. An omitted filter imposes no restriction.

| Match member | Values |
| --- | --- |
| `channels` | `character_melee`, `monster_melee`, or `melee` for both |
| `source_kind`, `target_kind` | Lists of `avatar`, `npc`, `monster` |
| `source_monsters`, `target_monsters` | Monster type IDs |
| `weapons`, `weapon_categories`, `weapon_skills` | Original attack item IDs, categories, or skill IDs; unarmed character attacks use weapon `null` and skill `unarmed` |
| `styles`, `techniques` | Selected martial-art and technique IDs |
| `body_parts`, `weakpoints` | Actual body-part ID and stable weakpoint ID; weakpoints use IDs from the victim's definition, never translated names |
| `terrains`, `furniture` | IDs at the victim's tile when primary damage processing begins |
| `hit`, `critical` | Booleans; `critical` is absent for standard monster attacks, so neither critical filter matches those attacks |
| `hp_loss` | Inclusive `{ "minimum": 0, "maximum": 2147483647 }` range for primary applied HP loss |
| `damage` | Up to 64 distinct `{ "type": "stab", "minimum": 0, "maximum": 2147483647 }` ranges for reported post-armor damage |
| `fatal_state` | Only `alive`; already fatal/dead participants are refused |

Unknown match fields, unsupported channels/events, reversed ranges, and invalid limits
are load errors. Referenced content IDs are checked during consistency validation.
Weakpoint IDs are local to monster definitions and have no global ID registry.

The two damage concepts intentionally differ. `damage` is the native primary hit's
reported per-type result; `hp_loss` is its positive HP write, measured before damage-event
callbacks. Overkill, HP caps and damage-distribution policies can make them differ.
Neither includes separate mutation attacks, retaliation, extra damage, or healing from
callbacks. On a resolved hit, an unreported damage type has value zero. On a miss,
damage, HP loss, body part and critical facts are unavailable rather than invented zeros.

Rules run after native combat callbacks and death checks return. The participants must
still be alive and loaded; a removed or dead actor is never replaced. Character melee
captures the original item UID before defensive callbacks, then the chosen style and
technique before technique effects. It preserves those facts if a callback switches the
weapon. A scene with `require_weapon` refuses an item lost before queueing. Impact
positions and attack direction remain snapshots even if later callbacks move actors.
Scene control and movement start from their actual positions at queue time.

Selection considers indexed candidates by descending priority. Each matching, available,
off-cooldown rule checks the normal start requirements and its optional dialogue
`condition`, then rolls its chance once. Conditions may inspect the supplied hit facts;
keep them free of interactive queries. If any rules pass at a priority, one is chosen
by weight, and lower priorities are skipped. If all fail, selection tries the next tier.
At most one automatic scene is queued per primary contact. A final start refusal does
not fall through to another rule and does not charge a cooldown. Manual EOC requests
retain their existing behavior and ownership rules.

Cooldown scopes are `actor`, `target`, `actor_target_pair`, and `world`, with dimension
and rule ID included in every key. Durations are integer game turns, at most 31,536,000.
They start on accepted queueing and survive scene completion and save/load. Cancelling
an accepted scene does not refund its cooldown. Priority is bounded to ±1,000,000,
weights to 1–1,000,000, and chances to finite numbers from 0 to 1. Zero/one chances and
single-candidate weight choices need no selection RNG draw; accepted scene creation
still seeds its saved RNG normally.

Supported producers are ordinary character primary melee (avatar/NPC, including a
resolved reach contact) and standard monster primary melee. Nested/counter attacks,
mutation contacts, direct damage and projectile callbacks inside the wrapped damage
handlers, and execution-scripted attacks cannot become new automatic roots. Mech
delegation, rider redirects, monster specials that bypass standard melee, projectiles/throws, spells,
environmental damage, and pending-fatal scenes have no automatic producer in this
milestone. Projectile/fatal rule definitions are rejected. This is not a universal
combat-event/causality API.

For engine tooling, `execution_manager::last_trigger_report()` returns the latest
contact ID, indexed candidate count, selected rule/queued handle, and up to 64 candidate
reasons without rerolling or reevaluating. Reasons include `filtered`, `condition`,
`chance_failed`, `cooldown`, `participant_busy`, `weapon_lost`, `not_selected`, and
`duplicate_contact`. There is no inspector UI or EOC binding for this report yet.
There are at most 2,048 rules and 4,096 saved cooldown keys. The last 128 processed
contact IDs suppress duplicate in-session submissions; this bounded cache and the
latest report are not saved. The monotonic contact counter is saved. Loading scenes
does not reemit combat contacts or rerun trigger selection.

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
| `incapacitate` | Physiological knockout with a separate saved recovery timer. See the knockout contract below. |

### Knockouts and recovery

```json
{
  "op": "incapacitate",
  "state": "unconscious",
  "role": "target",
  "source": "actor",
  "duration_ticks": [ 10, 30 ],
  "respect_immunity": true,
  "wake_on_damage": true,
  "store_result": "knockout"
}
```

This applies the native `knocked_out` state. The supported state is `unconscious`;
duration defaults to 10 game turns and accepts an inclusive range from 1 to 3,600.
The source defaults to the opposite role and uses native effect attribution:
character identity/faction or monster faction. Immunity is always respected; requesting
`respect_immunity: false` is a load error. The scene samples its duration once per
action occurrence using its saved RNG.

Branch on `execution_result_knockout` (or your chosen result name):

| Result | Meaning |
| --- | --- |
| `applied` | The target is knocked out. |
| `immune` | An immunity flag, effect blocker, or inability to gain effects prevented it. |
| `unsupported` | Unsupported physiology or context: mounting/riding, being in a vehicle, actively grabbing another creature, or an operation activity. |
| `already_incapacitated` | A knockout, ordinary sleep, or anesthesia already applies. No timer or wake policy is replaced. |
| `unavailable` | The required target is missing, dead, fake, or a hallucination. |
| `interrupted` | A native callback undid the knockout before the action finished. |

Characters are supported. Monsters default to flesh animals without electronic or
headless flags and outside the zombie/nether species. `KNOCKOUT_VULNERABLE` explicitly
opts another monster physiology in. `KNOCKOUT_IMMUNE` takes precedence over that opt-in.
Both flags are registered for monster definitions and for character/effect flags.
These checks do not infer knockout immunity from inability to sleep.

Knockout suppresses subsequent voluntary movement, melee/ranged attacks, casting,
activities, blocking/dodging, and ordinary vision and hearing. It does not undo an atomic action already
in progress. Ordinary work is cancelled; an execution's own avatar activity remains
under its manager's control. Positive moves are not banked for recovery. Knocked-out
actors remain alive, occupy their tiles, and still take damage; native gravity and
environmental effects continue. There is no corpse, kill reward, or on-kill event from
the knockout itself.

`wake_on_damage: true` wakes on positive applied HP loss, before damage-event callbacks.
Healing, zero damage, sound, alarms, and ordinary `wake_up` do not clear the state.
False selects a deeper knockout that lasts until its deadline or explicit removal;
damage and death still work normally. Removing `knocked_out` through an EOC ends it.
Ordinary sleep and anesthesia retain their own independent rules.

The deadline and depth are saved with the native effect, independently of any scene.
Completing, cancelling, or clearing the execution does not wake the target. The timer
uses game time across save/load and unloading, so actor speed, repeated effect pumps,
effect pausing, and NPC catch-up do not extend or double-decrement it. Saving and
information screens remain available while the avatar is unconscious.

The read-only conditions `u_is_knocked_out` / `npc_is_knocked_out` test this exact state
and observe expiry immediately. They do not classify ordinary sleep as a knockout.
`u_can_be_knocked_out` / `npc_can_be_knocked_out` check current admission without rolls
or effects. Missing and non-creature roles return false. `execution_incapacitation_ticks`
records the sampled duration; `execution_unconscious` records the state at action return.

[knockout.json](../../data/json/executions/knockout.json) supplies a critical-knockout
scene and a takedown requiring a real character strength advantage of at least four.
The optional demo sap adds critical-head-hit and strength rules; ordinary weapons
acquire no automatic knockout rule.

### Message pools

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
two wounded beats. It can end with survival. Fatal-hit reservation,
additional automatic combat producers, offscreen scene simulation, a dedicated transcript
inspector UI, and additional typed physical actions remain later milestones.
