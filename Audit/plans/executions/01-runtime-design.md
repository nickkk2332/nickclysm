# Execution runtime and integration contract

All interfaces in this document are proposed. The existing engine evidence is linked separately.

## 1. Definition, trigger, and instance are separate

An `execution` definition is an immutable graph of named steps, lifecycle EOCs, role requirements, and policies. A separate `execution_trigger` chooses when a definition is offered. One saved `execution_instance` owns a particular performance of that graph.

This separation lets one knife sequence be selected by a weapon, a technique, a monster attack, or an explicit quest EOC without copying the sequence. Different trigger rules can pass parameters, select message pools, or vary weights. None of those rules silently makes the outcome fatal.

Proposed instance data:

| Data | Meaning |
| --- | --- |
| Instance/definition/revision | Unique occurrence, source content, and compatibility version |
| Bound roles | Stable creature references for `actor`/`target`; optional item reference for `weapon` |
| Origin/context | Dimension, typed absolute locations, original attack vector, immutable combat snapshot |
| Program state | Current step, step-entry serial, action cursor/state, repeat counters, chosen next step/deadline |
| Local values | Typed variables private to this instance, separate from actor and global variables |
| Random state | Instance RNG stream and already sampled choices, including resolved snippet selections |
| Control claims | Which actor actions this instance currently owns, with lease/generation identity |
| Lifecycle | Pending/running/waiting/ending/completed/interrupted/failed; result/reason |
| Consequences | Last action results, attributed secondary effects, pending fatal-resolution token if any |
| Presentation | Owned transcript entry IDs, selected text/parameters, perceived beats, current append groups |

Callbacks may request a transition or cancellation, but they must not erase a running instance from inside its own stack frame. Record the request and settle it at the next safe checkpoint.

## 2. Timing, step execution, and deterministic randomness

One execution tick equals one calendar turn, currently one second. A 150-speed actor does not advance a scene faster than a 75-speed actor. Move points still govern ordinary actor actions and explicitly charged costs. The [main loop][turn-loop] advances calendar turns separately from the multiple actions an [NPC may take][npc-turn].

The manager pumps once per calendar turn at a documented point before normal actor decisions. It also drains accepted start requests at safe outer action boundaries, so a newly triggered scene need not wait a whole second before its opening beat. A start drain runs entry work only; it does not invent another `on_tick` invocation for the same turn.

Each instance records `last_tick_processed`. For a scene started during turn T, its first periodic tick is T+1. On each active tick:

1. Resolve required participants, dimension, owned control, and interruption requests.
2. Run `on_tick` once with the elapsed and step-relative tick counts.
3. Advance a pending multi-tick action at most once for this tick, or enter a due step.
4. On step entry, execute each immediate action once in order; a yielding action suspends at its cursor.
5. When all actions finish, select the transition once, sample its delay once, and save the destination/deadline.
6. Follow zero-delay transitions within a bounded per-pump budget; future transitions wait for their due turn.
7. Settle completion/interruption and release owned control at a safe boundary.

If a callback cancels the instance, removes a required actor, or changes dimensions, revalidate before the next action. `on_tick` never replays the step's entry actions. Movement is a stateful yielding action; an EOC callback is immediate and must use graph transitions for delayed continuation.

`repeat` is explicit and bounded. A repeat occurrence has its own entry serial, so it may draw a new line while resuming the same occurrence cannot reroll one. The two-tick message example uses this mechanism.

Random choices use an instance stream derived from a saved world/session seed and unique instance ID. Selection has its own saved stream. Evaluation order is canonical: priority, definition/rule ID, then occurrence serial. Random selection is not consumed by debug inspection or pure candidate conditions. Branch probability, weighted selection, interval, distance, and text choice are sampled at their documented entry/selection point, never repeatedly while waiting.

Proposed initial limits: 32 immediate transitions per pump, 256 total step entries per instance, a default 30-tick lifetime, and bounded movement/spawn/repeat counts. Definitions may request other validated limits within engine ceilings. Exceeding a work budget yields to a later pump; a graph that repeatedly exhausts its budget terminates with a diagnostic. Timeout always settles fatal holds and control claims.

Do not use wall-clock sleeps to express a tick. Optional cinematic display pacing can delay presentation briefly, but cannot affect RNG, physics, outcomes, or save state.

## 3. Stable participants and EOC context

