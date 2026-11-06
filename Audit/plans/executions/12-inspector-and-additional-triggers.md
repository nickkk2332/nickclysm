# Inspector and additional native triggers

This milestone adds the read-only execution inspector and three explicit native
producer channels. The supported contract is [EXECUTIONS.md](../../../doc/JSON/EXECUTIONS.md#additional-trigger-channels).

## Inspector

Debug Info offers **Inspect executions**, also searchable as **Execution inspector**
in the Data actions. Its entry bypasses debug-use events and achievement changes;
it does not invalidate simulation caches, run author conditions, consume RNG,
advance time or perform scene actions. Navigation reads detached snapshots.

Scene details include participant identity/dimension and loaded/visible state,
positions/HP/moves, control requests/claims, definition/revision, current step/action,
deadlines, action progress, local results/context, fatal ownership/readiness,
cancellation/terminal reasons, narration and transcript. Up to 128 recorded graph
choices include ranges, outcomes and whether a draw occurred. Active-scene choice
logs are optional in saves for compatibility with earlier snapshots.

The latest 32 native trigger reports include original facts and participant IDs,
indexed candidates, rejection reasons, priorities, weights, chance and actual
draws, selected instance and weighted selection. Lower-priority candidates skipped
after a selection are labeled as unevaluated. Candidate records remain capped at
64. Reports are transient; cooldown/contact-counter persistence is unchanged.

Inspection uses historical decisions, rather than pretending arbitrary EOC
conditions are pure. No live advance, cancel, editing or sandbox preview is offered.

## Native producer contracts

| Channel | Coverage |
| --- | --- |
| `thrown` | Loaded avatar/NPC native throws, original detached item UID/type/categories, throw skill and exact flight origin; existing native flight/target permissions exclude children. |
| `spell` | Primary real-caster attack effects, including area victims, native magical projectile hits and explicit split/percentage body-part packets. Spell ID/level are captured; extra spells, callback casts and fake/source-less casting do not create roots. |
| `monster_special` | JSON melee/bite actors with the exact configured attack ID, actual body part, direct damage and native cost/callback ordering. Arbitrary built-in specials require separate adapters. |

All three support living and fatal primary contacts, including the existing
additional player fatal opt-in. A root retains original bodies and buffers the
first living packet per victim. Later lethal packets supply their own facts, with
one fatal opportunity per victim/action. Subsequent harm can settle ownership;
there is no invulnerability. Accepted owners become ready only after the native
effect/action returns. Exceptions unwind capture guards, discard unpublished
living packets and settle real fatal injuries.

Repeated projectiles/body parts do not multiply living chances. Spells' native
extra effects remain children even with `EXTRA_EFFECTS_FIRST`; `WONDER` does not
manufacture a primary hit from its selected child. Direct spell-effect helpers
outside the cast wrapper are unsupported. Resource payment outside the spell
effect wrapper retains its normal caller ordering.

New channels are explicit: defaults and `melee` retain their original meanings.
`spells` and `monster_attacks` are indexed matching fields. New-channel miss rules
are rejected. Missing weapon/style/critical/body-part facts are not fabricated.
The existing 64-victim, 2,048-rule and 4,096-cooldown budgets still apply.

[The optional demonstration](../../../data/mods/EXECUTIONS_DEMO/additional_triggers.json)
adds a throwing knife, spell and manually spawnable monster with rare reactions
for NPC/monster victims. It adds no production weapon/monster rules or spawn groups.

## Validation

[Inspector tests](../../../tests/execution_inspector_test.cpp) cover observational
purity, recorded draws, legacy/current saves, rejected corrupt records, actual
chance/weight reports, condition non-reevaluation, bounded history and lost roles.
[Producer tests](../../../tests/execution_expanded_trigger_test.cpp) cover real
throws/casters/specials, multiple victims and limbs, extra/callback attacks,
unsupported paths, fatal ownership/save/resume, exceptions and schema boundaries.

The final combined native-message regression passes **339 cases / 398,533
assertions**, including all 17 new inspector/producer cases and the existing
execution, trigger, narration, knockout, NPC/monster/player fatality, projectile,
spell, melee, martial-art, weakpoint, monster-attack/grab, sleep/effect and affected
audit suites. Seed `41723`, `dda`, `executions_demo` and automatically supplied
`test_data` were used, without initialization or test errors.

The headless GCC C++17 engine archive, native-message test executable and explicit
`cataclysm` game target build with warnings treated as errors. All 37 engine units
depending on changed headers were refreshed. The final game passes its `--help`
smoke check. C++/JSON formatting, new JSON translation extraction, all 75 local
documentation file links, all 21 final source/data/test hashes and staged diff
checks pass. The staged source matches the successful regression build.

The unchanged auxiliary formatter-link limitation recorded in preceding milestones
remains; the explicit game target and standalone JSON formatter succeed. This is
a selected native-message suite and a headless smoke check. The full repository
suite and interactive graphical playtesting were not run.

With a normal test build, run the dedicated cases from the repository root:

```sh
./tests/cata_test '[execution_inspector],[expanded_execution]' --mods dda,executions_demo --rng-seed 41723
```

The runner supplies `test_data`; do not add it a second time.

## Remaining expansion

Environmental and source-less reactions need an explicit single-participant or
world-source model; rebinding them to the avatar would be incorrect. Turrets,
mounted/delegated attacks, rider redirects, arbitrary built-in specials, explosions,
deferred damage over time and additional spell-effect families need their own
primary-contact and death boundaries. Offscreen execution policy, more roles,
live editing and detached sandbox preview remain separate work.
