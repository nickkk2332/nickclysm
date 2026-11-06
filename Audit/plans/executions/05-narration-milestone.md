# Owned narration milestone

Execution messages now support named `entry` groups, `append_to`, whitespace separators,
and visible/audible audiences. The [supported JSON guide](../../../doc/JSON/EXECUTIONS.md)
contains the exact contract. This delivers the message ownership and observation work
from phase 4; automatic combat rules and pending-fatal actors remain later phases.

Each perceived beat creates an immutable timestamped segment. Adjacent segments from
the same owned group combine in the sidebar; full chronological history keeps their
actual order around unrelated combat messages. Ownership includes the execution
instance, and opaque message identities survive saves. A missing, hidden, cleared, or
fully pruned group starts a new visible group without restoring older text.

Sound-only narration uses native hearing checks. Mixed visual/audible content requires
an explicit translated hearing fallback, so hearing a cough cannot expose a visual
description of staring or silently moving a jaw. Unseen names remain anonymous.
Narration does not itself create physical noise; authored sound EOCs remain available.

The interface setting **Show execution narration** controls presentation. Disabling it
preserves action progress, branches, rolls, and outcomes. Message identity generation
uses independent entropy, leaving gameplay and per-execution RNG streams unchanged.

The message record/history model lives in `message_log.h/.cpp` and is shared by the
normal message UI and test sink. Existing ordinary message coalescing and cooldown
rules stay in the message layer. Sidebar compaction scans only the requested trailing
groups and builds joined text in chronological order, preserving its timestamps in
the full log.

The knife reference scene now appends its two wounded beats to the observed swaying
entry. Tests in [execution_narration_test.cpp](../../../tests/execution_narration_test.cpp)
cover ownership, simultaneous scenes, intervening combat, save/resume, hidden/pruned
anchors, legacy logs, blind/deaf observers, gameplay RNG, and presentation settings.

A dedicated inspector UI, offscreen simulation, snippet-ID pools, richer acoustic
propagation, real knockout physiology, indexed combat triggers, and fatal reservation
are not claimed by this milestone.

## Validation

- Headless GCC C++17 build passes with warnings treated as errors.
- A dedicated test binary linked the real `messages.o` instead of the test sink:
  **67 cases, 1,518 assertions passed**, covering execution/narration plus affected
  combat, EOC, projectile, powers, interactivity, and worldgen regressions.
- The standard test backend also passes **38 execution cases, 1,066 assertions**.
  Both runs load `dda`, `test_data`, and `executions_demo`, use seed `41723`, and report
  no initialization errors. The native run includes the normal message-history and
  coalescing integration; sidebar compaction uses the same tested history model.
- The display-toggle case replays one saved stagger with narration enabled and
  disabled, comparing actual final position, completion tick, outcome, and gameplay
  RNG state. Visibility is explicit so the enabled run also exercises message emission.
- JSON/C++ formatting, audible-fallback translation extraction, documentation links,
  and `git diff --check` pass.

The focused suite can be run from the repository root with a normal test build:

```sh
./tests/cata_test '[executions]' --mods dda,executions_demo --rng-seed 41723
```

The test runner adds `test_data` automatically.
