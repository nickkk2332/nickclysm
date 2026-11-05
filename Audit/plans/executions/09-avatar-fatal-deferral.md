# Avatar fatal deferral

The supported authoring contract is [Player fatal scenes](../../../doc/JSON/EXECUTIONS.md#player-fatal-scenes).
This extends [fatal melee scenes](08-fatal-hits.md) to the player using the existing
primary-melee commit boundary, bounded graph, saved owner and explicit control claims.
It adds no new combat producer or separate physics model.

## Admission and control

Definitions require both `allow_fatal_target` and `allow_avatar_fatal_target`.
The second flag defaults to false, including on existing fatal content. Native
character/monster primary melee can select player rules with `target_kind: ["avatar"]`.
Manual EOC starts cannot claim a fatal body. Finalized players, deathcam/quitting
sessions and the existing unsupported vehicle/restraint contexts are refused.

The reservation immediately suppresses voluntary actions. At the safe graph start,
existing player work and auto-move end before an execution activity is assigned.
Cancellation callbacks run with the owner installed and are revalidated before any
graph action. The activity releases on rescue, completion, cancellation and emergencies.

The normal view follows the actual body and map through checked steps and native
landing. The wounded player retains ordinary perception for narration; existing
blindness, deafness and unconsciousness still apply. Pending, finalizing and finalized
player deaths cannot change bodies. This does not add a detached cinematic camera.

The progress UI keeps saves and information screens available while suppressing
physical input. Pause skips the remaining presentation and settles the injury.
Skipping, losing the activity or clearing the runtime does not provide a rescue.

## Native death lifecycle

The avatar owns separate injury-committed, finalizing, finalized and presentation
state. `die()` can commit the ordinary native injury once without presenting game
over. A pending scene intercepts it and retains the original injury. Scene settlement
releases ownership before entering the same guarded player finalizer used by ordinary
game-over checks.

The prevention order is `PREVENT_DEATH`, `character_dies`, then `AVATAR_DEATH`,
with a real-health check between stages. An actual rescue clears captured attribution
and leaves no corpse, final death event or kill reward. The irreversible result emits
native character kill credit and `avatar_dies` once; scene style hooks follow the
actual result. Sources are retained through callbacks and revalidated by identity.

Reentrant `die()`, finalization, game-over queries and saves cannot replay a partial
death. Presentation waits until the native loop is outside combat and scene actions.
Both the last-words screen and deathcam choice now follow all prevention stages, so
an `AVATAR_DEATH` rescue does not first display a death screen. Ordinary unselected
deaths use the same guard and retain native immediate injury handling.

Corpse placement retains normal deathcam timing and latches before native placement.
Repeated `QUIT_DIED` checks and restored finalized checkpoints cannot add another
corpse. Presentation/deathcam state also persists: the last-words event does not
replay, saved deathcam resumes watching, and a finished deathcam stays finished.
Finalization and presentation guards also block reentrant saves during corpse drops.

## Simulation, persistence and boundaries

The avatar uses its existing world-turn body/effect/field processing, while the
manager continues providing the reserved NPC/monster passive turn. It does not run
a second passive avatar turn. Incoming harm, forced motion, cancellation, timeout,
actor loss and explicit runtime clearing settle promptly. Knockback and flinging
settle before their raw-death early returns. A checked fatal fall follows native
gravity, shifts the map with the body and settles at landing.

Saves retain the avatar's injury-commit, finalized, presentation/deathcam and corpse state; the
finalizing guard is transient and refuses serialization. Old avatar saves default
the new fields to false. Pending owner/deadline restoration precedes actor/map
processing, preserving activity and completed beats. Missing/expired content and
orphaned injuries return to normal native settlement and game-over handling.

Dimension transfer settles owners before its snapshot and refuses to move a player
whose death finalized. Atomic scene/finalizer callbacks cannot save partial state;
prevention scripts should restore health immediately and schedule world transfer
for a later safe tick. Body takeover after death and further combat producers remain
separate features.

## Content and validation

[Core player knife content](../../../data/json/executions/avatar_fatal.json) reuses the
existing stagger/ledge scene. [The demo rule](../../../data/mods/EXECUTIONS_DEMO/avatar_fatal.json)
is limited to NPCs wielding the fatal demonstration knife; normal production weapons
gain no automatic rule.

[Dedicated coverage](../../../tests/execution_avatar_fatal_test.cpp) exercises native
melee, real injury/occupancy, input/activity/body-control gates, passive effects and
fields, all prevention stages, finalizer reentry, exactly-once events/corpses, save
resumption, missing/expired owners, deathcam, checked movement and landing, and
emergency world/physics paths. Restored deathcam and corpse checkpoints also retain
native message suppression.

## Validation

The final native-message regression selection passes **274 cases / 125,732 assertions**,
including all 18 dedicated player fatality cases and the existing execution, trigger,
narration, knockout, NPC/monster fatality, combat, spell, sleep/effect and affected
audit regressions. The world-loop selection includes save failures and dimension
transfer. It uses seed `41723`, `dda`, `executions_demo` and automatically supplied
`test_data`, with no initialization or test errors.

The headless GCC C++17 engine archive, native-message test executable and explicit
`cataclysm` game target build with warnings treated as errors. The game passes its
`--help` smoke check. C++/JSON formatting, translation extraction and all 86 local
documentation links pass. The 22 source/data/test files match the snapshot used by
the final regression run; staged-file hashes and `git diff --check` also pass.

The unchanged aggregate Makefile formatter-link limitation recorded in
[the preceding milestone](08-fatal-hits.md#validation) remains; the explicit game
target and working standalone JSON formatter succeed. Validation uses a selected
native-message regression suite and a headless smoke check. The full repository
suite and interactive graphical playtesting were not run.

With a normal test build, run the dedicated cases from the repository root:

```sh
./tests/cata_test '[avatar_fatal]' --mods dda,executions_demo --rng-seed 41723
```

The runner supplies `test_data`; do not add that mod a second time.
