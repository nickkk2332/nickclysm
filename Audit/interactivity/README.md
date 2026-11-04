# Item, tile, and world interactivity audit

**Implementation follow-up:** The four concrete correctness observations below have been addressed in the working tree. See [the twelve-fix record](../correctness-fixes.md) for behavior, regression coverage, validation and remaining architectural scope. The original snapshot/evidence below is retained.

**Question:** What custom content can current Nickclysm JSON and EOCs express when the player interacts with items, tiles, machines, and the physical world? What prevents deeper combinations of those interactions?

**Snapshot:** `b32c9baea25f62478228c6a91cbbf9f356e4e8a3` (`master`). This report inspects the current source, documentation, shipped content, and existing audit work. It adds documentation only. No gameplay implementation, new content, runtime test, or benchmark was performed.

## Assessment

There is already enough support for substantial custom survival content: tools and containers, conditional item activations, named tile examine choices, traps, locks and doors, crafting and disassembly, construction, farming, fermentation, appliances, vehicle grids, hazards, destruction, and scripted puzzles. EOCs can connect many of these systems.

The boundary is **new relationships and persistent processes**, rather than a shortage of object definitions. A new wrench, battery, crop, trap, or recipe fits established mechanics. A new kind of independently stateful machine, automatic material-transfer network, pressurized room, load-bearing bridge, or reusable physical reaction often needs engine support.

Three recurring limits matter most:

1. **Incomplete identity and state.** Items have variables and serializable locations, but arbitrary furniture does not have a comparable instance. The existing “furniture talker” wraps a computer. Coordinate-keyed script variables are a workaround, not a lifecycle for a machine that can move, break, or be replaced.
2. **Actions are not one consistent contract.** Manual item use, drops, tile examination, traps, crafting, construction, and environmental changes provide different participants, callbacks, costs, and success meanings. Internal C++ callbacks are not automatically JSON hooks.
3. **Physics consists of specialized systems.** Materials, fire, gas spread, gravity, vehicle collisions, electricity, temperature, and liquid handling already do useful work. Their rules are not one general, configurable physical world. Adding a material property or a field with a familiar-looking flag does not necessarily install the corresponding behavior.

The best first investment is persistent tile/machine state, explicit action context and results, and a bounded job/catch-up lifecycle. Those support puzzles, production, salvage, gadgets, and environmental content without first requiring fluid dynamics or rigid-body physics.

## Documents

| Document | Contents |
| --- | --- |
| [01-capabilities.md](01-capabilities.md) | Current building blocks, representative custom content, and a hook/support matrix |
| [02-items-and-actions.md](02-items-and-actions.md) | Action contracts, item verbs, state, lifecycle, containers, identity, and production hooks |
| [03-tiles-and-machines.md](03-tiles-and-machines.md) | Tile instances, callbacks, sensors, structures, machines, networks, and automation |
| [04-world-physics.md](04-world-physics.md) | Field behavior, chemistry, fluids, heat, structural support, props, acoustics, and growth |
| [05-authoring-and-observations.md](05-authoring-and-observations.md) | NPC/UI support, validation and observability, plus four current code observations |
| [06-expansion-plan.md](06-expansion-plan.md) | Suggested sequence, concrete hook contracts, and acceptance scenarios for future implementation |

## Findings index

Priority is value for deeper custom interactivity, not security severity. **P0** is a shared foundation, **P1** unlocks an important family of content, and **P2** addresses a narrower simulation or authoring need. Effort is architectural: S = local; M = several cooperating systems; L = a new persistent model or broad integration. These are not delivery estimates.

