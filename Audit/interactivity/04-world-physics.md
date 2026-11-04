# World physics and interaction depth

The current engine contains substantial physical simulation. These findings distinguish that support from a general mechanism that modders can reconfigure. Most recommendations here are optional extensions for particular content, not prerequisites for adding ordinary items, terrain or recipes.

## PHYS-01 — Properties do not fully determine physical behavior

**P1 · M–L · Confirmed dispatch boundary.**

**Evidence:** [Materials][materials] define useful resistances, density, heat/freezing, conductivity, burn and salvage properties. [Field definitions][field_types] expose gas spread, decay, flags and intensity effects. [Field processor selection][field_processors] installs some generic behavior from properties, but acid/fire/electricity and other special behavior are selected by specific field IDs. [Electric field processing][field_electric] uses its own target/spread heuristics, rather than being a reusable circuit model for any conductive material.

**What this blocks:** A new-ID acid, unusual flame, reactive liquid or conductive hazard inheriting all the intended native physical behavior merely by setting a familiar flag. A field that hurts like fire need not consume fuel, ignite surroundings or produce the same byproducts as native fire.

**Workaround:** Reuse established types where acceptable; combine generic field/status properties with EOC damage/transforms; explicitly author interactions for a finite content set. Existing material and field properties are useful, not cosmetic placeholders.

**Expansion:** Named reusable behavior components or processor profiles with validated parameters, applicable to new types. Start by exposing existing processors without changing vanilla behavior. Separate hazard effects, physical reactions, propagation and presentation so a mod can choose each deliberately. Avoid a generic “has fire” flag whose implications differ by subsystem without documentation.

## PHYS-02 — Contact chemistry lacks a general reaction interface

**P2 · L · Confirmed schema boundary; scope requires design.**

**Evidence:** [Material data][materials] includes burn products and phase-related properties. [Brewable items][brew] define time/products; recipes and specialized processors transform known inputs. [Pocket schemas][container_schema] restrict containment but do not define mixture composition or a general reaction callback. [Field processors][field_processors] include special relationships such as native fire/fungus/extinguisher handling rather than arbitrary content-defined reaction pairs.

**What this blocks:** Reagent mixtures with concentration, corrosion dependent on exposure, contact explosives, catalysis, contamination transfer, coatings, and configurable reactions between a material, item, field and tile. A recipe can make a chemical product without the same chemicals reacting when physically brought together elsewhere.

**Workaround:** Explicit recipes, discrete item states, custom use/examine actions and controlled EOC exchanges. Many interesting alchemy or chemistry mods can use these intentionally abstract rules.

**Expansion:** Begin with bounded reaction rules: typed participants/material tags, conditions, exact quantities, environment requirements, products/heat/fields, and trigger stage. Define conservation, priority, once-per-contact behavior and loop prevention. A finite reaction table is a smaller and more useful first target than universal real-world chemistry. Preserve reagent identity and metadata through the item-transfer/process interfaces.

## PHYS-03 — Fluids and gases lack a shared volume/pressure model

**P2 · L · Confirmed mechanism boundary.**

**Evidence:** [Liquid handling][fluid_transfer] transfers liquid items to recognized destinations. [Map liquid sourcing][fluid_map] distinguishes configured sources, including sources with infinite charges; finite quantities also exist. [Gas fields][field_types] use intensity, spread and age/decay. Native map water depth is represented through terrain types/flags; it is not automatically a conserved per-tile water volume shared with containers and pumps.

**What this blocks:** Drain a lake by removing a measured volume, flood a basement according to height, equalize pressure across an airlock, compress gas into a cylinder, or run a hydraulic network using the same fluid state as spilled items. An airtight pocket is not a room atmosphere; a toxic field is not a measured number of gas molecules.

**Workaround:** Native liquid transfer, finite reservoirs represented by items/tanks, controlled terrain swaps for flood levels, gas hazards, and scripted room states. These can model a puzzle or device without general fluid simulation.

**Expansion:** Choose the needed scale explicitly: first finite reservoir/pump adapters, then optionally room compartments with composition/pressure, then local flow if content warrants it. Define transfer units, capacity, gravity/head, mixing and conservation. Do not require expensive cell-by-cell fluid dynamics just to support a configurable pump or airlock.

## PHYS-04 — Temperature effects are not a thermal compartment simulation

**P2 · L · Confirmed temperature-path boundary.**

**Evidence:** [Weather temperature lookup][weather_temp] combines ambient conditions with heat radiation, convection and map modifiers. [Map temperature modifiers][map_temp] are stored at submap scope. Native item/container processing models insulation, temperature, rot, refrigerators and other special cases; [material data][materials] includes heat and freezing properties. These do not form a common room heat-capacity/conduction/ventilation energy model.

**What this blocks:** A sealed industrial oven heated by measured fuel energy, a room cooling through a particular open vent, heat storage in machine masses, boiling-driven pressure, or a closed-loop coolant system that conserves energy between its components. A recipe's “requires heat” check and a warm tile are not those mechanisms.

**Workaround:** Specialized heating/cooling appliances, insulation and spoilage rules, native item temperature processing, discrete hot/cold states, and scripted process conditions.

**Expansion:** First expose reliable environmental/item temperature queries and declared process thermal inputs. Add optional thermal nodes/compartments only for content that needs capacity and transfer. Define how radiation, weather, refrigeration and catch-up interact. Distinguish “set a temperature modifier” from “add energy,” with explicit units so authoring does not accidentally invent a perpetual heat source.

## PHYS-05 — Structural collapse is not configurable load-path physics

**P2 · L · Confirmed support model.**