Characters use their saved `character_id`; bind the original avatar identity, not “whichever body is the current avatar later.” Monsters need a durable instance identity. The current [tracker IDs][tracker-id] are vector-position-based temporary IDs, and ordinary presence/lookups exclude dead creatures. A pointer, monster type, tile coordinate, or nearby name is not a replacement identity. [Tracker presence][tracker-presence], [monster save path][monster-save].

Add a narrow shared `creature_ref` or reuse a suitable durable-reference implementation if one exists by implementation time. It must distinguish character and monster identity, dimension, absent/unloaded, defeated, finalized-dead, and missing. Add monster UID persistence, allocation/reservation, legacy-save assignment, and duplicate detection before claiming saved monster scenes work. Keep the UID when the same monster polymorphs; record its original type separately for trigger facts. A spawned replacement is a new creature.

Weapon bindings store the original item UID/reference plus type/category/skill snapshots. A sequence requiring that weapon fails or branches when it is dropped, destroyed, or no longer usable; it cannot silently select a replacement knife. A narration-only reference may use the captured weapon description after the physical item is gone.

EOCs run by default with actor as `u` and target as `npc`, regardless of whether either is an NPC, monster, or avatar. An individual callback may remap those roles explicitly. Supply namespaced context values:

- Execution instance/definition, step/entry, elapsed ticks, current result/reason.
- Trigger channel, attack/contact IDs, weapon/style/technique snapshots, critical/hit state.
- Body-part and weakpoint IDs when available; per-type resolved damage and actual HP changes as separate values.
- Original and current participant locations, dimension, original attack direction.
- The last typed action result and declared instance parameters/locals.

Missing context is absent, not a fabricated body part, zero damage of an unknown type, or an avatar substitute. Snapshot data describes the triggering hit; live queries describe now. General EOCs may still query live actors normally.

The existing [activation API][eoc-run] supports reading values back through an `on_done` callback. Use that for explicitly declared local outputs/transition requests rather than accidentally persisting an entire mutable dialogue. Queued EOCs currently save context and rebuild dialogue around a queue owner; they do not preserve both original participants. [Queue record][eoc-queue], [delayed invocation][eoc-delay].

Therefore `run_eocs` with its own future timer is not automatically execution-owned. Document it as detached work, warn on obvious uses in sequence callbacks, and prefer graph delays. A future owned-child scheduler must have explicit cancellation/reference behavior.

## 4. Trigger selection and combat integration

Support manual `start_execution` first, returning an instance handle or a reasoned failure. Automatic rules subscribe to a small set of supported semantic trigger stages:

| Stage | Appropriate use |
| --- | --- |
| `attack_resolved` | A completed nonfatal hit, critical, or miss; rich channel/weapon/style context |
| `damage_resolved` | A configured damage/contact result, including non-attack sources when supported |
| `fatal_candidate` | A supported fatal hit before death finalization; requires the fatal-resolution phase |
| Explicit EOC start | A technique, monster attack, item, spell, dialogue, or scripted encounter intentionally starts a scene |

The current [character melee event][melee-before] is emitted before actual damage and technique resolution. The current [technique post-hit callback][melee-after] occurs after `check_dead_state`. Neither can simply be renamed into the universal trigger stage above. Existing [martial-arts context][ma-context] provides useful fields, and [damage EOCs][damage-eocs] provide source/target facts, but their timing and payloads differ.

Introduce an attack-resolution scope: producers accumulate a bounded combat snapshot and submit ordinary trigger candidates, then the outer attack boundary drains starts after normal attack processing is safe. Fatal admission is earlier: the supported damage-commit adapter selects/reserves a fatal candidate before callbacks or checks can finalize that injury. Only graph activation waits for the outer boundary. Do not move physical actors halfway through an attack whose remaining code still assumes their original location.

Index rules by trigger stage/channel and cheap actor/target/weapon/type discriminators. Matching supports:

- Attacker and target kind; exact monster IDs/species/flags, NPC class/faction/traits, and direction of the interaction.
- Weapon ID/flags/category/attack skill, selected style, technique, or monster attack ID.
- Applied damage types/amounts, actual HP loss, body part, weakpoint where the engine reports one.
- Critical/miss state, strength/size/skill differences, incapacitation, and health thresholds.
- Target position, terrain/furniture/field, Z/support, region/dimension, and ordinary EOC conditions.

Only fields actually reported by an adapter are matchable. A missing monster body-part model or an absent style on an environmental hit is not inferred from the avatar. A low-level damage callback does not automatically know the selected martial-arts style.

