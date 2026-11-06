# Combat and world interactions

These are the differences between a powerful collection of attacks and a system that supports powers interacting with one another. See [the overview](README.md) for method and priorities.

## COMBAT-01 — Reactive defense needs mutable transactions

**P0 · L · Confirmed hook boundary.**

**Evidence:** [`creature.cpp:1591–1644`][deal-damage] copies and mitigates a damage instance, computes damage/pain, invokes damage-type EOCs, and applies damage. [`damage.cpp:388–451`][damage-hooks] passes actor talkers and numeric context to EOCs, not a writable damage instance, and does not read edited context back into the packet. [`character_armor.cpp:138–195`][armor] implements `FORCEFIELD` as a chance to nullify damage and separately hardcodes the defensive CBM. Martial-art defensive hooks exist, but their reported damage is not a writable packet either.

**Impact:** Exact mana shields, type conversion, damage caps, directional barriers, projectile catching, protection of a third party, and true reflection cannot be implemented reliably by editing a current attack. Healing after damage can be too late for death, pain, wounds, armor damage, or other triggers.

**Current workaround:** Armor and damage modifiers, immunity flags, `FORCEFIELD`, scripted retaliation, and melee counters. These suffice for many ordinary defenses.

**Proposed extension:** Explicit pre-hit/pre-mitigation/post-mitigation/committed-result stages. Permit bounded, typed edits: cancel, redirect, scale/replace a damage component, consume shield capacity, or reserve/pay a defense resource. Specify order, once-per-attack versus once-per-component behavior, and reflected/proc ancestry. An observation-only event should not acquire a hidden veto meaning.

## COMBAT-02 — Damage payloads and resolution policy are too implicit

**P1 · M–L · Confirmed schema and attribution gaps.**

**Evidence:** [`magic.cpp:2336–2341`][spell-damage-type] builds the spell's damage instance with one damage type; [`magic.cpp:2350–2365`][spell-projectile] builds a synthetic projectile from it. [`magic_spell_effect.cpp:958–1117`][damage-targets] combines touch tests, status application, mitigation, multishot/body-part policies, healing and DoT. Status is applied before the positive-damage projectile resolution. [`damage.h:200–216`][dot-data] stores DoT amount/type/body parts/duration without a source, and [`creature.cpp:3502–3526`][dot-processing] applies positive DoT with a null attacker.

**Impact:** A fire-and-impact punch, an armor-breaking beam, or “apply stun only when this strike penetrates” needs multiple loosely coupled effects. Damage-type hooks do not supply a complete attack profile. Native DoT cannot retain the individual caster attribution in its current record.

**Current workaround:** `extra_effects`, `u_deal_damage`, melee techniques, and manually managed damage effects. Composition works, but separate child hits are not necessarily one combined hit for armor, counters, resource shields, or kill credit.

**Proposed extension:** Optional typed damage-component arrays and an explicit resolution profile: accuracy/save policy, body-part rule, penetration, status trigger phase, friendly fire, DoT attribution/stacking, and shared hit ID. Return actual results through CAST-04. Preserve existing spell resolution by default; this is an opt-in richer payload, not a mandatory combat rebalance.

## COMBAT-03 — Beams and projectiles cannot persist or contest each other

**P1 · L · Confirmed execution boundary.**

**Evidence:** [`magic_spell_effect.cpp:1119–1138`][attack] calculates an area and damages it in the same call. [`magic.cpp:2350–2365`][spell-projectile] supplies a speed-10000 synthetic projectile to the hit routine at the selected target. [`projectile.h:23–73`][projectile] represents projectile/attack data, but this spell path does not create a saved world entity that advances on later turns. Animation is not simulation travel time.

**Impact:** Homing missiles with interception windows, returning weapons, deflection, portal-routed shots, beam clashes, cutting off a beam by disrupting its caster, and projectiles stopped by a newly raised shield need an object to interact with after firing.

**Current workaround:** Delay a spell/EOC and choose its eventual target; use a monster as a rough visible proxy. These can create a satisfying scripted sequence, but neither inherits generic projectile collision and contest semantics.

**Proposed extension:** An optional active attack instance with owner/cast, trajectory, speed/tick policy, collision mask, payload, lifetime, target reference, and interception hooks. Add sustained beams as anchored/path instances with drain and update policy. Beam contests should consume these instances and configurable contest rules; they should not be hardcoded as special relationships between named spells.

## COMBAT-04 — Forced movement loses important context

**P1 · M–L · Confirmed missing context; useful mechanics already exist.**

