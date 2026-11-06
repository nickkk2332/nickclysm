# Power movement, summons and AI intent

These are opt-in consumers of [abilities](ABILITIES.md) and
[power accounting](POWER_OWNERSHIP.md). Existing native flings, spell summons and
NPC spell selection retain their policies unless routed through these interfaces.

## Attributed native movement

```json
{ "power_fling": { "source": "u", "target": "npc", "velocity": 40, "controlled": false, "hit_characters": true } }
```

`source` and `target` select real dialogue actors (`u`/`npc`, defaults shown).
Velocity is an integer expression from 10 to 300, using native fling units (roughly
10 per horizontal tile before resistance/collisions). Direction is away from the
source on the same z level. The operation dispatches immediately; an enclosing
full-action ability supplies windup, payment and targeting restrictions. This EOC
itself has no action cost and does not constitute a successful attack/hit.

Native grabs, knockback resistance, armor, wall bashing, creature collisions,
vehicle unboarding, avatar map shifts, water, gravity and landing traps still
apply. A moved monster with `IMMOBILE` or any actor with `CANNOT_MOVE` refuses.
Hallucinations, fake/missing roles, self-flings and an incapacitated source refuse.
Flights stop at reality-bubble boundaries. These are horizontal launches followed
by native gravity, not chosen 3D trajectories or vehicle throws.

Direct native impacts on the flung actor and direct collision damage retain the
instigator. A nested scripted environmental damage call does not borrow it. Traps
retain their own native damage semantics. References are held and checked after
damage callbacks; role removal, external displacement or a callback-induced map
shift stops the remaining motion. Nested authored requests are bounded to eight.
Saving, replacing the session or transitioning worlds during a flight is refused.
Committed damage/movement is not rolled back if a callback throws.

Outputs in dialogue context:

- `reason`: `moved`, `blocked`, `resisted`, `target_dead`, `interrupted`,
  `out_of_bounds`, or an admission reason (`invalid_velocity`,
  `participant_unavailable`, `source_incapacitated`, `unsupported_geometry`,
  `world_transition`, `ancestry_budget`).
- `power_motion_distance`: Chebyshev displacement from origin to final position;
  `power_motion_position`: absolute final position.
- `power_motion_hp_loss`: net HP decrease observed over the operation;
  `power_motion_impacts`: number of recorded contact/landing observations;
  `power_motion_fell`: whether final z is below initial z.

The inspector keeps the latest 32 native movement receipts: typed instigator and
moved identity, cause, original/remaining velocity, reached absolute path, landing,
fall, net HP decrease, and terrain/vehicle/creature/ground contacts. Each contact
records collider identity, force, observed HP differences and whether a terrain
blocker was broken. Native gravity contributes its final landing position and HP
difference. These observations include synchronous callback changes and are not a
separate damage transaction or a prediction of damage. History is not saved.

## Owned summoned monsters

```json
{
  "type": "power_summon",
  "id": "my_familiar",
  "name": "force familiar",
  "monster": "mon_dog",
  "revision": 1,
  "duration_ticks": 300,
  "max_per_owner": 2,
  "range": 5,
  "leash": 30,
  "abilities": [ "my_throw", "my_blast" ],
  "on_spawn": [],
  "on_command": [],
  "on_end": []
}
```

This definition creates new monsters, not bonds to existing creatures. Owners may
be avatars, NPCs or monsters; a bonded summon cannot own more bonds. There are at
most 128 nonterminal bonds, including unresolved cleanup tombstones, and at most
`max_per_owner` (1–16, default 2) per owner/definition. Revisions default to 1;
changing a revision ends incompatible bonds. Definitions support `copy-from`.

`duration_ticks` is 1–1,000,000 seconds (default 60); a finite native summon timer
also applies. `range` is the spawn radius, 1–30 (default 5). `leash` is the maximum
owner distance, 1–60 (default 30). Both spawn and owner must be loaded on the same
z level; spawning rejects occupied, impassable, open-air and vehicle tiles. Native
monster placement may impose additional habitat requirements. These deliberately
conservative spawn rules apply even to flying/swimming monster definitions.

