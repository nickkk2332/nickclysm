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
sets HP to one or restores a corpse. A triggering fatal hit is refused after native death
processing, rather than being retroactively converted into a delayed death scene.

Still to develop:

- Phase 0's full producer matrix, shared nested combat causality and committed per-type
  damage snapshots, followed by indexed automatic trigger rules and arbitration.
- Phase 6 fatal reservation/finalization and real knockout/incapacity semantics. These
  require their own native combat/death integration and tests before fatal scenes work.
- Phase 4's owned appendable narration and auditory audiences are now delivered in the
  [narration follow-up](05-narration-milestone.md); the dedicated transcript inspector UI remains.
- More physical actions, richer policies, per-action secondary attribution beyond the
  implemented native fall scope, and broad gameplay/tooling demonstrations.

Current constraints are deliberate: two living loaded participants, explicit EOC requests,
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
