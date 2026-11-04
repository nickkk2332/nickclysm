# World generation, game modes, settlements, work, and events audit

**Implementation follow-up:** The four concrete correctness observations below have been addressed in the working tree. See [the twelve-fix record](../correctness-fixes.md) for behavior, regression coverage, validation and remaining architectural scope. The original snapshot/evidence below is retained.

**Inspected revision:** `b32c9baea25f62478228c6a91cbbf9f356e4e8a3` of Nickclysm. This is a static source and content audit. No gameplay code, JSON content, or saves were changed. Existing tests were read as evidence; they were not run. Proposed acceptance scenarios are future work, not reported test results.

Nickclysm can already generate elaborate environments, seed authored NPC communities, and support substantially different ways to play through JSON and EOCs. Mutable specials, regional layouts, mapgen parameters, editable NPC behavior, work shifts, companion missions, and recurring events are real foundations. Sky Island is an existing example of a custom extraction loop, including return-on-death behavior.

The largest missing piece is **persistent organization across those systems**. A generated city is not automatically a settlement with residents, households, staffed services, productive jobs, and a history. A recurring global EOC is not automatically an event affecting several places independently of the avatar. A scenario can establish a custom game, but it does not provide a reusable mode/session lifecycle.

The most valuable expansion would connect generation, settlements, jobs, and events through explicit instance identities, spatial scope, structured results, and rules for unloaded areas. More map templates remain valuable, but templates alone cannot provide a living world.

## Reading order

| Document | Contents |
| --- | --- |
| [01-capabilities.md](01-capabilities.md) | Current systems, content feasibility, and the main architectural boundaries |
| [02-mapgen-worldgen.md](02-mapgen-worldgen.md) | Generation hooks, geography, city layouts, parameters, safe updates, and reproducibility |
| [03-gamemodes.md](03-gamemodes.md) | Scenarios versus modes, run lifecycle, rules, objectives, and progression |
| [04-settlements.md](04-settlements.md) | Town identity, residents, semantic places, services, resources, and offscreen continuity |
| [05-schedules-jobs.md](05-schedules-jobs.md) | Existing shifts and goals; agendas, work orders, reservations, travel, and companion jobs |
| [06-events.md](06-events.md) | Existing event mechanisms; event selection, instances, scope, consequences, and hooks |
| [07-authoring-correctness.md](07-authoring-correctness.md) | Tooling proposals and four current source observations |
| [08-expansion-plan.md](08-expansion-plan.md) | Priorities, concrete interface contracts, examples, and acceptance scenarios |

## Findings index

There are **34 findings**: 30 expansion/authoring findings and four specific source observations. Priority describes value for this content goal, not security severity. **P0** is a shared foundation, **P1** enables an important family of content, and **P2** is a narrower improvement. Effort is architectural: **S** = local change, **M** = several cooperating systems, **L** = a new persistent model or broad integration. These are not delivery estimates.

