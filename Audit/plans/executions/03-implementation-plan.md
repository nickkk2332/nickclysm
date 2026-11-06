# Implementation sequence and validation plan

**Delivery status:** the living-participant milestone covers portions of phases 0–3 and 7; the [narration milestone](05-narration-milestone.md) delivers phase 4's append, hearing, history, and presentation controls. [First-milestone status](04-implementation-status.md) records the runtime foundation. The [automatic melee milestone](06-automatic-triggers.md) delivers phase 5's initial living-melee slice; [physiological knockouts](07-knockouts.md) deliver the incapacity portion of phase 3. The [fatal melee milestone](08-fatal-hits.md) delivers phase 6 for NPC/monster victims, and [avatar deferral](09-avatar-fatal-deferral.md) adds the player lifecycle at supported primary-melee boundaries. Further combat producers, richer physical actions and the dedicated inspector remain further work.

## Proposed code boundaries

Use ordinary engine C++ for scheduling, identity, control, and result-bearing physical operations. Reuse the existing dialogue/EOC expression and effect machinery for authored conditions and effects. Do not add a second general scripting language.

| Responsibility | Proposed files/integration |
| --- | --- |
| Definition/trigger loading, validation, indexes | New `execution.h/.cpp`; IDs in `type_id.h`; load/reset/finalize/check registration in `init.cpp` |
| Instances, graph progress, RNG, owned control, safe start/cancel queues | New `execution_runtime.h/.cpp`; manager owned by `game` |
| Native action and predicate adapters | New `execution_actions.h/.cpp`, using existing creature/map/damage/movement APIs |
| Durable actor references | A shared `creature_ref` module or an existing equivalent; monster UID fields, allocation, lookup, save/load |
| Attack snapshot and nesting/causal scope | A small shared combat-context module, adapted at actual attack/damage producers |
| EOC start/cancel/query bindings | `npctalk.cpp`, `condition.cpp`, math bindings, and execution context adapters as needed |
| Turn and input/AI integration | `do_turn.cpp`, avatar activity/input handling, `npcmove.cpp`, `monmove.cpp`, activity actor registration |
| Physical actions and attribution | `game.cpp`, `creature.cpp`, `character.cpp`, `monster.cpp`, `map.cpp`, effect-source handling where required |
| Fatal lifecycle | `Creature::check_dead_state`, monster/NPC finalizers, cleanup/tracker filtering/removal, attack on-kill handling, prevention/rescue paths |
| Persistence | `savegame.cpp`, `savegame_json.cpp`, master/dimension UID counters, staged save ordering |
| Narration | `messages.h/.cpp`, snippet/translation expansion, UI rendering/inspection; extraction parser registration |
| Fixtures/docs | `data/mods/TEST_DATA/executions/`, focused tests, eventual `doc/JSON/EXECUTIONS.md` |

These are responsibility boundaries, not a requirement for one file per concept if a smaller split is clearer. The current source and tests use globbed source lists; verify each supported build and reconfigure CMake as needed when files are added. Explicit data/parser/activity registrations still matter.

## Phase 0 — Characterize integration points and lock the contracts

**Deliverable:** behavior tests and a concrete adapter matrix before altering combat/death ordering.

Trace a character→monster melee hit, NPC→NPC hit, monster→NPC hit, projectile hit, damage-type EOC, technique post-hit EOC, native death cleanup, and source-less environmental damage. Record which context is available and when callbacks can mutate health, position, weapon, or participants.

Current evidence that must guide the implementation:

- Character melee attack notifications fire before resolved technique/damage. [Emitter][melee-before].
- Technique post-hit EOCs run after a dead-state check; kill-style effects also run there. [Current ordering][melee-after].
- Damage EOCs execute within damage calculation before its final `apply_damage` path completes. Their “damage taken” input is not a universal final HP-loss report. [Damage path][damage-core], [damage EOCs][damage-eocs].
- Cleanup and tracker removal independently finalize/remove fatal actors. [Cleanup][cleanup], [tracker][tracker-cleanup].
- Martial arts already provide useful context; do not discard this work in favor of a new pre-hit event with fewer facts. [Existing context][ma-context], [dispatch][ma-dispatch].

