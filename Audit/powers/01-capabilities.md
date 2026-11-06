# Current capabilities and feasibility

Read [the overview](README.md) for scope and priority definitions. Source links below point to the audited revision, not a moving branch.

## Existing building blocks

| Capability | What a mod can use today | Boundary to keep in mind |
| --- | --- | --- |
| Energy attacks | `SPELL` attack, blast/cone/line shapes, armor penetration, typed damage, multiple hits, percentage/split damage, terrain bash scaling | Most are instantaneous area resolution; a visible bolt is not a persistent moving entity |
| Scaling | Numeric fields accept expressions; min/increment/max progression; custom XP and failure formulas through `magic_type` | Most numeric evaluations see caster only, not a full current cast or target |
| Composite powers | `extra_effects`, `EXTRA_EFFECTS_FIRST`, random child spells, EOC branches, bounded loops, delayed EOCs | Child casts and EOCs do not share a structured hit result or persistent cast identity |
| Power scripts | Spell EOC exposes effective level, damage, DoT, duration, AoE, range, damage type, ID, and a target location | Spell EOC uses **target as alpha, caster as beta**; ground may have no alpha; caller EOC context does not become a general cast context |
| Buffs and forms | Effects, conditional enchantments, mutations and their activation/deactivation EOCs, temporary added body parts | No independent source-owned instance for each overlapping application |
| Passive superpowers | Strength, speed, movement/attack costs, armor modifiers, resistances, regeneration, vision, weight, knockback/fall resistance | Values are cached and different actors support different mechanics; huge stats do not automatically grant object throwing or tactical competence |
| Flight and phasing | `LEVITATION`, `CLIMB_FLYING`, phase distance, teleport and related movement flags | Flight already exists; finer movement/impact contracts and actor parity are the expansion targets |
| Brawling | Manual technique menu, attack/counter EOCs, grapple hooks, grab/drag/throw, aimed knockback, `u_attack`, `u_choose_technique` | Existing contexts describe an exchange; they are not writable attack transactions |
| Reactive powers | Damage-type `onhit_eocs`/`ondamage_eocs`, martial art defensive hooks, enchantment hit effects, EVENT EOCs | Different timing and participants; no common cancellation/redirection or atomic absorb-and-pay operation |
| Summoning | Spell summons, EOC monster/NPC spawns, lifespan, summoner association, variables, `spawned_eocs` | EOC spawns have richer initialization than spell summons; ownership/commands and reliable later lookup remain fragmented |
| Conjured equipment | Ethereal items, spell item spawns, transformation of items, EOC inventory operations | Spell spawns may wear/wield items automatically; not a general summoned loadout policy |
| World alteration | Fields, emissions, traps, terrain/furniture transforms, mapgen updates, weather changes, dimensional travel, map reverts | No generic per-cast construct/zone instance owning all of its changes |
| NPC scripts | Editable JSON decision tree, scored JSON goals, errands, targeted activities, attitude/faction controls | These enable custom behaviors; they do not automatically give an EOC power a combat utility model |
| Menus and feedback | Spell list, mutation and item activation, manual techniques, EOC selector/checklist/text windows, messages/sounds, explosion rendering | No shared power registration/preview/resource/cooldown surface across those entry points |

Evidence: [spell schema and dispatch][spell-schema], [EOC spell context][spell-eoc], [enchantment values][enchant-values], [spell modifiers][spell-mods], [mutation hooks][mutation-hooks], [flight flags][flight], [martial-art context][ma-context], [spawn callbacks][spawn], [NPC behavior][npc], [EOC interfaces][eoc-ui].

## Representative powers

“Direct” means existing facilities can express a useful version; it does not assert that a balanced finished mod already exists. “Scripted” means possible with explicit EOC state and caveats. “Engine” means the defining interaction needs a new engine contract rather than another combination of current stats.

| Power fantasy | Feasible baseline now | What prevents the richer version | Findings |
| --- | --- | --- | --- |
| Super-strength bruiser | Direct: stats, durability, grapple throws, melee techniques, terrain damage | Secondary-collision attribution; lifting/throwing intact props and occupied cars | COMBAT-04/05, BUG-01 |
| Flying brick | Direct: flight flags, armor, speed, powerful melee and ranged spells | Unified charge/tackle/landing result, impact follow-ups, AI parity | COMBAT-04, AUTHOR-01, BUG-04 |
| Martial-arts anime fighter | Direct/scripted: technique choice, counters, grapple techniques, combo variables | A shared hit/result context, cancellable windups, attack-safe reactive hooks | CAST-03/04, COMBAT-01 |
| Elemental archwizard | Direct: beams as lines, blasts, summons, transformations, walls and weather | Ownership of overlapping constructs, source-aware dispelling and sustained domains | STATE-03/05, COMBAT-06/07 |
| Mana barrier | Direct approximation: armor or `FORCEFIELD`, with scripted drain | Exact absorption, partial overflow, direction/type rules, protect another actor | COMBAT-01, STATE-04 |
| Beam clash | Visual/scripted approximation: two line attacks and a chosen outcome | Beams do not persist as competing entities or expose collision/contest state | COMBAT-03 |
| Homing or returning projectile | Scripted approximation: delay and retarget/teleport an effect | Persistent identity, movement, interception, source and lost-target policy | STATE-01/02, COMBAT-03 |
| Damage reflection/absorption | Scripted retaliation and healing/resource adjustments | Editing the original packet and preventing reflected-hit recursion | COMBAT-01, CAST-04 |
| Telekinetic hero | Direct/scripted: push/pull, fling, remote item handling, teleport vehicles | A physical action with mass, trajectory, collision and ownership for each object kind | COMBAT-04/05 |
| Summoner/stand user | Direct: companion monster with custom attacks and variables | Bonded command target, independent dismissal, shared resources, save-safe owner lookup | STATE-01/06, AUTHOR-01 |
| Regenerating shapeshifter | Direct/scripted: healing, effect enchantments, traits, temporary limbs | Overlapping form contributions, equipment policy, source-aware reversion | STATE-03, COMBAT-09 |
| Speedster | Direct: speed and action cost changes, extra moves, rapid attacks | Selective time stop, projectile timing, duration/cooldown clock policies | COMBAT-08 |
| Illusionist | Direct approximation: invisibility, hallucination spawns, charm, NPC state changes | Observer-specific knowledge and decoy-belief rules are not a general spell contract | STATE-06, AUTHOR-01; deeper perception work would need a separate audit |
| Reality-warping arena | Scripted: terrain/mapgen/weather/dimension operations | Per-instance boundaries, exclusions, restoration, unload and overlapping ownership | COMBAT-06/07/08 |

