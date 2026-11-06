# Authoring, AI, presentation, and correctness

**Implementation follow-up:** The four concrete correctness observations below have been addressed in the working tree. See [the twelve-fix record](../correctness-fixes.md) for behavior, regression coverage, validation and remaining architectural scope. The original snapshot/evidence below is retained.

The first five findings concern expansion. The final four are current code observations worth characterizing before building a powers pack. None was fixed or runtime-tested during this audit.

## AUTHOR-01 — Scripted powers lack shared AI semantics

**P1 · M–L · Confirmed evaluation boundary.**

**Evidence:** [`magic.cpp:829–840`][dps] returns zero DPS for effects other than `attack`. [`npc_attack.cpp:448–495`][npc-rating] rates targets using DPS, attitudes, distance and field value. [`mattack_actors.cpp:368–423`][monster-cast] uses the current monster target or self. [`doc/JSON/NPC_BEHAVIOR.md`][npc-goals] already supports scored JSON goals, conditions, categories and EOC lifecycles.

**Impact:** An NPC cannot infer that an arbitrary EOC grants a shield, rescues a friend, sets up a combo, teleports out of danger, or creates a beneficial domain. A power can execute for an NPC without being selected intelligently. Custom JSON goals can address specific cases, so “NPC abilities cannot be modded” would be incorrect.

**Current workaround:** Write explicit goals/conditions or monster attack conditions for each family of powers. Native attack spells retain their existing rating path.

**Proposed extension:** Optional AI intent and query hooks for eligible targets, expected harm/heal/control, protection, movement destination, setup/payoff, upkeep and risk. Queries must be side-effect-free and RNG-stable. Reuse the shared target/result contracts; allow scripted scorers without requiring authors to reimplement native pathfinding and combat estimates. Include deliberate telegraph response and power-aware friendly-fire choices.

## AUTHOR-02 — Enchantments need source context and additional modifiers

**P1 · M · Confirmed gap after existing improvements.**

**Evidence:** [`magic_enchantment.h:166–189`][spell-mods] already supports filtered DAMAGE, RANGE, AOE, DURATION, COST and CAST_TIME. [`character.cpp:5128–5192`][rebuild] knows which item/effect/bionic supplies an enchantment, but calls `force_add(ench, *this)`. [`magic_enchantment.cpp:1033–1064`][force-add] evaluates through wearer/creature as alpha and no source beta. [`magic_enchantment.cpp:1527–1535`][intermittent] also implements intermittent activation as a probability per turn.

**Impact:** A sword's own charge/kills variable, a particular shield application, or a source item's condition cannot be read generically inside its enchantment formula. Failure chance, accuracy, piercing, sound, DoT and cooldown modifiers have no equivalent entries in the six-value spell modifier enum. A chance-per-turn proc is not an exact recurring timer.

**Current workaround:** Read wearer variables or a named effect's intensity; use actor-side EOCs and supported spell modifiers. This is practical for a fixed content set.

**Proposed extension:** Supply source item/effect/trait/bionic and application/cast context when evaluating and caching. Add modifiers only where their stage and filters are well defined. Exact interval procs should use scheduled instances, with state and a selected clock, rather than silently changing probabilistic activations.

## AUTHOR-03 — Powers need common discoverability and preview

**P2 · M · Confirmed interface fragmentation; product design recommendation.**

**Evidence:** [`magic.cpp:3136–3427`][spell-ui] presents spell-specific data. Mutation, bionic, item and martial-art activation take separate routes; [`npctalk.cpp:7544–7948`][menus] provides selectors, text and checklists. The new manual technique menu is a useful reusable selection component. None of these supplies a common request/eligibility/result interface across all those powers.

**Impact:** An anime fighter's learned spells, mutation stances, item powers and melee finishers can be scattered across menus. Scripted cooldowns, charges, target requirements and sustained modes may be absent from native spell previews or require handwritten text that drifts from behavior.

**Current workaround:** EOC selector/text windows, mutation activation, spell hotkeys/favorites and custom widgets. Authors can build a good dedicated menu now.

**Proposed extension:** Optional ability registration/adapters over existing systems, exposing name, availability/reason, costs, cooldown/charges, targeting and preview, invocation, cancel/release and active state. A shared power palette can consume them later. Do not begin by replacing every existing menu or adding implementation details to the player-facing UI.

