# Expansion sequence, interface contracts, and acceptance scenarios

This is a proposed direction. Names and fields below describe conceptual interfaces, **not new supported JSON syntax**. Implementation should extend existing mapgen, NPC goals/activities, missions, event subscriptions, and save mechanisms.

## Recommended sequence

| Stage | Deliverable | Findings | Demonstration |
| --- | --- | --- | --- |
| 0 | Characterize/fix narrow defects; document actual context and timing | OBS-01–04, TOOL-01 | Boundary hooks and multi-level NPC placement work; protected map regions and extra overrides behave as documented |
| 1 | Generation context, result-bearing updates, scoped identity/reference conventions | GEN-01/02/05/06, EVENT-02 | Two copies of one generated village initialize independently and report failed updates without awarding completion |
| 2 | Sparse settlement/place/resident records | TOWN-01/02/03 | Each village remembers its residents, named facilities, destroyed places, and vacant roles across visits |
| 3 | Agendas, service availability, persistent work orders, reservations | WORK-01/02/03, TOWN-04 | Residents commute, staff a clinic, perform repairs, and hand over guard duty through existing goals/activities |
| 4 | Explicit offscreen policy and journey/job adapters | TOWN-06, WORK-04/05, EVENT-03 | Work, travel, and selected incidents advance while absent without duplicating outputs or NPCs on return |
| 5 | Scoped incident instances, selection pools, consequence projection, history | EVENT-01/04/05/06, TOWN-05 | A raid interrupts a festival, damages supplies, creates recovery work, and leaves consistent consequences in visited and new locations |
| 6 | Data-defined mode sessions, objectives, rules, and run cleanup | MODE-01–04 | An arena/extraction mode resolves, rewards, resets, and resumes correctly through saves and interrupted runs |
| 7 | Optional macro-geography, districts, connectivity constraints | GEN-03/04/07 | A mixed-region world produces bounded, connected settlement layouts with required facilities and explainable fallbacks |

TOOL-01/02 belong in every stage. Stage 7 is not a prerequisite for using an authored village in stages 2–5; mode sessions can also start earlier against existing locations. This is a dependency guide, not a requirement for one enormous rewrite.

## Smaller improvements with immediate value

1. Correct the four source observations and add narrowly targeted regression coverage.
2. Expose explicit mapgen-update parameters and success/failure results through EOCs; retain the existing engine API.
3. Supply location/dimension/rotation/instance context to special generation predicates and committed callbacks.
4. Document and expose scoped location queries/results, including failed searches, rather than requiring avatar-relative assumptions.
5. Add a general callback/occurrence handle with cancellation and inspection, sharing the ownership/reference work identified in the earlier audits.
6. Add one agenda adapter over JSON goals and one custom companion-job adapter, with precise loaded/unloaded semantics.

These are useful independently. A service registry can initially wrap abstract dialogue transactions; a town can initially use only named NPCs; a director can initially manage one incident pool. Scale after those examples reveal the real requirements.

## Core identity and persistence contract

| Concept | Required meaning |
| --- | --- |
| Definition ID | A content template, such as a village archetype, job type, agenda, or incident definition |
| Instance ID | One saved village, building/place, resident, work order, run, or incident occurrence |
| Owner scope | The lifetime and persistence boundary: world, dimension, settlement, run, or character |
| Location | Typed coordinate scale plus dimension; a place reference may resolve to changing geometry |
| Participant reference | Stable identity plus missing/dead/unloaded policy; never a saved raw pointer |
| State/result | Pending/active/paused/completed/failed/canceled/expired as appropriate, with a terminal reason |
| Revision/projection marker | Which content/state version a saved instance or detailed map has already consumed |

A coordinate is useful geometry; it is not a substitute for a resident, service, event, or building identity. A content ID is useful type information; it is not a unique instance key.

