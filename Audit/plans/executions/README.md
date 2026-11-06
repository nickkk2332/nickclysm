# Executions: implementation design

**Status:** the living-participant runtime, [owned appendable/audible narration](05-narration-milestone.md), [automatic primary-melee rules](06-automatic-triggers.md), [physiological knockouts](07-knockouts.md), [NPC/monster fatal melee scenes](08-fatal-hits.md), [avatar fatal deferral](09-avatar-fatal-deferral.md), [nonfatal projectile triggers](10-projectile-triggers.md), and [fatal projectiles, physical actions and clashes](11-fatal-projectiles-physical-clashes.md) are implemented. See [first-milestone status](04-implementation-status.md) and the supported [JSON contract](../../../doc/JSON/EXECUTIONS.md). The documents below retain the broader design and future proposals; their proposed schemas must not be assumed implemented. Original source inspection was pinned to Nickclysm `b32c9baea25f62478228c6a91cbbf9f356e4e8a3`.

An **execution** is a short, stateful sequence of authored beats that can act on creatures and the world. Its name includes finishing moves, but its outcome can be survival, unconsciousness, escape, interruption, a completed exchange, or death. A sequence should be able to describe a brutal takedown, an unlucky stumble, or a rare cinematic clash using the same machinery.

The implementation should be a small saved state machine with EOC integration, typed physical actions, and explicit participant ownership. It should run through ordinary game ticks, retaining branches, rolls, messages, and progress across saves. Existing delayed EOCs alone are insufficient for keeping two particular creatures and their sequence coherent.

## Reading order

| Document | Purpose |
| --- | --- |
| [01-runtime-design.md](01-runtime-design.md) | Time, participants, triggers, control, movement, death handling, interruption, and persistence |
| [02-authoring.md](02-authoring.md) | Proposed JSON contract and worked examples, including the knife/stagger/ledge scene |
| [03-implementation-plan.md](03-implementation-plan.md) | Code integration, delivery phases, tests, validation, and release criteria |
| [08-fatal-hits.md](08-fatal-hits.md) | Fatal melee reservation, lifecycle adapters, attribution and validation record |
| [09-avatar-fatal-deferral.md](09-avatar-fatal-deferral.md) | Player control, prevention, saved death state and game-over handoff |
| [10-projectile-triggers.md](10-projectile-triggers.md) | Nonfatal handheld firing, first-hit arbitration, context and producer exclusions |
| [11-fatal-projectiles-physical-clashes.md](11-fatal-projectiles-physical-clashes.md) | Fatal firing admission, physical verbs, attribution and cinematic clash content |

## Decisions proposed for the first implementation

| Concern | Proposed behavior |
| --- | --- |
| Time | One execution tick is one game turn, currently one second. Speed/move points do not cause extra sequence ticks. No blocking real-time sleeps. |
| Authorship | Named steps form a graph. Steps perform actions and choose the next step; transitions can delay, branch, or choose weighted outcomes. |
| Repetition | Optional `on_tick` callbacks run once per active game tick. Explicit bounded repeats support a different message/action every tick. |
| Randomness | Sample each chance, interval, movement distance, and message choice once when its occurrence is entered. Save the result. |
| Participants | Canonical `actor` and `target` roles can each be avatar, NPC, or monster. A bound weapon is optional. Never reacquire the target as the next occupant of a tile. |
| Control | Each creature role is free, held, or scripted. The world continues. A cinematic clash can script both participants without freezing everyone else. |
| Triggering | Manual EOC invocation plus indexed, data-defined trigger rules over committed combat context. No implicit “every kill gets a cutscene.” |
| Death | Ordinary sequences start on living participants. Fatal-hit scenes additionally need explicit delayed death finalization; they cannot resurrect an already processed corpse. |
| Consequences | Movement, falling, damage, incapacity, map changes, and ordinary EOCs can change the actual world. Messages alone do not establish an outcome. |
| Interruption | Explicit policies for damage, participant loss, player cancellation, displacement, and invalid action state. Defaults keep scenes short and interruptible. |
| Visibility | Simulation and narration are separate. Only perceived beats are narrated; actor names and messages must not expose unseen actors. |
| Persistence | Save the instance, roles, random choices, active action, next deadline, owned control, transcript, and any pending fatal resolution. |
| Unloaded areas | Cancel/settle before a required participant unloads or changes dimension. No offscreen replay in the first release. Saving/loading a still-active area resumes normally. |