**Evidence:** [`game.cpp:9241–9450`][fling] handles obstacles, breakable walls, creature collisions, movement and landing. Its monster collision branch explicitly notes that the flinger should be passed rather than crediting the flung creature. Its travel direction advances x/y; later falling is not a general chosen 3D launch trajectory. [`game.h:176–185`][fling-options] exposes two optional behaviors, not an instigator or per-collision callback. [`magic_spell_effect.cpp:2288–2310`][spell-fling] uses this machinery; grappling adds richer throw targeting and events.

**Impact:** “Uppercut into the air, chase, then slam,” “gain energy for each wall broken,” and “the thrower gets the collision kill” need accurate participant, trajectory, collision, and landing outcomes. Existing throw events alone do not describe every intermediate impact.

**Current workaround:** Use grapple throws, `u_fling`, knockback, controlled techniques, and terrain attack effects. Throwing through a wall already works; that is not the missing feature.

**Proposed extension:** A movement request/result retaining instigator, moved entity, cause/cast, requested and actual path, remaining force, blockers, impact damage and landing. Add before-move/impact/land observations and narrowly defined edits. Start with attribution and results; arbitrary 3D motion and aerial follow-ups can follow. Preserve trap, vehicle, gravity and reality-bubble rules.

## COMBAT-05 — Physical actions are split by object kind

**P2 · L · Confirmed API fragmentation; expansion judgment.**

**Evidence:** [`npctalk.cpp:9989–10048`][teleport] can teleport creatures, items, vehicles and zones through different branches. [`magic_spell_effect.cpp:1326–1366,1540–1634`][push] provides movement for creatures/items/fields, while creature flinging and grapple dragging are separate. [`creature.cpp:1708–1775`][longpull] still distinguishes creature pulling from its object-latching TODO. Vehicle teleportation is real support, but it is not mass-dependent lifting, flight, and impact simulation.

**Impact:** A superhero can throw a zombie more naturally than a real car. A telekinetic ability must separately define moving a loose item, hauling furniture, moving an occupied vehicle, and striking with each object. Spawning a fake wreckage item does not preserve the original vehicle's occupants and contents.

**Current workaround:** Teleport or relocate objects, use vehicle/furniture tools, create representative items, and script damage at the destination. This can suit stylized powers if the simplification is deliberate.

**Proposed extension:** Shared physical-operation queries over typed targets: can lift/carry/push/throw, effective mass/anchoring, participants, route, collision, and result. Keep vehicle and furniture internals specialized behind that interface. Start with loose items and creatures; occupied vehicles and buildings are a separate, large extension.

## COMBAT-06 — Areas are not persistent membership objects

**P1 · L · Confirmed representation gap.**

**Evidence:** [`magic.h:114–121`][shapes] has blast/cone/line spell shapes. [`magic.cpp:1047–1066`][effect-area] returns a set of points for a spell effect. [`field_type.h:66–137`][field-types] supports field effects, intensity, light and movement properties; [`field.h:23–90`][field-entry] has per-tile state and attribution. None of those is an arbitrary owned area with a set of participants and power-specific enter/exit callbacks.

**Impact:** Moving ally auras, safe centers inside destructive rings, one-hit-per-crossing barriers, time zones, linked portals, and domains excluding their caster need reusable geometry and boundary membership. Repeatedly reapplying a short buff approximates an aura, but exit is only noticed by expiry and overlapping owners are hard to separate.

**Current workaround:** Fields, traps, recurring EOC scans, radius transforms, and location variables. Custom rings or chains can be scripted with map iteration; they do not automatically appear in a shared targeting preview or AI query.

**Proposed extension:** Reusable geometry queries (ring, capsule, segment, polygon, vertical volume, filtered sets) plus a separate optional area instance with anchor, owner, duration, inclusion/exclusion policy, enter/stay/exit triggers, and unload handling. Reuse the same geometry for preview, AI and resolution. An arbitrary geometry query does not itself need persistent state.

## COMBAT-07 — Constructs need selective restoration and ownership

**P2 · L · Confirmed lifecycle gap.**

**Evidence:** [`magic_spell_effect.cpp:1921–1931`][transform] applies terrain/furniture transformations. [`npctalk.cpp:5527–5560`][revert] schedules restoration from submap snapshots; mapgen updates and tile setters are additional routes. Terrain bash EOCs already exist and sometimes identify the basher. Those mechanisms do not create a common per-cast owner for a wall, its remaining HP, its tiles, and its eventual cleanup.

