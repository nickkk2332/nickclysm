# Items and contextual actions

Priority and effort definitions are in [README.md](README.md). These are gaps in reusable authoring contracts, not a claim that bespoke scripts cannot imitate an outcome.

## ACTION-01 — No common action request, result, and commitment contract

**P0 · M–L · Confirmed interface fragmentation.**

**Evidence:** [EOC item use][eoc_use] has user/item talkers, location, and charge/cancel outputs. [Item activation from an EOC][activate_item] invokes a method on an item, optionally at a location, without exposing its result to that EOC caller. [Tile examination][examine] dispatches functions/actors with different menu/return behavior. [Item movement effects][item_effects] have their own success/failure branches. [Crafting result callbacks][craft_result] and [activity hooks][activity_hooks] provide other specialized contexts.

[Character invocation][item_invoke] can also return false for a zero-charge use even after the EOC has performed effects. That return value is not a general statement that the world interaction failed.

**What this blocks:** A reusable “apply this selected tool to this specific object/tile” action with consistent reach, eligibility, quantity, time, ownership, target choice, cancellation and outcome. Examples include repairing a particular mechanism, installing a component in a machine, draining a vessel, or testing a device before consuming its test reagent.

**Workaround:** One EOC action selects targets and performs checks/mutations itself. This is practical for controlled interactions. `_cancel` can prevent ordinary spending, but cannot roll back arbitrary side effects already performed by earlier EOCs.

**Expansion:** An optional action request with actor, source/tool, typed target(s), method ID, quantity, location/dimension and declared cost policy. Separate read-only eligibility/preview, commitment, and structured result. Reuse native actions behind adapters. Return started/completed/cancelled/refused and reason; dispatch is not proof that work completed. Preserve free effects and existing actor-specific behaviors.

## ITEM-01 — Action identity is tied to implementation type

**P1 · M · Confirmed schema boundary.**

**Evidence:** [Item action parsing][item_actions] returns a map key based on action `type`; [the item definition][item_actions_schema] stores `use_methods` as a map. `repair_item` has a specialized alternate action-type route, but general EOC actions do not have independent IDs separating “scan,” “calibrate,” and “reset” from the EOC implementation type. The EOC actor's [custom `menu_text`][eoc_use_load] changes presentation, not that identity.

**What this blocks:** Multiple independently discoverable native verbs on the same custom item using one generic implementation, with separate conditions, costs, targeting and results. Tool qualities can expose existing use methods, but do not register an arbitrary new native interaction algorithm.

**Workaround:** A single EOC action opens a selector/checklist or text window and implements sub-actions. Different existing actor types can coexist normally.

**Expansion:** Give actions a stable instance ID and a separate implementation kind. Add optional eligibility/reason, target schema, costs and result callbacks per action; make hotkeys, EOC invocation and NPC discovery reference the action ID. Migrate old map keys compatibly. Do not require a new C++ actor class just to add another menu verb.

## ITEM-02 — Item state access remains incomplete and resource-specific

**P1 · M · Confirmed API coverage gap.**

**Evidence:** [Item talkers][item_talker] expose variables, flags, durability/degradation, weight/volume, qualities and limited power information. [Their power writer][item_power] specifically sets battery ammo. [The math registry][math_registry] already exposes container capacities and contents totals. C++ item state includes active/countdown state, ammo identities/quantities, temperature/rot/wetness, faults and other fields, but those are not one symmetric, typed query/mutation interface in EOCs. Some useful predicates such as “has ammo” and “is rotten” already exist.

**What this blocks:** A generic calorimeter, configurable drying machine, non-battery reagent tool, timed incubator, inspectable damaged mechanism, or content that reacts to exact item temperature instead of a yes/no food state. Using a battery-style setter for arbitrary ammo is not a general resource adapter.

**Workaround:** Item variables, transformations into discrete states, known resource routes, predicates, and specialized actors. Capacity queries are already a strong base.

**Expansion:** Typed item property access with documented units, supported item classes, read/write scope, bounds, and cache/processing invalidation. Prioritize exact charges/ammo, active/countdown, temperature/rot/wetness, fault queries, and item age. Expose validated operations rather than allowing scripts to mutate every private field without maintaining invariants.

## ITEM-03 — Lifecycles lack consistent instance-aware callbacks

**P0 · M–L · Confirmed hook boundary.**

**Evidence:** [Pickup][item_pickup] performs ownership, contents and acquisition bookkeeping. Pocket insertion/removal/unsealing has internal [contents-change handling][container_change]. Those are not a shared JSON lifecycle system. The [serialized Character/item event payload][item_event] identifies Character and type, but **current wear/takeoff/armor-destruction and wield paths already send an exact item beta talker** ([attire dispatch][attire_events], [wield dispatch][wield_events]). EOC use and consumption EOCs also have richer context. The gap is the remaining lifecycle coverage and consistent semantics, not missing equip callbacks; `drop_action` exists with the limitation in OBS-01.

**What this blocks:** “This particular cursed object binds on pickup,” “notify this container when its contents change,” “run cleanup when this non-armor gadget is destroyed,” and “update a device only when its specific component is removed” across all routes. A curse triggered by equipping a known item can already use the richer event path. Scanning inventory in response to a type-only event cannot substitute for a missing instance callback in other paths.

**Workaround:** Use current equip/armor event beta talkers, explicitly invoke follow-up EOCs in custom actions, use known activity hooks, or reconcile state periodically. This works when the relevant routes are covered or controlled by the mod.

**Expansion:** Complete an instance-aware before/after lifecycle across acquire/release, equip/unequip, insert/remove, seal/unseal, activate/deactivate, damage/break, transformation and destruction, retaining the callbacks already present. Include actor or explicit environmental source, exact item/pocket references, old/new owner/location, cause and result. Decide which stages permit vetoes and which are observation only. Avoid firing semantic “picked up” hooks during save loading or harmless inventory reshuffling.

