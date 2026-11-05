# Fatal melee execution milestone

The supported schema is documented in [EXECUTIONS.md](../../../doc/JSON/EXECUTIONS.md#fatal-hit-rules).

This milestone reserves final death resolution for a selected NPC/monster primary
melee hit. It preserves the actual fatal injury while the saved sequence performs
its short graph. Avatar fatal deferral, other combat producers and a medical injury
model remain separate work.

## Admission and ordering

`fatal_hit` rules use the existing indexed priority/chance/weight arbitration and
saved cooldowns. Selection occurs at the primary HP write, before native damage-event
callbacks and finalizers, with the contact's immutable commit-time facts. Only an
accepted rule creates a reservation; unmatched/refused hits retain immediate native
death. Graph activation waits until the outer attack returns. A failed fatal-stage
selection does not re-arbitrate the contact as a living hit.

Definitions opt in with `allow_fatal_target`, controlled targets and a maximum of
30 game turns. The actor must be alive. Missing/finalized/fake/hallucination victims,
avatar fatal targets, NPCs already marked for death, unavailable identities and unsupported control contexts are
refused. A manual start cannot reserve an arbitrary fatal actor or a corpse.

## Lifecycle adapters

| Path | Integration |
| --- | --- |
| Primary contact | Damage scopes arm actual body-part/type facts; the positive HP write admits fatal rules before damage callbacks. Character critical state is captured before damage. |
| Death state | Raw fatal health and ordinary `is_dead()` remain truthful. Explicit finalized-state accessors distinguish completed NPC/monster death. |
| Occupancy and identity | Tracker position/UID lookup and NPC occupancy retain reserved victims; ordinary creature references still exclude fatal health, with a narrow execution lookup for reservations. |
| Normal activity | Living actor ranges exclude reservations, including a healed victim awaiting settlement. Native movement, combat, casting, specials and defensive dispatch suppress voluntary actions. |
| Passive simulation | The manager runs the reserved victim's passive native turn and fields once per game turn, preserving effects, damage-over-time, inventory processing and environmental harm without ordinary AI. |
| Checked movement | A scripted victim can take checked flat steps despite fatal health. Supported native landing is a terminal settlement checkpoint, with the corpse created at the landing tile. |
| External harm | Positive damage requests settle before raw-fatal early returns. Direct death, external motion/physics and unsupported transformations also settle. |
| Projectile/explosion adapters | Pending victims cannot use projectile evasion. Shrapnel still tests real fragment contacts; shockwaves settle pending victims before ordinary living-actor iteration. Blast damage uses retained tile occupancy. |
| Cleanup/removal | Dead cleanup retains reservations; explicit tracker/NPC removal, clearing and unload settle while the participants still exist. Finalized monsters are not stored as living overmap residents on despawn. |
| Terminal callbacks | The scene latches settlement, releases the reservation, invokes native death/prevention, records the actual result, releases control and then runs lifecycle callbacks. |
| Saving/loading | The owner, deadline and deferred kill context are saved together. Restore installs reservations before map/actor processing and validates at the first safe pump. Replacing a session discards the old runtime without adding death side effects to the restored checkpoint. |

No HP-to-one patch, resurrection or ongoing damage shield implements this feature.
The normal native finalizer still creates corpses/drops, emits death events, awards
kill credit and handles faction/death functions. Reentrant NPC prevention is guarded
before its finalized flag is set. Real restored health or normal NPC prevention can
rescue the victim; cancellation and timeout do not provide a rescue themselves.

## Attribution and combat callbacks

The scene retains native participant lifetimes during callbacks and validates the
original source identity before settlement. It never substitutes the current avatar
for a missing source or credits a replacement body. A genuine rescue clears the
captured killer pointer, preventing a later unrelated death from inheriting it.
Unavailable original sources do not acquire a fabricated replacement attribution.

Post-hit martial-art context reports `killed: 0`, `death_pending: 1` while reserved.
The original style's on-kill context is saved separately and dispatched once after
actual native finalization if the character source remains alive. Native death/kill
events retain their own ordering. Without a selected scene, ordinary hit/kill ordering
is preserved, apart from the added state facts.

The final outcome is `killed`, `rescued` or `unavailable`; the authored end label is
retained separately. Read-only `u_death_pending` / `npc_death_pending` queries expose
the actual state without avatar fallback or RNG draws.

## Content

[The core fatal knife scene](../../../data/json/executions/fatal_knife.json) reuses the
existing stagger, ledge chance and wounded-message graph with fatal admission enabled.
[The optional demo knife](../../../data/mods/EXECUTIONS_DEMO/fatal_knife.json) has a
rare fatal primary-melee trigger. Ordinary production weapons gain no automatic rule.
Execution definitions now finalize deferred `copy-from` entries before trigger
validation, so the fatal core scene can inherit the existing graph regardless of file order.
Bulk character HP writers also invalidate cached fatal state without introducing
per-limb damage events, allowing genuine health restoration to be observed reliably.

## Validation

[Focused coverage](../../../tests/execution_fatal_test.cpp) exercises actual melee
producers, commit-time callback ordering, retained occupancy, checked motion and
landing, exactly-once death/rewards/corpses, prevention and rescue, source identity,
interruption/removal, saved resumption, expiry, EOC queries and definition bounds.
The final native-message regression selection passes **256 cases / 125,071 assertions**,
including all 28 dedicated fatal-hit and related projectile cases, the existing
execution/trigger/narration/knockout tests, combat, spells, sleep/effects and affected
audit regressions. The world-loop selection covers save failures and dimension-transfer
preflight. It uses seed `41723`, `dda`, `executions_demo` and automatically supplied
`test_data`, without initialization or test errors.

Coverage includes projectile evasion, shrapnel-only explosions, blast and shockwave
paths for NPCs and monsters, plus enchanted projectile evasion while unconscious for
all three actor kinds. The preceding focused run passed 26 cases / 830 assertions
before those final two cases were added.

The headless GCC C++17 engine archive, native-message test executable and explicit
`cataclysm` game target build with warnings treated as errors. The final game passes
its `--help` smoke check. C++/JSON formatting, translation extraction and local
documentation-link checks pass.
The 47 source/data/test files match the snapshot used by the successful final run;
staged-file hashes and `git diff --check` also pass.

The aggregate default Makefile target also attempts the auxiliary JSON formatter;
its unchanged link recipe fails on an unresolved `replace_substring`. The explicit
game target succeeds, and the working standalone formatter validates the new JSON.
The regression selection is a targeted native-message suite, not the entire repository
test suite or an interactive graphical smoke test.

With a normal test build, run the dedicated cases from the repository root:

```sh
./tests/cata_test '[fatal_execution]' --mods dda,executions_demo --rng-seed 41723
```

The runner supplies `test_data`; do not add that mod a second time.