Do not assume all state with a “global” name belongs in a shared world file. Establish authoritative storage and migration explicitly. Existing EOC and native timer persistence have different boundaries and should be adapted intentionally.

## Generation request and result contract

A generation predicate should see the candidate dimension, region, origin, rotation, footprint constraints, parameters, pass/reason, and owning instance. It should be read-only and cheap enough for repeated candidate evaluation.

A committed generation/update operation should report:

- Operation and owner IDs; whether this is initial generation, an update, preview, or migration.
- Requested and affected footprint, resolved parameters/variant, and created place/resident references.
- Success, explicit partial success, failure, or deferral, with machine-readable reasons.
- Protected/player-modified cells, occupied targets, missing requirements, and other conflicts where relevant.
- Which state/projection markers were committed, so retrying cannot blindly duplicate initialization.

World mutation from ordinary EOCs should happen after an appropriate commit boundary. If an operation cannot roll back item/NPC spawning or external effects, advertise staged/partial execution rather than an atomic transaction.

## Schedule and job contract

An agenda occurrence identifies a resident, calendar window, intended role/place, priority, travel policy, and exceptions. It proposes work to the behavior system; it does not bypass combat, needs, or player orders. Distinguish “appointment is due,” “worker is traveling,” “worker has arrived,” and “work is complete.”

A work order identifies owner/requester, target, eligibility, requirements, offered capacity, reservations, progress, deadline, and result. The execution adapter can be an existing activity, a JSON goal, a companion abstraction, or an explicitly supported coarse process. All adapters must settle the same order once.

| Transition | Minimum payload |
| --- | --- |
| Offered / claimed | Order, requester, worker(s), target/place, priority, required resources |
| Started / paused / resumed | Progress, reservation status, cause, next meaningful update |
| Completed | Actual work/output delivered, consumed inputs, result, credited participants |
| Failed / canceled / reassigned | Reason, unfinished progress, released/transferred claims, recoverable cargo/output |

Reservation enforcement should reuse existing craft/provider rules where they apply. New capacity or job claims should not weaken those rules or create parallel inventories with conflicting ownership.

## Incident contract and hook ordering

An incident definition chooses eligible scopes, candidates, timing, phases, and consequences. One occurrence stores bound targets/participants, selection seed/history, current phase, due transitions, cancellation/expiry policy, and applied consequence markers.

| Hook family | Purpose | Example context |
| --- | --- | --- |
| Pure eligibility/planning | Choose candidate places and participants without mutation | Scope, candidate IDs, location, resources, recent history, rejection reasons |
| Permission/preflight | Check a supported operation before committing costs or changes | Request, actor, owner, targets, proposed costs, conflicts |
| Committed transition | Notify subscribers of a fact | Instance, old/new state, cause, dimension, participants, result |
| Local projection | Materialize one saved consequence safely | Event/version, place/map, previously applied marker, conflicts, actual changes |
| Resolution/cleanup | Settle rewards, claims, participants, and retained traces | Terminal outcome, credited actors, cleanup policy, completed steps |

Notifications are immutable facts. A preflight hook may permit or reject only the operation that supports it. Document ordering, nested dispatch, and reentrancy; a canceled incident must not become active again because a stale delayed callback arrives.

## Offscreen execution contract

Choose a policy per process rather than treating every delayed effect as equivalent:

| Policy | Suitable example | Required behavior |
| --- | --- | --- |
| Detailed local execution | A visible guard fight or physical delivery | Advance through ordinary actor/map rules while active |
| Aggregate elapsed time | Food production or routine labor | Integrate bounded intervals with labor, storage, and resource limits |
| Abstract outcome | A distant courier or diplomatic mission | Resolve a declared model and record outcome/participants/cargo once |
| Deferred projection | Damage traces from a resolved distant raid | Commit abstract consequences now; materialize compatible local changes later |
| Defer until active | An explicitly player-present encounter | Keep intended scope; do not imply it happened during the absence |
| Expire or skip | A missed merchant visit or appointment | End predictably and release claims without spawning stale content |

