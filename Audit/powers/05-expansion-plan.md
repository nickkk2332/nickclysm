# Expansion sequence and acceptance scenarios

This is a proposed implementation direction, not work performed by the audit. Interface and hook names here are conceptual labels, **not supported JSON members**. Findings and evidence are in the preceding documents.

## Develop a coherent core, then add power families

| Stage | Deliverable | Findings | Demonstration that earns its complexity |
| --- | --- | --- | --- |
| 0 | Characterize current contracts and correct the four concrete observations | BUG-01–04 | One custom strike damages the correct target with correct source; cancellation is clean; eligible actions agree with invocation; NPC dash actually moves |
| 1 | Shared cast request, context, targets and results; adapters for current execution modes | CAST-01–05 | One ability works from player, NPC, item, monster and EOC entry points under explicit policies |
| 2 | Persistent typed references, cancellable scheduled/active instances | STATE-01/02 | A delayed mark follows the same enemy through movement/save/load, and can be cancelled individually |
| 3 | Damage transaction hooks and explicit damage/source payload | COMBAT-01/02 | A mana shield consumes exactly its capacity and passes excess damage; reflection has bounded ancestry |
| 4 | Resource/cooldown adapters, owned applications and sustained instances | STATE-03–05 | A character can maintain two independent powers, dispel one, and recover one charge without leaking state |
| 5 | Movement attribution/results, practical summon commands and AI intent queries | COMBAT-04, STATE-06, AUTHOR-01 | A bruiser and a familiar use their powers credibly against opponents with the same mechanics |
| 6 | Persistent projectiles, beams and area instances | COMBAT-03/06 | A visibly charging beam can be interrupted, intercepted or contested; overlapping auras clean up independently |
| 7 | Object physics, constructs, forms and selected time policies | COMBAT-05/07–09 | Add only the large simulations demonstrated to be needed by the chosen content |

AUTHOR-02–05 belong alongside the relevant stages, not in a final polish-only batch. A hook without reliable docs, inspection, actor coverage and predictable previews is expensive for modders to use.

Stages are dependency guidance, not a mandate for a wholesale rewrite. Source attribution, cancellation branches, spell spawn callbacks, additional lifecycle observations and enchantment source context can be delivered locally. Introduce larger instance machinery only when a concrete content example needs it. Existing spell IDs, content, save data and effect-only proc paths should remain usable throughout.

## Small additions with useful immediate value

These should still have narrow contracts and tests, but do not require the entire longer-term design:

1. Fix and document the direct-damage recipient/source split and targeted-cast cancellation result.
2. Add current spell/effective level and optional target context to formula evaluation, with explicitly chosen roll semantics.
3. Expose genuine cast start/cancel observations, distinguishing them from effect execution and failure rolls.
4. Bring spell summons onto the existing spawn-callback path.
5. Supply the source item/application to enchantment evaluation.
6. Preserve instigator through fling and report actual movement/impact results.
7. Document which `fake_spell` fields each consumer honors; warn on options that a route discards.

These are candidates for separate changes. Combining them into one all-purpose power abstraction first would obscure which new contract actually solved each problem.

## Shared context contract

The minimum useful cast context should distinguish the following concepts rather than putting every value into an unstructured global variable:

| Concept | Suggested content / decision |
| --- | --- |
| Identity | Root cast, this child/action, parent, ability ID, source definition/version as needed |
| Participants | Caster, instigator, source item/application, selected targets, current affected target; resolve through typed references |
| Locations | Chosen origin and target/anchor in absolute coordinates with dimension; never save bubble-relative coordinates as persistent targets |
| Scaling | Effective cast level separate from learned level; input parameters such as charge or chosen mode |
| Snapshots | Explicitly identify start-time snapshots versus release-time and per-target evaluation |
| Randomness | Stable rolls where declared; per-target rolls where declared; previews must not consume gameplay randomness |
| Invocation policy | Full action versus free effect/proc; validation, time, costs, failure, progression and target-lock policy |
| Lifetime | Owner, active instance, scheduled children, ending reason, cleanup state |
| Ancestry | Attack/cast ancestry and flags such as reflected, secondary, proc, environmental; prevent unintended recursive self-triggering |
| Outcome | Request accepted/cancelled/rejected, resolution summary, per-target results, spawned entities, actual payment and cooldown changes |

“Caster” is not always “source of damage,” “owner,” or “controller.” A familiar's attack, a reflected projectile and a thrown enemy show why those roles need to be distinct.

