# Casting, EOCs, and persistent power state

These findings concern shared infrastructure. Priorities and effort are defined in [README.md](README.md). Every “proposed extension” below is a recommendation, not existing syntax.

## CAST-01 — A cast has no shared, stable evaluation context

**P0 · L · Confirmed representation gap.**

**Evidence:** [`magic.cpp:821–892`][values] evaluates damage with caster as alpha and no beta. Range, AoE, duration, and related numeric getters use the same pattern. [`magic_spell_effect.cpp:2462–2509`][spell-eoc] supplies useful context to the effect EOC, but constructs it there rather than receiving a shared cast context. [`magic_spell_effect.cpp:958–1117`][damage-targets] calls getters repeatedly, including random-valued damage; the EOC context's random AoE roll can also differ from the earlier geometry roll.

**Impact:** A single cast cannot conveniently guarantee one declared roll, retain target-aware formulas, propagate caller variables into child effects, and correlate all resulting hits. An inherited nonlinear spell that asks for its own learned level by literal ID is especially awkward for temporary spells cast at a different effective level.

**Current workaround:** Use linear min/increment/max scaling; read the supplied spell EOC context; explicitly store selected values in actor/context variables. This works for controlled scripts, but native effects and previews do not automatically consume those stored values.

**Proposed extension:** A cast instance carrying effective level, caster/source, root/parent IDs, selected target, origin, parameters, and documented random rolls. Decide which properties snapshot at cast start and which evaluate per target. Pass that context through native effects, formulas, EOCs, previews, and child effects. Do not silently change existing random-per-call content.

## CAST-02 — There is no common invocation policy

**P0 · L · Confirmed path divergence.**

**Evidence:** [`npctalk.cpp:6495–6563`][eoc-cast] calls `cast_all_effects` directly. [`handle_action.cpp:1928–1980`][character-cast] checks Character conditions and starts an activity; [`activity_actor.cpp:2819–2931`][activity] handles failure, payment, components, XP, and effects. [`mattack_actors.cpp:368–423`][monster-cast] resolves monster spells immediately after deducting moves. [`magic.cpp:2514–2532`][children] resolves child effects without a new payment.

**Impact:** A super move reached from a mutation menu, EOC selector, monster attack, and normal spell menu can follow different rules. Copying cost and cooldown checks into every EOC creates inconsistent cancellation and accounting.

**Current workaround:** Put conditions and payment in the caller. Effect-only casting is valuable for procs and child effects and must remain available.

**Proposed extension:** Separate a full ability request from effect-only execution. A request should declare validation, payment, time, failure, XP, cooldown, targeting, and interruption policies explicitly, then return a result. Supply a dry-run eligibility query. Adapt existing entry points gradually; do not make every passive proc pay a spellbook cost.

## CAST-03 — Windup, telegraph, and interruption hooks are incomplete

**P1 · M–L · Confirmed lifecycle gap.**

**Evidence:** [`activity_actor_definitions.h:1130–1175`][casting-actor] gives the spellcasting activity an empty `do_turn` and no spell-specific cancellation override. [`activity_actor.cpp:2795–2931`][activity-start] contains distraction handling and finish logic. [`event.h:348–359,1081–1093`][cast-event] describes casting/finish events, without a target or cast-instance ID. [`magic.cpp:2408–2421`][effect-event] emits `character_casts_spell` at effect execution, not a universal windup start. Magic types have failure EOCs, but those are not a complete per-spell lifecycle.

**Impact:** Charging an ultimate with a visible danger area, a timed parry window, a counterspell before release, or distinct release/cancellation costs needs caller-specific scaffolding. Monster spells lack an intervening casting activity in which opponents could respond.

**Current workaround:** Stage an EOC sequence with effects, timers, and `u_cancel_activity`; use the existing channel system for some repeated actions. Such a script does not share one lifecycle across all cast paths.

