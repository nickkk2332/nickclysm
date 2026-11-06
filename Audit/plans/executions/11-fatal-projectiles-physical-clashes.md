# Fatal projectiles, physical actions and cinematic clashes

**Follow-up:** [the inspector and additional trigger channels](12-inspector-and-additional-triggers.md) are now implemented. The original milestone record below retains its scope.

This milestone extends [handheld projectile triggers](10-projectile-triggers.md)
and the existing fatal ownership adapters. The supported authoring contract is
[EXECUTIONS.md](../../../doc/JSON/EXECUTIONS.md).

## Fatal projectile admission

`event: "fatal_hit"` now accepts `projectile` rules. The actual lethal primary
HP write supplies the body part, damage, HP loss, ammunition and critical facts.
If an earlier round wounded the same victim, its living packet does not replace
the lethal round's packet. Each victim has at most one fatal admission opportunity
per firing action, within the existing 64-victim contact budget.

An accepted injury remains real: the victim has its native fatal HP state, keeps
its occupied tile, and has not yet produced death rewards or a corpse. Ownership
does not become ready until the complete firing action returns, including ammo
use, recoil, ranged events and synchronous callbacks. Starts cannot run between
pellets. Exceptional exits settle reserved injuries without playing graph beats.

This grants no protection from subsequent harm. A later damaging pellet, round
or callback settles the reservation through the existing death path. Cancellation,
timeout, participant loss and world transitions also settle it. Rescue and native
character death prevention remain possible; death attribution, corpse creation
and rewards use the existing exactly-once lifecycle. Player victims still require
both fatal opt-ins, and ordinary game-over handling follows settlement.

The producer boundary remains the real wielded main-map gun/bow firing path.
Mounted guns, turrets, underbarrel gunmods, thrown weapons, generic projectiles,
spells, secondary bounce flights and callback projectiles need separate adapters.
Nonfatal rules retain their first-direct-hit-per-victim behavior.

Secondary pre-commit injuries remain outside this producer. Native bleeding can
apply its first damage when the effect is added, before the shot's primary HP
write; a victim killed there receives ordinary death, rather than a fabricated
fatal primary packet.

[Core definitions](../../../data/json/executions/fatal_projectile.json) reuse the
checked projectile stagger, with separate player opt-in. The
[demo rules](../../../data/mods/EXECUTIONS_DEMO/clash_and_fatal_projectile.json)
apply only to demonstration weapons, with a 10% chance and pair cooldown.

## Physical verbs

| Action | Meaning and boundaries |
| --- | --- |
| `shove` | Move the displaced role 1–4 checked tiles away from an adjacent source. Capture the axis once and save it; character knockback resistance can shorten or prevent the motion. Walls, occupants, restraints and unsupported destinations can stop it. There is no invented wall damage. |
| `fling` | Invoke native flinging at velocity 10–160, with character-collision and controlled-landing options. Collisions, knockback resistance, landing damage, falling and death come from the engine. Only loaded, adjacent living participants qualify. |
| `melee` | The selected role attacks the opposite role through native Character/monster melee. Native hit rolls, armor, damage, callbacks and move costs apply. `attacked` records an attempted attack, including a miss; it does not promise damage. |

The acting or displaced role must have scripted control. Knocked-out, sleeping
and anesthetized sources cannot initiate these actions. Physical actions report
explicit results for branching, alongside position, net HP change, distance and
fall context where applicable. Fatal reserved bodies use the existing checked
`move` action for staging; these new verbs refuse them.

An owned fling retains both bodies and scopes instigator attribution to its exact
native flight, impacts and collateral collisions. Nested unrelated environmental
damage keeps its explicit source semantics. Cancellation or participant removal
from a synchronous callback stops further owned motion. Loaded map boundaries
also stop it. A player's scripted execution activity suppresses distraction
prompts during these synchronous actions.

Shove direction and per-tile progress survive saves. Fling and melee are atomic
native actions between graph checkpoints; restoring the following checkpoint
does not repeat them. Existing combat/save guards exclude partial native actions.

## Cinematic clash

[The core clash graph](../../../data/json/executions/clash.json) scripts both
fighters, opens an owned message, appends a struggle message, and waits a saved
1–2 ticks. Its weighted branch can:

- Shove the target and move the initiator back, separating the fighters.
- Let the target deliver a native counterattack after an announced beat.
- Attempt a throw when the initiating character has at least four more strength;
  otherwise use the push branch.

Refused or blocked exchanges can become a standoff. Ordinary damage, displacement,
participant loss and player cancellation use the existing interruption policies;
the example supplies an interruption message. It freezes neither the world nor
other creatures, and it does not guarantee survival or a kill.

The demo sword has a 2% living character-melee rule with a 30-tick pair cooldown,
excluding knocked-out, sleeping and anesthetized fighters.
Production weapons receive no automatic rules. Authors can invoke the same graph
manually through the existing EOC interface or supply their own matching rules.

## Validation

[Fatal projectile tests](../../../tests/execution_projectile_fatal_test.cpp) cover
native guns/bows, all three victim kinds, exact HP-commit facts, late lethal rounds,
subsequent harm, ordinary death fallbacks, rescue/prevention, cancellation,
exception cleanup, deadlines, transitions, saved ownership and checked motion.

[Physical/clash tests](../../../tests/execution_physical_test.cpp) cover saved shove
axes, blocked and unsupported movement, native collisions and falls, player/NPC/
monster bodies, causal damage attribution and nested environmental damage,
callback interruption/removal, native counterattacks, exception cleanup, all three
clash branches, save/resume, external interruption and schema validation.

The final combined native-message regression passes **315 cases / 127,513
assertions**, including the 24 new fatal-projectile and physical/clash cases,
existing execution/trigger/narration/knockout/fatality coverage, combat,
projectiles, weakpoints, spells, sleep/effects and affected audit regressions.
This includes the final sleep/anesthesia source guards. Seed `41723`, `dda`,
`executions_demo` and automatically supplied `test_data` were used, with no
initialization or test errors.

The headless GCC C++17 engine archive, native-message test executable and explicit
`cataclysm` game target build with warnings treated as errors. The game passes
its `--help` smoke check. C++/JSON formatting, translation extraction, all 68 local
documentation file links, all 22 final source/data/test hashes and staged diff
checks pass. The staged source matches the final combined regression build.

The unchanged auxiliary formatter-link limitation recorded in preceding milestones
remains; the explicit game target and standalone JSON formatter succeed. This is
a selected native-message suite and a headless smoke check. The full repository
suite and interactive graphical playtesting were not run.

With a normal test build, run the dedicated cases from the repository root:

```sh
./tests/cata_test '[fatal_projectile],[execution_physical]' --mods dda,executions_demo --rng-seed 41723
```

The runner supplies `test_data`; do not add it a second time.

## Remaining expansion

This delivers the requested fatal handheld projectile path and a working clash
example. Further work can add other trigger producers, additional participant
roles, offscreen execution policy, animation/camera presentation, an authoring
inspector, and physical verbs such as grapples, carries and throws to an explicit
destination. Those need their own ownership, collision and interruption contracts.
The current actions do not provide a general cinematic physics simulator.
