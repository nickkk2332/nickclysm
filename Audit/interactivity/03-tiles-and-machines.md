# Tile state, machines, and automation

See [README.md](README.md) for priorities and scope. The earlier [stateful-machine plan](../plans/grids-and-stateful-machines-design.md) is relevant, but remains a design proposal. The proposed additions below are not supported JSON syntax.

## TILE-01 — Arbitrary furniture needs persistent instance state

**P0 · L · Confirmed representation gap.**

**Evidence:** [Submaps][submap] have terrain/furniture storage and specialized per-location data such as computers, partial constructions, cosmetics and tile damage. They do not supply a general variables/lifecycle object for each placed furniture instance. [The furniture talker][furniture_talker] wraps `computer*`. [Examine EOCs][examine_eoc] receive user alpha, no tile beta, and location/type context rather than a general tile instance.

**What this blocks:** Independent machine inventories/settings, per-lock codes and wear, persistent contamination, configured workstations, sensor calibration, and timers that belong to a particular placed object. Two furnaces of the same type should be able to have different progress without inventing a separate furniture type for each state.

**Workaround:** Finite-state furniture transforms, computers, associated item/vehicle variables, or coordinate-keyed global variables. These are useful, but scripts must manually manage replacement, moving, packing, destruction and save migration.

**Expansion:** Sparse serialized tile/object state with distinct scopes: **state of the location** versus **state of the installed furniture**. Provide a real tile/furniture talker, typed references and explicit carry/reset/migrate rules. Furniture state may follow the object; consecration or pollution may remain with the ground. Do not reinterpret every computer-backed talker as arbitrary furniture without a compatibility layer.

## TILE-02 — Tile changes need shared lifecycle hooks

**P1 · M–L · Confirmed coverage gap.**

**Evidence:** [Bash data][bash_hooks] already supports hit/destroy EOCs, and [bash dispatch][run_bash] supplies available source and before/after tile information. Native [open-door][open_door], [terrain replacement][ter_set] and [furniture replacement][furn_set] paths are separate. [EOC tile setters][set_tile] perform replacements directly; not every change is a bash.

**What this blocks:** A door that logs every genuine opening, a chest that reacts to any removal, a workstation that shuts down on packing, or a custom floor that responds to construction, corrosion and replacement consistently. An examine EOC can observe its own action, but not every other route that changes the tile.

**Workaround:** Custom examine scripts and bash EOCs, explicit follow-ups in construction post-EOCs, or periodic reconciliation of known locations.

**Expansion:** Defined lifecycle observations for place/remove, open/close, damage/destroy, transform and move, with actor or environmental cause, tool/source, old/new layers, dimension and instance identity. Add preconditions only where a caller can safely honor them. Distinguish map generation/loading from meaningful play events, and avoid recursively treating a hook's bookkeeping edit as a fresh user action.

## TILE-03 — Sensors need richer trigger and persistence semantics

**P1 · M · Confirmed trap-path limitation.**

**Evidence:** [Trap data][trap_schema] includes thrown-item weight and sound thresholds, and traps support creature/item trigger functions. However, [the EOC trap function][trap_eoc] returns false without a creature, does not bind the triggering item, and removes the trap after running its EOCs. These semantics are narrower than a general persistent sensor.

**What this blocks:** Weighted-item EOC pressure plates, repeatable automatic doors, “trigger while occupied,” “release when vacated,” continuous load thresholds, device-to-device activation, and an EOC that behaves identically for a creature, thrown object or environmental trigger.

**Workaround:** Existing native trap actions, creature-triggered one-shot EOCs, rearming after the callback, or polling local occupancy/weight with a script. Rearming must account for the EOC trap's removal after its EOCs return; replacing it inside that callback is not an independent persistent-sensor lifecycle. Sound-triggered native traps already cover useful alarm behavior.

**Expansion:** Typed trigger participants and cause, location, measured weight/volume/sound, transition edge, and explicit consume/rearm/persistent policy. Separate enter/stay/exit or threshold-crossing semantics from “one callback happened.” Preserve current one-shot traps by default. Support an item reference at impact without assuming its final inventory/map location already exists.

## TILE-04 — Linked tiles need a common object identity