Transitions between policies must reconcile one authoritative record. A worker cannot both finish an abstract task and retain the same unfinished detailed task; a delivered shipment cannot also remain in the caravan inventory. Cap catch-up work and expose deferral through inspection.

## Acceptance scenarios for future implementation

These are proposed validation targets. They were not executed as part of this audit.

| Scenario | Acceptance condition | Covers |
| --- | --- | --- |
| Two villages from one template | Independent IDs, residents, place assignments, supplies, and incident cooldowns; generating the second does not rebind the first | GEN-01, TOWN-01/02/03 |
| Candidate and preview safety | Failed candidates and editor previews do not spend resources, register residents, or alter gameplay RNG/state unexpectedly | GEN-01/06 |
| Blocked clinic upgrade | A protected player structure or occupied target causes a reported failure/partial outcome; no false completion reward or mismatched OMT label | GEN-05 |
| Mixed-region crossing | Roads/water transitions remain valid under supported region pairs; incompatible settings produce a diagnostic/fallback | GEN-03 |
| Reachable procedural dungeon | Every required entrance/objective/exit and vertical connector is valid under declared actor rules; impossible layouts retry within a fixed bound | GEN-07 |
| Town loses its doctor | The resident remains dead, service availability updates, replacement work is explicit, and reload does not respawn the doctor | TOWN-02/04 |
| Day and night shifts | Midnight-crossing shifts, travel time, needs, and emergency overrides resolve consistently; two schedule callbacks are not emitted for one transition | WORK-01/04 |
| Two workers, one job | One exclusive claim or explicit cooperation; inputs and output are accounted once even after interruption and reassignment | WORK-02/03 |
| Custom envoy job | A JSON-authored job has real eligibility, duration, outcomes, cancellation, and callbacks beyond descriptive fields | WORK-05 |
| Leave for ten days | Bounded resource/agenda/job/event advancement; return produces no duplicate residents, goods, or completed tasks | TOWN-05/06, EVENT-03, TOOL-02 |
| Caravan enters the bubble | The same travelers and cargo transition between abstract and detailed movement; a destroyed destination yields a usable failure result | WORK-04, EVENT-02 |
| Siege cancels a festival | Cancellation releases service/job claims; queued festival callbacks cannot reactivate it; both outcomes are inspectable | EVENT-01/02/06 |
| Regional incident before discovery | Previously visited and newly generated locations reflect the same recorded event history while preserving later player repairs according to policy | EVENT-04 |
| Save during outcome handling | Run/event outcome, rewards, cleanup, and next-stage creation resume without duplicate rewards or lost completion | MODE-03/04, EVENT-02 |
| Mode entered through alternate start | Required setup occurs or the unsupported entry is rejected clearly; restrictions apply to supported alternate action paths and actors | MODE-01/02 |
| Second character in the same world | Shared settlement/incident authority follows the documented storage policy; character-specific progression remains separate | MODE-04, EVENT-03 |
| Large resident population | Measured update/search work stays within configured bounds, with fair observable deferral and bounded retained history | TOOL-02 |

## Content these additions would enable

- **Procedural living towns:** residents with homes, professions, shifts, staffed shops, local needs, and remembered losses.
- **Reconstruction and survival communities:** damage produces repair jobs; deliveries and labor restore facilities; shortages drive requests and migration.
- **Campaigns with a changing world:** invasions, relief caravans, festivals, faction occupations, and regional crises leave persistent consequences.
- **Reusable arena and expedition modes:** rounds, objectives, rules, death policies, rewards, resets, and progression with defined ownership.
- **More coherent exploration:** districts with required facilities, connected multi-level complexes, and mixed-region geography with explainable generation decisions.

Each can be delivered in bounded form. A credible town with twenty residents and three services is a useful first target; a universal simulation of every city inhabitant is unnecessary to demonstrate the architecture.