**Impact:** Two casters raising overlapping ice walls, a wall dispelled after someone builds on it, an arena destroyed before its timer ends, and a moving platform cannot safely use an unconditional old-state restore as their general lifecycle. Restoring too much can overwrite unrelated world changes.

**Current workaround:** Unique terrain/furniture and inverse transforms; mapgen/snapshot reverts for controlled areas. These are appropriate when restoring the whole captured region is actually intended.

**Proposed extension:** A construct record with owner/cast, typed footprint, health/damage hooks, expiry/dispel, and per-tile conflict-aware restoration policy. Restore a tile only when the construct still owns the expected contribution; define how permanent construction and destruction supersede it. Treat weather ownership similarly if several sustained domains can override it.

## COMBAT-08 — Speed and move changes are not selective time control

**P2 · L · Confirmed mechanism boundary; requested semantics require design.**

**Evidence:** [`magic_spell_effect.cpp:1973–1987`][mod-moves] adds/subtracts moves on creatures. Speed, movement and attack-cost enchantments already exist. Native [`creature.cpp:3507–3526`][dot-processing] decrements DoT by turns, while [`effect_on_condition.cpp:320–327`][due-eocs] checks scheduled EOCs against `calendar::turn`. Different subsystems therefore follow different mechanics when a creature simply loses its actions.

**Impact:** A speedster can act faster today. “Freeze everything except me and one ally, including bullets, fire and cooldowns” is a different contract. Negative moves can prevent actions without freezing passive effects, clocks, the world or all newly spawned entities.

**Current workaround:** Speed modifiers, `mod_moves`, recurring debuffs, and scripted exclusions. These are valid gameplay approximations of haste or a stun/time-stop fantasy.

**Proposed extension:** First make clock policy explicit for new powers: world time, actor action time, or instance time. A later time-domain system must define membership, passive effects, scheduled jobs, projectiles, fields, resource regeneration and nested domains. Do not promise general rewind: reconstructing prior inventory/world/AI state is a much larger feature beyond this audit.

## COMBAT-09 — Forms need an owned loadout lifecycle

**P2 · L · Confirmed composition gap.**

**Evidence:** [`mutation.h:292–315`][mutation-hooks] supplies learned spells, activation/processed/deactivation EOCs and enchantments. [`character.cpp:5128–5205`][enchant-rebuild] rebuilds enchantment contributions and temporary body parts. [`magic_spell_effect.cpp:1636–1690`][ethereal-items] creates ethereal gear and may automatically wear or wield it. These are enough for many transformations, but are not a general reversible form instance managing all contributions together.

**Impact:** A giant mode, armor suit, berserk form, stolen power set or temporary extra arms must decide what happens to wounds, limb HP, incompatible gear, learned powers and expended resources. Saving a few base stats and restoring them later can erase legitimate changes that happened during the form.

**Current workaround:** Conditional enchantments/effects and temporary traits are preferable to destructive base-stat edits. Use explicit activation/deactivation scripts with a documented gear policy.

**Proposed extension:** An optional form/loadout instance owning its granted abilities, traits, limbs, gear and modifiers; contribution-based removal; restoration/conflict policy; and inspection hooks. Preserve character identity and progression unless a distinct possession/body-swap feature explicitly requests otherwise. Require coexistence tests for two forms rather than assuming only one can ever be active.

[deal-damage]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/creature.cpp#L1591
[damage-hooks]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/damage.cpp#L388
[armor]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/character_armor.cpp#L138
[spell-damage-type]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic.cpp#L2336
[spell-projectile]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic.cpp#L2350
[damage-targets]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic_spell_effect.cpp#L958
[dot-data]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/damage.h#L200
[dot-processing]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/creature.cpp#L3507
[attack]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic_spell_effect.cpp#L1119
[projectile]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/projectile.h#L23
[fling]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/game.cpp#L9241
[fling-options]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/game.h#L176
[spell-fling]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic_spell_effect.cpp#L2288
[teleport]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npctalk.cpp#L9989
[push]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic_spell_effect.cpp#L1326
[longpull]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/creature.cpp#L1708
[shapes]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic.h#L114
[effect-area]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic.cpp#L1047
[field-types]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/field_type.h#L66
[field-entry]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/field.h#L23
[transform]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic_spell_effect.cpp#L1921
[revert]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npctalk.cpp#L5527
[mod-moves]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic_spell_effect.cpp#L1973
[due-eocs]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/effect_on_condition.cpp#L320
[mutation-hooks]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/mutation.h#L292
[enchant-rebuild]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/character.cpp#L5128
[ethereal-items]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic_spell_effect.cpp#L1636
