# Nonfatal projectile execution triggers

The supported contract is [Projectile rules](../../../doc/JSON/EXECUTIONS.md#projectile-rules).
This extends [automatic melee triggers](06-automatic-triggers.md) using the existing
saved runtime and native projectile damage path. Fatal projectile deferral remains
a separate milestone.

## Producer and arbitration

An explicit `projectile` channel enables living-hit rules for loaded avatar/NPC
shooters using their actual wielded gun on the main reality map. Native bows and
crossbows use the same `Character::fire_gun` path. Omitted channels and `melee`
continue to mean the original two melee producers.

One firing action spans the complete burst. Each victim receives one candidate,
using its first successful direct hit, including a hit stopped by armor. Later
pellets/rounds cannot replace its body part, critical flag or damage facts, or
reroll selection. Different victims can receive separate contacts. The first 64
victim identities are retained in native order; later victims receive normal
damage without automatic candidate work.

Selection follows ammo use, recoil, ranged events and synchronous effects. No
graph starts between pellets or rounds. A victim killed later fails living
admission. A primary hit fatal at its HP commit is excluded even if a callback
rescues it. This producer never intercepts or reserves projectile death.

## Captured facts and causality

Snapshots contain the firing item UID/type/categories, ranged skill, actual
ammunition ID when available, native critical result, body part where available,
stable monster weakpoint ID, reported damage types and the primary HP write.
Style and technique are absent. `ammunition` is a match filter and context value;
`projectile_origin` is the absolute flight origin. The backward axis retains that
origin when a later callback moves the shooter.

The existing HP-write token is consumed before damage-event callbacks. Nested
damage, healing and projectile effects cannot overwrite its result. The adapter
does not recalculate damage, armor or weakpoints.

The firing scope authorizes an exact native flight object once per round. The
outer flight also grants an exact target dispatch, consumed once at the selected
direct hit. These gates exclude callback projectiles, nested firing, secondary
generic/bounce flights, shrapnel, magical projectiles and rider redirection.
Melee spawned by firing/flight callbacks cannot acquire a new automatic root.
The gates cover both scalar and grouped shotgun-pellet paths.

Firing and flight join the combat-in-progress guard. Reentrant saves and scene
pumping cannot expose a partial shot. Exceptional unwinding restores guards and
discards unpublished contacts. Bodies remain retained through native ranged
events and selection; durable references reject removed/dead participants and
replacement bodies.

## Content and remaining boundaries

[Core stagger content](../../../data/json/executions/projectile.json) narrates the
impact and takes one or two checked backward steps, stopping before unsupported
terrain. The shooter stays free while the victim is scripted. It permits up to
60 tiles between participants; individual scenes can impose shorter distances.

[The demo mod](../../../data/mods/EXECUTIONS_DEMO/projectile.json) adds a pistol
and bow with rare stagger and qualifying critical-head knockout rules. Knockouts
reuse the physiological action and immunities. Production weapons gain no rule.

Mounted/delegated firing, turrets, underbarrel gunmod firing, thrown items, direct
generic projectile calls, spells, source-less hits and offscreen maps need
separate adapters. A primary bouncing-ammunition shot can qualify; its secondary
flights cannot. Queued future effects remain ordinary world events and can
interrupt scenes. This is a narrow firing adapter, not a universal causal graph.

## Persistence and validation

Accepted scenes use saved contact counters, cooldowns, participant references,
typed context, RNG, graph deadlines and control ownership. Partial native firing
is not saved. Restoring a scene does not fire again, republish its contact,
reroll its delay or repeat its start callback. Ordinary later damage can still
interrupt that scene; saved ownership grants no immunity.

[Dedicated tests](../../../tests/execution_projectile_test.cpp) cover real
avatar/NPC firing against all three victim kinds; guns/bows; scalar/grouped pellets
and bursts; ammunition filters; native damage callbacks; secondary attacks;
first-hit semantics; fatal/later-fatal and rescued-fatal exclusion; saved progress
and cooldowns; direction; real knockouts; removed victims and weapon identity;
producer exclusions; zero-chance RNG; exception cleanup; budgets; and inherited
schema validation.

The dedicated final selection passes **17 cases / 977 assertions**.

The final combined native-message regression passes **291 cases / 126,706 assertions**,
including the existing execution, trigger, narration, knockout, NPC/monster/player
fatality, combat, spell, sleep/effect and affected audit cases. Save-failure and
dimension-transfer paths are included. Both runs use seed `41723`, `dda`,
`executions_demo` and automatically supplied `test_data`, without initialization
or test errors.

The headless GCC C++17 engine archive, native-message test executable and explicit
`cataclysm` game target build with warnings treated as errors; the final game passes
its `--help` smoke check. C++/JSON formatting, translation extraction and all 52
local documentation links pass. All 13 source/data/test files match the snapshot
used by the successful dedicated run and the final combined regression. Staged
source hashes and `git diff --check` also pass.

The unchanged auxiliary formatter-link limitation recorded in preceding milestones
remains; the explicit game target and standalone JSON formatter succeed. This is a
selected native-message suite and a headless smoke check. The full repository test
suite and interactive graphical playtesting were not run.

With a normal test build, run from the repository root:

```sh
./tests/cata_test '[projectile_execution]' --mods dda,executions_demo --rng-seed 41723
```

The runner supplies `test_data`; do not add it a second time.