| ID | Finding | Priority | Effort |
| --- | --- | --- | --- |
| ACTION-01 | No common contextual action request, result, and commitment contract | P0 | M–L |
| ITEM-01 | Item action identity is tied to implementation type | P1 | M |
| ITEM-02 | Item state access remains incomplete and resource-specific | P1 | M |
| ITEM-03 | Item and inventory lifecycles lack consistent instance-aware hooks | P0 | M–L |
| ITEM-04 | Pockets need targeted, scriptable transfer and containment operations | P1 | M–L |
| ITEM-05 | Item quantities, transformations, and long-lived references need explicit policy | P1 | M–L |
| ITEM-06 | Production hooks do not form a general machine/process contract | P1 | M–L |
| TILE-01 | Arbitrary tiles/furniture lack durable instance state and a real tile talker | P0 | L |
| TILE-02 | Tile transitions do not share a lifecycle hook system | P1 | M–L |
| TILE-03 | Contacts, pressure plates, and sensors need richer trigger contracts | P1 | M |
| TILE-04 | Linked furniture tiles lack a common object/structure identity | P1 | L |
| TILE-05 | Autonomous furniture processing needs an owned machine lifecycle | P0 | L |
| TILE-06 | Tile editing lacks a shared query/result/conflict policy | P1 | M–L |
| MACH-01 | Networks share electricity rather than arbitrary resource channels | P1 | L |
| MACH-02 | Connectivity, signals, and remote control lack a reusable graph contract | P1 | M–L |
| MACH-03 | Automation needs actor-independent jobs, reservations, and catch-up | P0 | L |
| PHYS-01 | Physical behavior is only partly derived from configurable properties | P1 | M–L |
| PHYS-02 | Chemistry and mixtures lack a general contact/reaction interface | P2 | L |
| PHYS-03 | Liquid transfer and gas fields do not supply volume/pressure simulation | P2 | L |
| PHYS-04 | Local temperature effects are not a thermal compartment model | P2 | L |
| PHYS-05 | Structural collapse is not configurable load-path simulation | P2 | L |
| PHYS-06 | Props, ropes, and mechanisms lack reusable physical constraints | P2 | L |
| PHYS-07 | Environmental sensing lacks shared propagation/query/event contracts | P2 | M–L |
| PHYS-08 | Plant growth is configurable but not a general stateful ecology process | P2 | M–L |
| AUTHOR-01 | NPC and player routes cannot discover/use every custom interaction equally | P1 | M |
| AUTHOR-02 | Semantic validation, inspection, and processing budgets need expansion | P1 | M |
| OBS-01 | Drop actions receive the avatar rather than a causal actor | P1 | S–M |
| OBS-02 | Terrain examine EOCs report furniture identity in `this` | P1 | S |
| OBS-03 | Falling-item damage is applied after the destination copy is placed | P1 | S–M |
| OBS-04 | Emission formulas evaluate with the avatar as their actor | P1 | M |

## How to interpret “can” and “cannot”

- **Native:** supported by an existing content schema and ordinary execution path.
- **Scriptable:** EOCs, recipes, variables, menus, and transforms can make a useful version, with explicit assumptions and lifecycle work.
- **Needs an engine contract:** the defining interaction has no general supported primitive. It can sometimes be approximated, but adding unused JSON fields will not create it.

An arbitrary script can imitate many outcomes. This audit distinguishes those imitations from integration with inventory accounting, NPC use, save/load, map changes, and physics. A menu that exchanges items can be a perfectly good custom machine; it is not proof of an autonomous production network.

## Where engine work is needed

| Kind of gap | Examples | Appropriate first change |
| --- | --- | --- |
| Existing engine functionality lacks convenient JSON access | Exact item state, pocket operations, quantity splits and transfer results | Add validated bindings/adapters; reuse native item and containment rules |
| Existing events/actions lack consistent context or hooks | Item lifecycles, tile opening/replacement, trap trigger source, selected vehicle part | Add semantic dispatch points and documented participants/results |
| A new persistent model is missing | Furniture instances, owned machine jobs, connection endpoints, multi-tile object state | Add opt-in serialized state and lifecycle, with save/unload policy |
| The requested physics itself is not represented generally | Pressure/flow, thermal compartments, structural loads, rope tension, contact chemistry | Choose a bounded new simulation module; extra EOC keys alone cannot supply it |

Several findings span categories. A good implementation can expose a useful existing operation first and defer the larger simulation until content demonstrates a need.

## Existing improvements and related work

This is not a repetition of the original chapter findings. The current tree already has conditional EOC item use, `use_location`, charge/cancel outputs, item movement errands, container capacity queries, crafting result-item and disassembly EOCs, construction EOCs, activity lifecycle context, tile bash EOCs, editable NPC goals, and vehicle state queries. These are credited in the report.

The existing [grid and stateful-machine design](../plans/grids-and-stateful-machines-design.md) overlaps strongly with TILE-01/05 and MACH-01/03. Its status is **design only**. It is a useful starting proposal, not evidence that its `grid_resource`, part lifecycle, tile-variable, or elapsed-time interfaces are implemented. The new audit also covers item and physics gaps beyond that plan.

The [powers audit](../powers/README.md) discusses related entity-reference, transaction, movement, and ownership problems. Shared infrastructure should serve both areas; a second unrelated scripting runtime would make integration harder.

## Confidence and verification

Findings identify inspected representations and execution paths, with pinned source links. Negative claims concern the missing general contract, not a claim that no bespoke C++ or EOC workaround could ever exist. The four OBS findings are source-level observations; their runtime reach and compatibility impact should be characterized before changing them.

Links, source anchors, finding coverage, change scope, and whitespace were checked locally. No gameplay or performance result is claimed. Recheck current code before implementation if the repository has advanced beyond the pinned snapshot.
