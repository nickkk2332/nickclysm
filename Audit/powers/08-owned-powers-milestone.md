# Powers expansion: resources, charges and owned applications

This follows [damage transactions](07-damage-transactions-milestone.md). The
milestone adds shared accounting, source-aware runtime applications and sustained
instances around the existing ability/effect engine. The original pinned audit
sequence remains historical; its reference/scheduler and damage work was grouped
into the preceding two implementation milestones.

The [authoring contract](../../doc/JSON/POWER_OWNERSHIP.md) provides:

- `power_resource`: named independent pools for avatars, NPCs and monsters, fixed
  capacity/initial values, arithmetic regeneration, role support and explicit
  refusal. Native mana/avatar stamina adapt through the same service. Native
  NPC stamina remains synthetic; custom pools provide a usable NPC power backend.
- Optional ability energy reservations and atomic callback-free release
  accounting shared with shields and sustained upkeep. Claims are distinct
  from payment and cannot be spent again by another managed consumer.
- `power_cooldown`: shared per-actor groups, sequential charge recharge,
  separate minimum intervals, explicit charge recovery and pure projections.
- `power_application`: saved independent owner/target handles, magnitudes, tags,
  owned locals, start/upkeep costs, interval/expiry, end reasons and lifecycle
  EOCs. Sustained instances allow actors to continue taking ordinary actions.
- Exact-handle/source/tag dispelling, owned child cleanup, interruption policies,
  safe callback revalidation, bounded ancestry and prevention of terminal-hook
  recreation during world transitions.
- Pure formula queries and `damage_rule.required_application` to compose live
  contributions without deleting shared native effect IDs. Inspector views show
  claims, actual payments, projections, lifecycle state and bounded receipts.
- Actor-bound persistence, old-save support, saved reservation ownership checks,
  avatar resources preserved across dimension snapshots and finalized actor
  account retirement deferred safely from callback dispatch.

The acceptance demonstration maintains a force ward and force mirror at once,
ends only the ward, and recovers one shared strike charge across save/load.
Additional coverage applies the same ward from two owners: ending one preserves
the other's strength in an actual native damage packet.

## Ownership boundary

The owned contribution is an application record, its locals and child instances.
Native effects/enchantment grants, items, summons and terrain changes made by
arbitrary EOCs retain their native semantics. They do not automatically become
source-aware contributions or roll back on ending. Damage rules and formulas
consume application membership/magnitude directly; independent native grants
still need dedicated ownership adapters.

Resource/charge clocks advance arithmetically through time jumps. Sustained
world work does not: missed seconds end the instance rather than replaying a
large stream of costs, summons or callbacks. Reservations coordinate the new
service's consumers; unrelated native resource assignments can invalidate a
claim. Explicit policies and receipts expose those outcomes.

## Validation

Native integration coverage is in
[power_ownership_test.cpp](../../tests/power_ownership_test.cpp). It covers actor
pool separation, regeneration/projection purity, reservation/shield sharing,
real/native drains and cancellation, charged and free invocation policies,
shared/sequential cooldowns, exact charge recovery, two simultaneous maintained
powers, two sources of one ward, start/upkeep/expiry, save/load, recursive child
cleanup, interruption, role removal, exceptions/partial-save refusal, world
transition cleanup, source-specific EOCs, pure inspection, dimension snapshots,
bounded receipts/history, malformed state, legacy saves and death retirement.

Final validation on 2026-11-05:

- Combined native-message regression: **431 cases / 400,512 assertions passed**,
  including all 36 new resource/ownership cases and the 395 existing ability,
  damage, execution, knockout, fatal-hit, projectile, combat, effect, NPC sleep,
  monster attack/grab, weakpoint and affected audit cases. Seed 41723; `dda`,
  `executions_demo`, `powers_demo`, and automatically supplied `test_data`.
  No initialization or test errors.
- Focused native run: 34 cases / 692 assertions passed, including the actual
  ward/mirror item path and charged-strike recovery. The two final reservation
  ownership/parent-failure guards passed separately (25 assertions), then all
  36 passed together in the combined run.
- Headless GCC C++17 engine archive, native-message test executable and explicit
  `cataclysm` target built with project warnings treated as errors; `--help`
  smoke check passed. Refreshed 26 translation units consuming the changed
  public definitions/views; no `game`, `Creature` or `spell` layout changes.
- All 26 changed/new C++ formatting checks, four JSON formatting checks and
  translation extractions, six new local document links, 38 reviewed file
  hashes and staged diff checks passed. Source/data stayed frozen throughout
  the passing combined run; this validation record was completed afterward.
- Focused selection for a standard test build:
  `./tests/cata_test '[power_ownership]' --mods dda,executions_demo,powers_demo --rng-seed 41723`.

This was a selected native-message regression suite and headless smoke check.
The full repository suite and interactive graphical playtesting were not run.

## Remaining expansion work

Dedicated source-aware native effect/enchantment/item grants, a native NPC
fatigue backend, resource adapters for item charges/bionics, generic reservation
priority/refund policy across external native spenders, and offscreen sustained
simulation remain separate work. The next power-family consumers are movement
attribution/results, summon ownership/commands and AI intent queries; persistent
projectiles, beams, areas, constructs, forms and selective time policies follow
those foundations.
