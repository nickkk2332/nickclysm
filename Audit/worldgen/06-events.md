# Random events, incidents, and world history

Three meanings of “event” must stay distinct: a typed notification on the event bus, a delayed operation in a timer queue, and a gameplay incident such as a raid or festival. Nickclysm has strong building blocks for the first two and substantial incident content built from them. The proposed expansion makes gameplay incidents reusable and independently owned.

## EVENT-01 — Random events need shared eligibility, budgets, and selection history

**Priority:** P1 · **Effort:** M–L

**Current evidence.** Map extras use weighted collections and generation-time chances, with Z/region-flag eligibility. Recurring EOCs support variable/random intervals, conditions, false effects, and deactivation. Portal storms already demonstrate complex recurring world content. What is absent from these inspected schemas is a common selection record for competing incident candidates, per-place cooldowns, intensity budgets, exclusion groups, and repeat history. [Extra collections][extras-settings], [extra eligibility][extra-check], [EOC fields][eoc-model], [portal-storm implementation][portal].

**Content limit.** A mod can roll a raid each week. Several mods independently doing so can produce repetitive or simultaneous raids, or starve quieter events. “This town recently lost its doctor, so prefer a medical-aid event over another merchant visit” needs history and eligibility beyond independent recurring rolls.

**Workaround.** Use one coordinating EOC with weighted choices and namespaced variables for cooldowns and recent outcomes. This can be effective within one conversion.

**Proposal.** Add optional event pools/directors scoped to a settlement, region, dimension, run, or player experience. Define eligibility, weight, cooldown, exclusion group, repeat policy, concurrency/intensity cost, and bounded target selection. Record selection reasons and completed outcomes. Keep simulation events separate from player-paced encounters when their goals differ; an offscreen town economy should not require the player to be bored before a harvest occurs.

## EVENT-02 — Delayed effects need owned, cancellable event instances

**Priority:** P0 · **Effort:** L

**Current evidence.** A queued EOC stores its definition ID, time, and context values. Processing reconstructs dialogue around the current queue owner; it does not preserve an arbitrary original pair of participants as durable references. Native timed events separately retain type, location, key, dimension, and selected payloads. Keys can retime native events, and the EOC math API can inspect the next scheduled run of an EOC. Those are useful controls, but neither record is a general incident instance with participants, ownership, cancellation, and terminal state. [Queued EOC][eoc-queue], [delayed dispatch][eoc-delay], [processing][eoc-process], [native timer][timed-model], [EOC reference][eoc-doc].

**Content limit.** A festival delayed by a siege, a caravan whose sponsor dies, or an arena round canceled on abandonment needs to find and end exactly its own pending work. A definition ID can have several queued occurrences; canceling by that name is not enough. A coordinate in context also needs a dimension and invalidation policy.

**Workaround.** Add occurrence tokens and cancellation guards to context/global variables. Every callback must check them before acting, and cleanup remains the author's responsibility.

**Proposal.** Add event/job instance handles with owner scope, status, typed participant/location references, due time, and cancellation/reschedule policy. Resolve references safely at execution and provide missing-target callbacks. Distinguish definition, recurring schedule, and one occurrence. Reuse these records for runs, services, and work orders where appropriate rather than adding another unrelated timer implementation.

## EVENT-03 — Event scope and offscreen timing need explicit policies

**Priority:** P0 · **Effort:** L

**Current evidence.** Global EOCs are processed on the avatar's turn. Their queue is serialized alongside the player and other game state. Local queues belong to characters. Location-based native timed events retain a dimension, and selected types are deferred until that dimension becomes current. Recurring EOCs reschedule from the current turn after processing; they do not generically replay all missed intervals. NPC load catch-up is a separate selected-state approximation. [Global processing][eoc-process], [queue save][queue-save], [due-work processing][eoc-due], [native dimension gate][timed-process], [native persistence][timed-save], [NPC catch-up][npc-catchup].

**Content limit.** “Global” cannot safely be assumed to mean “every settlement advances in every dimension.” A fair that should have ended yesterday, an invasion delayed until the player returns, and a crop process that should accumulate six days of yield need different semantics. Multiple characters sharing world content also require an explicit decision about where authoritative world events live.

**Workaround.** Record timestamps and check current dimension/location in EOCs. Sky Island's portal-storm override demonstrates deliberate dimension-sensitive event handling. [Existing content][sky-portal].

**Proposal.** Separate ownership scope from execution policy. An event can belong to a world, dimension, settlement, run, or character, and independently choose exact local execution, bounded catch-up, aggregate elapsed-time resolution, deferred materialization, or expiry. State whether deadlines use world calendar time or active participation time. Define persistence/ownership across avatar replacement and multi-character world use before advertising world-global guarantees.

## EVENT-04 — Global consequences need durable state and local projection

**Priority:** P1 · **Effort:** L

**Current evidence.** EOCs can change weather, variables, OMT labels, detailed maps, and creature state through existing operations. Native timers can defer updates; map extras record selected generated scenes. These tools can create visible local effects, but there is no general authoritative incident footprint/version from which both existing and not-yet-generated maps derive their state. [EOC operations][eoc-doc], [map updates][update-eoc], [timed update][timed-update], [extra application/recording][extra-apply].

**Content limit.** A regional war, flood, plague, blackout, or faction occupation should affect places already visited and places generated later. Applying damage only at first generation makes discovery timing decide the history. Reapplying updates on every visit can duplicate victims, loot, or destruction. A map change also should not silently replace the player's later repairs.

