# Powers authoring audit

**Implementation follow-up:** The four concrete correctness observations below have been addressed in the working tree. See [the twelve-fix record](../correctness-fixes.md) for behavior, regression coverage, validation and remaining architectural scope. The original snapshot/evidence below is retained.

The first expansion slice now has an opt-in [shared ability lifecycle](06-ability-lifecycle-milestone.md): saved charged actions, common player/NPC/monster requests, explicit payment, interruption, spell/EOC context and read-only results. See the [authoring contract](../../doc/JSON/ABILITIES.md). The pinned audit below remains the original assessment; later-stage resource, damage and ownership findings remain open.

The next slice adds [damage transactions and defenses](07-damage-transactions-milestone.md): ordered typed edits, exact-capacity mana shields, bounded reflection and recorded packet results. See the [damage authoring contract](../../doc/JSON/DAMAGE_TRANSACTIONS.md). Resource ownership and the broader physical/AI expansion remain open.

The third milestone adds [resources, charge groups and owned sustained applications](08-owned-powers-milestone.md), including source-specific dispelling and shared reservation/defense accounting. See the [power ownership contract](../../doc/JSON/POWER_OWNERSHIP.md).

The fourth milestone adds [movement, summon bonds and AI intent](09-movement-summons-milestone.md): attributed native fling receipts, saved familiar orders, pure eligibility/estimate queries and opt-in selection. See the [actor authoring contract](../../doc/JSON/POWER_ACTORS.md).

The fifth milestone adds [persistent projectiles, beams, interception and area membership](10-projections-areas-milestone.md), including charged shapes, deterministic strength contests and source-specific aura defenses. See the [projection authoring contract](../../doc/JSON/POWER_PROJECTIONS.md).

**Question:** How far can current Nickclysm JSON and EOCs take superhero brawls, archwizards, anime fighters, and similar ability-heavy play? Which additions would remove the most important limits?

**Snapshot:** `b32c9baea25f62478228c6a91cbbf9f356e4e8a3` (`master`, inspected 2026-11-03). This is an audit and a proposed direction, not an implementation. No engine, gameplay JSON, or tests were changed for this report.

## Assessment

The engine can already support a substantial powers mod. Flight, large stat changes, energy blasts, transformations, regeneration, summons, magical equipment, grappling, aimed throws, and manually selected martial techniques have usable building blocks. Mind Over Matter, Magiclysm, Xedra Evolved, and Aftershock demonstrate many of them.

The main limitation is **composing those building blocks into an interaction that stays coherent across player, NPC, monster, item, and delayed execution**. The missing foundations are a cast/attack instance with explicit context and results, persistent references to participants, and ownership of the resources and effects that a power creates. Adding another named blast effect helps much less than supplying those foundations.

For example:

- A fire beam that damages a line is straightforward. A beam that takes time to travel, can be intercepted, contests another beam, and leaves its caster vulnerable needs substantial engine work.
- A tough character is straightforward. A barrier that spends exactly one mana per point absorbed, leaks the remainder of an oversized hit, and can protect another character needs a damage transaction hook.
- Throwing a zombie through a wall is substantially supported. Crediting the thrower for every secondary collision, reacting when the wall breaks, and throwing a real occupied car require additional support.
- A timed transformation is practical. Independent overlapping transformations, gear restoration, dispelling one caster's transformation, and copying another fighter's power loadout are much harder.

## Reading order

| Document | Purpose |
| --- | --- |
| [01-capabilities.md](01-capabilities.md) | What works now, actor differences, scenario feasibility, and an evidence map |
| [02-casting-and-state.md](02-casting-and-state.md) | Cast context, invocation, lifecycle, targeting, entity references, delayed work, resources, effects, and summons |
| [03-combat-and-world.md](03-combat-and-world.md) | Shields, attack payloads, beams, movement, props, zones, constructs, time, and transformations |
| [04-authoring-and-correctness.md](04-authoring-and-correctness.md) | AI, enchantments, UI, presentation, validation, and four current correctness observations |
| [05-expansion-plan.md](05-expansion-plan.md) | Recommended sequence, proposed hook contracts, and acceptance scenarios for future implementation |

## Findings index

Priority means value for the requested powers use case, not vulnerability severity. **P0** is a shared foundation; **P1** enables a major family of powers; **P2** improves coverage or authoring. Effort is an architectural estimate: S = local change, M = several cooperating systems, L = new persistent/runtime model. It is not a delivery estimate.