These are working design decisions, not requests for another approval step.

## The requested knife scene

The full example is in the authoring document. Its intended behavior is:

1. A configured knife hit selects the execution, binds the real attacker/victim/weapon and attack direction, and starts it at a safe attack boundary.
2. Roll a stagger distance of 1–4 tiles once. The victim takes one backward step per tick, stopping at obstacles or unsupported terrain rather than teleporting through them.
3. Inspect the tile immediately behind the victim at the end of that motion. If it is a traversable unsupported destination and this victim can actually fall, roll the 50% branch once.
4. On that branch, commit a backward step into the opening and use native gravity/collision handling. Emit the tumble message when movement actually starts. Normal impact can kill, injure, or be survived.
5. Otherwise hold position for two ticks, drawing a message from the configured pool each tick. Examples include coughing blood and grasping at the knife, staring wide-eyed, or moving the jaw silently.
6. Finish with the actual outcome. A nonfatal initial hit can end with the victim alive. A reserved fatal hit settles through ordinary death/prevention processing. An explicitly lethal authored ending requires a separate configured injury action.

An open-air tile is not proof of death. A short fall, flight, a vehicle floor, an obstruction, or an intervention can change the result. If the author wants a particular scene to be fatal, they must request a real lethal consequence and accept its documented prevention/immunity behavior.

## Existing pieces to reuse

- Martial arts already supply opponent, weapon, technique, body part, damage, hit, critical, and kill context. Technique `post_hit_eocs` and style event EOCs are real current features. [Context][ma-context], [dispatch][ma-dispatch].
- Damage types already have EOC hooks with source, target, body part, and damage values. Their timing is inside damage processing; they are not a universal finalized-hit callback. [Damage processing][damage-core], [damage EOCs][damage-eocs].
- NPC goals and character errands already demonstrate saved commitments, interruption callbacks, and work spread across turns. They can inform control adapters, but the execution manager must also support monsters and both participants together. [Goals][npc-goals], [errands][errands].
- Knockback, flinging, falling, effects, and EOCs already manipulate the world. Use adapters with result reporting and attribution instead of a second physics implementation. [Knockback][knockback], [flinging][fling], [falling][map-fall].
- Snippets and translations can supply message pools. Message append requires an explicit owned-entry API; the current public message API is not a stable append-by-execution interface. [Snippets][snippets], [message API][messages-api].

The existing Perk Melee mod has techniques named executions with large damage bonuses and an EOC. Those are useful content examples, but not this proposed multi-tick system. Preserve their behavior and IDs; migration is optional future content work. [Existing techniques][perk-executions], [associated EOC][perk-eoc].

## Scope and delivery target

The first complete demonstration should include:

- The knife scene with random stagger distance, a real ledge branch, per-tick message pools, and a surviving alternative.
- A critical-hit knockout with no kill rewards or corpse.
- A strength-dependent short takedown.
- A rare two-actor clash that branches into separation, a counter, or interruption.
- Monster→NPC and NPC→monster examples, plus a saved scene resuming without rerolls or duplicated actions.

The full release includes fatal-hit scenes only after the death-lifecycle phase passes. Earlier milestones can demonstrate living-actor scenes without pretending they safely intercept already lethal hits.

Related foundation work appears in the [powers audit](../../powers/README.md), [interactivity audit](../../interactivity/README.md), and [world/events audit](../../worldgen/README.md). Reuse their reference, action-result, ownership, and cancellation contracts where implementation overlaps; the execution system need not wait for entire settlement or physics frameworks.

<!-- Source references -->

[damage-core]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/creature.cpp#L1591
[damage-eocs]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/damage.cpp#L437
[errands]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/errand.h#L37
[fling]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/game.h#L715
[knockback]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/game.cpp#L4175
[ma-context]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/martialarts.h#L114
[ma-dispatch]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/martialarts.cpp#L1616
[map-fall]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/map.cpp#L11753
[messages-api]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/messages.h#L25
[npc-goals]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npc_json_goal.h#L40
[perk-eoc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/data/mods/Perk_melee/EOC/shared_eocs.json#L69
[perk-executions]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/data/mods/Perk_melee/techniques/executions.json#L3
[snippets]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/text_snippets.h#L21