**P1 · L · Confirmed structure boundary.**

**Evidence:** [Furniture/terrain definitions][multi_tile] define per-tile types and transforms. [Submap state][submap] stores specialized tile data; mapgen and EOCs can arrange many tiles but do not make those tiles one shared interactive object. Vehicles already supply a separate multi-part object model and should be recognized as a valid workaround for some machinery.

**What this blocks:** A multi-tile press with one control panel and inventory, a movable cabinet that carries its state and contents, a linked blast door, or a structure whose destruction or relocation affects only its own members. Matching nearby furniture IDs cannot distinguish two identical adjacent installations reliably.

**Workaround:** Mapgen-defined layouts and explicit coordinate links, or a vehicle/appliance assembly when its mechanics fit. Small fixed puzzle rooms can use a controlled set of locations successfully.

**Expansion:** Optional anchored object/structure identity with typed member roles and footprint, shared state, inventory endpoints, and movement/removal policies. Define splitting, partial destruction and orphaned members. Reuse vehicle assemblies where appropriate; avoid making every wall tile a heavyweight object or treating a visual connect group as object ownership.

## TILE-05 — Autonomous furniture needs a machine lifecycle

**P0 · L · Confirmed process gap.**

**Evidence:** [Tile examination][examine_eoc] runs only when called. [Map actualization][actualize] handles selected existing processes such as plant growth, fruit restocking, sap, funnels and radiation effects. [Item processing][item_process] and [vehicle catch-up][vehicle_catchup] are different routes. [Manual part activation][machine_manual] exists, but is not a per-part start/tick/stop/break/catch-up system.

**What this blocks:** Unattended dryers, custom fermenters, smelters, extractors, magical flowers, scanners and powered stations whose own state, resource drain and outputs progress independently. Creating another furniture definition does not register an autonomous processor.

**Workaround:** Specialized native process schemas; an appliance shell; an appropriately defined active tool/item proxy; or bounded global/recurring EOC scans. The proxy inherits its own carrier, processing and unload constraints, which must be accounted for.

**Expansion:** Optional registered machine instances with state, interval, eligibility, start/pause/stop/break hooks, resource/inventory endpoints, persistent next-run/last-resolved timestamps, and reasoned cleanup. Furniture and vehicle parts can be adapters. Avoid a full-world per-turn scan: inactive machines should have negligible processing cost, and elapsed-time policy must be explicit.

## TILE-06 — Tile editing needs shared query, result, and conflict policy

**P1 · M–L · Confirmed API boundary.**

**Evidence:** [EOC setters][set_tile] perform radius edits and discard individual setter return values. [Furniture setting][furn_set] does occupancy and map bookkeeping, but can install the furniture before detecting an invalid open-air placement and returning a failure indication. [Door opening][open_door] has a separate check-only route. These are not interchangeable transaction contracts.

**What this blocks:** Placement tools that promise an all-or-nothing footprint, safe restoration of temporary structures, conditional edits that only replace the expected old tile, and reliable “consume materials only if every tile was placed” behavior. A failure return from a low-level helper must not be assumed to imply no mutation occurred.

**Workaround:** Query expected terrain/flags/occupants before editing, control the footprint, and explicitly verify the resulting state. Native construction already provides a better workflow for many building actions.

**Expansion:** Read-only eligibility queries and structured mutation results; expected-old-state/version checks; declared layer/occupancy/support rules; and explicit partial versus atomic footprint policy. Preserve the relevant state/contents and invalidate native caches through existing setters. Roll back only owned changes, rather than blindly restoring an old whole-submap snapshot over unrelated building or destruction.

## MACH-01 — Network resources are electricity-specific

**P1 · L · Confirmed runtime boundary.**

**Evidence:** [Vehicle fuel lookup][fuel_grid] pools battery fuel across connected vehicles, while ordinary fuel is counted locally. Battery charging/discharging has established native accounting. [Power network snapshots][power_network] explicitly describe persisted infrastructure not yet used for runtime generation decisions. [Part data][part_schema] and current producer/consumer paths do not implement the proposed generic `grid_resource` schema.