## AUTHOR-04 — Presentation lacks a reusable lifecycle

**P2 · M–L · Confirmed spell presentation boundary.**

**Evidence:** [`magic_spell_effect.cpp:907–925`][spell-drawing] renders the computed area as a custom explosion identified by spell ID and damage-type color. Spells also have sounds/messages; fields, creatures, terrain and tilesets can provide persistent visuals. [`projectile.h`][projectile] does not make these spell visuals a saved active attack instance.

**Impact:** Charge-up glow, telegraphed danger tiles, a tether to the caster, changing beam width during a contest, and a visually distinct dispel are assembled independently. Fields or dummy creatures used solely as visuals can accidentally acquire gameplay behavior.

**Current workaround:** Sound variants, messages, explosion sprites, temporary fields and custom tiles. Static attacks can already be distinctive.

**Proposed extension:** Presentation descriptors on lifecycle stages and active instances: telegraph, start, travel, hit, sustain and end. Render from authoritative simulation state and make duration/animation settings cosmetic; turning animations off must not change combat timing. Consider both tiles and text rendering, observer visibility, and information the caster should not know.

## AUTHOR-05 — Validation needs semantic and path coverage

**P1 · M · Confirmed remaining contract risk.**

**Evidence:** [`magic.cpp:565–747`][spell-validation] now checks many spell references and recursion; [`magic_type.cpp:108–144`][type-validation] validates formulas and EOCs. Those improvements should be retained. However, schema-valid fields can have different meanings by caller: [`magic.cpp:2514–2532`][extra-effects] does not apply `fake_spell.once_in`, while enchantment hits do; [`npctalk.cpp:6495–6563`][eoc-cast] reads that value without testing it. [`doc/JSON/MAGIC.md:278`][accuracy-doc] says accuracy currently does not work, although [`magic_spell_effect.cpp:993–1025`][accuracy-use] uses it.

**Impact:** Loading successfully is insufficient evidence that a complex power works for monsters, child casts, or proc execution. A doc/example mismatch can cause an author to invent unnecessary workarounds. Poorly bounded recursive attacks, enormous target scans and repeated geometry can also make otherwise valid brawls unusable.

**Current workaround:** Inspect the actual consumer, use the EOC trace and NPC decision debugger, and write focused content tests. Existing nesting and due-queue guards already protect some runaway behavior.

**Proposed extension:** Consumer-aware validation and a documented support matrix for fields, actors and execution modes. Exercise examples through actual dispatch paths, not only parsers. Add diagnostics for discarded options, unsupported talkers, invalid numeric bounds and impossible target rules. Budget active instances, area size and proc chains; collect performance measurements before choosing limits.

## BUG-01 — Direct EOC damage uses the victim as source and receiver

**P1 · S–M · Statically confirmed; runtime characterization still required.**

**Evidence:** [`npctalk.cpp:9219–9269`][eoc-damage] builds one damage instance and passes the damaged talker's own creature as `source`. [`talker_character.cpp:173–177`][character-damage] and [`talker_monster.cpp:303–307`][monster-damage] then call `source->deal_damage(source, ...)`, not their bound creature's `deal_damage`.

**Consequence:** The existing EOC damages the intended actor, but describes it as self-inflicted. A custom power using direct damage cannot supply the actual attacker for kill credit and source-sensitive reactions. **Changing only the EOC argument to the attacker would also change the damaged creature**, because the talker implementation incorrectly uses `source` as the receiver.

**Recommended characterization/fix:** Separate receiver from optional source in both layers, define a compatible explicit JSON source policy, and retain a valid null/environmental source. Test two distinct actors and both talker kinds: recipient HP falls, attacker HP does not, attribution points to attacker, and no-source damage still works. Also verify source-dependent on-damage EOCs and kill credit. Avoid routing this through raw HP setters.

## BUG-02 — Cancelling a targeted EOC cast follows the success branch

**P1 · S · Statically confirmed.**

**Evidence:** [`npctalk.cpp:6543–6563`][eoc-cancel] invokes `select_target`; it casts only when a target is returned, but runs `true_eocs` unconditionally after that block. `false_eocs` handles invalid spell/caster, not targeting cancellation.