## ITEM-04 — Pockets need targeted transfer and containment operations

**P1 · M–L · Confirmed binding gap; rich containment already exists.**

**Evidence:** [Pocket data][container_schema] includes capacity, restrictions, sealing, spill, watertight/airtight, insulation and spoilage properties. [Contents processing][container_process] carries these properties into native item processing. [Math queries][math_registry] expose aggregate capacity/contents values. [EOC item errands][item_effects] move items into a Character's inventory/hand/clothing or onto its tile; they are not a general “move N from source pocket A to destination pocket B” interface.

**What this blocks:** Automatic sorters, reagent feeders, reactive containers, machine input/output ports, exact-quantity decanting, and pocket-specific conditions without using a Character as an intermediary or replacing the real item with a freshly spawned copy.

**Workaround:** Existing inventory zones, NPC errands, item iteration, reload/unload activities, item restrictions, and custom menu exchanges. These support useful manual workflows.

**Expansion:** Addressable pocket references; enumerate/inspect children; validate fit/seal/access/ownership; split and transfer exact quantities; insert/remove/seal/unseal with structured results and optional events. Add item/container/vehicle endpoints before conveyors. Return both moved quantity and remainder, including merge/spill policy. Preserve contents and instance metadata; aggregate free volume is not sufficient evidence that a particular object fits a particular pocket.

## ITEM-05 — Quantities, transformations, and persistent identity need explicit policy

**P1 · M–L · Confirmed representation/binding boundary.**

**Evidence:** [Item locations][item_locs] are serializable handles and explicitly have invalidation conditions; they already use UID-assisted lookup in the current implementation. Native items support [split][item_split], [merge][item_merge], and [convert][item_convert]. [EOC transformation][item_transform] converts the beta item; conversion rebuilds contents against the new type's pockets and updates related state. These do not constitute a general script-level reference and quantity transaction with mapping from old to new participants.

**What this blocks:** Marking an exact batch for later processing, preserving ingredient provenance through division and recombination, removing only the transferred quantity after a stack merge, and keeping a reference to a specific installed component after assembly or disassembly. Copying an item and recreating it by type is not identity preservation.

**Workaround:** Existing item talkers and locations, item variables, constrained workflows, and explicit post-result initialization. Recognize that a moved charge-counted item may merge into a larger stack; subsequent operations may then act on that whole stack.

**Expansion:** Script-visible typed item/pocket references and quantity-aware operation results. Define UID, variables, ownership, reservation and provenance behavior for split/merge/convert/consume; define missing/unloaded/transformed reference outcomes. Reuse current locations/UIDs rather than claiming items have no identity at all. Expose transformation preservation policies deliberately instead of requiring authors to infer them from item internals.

## ITEM-06 — Production hooks are not a general process contract

**P1 · M–L · Confirmed partial coverage.**

**Evidence:** [Recipes][craft_hooks] already have result, per-result-item and disassembly EOCs, and [completion code][craft_result] supplies recipe/batch context. [Activities][activity_hooks] and [construction][construction] have lifecycle hooks. [Brewable items][brew] configure time and products, while [fermentation furniture][ferment] still uses specialized vat states; [milling][mill] has a specialized time/processing path. [Fault fixes][faults] are another specialized requirements/time mechanism.

**What this blocks:** A reusable process that inspects exact inputs, reserves them, accumulates work/heat/resources, pauses or spoils on interruption, then transforms those inputs into traceable outputs while an appliance or tile owns the job. A recipe-completion EOC does not give every other process an equivalent lifecycle or an input-instance list.

**Workaround:** Recipes and workbenches for Character labor; specialized fermentation/milling/repair schemas for their supported cases; EOC menus/variables for bespoke machine exchanges. Activity and result hooks can add a great deal without new code.

**Expansion:** Optional process/job adapters over recipes, repairs and world machines: eligible input set, reservations, progress state, resource/time policy, start/pause/resume/fail/finish, exact inputs and result-item references. Keep labor, autonomous processing and elapsed-time catch-up distinct. First expose missing inputs/results around existing processes; a universal manufacturing simulator is not necessary for every new recipe.

<!-- source-links -->
[eoc_use]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/iuse_actor.cpp#L6100
[activate_item]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npctalk.cpp#L7208
[examine]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/mapdata.cpp#L728
[item_effects]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npctalk.cpp#L4474
[craft_result]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/crafting.cpp#L3867
[activity_hooks]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/player_activity.cpp#L65
[item_invoke]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/character.cpp#L4702
[item_actions]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/item_factory.cpp#L2231
[item_actions_schema]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/itype.h#L1512
[eoc_use_load]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/iuse_actor.cpp#L5979
[item_talker]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/talker_item.cpp#L108
[item_power]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/talker_item.cpp#L200
[math_registry]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/math_parser_diag.cpp#L2784
[item_pickup]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/item.cpp#L1516
[container_change]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/item_pocket.cpp#L1997
[item_event]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/event.h#L204
[attire_events]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/character_attire.cpp#L370
[wield_events]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/character_inventory.cpp#L3048
[container_schema]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/item_pocket.h#L34
[container_process]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/item_pocket.cpp#L2062
[item_locs]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/item_location.h#L47
[item_split]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/item.cpp#L506
[item_merge]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/item.cpp#L1037
[item_convert]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/item.cpp#L433
[item_transform]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npctalk.cpp#L7257
[craft_hooks]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/recipe.h#L302
[construction]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/construction.h#L94
[brew]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/itype.h#L316
[ferment]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/iexamine.cpp#L3680
[mill]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/iexamine.cpp#L326
[faults]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/fault.h#L55