Specify start/drain ordering, immutable context fields, status/reason values, tick boundary, ownership, and fatal admission policy in tests. This prevents later schemas from depending on a callback that occurs too early or too late.

**Exit criterion:** ordinary combat has unchanged outcomes and event order with no execution definitions/rules loaded. Adapter tests explain supported and missing fields rather than manufacturing context.

## Phase 1 — Definitions and a bounded graph runner

**Deliverable:** loader/validator plus a headless runner that can execute named steps, delays, conditions, chance/weighted transitions, bounded repeats, locals, and lifecycle callbacks.

Register definition and trigger factories using the current [loader lifecycle][loader]. Compile validated JSON into tagged operations/transitions rather than retaining arbitrary JSON objects for per-tick interpretation. Resolve referenced IDs after all relevant content is loaded.

Implement immediate step/action execution, yielding action cursors, transition selection, per-instance RNG, start/cancel queues, terminal-state guards, and resource limits. Allow tests to drive explicit calendar turns and inject deterministic rolls/results. Pure debug inspection cannot consume RNG or run effects.

Start with test handles/actions rather than claiming production creature persistence. Add `start_execution` as an explicit API with reasoned acceptance/refusal, followed by the EOC wrapper. Top-level lifecycle EOCs and graph transitions must not advance the instance twice when callbacks recursively start/cancel other scenes.

**Tests:** seeded timing and branch choices; zero-delay chains; repeats at T+1/T+2; timeout; invalid graph; callback cancellation; reentrancy; no `on_start` replay; no same-tick infinite loop. A pure scheduler test is necessary here because tick/reentrancy errors are the behavior being implemented, not incidental implementation details.

**Exit criterion:** a fixed graph produces the same trace for the same initial state and RNG, within declared budgets.

## Phase 2 — Durable references and saved instances

**Deliverable:** production identity/persistence sufficient for avatar, NPC, and monster scenes.

Use `character_id` for characters and add durable monster UIDs. The current [temporary monster ID][tracker-id] cannot survive tracker reordering. Track UID allocation in authoritative world counters, reserve loaded IDs, assign legacy monsters once, detect duplicates, and define copy/spawn/transformation semantics. Dimension travel must not leave an allocator counter behind saved monsters; existing item UID persistence illustrates why both master and dimension paths matter. [Master save][save-master], [monster records][monster-save].

Save manager state with the active game/session, not one independent scene file per creature. Bind weapon items by their existing stable identity facilities. Preserve each chosen deadline, random draw, active action cursor, repeat occurrence, and transcript choice. Rebuild indexes/control from authoritative instance data during load.

Load order must make actor identities and scene state available before normal actor turns/death cleanup. Restore definition/step compatibility checks; invalid data settles safely with a diagnostic. Respect [staged save ordering][game-save] and avoid saves from inside partially committed action callbacks.

**Tests:** resume between beats and mid-movement; tracker insertion/removal; actor polymorph; dropped/destroyed weapon; two same-type monsters; legacy saves; missing definitions/revisions; avatar identity/body change; duplicate UID; dimension save/travel and allocator monotonicity.

**Exit criterion:** a saved live-actor scene resumes with the same participants and pending decisions. It never reacquires “another zombie at the same tile.” Fatal persistence remains gated until Phase 6.

## Phase 3 — Control, physical actions, and nonfatal scenes

**Deliverable:** living actors can be held/scripted together while the rest of the world runs.

Add atomic role-control acquisition and cleanup. Enforce claims in NPC/monster/input paths without spinning native action loops or granting banked moves. The avatar adapter delegates to the manager; it does not independently tick the graph. Respect existing orders, needs, activities, mounts, vehicles, and grapples through explicit supported policies.

Implement immediate EOC actions, effects, result-bearing attributed damage, checked relative movement, edge probes, and incapacity adapters. Movement reuses native collision/gravity behavior with one checked tile per beat where requested. Add motion-result state and source attribution for secondary falls/collisions. Review native [falling][map-fall], [character gravity][character-gravity], [monster gravity][monster-gravity], [knockback][knockback], and [effect-source limits][effect-source].

