# Authoring, performance, and current source observations

**Implementation follow-up:** The four concrete correctness observations below have been addressed in the working tree. See [the twelve-fix record](../correctness-fixes.md) for behavior, regression coverage, validation and remaining architectural scope. The original snapshot/evidence below is retained.

## TOOL-01 — Authoring needs integrated inspection and semantic validation

**Priority:** P1 · **Effort:** M

**Current evidence.** Existing tools already include map/overmap editing, parameter inspection, map-extra testing, EOC tracing/queue inspection, NPC goal debugging, and repeated generation tests. Tests cover weighted updates and same-RNG-state overmap determinism. These are important foundations. The gap is inspecting the relationship between a generated instance, its places/residents/jobs/events, and its saved or projected state. [Parameter inspection][params-doc], [NPC debugging][npc-behavior], [mapgen regression tests][test-mapgen], [placement tests][test-overmap], [determinism test][test-determinism].

**Content limit.** A mod author needs to know why a town lacks a clinic, why its doctor never arrives, why a raid was rejected, and why an update did not apply. A schema-valid ID does not establish reachable service access, sufficient capacity, correct scope, or a terminating schedule.

**Workaround.** Add diagnostic EOCs/messages and inspect variables, maps, and individual NPC goals manually.

**Proposal.** Extend existing debug views with an instance inspector linking generation decisions, settlement members/places, agenda occurrence, work claims, incident phases, timers, and result history. Add bounded seed sweeps for semantic constraints and dry-run traces for selection/update planning. Validate referenced roles, impossible time windows, cycles, absent cleanup policies, and unsupported offscreen effects. Report the responsible definition/mod and execution context. New schemas should arrive with executable examples and precise context documentation.

## TOOL-02 — Large populations and histories need explicit processing budgets

**Priority:** P0 · **Effort:** M–L

**Current evidence.** Existing systems already throttle or cache selected work: NPC body updates are periodic, load catch-up is coarse/capped, behavior scores can use `evaluate_every`, and job search has explicit paths. EOC processing snapshots due work before running it, so an immediately requeued effect waits for another pass. These are useful safeguards, not a complete budget policy for thousands of residents, event candidates, distant maps, and historical transitions. [Body/load processing][npc-catchup], [behavior caching][npc-behavior], [EOC processing][eoc-due], [job dispatch][npc-jobs].

**Content limit.** A living world cannot require per-turn scans of every resident or generation of every distant target map. Unbounded catch-up, incident retries, and per-worker inventory/path searches can make a seemingly simple content addition expensive.

**Workaround.** Limit populations, keep simulations local, use longer intervals, cache stable queries, and make offscreen content explicitly abstract.

**Proposal.** Schedule meaningful next transitions rather than polling every turn. Bound work per update, target search, generated area, and catch-up batch. Add indexes for settlement membership, semantic places, offered jobs, and event scope. Make deferred work observable and use fair queues so a busy town cannot starve others. Establish retention/compaction rules for finished instances and history. Measure work counts as well as wall time; content authors need predictable scaling limits.

## Specific source observations

These are four narrow observations at the pinned revision, distinct from the architectural proposals. They were traced statically; no runtime reproduction or fix was performed. Some overlap older audit findings and are retained here because they directly affect the requested content.

### OBS-01 — OMT exit dispatch checks the old terrain but invokes the new terrain

**Priority:** P1 · **Effort:** S

In `cata_event_dispatch::avatar_moves`, crossing an OMT boundary checks `past_ter->get_exit_EOC()`, then obtains the EOC from `cur_ter->get_exit_EOC()`. The intended callback of the place being left is therefore not selected. If both sides have callbacks, the destination's exit callback is selected; if only the source has one, the selected ID is null. [Movement dispatch][omt-cross].

**Impact:** Leaving an arena, town, hazard, or quest area cannot reliably use the documented exit hook. Entry-only scripts can approximate selected transitions but must track prior membership themselves.

**Proposed correction:** Invoke the old terrain's exit EOC. Separately add old/new OMT, terrain IDs, dimension, and movement cause to both callbacks. Check callback references during validation.

