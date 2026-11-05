# Powers milestone 4: movement, familiars and AI intent

This follows [owned applications and resources](08-owned-powers-milestone.md)
and implements the next consumer stage of the original
[expansion sequence](05-expansion-plan.md). See the
[authoring contract](../../doc/JSON/POWER_ACTORS.md).

- Attributed native fling requests retain instigator, moved identity, actual
  absolute path, collision/landing observations and final HP/velocity results.
  Direct impacts and collateral retain their source; nested environmental damage
  keeps its own semantics. Native physics still decides the motion.
- Saved independent summon bonds support avatar/NPC/monster owners, follow,
  positional guard, identity-targeted attack, selective dismissal, finite
  lifetime/leash and lifecycle hooks. Typed ownership supplies native summoner
  lookup; cleanup cannot adopt another creature at the old position.
- Declarative ability intents enable pure admission/estimate queries and stable
  best-candidate selection. Existing resource/charge policies still govern actual
  invocation. Summon turns, monster EOC specials and NPC goal EOCs can opt in.
- The demo brawler and commanded force familiar share close throws and ranged
  strikes. Four familiar focuses expose creation/dismissal and orders. Inspector
  views include movement receipts and bond identities/orders/deadlines.

## Boundaries

Fling remains a horizontal native launch followed by gravity. This does not add
arbitrary aerial trajectories, movable vehicles/props, interception hooks or a
new damage pipeline. Receipts report synchronous native outcomes (including any
callback changes), not pure damage estimates. Movement history is bounded and
unsaved. Mid-flight save/session replacement is refused.

Bonds apply to newly spawned monsters; NPC summons, adopting existing actors,
owner-resource/damage routing and automatic spell-summon adoption remain later
adapters. Finite native lifetimes and inactive saved tombstones bound unloaded
summons without pretending to run offscreen AI. Guard is a positional order;
attack follows a chosen identity and uses native movement/melee. Native specials
retain their own policies. Callback recreation is refused, not recursively pumped.

AI intent is author-supplied metadata, not inferred EOC semantics. Selection
checks resource/range/relationship/safety constraints but cannot promise release
hooks or random native attacks will succeed. Point searches and universal NPC
power planning are outside this slice.

## Validation

Final validation on 2026-11-05:

- Combined native-message suite: **461 cases / 401,011 assertions passed**,
  including all 30 new cases and the 431 existing ability, damage, resource,
  execution, knockout, fatal-hit/projectile, physical interaction, NPC sleep,
  monster attack/grab, melee, effect, martial-art, weakpoint and affected audit
  regressions. Seed 41723; `dda`, `executions_demo`, `powers_demo` and
  automatically supplied `test_data`. No initialization or test errors.
- Focused native integration: **30 cases / 499 assertions passed**. Coverage in
  [power_actor_test.cpp](../../tests/power_actor_test.cpp) includes actual
  avatar/NPC/monster flings, terrain breaking and gravity, direct/collateral
  attribution, nested environmental damage, callback removal/displacement and
  exceptions, save refusal, and cleanup deferred through legacy native flight.
  Bond cases cover two owners, selective dismissal, native pathfinding and paid
  abilities, lost-target/vision fallback, Character kill credit, finite expiry,
  leash/death, unloaded identity tombstones, malformed/recursive bonds, callback
  bounds, world transitions, and the actual demo item route. Pure query cases
  cover range/relationships, resource/charge admission, deterministic ties,
  conservative area safety, formula/EOC adapters and unchanged RNG/save state.
- Headless GCC C++17 engine archive, native-message test executable and explicit
  `cataclysm` target built with project warnings treated as errors. Final CLI
  `--help` smoke check passed. Refreshed 32 concrete consumers of the changed
  ability definition and native fling-options layout; no `game` or `Creature`
  layout change.
- All 29 changed/new C++ formatting checks, four JSON formatting/translation
  extraction checks, seven new local document links, 40 reviewed file hashes
  and staged diff checks passed. Source, fixtures and demo data stayed fixed
  throughout the passing combined run; final documentation was completed after it.
- Standard focused selection:
  `./tests/cata_test '[power_actors]' --mods dda,executions_demo,powers_demo --rng-seed 41723`.

This was a selected native regression suite and headless smoke check. The full
repository suite and interactive graphical playtesting were not run.

## Next

Persistent projectiles, beams and area membership are the next power-family
milestone. Dedicated native effect/equipment grants and richer summon routing
remain independent adapters over the existing ownership/accounting records.