Implement actual unconsciousness semantics for supported NPCs/monsters: action suppression, immunity, duration, damage wakeup/recovery, and no death rewards. Do not merely rename `stunned` unless that effect truly provides the documented state. Unsupported creature physiology returns a branchable result.

Cancellation/unload/dimension transitions release claims before detachment; committed map/damage/effect changes remain. Mutation from an EOC revalidates the next action. A sequence should not restore an old activity over a newer order issued during interruption.

**Tests:** all nonfatal actor-kind pairs that the adapter advertises; movement against walls/diagonal corners/occupied tiles; unsupported terrain; vehicle floors; flying/gliding/levitating targets; moving attacker with a fixed captured direction; interrupted walks; high-speed actors; multiple competing scenes; third-party damage; knockout immunity/wakeup; role removal; ordinary fire/other combat continuing.

**Exit criterion:** the nonfatal knife movement and knockout can run manually, with correct positions, action costs, and branchable failures. No invulnerability or global world freeze is introduced.

## Phase 4 — Message pools, append, observation, and presentation

**Deliverable:** translated, perceived beats with owned appendable messages.

Add stable execution-local message entries/groups and ordered timestamped segments to the [message layer][messages-api]. Preserve ordinary message coalescing/history behavior for unrelated messages. A compact scene view may combine segments, while chronological history preserves their actual order around other combat.

Reuse [snippet selection][snippets] and translation objects; add named role substitutions with observer-aware identity/pronoun formatting. Persist chosen lines/parameters, and add the new JSON parser to [translation extraction][parser] following existing [effect extraction][translation-parser]. An append with no observed/existing anchor creates a fresh visible entry; it never reveals hidden earlier narration.

Offer concise presentation/extra pacing controls separately from simulation cancellation. Never use a blocking popup or sleep loop as the scheduler. UI settings that hide narration must preserve mechanical outcomes and ticks.

**Tests:** deterministic pools/no-repeat edge cases; two simultaneous executions; unrelated intervening messages; append after reload/log truncation; unseen actors; sound-only witnesses; observation of a fall before a target disappears; blind/deaf players; text extraction; renderer behavior in supported terminal and tiles builds.

**Exit criterion:** the two held beats display the intended randomized lines, with append ownership and chronological history intact.

## Phase 5 — Indexed automatic triggers and combat adapters

**Deliverable:** weapon/style/actor/damage/location rules can select living-actor sequences from actual combat.

Add the shared combat snapshot/causal scope and supported producers. Reuse [martial-arts fields][ma-context] and [damage results][damage-result]; add missing stable weakpoint IDs, style snapshot, original item identity, per-type result/HP-loss distinctions, and channel/monster attack ID where known. Do not match translated weakpoint display names.

Initial required coverage is character melee, including NPCs, and standard monster melee against NPCs/characters. Explicit EOC starts support selected techniques/special attacks. Add projectile/common damage adapters afterward with a published matrix of their context fields. A source-less damage event may support a one-actor scene without acquiring a fictional attacker; two-actor rules reject it.

Implement indexed matching, once-per-contact/stage deduplication, priority/chance/weight selection, cooldown scopes, final eligibility revalidation, participant acquisition, and recursion suppression. Keep starts queued until the current attack can finish safely. Distinguish acceptance from completion in EOC result callbacks.

**Tests:** exact weapon and category/skill matches; style/technique; body part and stable weakpoint; damage type thresholds versus final HP loss; monster→NPC and NPC→monster filters; unsupported missing context; once-per-contact odds; cooldown persistence; simultaneous clash candidates; child damage not retriggering itself; no-match baseline performance.

**Exit criterion:** nonfatal examples trigger from their intended interactions and not from unrelated hits or duplicated per-type callbacks. Fatal rules still refuse unsupported deferral until Phase 6.

## Phase 6 — Fatal reservation and one normal death finalization

**Deliverable:** NPC/monster fatal-hit scenes can briefly move/narrate a defeated actor before normal death side effects, without resurrecting it or suppressing the wider world.

This phase is broad engine integration, not a presentation-only addition. Establish the pending-fatal state and predicates described in the runtime design. Reserve only at supported attack boundaries, before nested callbacks/finalizers can commit death. Keep fatal health truthful while retaining occupancy and a stable execution-specific lookup. Ordinary AI remains excluded.