Selection order: pure matching → eligibility/control checks → per-rule chance roll once → highest surviving priority tier → weighted choice within that tier → final revalidation → atomic participant acquisition → queue start. If acquisition fails, use a documented next candidate without rerolling the evaluated pool. If no rule is selected, the original action completes normally. Empty pools and all-zero weights select nothing.

For `fatal_candidate`, apply that selection order at the supported fatal-commit point using facts available there, then reserve finalization before exposing the fatal injury to callbacks. Publish its field matrix separately from `attack_resolved`: it cannot depend on effects or aggregate results that happen later in the attack. Later callbacks can heal/invalidate the chosen scene; that releases/settles it without rerolling another candidate. With no selected fatal rule, create no hold and retain normal immediate death ordering. A direct death caused earlier by an arbitrary EOC is outside this adapter's supported fatal path.

Record cooldowns by rule plus declared scope, such as actor, target, pair, or world. Global/session storage is necessary for monster-only pairs. No cooldown is committed until a scene starts. Carry a causal chain/parent ID and suppress automatic retriggering from a sequence's own child actions by default. One root attack/contact is evaluated once at each advertised stage; damage units and repeated wrapper notifications must not multiply its chance.

## 5. Participant control, action costs, and interruption

Each creature role requests `free`, `held`, or `scripted` control. `free` allows ordinary AI/input; `held` suppresses voluntary locomotion/attacks while waiting; `scripted` also permits sequence actions to direct it. Control is a saved claim owned by the instance, not a permanent stun effect. One exclusive claim per creature; acquire all required claims together or start none.

The manager owns time and program progress. NPC/monster/character adapters enforce the claim and spend or clear the appropriate action opportunity so native move loops cannot spin forever. The avatar can use an activity adapter that waits normally between beats; it must not become a second scheduler. Current [NPC loops][npc-turn] explicitly detect iterations that fail to spend moves. [NPC entry][npc-move], [monster entry][monster-move].

The original attack pays its ordinary cost once. Definitions may add named stamina/move/resource costs at a declared commit point. Held time does not grant stored free actions on release. Do not double-charge one move as both a normal voluntary step and a scripted wait. Suspending an activity/order needs an explicit token and compatibility check before resumption; never overwrite a newer player order with stale pre-scene state.

Default interruption behavior:

| Situation | Default |
| --- | --- |
| Third-party damaging hit or dangerous displacement | Interrupt before the next scripted action; settle any fatal state immediately |
| Required actor/weapon lost or out of permitted range | Interrupt with a reason; no replacement participant |
| Optional actor gone after its role is complete | Continue only steps whose requirements remain satisfied |
| Action blocked or unsupported | Follow its result branch, otherwise fail cleanly |
| Player cancels an interruptible controlled-avatar scene | End control and run interruption cleanup; committed damage/world changes remain |
| Required participant unloads or changes dimension | Settle/cancel before detachment; no offscreen continuation |
| Scene lifetime expires | End with `timeout`, release claims, settle pending fatal resolution |

A definition may choose supported continue/branch behavior for ordinary interruptions, but cannot prevent destruction, invalid references, or emergency finalization. The world continues: fire, other combatants, traps, falling, and weather are not paused. Automatic hostile control of the avatar must be explicitly opted into by content/policy; ordinary nonfatal avatar participation is supported, while fatal avatar deferral is deferred beyond the first fatal-release scope.

“Hide/accelerate presentation” preserves simulation ticks and consequences. “Cancel the sequence” invokes interruption semantics. Neither is permission to reverse committed injury or avoid an already fatal outcome.

## 6. Physical and world actions

Use a small typed action set with structured results, plus ordinary EOCs as an escape hatch. Initial native actions should cover relative movement, messages, effects, attributed damage, incapacity, waits/repeats, and fatal-resolution settlement. Map/item/field changes can initially call existing EOCs, with narrow typed adapters added when results are needed.

Every result identifies source/target, attempted and actual change, position/dimension, completion status, and reason. Movement additionally reports requested/actual distance, collision, support transition, falling/landing, and death if observed. Damage reports per-type resolved damage, actual HP changes, and finalization/prevention status separately. Existing [damage results][damage-result] are useful input but should not be mislabeled as every downstream health/side-effect result.

For `backward`, capture the direction away from the attacker at start, quantized deterministically to the grid. It is not the victim's facing, which the scene cannot assume exists. Recompute the destination from the victim's current position along that fixed vector. A coincident origin has no valid direction and needs an authored fallback.

Staggering performs one checked adjacent movement per tick. It respects occupancy, impassable geometry, diagonal corner rules, vehicles/mounts/grapples, and support policy. `avoid_unsupported` stops at the edge; a separate authored branch can then choose to step into it. Do not use `setpos` to bypass collision and cleanup.

