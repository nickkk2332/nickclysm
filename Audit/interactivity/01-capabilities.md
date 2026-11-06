# Current content capabilities

See [the overview](README.md) for scope, terminology, and priorities. The examples below describe feasible designs, not tested new content added by this audit.

## Existing building blocks

| Surface | What is supported now | Important limit |
| --- | --- | --- |
| Item definitions | Materials, weight/volume, qualities, flags, durability, faults, pockets, charges/ammo, transformations, variants and recipes | A new type only gets behavior implemented by the selected properties, flags and actors |
| Conditional gadgets | EOC use actions have conditions, custom menu text, worn/wielded requirements, user and item talkers, and target location | Separate named actions of the same implementation type are not a general item schema |
| Use accounting | EOC use can return `_charges` and `_cancel`; native invocation handles spending/refund behavior | Cancellation does not undo arbitrary earlier script mutations |
| Containers | Capacity, weight/size restrictions, liquid/gas eligibility, sealing, spill behavior, insulation, spoilage, nested contents | Not a general reactive chemistry vessel or pressure simulation |
| Item scripts | Instance variables, flags, HP/degradation, some resource access, inventory/map iteration, pickup/wield/wear/drop, targeted activity starts | Item state and per-pocket operations are not fully exposed; three-party transfers are cumbersome |
| Tile definitions | Terrain/furniture, movement/visibility properties, storage volume, open/close transforms, harvest, emissions, bash/shoot/tool activity metadata | Most persistent furniture is a type ID at a coordinate, not an independently stateful object |
| Tile interaction | Multiple named examine actions, EOC examine, existing tool operations, doors/gates, construction and disassembly | Examine does not supply the same target/result contract as item use; NPC choices differ |
| Destruction | Bash thresholds/results, drops, spawned fields, hit/destroy EOCs, support checks, falling items/furniture and vehicle collisions | Not every replacement/removal is a bash; source attribution and callbacks vary |
| Sensors and traps | Creature triggers, item-weight and sound trigger data, custom EOC or spell trap actions | The EOC trap implementation needs a creature and removes the trap after execution |
| Crafting | Requirements, tools/qualities, skills/proficiencies, workbenches, byproducts, in-progress crafts, result/item/disassembly EOCs | Recipe hooks are not a universal input/output job model for unattended machines |
| Construction | Site conditions, partial construction, per-turn and post-completion EOCs, terrain/furniture results | Linked structures and instance-state migration are not automatic |
| Timed work | Serialized activities with start/tick/completion/cancel hooks and optional target-item context | Typically owned by a Character; not every custom machine can own an equivalent job |
| Appliances | Deployed appliances use vehicle/part mechanics, power storage/generation, cables, toggles and some manual EOC activation | Manual EOC activation has limited participants; arbitrary per-part machine state/lifecycles are not present |
| World processes | Rot, temperature, phase changes, growth, harvest, fermentation/milling, weather, fire/gases, renewable catch-up | Each process has its own rules; generic machine catch-up and reaction hooks are missing |
| NPC automation | JSON behavior goals, errands, targeted crafting/reading/reload/etc., and a restricted EOC examine route | A script can automate specific tasks, but arbitrary player-facing menus are not NPC APIs |

Evidence: [item EOC setup][eoc_use_load], [EOC use execution][eoc_use], [pocket schema][container_schema], [math queries][math_registry], [tile examination][examine], [bash hooks][bash_hooks], [trap execution][trap_eoc], [craft hooks][craft_hooks], [construction hooks][construction], [activity context][activity_hooks], [manual vehicle EOC][machine_manual], [map catch-up][actualize].

## Representative custom content

