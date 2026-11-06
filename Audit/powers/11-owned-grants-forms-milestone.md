# Powers milestone 6: owned native grants and armor forms

This follows [persistent projections and areas](10-projections-areas-milestone.md).
It adds native consumers to existing independent power applications, covering
source-owned effects/equipment and the equipment-restoration portion of forms.
See the [authoring contract](../../doc/JSON/POWER_GRANTS.md).

- Optional application `grants` define fixed-intensity native effects, projected
  armor/melee equipment and an exclusive equipment form. Source-specific
  end/dispel, parent ownership, shared accounting, deadlines, interruption and
  safe-boundary callbacks use the established application runtime.
- Native effects retain an ordinary component beneath additive owned layers.
  Their individual native timers, intensity and source survive overlapping
  grants; visible caps do not destroy hidden contributions. Whole-effect cures
  remain cures and do not cause automatic reapplication. Copy-on-write snapshots
  and native serialization preserve independent ownership.
- Forms move real worn/wielded items into saved storage, retaining original and
  nested UIDs, damage, variables, contents and side. Native removal permissions
  apply. Ending returns compatible originals without overwriting new equipment;
  incompatible/occupied slots produce native ground placement. Missing targets
  cannot be replaced by another resident. Unavailable cleanup locations retain
  bounded saved storage records.
- Projected equipment keeps native armor/melee/enchantment behavior and source
  identity through normal transfers, drops, loans and loaded vehicle cargo.
  Loaded cleanup is selective; detached items expire through native processing.
  Ordinary inserted contents spill, and normal crafting/salvage/disassembly
  cannot consume projected equipment as material.
- Materialization/cleanup callbacks retain committed costs and world changes.
  Real ownership transfers precede callbacks; cleanup resumes remaining work
  without recreating already returned gear. Read-only inspector views expose
  source/target, storage, grant and return counts.
- Demo focuses request ordinary self abilities for a strength buff, projected
  blade and charged armor form, then selectively end their own source.

## Boundaries

These are equipment/stat forms, not replacement anatomies or species. Native
mutation/loadout grants, enchantment source-formula context and general form
compatibility need further adapters. Effects process one aggregate; per-source
native damage/kill attribution and alternative stacking policies remain open.
`knocked_out` uses its physiological interface rather than held grants.

The first equipment adapter supports nonconsumable armor/melee items on loaded
Characters. It excludes guns, tools, charge stacks and food. Arbitrary scripts,
native break replacements and other item-producing consumers do not
transitively inherit source ownership. Normal transfers/spilling keep their
native UID semantics; the form's explicit original-gear transfers preserve UIDs.

Grant records belong to the active character's ability-runtime save; shared NPC
form authority across different player saves remains world-level work.

Storage is outside physical inventory while a form holds it. Native item
processing runs once per observed second, with no offscreen/catch-up simulation.
Stowing uses bulk takeoff callbacks rather than inventing manual takeoff action
EOCs. New/returned clothing runs normal wear callbacks. End hooks cannot roll
back arbitrary callback changes or force an incompatible outfit onto a body.

Cancellation hides application contributions immediately; physical grants settle
at safe boundaries, before terminal observers. Existing native attacks already
in progress retain their ordinary commit policy. Worldgen, machine/prop physics,
settlement simulation and generic NPC power planning remain independent work.

## Validation

- **537 native-message cases / 401,931 assertions passed**, including all 42 new
  grant integration cases (403 assertions) and the 495 existing ability, resource,
  damage, summon/AI, projection, execution, knockout, fatal projectile, physical
  action, melee, monster/grab, NPC sleep, effect, weakpoint and audit regressions.
  Seed 41723; `dda`, `executions_demo`, `powers_demo` and automatic `test_data`.
  Exit status zero; no initialization or test errors.
- New coverage includes additive/capped native effects, ordinary reapplication
  and decay, whole-effect cures, bodyparts and copy-on-write snapshots; actual
  avatar/NPC/monster participants; native armor; exact original/nested item
  identities through a real avatar/runtime save; outfit conflicts and removal
  locks; force-unarmed styles; selective dropped, copied, loaned and vehicle
  equipment cleanup; ordinary cargo; source loss, upkeep and world transitions;
  parent cleanup; committed callback exceptions; unavailable/replaced targets;
  malformed saves; pure inspector/admission queries; and the paid demo ability.
- Headless GCC C++17 archive, native-message test executable and `cataclysm`
  built with warnings treated as errors. All 386 identified consumers of the
  changed effect/application layouts and all 45 selected native test units were
  refreshed. CLI `--help` passed. The full repository suite and interactive
  graphical playtesting were not run.
- All 25 changed/new C++ formatting checks, three JSON format/translation
  extractions and five new documentation links passed. The 28 implementation,
  test and data file hashes remained fixed throughout the passing focused and
  combined runs; final documentation was completed afterward.

Coverage is in [power_grant_test.cpp](../../tests/power_grant_test.cpp). Reviewer
command: `./tests/cata_test '[power_grants]' --mods dda,executions_demo,powers_demo --rng-seed 41723`.

## Next

Source-owned mutation/loadout adapters and fuller transformations can build on
this storage/lifetime contract. Temporary constructs with selective restoration,
telekinetic objects/vehicles, richer shape/attack integration and selected time
policies remain later power families.
