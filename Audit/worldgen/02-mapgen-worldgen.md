# Map generation and world generation

The current tools are sufficient for varied buildings, procedural complexes, unusual regions, and custom dimensions. The expansion opportunity is to give those generators shared context, stronger layout guarantees, and safe ways to evolve existing places.

## GEN-01 — Generation EOCs lack a shared location/instance context and lifecycle

**Priority:** P0 · **Effort:** M–L

**Current evidence.** Specials already accept an EOC: its condition is tested in `can_place_special`, and its true effects run in `overmap_special::place`. Both construct dialogue with the avatar as alpha and no beta. The placement effects run before the underlying fixed/mutable implementation returns its footprint. Mapgen itself has `mapgendata`, parameters, mission context, neighbors, and a map, but no corresponding general JSON callback context for a particular generation instance. [Placement predicate][special-check], [placement effects][special-eoc], [local generator][mapgen-exec].

**Content limit.** “When this town is successfully generated, register its clinic and residents” requires the actual town instance, origin, rotation, region, dimension, and completed footprint. The existing special EOC is useful for counters or avatar/world conditions; it cannot safely be treated as that complete lifecycle. Initialization also needs to distinguish special placement, first detailed-map generation, previews, updates, and reloads.

**Workaround.** Export known coordinates with `place_variables`, use separate update EOCs, and manually track initialization. This works for selected authored places but needs careful namespacing and ordering.

**Proposal.** Add read-only generation predicates and explicit committed hooks for special placement and detailed generation. Supply an instance ID, typed origin/footprint, rotation, dimension, region, parameters, mission/owner if present, generation reason, and a result. Emit initialization once per committed instance; previews and candidate checks must not run ordinary world-mutating effects. Reuse `mapgendata` through a bounded adapter rather than pretending the avatar is the generated location.

## GEN-02 — Conditional composition needs generation-scoped predicates and inputs

**Priority:** P1 · **Effort:** M

**Current evidence.** Neighbor/join/predecessor conditions, parameter distributions and switches, regional palettes, and variable weights already provide conditional composition. Ordinary mapgen and nested/update variant selectors can evaluate formulas, but their dialogue is avatar-based. Map extras check Z and regional feature flags, rather than accepting the complete EOC condition language with a generation-location context. [Conditional nesting][nested-doc], [parameters][params-doc], [mapgen weighting][mapgen-weight], [update weighting][update-core], [extra eligibility][extra-check].

**Content limit.** A mod can make a roof follow a parameter or an entrance follow a neighboring road. It cannot uniformly express “choose a working clinic if this settlement has staffed medical capacity, otherwise a looted clinic” across extras, nested pieces, palettes, and variants without external state and custom conventions. Looking at the avatar's position or faction can accidentally make an unrelated map depend on who caused it to generate.

**Workaround.** Choose broader variants outside mapgen, distribute parameter values, and keep weights in namespaced global variables. This is useful but not a location-aware contract.

**Proposal.** Expose a documented pure predicate subset over generation context. Allow eligible variants and extras to use it, with explicit parameter overrides and precedence. Separate stable world facts from “state at first visit” inputs. Cache only predicates whose declared inputs are stable, and report why alternatives were rejected. Preserve current lightweight parameter and neighbor checks.

Related low-cost work is to improve spatial query adapters: the current `closest_city` binding exposes known/all search and returns name/size, but cannot directly filter by size or a future settlement role/service. It also lacks an explicit failure callback. Use typed coordinate scales and failure outputs when extending it; do not add more queries that require callers to guess their units. [Current binding][city-query].

## GEN-03 — Regional geography lacks an extensible cross-boundary planning contract

**Priority:** P1 · **Effort:** L

**Current evidence.** Region layouts already distribute regions over a dimension. The overmap pipeline calls specialized geographical generators and passes neighboring overmaps to several of them; seeded spatial noise also supports continuity. This is meaningful boundary handling. The current region-layout documentation nevertheless explicitly warns that features across different regional settings are not coordinated sufficiently. There is no general data-driven pass registry in the inspected generation path. [Layouts and warning][layout-doc], [pipeline][worldgen-core], [water generation][water-gen], [layout implementation][region-layout].