Implement fatal admission at the supported damage-commit boundary, before fatal-state callbacks/checks can finalize the injury. Select and acquire the candidate there, but defer graph activation until the outer attack ends safely. Restrict that trigger stage to fields available at commit. Do not delay every death provisionally and select a scene afterward: failed rolls/no-match rules must retain the ordinary finalization path. An earlier arbitrary EOC/direct death outside the supported adapter is a normal death, not an opportunity to resurrect the target.

Audit/update all relevant routes, not just `Creature::check_dead_state`:

- Direct monster/NPC `die` calls, cleanup, dead-filtered tracker lookup/removal, and collision/occupancy.
- Supported motion/falling with a defeated actor and source attribution.
- Normal/third-party damage, healing/rescue, transformation, despawn, unload, dimension travel, and teardown.
- Martial-arts on-kill effects, `killed` versus `death_pending` context, death messages/functions, corpse/drop placement, XP, statistics, and faction consequences.
- Existing NPC death-prevention EOCs and source-less finalization; preserve normal prevention semantics rather than bypassing them. [NPC finalizer][npc-death], [monster finalizer][monster-death].
- Saved token/owner consistency and load-before-cleanup ordering, including removed or incompatible definitions.

Release the reservation before invoking native finalization and guard that release/finalizer against reentry. Cancellation/timeout/unload settles the injury immediately; it does not rescue a still-fatally-injured actor. Real healing that clears fatal state can rescue the actor. A third-party harmful event forces prompt settlement instead of granting several seconds of protection.

Pre-existing death/prevention/reward callbacks must fire under one documented ordering, once for the actual outcome. A selected fatal scene intentionally postpones final kill reporting; ordinary attacks with no selected scene retain their old order. Do not fake early kill events to preserve a cosmetic log line.

Avatar fatal deferral is outside the first fatal release. The [avatar follow-up](09-avatar-fatal-deferral.md) adds its separate opt-in, native prevention/finalization guard, game-over handoff and body-control tests.

**Tests:** fatal injury preserved; no HP-to-1 workaround; exactly one corpse/drop/XP/death function; kill-style callback delayed correctly; prevention/rescue; source dies or changes avatar; fatal target blocks occupancy; third-party damage/fire; owned ledge fall; direct finalizer reentry; canceled scene; timeout; save during pending defeat; missing definition/owner; unload/dimension clear; unsupported channel immediately falls back to ordinary death.

**Exit criterion:** no actor can remain fatally held without a valid bounded owner, no finalized creature is revived for narration, and ordinary deaths still behave normally.

## Phase 7 — Reference content, tooling, and release checks

**Deliverable:** documented supported feature, validated content examples, and usable diagnostics.

Add TEST_DATA fixtures for the full knife scene, a critical knockout, a strength-based takedown, and a two-actor clash. Include a monster→NPC and an NPC→monster binding. Keep production automatic rates opt-in; the engine feature should not silently alter every modpack's combat frequency.

Add an execution inspector showing candidate rules/rejections, selected rolls, roles/UIDs, control claims, current step/action, due tick, local results, visibility, pending fatal state, and cancellation reason. Debug advance/preview must operate on a detached sandbox or an explicitly marked live instance; inspection alone is read-only.

Publish `doc/JSON/EXECUTIONS.md` with implemented schema, actor/channel coverage, timing, failure reasons, compatibility policy, and examples. Remove or mark any draft-only fields that did not ship. Link relevant EOC/martial-arts documentation. Do not mark this planning document “implemented” merely because the loader exists.

Run focused meaningful tests for the new behavior plus the affected combat, martial-arts, death, NPC, movement, EOC, and save suites. Existing useful regression neighbors include `audit_martial_art_events_test.cpp`, `martial_art_test.cpp`, `melee_test.cpp`, `npc_json_goal_test.cpp`, and errand tests. Validate JSON formatting/loading and translation extraction; verify terminal/tiles builds that touch the message/actor adapters. Broaden only for changed or failing paths.

## End-to-end acceptance scenarios