| ID | Finding | Priority | Effort |
| --- | --- | --- | --- |
| CAST-01 | No shared, stable cast context for formulas, rolls, and child effects | P0 | L |
| CAST-02 | No common invocation policy across ability entry points | P0 | L |
| CAST-03 | Cast lifecycle lacks generic telegraph and interrupt hooks | P1 | M–L |
| CAST-04 | Effects do not expose a structured resolution result | P0 | M–L |
| CAST-05 | Targeting is tile/creature oriented, without a reusable target-set contract | P1 | M–L |
| STATE-01 | Persistent entity references are incomplete across actor types | P0 | L |
| STATE-02 | Delayed EOCs do not preserve participants or expose cancellable jobs | P0 | L |
| STATE-03 | Effects lack independent power applications and source-aware dispelling | P1 | L |
| STATE-04 | Resources, cooldowns, and charges are not one reusable ability system | P1 | M–L |
| STATE-05 | Sustained powers need owned lifetimes beyond the existing channel model | P1 | L |
| STATE-06 | Summons lack a unified bond, command, and cleanup contract | P1 | M–L |
| COMBAT-01 | Reactive defense needs mutable attack/damage transactions | P0 | L |
| COMBAT-02 | Spell attacks need explicit payload and resolution policies | P1 | M–L |
| COMBAT-03 | Projectiles and beams have no persistent contestable presence | P1 | L |
| COMBAT-04 | Forced movement lacks full instigator, collision, and landing context | P1 | M–L |
| COMBAT-05 | Telekinetic props and vehicles lack a common physical action interface | P2 | L |
| COMBAT-06 | Area geometry and persistent membership are not reusable power objects | P1 | L |
| COMBAT-07 | Temporary constructs need ownership and selective restoration | P2 | L |
| COMBAT-08 | Speed and move changes do not define a selective time domain | P2 | L |
| COMBAT-09 | Transformations lack an owned form/loadout lifecycle | P2 | L |
| AUTHOR-01 | AI cannot generally understand the intent or outcome of scripted powers | P1 | M–L |
| AUTHOR-02 | Enchantments lack their source context and some power modifiers | P1 | M |
| AUTHOR-03 | Power controls and previews remain split across several interfaces | P2 | M |
| AUTHOR-04 | Presentation is not a reusable ability lifecycle | P2 | M–L |
| AUTHOR-05 | Validation needs semantic and execution-path coverage | P1 | M |
| BUG-01 | EOC direct damage attributes the hit to its victim | P1 | S–M |
| BUG-02 | A cancelled targeted EOC cast still runs `true_eocs` | P1 | S |
| BUG-03 | `NON_MAGICAL` short-circuits `can_cast` checks | P1 | S–M |
| BUG-04 | `dash` movement is implemented only for the avatar | P1 | M |

## Recommended first investment

1. Characterize and correct the four concrete correctness observations, with tests for their actual callers.
2. Introduce explicit cast requests, stable cast context, and structured results while preserving existing effect-only casts.
3. Add typed persistent participants and cancellable scheduled instances.
4. Add mutable damage transactions, then use them for barriers, absorption, counters, and reflection.
5. Build resource/cooldown and sustained-effect ownership on those foundations.
6. Add physical movement, projectiles, area instances, and AI semantics as separate consumers of the shared infrastructure.

Do not require a whole new magic engine before writing content. A first powers pack can use existing JSON for flight, strength, regeneration, elemental attacks, forms, and grapple techniques while the foundations are developed.

## Scope and confidence

This is source inspection of the checked-out revision, supplemented by reading documentation, shipped content, and relevant tests. No game was launched, no benchmark was run, and no test suite was executed. “Confirmed” describes a code path or representation inspected here; proposed capabilities and their gameplay consequences are design judgments. The four BUG observations are statically supported and still need targeted runtime regression tests before fixing.

The older [magic audit](https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/Audit/27-magic-enchantments.md) is useful background, but its original findings are not a current missing-feature list. The present tree already includes spell EOC context, six filtered enchantment spell modifiers, additional martial-art contexts, spawn callbacks, mutation activation guards, and JSON NPC goals. This audit explicitly credits those additions. General engine findings remain in the original audit; they are repeated here only where their effect on powers authoring matters.

All source links are pinned to the inspected commit. Proposed interface names in the expansion plan are illustrative and are not supported JSON syntax.
