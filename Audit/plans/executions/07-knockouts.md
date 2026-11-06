# Physiological knockout milestone

The supported authoring contract is [EXECUTIONS.md](../../../doc/JSON/EXECUTIONS.md#knockouts-and-recovery).

This follow-up adds an `incapacitate` action for the `unconscious` state. It applies a
real native knockout, with result branches for immunity, unsupported physiology or
context, prior incapacity, unavailable actors, and callback interruption. It leaves
death, HP, corpses, rewards and ordinary sleep under their existing systems.

## State and time

The native `knocked_out` effect owns the state. Its source and depth use existing
effect fields; its saved calendar deadline is additional per-effect instance data.
The effect's public duration reports actual remaining game time. Only this effect
uses the new deadline path; ordinary effects keep their existing decay behavior.

This prevents speed, duplicate effect processing, NPC catch-up and unloading from
shortening or extending the knockout. The state continues after scene completion,
interruption, cancellation or manager clearing. It survives normal actor serialization
and expires even when an actor has spent the interval outside the active area.
Save data validates the deadline against a one-hour maximum. Effect pausing cannot
make the knockout permanent.

Light knockout wakes on positive applied HP loss, before native damage-event callbacks.
Deep knockout waits for its deadline or explicit effect removal. Both retain ordinary
damage, gravity, fields, bleeding and death; neither sets HP to one, heals the target,
or reserves a fatal injury. Zero damage, healing and ordinary sleep wakeup do not end
the state. Removal during effect iteration expires it safely rather than erasing a
live iterator's entry.

## Native adapters

- Characters, ordinary flesh animals, and explicitly opted-in monster physiologies
  are supported. Explicit knockout immunity takes precedence over a monster opt-in.
- Mounting/riding, vehicle occupancy, actively holding another creature and an
  operation activity return unsupported. Ordinary sleep, anesthesia and an existing
  knockout report prior incapacity without changing its timer or policy.
- Avatar input, NPC/monster decisions, voluntary movement, melee/ranged actions,
  normal spell casting, activities, blocking/dodging and ordinary perception respect
  the state. Native monster special-attack wrappers and EOC attack dispatch are gated.
- Ordinary activities are cancelled. The execution's own avatar activity remains
  under its manager, avoiding a self-cancellation when that scene knocks its actor out.
- Positive moves are suppressed; existing action debt is preserved. Recovery does
  not create a bank of missed turns. Already committed atomic actions are not rewound.
- Saves and information screens remain available to the unconscious avatar. UI
  cancellation does not remove a physiological injury state.

The read-only EOC conditions `u_is_knocked_out` / `npc_is_knocked_out` check precise
current state, including expiry before the effect-map entry is cleaned up.
`u_can_be_knocked_out` / `npc_can_be_knocked_out` share the action's admission check.
Missing and non-creature talkers never acquire an avatar fallback.

## Content and remaining scope

[Core reference scenes](../../../data/json/executions/knockout.json) demonstrate a
critical knockout and a takedown with a real character strength advantage. The
optional demonstration sap has rare critical-head-hit and strength rules. Production
weapons do not acquire automatic knockout rules.

The new action supports finite gameplay unconsciousness, not a medical injury model.
The [fatal melee follow-up](08-fatal-hits.md) delivers NPC/monster fatal reservation
and native finalization. More automatic combat producers, avatar fatal deferral,
richer physical actions and the dedicated inspector remain later work. Scripted world effects and
forced displacement remain available; they are not voluntary actor decisions.

## Validation

Coverage is in [execution_knockout_test.cpp](../../../tests/execution_knockout_test.cpp).
It exercises avatar/NPC/monster application, scene/control cleanup, no death rewards,
time and depth persistence, same-turn processing, positive HP wakeup, environmental
and fatal damage, native action/special/casting suppression, flight/gravity, activity
cancellation, callback removal, immunity/physiology, offscreen NPC catch-up, EOC
queries, reference content and definition validation. Existing sleep, effect, combat,
execution and narration regressions are included in the native-message selection.

- Headless GCC C++17 engine and game builds passed with warnings treated as errors;
  the final game executable also passed its command-line help smoke check.
- Broad native-message regression selection: **211 cases / 123,871 assertions passed**,
  including all 23 knockout cases present at that point and the existing sleep,
  effect, spell, combat, execution, narration and affected audit regressions.
- After adding explicit dodge/evasion gates and their 24th knockout case, the final
  `[knockout],[dodge]` selection passed **30 cases / 573 assertions**.
- Both successful runs used seed `41723`, core `dda` and `executions_demo`, with
  `test_data` supplied by the test runner. Neither logged initialization or test errors.
- C++/JSON formatting, translation extraction, documentation links and diff checks
  passed. Final implementation, data and test files match the validated file hashes.