```json
{ "spawn_power_summon": { "id": "my_familiar", "owner": "u", "loc": { "context_val": "spawn_point" } } }
```

`id` accepts a string/variable. `owner` defaults to `u` and accepts `u`/`npc`.
Optional `loc` is a variable containing an absolute map position; without it the
first suitable adjacent tile is attempted. Outputs are `power_bond`, an exact
string handle (`"0"` on refusal), and `reason` (`accepted` or a specific refusal).
No spawn payment is implicit: wrap spawning in an ability/application if needed.

A bond saves owner/summon identities, definition/revision, expiry, order, guard
position and command target. It does not adopt a replacement at an old position.
Allegiance follows the actual owner's native relationships, including monster and
hostile NPC owners. Other summons of that owner are friendly. `get_summoner()`
resolves the typed owner, allowing the existing native kill-credit path to credit
a Character summoner; monster owners remain monsters and have no fabricated player
credit. Each familiar pays its own ability energy/charges. Shared owner energy,
redirected damage and inventory/equipment restoration are later adapters.

### Orders

```json
{ "command_power_summon": { "bond": { "u_val": "my_bond" }, "command": "attack", "owner": "u", "target": "npc" } }
{ "command_power_summon": { "bond": { "u_val": "my_bond" }, "command": "guard", "loc": { "context_val": "guard_point" } } }
{ "command_power_summon": { "bond": { "u_val": "my_bond" }, "command": "follow" } }
{ "command_power_summon": { "bond": { "u_val": "my_bond" }, "command": "dismiss" } }
{ "dismiss_power_summons": { "owner": "u", "id": "my_familiar" } }
```

Handles are strings; do not convert them to floating-point math values. The
requesting `owner` must match the bond. `attack` requires a real hostile target
visible to the summon and within the owner's leash. It follows that identity;
losing sight, losing the target or changing its allegiance falls back to following
the owner. It does not select a replacement opponent automatically. Set
`targeted: true` for an avatar-only look-around selection. Cancellation preserves
the previous order. `target` otherwise defaults to beta (`npc`).

`follow` uses native pathfinding toward the owner and waits within one tile.
`guard` uses native pathfinding to the given point (default the summon's current
tile), then holds position. It is a positional order, not an autonomous sentry
scan. The bond supplies targeting/allegiance; native movement constraints and specials
still apply. Choose suitable
monster definitions. `attack` first considers the definition's opted-in abilities,
then uses native pathfinding/melee if none is eligible. No scripted teleport or
cost-free melee is substituted. The ability scheduler holds the actor during
windup; it revalidates and commits the ordinary resource/charge/move policy.

Orders apply to subsequent decisions. An already admitted ability retains its
own target and cancellation policy; changing an order does not implicitly cancel
that cast.