**What this blocks:** Shared steam, mana, compressed air, coolant, reagent, or mechanical-power networks with storage, connection eligibility, losses, flow direction and resource-aware consumers. A local tank holding a custom liquid is not automatically a network resource.

**Workaround:** Existing electric appliances and fuel tanks, explicit EOC variable transfers, or representing a custom resource using an established backend when the compromise is acceptable.

**Expansion:** Optional named resource adapters over storage/transfer/consumer endpoints, with conservation and declared units/losses. Begin by expressing current electricity unchanged, then add one non-electric resource. Pumps/pressure and kinetic mechanisms need their own semantics; renaming battery charge alone does not simulate them. Build on the existing machine-grid proposal rather than treating its unimplemented example JSON as available.

## MACH-02 — Connectivity and signals need a reusable graph contract

**P1 · M–L · Confirmed partial support; expansion judgment.**

**Evidence:** Electrical grid traversal provides one connection model; [power-network state][power_network] tracks its components. [Tile open/close][open_door], [manual vehicle activation][machine_manual], trap triggers and EOC coordinate edits provide other specific forms of control. Those do not constitute a generic graph of addressable devices and typed signal/resource ports.

**What this blocks:** Relays, logic gates, remote sensor buses, configurable security systems, interrupted wires, paired controls and machines, and a signal sent to one installation rather than whichever matching tile is nearby. Visual connection groups do not establish an operational circuit.

**Workaround:** Named variables and explicit location links; existing vehicle cables and native remote-control systems; bounded scripted logic for a fixed puzzle layout.

**Expansion:** Opt-in connection endpoints with instance IDs, port roles, direction/channel, connection/disconnection events and traversal queries. Keep discrete control signals separate from conserved resource flow. Define signal ordering, loop limits, delay, persistence and behavior when an endpoint moves or unloads. A simple linked-device registry can unlock useful content before any circuit solver is considered.

## MACH-03 — Automation needs owned jobs and explicit catch-up

**P0 · L · Confirmed integration gap.**

**Evidence:** [Character activities][activity_hooks] already persist work and expose hooks. Crafting and zone/NPC systems have specialized work and reservation logic. [Map actualization][actualize] and [vehicle renewable catch-up][vehicle_catchup] update specific systems, not arbitrary custom processing. [Delayed EOC scheduling][queue] requires a Character alpha for non-global callbacks and is not a general machine-owned job queue.

**What this blocks:** Autonomous production chains, feeding real items through machines without an invisible Character, stable throughput while away, correct power-starvation pauses, and simultaneous workers that do not spend the same input twice. “Apply current rate × all missed time” is wrong when input, output space or power ran out during that interval.

**Workaround:** NPC errands, loot zones, Character crafting, specialized native processes, or scripts that reconcile a simple independent machine on return. Existing rot/growth/renewable catch-up is real and should not be reported as absent.

**Expansion:** Machine-owned jobs with exact input/output references, reservations, resource endpoints, progress, blockage and resume policy. Add bounded elapsed-time settlement with explicit dependencies, not unlimited tick replay. Define whether a process freezes, settles analytically, or uses a coarser simulation while unloaded. Share job primitives with activities where useful, preserving their different owners and clocks.

<!-- source-links -->
[submap]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/submap.h#L332
[furniture_talker]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/talker_furniture.h#L19
[examine_eoc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/iexamine_actors.cpp#L296
[bash_hooks]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/mapdata.h#L81
[run_bash]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/map.cpp#L4631
[open_door]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/map.cpp#L5508
[ter_set]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/map.cpp#L2657
[furn_set]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/map.cpp#L2141
[set_tile]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npctalk.cpp#L9657
[trap_schema]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/trap.h#L158
[trap_eoc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/trapfunc.cpp#L494
[multi_tile]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/mapdata.h#L770
[actualize]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/map.cpp#L10098
[item_process]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/item.cpp#L4931
[vehicle_catchup]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/vehicle.cpp#L8627
[machine_manual]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/vehicle_use.cpp#L2423
[fuel_grid]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/vehicle.cpp#L3869
[power_network]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/power_network.h#L22
[part_schema]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/vehicle.h#L262
[activity_hooks]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/player_activity.cpp#L65
[queue]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npctalk.cpp#L7370