| Desired content | Useful version possible today | Boundary for a deeper version | Main findings |
| --- | --- | --- | --- |
| Multi-purpose scanner or survival gadget | Scriptable: one EOC action opens a selector, checks resources, scans and reports | Independent native verbs, common preview and result/accounting | ACTION-01, ITEM-01/02 |
| Keyed safe or puzzle door | Native/scriptable: item/flag/variable checks, cardreader/examine, terrain transforms and EOC menus | Per-lock state, arbitrary tool-on-lock interaction, actor-aware opening callbacks | TILE-01/02, ACTION-01 |
| Personal diary, inspectable artifact, coded note | Native/scriptable: item variables, dialogue/text windows, conditions | Instance-aware acquisition/destruction hooks, persistent cross-object references | ITEM-03/05 |
| Backpack with special storage rules | Native: pocket restrictions, sealing, insulation, spoil multipliers | React on inserting a specific object, inspect one pocket, choose an exact transfer quantity | ITEM-04 |
| Alchemical flask | Scriptable: recognize configured inputs and exchange them for configured products | General mixing, concentration, heat-dependent reaction and gas pressure | ITEM-04/06, PHYS-02/03/04 |
| Custom foods, crops and brews | Native: recipes, seed growth parameters, harvest outputs and brew duration/results | Individual soil chemistry, irrigation history, competing organisms and new growth logic | PHYS-08 |
| New repairable mechanism | Native/scriptable: materials, faults/fixes, use actions and recipes | Generic fault transitions, per-component inspection and all-path maintenance events | ITEM-02/03/06 |
| Pressure-plate dungeon | Scriptable for creature-triggered EOC plates and linked transforms | Weighted-item EOC activation, repeated enter/exit membership and rearm policy | TILE-03, MACH-02 |
| Stateful workstation | Scriptable while used: examine, variables stored elsewhere, item exchange, activities | State belongs to that placed machine, survives movement/replacement and runs unattended | TILE-01/05 |
| Solar-powered custom appliance | Native for established electrical part functions; scriptable extras | General formula-driven producer/consumer lifecycle and per-part state | TILE-05, MACH-01/03 |
| Magical resource pool and spreaders | Scripted counter/transfer approximation | Named shared resource networks with connectivity, storage and flow accounting | MACH-01/02 |
| Conveyor or automatic sorter | Character/NPC errands and existing loot zones cover useful tasks | Autonomous item-to-container transfer, throughput, blockage and exact quantities | ITEM-04, MACH-03 |
| Refinery or factory chain | Recipes and appliance shells can imitate individual stages | Durable jobs, material reservations, network resources, catch-up and conservation | ITEM-06, MACH-01/03 |
| Remote switch, alarm or puzzle circuit | Scriptable: EOC variables, location edits, sound traps and configured links | Persistent device graph, addresses, signal propagation, relay and break/disconnect rules | MACH-02 |
| Floodable room or drainable reservoir | Scriptable: controlled tile changes and finite liquid items | Conserved fluid volume, height/pressure, spill routing and pump behavior | PHYS-03 |
| Airlock, vacuum room or gas machinery | Scriptable scripted states/hazards; gas fields and airtight containers exist | Room gas composition, pressure, opening equalization and shared atmosphere | PHYS-03/04 |
| New corrosive/fire-like field | Native for generic gas/status/decay properties; scriptable damage/transforms | Reusing actual acid/fire/electric processing under a new ID and new reaction rules | PHYS-01/02 |
| Destructible bridge or building | Native support flags, bash results, construction and collapse behavior | Configurable load paths, beam capacities and weight-dependent failure | PHYS-05 |
| Winch, pulley, crane or seesaw | Existing tools, cables, vehicle machinery and scripted relocation approximate pieces | Shared constraints, anchors, force/tension, linked prop and passenger movement | PHYS-06 |
| Soundproof stealth facility | Existing noise, hearing, traps, sight and scent support basic stealth | Geometry/material-aware acoustics and shared sensor queries/events | PHYS-07 |
| Persistent off-screen base industry | Some established processes catch up already | Generic jobs and resource dependencies with explicit elapsed-time policy | MACH-03 |

## Hook/support matrix

“Present” does not mean every caller uses identical semantics.