## Execution paths are meaningfully different

| Entry point | Actual path and limitations |
| --- | --- |
| Character spellcasting | `Character::cast_spell` starts a serialized activity; finish handles targeting, failure, resource/components, effects, XP and finish event |
| EOC `u_cast_spell` / `npc_cast_spell` | Builds a temporary spell and calls `cast_all_effects`; does not start that activity or inherit its resource/failure policy |
| Monster spell attack | Uses the monster's attack target or self, charges moves immediately using a fake avatar's casting-time calculation, then resolves effects immediately |
| Enchantment hit effects | Checks proc chance and, for other-target casts, target/range; casts a temporary spell without normal activity costs |
| Enchantment intermittent activation | Uses a probabilistic per-turn activation; not an exact interval scheduler |
| Bionic spell activation | `bionic::activate_spell` explicitly limits this route to avatars; other NPC bionic support does not imply spell-activation parity |
| Child spell | Re-enters effect resolution with the parent caster and chosen location/self/random target; not a separately costed cast |

Evidence: [Character entry][character-cast], [activity finish][activity], [EOC cast][eoc-cast], [monster cast][monster-cast], [enchantment execution][enchant-cast], [child effects][children]. “Fake spell” means a temporary spell instance in these paths; it does not mean the resulting effect is unreal.

## Existing hooks that must not be reported as absent

- Spell EOCs already receive cast-value context. `tests/audit_spell_context_test.cpp` checks it. The remaining gap is making that context consistent, stable, and available to formulas and all lifecycle hooks.
- Filtered `spell_values` already modify damage, range, AoE, duration, cost, and cast time, including fake casts; `tests/audit_spell_enchant_test.cpp` covers that addition.
- Martial-art EOCs already identify attacker/defender and provide weapon, technique, hit, crit, body part, damage, kill, and grapple information where applicable.
- EOC spawns already have `spawned_eocs`; new NPC goals already have lifecycle hooks and persistence.
- Effects can already supply enchantments and body parts temporarily. Mutations already have activation conditions checked before cost.
- Fields already store an `effect_source`; monsters already have a summoner association. The gap is full per-cast/per-application ownership and accessible persistent references, not the complete absence of attribution.

## Source map

| Surface | Main files inspected |
| --- | --- |
| Definitions, value evaluation, casting and UI | `src/magic.h`, `src/magic.cpp`, `src/magic_type.*`, `src/handle_action.cpp`, `src/activity_actor.cpp` |
| Effect execution and geometry | `src/magic_spell_effect.cpp`, `src/magic_ter_fur_transform.cpp` |
| EOCs and math | `src/npctalk.cpp`, `src/effect_on_condition.*`, `src/math_parser_diag.cpp`, talker implementations |
| Combat and movement | `src/creature.cpp`, `src/damage.*`, `src/character_armor.cpp`, `src/character_health.cpp`, `src/melee.cpp`, `src/game.cpp`, `src/grapple.*`, `src/projectile.h` |
| Persistent state and modifiers | `src/effect.*`, `src/effect_source.h`, `src/field*`, `src/character.cpp`, `src/magic_enchantment.*`, `src/mutation.*`, `src/savegame_json.cpp` |
| AI and authoring | `src/npc_attack.cpp`, `src/mattack_actors.cpp`, `doc/JSON/NPC_BEHAVIOR.md`, `src/eoc_ui.cpp`, JSON documentation |
| Content examples | Mind Over Matter telekinesis/telepathy and concentration EOCs; Aftershock rocket-jump spells; Magiclysm spells; Xedra Evolved martial-arts spells |

[spell-schema]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic.h#L196
[spell-eoc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic_spell_effect.cpp#L2462
[enchant-values]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic_enchantment.h#L36
[spell-mods]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic_enchantment.h#L166
[mutation-hooks]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/mutation.h#L292
[flight]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/JSON_FLAGS.md#L385
[ma-context]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/MARTIALART_JSON.md#L96
[spawn]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npctalk.cpp#L9272
[npc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/NPC_BEHAVIOR.md#L1
[eoc-ui]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/Audit/plans/modding-queue.md#L1
[character-cast]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/handle_action.cpp#L1928
[activity]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/activity_actor.cpp#L2819
[eoc-cast]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npctalk.cpp#L6495
[monster-cast]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/mattack_actors.cpp#L368
[enchant-cast]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic_enchantment.cpp#L1527
[children]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/magic.cpp#L2459