Orders report `reason` (`commanded`, `dismissed`, `wrong_owner`, `not_found`,
`invalid_target`, `target_out_of_reach`, `invalid_guard_tile`, `expired`, etc.).
Owner-filtered dismissal optionally filters by definition (empty/omitted means
all that owner's bonds) and returns `power_summons_dismissed`.

### Lifecycle and cleanup

Hooks contain up to 16 `ACTIVATION` EOCs each. Alpha is owner, beta is summon.
Context includes `power_bond`, `power_summon` (definition),
`power_summon_command`, `power_summon_reason`, `power_owner_present`,
`power_summon_present`, and the live command target's absolute position when
available (`power_command_target`). Check presence before acting on missing roles.
Hooks run on creation, accepted orders, and terminal cleanup. Ending removes only
that spawned identity quietly, without manufacturing kills/corpses. Its existing
carried contents are not restored or dropped by this cleanup.

Death, owner loss, expiry, broken leash, incompatible definitions and world
transitions end bonds. Missing/unloaded summons leave saved, inactive tombstones
until that identity returns; their finite native lifespan remains a second limit.
There is no offscreen AI, remote map mutation or callback catch-up. Finalized
native death retires the bond at the next safe pump. Repeated processing cannot
run an end hook twice. Terminal history is limited to 32 records and is not saved.

Lifecycle callbacks cannot recursively spawn or issue more ordinary orders
(`callback_busy`); they can mark bonds for dismissal. Membership becomes inactive
immediately. Removal waits for native combat/flight and ability callbacks to
finish, then settles at a safe runtime pump. Exceptions preserve committed world
changes, mark the failing bond terminal and attempt its cleanup once. Saving
and session replacement inside a lifecycle callback are refused. Legacy ability
saves without `power_actors` load with no bonds.

## Pure intent queries and selection

An ability can optionally declare:

```json
"ai": {
  "intent": "control",
  "relation": "hostile",
  "priority": 20,
  "magnitude": 15,
  "min_range": 0,
  "safe_radius": 0
}
```

`intent` is `harm`, `heal`, `protect`, `control`, `mobility` or `summon`.
`relation` is `hostile` (default), `friendly`, `self` or `any`. `priority` is
−1,000,000–1,000,000 (default 0); `magnitude` is 0–1,000,000 (default 0).
`min_range` defaults to 0, bounded by the ability's ordinary range. `safe_radius`
defaults to 0 (disabled); when positive (up to 60), any other nonhostile creature
or the caster within that distance of the target refuses the candidate. It is a
conservative geometry check, not native line/cone/blast tracing. Use it only where
that estimate matches the authored effect.

Queries perform native ability admission (participants, LOS, range, busy state,
resources/reservations, charges and cooldowns), then relation/minimum-range/safety
checks. Healing intent refuses a fully healthy target. They neither execute EOCs
nor evaluate spell formulas or random windup. They do not run condition scripts,
materialize power accounts or advance clocks. Future release hooks can still veto
an eligible candidate. Declared magnitude is an author's estimate, not simulated
harm/healing or a guarantee that the effect succeeds. AI selection currently
supports creature/self targets, not arbitrary point/area target search.

`u_ability_query('my_throw')` (or `n_`) evaluates eligibility against the other
actor. Keyword `field` accepts `eligible` (default), `score`, `magnitude`,
`energy_cost`, `move_cost`, `windup_min`, `windup_max`. Score is
`priority + magnitude` for eligible actions, otherwise 0. Other numeric fields
are declared estimates and can be read even when admission refuses. C++ query
results additionally expose a reason and intent. `u_power_summons('my_familiar')`
counts active loaded, unexpired owned instances; an empty ID counts all definitions.

```json
{ "use_best_ability": { "abilities": [ "my_throw", "my_blast" ] } }
```

This EOC considers 1–16 definitions with alpha as caster and beta as target,
chooses the greatest eligible score, breaks ties by ability ID, then submits one
normal full-action request with the caller's context. No eligible action reports
`no_eligible_ability`. Successful submission reports `ability_instance` and the
ordinary admission `reason`; it does not claim release/hit success. No implicit
free proc invocation is available to AI. Monster EOC specials and existing NPC
JSON goals can opt into this operation. This does not replace all native NPC AI
or teach it to infer the effects of arbitrary scripts.

## Demonstration

Enable `powers_demo`. Spawn a `mon_power_demo_brawler`: it chooses an adjacent
throw or a ranged charged strike. The four familiar focuses summon/dismiss, order
an attack, guard, and follow. The five-minute force familiar uses the same two
abilities and its own regenerating focus/charge group. The execution/ability
inspector shows bonds and movement receipts alongside the existing accounts and
casts. [Milestone validation](../../Audit/powers/09-movement-summons-milestone.md)
records the tested boundaries.
