# Twelve correctness fixes

This follow-up implements the four concrete bugs in each of the [powers](powers/04-authoring-and-correctness.md), [interactivity](interactivity/05-authoring-and-observations.md), and [worldgen](worldgen/07-authoring-correctness.md) audits. The original reports describe commit `b32c9baea25f62478228c6a91cbbf9f356e4e8a3`; their pinned evidence remains historical. The fixes below are in the working tree.

| Audit ID | Corrected behavior | Regression coverage |
| --- | --- | --- |
| Powers BUG-01 | Damage always reaches the selected receiver. Optional `source`: `u`, `npc`, or environmental `none` (default), independently controls attribution. An invalid explicit source fails rather than changing attribution. | Distinct Character/monster receivers; unchanged attacker HP; damage-type EOC source; beta source, environmental damage and fatal-hit killer attribution. |
| Powers BUG-02 | Cancelled/empty targeted selection runs only `false_eocs`, with `reason = target_cancelled`, and produces no cast effect/message. | Successful self-target and deterministic empty random-target selection, checking cast and callback counts. |
| Powers BUG-03 | `NON_MAGICAL` bypasses `NO_SPELLCASTING` and resistance, while ordinary eligibility still checks energy, components, caster condition, verbal requirements and other restrictions. | Energy for avatar/NPC; failing caster condition; missing/present components; nonmagical versus magical casting with `NO_SPELLCASTING`. |
| Powers BUG-04 | Dash uses actor movement for avatars, NPCs and monsters, stops on blockers/displacement/death, and attacks from the reached location. | Clear path, wall, occupied tile, vehicle, displacing trap, reached-origin attack and self-target. |
| Interactivity OBS-01 | Drop/placement callbacks receive the supplied actual Character or no actor. Item mutation still occurs before insertion/merging; the item's position refers to its destination. A zero/cancelled callback result preserves the item; explicit consumption removes it. Normal drop, inventory overflow/setdown and projectile landing supply the responsible Character. | Avatar/NPC/environmental drop and NPC projectile landing; actor, item variable, destination, quantity, explicit consumption and safety after a ground-item EOC removes its original item. |
| Interactivity OBS-02 | Examine dispatch identifies its actual terrain/furniture definition, even when terrain is explicitly examined under furniture. Context includes `this`, `terrain`, `furniture`, `examined_layer` and `pos`. | Avatar/NPC examination of bare terrain, furniture and terrain under furniture. |
| Interactivity OBS-03 | Falling-item impact damage is applied before the destination item is inserted/merged. Destroyed items spill contents; zero-mass falls avoid division by zero. | Persisted impact damage, zero-mass item, charge-stack merging, destruction of an already broken item, and preservation of a destroyed container's contents. |
| Interactivity OBS-04 | Emissions evaluate from the supplied creature/item/vehicle/EOC context. Environmental emissions have no avatar fallback. All get `emit_location`, `terrain` and `furniture`. Optional-actor variable reads can use their defaults without invalid-actor diagnostics. | Environmental, NPC, monster, beta EOC emitter and independent item state. |
| Worldgen OBS-01 | OMT exit dispatch invokes the terrain being left. | A→B, A→no-exit terrain, then B→A; exactly the source callback runs. |
| Worldgen OBS-02 | Unexplored-only mutable placement checks the root and every piece of each expansion candidate. Existing connection protection remains in force. | Protected non-origin piece of a multi-piece chunk; the entire candidate is rejected; explicit unprotected placement still works. |
| Worldgen OBS-03 | Map-extra override deletion reads `delete`, independently of `extend`. | Delete-only and simultaneous extend/delete overrides preserve the expected flag set. |
| Worldgen OBS-04 | NPC mapgen retains the requested three-dimensional position. The two-dimensional API remains available to existing callers. | Relative Z=-1 and Z=+1 placements and NPC serialization/deserialization. |

## Validation

The engine library and selected test executable build successfully with GCC 14, warnings treated as errors, and `TILES=0 SOUND=0 LOCALIZE=0 BACKTRACE=0 RELEASE=1 OPTLEVEL=-O0 PCH=1`. Header consumers were explicitly rebuilt because the local compiler's PCH dependency output omitted project headers.

Validation completed with RNG seed `120012` and lexicographic order:

- **195 cases / 10,382 assertions passed** in the broader selection: the first 19 new correctness cases plus 176 existing cases covering spells, EOCs, martial arts, NPC casting/errands, map fields/items, examination, projectiles, mapgen, overmap placement and JSON overrides.
- After the final item-lifetime guard, **21 cases / 332 assertions passed** against the final library: all **20 new correctness cases** plus the existing worn-item removal regression. The additional case verifies that a ground item removed by its use EOC is not consumed again through an invalid reference.
- JSON formatting, `git diff --check`, fixture identifier uniqueness and the fix-record links passed.

The standard test executable was refreshed with the final library and all 196 selected cases. This is a focused selection, not the full repository test suite. No tiles build or full save/reload integration suite was run; the NPC test round-trips NPC serialization and exercises mapgen's normal cleanup/loading path.

```sh
# Broader selection (before the final item-lifetime guard):
tests/cata_test --rng-seed 120012 --order lex
# Re-run the final focused filter with the refreshed executable:
tests/cata_test '[powers],[interactivity],[worldgen],EOC_wear_event_removes_the_garment' --rng-seed 120012 --order lex
```

The final focused run used a separately linked executable with the same final library and test objects; the refreshed standard executable includes that additional lifetime case.

## Scope and compatibility

These are corrections to existing behavior, not implementation of the audits' expansion roadmaps or the executions runtime. The JSON documentation describes the explicit damage source and updated callback/emission context. Content relying on implicit self-attribution, avatar variables in unattended emissions/drops, or successful callbacks after target cancellation must use the corrected contracts.

Mutable special placement retains its existing incremental layout algorithm: an obstructed required join can still leave a partial special and emit its existing diagnostic. This fix prevents protected generated OMTs from being used by any candidate piece. Atomic footprint planning, rollback and structured partial-placement results remain the separate GEN-05 proposal.

Tests and fixtures: [powers](../tests/audit_powers_correctness_test.cpp), [interactivity](../tests/audit_interactivity_correctness_test.cpp), [worldgen](../tests/audit_worldgen_correctness_test.cpp), [TEST_DATA](../data/mods/TEST_DATA/audit_twelve_fixes.json).