## Hook contract matrix

Use alpha/beta consistently within each hook and document the difference from current spell-effect EOCs. One defensible convention for *new* hooks is acting entity as alpha, other participant as beta, with named references for additional roles. Keep the existing target-alpha/caster-beta spell-effect EOC convention for compatibility unless an explicit new mode is selected.

| Hook family | Required context | Permitted outcome |
| --- | --- | --- |
| Eligibility/query | Actor, proposed ability/mode, proposed target(s), cost/cooldown snapshot | Read-only eligible/reason and preview; no payment, state changes or RNG consumption |
| Before start | Actor/source, selected target policy, requested costs/time | Reject or adjust approved request fields before commitment |
| Start/windup tick | Cast instance, progress, intended area, active resources | Observation and bounded adjustment; interrupt through an explicit operation |
| Before release | Final target/origin, cast parameters, payment reservation | Revalidate/veto with documented refund/cooldown policy |
| Released/failed/interrupted/cancelled | Cast, reason, actual payment, spawned/active children | Observe, schedule permitted follow-ups, clean up; no retroactive ambiguous success |
| Per-target before hit | Attack/cast, instigator, defender, geometry/hit policy | Dodge/intercept/redirect/cancel within a defined resolution stage |
| Damage before mitigation | Typed mutable packet, source/target, body part, ancestry | Approved packet transforms, barrier routing, defense resource reservation |
| Damage after mitigation/before commit | Remaining packet, blocked/absorbed totals, defender resources | Atomic absorption/overflow decisions; define precedence with armor and wounds |
| Damage committed | Actual HP/pain/wound/armor/resource outcomes | Observation and bounded procs; changes do not retroactively rewrite the committed result |
| Projectile move/impact/intercept | Active attack, segment, blocker, remaining lifetime/force | Stop/redirect/contest/resolve; provide resulting authoritative path |
| Forced move/impact/land | Instigator, moved entity, requested/actual path, collider, force, damage | Defined movement edits and results; preserve normal occupancy and map invariants |
| Application add/refresh/remove | Application/source/target, tags, magnitude, stacking, remaining time, reason | Allowed stacking/resistance rules and exactly-once lifecycle work |
| Area enter/stay/exit | Instance owner/anchor, member, boundary crossing and reason | Affect that member; membership changes remain safe when hooks move or kill actors |
| Summon created/commanded/ended | Owner/summon references, command, target, lifespan, reason | Initialize, accept/refuse command, or clean up only that bond |
| Resource/cooldown change | Resource/charge/group, previous/current values, cause | Observation; mutation only through the accounting operation to avoid recursive adjustment |

Do not implement all of these as undifferentiated EVENT EOCs. Events are useful observations; mutable transactions need validated outputs consumed by the caller. `effect_on_condition::activate` already has an `on_done` callback for reading the executed dialogue's copy ([source][activate]); that is a possible integration point for small explicit outputs, not proof that arbitrary context edits currently affect combat.

## Resource and ordering rules to settle before coding

- Validate and reserve costs before irreversible effects; define commit/refund on failure, cancel, interruption and partial resolution. A full cast, child effect and proc may intentionally have different policies.
- Resolve barriers once at the documented scope. A multicomponent hit should not independently promise the same last ten mana to every component.
- Give redirects, reflections and retaliations explicit ancestry and depth/trigger rules. Two reflectors must not bounce one hit without bound; suppress only the loop-producing rule, not all useful reactions.
- Define ordering by stage and explicit priority/tie-break, not accidental JSON load order. Two mods claiming the same exclusive replacement must produce a visible deterministic result.
- Use contribution-based cleanup. End an instance's own buffs, reservations, areas, visuals and scheduled children; do not indiscriminately erase every matching effect ID.
- Keep previews/AI queries pure. Looking at the menu must not consume resource charges, reroll a committed attack, create an entity, or change the live target.
- Re-resolve references after arbitrary callbacks, because an EOC may kill, move, transform or remove a participant. Do not continue using a stale pointer or container iterator.
- Define actor support explicitly. A missing resource on a monster should cause a reasoned unsupported/refused result or a declared adapter policy, not a hidden fake avatar fallback.

## Acceptance scenarios for later implementation

These are proposed tests and play scenarios; **none was executed for this audit**. Start with small deterministic fixtures and the real dispatch path. The purpose is to verify interactions, not mirror helper implementations.

