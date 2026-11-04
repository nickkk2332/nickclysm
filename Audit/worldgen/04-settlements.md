# Cities and towns with NPCs

A populated hub is already practical. The refugee center provides a concrete example of extensive authored NPC placement. Factions, dialogue, shops, missions, camps, guard posts, and custom behavior can supply considerable depth. The missing layer is a reusable settlement whose identity and state connect those features. [Refugee mapgen][refugee], [NPC behavior][npc-behavior], [factions][faction-model], [camps][camp-model].

## TOWN-01 — Cities, camps, and authored hubs lack a common settlement instance

**Priority:** P0 · **Effort:** L

**Current evidence.** `city` stores location, name, size, database/content IDs, and original population. Specials record footprints and shared generation arguments. Camps separately have owners, workers, resources, expansions, and takeover operations. None of those inspected records is the shared runtime container for a town's places, resident membership, services, event history, and simulation policy. [City model][city-model], [special records][special-records], [camp model][camp-model], [takeover interface][camp-owner].

**Content limit.** Two generated villages using the same content should each have their own mayor, clinic capacity, food crisis, reputation context, and history. A special type ID identifies a template, not necessarily one independently evolving instance. A faction can own several places, so faction-wide variables do not automatically distinguish their state.

**Workaround.** Use an authored camp or special, store coordinates/unique NPCs, and allocate namespaced variables manually. This is appropriate for a small number of known hubs.

**Proposal.** Add a sparse, opt-in settlement instance with stable runtime ID, dimension, footprint or membership regions, archetype, affiliation/claims, references to places and residents, and a versioned state record. Allow a settlement to reference an existing city, special, or camp rather than replacing them. Settlements may occupy a few OMTs or part of a large city; do not equate settlement boundaries with city radius.

## TOWN-02 — Procedural residents need durable identities and population membership

**Priority:** P1 · **Effort:** L

**Current evidence.** Mapgen places NPC templates and supports added traits, mission targeting, and a unique ID. NPC classes provide randomized attributes/equipment, shop data, and work hours. These are useful ingredients for generated residents. The placement piece does not allocate households, settlement roles, or a population registry; `city.population` explicitly means original population. [NPC placement][npc-spawn], [class/shift loading][shift-load], [city record][city-model].

**Content limit.** A living village needs to remember that its blacksmith is the same person after leaving, that a resident died or moved away, and that a new arrival fills a vacant role without recreating the deceased. Household links, dependents, visitors, and temporary evacuees cannot be inferred from a count or from repeatedly running `place_npcs`.

**Workaround.** Author individual templates and unique IDs, then script replacements and relationships. This handles named story settlements well but scales poorly across many procedural towns.

**Proposal.** Define resident-generation profiles with bounded populations, role/skill distributions, optional households/social links, and assignment rules. Allocate resident identities once, independently of detailed-map materialization. Track absent, traveling, dead, displaced, and visiting states explicitly. Unique story characters should remain authored NPCs; abstract residents should materialize only through a documented identity-preserving transition, not be silently rerolled on every visit.

## TOWN-03 — Buildings need named places and assignments that survive map changes

**Priority:** P0 · **Effort:** M–L

**Current evidence.** Mapgen can place zones, ownership, NPCs, and global coordinate variables; specials retain footprints and parameters. NPCs also have guard posts and camp assignments. Those features identify useful coordinates or work areas, but they do not form a shared registry for “the clinic's treatment room,” “this household's bed,” “the night watch post,” or “this shop's public counter.” [Mapgen facilities][mapgen-doc], [location variables][place-vars], [special metadata][special-records], [duty/post behavior][shift-duty].

**Content limit.** Schedules and services need to choose a role-appropriate destination, check access/capacity, and respond when that location burns down or is rebuilt. Matching furniture IDs or storing one global coordinate is fragile when several towns use identical templates, a building rotates, or a workshop is relocated.

**Workaround.** Export a few well-known coordinates and manually update scripts after transformations. Use zones for existing activities and posts for guards.

**Proposal.** Register semantic places within a settlement or generated structure: stable instance identity, role tags, bounds, interaction/standing points, capacity, permissions, and optional equipment requirements. Bind residents and services to place IDs, with fallback selection by role. Keep coordinates as resolved geometry, not the only identity. Destruction, rebuilding, rotation, ownership change, and dimension transfer must invalidate or rebind assignments predictably. This should share structure/tile identity work from the interactivity audit.

## TOWN-04 — Services need availability, staffing, access, and transaction contracts

**Priority:** P1 · **Effort:** M–L

**Current evidence.** NPC classes provide shopkeeper item groups, price rules, restock intervals, and work hours. Dialogue/mission/EOC code can author medical care, repairs, lodging, hiring, and other services. Duty scheduling and shop inventory are separate mechanisms; a work-hours field does not by itself establish a complete service-opening contract. [Class loading][shift-load], [shift duty][shift-duty], [dialogue/EOC operations][eoc-doc], [mission support][mission-doc].

