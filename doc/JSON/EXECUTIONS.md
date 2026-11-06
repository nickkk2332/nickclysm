# Executions

An execution is a saved sequence of actions on two particular creatures. It can
describe a stagger, a brief clash, or an interrupted takedown. Completion does not imply
death. One tick is one game turn (one second), regardless of participant speed.

The current implementation supports living EOC starts and automatic living/fatal melee rules,
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
callback. Manual starts refuse fatal/dead targets. Fatal-hit rules use the separate
commit-time reservation described below.

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
| `channels` | `character_melee`, `monster_melee`, `melee` for both original melee channels; explicit `projectile`, `thrown`, `spell`, `monster_special` |
| `source_kind`, `target_kind` | Lists of `avatar`, `npc`, `monster` |
| `source_monsters`, `target_monsters` | Monster type IDs |
| `weapons`, `weapon_categories`, `weapon_skills` | Original attack item IDs, categories, or skill IDs; unarmed character attacks use weapon `null` and skill `unarmed` |
| `ammunition` | Actual fired ammunition item IDs, when available; projectile rules only have ammunition facts |
| `spells` | Exact primary spell IDs on the `spell` channel |
| `monster_attacks` | Exact configured JSON melee/bite attack IDs on `monster_special` |
| `styles`, `techniques` | Selected martial-art and technique IDs |
| `body_parts`, `weakpoints` | Actual body-part ID and stable weakpoint ID; weakpoints use IDs from the victim's definition, never translated names |
| `terrains`, `furniture` | IDs at the victim's tile when primary damage processing begins |
| `hit`, `critical` | Booleans; `critical` is absent for standard monster attacks, so neither critical filter matches those attacks |
| `hp_loss` | Inclusive `{ "minimum": 0, "maximum": 2147483647 }` range for primary applied HP loss |
| `damage` | Up to 64 distinct `{ "type": "stab", "minimum": 0, "maximum": 2147483647 }` ranges for reported post-armor damage |
| `fatal_state` | `alive` for `attack_resolved`, `fatal` for `fatal_hit`; defaults to the event's state |

Unknown match fields, unsupported channels/events, reversed ranges, and invalid limits
are load errors. Referenced content IDs are checked during consistency validation.
Weakpoint IDs are local to monster definitions and have no global ID registry.

The two damage concepts intentionally differ. `damage` is the native primary hit's
reported per-type result; `hp_loss` is its positive HP write, measured before damage-event
callbacks. Overkill, HP caps and damage-distribution policies can make them differ.
Neither includes separate mutation attacks, retaliation, extra damage, or healing from
callbacks. On a resolved hit, an unreported damage type has value zero. On a miss,
damage, HP loss, body part and critical facts are unavailable rather than invented zeros.