| Scenario | Observable acceptance behavior | Main findings |
| --- | --- | --- |
| Custom attributed strike | Only the selected victim loses HP; source-sensitive hooks and kill credit identify the attacker; null environmental source remains valid | BUG-01, COMBAT-02 |
| Cancelled ultimate | Cancel targeting before commitment: no hit, payment, charge use, success callback or cooldown; after windup, the chosen cancellation policy is applied exactly once | BUG-02, CAST-02/03/04 |
| Shared action paths | A requested ability uses declared eligibility/cost/time policy from player, NPC, monster, item and EOC; a free proc remains free and cannot masquerade as a paid request | CAST-02, BUG-03 |
| Delayed mark | Two targets of the same monster type move and swap positions; the marked one is hit after save/load; a missing target returns a defined result; cancelling one mark leaves the other | STATE-01/02 |
| Mana barrier overflow | Defender has 10 capacity; a 25-point eligible hit absorbs exactly 10 and passes 15 to the declared next stage; mixed damage and multishot cannot spend the same reserve twice | COMBAT-01, STATE-04 |
| Two reflectors | One original hit generates only the explicitly allowed reflection chain, with retained attribution and no infinite recursion | COMBAT-01, CAST-01/04 |
| Superhero wall throw | Thrower launches victim through a breakable wall into another actor; both impacts have correct source/results; held targets, vehicles, landing traps and map shifts remain coherent | COMBAT-04 |
| Opponent's charge | NPC and monster use the dash against the player; they move along a legal path, stop at blockers and attack from the reached location | BUG-04, AUTHOR-01 |
| Two casters, same curse | A and B apply the same effect; dispelling A's instance leaves B's magnitude/lifetime intact; death/unload does not accidentally transfer ownership | STATE-03 |
| Sustained aura and beam | One caster sustains both; losing the beam's target ends only the beam; concentration loss follows the chosen policy; resource drain and cleanup survive save/load | STATE-02/04/05 |
| Familiar/stand | Two owners summon identical creatures; commands and dismissal affect only their own; summon damage/kill credit and resource routing follow the declared bond | STATE-01/06 |
| Beam contest | A charge is visible before release; an opponent may interrupt, raise an interceptor or contest a persistent beam; disabling animation does not alter the outcome | CAST-03, COMBAT-03, AUTHOR-04 |
| Overlapping domains | Two moving zones overlap, an actor enters/exits/teleports, and one owner dies; membership and cleanup occur once without erasing the surviving zone | COMBAT-06, STATE-05 |
| Ice wall restoration | A and B overlap walls; a third actor modifies a tile; expiry removes only the still-owned contribution and preserves unrelated changes | COMBAT-07 |
| Form coexistence | Temporary limbs, powers and gear coexist with independent effects; one form ends after save/load without deleting earned progression or the other's grants | COMBAT-09 |
| Speed/time policy | Haste affects declared actions; a time-domain power explicitly defines DoT, cooldown, projectile, weather and regeneration behavior instead of only subtracting creature moves | COMBAT-08 |

For each newly shared primitive, include supported combinations of avatar/NPC/monster; self, ally, hostile, neutral and absent source; empty ground versus occupied tile; target death/movement; save/load, dimension travel and unload; and two simultaneous instances. Do not multiply every content test by every combination when a shared primitive's coverage already establishes the invariant.

## Practical content track available now

A useful initial content pack does not have to wait for persistent beams or throwing cars. It can prove the direction with:

- A flying bruiser: conditional strength/durability/flight, current grapple techniques and controlled throws.
- A martial artist: explicit manual techniques, existing post-hit context, a modest EOC combo state and a clear reset rule.
- An elemental wizard: existing attacks and transforms, EOC-initialized summons, and timed buffs with known ownership limits.
- A shapeshifter: additive conditional effects/enchantments and temporary limbs, with an explicit gear policy.

Use these as demonstrations, not as evidence that unsupported interactions already work. In particular, avoid marketing post-hit healing as an exact damage shield, negative moves as a complete world time-stop, a line animation as a contestable beam, or vehicle teleportation as a physical car throw.

## Validation of this audit itself

The report was checked for a complete findings index, valid internal document links, source files and line anchors in the checked-out tree, and whitespace errors. This is documentation validation. It does not validate the proposed features or replace runtime characterization of the four BUG observations. Keep the pinned revision when using these findings for implementation; recheck current code before changing a path that may have evolved.

[activate]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/effect_on_condition.cpp#L458