**Content limit.** Mixed climate belts, mountain drainage, river cities, coast-to-inland transitions, and transport networks spanning several regions need more than independent settings for each overmap. JSON can tune or replace content, but it cannot define an arbitrary new watershed, elevation, or network-planning algorithm through the existing settings schema.

**Workaround.** Use a uniform region, constrain transitions, author large fixed specials, or maintain custom C++ generation. Those are legitimate choices for a focused mod.

**Proposal.** Introduce opt-in planning layers with stable spatial inputs and boundary contracts: elevation or abstract terrain suitability, water connections, settlement anchors, and transport corridors. Let data select supported algorithms and constraints; new algorithm types can still require C++. Validate neighboring regions' compatibility and provide explicit transition/fallback rules. Do not require a full geological simulator before improving border consistency.

## GEN-04 — City generation lacks reusable district and service constraints

**Priority:** P1 · **Effort:** L

**Current evidence.** Regional city settings already control sizes, spacing, megacity generation, and weighted houses/parks/shops. Building selection uses distance and distribution settings, checks city-size constraints and uniqueness, and bounds retries. Streets are generated by dedicated code. Mutable specials can author more structured settlements independently. [City settings][city-settings], [building selection][city-gen], [street generation][city-streets], [mutable layouts][mutable-doc].

**Content limit.** These mechanisms do not offer a reusable city program such as “one civic center, a market by the main road, housing near workplaces, two staffed gates, and at least one reachable well.” A rare clinic in a weighted table is a probability, not a service guarantee. A `CITY_UNIQUE` building limits duplicates; it does not establish a minimum count, service catchment, or district assignment.

**Workaround.** Build an entire village as a fixed/mutable special, or tune the tables and accept statistical results. This already supports compelling authored towns.

**Proposal.** Add an optional settlement-layout profile over existing specials and connections. Define district tags, required/optional facilities, count/capacity ranges, adjacency/access constraints, and fallback priorities. Keep generation bounded: allocate required facilities first, then fill remaining lots probabilistically. Export district/place identities for NPC assignments instead of inferring jobs from building ID substrings. Leave ordinary ruined-city generation available without this layer.

## GEN-05 — Runtime world edits need structured results and explicit conflict policy

**Priority:** P0 · **Effort:** M–L

**Current evidence.** The engine update API accepts `mapgen_arguments` and returns `ret_val<void>`. The EOC `mapgen_update` wrapper supplies `{}` for those arguments and does not branch on the returned result. Its delayed path stores update IDs in native timed events. The phase executor has vehicle-collision verification, but it is not a general transaction over buildings, residents, ownership, and player edits. Some collision checks occur as phases execute, so `verify` should not be advertised as universal rollback. [EOC binding][update-eoc], [update API][update-core], [phase application][update-phases], [delayed update][timed-update].

**Content limit.** Town upgrades, earthquake damage, faction takeovers, arena resets, and reconstruction need to know which changes succeeded. Changing the OMT label alone can disagree with an already generated detailed map. A script should not pay workers or announce a completed clinic when the map operation failed or affected only part of the intended footprint.

**Workaround.** Use narrowly authored updates at controlled locations and manually guard them with variables. `place_overmap_special` already supplies success/failure EOCs for its placement attempt, which is a useful precedent.

**Proposal.** First expose explicit update arguments and result callbacks without replacing the executor. Then add preflight and commit policies for protected/player-built tiles, occupied cells, vehicles, items, residents, generated/unexplored maps, and OMT/detail consistency. Return operation ID, affected footprint, skipped/conflicting cells, and reason codes. Multi-OMT operations must state whether they are atomic or may make reported partial progress. Never imply rollback unless all external effects participate.

## GEN-06 — Reproducibility needs stronger scope and provenance guarantees

**Priority:** P1 · **Effort:** M–L