An edge probe considers live support, traversability, occupied space, vehicle floors, flight/gliding/gravity immunity, and loaded bounds. “Open air” by terrain ID alone is insufficient. Revalidate when the step commits, since callbacks can change the map after the earlier probe. Use [gravity][character-gravity], [monster gravity][monster-gravity], [falling][map-fall], and [knockback][knockback] adapters, supplying source attribution to secondary impacts where the existing path loses it. Do not force-load distant terrain just to finish a cutscene.

Normal impact determines whether a fall kills. An explicit authored fatal-injury action uses normal injury/death-prevention rules and reports `prevented` if those rules save the target. A knockout action must model incapacity without marking death: use character/monster adapters, immunity checks, wake/recovery rules, and effect sources. It must not implement unconsciousness by invoking death or creating a corpse.

Arbitrary EOCs can mutate more than the typed actions report. Their effects are not automatically reversible or transactionally rolled back. Failures end future sequence work; they do not restore the old world wholesale.

## 7. Fatal-hit sequences require a death-finalization contract

This is the highest-risk part and a distinct implementation phase. A normal sequence must not begin on a victim whose death has already produced loot, death functions, kill XP, faction consequences, or corpse placement. [Monster death][monster-death], [NPC death][npc-death]. Existing [cleanup][cleanup] and [tracker cleanup/removal][tracker-cleanup] can process fatal health independently of a chosen melee callback.

Recommended model: **apply the real injury, but reserve its final death resolution** for a short supported sequence. Keep raw lethality truthful. Add an explicit `pending_fatal_resolution` record; do not set HP to 1, grant invulnerability, revive a finalized creature, or overload every `is_dead_state` query to pretend it is healthy.

The lifecycle needs distinct predicates/operations for raw fatal health, finalized death, eligibility for normal action, occupancy, and execution-controlled motion. Current [monster][monster-dead] and [NPC][npc-dead] `is_dead()` combine raw fatal health with finalized state, so adding one conditional in `check_dead_state` is insufficient.

A valid reservation:

- Is created inside a supported attack-resolution scope before the first possible finalizer, with saved victim/source identity, combat facts, deadline, and sequence owner.
- Retains the creature in tracking and occupancy, but excludes ordinary AI/input and ordinary living-target selection. The execution resolves it by handle through an explicit defeated-actor path.
- Defers all normal death side effects, including on-kill rewards/callbacks, until one finalization. Existing post-hit hooks need an explicit `death_pending` fact; `killed` cannot be reported as finalized while the outcome is still preventable.
- Allows only supported sequence motion/physics while held. Adapters must account for dead-filtered lookups; raw coordinates cannot substitute for the held creature.
- Allows ordinary prevention/rescue to win through defined paths. If genuine healing removes fatal health before finalization, settle as `rescued`; do not force death merely because a scene once reserved it.
- Ends immediately on a third-party harmful action, invalidation, cancellation, timeout, unload, or unsupported physics. If health is still fatal, finalize normally; if it recovered, release alive. It supplies no ongoing shield from the world.
- Releases the hold before invoking native death/prevention processing, under a one-shot reentrancy guard. Existing NPC/avatar prevention hooks must not be bypassed or run repeatedly by retrying a cinematic ending.

For an owned fall, keep the reservation only through the supported motion/landing operation, then settle it. An already fatal victim can land before the corpse is created; an ordinary living victim takes normal fall damage. Preserve original and contributing sources rather than assigning credit to the victim or whichever avatar happens to be current. [Current source limitations][effect-source].

Fatal support initially targets NPCs and monsters. Avatar fatal handling has additional death/prevention/game-over flows and requires a separate adapter/test pass; it must be rejected with a clear unsupported-policy result until delivered. Nonfatal scenes may still include the avatar.

This phase must review direct `die` callers, cleanup, tracker occupancy/removal, healing, on-kill martial arts, death functions, transformation/despawn, saves, and dimension/unload paths. Unsupported fatal channels fall back to normal immediate death; they must never leave an unowned defeated creature. The exact knife scene can also run on a nonfatal hit without this feature.

## 8. Narration, pools, and append semantics

Reuse translated text/snippet pools, with optional weights and bounded no-immediate-repeat memory. Draw once per occurrence and retain the selected translation/snippet identity and parameters. Snapshot names/pronouns/anatomical descriptions when needed; do not call an invalid creature pointer to finish a sentence.

