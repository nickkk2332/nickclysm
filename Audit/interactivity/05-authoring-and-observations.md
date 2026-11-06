# Authoring support and current code observations

**Implementation follow-up:** The four concrete correctness observations below have been addressed in the working tree. See [the twelve-fix record](../correctness-fixes.md) for behavior, regression coverage, validation and remaining architectural scope. The original snapshot/evidence below is retained.

The first two findings concern expansion. The remaining four describe current source paths worth characterizing before implementing richer interactions. None was runtime-tested or fixed during this audit.

## AUTHOR-01 — Actor routes do not discover and execute every interaction equally

**P1 · M · Confirmed path divergence.**

**Evidence:** [Tile examine selection][examine] lets the avatar choose from multiple options but selects the first available actor/function for an NPC. [EOC NPC examination][npc_examine] explicitly limits native examination to a safe menu-free list or the first EOC examine actor. [Character item invocation][item_invoke] can invoke a specified usable method, and [EOC activation][activate_item] can supply a location. Editable NPC JSON goals and errands already provide useful automation infrastructure.

**What this blocks:** A companion choosing “refill,” “empty,” or “repair” on a custom station, a technician using a tool on a particular target without opening player menus, and autonomous workers selecting the correct interaction by capability rather than a hardcoded action list. A content script may technically run for an NPC without the NPC knowing when or why to choose it.

**Workaround:** Write explicit JSON goals/errands and menu-free EOC branches; use supported targeted activities and native examine operations. A dedicated script for one workstation is viable now.

**Expansion:** Actor-neutral action discovery and invocation with pure eligibility/reason/target queries, explicit parameters in place of prompts, and results AI can act on. Player menus should collect arguments for the same action request. Add supported-action metadata and rejection reasons; do not expose arbitrary player-facing native functions to NPCs just because they accept `Character&`.

## AUTHOR-02 — Semantic validation and runtime inspection need expansion

**P1 · M · Confirmed authoring risk; tooling recommendation.**

**Evidence:** [Item action loading][item_actions], [tile examine loading][examine_loader], [field processor selection][field_processors], [manual part EOC invocation][machine_manual], and the specialized process paths demonstrate that valid data can have different meanings and context by consumer. Current EOC tracing, existing validation/tests, and NPC decision inspection are useful, but there is no common inspector for the proposed tile/machine/job/connection state.

**What this blocks:** Authors diagnosing why a new field does not behave like fire, which actor a dropped item sees, why an appliance stopped while unloaded, which pocket rejected a transfer, or which job owns a reserved input. A parser accepting a field is not evidence that every execution route uses it.

**Workaround:** Existing EOC trace, debug views/logging, source inspection, explicit support matrices, and focused content tests. The older audit findings must be checked against current implementations rather than copied as still-open defects.

**Expansion:** Consumer-aware validation and inspection: supported actors/targets, context roles, honored fields, units, persistent variables, job state, last/next processing time, resource transactions, connected ports and failed conditions. Validate examples through their actual route. Measure job/graph/query costs, and place explicit budgets on region scans, reaction chains, queued work and catch-up. No benchmark numbers or universal limits are established by this audit.

## OBS-01 — Drop actions use avatar context

**P1 · S–M · Statically confirmed context issue.**

**Evidence:** [The map-aware `item::on_drop`][item_drop] obtains `get_avatar()` and passes it as the Character to `drop_action.call`. It has no dropping-actor argument. [Map placement][map_place] and [pocket leaking][container_leak] also call drop handling, so this callback is broader than the avatar deliberately selecting a drop command.

**Consequence:** A custom drop EOC may read the avatar's traits, stats or variables when the causal action came from another actor, a spill or a placement operation. Item/location context on these paths also needs deliberate testing; an item being placed is not necessarily already in the nominal actor's inventory or destination stack.

**Recommended characterization/fix:** Distinguish placement cause from deliberate drop. Propagate an optional true actor/source, exact item reference and destination, with an environmental/no-actor case. Keep the old behavior only as an explicit compatibility policy if content relies on it. Test avatar drop, NPC drop, leaking nested container, generated/placed item, and a terrain-destruction drop. Check both callback attribution and final quantity/location.