**Proposed extension:** Hooks for validate/start, windup tick, release, fail, interrupt, cancel, and end, with target/origin and an explicit interruption reason. Define exactly when payment occurs and whether a reaction can veto release. Keep these distinct from damage resolution hooks.

## CAST-04 — Resolution has no structured result

**P0 · M–L · Confirmed interface gap.**

**Evidence:** [`magic.h:196–237`][spell-type] stores effect functions returning `void`; [`magic.cpp:2392–2532`][cast-effects] also exposes void effect execution. Native attacks locally calculate dealt damage, but the enclosing cast does not return per-target hit data. EOC casting's `true_eocs` is an invocation branch, not proof of a hit, a kill, or a successful teleport.

**Impact:** “Refund half the energy if nobody was hit,” “chain to the nearest unhit enemy,” “lifesteal actual HP damage,” and “launch only after a successful strike” need fragile reconstruction or duplicate damage logic. Source events can report some of this after the fact but do not aggregate one composite cast.

**Current workaround:** Martial-art post-hit context provides real melee outcomes. EOC-specific branches and damage events can maintain actor variables for simple cases.

**Proposed extension:** A resolution result with accepted/cancelled/failed/resolved state, reason, per-target hit and damage by type, body part, movement result, spawned references, and child results. Separate the meaning of “effect dispatched,” “target affected,” and “attack hit.” Make native and EOC effects contribute to the same result.

## CAST-05 — Targeting needs a reusable target-set contract

**P1 · M–L · Confirmed schema boundary; expansion judgment.**

**Evidence:** [`magic.h:102–121`][target-types] defines the target and shape enums. [`magic.cpp:964–1019`][select-target] selects a location, while [`magic.cpp:1753–1825`][valid-target] recognizes a creature, then a vehicle, otherwise ground. EOC spell processing in [`magic_spell_effect.cpp:2482–2509`][spell-eoc] binds creature/vehicle or no alpha. EOC tile queries, item searches, and adjacent selectors exist separately.

**Impact:** “Choose three different enemies,” “pick a point and an axis,” “select a particular buff to dispel,” and “choose the item inside the enemy's hand” lack a shared targeting/preview contract. A `valid_targets` enum entry alone does not guarantee native item/field targeting semantics.

**Current workaround:** Compose EOC selectors, location variables, and item/creature iteration. An EOC can implement many selection rules today.

**Proposed extension:** Typed targets and ordered target sets: creature, item, vehicle/part, tile, area, active effect, and power instance. Support count, filters, duplicates/visited policy, distance metric, visibility, lock-to-entity versus lock-to-location, and explicit failure/cancel results. UI and AI should use the same query.

## STATE-01 — Persistent participant references are incomplete

**P0 · L · Confirmed reference-model gap.**

**Evidence:** [`npctalk.cpp:7422–7459`][talker-resolution] resolves `run_eocs` talkers from current actors, avatar, character IDs, or an entity's current tile. [`effect_source.h:24–64`][effect-source] retains character/faction attribution and explicitly cannot resolve an individual monster. Location variables, item locations, NPC IDs, and immediate spawn callbacks provide useful but different forms of identity.

**Impact:** A curse must keep following the same monster after it moves; a homing attack must not hit whoever later occupies its old tile; a stand needs to identify its own summoner. Searching by type or a mod variable can approximate identity but is not a generic reference with invalidation behavior.

**Current workaround:** Character IDs, immediate `spawned_eocs`, actor variables, localized searches, and item talkers for the duration of a call.

**Proposed extension:** Typed, serializable references to creatures including monsters, items, vehicles/parts, and power instances. Define gone/dead/unloaded/wrong-dimension resolution separately; never retain raw pointers in delayed scripts. Reuse existing IDs and item locations where their guarantees suffice rather than adding competing identities.

## STATE-02 — Delayed work needs participants, cancellation, and ownership

**P0 · L · Confirmed scheduling gap.**

