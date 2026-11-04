# Expansion plan and acceptance scenarios

This is a proposed direction, not implementation work. Names below describe conceptual interfaces and hooks; they are **not supported JSON members** unless the current-capability documents explicitly identify them as existing.

## Recommended sequence

| Stage | Deliverable | Findings | Concrete content demonstration |
| --- | --- | --- | --- |
| 0 | Characterize the four observations and publish the current action/context support matrix | OBS-01–04, AUTHOR-02 | A dropped gadget, terrain examine action, falling item and machine emission see the expected participants and results |
| 1 | Named contextual actions, typed item state/targets, structured results and exact transfers | ACTION-01, ITEM-01/02/04/05 | A multi-purpose tool can select a real vessel, measure its contents, transfer a chosen amount, and cancel without spending materials |
| 2 | Instance-aware item and tile lifecycle hooks plus sparse tile/object state | ITEM-03, TILE-01/02/06 | Two identical locks or workstations retain independent settings and react to the actual open/remove/transform action |
| 3 | Owned machine jobs, resource/inventory endpoints, and bounded catch-up | ITEM-06, TILE-05, MACH-03 | A custom dryer/refiner pauses on missing power/input/output space and resumes correctly after leaving and reloading |
| 4 | Typed connections, resource channels, and richer sensor events | TILE-03/04, MACH-01/02 | A pressure plate, linked door and resource-powered machine work as separate addressed devices with predictable disconnect behavior |
| 5 | Reusable field/physical behavior and bounded reaction rules | PHYS-01/02 | A new-ID corrosive field and an alchemical vessel reuse real configured behavior rather than duplicating whole processors |
| 6 | Optional simulation modules chosen by actual content needs | PHYS-03–08 | Add finite reservoirs, a thermal process, a structural assembly, a hoist or improved sensors one at a time |

AUTHOR-01/02 belong in every stage. A feature should have a menu-free invocation path, meaningful failure reason, inspection and an executable example when introduced, not after an entire world simulation rewrite.

The existing [grid/stateful-machine plan](../plans/grids-and-stateful-machines-design.md) is a starting design for portions of stages 2–4. Reconcile its proposals with source changes and the action/instance contracts before implementing it. Its example JSON is not a current capability.

## Small changes with useful immediate value

These can be assessed independently of a larger machine framework:

1. Supply reliable terrain/furniture/selected-layer context to examine EOCs while preserving `this` compatibility.
2. Propagate optional actor/source and cause through drop/placement and emission routes.
3. Give item actions independent IDs and expose invocation/cancellation results to scripts.
4. Add missing typed item queries and a small exact-quantity transfer API, using current item locations and pocket rules.
5. Expose lifecycle observations around native open/close, placement/removal and pocket changes.
6. Add a source/vehicle/part context to manual part EOC activation; it should identify the selected part rather than requiring a radius search.
7. Prototype sparse tile state and one bounded process with catch-up before expanding to multiple resource networks.

A local fix should not require every consumer to migrate at once. Preserve existing action actors, recipe callbacks, electrical behavior, saves and ordinary static furniture. Shared infrastructure should also support the [powers audit](../powers/README.md), especially persistent references, result contracts and owned cleanup.

## Action contract

The following roles should be distinct even when an operation happens to use the same entity for several of them:

| Role | Meaning |
| --- | --- |
| Acting entity | Who deliberately initiated the interaction; may be absent for an environmental process |
| Instigator/owner | Who is responsible for delayed effects, ownership and attribution; not always the current worker |
| Source/tool | The specific item, machine or process performing the action |
| Primary target | Item, pocket, tile layer, structure member, vehicle/part or device endpoint |
| Additional targets | Reagent source, destination container, worksite, output port, linked control, etc. |
| Location | Absolute coordinate and dimension; distinct from the current location of a moving target |
| Cause | Manual use, NPC job, leak, fall, bash, explosion, construction, scripted replacement, load/catch-up, etc. |
| Request parameters | Action ID, exact quantity, chosen mode, input selection, declared time/resource policy |
| Result | Refused/cancelled/started/completed/partially completed, reason, actual moved/consumed quantities, created/changed references |

Keep the existing alpha/beta conventions compatible. New hooks should document named roles rather than silently treating every alpha as a Character. A machine or terrain emission may have no Character participant; that must be a supported state rather than a reason to substitute the avatar.

Separate three stages:

1. **Query/prepare:** pure eligibility, target selection, estimates, reservations and required choices. No hidden payment or world mutation in a tooltip or NPC scorer.
2. **Commit/work:** apply the approved costs and changes; if work continues, create a persistent job with an owner and progress model.
3. **Result/observe:** report actual outcome and exact affected references. Observation hooks do not acquire an implicit rollback or veto meaning.

Cancellation policy is explicit. Preventing ordinary charge/time expenditure is useful, but does not undo script effects that already happened. Prefer validate-then-commit over a promise of generic rollback for arbitrary EOCs.

## Suggested hook coverage

| Family | Context needed | Policy decisions |
| --- | --- | --- |
| Item action | Actor, source item, action ID, typed targets, quantity/location | Per-action condition, costs, unsupported actor behavior, started versus completed result |
| Item lifecycle | Exact item, old/new owner/location/container, cause | What counts as acquisition versus inventory rearrangement; no load-time replay of semantic events |
| Pocket operations | Parent/pocket and child references, moved quantity, old/new sealed state | Fit/ownership rules, partial transfer, merge/spill and result identity |
| Item damage/transform/destroy | Source, victim item, original/final type/state, surviving contents | Observation versus prevention, destruction-safe callback lifetime and provenance |
| Tile lifecycle | Location/dimension, examined/changed layer, old/new IDs, instance and cause | Carry/reset state, actor absence, mapgen/loading, no-op edits and recursive changes |
| Structure lifecycle | Assembly/anchor, member role, footprint, state | Split/orphan policy, partial destruction, movement and packing |
| Sensor contact | Trigger source, exact item/creature, threshold value, transition | One-shot/rearm/persistent; enter/stay/exit; whether an object exists at final location yet |
| Machine process | Owner/source, input/output endpoints, job/progress, elapsed time | Power/input/output blockage, pause/resume, interruption, cleanup and catch-up |
| Part lifecycle | Vehicle/part reference, installed source item, enabled/broken state | Stable part identity across installation/removal, toggles, destruction and vehicle splitting |
| Connection/resource | Endpoint/channel, transferred quantity, loss, topology change | Conservation, discrete signal versus flow, ordering, loops and unload semantics |
| Physical reaction | Typed contact participants, material/field data, exposure and source | Trigger frequency, conserved inputs/products, cascading reactions and bounded work |
| Catch-up | Last-resolved/now, elapsed interval, context needed to settle | Freeze, analytical settlement or bounded coarse steps; no unbounded turn replay |

Internal callbacks such as `on_contents_changed` are useful integration points, but simply exposing every call as an event can produce misleading duplicate interactions. Define semantic stages and precisely which operations send them before wiring content to them.

## Persistent state and simulation rules

- Store object state with the object and location state with the location. Moving a configured machine should not leave its active job at the old coordinate; replacing it should not accidentally hand its inventory/settings to a new unrelated object.
- Reuse item locations, UIDs, current vehicle mechanics, recipe outputs and activity state where their guarantees suffice. New typed references must define gone/unloaded/wrong-dimension outcomes without holding raw pointers through callbacks.
- Protect exact quantities and reservations. A stack that merges must not make “move one” become “drop all”; two jobs must not both spend the same source material.
- Model inventories, resources, job progress, signals and optional physics as separate contracts. One generalized “tick script” is not enough to maintain their invariants.
- Settle elapsed time according to dependencies. Stop at power, input, output-space, temperature or other limiting events. Large intervals should not multiply a new current rate by old time unless that approximation is explicitly the process design.
- Revalidate after arbitrary EOCs: they can remove, move or transform the very participants a native operation was using. Do not keep invalid pointers or iterate a container that a hook may mutate.
- Keep budgets visible: machine count, interval, scanned area, graph traversal, reaction chains and catch-up work. Measure representative bases before choosing defaults; this audit provides no benchmark-derived thresholds.
- Define physical fidelity by content need. Finite reservoirs or a small thermal model can unlock valuable devices without committing to full fluid, pressure, stress, chemistry or rigid-body solvers everywhere.

## Acceptance scenarios for later work

These are proposed regression/content tests and play scenarios. **None was run during this audit.** Test the actual player, NPC, EOC and environmental dispatch paths rather than only constructing helper objects.

