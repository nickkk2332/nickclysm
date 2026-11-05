# Powers milestone 5: persistent projections and areas

This follows [movement, familiars and AI intent](09-movement-summons-milestone.md).
The new opt-in `power_projection` family addresses COMBAT-03 and the core geometry
and membership portion of COMBAT-06. See the
[authoring contract](../../doc/JSON/POWER_PROJECTIONS.md).

- Saved travelling projectiles and charged/pulsing beams retain real typed
  owner/target identities, absolute geometry, capacity, deadline, cost totals,
  declared locals and bounded ancestry. Projectiles collide with terrain and
  bodies; tracked targets cannot silently become another creature.
- Hostile beams/projectiles and intercepting areas contest finite strength before
  damage. Equal capacities cancel both; a survivor retains the difference and
  proportionate payload. Crossing bolts check every traversed tile. Resolution
  order is stable and consumes no RNG.
- Independent fixed or owner-following areas observe enter/stay/exit membership.
  Native damage-rule gates and pure area-strength queries provide overlapping
  source-specific aura defenses without installing indistinguishable effects.
  Dispelling one source preserves others.
- Payloads use native typed damage and actual instigators, integrating armor,
  shields, reflection and existing damage events/kill credit. Shared accounting
  respects reservations for admission and pulse upkeep.
- Safe-boundary lifecycle hooks, source-application binding, callback bounds,
  once-only cleanup attempts and committed-outcome exception handling reuse the
  earlier lifecycle contracts. Identity/deadline/membership saves do not replay
  hooks, costs or overdue hits.
- Inspector views expose objects, geometry, membership, clocks, strength,
  ancestry, paid amounts and contact receipts. Visible map overlays distinguish
  charging and active shapes; they do not create native fields or lighting.
- Demo focuses author a travelling force bolt, interruptible beam, independent
  defensive domain and intercepting bubble. A native monster special selects the
  same paid bolt/beam abilities.

## Boundaries and remaining work

These are new persistent power objects. Existing gun bullets and instantaneous
spell projectiles retain their native resolution. The new direct-contact packets
have no automatic hit/dodge contest or primary melee/gun execution channel;
explicit EOC execution requests remain available. Universal spell/gun adapters
need separate policies and compatibility tests.

Bolts/beams select horizontal paths. Areas occupy one flat Chebyshev footprint,
optionally terrain-LOS clipped, and reconcile at safe action pumps rather than
continuous swept movement. Shapes do not bash terrain or install native effects,
items, light, sound or temporary terrain automatically; hooks can author explicit
world changes. Arbitrary hook side effects are not rolled back or selectively
restored by area deletion. Dedicated effect/equipment grant ownership and
construct restoration remain open.

Contests use scalar strength subtraction, not full material/energy physics or
beam pushing. Hostility comes from native actor attitudes. Area/area pairs never
contest; allies coexist. There is no arbitrary shape language or point-search AI.
Intent metadata can select enclosing abilities, but general NPC power planning
and impact-aware native accuracy remain separate.

Only loaded, living owners are supported. No offscreen projectile/owner
simulation or time-jump catch-up occurs. Membership/pulse target lists cap at 64
identities, active objects at 128, family requests at 64 and depth at eight.
Ancestry is recursion accounting; only explicit source application binding ties
projection descendants to a shared owned lifetime.

## Validation

Final validation on 2026-11-05:

- **495 native-message cases / 401,528 assertions passed**, including all 34 new
  projection integration cases and the 461 existing ability, damage, ownership,
  summon/AI, execution, knockout, fatal-projectile, physical interaction, melee,
  monster/grab, NPC sleep, effect, weakpoint and affected audit regressions.
  Seed 41723; `dda`, `executions_demo`, `powers_demo` and automatically supplied
  `test_data`. Exit status zero; no initialization or test errors.
- New cases in [power_projection_test.cpp](../../tests/power_projection_test.cpp)
  cover actual avatar/NPC/monster sources, native typed damage/shields, walls and
  body blockers, tracked identity removal/replacement, charge/pulse save clocks,
  damage interruption, equal/unequal beam contests, crossing bolts/interceptors,
  allied coexistence, independent enter/stay/exit and overlapping defenses,
  moving areas including an upstairs save/load, source-application cancellation,
  reservation-aware upkeep, time skips, owner/session loss, committed damage and
  callback exceptions, once-only terminal cleanup, bounded recursion, malformed
  saves, exact EOC handles, pure math/render/inspector reads and actual demo items
  plus the ordinary paid ability route.
- Headless GCC C++17 engine archive, native-message test executable and explicit
  `cataclysm` target built with project warnings treated as errors. CLI `--help`
  passed. Refreshed all three concrete engine consumers of the changed damage
  rule layout; its test consumer also rebuilt. No `game` or `Creature` layout
  change.
- All 22 changed/new C++ formatting checks, three JSON format and translation
  extraction checks, Python compilation, seven new local document links and
  staged diff checks passed. Reviewed hashes cover 33 changed/new files.
  Implementation, fixtures and demo data stayed fixed throughout the passing
  combined run; final documentation was completed afterward.
- Standard focused selection:
  `./tests/cata_test '[power_projections]' --mods dda,executions_demo,powers_demo --rng-seed 41723`.

This was a selected native regression suite and headless smoke check. The full
repository suite and interactive graphical playtesting were not run. Render-data
queries are tested for purity; graphical appearance has not been playtested.

## Next

Owned native effect/equipment grants and transformations are a useful next
consumer: two independent sources must stack and dispel predictably, with gear
restoration and source-aware modifiers. Shape customization and native attack
adapters can then build on the saved-object contract without coupling their
compatibility rules to this milestone.
