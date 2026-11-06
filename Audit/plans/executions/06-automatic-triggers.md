# Automatic melee trigger milestone

This follow-up adds opt-in `execution_trigger` JSON definitions over immutable native
primary-contact facts. The supported schema and producer limits are in
[EXECUTIONS.md](../../../doc/JSON/EXECUTIONS.md#automatic-melee-rules).

Delivered behavior:

- Ordinary avatar/NPC primary melee and standard monster primary melee publish after
  their callbacks and death checks. Reach attacks use the character producer when a
  real contact reaches that path. No rule in core data changes ordinary weapons.
- The packet binds durable participants and captures original item UID/type/categories/
  skill, chosen style/technique, hit/available critical result, actual body part, stable
  weakpoint ID, reported damage types, separately measured primary HP loss, impact
  positions, attack direction, and terrain/furniture.
- Root/primary-hit/damage scopes suppress nested, counter, mutation and execution
  attacks. A primary HP-write token is consumed before damage-event callbacks, so
  nested damage and healing cannot replace that value. The scopes do not change the
  layout or meaning of `dealt_damage_instance`.
- Channel and selective-ID indexes limit candidate work. Priority tiers, per-rule
  chance, weighted arbitration and a contact dedup cache queue at most one automatic
  scene. Normal start validation has a dry-run path with no RNG, IDs or claims.
- Actor, target, ordered-pair and world cooldowns are saved independently of scenes,
  include dimension and rule identity, and charge only accepted requests.
- The latest bounded diagnostic report can be inspected from engine code without
  repeating any selection work. The [inspector follow-up](12-inspector-and-additional-triggers.md) adds its read-only UI.
- The optional demo knife uses an automatic rule instead of its former explicit
  technique EOC request. Manual EOC starts remain available.

## Deliberate limits

This closes the first indexed living-melee slice of Phase 0/Phase 5, rather than the
full combat-producer matrix. The [projectile follow-up](10-projectile-triggers.md)
adds nonfatal handheld firing. [Throws, direct spell attacks and JSON melee specials](12-inspector-and-additional-triggers.md) are also supported. Environmental damage,
monster specials that bypass standard melee, and mounted delegation/rider redirects
need additional adapters and causality contracts. The [fatal melee follow-up](08-fatal-hits.md)
adds a separate commit-time stage for NPC/monster fatal victims on these primary-melee
producers. The [fatal projectile follow-up](11-fatal-projectiles-physical-clashes.md)
adds commit-time handheld firing admission. Unsupported events remain rejected.

The one-contact guarantee applies to automatic rules. An author can still explicitly
queue independent free-role scenes through EOCs. Existing ownership requirements
decide whether manual and automatic requests can coexist.

Cooldowns have a 4,096-key budget. Exhaustion refuses new keys until entries expire;
it does not evict live cooldowns. Trigger definitions are limited to 2,048, filter
lists to 64 values, per-contact diagnostic candidates to 64, and the in-session dedup
cache to 128 IDs. The definition budget includes array IDs and deferred definitions.
Contact IDs advance across saves; loading does not replay contacts.
These limits keep state bounded without claiming an unlimited event history.

Real knockout/incapacity is delivered by the [knockout follow-up](07-knockouts.md).
Fatal primary-melee reservation/finalization is delivered by the fatal follow-ups.
The fatal projectile/physical follow-up also supplies shove, fling and native melee.
The inspector follow-up delivers a read-only transcript/trigger view. Additional physical verbs and sandbox preview remain separate milestones.

## Validation

Regression coverage is in [execution_trigger_test.cpp](../../../tests/execution_trigger_test.cpp),
with gated definitions in the test-data mod. It exercises real character/monster
contacts, immutable weapon/weakpoint facts, nested damage/healing/attacks, unavailable
participants, priority/weight/chance behavior, deduplication, pure preflight/inspection,
cooldown scope/save/expiry, refusals without cooldown charges, and the array-ID
definition budget.

- Headless GCC C++17 engine and game executable build with warnings treated as errors;
  command-line help smoke check passes.
- Native-message regression suite: **104 cases, 108,550 assertions passed**, including
  execution/narration, audited fixes, projectiles, melee, weakpoints, martial arts and
  dodge behavior.
- Final focused trigger suite after the definition-budget guard: **15 cases, 963
  assertions passed**, without initialization or test errors.
- C++/JSON formatting, translation extraction, documentation links and diff checks pass.
- Seed `41723`; the test runner supplies `test_data` automatically when invoked with
  `--mods dda,executions_demo`.

The supported trigger cases can be run with a normal test build:

```sh
./tests/cata_test '[execution_triggers]' --mods dda,executions_demo --rng-seed 41723
```
