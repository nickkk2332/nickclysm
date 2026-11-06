# NPC schedules, jobs, and travel

Do not start by replacing the behavior tree. Current JSON goals already support conditions, math scores, categories, lifecycle EOCs, errands, activities, and saved commitments. Native arbitration also accounts for needs, orders, guard posts, shifts, and constrained trips. The useful expansion is an agenda and work layer that supplies those systems with durable intentions. [Behavior/goal documentation][npc-goals], [saved commitments][npc-goal-state], [duty arbitration][shift-duty], [trip interface][npc-trip].

## WORK-01 — Work shifts and behavior goals need a reusable agenda layer

**Priority:** P1 · **Effort:** M–L

**Current evidence.** An NPC class can specify one `[start, end)` daily shift, including a midnight-crossing shift. `is_on_shift` also requires a guard post and accounts for player tasks; it is not simply a test of the clock. Schedule reconciliation wakes sleeping NPCs at shift time and adjusts sleepiness for selected off-duty NPCs. JSON goals can implement time-dependent behavior and persist their active state. [Shift input][shift-load], [duty semantics][shift-duty], [reconciliation][shift-reconcile], [custom goals][npc-goals].

**Content limit.** A citizen with breakfast, workshop hours, lunch, an evening market visit, worship, and a home bed needs several appointments and destinations. Weekly duties, seasonal work, special-event overrides, sickness, and emergency evacuation make hand-built conditions increasingly hard to coordinate. “Off shift” also needs a meaningful destination and behavior rather than simply reduced duty urgency.

**Workaround.** Add scored JSON goals gated by time, traits, variables, and circumstances. This can already implement a useful daily routine for a small cast.

**Proposal.** Add reusable agenda definitions assigned to residents/roles, with calendar windows, destination roles, travel allowance, priority, exceptions, and missed-appointment policy. Resolve the current agenda entry into an existing goal or activity; keep needs/combat/order arbitration active. Expose entry/start/late/interrupted/completed/skipped transitions once per agenda occurrence. On load, resolve the current appointment and necessary consequences instead of replaying every per-turn callback since departure.

## WORK-02 — NPC jobs need discoverable, persistent work orders

**Priority:** P0 · **Effort:** L

**Current evidence.** Camp UI configures job priorities; `find_job_to_perform` maps supported activity IDs to native activity actors for sorting, construction, farming, crafting, repair, and other work. EOCs can assign activities or implement a custom goal. This provides useful labor behavior, but an arbitrary new JSON work type does not become a discoverable camp job merely by naming an activity. The job selector still explicitly dispatches supported work categories. [Job UI][camp-jobs], [job selection][npc-jobs], [JSON goals][npc-goals].

**Content limit.** “Repair this wall,” “deliver these medicines to the clinic,” and “staff the gate until relief arrives” need a requester, target, requirements, eligible workers, priority/deadline, progress, and a result. A per-NPC priority list does not represent a shared backlog or distinguish two instances of the same job.

**Workaround.** Assign a goal/activity to a specific NPC and keep job state in variables. Existing zone activities should remain the preferred path for work they already implement.

**Proposal.** Introduce work orders with stable IDs, typed targets, requester/owner, skill/tool/access requirements, priority, claim state, progress, and terminal reasons. Define job providers that advertise work and adapters that fulfill it through existing actors or JSON goals. Completion should be based on a reported result, not just the absence of an activity. Persist enough state for reassignment after worker death, departure, interruption, or unload.

## WORK-03 — Shared work needs broader reservations and cooperation contracts

**Priority:** P1 · **Effort:** M–L

**Current evidence.** Crafting already reserves concrete and abstract providers, including item, furniture, terrain, vehicle-part, intrinsic, and environmental sources. Automation explicitly checks these reservations. That is a substantial existing mechanism, not an absent feature. Its contract is craft-specific; it does not automatically reserve a clinic appointment, a bed, a town work order, a guard slot, or a convoy manifest. [Reservation types and enforcement][reservations], [job dispatch][npc-jobs], [trip destinations][npc-trip].

**Content limit.** Two NPCs can be told to pursue the same bespoke objective unless the author adds coordination. Multi-worker construction needs a rule for shared progress; a delivery needs a claim on cargo and a destination; a shift handover needs the old guard to know whether relief has actually arrived.