**Current evidence.** World-seeded noise, shared mapgen parameters, and saved special-wide post-process decisions already improve consistency. Special post-process decisions even retain seeds. The current determinism test restores the RNG engine state and generates the same overmap sequence repeatedly; that is useful evidence of a deterministic path under those conditions. It does not test independence from exploration order, unrelated RNG draws, different script state, or content revisions. [Saved decisions][special-records], [parameters][params-doc], [existing determinism test][test-determinism], [variable selectors][mapgen-weight].

**Content limit.** A bug report containing only a world seed may be insufficient to reconstruct an exact procedural town after its mapgen inputs or execution sequence changed. Previews and added random EOCs can complicate diagnosis. This is an authoring guarantee gap, not a claim that the existing generator is nondeterministic under its tested contract.

**Workaround.** Save the world, seed, mod list, relevant variables, and reproduction path; use fixed parameters for reproducible layouts.

**Proposal.** Define opt-in named random streams derived from world seed, dimension, generation instance, pass, and content revision. Record selected variants, parameter values, relevant dynamic inputs, and failure/fallback decisions in a compact generation manifest. Make preview execution isolated from gameplay RNG and effects. Document which results remain dependent on already generated neighbors or world state; those dependencies need not disappear to become inspectable.

## GEN-07 — Vertical and special layouts need semantic connectivity contracts

**Priority:** P1 · **Effort:** M–L

**Current evidence.** Multi-OMT and multi-Z content, mutable joins and phases, predecessor handling, and `omt_stack` parameters already support large coherent structures. Existing tests exercise mutable placement many times. Vertical overmap generation still includes dedicated feature handling, while local templates supply actual stairs, doors, rooms, and obstacles. Matching overmap joins alone does not establish every detailed route a particular actor needs. [Mutable schema][mutable-doc], [vertical generation][worldgen-sub], [stack parameters][params-doc], [placement tests][test-overmap].

**Content limit.** A dungeon needs its objective reachable from the entrance; a clinic needs an accessible treatment station; a multi-floor town needs matching stair endpoints. A road or abstract join can be connected while local doors, rotations, blocked corridors, or mismatched Z endpoints prevent the intended route. Different actors may also need different access policies.

**Workaround.** Use tested templates, align connectors by convention, and run repeated placement/gameplay checks. Author a fallback exit for critical locations.

**Proposal.** Give reusable pieces named connector roles and semantic anchors, including entrance, public route, stair/elevator endpoint, service station, objective, and emergency exit. Validate required paths after composing the actual geometry, under declared actor assumptions. For random layouts, use bounded retries and explicit fallback pieces, with diagnostics identifying the unsatisfied contract. This extends existing join and test machinery rather than replacing it with a universal solver.

<!-- Source references -->

[city-gen]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/overmap_city.cpp#L240
[city-query]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npctalk.cpp#L9932
[city-settings]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/REGION_SETTINGS.md#L440
[city-streets]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/overmap_city.cpp#L403
[extra-check]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/map_extras.cpp#L1300
[layout-doc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/REGION_LAYOUT.md#L1
[mapgen-exec]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/mapgen.cpp#L6416
[mapgen-weight]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/mapgen.cpp#L513
[mutable-doc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/OVERMAP.md#L833
[nested-doc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/MAPGEN.md#L1207
[params-doc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/MAPGEN.md#L1317
[region-layout]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/overmap_worldgen.cpp#L133
[special-check]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/overmap.cpp#L2961
[special-eoc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/overmap_special.cpp#L276
[special-records]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/overmap.cpp#L3058
[test-determinism]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/tests/overmap_test.cpp#L1030
[test-overmap]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/tests/overmap_test.cpp#L323
[timed-update]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/timed_event.cpp#L333
[update-core]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/mapgen.cpp#L7886
[update-eoc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npctalk.cpp#L5465
[update-phases]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/mapgen.cpp#L6373
[water-gen]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/overmap_water.cpp#L259
[worldgen-core]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/overmap.cpp#L925
[worldgen-sub]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/overmap.cpp#L1053