**Evidence:** [`character.h:321–327`][queued] stores only EOC ID, time, and context. [`npctalk.cpp:7370–7385`][process-eoc] queues non-global delayed activation on a Character alpha; non-global monster/item alpha is silently skipped. [`effect_on_condition.cpp:320–386`][process-queue] restores saved context into the processing dialogue rather than restoring both original participants.

**Impact:** “Detonate this marked enemy in three turns,” “cancel only my first projectile,” and “end the aura when its caster dies” need manually recreated participants and cancellation flags. The queue has no first-class per-job ID, parent cast, owner, or cancellation result exposed to JSON.

**Current workaround:** Schedule on a Character/global EOC, retain IDs/locations in variables, and guard callbacks with generation counters. Useful, but easy to mishandle when the participant disappears or several uses overlap.

**Proposed extension:** Cancellable scheduled instances with actor references, origin/dimension, owned lifetime, reasoned cancellation, and an explicit unload/catch-up policy. Keep immediate EOC loops as loops. A delayed iteration must not retain an unsafe dialogue pointer or depend on the next occupant of a tile.

## STATE-03 — Effects are not independent power applications

**P1 · L · Confirmed storage boundary.**

**Evidence:** [`creature.cpp:1954–2006`][add-effect] merges an existing effect by effect ID and body part, updating duration/intensity. [`effect.h:454–466`][effect-instance] has source attribution, but no general cast-instance metadata bag. [`creature.cpp:2073–2106`][remove-effect] removes by type/body part. Character gain/loss/intensity events already exist; they are not absent.

Source storage also does not guarantee source propagation: the native spell status helper ([`magic_spell_effect.cpp:927–950`][status-source]) and Character talker effect application ([`talker_character.cpp:595–607`][talker-effect]) call source-less `add_effect` overloads rather than supplying the caster. Grappling's explicit source-aware hold is an example of a richer, specialized path already in the tree.

**Impact:** Two wizards applying the same curse cannot generally retain separately dispellable durations and strengths. A copied form cannot easily distinguish its own contribution from a naturally acquired trait. Generic purge-by-school, dispel-by-caster, immunity duration, and source-aware stacking need more than an effect ID.

**Current workaround:** Different IDs for different powers/sources, variables for stacks, Character effect events, or a recurring EOC that reconciles state. Finite content can use these successfully.

**Proposed extension:** An owned application layer recording source/cast, tags, magnitude, stacking group/rule, duration clock, and lifecycle reasons. Existing `effect_type` remains the mechanical projection if desired. Add application/removal hooks with actor parity and source-aware enumeration/dispel rather than replacing every existing effect immediately.

## STATE-04 — Resources, cooldowns, and charges are fragmented

**P1 · M–L · Confirmed schema boundary.**

**Evidence:** [`magic_type.h:20–31`][energy-types] limits spell energy kinds; [`magic.cpp:2067–2099`][payment] consumes one selected kind. Vitamins already provide a customizable resource route. Mutation [`mutation.h:204–226`][mutation-cost] and monster `special_cooldown` math provide separate mechanisms. Spell definitions/known spell state have no common named cooldown-group, charge-recovery, or multi-resource cost object.

**Impact:** Ki plus stamina, a heat/overload gauge, rechargeable dashes, shared ultimate cooldowns, and reserved energy for a sustained barrier are all possible to script but do not automatically share UI, eligibility, save behavior, or AI use.

**Current workaround:** Vitamins, mana, variables, timed marker effects, mutation charges, monster cooldowns, and custom widgets. The absence is a common ability contract, not an inability to store a number or enforce a timer.

**Proposed extension:** Optional named resource adapters and multi-cost requests, cooldown groups, charge recovery, reserve/commit/refund, and clock policy. Existing mana/stamina/vitamins must remain valid backends. Tie spending to CAST-02 and damage absorption to COMBAT-01 so a shield cannot overspend during one hit.

## STATE-05 — Sustained powers need owned lifetimes

**P1 · L · Confirmed limitation of current channel model.**