## OBS-02 — Terrain examination identifies furniture in `this`

**P1 · S · Statically confirmed context limitation.**

**Evidence:** [The EOC examine actor][examine_eoc] always writes `here.furn(examp).id().str()` to `this`, plus the tile location to `pos`. [The common terrain/furniture loader][examine_loader] allows EOC examine actors for either kind of tile. A bare terrain tile therefore supplies its furniture ID, normally `f_null`, rather than the examined terrain's ID.

**Consequence:** A shared script cannot treat `this` as the identity of whatever object was examined. Two different bare terrain interactions can receive the same value. Authors can still query the terrain at `pos`; the loss is a reliable direct context contract, not the complete inability to discover the terrain.

**Recommended characterization/fix:** Preserve/document the existing meaning if compatibility requires it, and add explicit terrain/furniture IDs plus examined layer/definition and actor/source context. Test bare terrain, furniture over terrain, two distinct definitions sharing one EOC, and manual versus `u_examine`/`npc_examine` entry points.

## OBS-03 — Falling-item damage is applied after placement of the destination copy

**P1 · S–M · Statically confirmed ordering issue; observable final damage needs a targeted test.**

**Evidence:** [The falling-item routine][fall_items] iterates items at the old location, calls `add_item_or_charges(below, i)`, then later may call `i.inc_damage()` on the original item. After processing it bashes the lower tile and clears the old tile. The fall-specific increment therefore does not directly update the already placed destination copy.

**Consequence:** That damage step can be lost from the object that survives the fall. The later lower-tile bash can independently damage objects, so this is **not** a claim that falling items can never be damaged. Merge behavior, landing damage and callbacks also make “place first, damage the old reference later” an unsafe basis for new item-impact content.

**Recommended characterization/fix:** Define the intended fall/damage/placement order and operate on the actual surviving item/result reference. Test a positive-mass item with fall damage forced deterministically, distinguish the fall increment from lower-tile bash damage, and include merging, fragile contents and an item destroyed during impact. Audit zero-damage arithmetic separately rather than assuming a test of ordinary mass covers all quantities.

## OBS-04 — Emission formulas evaluate against the avatar

**P1 · M · Statically confirmed source-context limitation.**

**Evidence:** [`map::emit_field`][field_emit] constructs a dialogue with `get_avatar()` as alpha and no beta, then evaluates the emission chance, quantity, field and intensity. The function accepts a location and emission definition, not an emitting object/actor context. It is also used by terrain/furniture and other non-avatar emitters.

**Consequence:** A formula authored to depend on the source machine, placed object, monster or local environmental state cannot use those as its actual talkers through this route. Reading actor values can accidentally make autonomous environmental output depend on the player's state. Constant-value emissions remain useful and are not affected in the same way.

**Recommended characterization/fix:** Introduce explicit emitter/source and environmental location context, using no Character when none caused the emission. Decide how legacy avatar-dependent emissions migrate. Test two otherwise identical emitters with different instance state, an NPC/monster emitter, terrain/furniture, and a change to avatar stats that should not alter an independent machine's output.

## Relationship to existing audit chapters

The observations and architectural gaps overlap several older item, map, inventory, examine and vehicle chapters. They are repeated here only where the inspected current source still matters for custom interactivity. Conversely, the capacity queries, item-use outputs, crafting/construction hooks and NPC errands were verified as current capabilities and are not filed again as missing.

<!-- source-links -->
[examine]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/mapdata.cpp#L728
[npc_examine]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/iexamine_eoc.cpp#L150
[item_invoke]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/character.cpp#L4702
[activate_item]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npctalk.cpp#L7208
[item_actions]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/item_factory.cpp#L2231
[examine_loader]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/mapdata.cpp#L1273
[field_processors]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/map_field.cpp#L2243
[machine_manual]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/vehicle_use.cpp#L2423
[item_drop]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/item.cpp#L5236
[map_place]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/map.cpp#L6138
[container_leak]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/item_pocket.cpp#L2079
[examine_eoc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/iexamine_actors.cpp#L296
[fall_items]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/map.cpp#L3341
[field_emit]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/map_field.cpp#L2163