| Scenario | Required result |
| --- | --- |
| Knife scene with fixed RNG | 1–4 checked backward steps, one saved ledge roll, two held message beats on the alternate branch, correct final state |
| Obstacle behind victim | No teleport or false fall message; partial movement/block result selects a valid alternative |
| Real ledge versus vehicle floor/flight | Only the valid unsupported destination takes the fall branch; gravity/immunity rules remain real |
| Short fall survived | No authored claim or kill reward solely because the branch was named “tumble” |
| Fatal scene saves mid-stagger | Same victim, direction, selected distance, message history, and pending injury after load; one final death |
| Nonfatal critical knockout | Real incapacity, no corpse/kill XP, correct wakeup/immunity behavior |
| Strength takedown against another character | Eligibility reads the two actual supported stats; monster stats are not fabricated |
| Rare clash with two candidates | One scene owns the pair; unrelated actors keep acting and can interrupt it |
| Specific monster→NPC binding | Only that attacker/target interaction qualifies; roles and damage source are correct |
| Specific damage/body-part/terrain binding | Uses the original resolved attack facts and the declared live/snapshot location policy |
| Source or weapon removed | Safe branch/interruption with no pointer reuse, replacement participant, or false reward |
| Append during surrounding combat | Correct owned entry/group; other messages and timestamps remain coherent |
| Narration hidden/accelerated | Identical mechanical outcome and game ticks; no offscreen information disclosure |
| Presentation canceled with fatal injury | Control ends, injury settles through ordinary death/prevention; no escape by canceling text |
| No matching executions | Same combat results and callback order as baseline; matching work is bounded/indexed |

## Main implementation risks and containment

| Risk | Containment |
| --- | --- |
| Fatal actors removed by an unadapted path | Dedicated fatal phase, explicit state/predicate review, unsupported-channel fallback, watchdog settlement |
| Monster reference points to a replacement | Durable UID plus dimension/lifecycle checks; no coordinate/type reacquisition |
| A callback changes participants mid-step | Action checkpoints, queued transitions/cancellation, revalidation before the next action |
| Nested attacks double-trigger or double-award | Root/contact IDs, causal scope, indexed stage deduplication, native finalization once |
| Movement narrates success but fails physically | Checked commit/result-coupled narration and actual collision/support adapters |
| Cutscenes create free actions or invulnerability | Explicit control/cost model, world continues, third-party interruption, no HP patching |
| Content changes break saved graphs | Revision/action-shape validation and safe control/fatal cleanup |
| Scope grows into a general film editor | Deliver the small graph/action system first; camera choreography, synchronized VFX tracks, arbitrary offscreen scenes, and avatar fatal deferral remain separate extensions |

The architectural difficulty is concentrated in durable creature identity and fatal lifecycle handling. Branching text and delays are smaller work. Deliver those foundations explicitly so an attractive demo does not hide incorrect death accounting or unsafe saved participants.

<!-- Source references -->

[character-gravity]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/character.cpp#L7279
[cleanup]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/game.cpp#L4133
[damage-core]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/creature.cpp#L1591
[damage-eocs]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/damage.cpp#L437
[damage-result]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/damage.h#L216
[effect-source]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/effect_source.h#L19
[game-save]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/game_io.cpp#L892
[knockback]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/game.cpp#L4175
[loader]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/init.cpp#L404
[ma-context]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/martialarts.h#L114
[ma-dispatch]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/martialarts.cpp#L1616
[map-fall]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/map.cpp#L11753
[melee-after]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/melee.cpp#L1066
[melee-before]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/melee.cpp#L839
[messages-api]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/messages.h#L25
[monster-death]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/monster.cpp#L3194
[monster-gravity]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/monster.cpp#L412
[monster-save]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/savegame_json.cpp#L2602
[npc-death]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npc.cpp#L3398
[parser]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/lang/string_extractor/parser.py#L140
[save-master]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/savegame.cpp#L1914
[snippets]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/text_snippets.h#L21
[tracker-cleanup]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/creature_tracker.cpp#L263
[tracker-id]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/creature_tracker.cpp#L49
[translation-parser]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/lang/string_extractor/parsers/effect.py#L4