| ID | Finding | Priority | Effort |
| --- | --- | --- | --- |
| GEN-01 | Generation EOCs lack a shared location/instance context and lifecycle | P0 | M–L |
| GEN-02 | Conditional composition needs generation-scoped predicates and inputs | P1 | M |
| GEN-03 | Regional geography lacks an extensible cross-boundary planning contract | P1 | L |
| GEN-04 | City generation lacks reusable district and service constraints | P1 | L |
| GEN-05 | Runtime world edits need structured results and explicit conflict policy | P0 | M–L |
| GEN-06 | Reproducibility needs stronger scope and provenance guarantees | P1 | M–L |
| GEN-07 | Vertical and special layouts need semantic connectivity contracts | P1 | M–L |
| MODE-01 | Custom modes lack a registered, saved session lifecycle | P1 | M–L |
| MODE-02 | Mode rules lack consistent scope, enforcement, and explanations | P1 | M–L |
| MODE-03 | Objectives and run outcomes lack a common orchestration model | P1 | M–L |
| MODE-04 | Repeated runs and progression need explicit world/character/run ownership | P1 | L |
| TOWN-01 | Cities, camps, and authored hubs lack a common settlement instance | P0 | L |
| TOWN-02 | Procedural residents need durable identities and population membership | P1 | L |
| TOWN-03 | Buildings need named places and assignments that survive map changes | P0 | M–L |
| TOWN-04 | Services need availability, staffing, access, and transaction contracts | P1 | M–L |
| TOWN-05 | Settlement resources and change need an opt-in simulation model | P1 | L |
| TOWN-06 | Loaded and unloaded settlements need one continuity policy | P0 | L |
| WORK-01 | Work shifts and behavior goals need a reusable agenda layer | P1 | M–L |
| WORK-02 | NPC jobs need discoverable, persistent work orders | P0 | L |
| WORK-03 | Shared work needs broader reservations and cooperation contracts | P1 | M–L |
| WORK-04 | Scheduled travel needs semantic destinations and consistent arrival | P1 | M–L |
| WORK-05 | Companion mission data needs an executable custom-job lifecycle | P1 | M–L |
| EVENT-01 | Random events need shared eligibility, budgets, and selection history | P1 | M–L |
| EVENT-02 | Delayed effects need owned, cancellable event instances | P0 | L |
| EVENT-03 | Event scope and offscreen timing need explicit policies | P0 | L |
| EVENT-04 | Global consequences need durable state and local projection | P1 | L |
| EVENT-05 | Semantic world hooks and custom notifications need expansion | P1 | M–L |
| EVENT-06 | Events need reusable phases, participants, and player-facing follow-through | P1 | M–L |
| TOOL-01 | Authoring needs integrated inspection and semantic validation | P1 | M |
| TOOL-02 | Large populations and histories need explicit processing budgets | P0 | M–L |
| OBS-01 | OMT exit dispatch checks the old terrain but invokes the new terrain | P1 | S |
| OBS-02 | Mutable special expansion does not fully honor unexplored-only placement | P1 | M |
| OBS-03 | Map-extra flag deletion reads the `extend` object | P2 | S |
| OBS-04 | Mapgen NPC placement discards the requested relative Z | P1 | S–M |

## What “possible” means here

- **Native:** an existing schema and ordinary engine path support the feature.
- **Scriptable:** JSON/EOCs can build a useful version, with the author handling state, timing, and integration.
- **Needs engine support:** the defining behavior requires a missing shared model or execution contract. More JSON fields alone would not create it.

“No common model” does not mean every individual example is impossible. A custom town can use hand-authored NPCs, variables, and dialogue; a tournament can count victories with EOCs. The gap is making those examples reusable, independently instanced, inspectable, and consistent through save/load, travel, death, and unloaded time.

Negative findings are bounded to the inspected schemas, dispatch paths, and examples. This audit does not establish that every possible mod workaround has been exhausted.

## Different gaps need different kinds of work

| Gap | Examples | Appropriate approach |
| --- | --- | --- |
| Existing engine operation needs a better JSON binding | Update arguments/results, filtered city searches, overmap group operations | Add validated adapters over existing APIs |
| An existing action needs better context or dispatch | Generation completion, OMT movement, job/incident transitions | Supply typed context, semantic hooks, and documented ordering |
| A durable shared record is missing | Settlement membership, place assignments, work orders, incident/run instances | Add opt-in serialized state with ownership and invalidation rules |
| A new simulation/generator is requested | Resource-driven town growth, cross-region geography, district allocation | Add a bounded model or supported algorithm; EOC fields alone are insufficient |

The first two categories can deliver useful content before the larger persistent models are built.

## Relationship to earlier work

The [powers audit](../powers/README.md) and [interactivity audit](../interactivity/README.md) identify related needs for persistent references, result contracts, ownership, and jobs. Those should be shared foundations. A fourth timer system or a separate settlement scripting language would increase integration work.

The original chapters on [mapgen](../08-mapgen.md), [overmaps](../09-overmap-worldgen.md), [NPCs](../21-npc-ai-factions.md), and [missions/camps](../22-missions-trade-camps.md) contain useful history, but several older gaps have since been addressed. The current tree already supports special-placement EOCs, OMT entry/exit fields, runtime OMT edits and special placement, adjustable mapgen weights, weighted update variants, saved JSON NPC goals, work shifts, and companion lifecycle events. These are credited here; the observations are rechecked against the pinned source.
