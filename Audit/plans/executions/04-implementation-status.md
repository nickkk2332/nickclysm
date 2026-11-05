# First execution milestone

The supported authoring contract is [EXECUTIONS.md](../../../doc/JSON/EXECUTIONS.md).
The older authoring proposal includes future features and is not a loadable schema.

Delivered:

- A registered execution definition factory, named graph steps, game-turn delays,
  condition/chance/weighted/result transitions, bounded repeats and private typed locals.
- Saved instances with isolated RNG, accepted/cancelled requests at safe boundaries,
  lifecycle EOCs, budgets, terminal guards, interruption, and control cleanup.
- Durable loaded creature references, persistent monster UIDs with save migration,
  avatar/NPC IDs, dimension checks, and actual wielded-item UID binding.
- Free/held/scripted roles and a saved cancellable avatar activity. Controlled
  participants do not accumulate voluntary actions; unrelated world simulation continues.
- Result-bearing native movement/falling, effects, damage, activation EOCs and visible
  translated message pools. Existing EOCs provide world manipulation.
- Start/cancel EOC bindings and participation/ledge conditions. Existing resolved melee
  context now also snapshots the attack weapon UID and selected style.
- Core knife-stagger reference content plus an opt-in demo knife/technique mod. Ordinary
  core weapons do not automatically request executions.
- Focused regression coverage in [execution_test.cpp](../../../tests/execution_test.cpp).

The reference scene samples 1–4 backward tiles, respects obstacles/support, and offers a
50% ledge tumble with native gravity or two wounded message beats. It permits survival;
authored outcome labels and actual death are separate. Sequence damage never temporarily
sets HP to one or restores a corpse. Manual starts still refuse fatal actors. The
[fatal melee follow-up](08-fatal-hits.md) can reserve NPC/monster death at the supported
primary HP write, before native finalization, and resume that ownership across saves.

Still to develop:

- Phase 0's full producer matrix and shared combat causality. The first indexed
  living-melee slice, committed primary damage snapshots and arbitration are now in
  the [automatic-trigger follow-up](06-automatic-triggers.md), followed by
  [nonfatal handheld projectile triggers](10-projectile-triggers.md).
- Phase 6's NPC/monster fatal primary-melee reservation and native finalization are
  delivered by the [fatal melee follow-up](08-fatal-hits.md), with the player lifecycle
  in [avatar deferral](09-avatar-fatal-deferral.md). Further combat producers remain.
  Real knockout/incapacity is delivered by the
  [knockout follow-up](07-knockouts.md).
- Phase 4's owned appendable narration and auditory audiences are now delivered in the
  [narration follow-up](05-narration-milestone.md); the dedicated transcript inspector UI remains.
- [Fatal projectile admission, shove/fling/native melee and the clash example](11-fatal-projectiles-physical-clashes.md) are delivered. Broader physical policies,
  remaining combat producers and authoring/tooling demonstrations remain.

Current constraints are deliberate: two loaded participants, living manual starts or
the supported automatic [melee](06-automatic-triggers.md) and
[handheld projectile](10-projectile-triggers.md) producers with explicit
[fatal admission](11-fatal-projectiles-physical-clashes.md),
no offscreen catchup, no save from inside an in-progress action, at most 64 active scenes,
and bounded graphs. Damage interruption includes periodic environmental/bleeding HP loss.
Revision changes interrupt incompatible instances; authors must bump revisions for graph
changes. These limits are documented and enforced instead of promising the full proposal.

## Validation

- Headless GCC C++17 build and test link pass with warnings treated as errors.
- Selected combat, EOC, activity, map, worldgen, spell, projectile, and execution
  regression run: **221 cases, 11,253 assertions passed**.
- After the final native-callback participant-lifetime guard, the focused execution
  run passes **26 cases, 888 assertions**, including removing the attacker during fall
  damage dispatch. Both runs load `dda`, `test_data`, and `executions_demo`, with RNG seed
  `41723`, and report no initialization errors.
- Coverage includes serialized graph deadlines/RNG/cursors, monster identities and
  avatar activities; control ownership and cancellation; real blockers, flight and
  vehicle support; native fatal/nonfatal falls and attribution; real melee context;
  typed locals; callback reentry; and bounded failure/cleanup paths.
- JSON/C++ formatting, translation extraction, documentation links, and
  `git diff --check` pass.

Integration with master `5510d452bc` passes a fresh headless engine build and the
real-message regression suite: **67 cases, 1,518 assertions**, seed `41723`, without
initialization or test errors. Carried/ground/vehicle item discovery is preserved;
unattached items in drop callbacks retain the supplied destination as their fallback
location. This covers avatar/NPC drops, thrown items, and consumed-drop callbacks.

To run the focused cases from the repository root:

```sh
./tests/cata_test '[executions]' --mods dda,executions_demo --rng-seed 41723
```

The test runner adds `test_data` automatically.
