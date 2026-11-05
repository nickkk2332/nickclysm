# Shared ability lifecycle: first implementation

Implemented on top of the execution inspector/producer branch, 2026-11-05.
This is the first powers expansion slice, addressing the shared action foundation
in CAST-01/02/03/04. It does not close every finding in the original audit.

## Delivered contract

- New opt-in `ability` JSON definitions wrap existing real-caster spell effects
  or ACTIVATION EOCs. A separate session-owned runtime handles admission,
  queued start, sampled world-tick windup, release validation, interruption,
  commitment and structured results.
- `request_ability` and `cancel_ability` EOC actions, interactive avatar target
  selection, fixed-point and typed creature targets, source-item provenance,
  and `u_in_ability` / `npc_in_ability` control conditions.
- Pure `can_request_ability` queries expose native eligibility to JSON without
  running spell conditions/formulas or sampling windup.
- Shared player/NPC/monster paths. Player activity and existing native AI turn
  loops hold voluntary actions; physiology and the world continue. The demo
  monster requests the same ability through a native EOC special.
- Explicit `action` versus opt-in `effect_only` invocation. Fixed move costs
  work for all supported real creatures; nonzero mana/stamina costs require a
  Character. No hidden avatar, native spell payment or component consumption.
- Payment occurs synchronously after final revalidation, once. Pre-commit
  interruption pays no release cost; spent time remains spent. Post-commit
  partial work is not rolled back or falsely refunded.
- Saved actor identities, dimensions, context/locals, origin/selection, progress,
  sampled windup/deadline, XP/cost snapshots and bounded causal ancestry.
  Callback work is queued, not recursively pumped. Loading preserves root
  budgets even after the root action has finished.
- Explicit spell-object context reaches instance formulas and spell-effect EOCs,
  and is inherited by declared extra spells/`WONDER`. Independent callback/proc
  spells keep an empty/default context. Legacy spell EOC actor order is preserved.
- Read-only ability inspection in the existing debug inspector, including
  payments, deadlines, cancellation reasons, causal IDs and bounded primary
  native contact facts. Dispatch success is separate from hit/death claims.

The complete schema, ordering and boundary contract is in
[ABILITIES.md](../../doc/JSON/ABILITIES.md). The opt-in content pack is
[POWERS_DEMO](../../data/mods/POWERS_DEMO/charged_strike.json).

## Proof and validation

The native integration scenarios are in
[ability_lifecycle_test.cpp](../../tests/ability_lifecycle_test.cpp):
the same charged strike from avatar/NPC/EOC/monster-special paths; actual native
contacts; saved random windup/activity and moving identity; fixed points/self;
native item EOC invocation; mana/stamina commitment and passive drains; explicit
monster resource refusal; cancellation, damage, displacement and incapacitation;
callbacks changing participants; pure queries/inspection; spell formulas/EOCs,
extra inheritance and unrelated proc isolation; area/fatal observations;
deferred bounded child requests; exceptions and mid-dispatch save blocking;
independent simultaneous casters; time jumps, cancellation and invalid schemas.

Final validation on 2026-11-05:

- Clean combined native-message regression: **362 cases / 399,267 assertions
  passed**, including all 23 new ability cases and the existing execution,
  narration, combat, spell, projectile, knockout, fatal-hit, NPC sleep, monster
  attack/grab, weakpoint and affected audit cases. Seed 41723; `dda`,
  `executions_demo`, `powers_demo` and automatically supplied `test_data`.
  No initialization or test errors.
- Headless GCC C++17 engine archive and explicit `cataclysm` target built with
  project warnings treated as errors; `cataclysm --help` smoke check passed.
  All 200 engine translation units depending on the changed game layout and all
  40 selected test translation units were refreshed against current headers.
- C++ formatting checks passed for 28 files; JSON formatting and translation
  extraction passed for all four changed/new JSON files. Twelve powers document
  file links and all 34 final source/data/test hashes were checked.
- Focused selection for a standard test build:
  `./tests/cata_test '[abilities]' --mods dda,executions_demo,powers_demo --rng-seed 41723`.

This was a selected native-message suite and headless smoke check. The full
repository suite and interactive graphical playtesting were not run. The
pre-existing auxiliary formatter-link limitation remains unchanged; formatting
was checked with the working standalone JSON formatter.

## Deliberate remaining work

This is an opt-in wrapper, not a migration of every legacy entry point. Existing
native spell learning, failure, XP, components, caller item consumption and
monster special cooldowns remain with their original callers. The wrapper's
pure query certifies native eligibility, not arbitrary stateful EOC conditions.
Native spell formulas and randomness still evaluate during actual effects.

Resource reservation across turns, custom pools, item ownership/charges,
cooldown groups, sustained/held powers, cost failure/refund transactions across
arbitrary EOCs, complete per-packet resolution, owning applications, areas and
summons, mutable damage/shields/redirects and autonomous power-selection AI
remain later stages. The next useful slice is the damage transaction and defense
contract, with an exact-capacity mana shield as its acceptance example.