**Workaround.** Assign one worker, use existing craft reservations where applicable, and implement locks in EOC variables. Such locks need manual cleanup when an NPC disappears or the task fails.

**Proposal.** Generalize shared identity/lease principles while preserving craft-specific requirement semantics. Add claims for work orders and capacity slots, item/tool bindings through existing reservation rules, and cooperative contribution records where useful. Define expiry, renewal, abandonment, invalidation, and owner-death cleanup. Separate a cheap tentative offer from a committed claim, and revalidate resources at execution. Do not use blanket locks that prevent ordinary player interaction without a stated policy.

## WORK-04 — Scheduled travel needs semantic destinations and consistent arrival

**Priority:** P1 · **Effort:** M–L

**Current evidence.** NPCs have local errands/trips, pathing, guard posts, and overmap destinations. The overmap travel loop moves selected traveling NPCs near the player, while local trip machinery resolves standing positions and constrained paths. These are useful navigation paths, but neither is a general schedule journey record connecting a resident's home, workplace, service appointment, or another settlement across levels of detail. [Overmap movement][npc-travel], [local trip contract][npc-trip], [duty constraints][shift-duty].

**Content limit.** Workers should commute to the same place, caravans should retain cargo/escorts, and emergency schedules should notice a blocked bridge or destroyed clinic. Simply teleporting an NPC to the expected endpoint hides route failures; forcing all journeys through tile pathing can make distant towns expensive or dependent on loaded maps.

**Workaround.** Keep routine movement local, use known positions, and implement distant travel as a companion mission or explicit abstraction.

**Proposal.** Add a journey instance with origin/destination place IDs, participant set, travel mode, route/ETA, carried work/cargo, and arrival/failure callbacks. Use abstract movement outside detailed simulation and local navigation near endpoints. Reconcile once during handoff, preserve delays/interruptions, and revalidate destination access. Return distinct reasons for missing destination, unreachable route, denied access, and temporary congestion so schedules can select sensible fallbacks.

## WORK-05 — Companion mission data needs an executable custom-job lifecycle

**Priority:** P1 · **Effort:** M–L

**Current evidence.** `faction_mission` JSON loads names, descriptions, skill/risk/difficulty, activity level, and presentation fields such as translated time, positions, possible items, and effects. The class does not define arbitrary mission execution callbacks, outcome distributions, or a new dispatch implementation. Existing mission/camp code supplies the behavior. Companion started/finished events already exist, with finish outcomes including return, cancellation in camp paths, and death; this hook improvement must be retained. [Loaded fields][camp-mission-load], [record structure][camp-mission-model], [documentation][camp-mission-doc], [finish dispatch][camp-mission-events].

**Content limit.** Adding a JSON record that says “Diplomatic envoy, two days, low risk” does not create a real envoy job with travel, costs, negotiations, outcomes, and a return. Reacting to an existing job's finish event is different from authoring a new executable job type. The `effects` strings are descriptive text, not EOC effects.

**Workaround.** Reuse supported native missions and react to their lifecycle events, or build a separate EOC/goal state machine for a custom assignment.

**Proposal.** Add opt-in executable companion-job definitions with eligibility, participants/slots, requirements and commitment, duration/progress, travel policy, outcome resolution, and start/interrupt/cancel/finish callbacks. Store a mission instance independent of the descriptive definition. Route results through existing companion events and shared work-order accounting, including death and recovery policy. Keep risk/outcome resolution bounded and explicit; authors should be able to model a safe courier without inheriting combat-mission assumptions.

<!-- Source references -->

[camp-jobs]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/faction_camp.cpp#L2157
[camp-mission-doc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/FACTION_MISSIONS.md#L7
[camp-mission-events]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/mission_companion.cpp#L2361
[camp-mission-load]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/faction_mission.cpp#L41
[camp-mission-model]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/faction_camp.h#L100
[npc-goal-state]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npc_goal_state.h#L27
[npc-goals]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/NPC_BEHAVIOR.md#L408
[npc-jobs]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npcmove.cpp#L4185
[npc-travel]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/do_turn.cpp#L452
[npc-trip]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npc_trip.h#L55
[reservations]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/craft_reservation.h#L25
[shift-duty]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npc_arbitration.cpp#L164
[shift-load]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npc_class.cpp#L274
[shift-reconcile]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npc.cpp#L2515