**Future regression:** Cross A→B with different exit markers, then A→a terrain without an exit callback. A's callback runs exactly once in both cases, and B's exit callback runs only when leaving B.

### OBS-02 — Mutable special expansion does not fully honor unexplored-only placement

**Priority:** P1 · **Effort:** M

`overmap::can_place_special` checks generated submaps for required starting locations when `must_be_unexplored` is set. Mutable placement receives the flag, but contains `TODO: respect must_be_unexplored`; its later candidate checks inspect bounds, terrain locations, and joins rather than testing every expanded piece against generated detailed maps. The flag is used for connection building, which does not make the entire mutable footprint protected. [Initial check][special-check], [mutable placement][mutable-place].

**Impact:** Runtime placement advertised as unexplored-only cannot be assumed to exclude every generated OMT reached by mutable expansion. This can create overmap/detail disagreement around existing places. The observation does not imply that every placement overwrites detailed submaps immediately.

**Proposed correction:** Propagate the policy into every candidate piece and connection, preflight the footprint where possible, and return explicit blocked/partial outcomes. Keep forced placement an explicit separate policy.

**Future regression:** Reserve a generated OMT adjacent to an eligible root, force a layout that would expand into it, and verify that protected terrain/detail state remains unchanged or that placement cleanly reports failure.

### OBS-03 — Map-extra flag deletion reads the `extend` object

**Priority:** P2 · **Effort:** S

In `map_extra::load`, the `delete` branch calls `jo.get_object("extend")`, then erases the flags found there. The branch does not read the requested deletion object. [Loader][extra-load].

**Impact:** An override that intends to remove flags used by regional feature filtering cannot reliably perform that removal. A delete-only override may try to read a missing object; an override with both objects can erase the wrong flags.

**Proposed correction:** Read `delete`, preserving the intended override semantics. This is a local loader correction, independent of the event-director proposal.

**Future regression:** Load a base extra and overrides with delete-only and extend-plus-delete flags; check the final set and region eligibility.

### OBS-04 — Mapgen NPC placement discards the requested relative Z

**Priority:** P1 · **Effort:** S–M

The NPC placement piece computes `dst` using the mapgen Z plus the requested relative Z, then calls `dat.m.place_npc(dst.xy(), chosen_id)`. Its own comment says to make `place_npc` 3D-aware. The requested Z is therefore not passed through this placement call even though the mapgen documentation lists relative Z support. [Placement implementation][npc-spawn], [documented fields][npc-spawn-doc].

**Impact:** A single multi-level placement cannot rely on this path to put residents on the requested alternate floor. Subsequent map cleanup checks using the computed destination do not supply the omitted placement coordinate.

**Workaround:** Place NPCs from the mapgen for their actual target level, or move them through a separately verified script.

**Proposed correction:** Give NPC placement a typed 3D destination and preserve it through map/overmap registration and materialization. Ensure mission targeting and unique-ID handling still refer to the resulting NPC.

**Future regression:** Generate a multi-level site with otherwise identical placements at relative Z=0 and Z=1; verify actual saved NPC positions before and after reload.

<!-- Source references -->

[eoc-due]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/effect_on_condition.cpp#L289
[extra-load]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/map_extras.cpp#L1347
[mutable-place]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/overmap_special_mutable.cpp#L782
[npc-behavior]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/NPC_BEHAVIOR.md#L1
[npc-catchup]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npc.cpp#L3697
[npc-jobs]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npcmove.cpp#L4185
[npc-spawn]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/mapgen.cpp#L2059
[npc-spawn-doc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/MAPGEN.md#L696
[omt-cross]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/game.cpp#L11805
[params-doc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/MAPGEN.md#L1317
[special-check]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/overmap.cpp#L2961
[test-determinism]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/tests/overmap_test.cpp#L1030
[test-mapgen]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/tests/audit_mapgen_test.cpp#L73
[test-overmap]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/tests/overmap_test.cpp#L323