| Trigger or operation | Current mod-facing support | Important context/coverage limit |
| --- | --- | --- |
| Activate item | EOC use actor with user alpha, item beta, ID and `use_location`; charge/cancel output | One source item and location are not a generic typed target set |
| Tick/countdown item | Existing actors, including EOC actor, through item processing | May have no carrier; normal tool tick and countdown use different routes; no generic elapsed-time job |
| Drop/place item | `drop_action` exists | Uses avatar context; invoked by broader placement/spill paths, not just a deliberate drop |
| Wear/take off/wield and armor destruction | Supported paths send Character events with the actual item as beta | Serialized payload has type/Character only; some routes such as unwielding have no item beta; other item lifecycles need separate coverage |
| Pickup/container insert/remove/unseal | Internal C++ callbacks exist; selected EOC operations report results | No uniform content-level before/after callback carrying every affected item/pocket |
| Examine tile | Named actor/function list; EOC examine | User alpha, no tile beta; `pos` and furniture-derived `this` |
| Bash tile | `eoc_on_hit`, `eoc_on_destroy` | Source present only where caller supplies it; replacement by other routes is not a bash |
| Step/contact trap | Trap EOCs and spells | EOC branch has creature-only, one-shot semantics |
| Open/close/replace tile | Native functions and explicit EOC setters | No common lifecycle/result hook for all causes |
| Complete craft/disassembly | Recipe callbacks including result-item beta for crafting | Not a general callback for every autonomous process or each input consumed |
| Construction work/finish | `condition`, `do_turn_eocs`, `post_eocs` | Builder and site context; no automatic multi-tile object identity |
| Character activity lifecycle | Start/tick/completion/cancel EOCs | Optional target item supplied by supported actor; does not imply a furniture-owned activity |
| Manually activate vehicle part | `activatable_eoc` through `EOC_ACTIVATION` menu | Current invocation supplies player alpha, no vehicle/part beta/context |
| Load/catch up machine | Existing system-specific catch-up | No universal tile/part process hook with elapsed time and resource dependency semantics |

Sources: [item event schema][item_event], [attire event talkers][attire_events], [wield event talkers][wield_events], [pickup internals][item_pickup], [item drop][item_drop], [tile examine context][examine_eoc], [activity hooks][activity_hooks], [part activation][machine_manual], [vehicle catch-up][vehicle_catchup].

## What is already fixed or available

- Container **weight, volume, counts, remaining capacity** queries exist. Do not repeat the old claim that scripts cannot inspect capacity at all. The remaining gaps are exact pocket/transfer operations and richer item state.
- EOC item use already has a **condition, target location, charge selection and cancellation**. The missing feature is a shared multi-role action/result contract, not the absence of programmable item activation.
- Recipe **result-item and disassembly** EOCs, construction **conditions/work/finish** EOCs, and activity **lifecycle** EOCs are present.
- EOC **pickup/wield/wear/drop** operations preserve much more of the actual item than spawning a replacement. Item locations and UIDs already exist; identity problems should build on them.
- **Wear/takeoff/wield and armor-destruction event paths already provide item beta talkers.** The event's smaller serialized payload must not be mistaken for all of its runtime context.
- Tile **hit/destroy** EOCs, named examine actions, configurable traps, and manual vehicle-part activation already exist.
- **Electrical grids, field gas spread, gravity, structural collapse, vehicle collisions, temperature and growth** are real systems. The expansion target is their configurability/composition, not inventing their first implementation.

## Research boundary

This audit inspected item use and processing, pockets and item talkers, relevant EOC/math routes, terrain/furniture and submap state, examination and traps, recipes/construction/activity hooks, vehicle/appliance/grid paths, fields/materials/liquids, temperature, gravity/collapse, sound/scent, and crop growth. It does not claim an exhaustive audit of NPC economies, every crafting recipe, every vehicle collision case, or ecological population simulation.

<!-- source-links -->
[eoc_use_load]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/iuse_actor.cpp#L5979
[eoc_use]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/iuse_actor.cpp#L6100
[container_schema]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/item_pocket.h#L34
[math_registry]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/math_parser_diag.cpp#L2784
[examine]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/mapdata.cpp#L728
[bash_hooks]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/mapdata.h#L81
[trap_eoc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/trapfunc.cpp#L494
[craft_hooks]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/recipe.h#L302
[construction]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/construction.h#L94
[activity_hooks]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/player_activity.cpp#L65
[machine_manual]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/vehicle_use.cpp#L2423
[actualize]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/map.cpp#L10098
[item_event]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/event.h#L204
[attire_events]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/character_attire.cpp#L370
[wield_events]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/character_inventory.cpp#L3048
[item_pickup]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/item.cpp#L1516
[item_drop]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/item.cpp#L5236
[examine_eoc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/iexamine_actors.cpp#L296
[vehicle_catchup]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/vehicle.cpp#L8627