`attack_resolved` rules run after native combat callbacks and death checks return. The participants must
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
resolved reach contact), standard monster primary melee, handheld
[projectiles](#projectile-rules), and explicit [thrown, spell and JSON monster-special
channels](#additional-trigger-channels). Supported primary HP commits can also use
fatal rules. Nested/counter attacks, mutation contacts, secondary projectiles and
execution-scripted attacks cannot become new automatic roots. Mech delegation,
rider redirects, turrets, arbitrary built-in monster specials and environmental
damage still need their own adapters and participant policies.

For engine tooling, `execution_manager::last_trigger_report()` returns the latest
contact ID, indexed candidate count, selected rule/queued handle, and up to 64 candidate
reasons without rerolling or reevaluating. Reasons include `filtered`, `condition`,
`chance_failed`, `cooldown`, `participant_busy`, `weapon_lost`, `not_selected`, and
`duplicate_contact`, and `lower_priority_not_evaluated`. The [read-only inspector](#execution-inspector)
shows these recorded decisions; there is no EOC binding for reports.
There are at most 2,048 rules and 4,096 saved cooldown keys. The last 128 processed
contact IDs suppress duplicate in-session submissions; this bounded cache and the
latest 32 reports are not saved. The monotonic contact counter is saved. Loading scenes
does not reemit combat contacts or rerun trigger selection.

## Projectile rules

Use `"channels": [ "projectile" ]` with `event: "attack_resolved"` to match direct
nonfatal gun, bow and crossbow hits from a loaded avatar or NPC. Omitted channels and
`melee` retain their previous melee-only meanings. Use `event: "fatal_hit"` for
[commit-time fatal admission](#fatal-projectile-rules). Projectile `hit: false` rules
remain load errors, including when inherited through `copy-from`.

```json
{
  "type": "execution_trigger",
  "id": "example_projectile_reaction",
  "event": "attack_resolved",
  "execution": "execution_projectile_stagger",
  "chance": 0.1,
  "cooldown": { "ticks": 30, "scope": "actor_target_pair" },
  "match": {
    "channels": [ "projectile" ],
    "weapons": [ "glock_19" ],
    "ammunition": [ "9mm" ],
    "hp_loss": { "minimum": 1 }
  }
}
```

For living reactions, one native firing action offers **one contact per victim**,
using its first direct hit. A burst can hit several victims; pellets, later rounds and later body parts do
not replace that victim's first hit or roll additional chances. A first hit stopped
by armor can have zero HP loss even if a later pellet damages the victim. Add an
`hp_loss` minimum when damage is required. Dodges and misses produce no contact.
The producer retains at most 64 victim candidates per firing action, in native hit
order; additional victims receive normal damage without automatic selection.

Selection waits until the complete native firing action returns, including all
rounds, ammo use, recoil, ranged events and synchronous effects. No scene executes
between pellets or rounds. Participants must still be alive and loaded. A fatal
primary hit is excluded from living selection even if a callback rescues it; a
victim killed by a later round cannot start a living scene. An indexed fatal-stage
evaluation supersedes buffered living selection for that victim.
Future queued effects remain ordinary world events and can interrupt scenes normally.

Snapshots include the original firing item UID/type/categories, gun skill, actual
ammunition type when present, native critical flag, hit body part where available,
stable monster weakpoint ID, primary damage/HP loss and impact terrain/furniture.
Styles and techniques are absent. Context adds `ammunition` and `projectile_origin`
(the absolute native flight origin); `weapon_skill` is the gun's ranged skill.
The horizontal backward direction uses that flight origin, preserving the impact
axis even when a callback moves the shooter. A scene starts at participants' actual
current positions and still enforces its own distance, weapon and control requirements.

Eligibility requires the actual wielded gun and main reality map. Handheld guns,
bows and crossbows use the same producer. Mounted/delegated firing, vehicle turrets,
underbarrel gunmod firing, direct generic projectile calls, thrown items, magical
projectiles, shrapnel, secondary bounce flights and rider-redirection contacts are
outside this adapter. A primary shot from bouncing ammunition can qualify; its
secondary flights cannot. Defensive reactions, damage/event callbacks and authored
scene attacks cannot create new automatic roots during firing or native flight.
Saves and scene pumping inside those boundaries are refused/deferred.

[Core stagger content](../../data/json/executions/projectile.json) takes one or two
checked backward steps and stops before unsupported terrain. The optional demo mod
adds a pistol and bow with rare stagger rules and qualifying critical-head knockout
rules. Production weapons gain no automatic rule. Existing native incapacity and
its immunities determine whether a knockout actually applies.

### Fatal projectile rules

Use `event: "fatal_hit"` and `"channels": [ "projectile" ]` with a definition that
sets `allow_fatal_target: true`. Player victims additionally require
`allow_avatar_fatal_target: true`. The shared [fatal lifecycle](#fatal-hit-rules)
still applies, including its 30-tick bound and living manual starts.

A victim has at most one fatal opportunity per native firing action. The first
lethal primary HP write uses its own round's ammunition, critical/body-part/
weakpoint and damage facts, even when earlier rounds caused nonfatal wounds.
Fatal selection happens before that write's callbacks/death checks. An accepted
owner remains unready until the entire firing action ends; no beat runs between
pellets, rounds, ranged events, ammo consumption or recoil.

Secondary effects are not primary commits. For example, a newly applied bleeding
effect can kill a 1-HP monster before the projectile's own HP write; that death
remains ordinary native death and does not acquire a projectile execution.

Later positive damage, including a following pellet, settles the injury promptly.
A reservation grants no protection from the remainder of a shotgun blast or burst.
Failed chances and refused/no-match admission retain ordinary native death.
Initially fatal hits rescued by callbacks do not reroll fatal selection in that
same firing action. Exceptional firing exits settle accepted reservations.

The ordinary producer exclusions still apply: secondary flights, shrapnel, magic,
throws, mounted/delegated/turret and underbarrel paths do not acquire fatal owners.
Saved accepted owners reuse normal scene deadlines, player activities and native
prevention, death events, kill credit and corpse placement.

[Core fatal projectile content](../../data/json/executions/fatal_projectile.json)
contains separate NPC/monster and player definitions. The demo pistol/bow add
explicit fatal rules. Ordinary production weapons acquire no rule.

## Additional trigger channels

These are explicit opt-ins. Omitted `channels` and the `melee` alias still select
only the original two melee producers. Each new channel supports
`attack_resolved` and `fatal_hit`, with the existing fatal-target/player opt-ins.
New-channel `hit: false` rules are rejected, including inherited rules.

| Channel | Native boundary and available extra facts |
| --- | --- |
| `thrown` | Loaded avatar/NPC `Character::throw_item`, including blind throws. `weapons` matches the original thrown item's type; `weapon_uid`, categories, `weapon_skill: "throw"` and actual `projectile_origin` are captured. The item can already be detached from inventory. Ammunition/style/technique are absent. Mounted throws are outside this adapter. |
| `spell` | A real loaded avatar, NPC or monster's primary `cast_all_effects` / `cast_spell_effect` attack effect. `spells` matches its ID; context adds `spell` and `spell_level`. Area attacks have independent victims. Ordinary magical projectile hits and direct split/percentage body-part damage use their actual native damage packets. No wielded weapon or style is inferred. |
| `monster_special` | JSON `melee_actor` special attacks, including its bite subclass. `monster_attacks` matches the actor's exact configured ID; context adds `monster_attack`. Native body-part selection, mitigation, HP loss, costs, post-hit effects and EOCs remain in place. No technique, weapon or critical fact is invented. |

For example:

```json
{
  "type": "execution_trigger",
  "id": "example_arcane_impact",
  "execution": "execution_projectile_stagger",
  "chance": 0.1,
  "match": { "channels": [ "spell" ], "spells": [ "my_arcane_bolt" ], "hp_loss": { "minimum": 1 } }
}
```

Living reactions use the first direct packet per victim per complete native
action/effect, within a 64-victim budget. Repeated spell projectiles, multiple limbs
and repeated special hits do not aggregate damage or multiply chances. A later
lethal packet has one fatal admission opportunity using its own facts; subsequent
harm can settle it immediately. Native extra spell effects and spells/attacks
created by callbacks are children, rather than independent automatic roots.
`EXTRA_EFFECTS_FIRST` retains that rule. A `WONDER` cast with only extra spells has
no eligible primary damage packet.

`require_weapon` still requires the bound original item to be wielded. Thrown
reactions normally leave it false; the runtime does not bind or reclaim the landed
item as an execution-owned object.

Graphs do not start inside partial throws, cast effects or JSON melee specials.
Fatal ownership is selected at the primary HP write, while readiness and living
selection wait until the native action/effect returns. Exceptions discard
unpublished living contacts and settle accepted fatal injuries. Save attempts
inside these boundaries are refused. Native resource payment outside the spell
effect wrapper keeps its existing caller ordering; this adapter does not promise
a universal post-resource spell-cast event.

Healing, damage over time, source-less/fake casts, direct unwrapped effect helper
calls, deferred environmental damage, arbitrary built-in monster attacks and
secondary native projectile flights are outside these channels. Missing facts
remain absent. `monster_attacks` IDs are local/configured strings and cannot all
be validated against a global registry; `spells` IDs are validated normally.

The optional [demo content](../../data/mods/EXECUTIONS_DEMO/additional_triggers.json)
provides a throwing knife, direct spell and a manually spawnable brawler, with rare
living/fatal rules for NPC/monster victims. Production content gains no rules.

## Execution inspector

Open **Debug → Info → Inspect executions**, or search for **Execution inspector**
in the debug console's Data actions. It lists active/recent scenes and the latest
32 trigger contacts. Detail views scroll and wrap long values.

Scenes show definition/revision, state, step/action, due tick, participant UIDs and
dimensions, loaded/visible states, positions, HP, moves, requested control and
active claims, movement progress, local results/context, fatal readiness/deadline,
pending cancellation, terminal reasons, owned narration and transcript.
The latest 128 graph choices show occurrence/step, range, selected value and
whether RNG was sampled. Fixed ranges are identified without consuming RNG.
Draw logs are saved with active scenes; legacy saves may omit them.

Contacts show captured facts, indexed candidate count, up to 64 recorded decisions,
priorities, weights, chance values and actual draws, selected rule/instance, and
the weighted draw when needed. Lower tiers skipped after selection are explicitly
marked as unevaluated. Reports are transient, bounded session diagnostics; cooldowns
and the contact counter keep their existing saved behavior.

Inspection never evaluates EOC conditions again, consumes randomness, starts or
advances scenes, changes cooldowns, or emits debug-use gameplay events. Opening
this inspector does not disable achievements. There are no live advance, preview,
cancel or edit controls. The view is privileged debug information about bound
participants; it is not an in-world player perception interface.

## Fatal-hit rules

```json
{
  "type": "execution_trigger",
  "id": "example_mortal_knife_wound",
  "event": "fatal_hit",
  "execution": "execution_fatal_knife_stagger",
  "chance": 0.2,
  "match": {
    "channels": [ "character_melee" ],
    "weapons": [ "knife_combat" ],
    "target_kind": [ "npc", "monster" ],
    "fatal_state": "fatal",
    "hp_loss": { "minimum": 1 }
  }
}
```

Fatal selection runs immediately after the supported primary HP write makes the
target fatally injured, before its damage-event callbacks and ordinary death checks.
Only the selected, accepted rule reserves death finalization. No match, a failed roll,
or refused admission takes the normal immediate-death path. The scene begins after
the outer attack returns, never inside the damage callback. One contact is arbitrated
once; fatal selection is not retried as a living hit afterward.

The referenced definition must set `allow_fatal_target: true`, control its target with
`held` or `scripted`, and have `max_ticks` at most 30. This enables fatal admission
only from supported native producers; it does not make a manual EOC start accept a
corpse or an arbitrary earlier fatal injury. The actor must be alive and both roles
must satisfy normal binding, distance, control, weapon and restraint checks.
NPC and monster victims use that opt-in. A player victim additionally requires
`allow_avatar_fatal_target: true`; existing definitions do not gain player targets.
Supported primary-melee and handheld-projectile producers both support this separate opt-in.

At this boundary, filters see the original weapon/style/technique, critical fact
(character melee only), resolved body part and weakpoint, reported damage types,
primary HP loss and impact location. These are commit-time facts, before later
callbacks can heal or add damage. `attack_event` is `fatal_hit`, `attack_fatal` is 1,
and `killed` is 0. Selection conditions run before a reservation exists; use those
facts rather than requiring `npc_death_pending` during admission.

The injury stays real: fatal health is not raised to one, and a finalized actor is
never revived for narration. A reservation retains occupancy and explicitly permitted
scene movement while excluding ordinary AI, targeting ranges and voluntary actions,
including scripted combat dispatch and evasion. Native passive turn processing,
damage-over-time, inventory processing and fields continue once per game turn. The
manager supplies the NPC/monster passive turn; the avatar stays on its ordinary
world-turn path to avoid duplicate processing. The actor gains no bank of missed actions.

Checked flat movement can span multiple beats. A supported owned fall keeps the
reservation through native landing, then ends the scene and settles death there.
Other displacement/physics, direct death calls, positive incoming damage requests,
removal, transformation, cancellation, timeout, unload and world transitions settle
promptly. The damage-request check runs before the raw-fatal early return (and before
armor when entering `deal_damage`); it cannot be disabled by `external_damage: continue`.
Projectile evasion is suppressed. Shrapnel can still contact a reserved victim, and
shockwaves settle defeated participants before iterating ordinary living actors.
An arbitrary EOC teleport is not a checked fatal movement action.

Settlement releases the reservation before native death/prevention processing.
Corpses, drops, kill rewards, death functions and faction consequences run through
the native finalizer once. Genuine health restoration can rescue the victim, as can
normal NPC or avatar death prevention. The original retained source is validated; a replacement
avatar/body is never credited. If that identity is unavailable, no replacement source
is invented. Rescue clears the captured killer pointer. Deferred martial-art on-kill
hooks use the original style/contact after actual finalization, once, while that
character source remains alive. Post-hit hooks see `killed: 0` and `death_pending: 1`
while the victim is reserved.

The actual fatal outcome is `killed`, `rescued`, or `unavailable` in the scene's
`outcome` and `execution_fatal_outcome`. The graph's requested end label is retained
as `execution_authored_outcome`. `execution_target_dead` reflects finalization;
`death_pending` reflects the target's current reservation. The read-only conditions
`u_death_pending` / `npc_death_pending` also query that state and return false for
missing/non-creature talkers. They include an opted-in avatar reservation.

Reservations, absolute deadlines and deferred kill context are saved with their
owners. Load installs them before actor/map processing, then validates actors at the
first safe pump without rerolling or replaying completed beats. Missing definitions,
incompatible content and expired deadlines settle the injury. Saves inside an attack
or an in-progress scene action are refused. Replacing a session for load discards its
old runtime without adding death side effects to the checkpoint being restored.

[The core fatal knife scene](../../data/json/executions/fatal_knife.json) reuses the
stagger/ledge/message graph. The optional demo mod adds a separate knife with rare
fatal rules; production weapons acquire no automatic rules.

### Player fatal scenes

Set both `allow_fatal_target` and `allow_avatar_fatal_target` on a definition and
match `"target_kind": [ "avatar" ]` in its `fatal_hit` rule. Manual starts still
refuse fatal targets. Existing distance, restraint, mounted/vehicle and graph bounds
apply. The game must still be playing, outside deathcam or a completed death.

The accepted injury suppresses player actions immediately. At the safe start boundary,
prior work and auto-move are canceled and the execution owns the avatar activity.
The view follows the actual body through checked movement and native landing. The
avatar retains ordinary sight/hearing for its scene; blindness, deafness and existing
unconsciousness still apply. Body switching is refused during a pending or finalizing
player death and after finalization.

The progress UI permits save/quit, quicksave, messages, character information, help
and zoom. Pause skips the remaining beats and settles the injury. Ending presentation
does not heal it. Passive effects, fields and third-party harm continue normally.

Settlement runs `PREVENT_DEATH`, `character_dies`, then `AVATAR_DEATH` while the
player remains fatally injured, stopping if genuine prevention restores life. Only
an actual death emits kill credit and `avatar_dies`, then the original style's delayed
kill hook. Reentry cannot repeat native death, and saves inside finalization are refused.
Execution callbacks remain atomic; prevention scripts that transfer worlds should
restore health and schedule that transfer for a later safe tick.

The death screen and deathcam choice wait for the ordinary game-over boundary,
outside combat/scene callbacks. A prevented death shows neither. Corpse placement
keeps native deathcam timing and is guarded against repeated game-over checks.
The avatar saves its injury-commit, finalized-death, presentation/deathcam and corpse-placement state, while
the execution saves its pending owner/deadline. Older avatar saves default those flags
to false. A restored finalized checkpoint cannot replay death events or add another corpse;
a saved deathcam resumes watching without repeating the last-words screen.

[The player knife scene](../../data/json/executions/avatar_fatal.json) inherits the
stagger/ledge graph with the extra opt-in. The demo mod adds a rare NPC knife rule
against the avatar using the existing fatal demonstration knife.

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
  "allow_fatal_target": false,
  "allow_avatar_fatal_target": false,
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
Ending a reserved fatal scene does settle its already committed injury as described above.

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

All actions accept `role` and optional `store_result`. The default role is `target`,
except `melee`, whose default `actor` is the attacking fighter.

| `op` | Members and behavior |
| --- | --- |
| `message` | Exactly one of translated `text` or `pool`. Optional `entry`, `append_to`, `separator`, and perception fields described below. |
| `move` | `direction`: `backward` (default) or `forward`; `tiles`: integer or range (default 1); `ticks_per_tile`: 1; `first_step_after_ticks`: 1; `stop_before_unsupported`: true; `collision`: `stop`. Requires scripted control of this role. |
| `shove` | Checked displacement away from a distinct `source` role. `tiles`: 1–4; timing, support and collision fields follow `move`. Requires scripted control of the displaced role. |
| `fling` | Native flight from a distinct adjacent `source`. `velocity`: integer/range 10–160 (default 20); `hit_characters`: true; `controlled_landing`: false. Requires scripted control of the displaced role. |
| `melee` | The scripted `role` performs one native adjacent melee attack against the other role, without selecting special techniques. |
| `eoc` | Nonempty `eocs` array. Existing EOCs can manipulate terrain, furniture, items, and other world state. These effects retain their existing limitations and narration rules. |
| `effect` | Required `effect` ID; `duration_ticks`: 1; `intensity`: 1. Applies the existing native effect. |
| `damage` | Required `damage_type`; `amount`: 0; `bodypart`: `torso`. Uses ordinary damage handling and armor, but never rolls a monster weakpoint; reported actual HP change can differ from the amount. |
| `incapacitate` | Physiological knockout with a separate saved recovery timer. See the knockout contract below. |

### Physical actions and cinematic clashes

`shove` and `fling` default `source` to the opposite role; source and displaced
role must differ. Both start from adjacent actors on the same Z level. Dead or
pending-fatal participants return `fatal_target`; use checked `move` for supported
fatal staging. A knocked-out, sleeping or anesthetized source returns `incapacitated`. Vehicles, mounts,
existing grabs/restraints and hallucinations are outside these physical adapters.
Native source/target identities are retained and revalidated through callbacks.

A shove captures its source-to-victim horizontal axis at the first committed step.
It then moves one checked tile per beat, saving its sampled distance, axis and
progress. It respects native character knockback resistance, immobility, diagonal
corners, occupancy, terrain and support. It stops at obstacles without adding
invented collision damage. Results include `moving`, `moved`, `blocked`, `resisted`,
`unsupported`, `out_of_range`, `fatal_target`, `incapacitated` and `unavailable`.
`execution_resistance` reports the character resistance used at admission.

A fling delegates to native flight, wall bashing, body collisions, landing, gravity
and traps. It preserves knockback resistance; the author cannot bypass it through
an intentional-motion flag. Native velocity determines range and impacts, rather
than an exact scripted destination. `moved` means the body changed position;
`blocked` means native flight was accepted without displacement; `resisted` means
native flight refused. Other preflight results include `unsupported`,
`out_of_bounds`, `out_of_range` and `fatal_target`. Callback cancellation or identity
loss stops further owned flight and interrupts the scene. `hit_characters` controls
damage to character blockers; monster collisions follow native fling behavior.

`execution_velocity` is the sampled input, `execution_distance` the actual net
grid distance, `execution_position` the absolute position, `execution_fell`
the Z decrease, and `execution_hp_change` the net target HP change at native action
return. The runtime then performs its final death check; a prevention callback
can change the body again. The native instigator receives direct impact, landing/fall
and collateral attribution. Explicit environmental damage from nested callbacks
retains no source; unrelated trap/field effects keep their native attribution.
Terrain-bash callbacks retain their existing engine contract.

A melee action uses native Character/monster combat, armor, evasion, callbacks and
death handling. `attacked` means a native attempt occurred, including misses or
armor-stopped blows; `refused` means native attack refused. It also returns the
shared preflight failures. It pays native stamina/move costs, recorded in
`execution_attack_moves`, and reports net `execution_hp_change`. Scripted attacks
do not create new automatic execution roots. They cannot force an unconscious
fighter to act.

[The cinematic clash](../../data/json/executions/clash.json) scripts both fighters,
holds a sampled 1–2-turn struggle, then selects separation, a native counterattack,
or a strength-qualified throw. Blocked/refused physical actions branch to a
standoff. Real incoming harm, displacement, cancellation and participant loss use
ordinary interruption and release both controls. Player participants receive an
interruption message. The demo sword selects this scene rarely; no production
weapon acquires automatic clash rules.

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
two wounded beats. It can end with survival. Further native producer families,
source-less participant policies, offscreen scene simulation, editing/sandbox
preview tooling and additional typed physical actions remain later milestones.