**Content limit.** A clinic should have qualified staff, an available station, permitted customers, supplies, a duration, and a result. An inn needs a reserved bed; a blacksmith needs a queue or an explicit instant-service abstraction. If the assigned provider dies, goes off duty, or evacuates, the town should know whether the service closes or a replacement can take over.

**Workaround.** Script each transaction in dialogue with conditions, costs, time changes, and effects. This remains suitable for services intentionally presented as an abstract exchange.

**Proposal.** Create optional service definitions backed by settlement service instances. Define availability, staffing roles, places/capacity, requirements/costs, reservation, fulfillment method, cancellation/refund, and result hooks. Support both an explicitly abstract transaction and a real activity/work order. UI and NPC behavior should query the same availability state, and service state should not be inferred solely from whether the customer can currently see the merchant.

## TOWN-05 — Settlement resources and change need an opt-in simulation model

**Priority:** P1 · **Effort:** L

**Current evidence.** Factions track resources/opinions and trade rules; camps consume food, track workers/resources, and build expansions. Shops have their own inventory and restock behavior. Missions and EOCs can change supplies and maps. These systems support substantial camp gameplay but do not define a general settlement economy, housing demand, staffing capacity, local construction policy, or resource-driven migration. [Faction state][faction-model], [camp resource/work interfaces][camp-model], [shop configuration][shift-load], [map updates][update-eoc].

**Content limit.** A town exporting grain, suffering a fuel shortage, losing a caravan, building housing, or recovering from a siege needs consistent resource and population consequences. Automatically restocking a merchant is a valid game abstraction; it is not proof that residents produced or transported those goods.

**Workaround.** Represent a few abstract supplies as variables, use camp expansion recipes for selected upgrades, and author incident-specific effects.

**Proposal.** Offer bounded settlement processes with explicit inputs, outputs, labor/capacity, storage, and update intervals. Choose physical or abstract accounting for each resource and define their conversion boundary; never count both an abstract shipment and its spawned cargo. Start with a small useful set such as food, medicine, building materials, housing, and security. Let shortages and surpluses drive authored policies/events. Dynamic expansion should use GEN-05's conflict-aware updates and named places, not overwrite player construction blindly.

## TOWN-06 — Loaded and unloaded settlements need one continuity policy

**Priority:** P0 · **Effort:** L

**Current evidence.** NPC `on_load` performs coarse body/effect/focus catch-up capped at two days, then reconciles selected state, shifts, and shops. Overmap travel and companion missions also abstract selected actions away from detailed movement. These are real offscreen mechanisms; they do not execute every resident's behavior tree or reproduce a town's elapsed work and incidents. [NPC catch-up][npc-catchup], [travel loop][npc-travel], [companion lifecycle][camp-mission-events], [shift reconciliation][shift-reconcile].

**Content limit.** A town should not produce extra goods because it is briefly loaded, forget a siege when unloaded, or recreate an absent merchant on return. Conversely, continuously simulating thousands of NPCs at tile resolution would be unnecessary and expensive. The challenge is maintaining one causal state across levels of detail.

**Workaround.** Keep settlements mostly static; apply manually authored elapsed-time changes at visit or mission completion. Explicitly abstract simulations can be convincing without simulating every intervening action.

**Proposal.** Give each opted-in settlement a last-advanced time, process/event state, and defined coarse update policy. Use aggregate labor, travel, and incident outcomes while unloaded, then reconcile those records with detailed NPCs/items/maps on activation. Specify partial intervals, interruptions, resident deaths, resource exhaustion, and capped catch-up. Detailed and abstract execution must consume the same outstanding work/result records so a handoff cannot duplicate output or restart a finished job.

<!-- Source references -->

[camp-mission-events]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/mission_companion.cpp#L2361
[camp-model]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/basecamp.h#L162
[camp-owner]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/basecamp.h#L295
[city-model]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/city.h#L16
[eoc-doc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/EFFECT_ON_CONDITION.md#L2
[faction-model]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/faction.h#L189
[mapgen-doc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/MAPGEN.md#L143
[mission-doc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/MISSIONS_JSON.md#L31
[npc-behavior]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/NPC_BEHAVIOR.md#L1
[npc-catchup]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npc.cpp#L3697
[npc-spawn]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/mapgen.cpp#L2059
[npc-travel]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/do_turn.cpp#L452
[place-vars]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/mapgen.cpp#L3519
[refugee]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/data/json/mapgen/refugee_center/refugee_center.json#L143
[shift-duty]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npc_arbitration.cpp#L164
[shift-load]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npc_class.cpp#L274
[shift-reconcile]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npc.cpp#L2515
[special-records]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/overmap.cpp#L3058
[update-eoc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npctalk.cpp#L5465