**Evidence:** [`magic.cpp:2851–2910`][channel] channels using `last_spell`, one channeling effect, and `last_magic_target_pos`; it chooses tick/end/interrupt child spells. Mutations already have processed/deactivation EOCs, and shipped Mind Over Matter concentration scripts manage sustained buffs using effects and variables.

**Impact:** Keeping two independent barriers, maintaining a beam while moving, reserving concentration slots, transferring an aura, and dismissing only one of three active summons lack one runtime owner. A global recurring EOC can do this but must invent the lifetime and cleanup rules.

**Current workaround:** Existing channel data for one channel; effects plus recurring EOCs for multiple buffs; mutation toggles for stances.

**Proposed extension:** Optional sustained instances with parent ability, owner, target/anchor, resource drain, concentration group, interruption rules, and exactly-once cleanup. Define behavior at death, sleep, dimension change, unload, dispel, and mod removal. Do not mistake all timed buffs for channels or require every stance to occupy an activity.

## STATE-06 — Summoning needs a common bond and command contract

**P1 · M–L · Confirmed split implementation.**

**Evidence:** [`magic_spell_effect.cpp:1757–1820`][summon] gives spell summons friendliness, lifetime, drops policy and a summoner. [`npctalk.cpp:9272–9465`][spawn] adds immediate `spawned_eocs`, variables, and richer EOC spawn control. [`monster.cpp:3204–3215`][summon-credit] already recognizes a summoner for some kill credit. These are real capabilities, not an entirely ownerless summon system.

**Impact:** A stand, familiar, animated sword, or clone needs a stable link, command target, follow/guard/attack order, dismissal, and optional shared energy or damage. Spell summons have no matching per-spawn callback field; allegiance is selected from spell flags rather than a reusable relationship policy for every caster.

**Current workaround:** Use EOC spawns and `spawned_eocs` to set variables, faction, attacks and scripts; use NPC JSON goals for more elaborate companions. Avoid assuming every friendly monster is a particular caster's summon.

**Proposed extension:** A bond recording both entity references, faction/allegiance policy, command interface, leash and lifetime, cleanup, and optional resource/damage routing. Reuse existing summoner credit and callbacks, expose spell spawns through the same initialization mechanism, and keep summoned AI choices distinct from ownership itself.

[values]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic.cpp#L821
[spell-eoc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic_spell_effect.cpp#L2462
[damage-targets]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic_spell_effect.cpp#L958
[eoc-cast]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npctalk.cpp#L6495
[character-cast]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/handle_action.cpp#L1928
[activity]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/activity_actor.cpp#L2819
[monster-cast]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/mattack_actors.cpp#L368
[children]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic.cpp#L2514
[casting-actor]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/activity_actor_definitions.h#L1130
[activity-start]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/activity_actor.cpp#L2795
[cast-event]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/event.h#L348
[effect-event]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic.cpp#L2408
[spell-type]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic.h#L196
[cast-effects]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic.cpp#L2392
[target-types]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic.h#L102
[select-target]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic.cpp#L964
[valid-target]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic.cpp#L1753
[talker-resolution]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npctalk.cpp#L7422
[effect-source]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/effect_source.h#L24
[queued]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/character.h#L321
[process-eoc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npctalk.cpp#L7370
[process-queue]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/effect_on_condition.cpp#L320
[add-effect]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/creature.cpp#L1954
[effect-instance]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/effect.h#L454
[remove-effect]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/creature.cpp#L2073
[status-source]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic_spell_effect.cpp#L927
[talker-effect]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/talker_character.cpp#L595
[energy-types]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic_type.h#L20
[payment]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic.cpp#L2067
[mutation-cost]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/mutation.h#L204
[channel]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic.cpp#L2851
[summon]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic_spell_effect.cpp#L1757
[spawn]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npctalk.cpp#L9272
[summon-credit]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/monster.cpp#L3204