| Scenario | Observable acceptance behavior | Main findings |
| --- | --- | --- |
| Multi-purpose gadget | Three independently named verbs on one item have separate conditions/costs; a cancelled target selection changes neither resources nor world state | ACTION-01, ITEM-01/02 |
| Exact vessel transfer | Move a chosen amount between named pockets; fit/seal/ownership is enforced; merged stack and remainder references are correct; no material is duplicated or lost | ITEM-04/05 |
| Instance-bound object | Two identical artifacts differ by variable; only the one acquired/equipped/transformed/destroyed gets the right lifecycle effect | ITEM-03/05 |
| Two identical locks | Codes/settings survive save/load independently; examining bare terrain supplies terrain identity; opening through another route triggers the right lifecycle event | TILE-01/02, OBS-02 |
| Real actor drop/spill | Avatar and NPC deliberately drop items; another item leaks; callback actor/cause/location are correct and environmental placement does not read the avatar as its worker | OBS-01, ITEM-03 |
| Weighted pressure plate | Creature and thrown-item triggers provide appropriate references; persistent occupancy/release differs from one-shot trap removal; two plates remain independent | TILE-03 |
| Shared workstation | A multi-tile device has one inventory/settings object; moving or partially destroying it follows the declared member/anchor policy | TILE-01/04 |
| Safe placement | A footprint includes occupied/unsupported/unexpected tiles; declared atomic or partial behavior is honored, with matching material use and result locations | TILE-06 |
| Unattended dryer/refinery | Job reserves exact inputs, pauses when power/output space fails, resumes without duplication, and transforms real input metadata into outputs | ITEM-06, TILE-05, MACH-03 |
| Away from the base | Compare loaded progression with declared catch-up over the same interval; input starvation and changing supply are bounded and accounted for once | MACH-03 |
| Resource pool and cables | One non-electric resource has measured storage/transfer/loss; disconnecting a cable changes only the intended graph; electricity keeps its native behavior | MACH-01/02 |
| Relay puzzle | Linked controls identify devices, not nearest matching tile types; simultaneous signals and loops terminate deterministically; moving/removing endpoints follows policy | MACH-02 |
| Custom corrosive field | A new field ID explicitly opts into corrosion/propagation; immunity and effects do not accidentally stand in for missing physical reactions | PHYS-01/02 |
| Alchemy vessel | Reagent quantities and products balance; sealed/open and hot/cold cases follow declared rules; a chain reaction cannot recurse without bound | PHYS-02/03/04 |
| Finite pump/airlock | Reservoir capacity or room composition changes by recorded transfer; opening/disconnecting has the declared equalization behavior rather than only swapping a visual tile | PHYS-03 |
| Thermal process | Input energy, temperature and process requirements use defined units; the process does not create unbounded heat from a temperature-setting command | PHYS-04 |
| Load-bearing assembly/hoist | A declared load/anchor limit affects only an opted-in structure/mechanism; breakage produces coherent item, participant and tile results | PHYS-05/06 |
| Falling item | Force the fall-specific damage step, inspect the surviving destination item, and distinguish it from lower-tile bash damage and stack merging | OBS-03 |
| Independent emission | Two machines with different settings produce different output; changing avatar stats does not change an independent emitter | OBS-04 |
| Sensor and growth bed | A bounded local measurement/growth input changes the intended instance; unload/catch-up, neighboring devices and harvest metadata follow the declared process | PHYS-07/08 |
| NPC technician | The NPC selects the same configured verb and targets as a player, without opening a player menu; unsupported actions produce an explicit refusal | AUTHOR-01 |

For shared primitives, include multiple simultaneous instances, save/load, moving/removing a participant, dimension changes, missing sources, quantity-counted items, containers within containers and map boundaries. Reuse that coverage instead of multiplying every simple content example into a full engine test matrix.

## Useful content before the larger systems exist

A first expansion can demonstrate depth using current mechanics:

- A configurable survival gadget built with an EOC submenu, conditions, existing queries and explicit consumption policy.
- A small puzzle workshop with named examine choices, controlled location links, cardreader/door transforms and bash hooks.
- A craft/repair station using recipe result-item initialization, construction hooks and NPC errands.
- New crops, brews and foods inside the existing native lifecycle, with clearly stated discrete states.

Keep assumptions explicit. A manually operated menu machine can be good content; do not describe it as autonomous off-screen industry. A finite tank represented by items can be good content; do not imply it drains the map's water terrain. The audit's recommended foundations make those richer versions possible without requiring every initial content pack to simulate them.