**Evidence:** [Collapse checks][collapse] count nearby support/collapse/wall flags and derive a probability-related value; [collapse propagation][collapse_at] recursively checks nearby tiles above. [Gravity processing][gravity] and construction already support floors, open air and falling objects. They do not calculate arbitrary beam loads, span strength or stress from every object and occupant.

**What this blocks:** A bridge that fails at a defined carried load, different beam spans/material strengths, a crane affecting a support foundation, or a multi-tile shelf failing from its accumulated inventory weight under one configurable model. A terrain resistance value is not a load-bearing capacity.

**Workaround:** Existing support flags and native collapse, plus scripted weight thresholds or controlled destruction for a specific structure. Native destructible buildings are already viable content.

**Expansion:** Expose support queries and failure observations first. If engineering gameplay needs more, use opt-in structural assemblies with declared supports, spans, capacities and loads rather than replacing all terrain with a full stress solver. Tie failure to existing destruction/gravity paths, with explicit processing and unload bounds.

## PHYS-06 — Props and mechanisms lack reusable constraints

**P2 · L · Confirmed fragmented models; expansion judgment.**

**Evidence:** [Gravity][gravity] handles items/furniture/vehicles through separate routes. [Falling furniture][fall_furn] estimates weight from its movement strength requirement, while [falling items][fall_items] use item mass and impact calculations. [Vehicle collisions][vehicle_collision] provide a much richer separate motion model. Cables, climbing aids, vehicles and existing pulling/throwing functions cover particular relationships, not a JSON-defined system of general rigid bodies and constraints.

**What this blocks:** Pulleys with tension, weighted seesaws, freely toppling cabinets, chained objects, a crane lifting an occupied object, or a rope that transmits force and breaks according to anchors and load. Scripted teleportation/relocation can reproduce a destination without modeling the intervening physical relationship.

**Workaround:** Existing vehicle mechanisms and cables, drag/move operations, climbing tools, grapple/fling mechanics, and a deliberately abstract scripted device. The powers audit covers related forced-movement context gaps.

**Expansion:** Define a small set of opt-in physical operations and constraints—anchor, tether, lift, push, rotate—with typed participants, mass/force limits, collision and result hooks. Start with one practical hoist/weight puzzle. Full rigid-body simulation is a distinct large project, not a field that can be added to an item definition.

## PHYS-07 — Environmental sensing needs consistent queries and events

**P2 · M–L · Confirmed propagation boundary.**

**Evidence:** [Sound distance][sound] is horizontal distance plus vertical attenuation, not a traversal of intervening material geometry. [Scent diffusion][scent] explicitly uses scent-blocking/reducing flags, and lighting/vision have their own systems. [Trap data][trap_schema] already provides sound thresholds; EOCs have useful light/field/weather/location queries. There is no common sensor interface for all these environmental channels and their changes.

**What this blocks:** Material-aware soundproofing, hearing through a vent rather than a wall, configurable chemical detectors, beam-break sensors, and devices that react only when a local measured value crosses a threshold. A sound effect for a material does not alter acoustic propagation.

**Workaround:** Native hearing/noise, light/visibility and scent flags, sound-triggered traps, periodic EOC queries and controlled puzzles with known links.

**Expansion:** Document channel-specific truth first: what the sensor actually measures and which obstacles matter. Add reusable read-only sample queries and bounded threshold subscriptions with origin/range/dimension. Introduce improved acoustic or chemical propagation only if the chosen content needs it; do not imply every channel already shares light-style occlusion. Preserve visibility/information limits for player feedback and NPC behavior.

## PHYS-08 — Growth is configurable but not a general ecology process

**P2 · M–L · Confirmed growth model boundary.**

**Evidence:** [Seed data][seed] provides growth stages, growth temperature, terrain requirements, fruit, seeds and byproducts. [Plant actualization][growth] uses the seed item, age/stages and furniture transforms; [map catch-up][actualize] also runs selected restocking, sap and environmental updates. These are meaningful data-driven systems, not a prohibition on custom crops.

**What this blocks:** An independently stateful soil bed with moisture/nutrient/pollution budgets, irrigation devices affecting many plant types, custom symbiosis or pest cycles, and a new biological process that runs through a general environment-aware growth lifecycle. Existing stage-duration parameters do not automatically make those resources causal.

**Workaround:** Configure seeds and harvests, use discrete terrain/furniture states, timed item processes and explicit EOC plant/machine behavior. A useful farming expansion can remain intentionally abstract.

**Expansion:** Reuse tile state and process jobs for optional growth inputs, environmental queries, stage/harvest hooks and elapsed-time settlement. Begin with one measurable resource, such as bed moisture, before attempting an ecosystem. Define whether harvested outputs retain growth/contamination provenance and how neighboring sources affect a bed while unloaded.

<!-- source-links -->
[materials]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/material.h#L21
[field_types]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/field_type.h#L188
[field_processors]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/map_field.cpp#L2243
[field_electric]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/map_field.cpp#L671
[brew]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/itype.h#L316
[container_schema]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/item_pocket.h#L34
[fluid_transfer]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/handle_liquid.cpp#L264
[fluid_map]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/map.cpp#L6291
[weather_temp]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/weather.cpp#L1320
[map_temp]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/map.cpp#L5763
[collapse]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/map.cpp#L4265
[collapse_at]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/map.cpp#L4321
[gravity]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/map.cpp#L3181
[fall_furn]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/map.cpp#L3195
[fall_items]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/map.cpp#L3341
[vehicle_collision]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/vehicle_move.cpp#L745
[sound]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/sounds.cpp#L274
[scent]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/scent_map.cpp#L189
[trap_schema]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/trap.h#L158
[seed]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/itype.h#L1256
[growth]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/map.cpp#L9777
[actualize]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/map.cpp#L10098