Messages have an execution-local entry ID and an explicit audience: visible action, audible action, controlled participant, or an already observed action result. Visibility is checked at the beat, including source and destination for motion. Hidden actors do not become named merely because the narrator has stable references.

`append_to` targets an entry created by the same instance, not the global last message. Store ordered, timestamped segments so unrelated combat log lines remain intact and history retains when each fragment appeared. The compact view can show a growing passage; the chronological log must not silently move a later event earlier. After a reload or log truncation, a missing append target falls back to a new owned entry.

Support author-supplied fragments, but prefer full translated sentence segments with named role substitutions. Concatenating English clauses is not a reliable localization format. An unseen segment cannot be revealed retroactively when the scene becomes visible.

The [message API][messages-api] and [message record][message-record] need extension for stable owned entries/groups. Do not modify the last line by position, rely on duplicate-message coalescing, or block simulation on modal narration. Presentation-off mode suppresses text/extra display pacing while retaining gameplay behavior.

## 9. Save/load, unload, and terminal callbacks

Save instances at action-safe checkpoints alongside the active game/session state, with monster/resident/item identity data in their appropriate save records. The [current save path][game-save] already stages player state and saves world/dimension data; integrate with its ordering rather than creating an unrelated scene save file.

Persist the selected transition/due turn, random state, message choices, repeated occurrence, motion progress, role/control ownership, and pending fatal token together. Do not serialize dialogue pointers or callback closures. On load, resolve references before processing callbacks or allowing death cleanup. Restore control without replaying `on_start` or completed actions.

If content changed, require the same declared compatibility revision and valid saved step/action shape. Otherwise invalidate safely, release controls, settle fatal state, and report a diagnostic. A missing definition or malformed save must never strand a creature. Restoring an older checkpoint is ordinary game rollback; the guarantee is no duplicate actions within a successfully loaded saved state, not cross-crash distributed transaction semantics.

Terminal ordering is explicit: latch the requested end path and stop future steps → settle any pending fatal token through native death/prevention → record the actual outcome → mark terminal and release this instance's control/owned scheduling claims → run `on_complete` or `on_interrupt` as appropriate → run `on_end`. Failure/invalidation/timeout carry their distinct reasons through interruption cleanup. Native finalization may rescue the actor, so final outcome is sampled after it, not predicted from the requested end label.

Guard the whole terminal checkpoint against reentry and defer save requests until it finishes. Callbacks use still-valid optional talkers and immutable snapshots; they cannot resurrect an unavailable pointer. An interruption callback that wants to prevent a fatal outcome is too late: rescue belongs before settlement or in normal death-prevention hooks. Core control/fatal cleanup cannot depend on a content callback succeeding, and old cleanup must not release a new scene's claims. Canceled/finished instances leave only bounded diagnostics/transcript/history and the intended physical consequences.

Before unload, dimension switching, actor removal, or clearing a dimension, settle affected scenes while their actors and map are still available. No catch-up replay of missed cinematic ticks in the first version. A normal save/load without unloading the active participants resumes at the recorded deadline.

<!-- Source references -->

[character-gravity]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/character.cpp#L7279
[cleanup]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/game.cpp#L4133
[damage-eocs]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/damage.cpp#L437
[damage-result]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/damage.h#L216
[effect-source]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/effect_source.h#L19
[eoc-delay]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npctalk.cpp#L7370
[eoc-queue]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/character.h#L321
[eoc-run]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/effect_on_condition.cpp#L458
[game-save]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/game_io.cpp#L892
[knockback]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/game.cpp#L4175
[ma-context]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/martialarts.h#L114
[map-fall]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/map.cpp#L11753
[melee-after]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/melee.cpp#L1066
[melee-before]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/melee.cpp#L839
[message-record]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/messages.cpp#L48
[messages-api]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/messages.h#L25
[monster-dead]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/monster.cpp#L4031
[monster-death]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/monster.cpp#L3194
[monster-gravity]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/monster.cpp#L412
[monster-move]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/monmove.cpp#L995
[monster-save]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/savegame_json.cpp#L2602
[npc-dead]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npc.cpp#L3344
[npc-death]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npc.cpp#L3398
[npc-move]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npcmove.cpp#L1586
[npc-turn]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/do_turn.cpp#L277
[tracker-cleanup]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/creature_tracker.cpp#L263
[tracker-id]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/creature_tracker.cpp#L49
[tracker-presence]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/creature_tracker.cpp#L201
[turn-loop]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/do_turn.cpp#L559