**Consequence:** An author attaching payment, combo advancement or cooldown to `true_eocs` can apply it after the player cancels targeting. This is distinct from whether the spell hits anything after a valid cast.

**Recommended characterization/fix:** Return or branch explicitly on cancellation, with a documented reason. Preserve a distinction between invocation, resolution and hit result (CAST-04). Test cancelled versus valid selection through a target-selection seam; verify neither effects nor success costs run on cancel. Review callers that might have relied on the old branch behavior before changing it.

## BUG-03 — `NON_MAGICAL` bypasses part of `can_cast`

**P1 · S–M · Statically confirmed contract inconsistency.**

**Evidence:** [`magic.cpp:1287–1320`][can-cast] checks `CANNOT_ATTACK`, then returns true immediately for `NON_MAGICAL`, before magic-type flags, caster condition, verbal requirements, components or available energy. [`handle_action.cpp:1928–1980`][character-cast] independently rechecks some requirements before starting a Character activity. Effect-only callers bypass the normal eligibility route separately.

**Consequence:** “Non-magical” is attractive for superhero techniques that should ignore magic resistance, but the same flag also changes eligibility reporting. This does **not** prove every player cast gets free energy: the subsequent Character path still checks energy and caster conditions. It does mean UI/AI callers of `can_cast` can disagree with later execution and with expected flag semantics.

**Recommended characterization/fix:** Specify whether this flag only bypasses supernatural resistance or intentionally bypasses casting requirements, then split policies if necessary. Test `can_cast`, Character invocation, NPC use and effect-only execution independently with empty energy, failed caster conditions and missing components. Keep intended nonmagical attacks possible.

## BUG-04 — Native dash does not move NPC or monster casters

**P1 · M · Statically confirmed.**

**Evidence:** [`magic_spell_effect.cpp:2312–2362`][dash] walks the trajectory only inside `if(caster_you != nullptr)`, with `caster_you = caster.as_avatar()`. It still iterates the trajectory, restores moves and resolves the attack area for other casters. The shipped [`jetpack_spells.json`][jetpack] demonstrates a player-facing dash/rocket-kick use case.

**Consequence:** Giving an opponent or companion the same dash power does not give it the avatar's movement. A fighter can execute an effect from that spell without performing the intended charge. This matters for mobility attacks and does not establish that every non-avatar dash is selected by its normal AI.

**Recommended characterization/fix:** Define a shared deliberate-movement operation with actor-appropriate path, collision, trap, vehicle, occupancy and map-shift handling. Do not substitute unconditional `setpos`. Test avatar, NPC and monster with clear path, wall, occupied destination, trap, vehicle and failed/partial movement; make the attack origin depend on the reached location.

[dps]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic.cpp#L829
[npc-rating]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npc_attack.cpp#L448
[monster-cast]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/mattack_actors.cpp#L368
[npc-goals]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/NPC_BEHAVIOR.md
[spell-mods]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic_enchantment.h#L166
[rebuild]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/character.cpp#L5128
[force-add]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic_enchantment.cpp#L1033
[intermittent]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic_enchantment.cpp#L1527
[spell-ui]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic.cpp#L3136
[menus]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npctalk.cpp#L7544
[spell-drawing]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic_spell_effect.cpp#L907
[projectile]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/projectile.h
[spell-validation]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic.cpp#L565
[type-validation]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic_type.cpp#L108
[extra-effects]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic.cpp#L2514
[eoc-cast]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npctalk.cpp#L6495
[accuracy-doc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/MAGIC.md#L278
[accuracy-use]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic_spell_effect.cpp#L993
[eoc-damage]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npctalk.cpp#L9219
[character-damage]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/talker_character.cpp#L173
[monster-damage]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/talker_monster.cpp#L303
[eoc-cancel]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npctalk.cpp#L6543
[can-cast]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic.cpp#L1287
[character-cast]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/handle_action.cpp#L1928
[dash]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic_spell_effect.cpp#L2312
[jetpack]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/data/mods/aftershock_exoplanet/spells/jetpack_spells.json