**Workaround.** Store a global incident flag and carefully gate per-location updates, keeping separate paths for generation and revisits. This is feasible for a single well-defined event.

**Proposal.** Record event footprints, stage/time, affected settlement/entity IDs, and semantic consequences before materializing local scenes. Track which consequence version each place has applied. Generate new maps from that state; update existing ones through conflict-aware operations. Support explicit policies for historical traces, active hazards, and completed incidents. Abstract loss of supplies/residents must reconcile with physical stock and NPC identities rather than spawning another independent version of the outcome.

For invasion content, expose the existing overmap monster-group/horde operations through validated bindings before inventing a second army representation. Their current native query/spawn APIs are not directly exposed by the inspected EOC/math registries. A reusable event may eventually need group identity, destination, and outcome hooks beyond those initial adapters. [Native queries][horde-query], [native spawning][group-spawn].

## EVENT-05 — Semantic world hooks and custom notifications need expansion

**Priority:** P1 · **Effort:** M–L

**Current evidence.** The engine already emits many useful typed notifications, including avatar OMT entry, dimension travel, activity completion/cancellation, NPC goals/attitudes, camp upgrades/takeovers, and companion mission start/finish. `trigger_event` can emit allowed existing event types; it does not register arbitrary new event schemas. EVENT EOCs subscribe to the compiled enum. Dispatch can accept explicit talkers, or infer a character from recognized payload fields; it skips an unresolved named character rather than silently substituting the avatar. [Event enum/payloads][event-types], [OMT payload][event-omt], [subscription dispatch][event-dispatch], [dynamic emission][event-trigger], [companion dispatch][camp-mission-events].

**Content limit.** There is no common settlement-created/resident-assigned/place-invalidated/service-opened/job-claimed lifecycle because those shared models do not yet exist. OMT movement hooks are avatar-oriented and do not establish town membership transitions for NPCs. Authors can directly call another EOC with context, but loosely coupled mods cannot subscribe to a validated custom gameplay topic through the existing event enum.

**Workaround.** Call named EOCs directly and document context conventions. Use existing native events when they accurately represent the action; avoid fabricating an unrelated engine event merely to carry data.

**Proposal.** Add semantic hooks alongside each new model, with stable instance IDs, dimension/location, cause, prior/new state, and explicit participant roles. Offer namespaced custom notification schemas or an equivalent validated subscription mechanism for mod interoperability. Treat notifications as immutable facts after commit. Mutable permission/transaction hooks should be separate operations with documented ordering and recursion rules. Never claim a notification cancels an action that has already happened.

## EVENT-06 — Events need reusable phases, participants, and player-facing follow-through

**Priority:** P1 · **Effort:** M–L

**Current evidence.** Missions already offer conditions, deadlines, dialogue, followups, and start/end/fail effects. EOCs can choose locations, spawn participants, display messages, and alter maps. Portal-storm content shows multi-part behavior built from those primitives. The inspected incident mechanisms do not provide one common phase/participant record joining warning, travel, arrival, active encounter, resolution, and aftermath. [Missions][mission-doc], [mission loader][mission-load], [existing staged phenomenon][portal], [EOC tools][eoc-doc].

**Content limit.** A siege should warn the right town, send the same attacking group, offer a defense or evacuation objective, resolve once, and leave an appropriate aftermath. A merchant visit should persist long enough to be useful and leave rather than become another permanent spawn. Flavor messages alone do not make the underlying event recoverable or explain its present state.

**Workaround.** Author a bespoke mission/EOC chain and guard each phase transition with variables. This remains appropriate for unique story incidents.

**Proposal.** Provide optional incident phases with entry conditions, participant/location bindings, timeout, transition/result hooks, and cleanup/retention rules. Link missions and notices to the same event instance. Distinguish authoritative event state from how the player learns it: radio, rumors, a notice board, visible evidence, or direct contact. Preserve a bounded history sufficient for follow-up events and inspection. Reactions should reflect actual outcomes, not merely that an encounter was spawned nearby.

<!-- Source references -->

[camp-mission-events]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/mission_companion.cpp#L2361
[eoc-delay]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npctalk.cpp#L7370
[eoc-doc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/EFFECT_ON_CONDITION.md#L2
[eoc-due]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/effect_on_condition.cpp#L289
[eoc-model]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/effect_on_condition.h#L49
[eoc-process]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/effect_on_condition.cpp#L393
[eoc-queue]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/character.h#L321
[event-dispatch]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/effect_on_condition.cpp#L698
[event-omt]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/event.h#L246
[event-trigger]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npctalk.cpp#L10214
[event-types]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/event.h#L24
[extra-apply]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/map_extras.cpp#L1145
[extra-check]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/map_extras.cpp#L1300
[extras-settings]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/REGION_SETTINGS.md#L498
[group-spawn]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/overmapbuffer.h#L586
[horde-query]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/overmapbuffer.h#L274
[mission-doc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/MISSIONS_JSON.md#L31
[mission-load]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/missiondef.cpp#L127
[npc-catchup]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npc.cpp#L3697
[portal]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/data/json/effects_on_condition/nether_eocs/portal_storm_effect_on_condition.json#L19
[queue-save]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/savegame.cpp#L153
[sky-portal]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/data/mods/Sky_Island/effectoncondition/portal_storms.json#L4
[timed-model]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/timed_event.h#L42
[timed-process]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/timed_event.cpp#L422
[timed-save]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/savegame.cpp#L1994
[timed-update]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/timed_event.cpp#L333
[update-eoc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npctalk.cpp#L5465
