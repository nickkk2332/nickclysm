# 19 · Monster behavior & special attacks

> Engine audit – static code reading at commit `a1a646d6`; nothing was compiled or run. Each finding was checked against the code by a separate reviewer: **confirmed** = verified by reading the code; **plausible** = consistent with the code, but reachability or impact not proven. Line numbers refer to that commit.

**Scope:** `src/monster.cpp`, `src/monmove.cpp`, `src/monattack.cpp`, `src/mattack_actors.cpp`, `src/creature_tracker.cpp`, `src/behavior.cpp`, `src/behavior_oracle.cpp`, `src/behavior_strategy.cpp`, `src/monster_oracle.cpp`

## Summary

Monster AI works for vanilla content, but most of its tuning lives in C++. Attitudes, immunities, trigger strengths, senses, trails and bite infection are hardcoded tables keyed on traits, species and flags. EOCs can barely reach into monster combat: `u_attack` is a silent no-op on monsters, and no EOC can touch targets, cooldowns or factions. There are a few real correctness bugs: effect `speed_mod` is discarded for every monster, a use-after-free in `absorb_meat`, `MFA_HATE` factions chasing NPCs they cannot see, and AQUATIC monsters dying on any knockback. Performance trouble comes from per-step work that scales with the whole bubble. Target-less monsters scan every submap for moving vehicles, a broken early return makes cub checks O(N²), and reachability zones are re-flood-filled after any door or vehicle change.

| Lens | Critical | High | Medium | Low | Total |
|---|---|---|---|---|---|
| Missing JSON / EOC | 0 | 1 | 8 | 4 | 13 |
| Bugs & quirks | 0 | 3 | 7 | 6 | 16 |
| Performance | 0 | 2 | 5 | 9 | 16 |

**Fix first:**
1. **MONAI-B03** · absorb_meat calls `tname()` on an item it just destroyed – undefined behavior on a common path, and the fix is a one-line reorder.
2. **MONAI-B01** · Effect `speed_mod` is silently discarded for every monster – every speed-changing effect on monsters (staggered, frenzy, venom) does nothing; small, contained fix.
3. **MONAI-B02** · `MFA_HATE` factions target NPCs they cannot see – portal-storm and nether monsters ignore a visible player and track unseen NPCs through walls; one extra condition fixes it.
4. **MONAI-P01** · Target-less monsters scan all 2,541 bubble submaps for moving vehicles on every plan() step – constant per-turn cost for every wandering monster; reordering one test removes most of it.
5. **MONAI-P02** · anger_cub_threatened's early return never fires – a one-character guard fix removes O(N²) work during horde fights, and it also stops the related bug in MONAI-B08.

## 1. Missing JSON bindings & EOC features

### MONAI-J01 · EOCs cannot drive monster combat or AI: `u_attack` is a silent no-op, and cooldowns, targets and destination are unreachable
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/talker.h:936`, `src/talker_character.h:335`, `src/npctalk.cpp:5919`, `src/talker_monster.h:113`, `src/monster.cpp:2960`, `data/json/monster_special_attacks/void_spider_mechanics.json:299`

`u_attack`/`npc_attack` calls `talker::attack_target`, an empty virtual that only `talker_character` overrides. On a monster talker the effect does nothing and reports no error. Shipped content already relies on it: `EOC_VOID_SPIDER_CONTROL` runs `u_attack: void_spider_bite` with the void spider as `u`, so that bite never happens. Beyond that, an EOC can only change a monster's effects, pain, vars, anger, morale, friendliness, death and HP. None of the following is exposed, although the C++ already has all of it: the current attack target and destination (`attack_target()`, `get_dest()`), `set_dest`/`wander_to`, `aggro_character`, the monster's faction, and the per-monster special-attack map (`set_special`, `disable_special`, `reset_special`). No condition tests "this monster is mtype X" either; content uses `u_monsters_nearby('id', 'radius': 0)` as a workaround. This blocks pack-leader focus fire, boss phases that enable or disable attacks, taunt spells, and EOC-driven taming.

```cpp
// talker.h:936 – overridden only in talker_character.h:335
virtual void attack_target( Creature &, bool, const matec_id &, bool, int ) {}
// talker_monster.h:113-115: only set_anger, set_morale, set_friendly
```

**Fix:** Implement `talker_monster::attack_target` so that `u_attack` with `"allow_special"` triggers a named special or a basic melee attack. Add:
```json
{ "u_use_special_attack": "SHRIEK", "ignore_cooldown": true }
{ "u_set_special_enabled": "SHRIEK", "value": false }
{ "u_set_monster_target": "npc" }
{ "u_set_dest": { "u_val": "loc" }, "wander_turns": 20 }
{ "u_set_monster_faction": "bee" }
```
Also add the assignable math functions `u_special_cooldown('SHRIEK')` and `u_aggro_character()`, and the conditions `u_has_special_attack` and `u_is_mtype` (string or var).

### MONAI-J02 · EOC attack actor: range 1 skips the distance check, it costs no moves, `allow_no_target` drops an existing target, and the docs are wrong
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mattack_actors.cpp:290`, `src/mattack_actors.cpp:304`, `src/mattack_actors.cpp:325`, `src/mattack_actors.cpp:333`, `src/mattack_actors.cpp:334`, `src/mattack_actors.cpp:385`, `src/npctalk.cpp:5536`, `doc/JSON/MONSTER_SPECIAL_ATTACKS.md:292`, `doc/JSON/MONSTER_SPECIAL_ATTACKS.md:331`, `doc/JSON/MONSTER_SPECIAL_ATTACKS.md:337`

`attack_type: eoc` is the main JSON escape hatch for custom monster attacks, and it has five surprises. (1) The distance and vision check runs only when `range > 1`, so with the default `range: 1` an attack meant as melee fires at any visible target at any distance. `hekatonkheire_rend` uses `range: 1` and is held back only by its own EOC conditions. (2) There is no `move_cost`, and `call()` never calls `mod_moves`, so EOC attacks take no time and chain with other specials in the same turn. The EOC itself cannot charge moves either, because `turn_cost` only affects Characters. (3) `allow_no_target: true` forces the beta talker to null even when a target exists, so "use the target if there is one" cannot be written. (4) The spell actor behaves the same way: with `allow_no_target` it always aims at the caster's own tile, even with the player in range. (5) The EOC attack doc table is copied from leap: it describes `range` as "maximal range of the jump", gives `cooldown` the leap text, and says vision is always required. Items (3) and (4) match the documented behavior (doc:337, doc:290) and no shipped content hits them, so they are feature gaps, not crashes. That part was rated plausible on its own.

```cpp
Creature *target = ( allow_no_target ) ? nullptr : mon.attack_target();         // :304
if( range > 1 && !allow_no_target ) { if( !mon.sees( here, *target ) || ... ) } // :325
const tripoint_bub_ms target = ( spell_data.self || allow_no_target ) ?
                               mon.pos_bub() : mon.attack_target()->pos_bub(); // :385
```

**Fix:** Always enforce `rl_dist <= range`, with `"range": -1` (or an omitted range) meaning unlimited. Add `"move_cost"`, defaulting to 0 so current content keeps working, and document it. Add `"target_optional": true`, which passes the target as beta (or aims the spell at it) when one exists and otherwise falls back to self or a null beta. Add `"no_adjacent"` to match melee_actor. Make `turn_cost` apply to any Creature through `Creature::mod_moves`. Rewrite the EOC attack table in MONSTER_SPECIAL_ATTACKS.md.

### MONAI-J03 · Attack actors give EOCs different talkers: no target in leap/gun conditions, cooldowns see the avatar, thin melee EOC context
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mattack_actors.cpp:154`, `src/mattack_actors.cpp:1342`, `src/mattack_actors.cpp:1139`, `src/monster.cpp:2949`, `src/monster.cpp:271`

The leap and gun actors evaluate `condition` with a null beta talker, before any target is chosen. Their conditions therefore cannot refer to the target ("only fire at targets in vehicles", "only leap at weaker targets"). Neither actor has an `eoc` hook. `melee_actor` runs its EOCs only after a successful, undodged hit, and passes a single context var, `damage`: there is no on-miss EOC, no hit bodypart and no per-type damage. Cooldowns are evaluated with the avatar as beta at spawn and on every `reset_special`, even after an attack on an NPC or another monster, so cooldown math that uses `n_`/`npc_` reads the player's stats. The docs do say that `u` is the monster for leap/gun conditions, and the `eoc` actor and the `character_takes_damage` event cover some of this, but on-miss and per-type data are still missing.

```cpp
dialogue d( get_talker_for( &z ), nullptr );                                  // :154 leap, :1342 gun
dialogue d( get_talker_for( this ), get_talker_for( get_avatar() ) );         // monster.cpp:2949
```

**Fix:** Pass `z.attack_target()` (or the gun's chosen target) as beta in leap and gun conditions. Add `"eoc"` to the gun actor (run per shot, with context var `hit`) and to the leap actor (run on landing). Add `"eoc_on_miss"` and the context vars `bodypart`, `damage_<type>` and `dodged` to melee_actor. Evaluate cooldowns with `dialogue(monster, attack_target or nullptr)`.

### MONAI-J04 · Monster hearing, smell and footsteps are hardcoded flags, and monsters ignore the HEARING_MULT/FOOTSTEP_NOISE enchantments
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monster.cpp:4111`, `src/monster.cpp:4151`, `src/monmove.cpp:1611`, `src/monmove.cpp:1677`, `src/monster.cpp:1533`

Vision is numeric and honors the VISION_RANGE enchantment. The other senses do not. Hearing is HEARS plus a binary GOODHEARING flag (double effective volume, ×6 wander time), with fixed error bands and a fixed `normal_roll(30,5)` provocation roll. Smell is a binary KEENNOSE flag that switches the thresholds from 10/200 to 1/400. Footstep volume comes from a size table plus LOUDMOVES/QUIETMOVES/SILENTMOVES. `HEARING_MULT` and `FOOTSTEP_NOISE` are read only for Characters. MAGIC.md documents that only a subset of enchantments apply to monsters, so this is a documented gap rather than a bug. A related small bug: a small QUIETMOVES monster gets `6/3 - 3 = -1` footstep volume, which the `volume == 0` guard does not catch.

```cpp
const int volume = goodhearing ? 2 * vol - dist : vol - dist;   // monster.cpp:4119
int bestsmell = 10; int smell_threshold = 200;
if( has_flag( mon_flag_KEENNOSE ) ) { bestsmell = 1; smell_threshold = 400; }  // monmove.cpp:1677
```

**Fix:** Add mtype fields `"hearing_mult": 1.0` (GOODHEARING maps to 2.0), `"scent_sensitivity": { "min": 10, "max": 200 }` (KEENNOSE maps to 1/400) and `"footstep_volume"` (default derived from size). Apply `calculate_by_enchantment` with HEARING_MULT in `monster::hear_sound` and with FOOTSTEP_NOISE in `monster::footsteps`. Keep the flags as presets, and clamp footstep volume at 0.

### MONAI-J05 · Monster lifecycle has almost no EOC hooks: only death has an EOC field, and `monster_takes_damage` carries no monster id
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/mtype.h:299`, `src/event.h:506`, `src/monster.cpp:2517`, `src/monster.cpp:466`, `src/monster.cpp:569`, `src/monster.cpp:2423`, `src/monster.cpp:4038`, `src/monstergenerator.cpp:1051`

The only per-type monster EOC field is `death_function.eoc`. The `monster_takes_damage` event has only `{damage, dies}`, although it is sent with the monster as alpha talker, so an EOC can at least filter by flag or species. These moments cannot trigger an EOC: evolution (`try_upgrade`), reproduction (`try_reproduce`), spawning or loading, use of a special attack, target acquisition or the start of fleeing, being hit (`special_when_hit` accepts only the hardcoded `mdefense` functions), and the default melee hit. The default melee hit applies hardcoded VENOM/BADVENOM/PARALYZEVENOM poison durations and CLUMSY_ATTACKS odds. Melee attack *actors* already support an on-hit `eoc`, and the death EOC fires with any killer as alpha, so the gap is medium, not high.

```cpp
cata::event::make<event_type::monster_takes_damage>( dam, hp < 1 );   // monster.cpp:2517
if( stab_cut > 0 && has_flag( mon_flag_VENOM ) ) { ... add_effect( effect_poison, 3_minutes ); }  // :2423
```

**Fix:** Add mtype fields `"upgrade_eoc"`, `"birth_eoc"`, `"spawn_eoc"`, `"melee_hit_eoc"` and `"when_hit_eoc"`, with the monster as alpha and the other party as beta, plus context vars `damage`, `damage_type` and `bodypart`. Let `special_when_hit` accept `{ "eoc": "ID", "chance": 50 }`. Add the events `monster_upgrades {old_type, new_type}`, `monster_uses_special {monster_id, attack}` and `monster_kills_creature`, and add `monster_id` to `monster_takes_damage`. Migrate VENOM/BADVENOM/PARALYZEVENOM to `attack_effs` and deprecate the flags.

### MONAI-J06 · `monster::attitude` hardcodes trait, faction and species relations that mutation JSON already models
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/monster.cpp:1759`, `src/monster.cpp:1815`, `src/monster.cpp:1827`, `src/monster.cpp:1842`, `src/monster.cpp:1847`, `src/monster.cpp:1853`, `src/monster.cpp:1883`

Mutations already have generic JSON relations (`anger_relations`, `ignored_by`), which `attitude()` reads at 1883-1896. Next to them, the function hardcodes several rules:
- BEE makes the character a friend of the `bee` faction, and FLOWERS gives -10 anger.
- THRESH_MYCUS/MYCUS_FRIEND make the character a friend of the FUNGUS species.
- PHEROMONE_MAMMAL/AMPHIBIAN give -20 anger by species.
- PHEROMONE_INSECT gives -20 anger for four hardcoded factions.
- TERRIFYING gives -10 morale.
- ANIMALDISCORD/ANIMALEMPATH(2) apply fixed anger and morale changes to ANIMAL monsters.
- The Portal Storms faction override is hardcoded.

The JSON relations are species-only and anger-only: they have no faction target, no morale effect and no FRIEND outcome. Modders therefore cannot add a reptile pheromone or have their own insect faction respond to the existing one. Quirk: any `anger_relation`, even a negative (calming) one, sets `target_has_anger_relation`, which bypasses the `!aggro_character` ignore. A calming relation can therefore make an otherwise-ignoring monster evaluate the character.

```cpp
if( faction == monfaction_bee ) { if( u->has_trait( trait_BEE ) ) { return MATT_FRIEND; } ... }  // :1815
if( ( faction == monfaction_acid_ant || faction == monfaction_ant || faction == monfaction_bee ||
      faction == monfaction_wasp ) && effective_anger >= 10 && u->has_trait( trait_PHEROMONE_INSECT ) )
```

**Fix:** Extend the mutation (and character-flag) JSON:
```json
"monster_relations": [ { "species": "MAMMAL", "anger": -20, "morale": 0, "min_anger": 10 },
                       { "faction": "bee", "attitude": "FRIEND" },
                       { "monster_flag": "ANIMAL", "anger": -10, "morale": 55 } ]
```
Move the BEE, MYCUS, PHEROMONE_*, TERRIFYING and ANIMALEMPATH/DISCORD rules into data/json/mutations and delete the C++ blocks. Replace the portal-storm override with a monfaction field such as `"ignores_npcs_if_option"`.

### MONAI-J07 · Effect and damage immunities are C++ tables keyed on species, materials and bodytype strings
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/monster.cpp:2054`, `src/monster.cpp:2066`, `src/monster.cpp:2074`, `src/monster.cpp:2091`, `src/monster.cpp:2127`, `src/monster.cpp:2131`, `src/monster.cpp:2146`, `src/monster.cpp:2578`

`is_immune_effect` hardcodes several immunities:
- Venom requires flesh and skips the NETHER, MIGO and LEECH_PLANT species.
- Poison-family effects are blocked for ZOMBIE, NETHER, MIGO, LEECH_PLANT and non-flesh monsters.
- tpollen is blocked for PLANT.
- `downed` is resisted 75% of the time by the bodytype strings "insect", "flying insect", "spider" and "crab", and fully by "snake", "blob" and "fish".

Each of these branches returns *before* the generic `effect->immune_flags` loop at 2100, so effect-side opt-in never works for these effects. For example, `venom_player1/2` declare `immune_flags: [VENOM_IMMUNE]`, and monsters never consult it. `is_immune_damage` has a FIXME: heat immunity if made of steel or stone, and electric immunity only if the special_when_hit is ZAPBACK. Bleeding becomes `dripping_mechanical_fluid` for ROBOT, and escaping bonds is keyed on species and bodytype too. A mod's new species or bodytype gets none of this and cannot opt out (a poison-vulnerable zombie, say). Also, the `downed` immunity is a random roll inside an `is_immune` query that several places call. Damage immunity is otherwise already JSON-driven through damage_type `immune_flags.monster`; only the FIXME rules need moving.

```cpp
return type->in_species( species_ZOMBIE ) || type->in_species( species_NETHER ) ||
       type->in_species( species_MIGO ) || !made_of_any( Creature::cmat_flesh ) || ...;  // :2078
if( type->bodytype == "insect" || ... ) { return x_in_y( 3, 4 ); }                  // :2092
if( dt == damage_electric && type->sp_defense == &mdefense::zapback ) { return true; } // :2131
```

**Fix:** Add `"effect_immunities": [ "poison", { "id": "downed", "chance": 75 } ]` and `"damage_immunities"` to both species and monster JSON, with the monster value overriding the species (supporting extend/delete). Add `"bleed_effect": "dripping_mechanical_fluid"` to species. Move the current tables into data/json/species.json and the bodytype definitions. Run the `immune_flags` check before any special case.

### MONAI-J08 · Anger, fear and placate triggers are a closed enum with hardcoded strengths, radii and odds; the docs drift
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/monmove.cpp:443`, `src/monmove.cpp:584`, `src/monster.cpp:1936`, `src/monster.cpp:1944`, `src/monster.cpp:4059`, `src/monster.cpp:3345`, `src/monmove.cpp:869`

JSON can only choose which triggers a monster has. Every magnitude is fixed in C++:

| Trigger | Hardcoded behavior |
|---|---|
| HOSTILE_CLOSE | +5 within 5 tiles, only while anger <= 30 |
| HOSTILE_SEEN | rng(0,2), up to 20 |
| MATING_SEASON | +3 |
| PLAYER_NEAR_BABY | +8 within 3 tiles |
| HOSTILE_WEAK | applies below 70% HP |
| FRIEND_ATTACKED, FRIEND_DIED | ±15 |
| FIRE | 5 × intensity within radius 3 |
| BRIGHT_LIGHT | +10 at 75% of daylight |
| STALK | fires 1 in 5 |

There is no condition- or EOC-driven trigger. An `eoc` special attack can write `u_val('anger')`, but only with a target and at the cost of a special slot. Doc drift: BRIGHT_LIGHT is used by vanilla and Magiclysm but missing from JSON_FLAGS.md, and STALK is documented as "aggression > 5" while the code checks `anger > 0`.

```cpp
process_trigger( mon_trigger::STALK, [this]() { return anger > 0 && one_in( 5 ) ? 1 : 0; } );  // :1938
static const int dim_light = round( .75 * default_daylight_level() );                      // :1944
```

**Fix:** Accept objects in trigger arrays, and add a condition-driven trigger evaluated in `plan()` with the candidate target as beta:
```json
"anger_triggers": [ "HURT", { "id": "PLAYER_CLOSE", "amount": 8, "range": 7, "cap": 50 },
                    { "id": "CONDITION", "condition": { ... }, "amount": { "math": [ "..." ] } } ]
```
Document BRIGHT_LIGHT and correct STALK. (The `anger_cub_threatened` bugs found while tracing this are in MONAI-B08.)

### MONAI-J09 · Monster behavior trees are half-wired: no condition predicate, no scores, and goals can only fire special attacks
**Severity:** Medium · **Effort:** Large · **Verified:** confirmed
**Where:** `src/monmove.cpp:996`, `src/monmove.cpp:1005`, `src/behavior_oracle.cpp:66`, `src/behavior_oracle.cpp:74`, `src/behavior.cpp:178`, `src/monstergenerator.cpp:343`, `data/json/monsters/monster_goals.json:1`

A monster type gets a behavior tree only when one of its special-attack ids is also a behavior node id; there is no mtype `behavior` field. The goal string returned by `tick()` is used only as a key into `type->special_attacks`, so a tree cannot drive fleeing, wandering, following or an EOC. There are only five monster predicates, and none accepts a dialogue condition or math. No monster score functions are registered, so a nested `utility` strategy always ties at 0. Vanilla ships only three monster nodes (SPLIT, ABSORB_ITEMS, EAT_CROP). The tree path also skips the pacified and CANNOT_ATTACK gates; see MONAI-B12.

```cpp
std::string action = goals.tick( &oracle );
const auto attack = type->special_attacks.find( action );                  // monmove.cpp:1005
if( string_id<behavior::node_t>( attack.first ).is_valid() ) { type.add_goal( attack.first ); }  // monstergenerator.cpp:347
```

**Fix:** Add a generic predicate `{ "predicate": "condition", "condition": { "math": [ "u_hp() < u_hp_max() / 2" ] } }`, evaluated with the monster as alpha and its attack target as beta. Add a math score, `"score": "math", "score_argument": "..."`. Add an mtype field `"behavior": "ROOT_NODE"`. Use namespaced goals: `special:ID`, `eoc:EOC_ID`, `flee`, `wander`, `follow_leader`. Route tree-chosen attacks through the same gate as the special-attack loop.

### MONAI-J10 · Special attacks run in alphabetical id order with no priority field, and the docs misdescribe the loop
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mtype.h:446`, `src/mtype.h:415`, `src/monmove.cpp:1023`, `src/savegame_json.cpp:2447`, `doc/JSON/MONSTER_SPECIAL_ATTACKS.md:31`

`type->special_attacks` is a `std::map<std::string, ...>`, so attacks are tried in alphabetical order of id. The JSON order is recorded in `special_attacks_names`, but that list is only used for save compatibility. Giving one attack precedence means renaming ids, which breaks `copy-from`/`delete` in other mods. The loop has no `break`. Move-costing actors usually fail their `can_act()` check after the first success, but zero-move actors (eoc, polymorph_special, CALLBLOBS, JACKSON) chain freely in the same turn. The docs do mention alphabetical order, but they also imply that the first success ends the loop.

```cpp
for( const auto &sp_type : type->special_attacks ) { ...
    if( !sp_type.second->call( *this ) ) { continue; } ... reset_special( special_name ); }  // no break
```

**Fix:** Iterate in JSON order through `special_attacks_names`, or add an optional per-attack `"priority": int` (higher first, ties keep JSON order). Add an optional `"ends_turn": true` (or a monster-level `"one_special_per_turn"`). Correct the doc paragraph.

### MONAI-J11 · bite_actor's infection logic is hardcoded: fixed effects and durations, a hidden 5× virus chance, a TOXICFLESH special case
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mattack_actors.cpp:1208`, `src/mattack_actors.cpp:1217`, `src/mattack_actors.cpp:1230`, `src/mattack_actors.cpp:1238`, `src/mattack_actors.h:223`, `doc/JSON/MONSTER_SPECIAL_ATTACKS.md:225`

`bite_actor::on_damage` hardcodes the escalation chain: bite, then +40 min bite, then +25 min infected. The virus roll is `x_in_y(infection_chance, 20)`, five times the documented `infection_chance/100`, so a default bite (5) gives a 25% virus chance per damaging bite with deadly_bites. Which virus applies is decided by the DEADLY_VIRUS/VAMP_VIRUS flags, mapped to fixed effects with a hardcoded VAMPIRE exemption. A target with TOXICFLESH poisons the biter. The header comment describes an older formula. Mods can still apply arbitrary curses through melee_actor's inherited `effects` and `eoc`, which is why this is low.

```cpp
if( x_in_y( infection_chance, 20 ) ) { if( z.has_flag( mon_flag_DEADLY_VIRUS ) ... effect_zombie_virus ...  // :1230
if( target.has_trait( trait_TOXICFLESH ) ) { z.add_effect( effect_poison, 5_minutes ); ...             // :1238
```

**Fix:** Replace the logic with data on the attack, defaulted through species or monster flags:
```json
"infection": { "chance": 5, "effect": "bite", "escalate": [ { "if_has": "bite", "add": "40 m" }, { "if_has": "infected", "add": "25 m" } ] },
"virus": { "effect": "zombie_virus", "chance": 25, "immune_traits": [ "VAMPIRE" ] }
```
Move the TOXICFLESH response to the mutation side as an on-bitten EOC or flag. Document the real virus probability and fix the header comment.

### MONAI-J12 · Movement trails are hardcoded flags with fixed fields, chances and itype ids
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monmove.cpp:2395`, `src/monmove.cpp:2424`, `src/monmove.cpp:2436`, `src/monmove.cpp:2454`, `src/monmove.cpp:2467`, `src/monster.cpp:3052`, `src/monstergenerator.cpp:1049`

Six flags hold all per-step trail behavior:
- ACIDTRAIL leaves `fd_acid` at intensity 3 every step; SHORTACIDTRAIL does so 1 step in 5.
- SLUDGETRAIL leaves a diamond of sludge; SMALLSLUDGETRAIL does so 1 step in 2.
- DRIPS_NAPALM drops 50 napalm 1 step in 10, drawn from the hardcoded `pressurized_tank` ammo.
- DRIPS_GASOLINE drops gasoline 1 step in 5 with no ammo limit (the code has a TODO about it).

ELECTRIC_FIELD hardcodes gas-pump terrain ids. Field trails can be approximated with `emit_fields` on a 1-turn delay (which also fires while standing still), but per-step item or liquid drips with ammo consumption need C++.

```cpp
if( has_flag( mon_flag_ACIDTRAIL ) ) { here.add_field( pos, fd_acid, 3 ); }        // :2424
if( ammo[itype_pressurized_tank] > 0 ) { ... item( itype_napalm, calendar::turn, 50 ) ...  // :2457
```

**Fix:** Add an mtype field, or reuse `emit_id` with an `on_move` mode. Map the six flags to presets at load time, then deprecate them:
```json
"move_trails": [ { "field": "fd_acid", "intensity": 3, "chance": 100, "radius": 0 },
                 { "item": "napalm", "charges": 50, "chance": 10, "consumes_ammo": "pressurized_tank", "not_on": "SWIMMABLE" } ]
```

### MONAI-J13 · Special-attack docs drift, `max_ammo` is unused, and missing load checks let wrong gun or polymorph JSON load silently
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `doc/JSON/MONSTER_SPECIAL_ATTACKS.md:108`, `doc/JSON/MONSTER_SPECIAL_ATTACKS.md:256`, `doc/JSON/MONSTER_SPECIAL_ATTACKS.md:257`, `doc/JSON/MONSTER_SPECIAL_ATTACKS.md:273`, `doc/JSON/MONSTER_SPECIAL_ATTACKS.md:274`, `doc/JSON/MONSTER_SPECIAL_ATTACKS.md:302`, `src/mattack_actors.cpp:1272`, `src/mattack_actors.cpp:1285`, `src/mattack_actors.cpp:1529`, `src/mattack_actors.h:249`, `src/mattack_actors.h:258`, `src/monstergenerator.cpp:1586`, `src/flexbuffer_json-inl.h:871`

The docs list five hardcoded attacks that are not registered (DARKMAN, FEAR_PARALYZE, GENE_STING, HOWL, STARE) and omit seven that are (ASSIST, CHECK_UP, OPERATE, DEPART, DOOT, EAT_CLONE, SPEAKER). The gun table documents `range` and `range_no_burst`, which the loader never reads (using them gives a load-time "invalid field" error). It omits `ranges`, which all 46 vanilla gun attacks use, plus `require_sunlight` and `failure_msg`, and marks `ammo_type` as required though it is optional. `max_ammo` is read and never used, despite the doc promising a debug message. A gun attack with no `ranges` at all loads cleanly, because `get_array` returns an empty array, and then never fires. Leap's `min_range` is marked required but defaults to 1. polymorph_special is undocumented and its `mon_id` is not validated. `check_monster_definitions` only validates melee damage types. In the header, `fake_str`/`fake_per` default to 16/12, but the loader overwrites them with 8.

```cpp
for( JsonArray mode : obj.get_array( "ranges" ) ) {   // :1272, empty if absent
obj.read( "max_ammo", max_ammo );                      // :1285, never used again
int fake_str = 16;  // .h:249, but optional( obj, was_loaded, "fake_str", fake_str, 8 );
```

**Fix:** Add a virtual `mattack_actor::check(const mtype&)` called from `check_monster_definitions`. For guns, check that gun_type and ammo_type are valid, `ranges` is non-empty and the ammo is in starting_ammo. Also validate polymorph `mon_id`, EOC actor eoc ids and spell ids. Implement or remove `max_ammo`. Regenerate the hardcoded-attack list in the docs from the registry, fix the gun, leap and EOC tables, document polymorph_special and effects_require_organic, and align the header defaults.

## 2. Bugs, oversights & quirks

### MONAI-B01 · Effect `speed_mod` is silently discarded for every monster (overwritten by enchantment speed)
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monster.cpp:3535`, `src/monster.cpp:3603`, `src/monster.cpp:3614`, `src/monster.cpp:3625`, `src/creature.cpp:478`, `src/creature.cpp:3332`, `src/creature.cpp:2007`

Each turn `Creature::process_turn_no_moves` calls `reset_bonuses()` and then `process_effects()`. `monster::process_one_effect` adds the effect's SPEED modifier with `mod_speed_bonus()`. At its end, though, it rebuilds the enchantment cache and calls `set_speed_bonus(enchanted_base - base)`, which overwrites `speed_bonus`. The same happens on the `is_new` path from `Creature::add_effect`. monster.h has no `get_speed` override, so no effect `speed_mod` ever reaches a monster's moves. This affects 'staggered' (-20, "mainly for monsters"), 'frenzy' (+10 per intensity), venom_player2 (-50) and others. Hit, dodge and bash mods still work, which makes the bug easy to miss. The per-effect rebuild is also a performance issue (MONAI-P08).

```cpp
mod_speed_bonus( get_effect( "SPEED", reduced ) );            // :3535
enchantment_cache->clear();                                   // :3603
set_speed_bonus( calculate_by_enchantment( get_speed_base(), enchant_vals::mod::SPEED, true )
                 - get_speed_base() );                         // :3614, overwrites the line above
```

**Fix:** Move the enchantment-cache rebuild and the speed recalculation out of `process_one_effect` into `process_effects`, after the effect loop. Apply the result with `mod_speed_bonus(ench(base) - base)` instead of `set_speed_bonus`, and keep the -75% clamp after that.

### MONAI-B02 · `MFA_HATE` factions target NPCs they cannot see, even over an adjacent visible player
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monmove.cpp:630`, `src/monmove.cpp:642`, `src/monmove.cpp:389`, `src/monmove.cpp:846`, `src/monster.cpp:1759`, `data/json/monster_factions.json:448`, `data/json/npcs/factions.json:25`

In `monster::plan()`'s NPC loop, `faction_att == MFA_HATE` switches the target to that NPC without checking the rating. `rate_target` returns `FLT_MAX` for an NPC that is unseen or farther away, so the monster picks the last hated NPC anywhere in the bubble, sets `dist = FLT_MAX`, and paths toward it through walls. That replaces a visible avatar target. Affected factions: `attack_player_only`, `nether_hate_humans`, and the 19 portal-storm monsters in `nether_player_hate`, which hate faction `player` (every follower NPC). With Personal Portal Storms' `PORTAL_STORM_IGNORE_NPC`, `attitude()` returns FPASSIVE for the hated NPC, so `plan()` calls no `set_dest` at all and the monster stops chasing the player it can see.

```cpp
if( ( rating < mon_plan.dist && mon_plan.fleeing ) || ( faction_att == MFA_HATE ) || ... ) {
    mon_plan.target = &who; mon_plan.dist = rating;               // :641-647
// rate_target :389 – if( !sees( here, c ) ) { return FLT_MAX; }
```

**Fix:** Require `rating < FLT_MAX` (the NPC is seen) for the HATE branch. Treat HATE as a priority bonus (for example, compare `rating * 0.5` against `dist`) rather than an unconditional override. Skip NPCs that `attitude()` reports as FPASSIVE/IGNORE before changing the target.

### MONAI-B03 · `absorb_meat` calls `tname()` on an item it just destroyed, and prints an unconditional message every turn
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monattack.cpp:3269`, `src/monattack.cpp:3278`, `src/monattack.cpp:3284`, `src/monattack.cpp:3287`, `src/map.cpp:6873`

`current_item` is a reference into the tile's item stack. `map::use_amount`/`use_charges` erase the consumed item, and afterwards the message calls `current_item.tname()`. That is a use-after-free whenever the player sees the absorption (jabberwock, aftershock flesh golems, singularity blob). Separately, adjacent meat that is too big triggers a plain `add_msg` ("quivers hungrily") and `return false`. The false return means the cooldown is not reset, so the message prints every turn next to any large corpse, even off-screen. The early return also skips smaller meat on other tiles.

```cpp
here.use_amount( p, 0, current_item.type->get_id(), meat_absorbed );                 // :3284
add_msg( m_warning, _( "The %1$s absorbs the %2$s, growing larger." ), z->name(),
         current_item.tname() );                                                     // :3287, dangling
```

**Fix:** Store `const std::string name = current_item.tname();` before consuming the item. Use `add_msg_if_player_sees` for both messages. Replace `return false` on oversized items with `continue`.

### MONAI-B04 · polymorph_special loads `condition` but never evaluates it, so Xedra's wendi transforms unconditionally
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mattack_actors.cpp:1536`, `src/mattack_actors.cpp:1542`, `src/mattack_common.h:33`, `src/monmove.cpp:1041`, `src/monster.cpp:274`, `data/mods/Xedra_Evolved/monsters/cryptids.json:210`, `data/mods/Xedra_Evolved/monsters/cryptids.json:216`

`polymorph_special::load_internal` reads `condition` into the base class's `condition`/`has_condition`, but `call()` goes straight to `z.poly(mon_id)`. Every other actor checks the condition inside its own `call()`, and `monster::move` has no central gate. Xedra Evolved's `mon_civilian_wendi` ("stray soul") should become `mon_cryptid_wendi` only while grabbing someone. Instead it transforms the first time its cooldown reaches 0, which is 0-50 turns after spawn (the initial cooldown is `rng(0, cooldown)`), whatever the player does. The actor also skips `can_act()` and target checks, its `mon_id` is never validated, and it is undocumented. The vanilla user (`data/json/monsters/misc.json:257`) has no condition.

```cpp
if( jo.has_member( "condition" ) ) { read_condition( jo, "condition", condition, false ); has_condition = true; }  // :1536
bool polymorph_special::call( monster &z ) const { const int old_speed = ...; z.poly( mon_id ); ... return true; }   // :1542
```

**Fix:** In `call()`, take `Creature *t = z.attack_target()`, build `dialogue d(get_talker_for(z), t ? get_talker_for(t) : nullptr)`, and return false if `has_condition && !condition(d)`. Better: add a non-virtual `mattack_actor::try_call(monster&)` that checks `can_act` and `condition` for every actor, and have both `monster::move` and the behavior-tree path use it. Add `"allow_no_target"`, `"message"` and `"chance"`, validate `mon_id` (MONAI-J13), and document the actor.

### MONAI-B05 · New monsters act as if unloaded since `turn_zero`: up to 51 biosignature items dumped at once, special cooldowns zeroed
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monster.cpp:258`, `src/monster.cpp:259`, `src/monster.cpp:683`, `src/monster.cpp:691`, `src/monster.cpp:4257`, `src/monster.cpp:4352`, `src/savegame_json.cpp:2521`, `src/do_turn.cpp:375`, `src/map.cpp:10264`, `src/overmapbuffer.cpp:2177`

The constructor sets `biosig_timer = calendar::before_time_starts`, which engages the optional, so the lazy init in `try_biosignature` is dead code. The first `try_biosignature` call, at the daily tick or in `on_load`, then catches up from turn -1 until the 51-item cap. The default start is day 61, so a cow (feces every 1 day) entering the bubble drops 51 cow pies on its tile. Old saves without the key load as `time_point(-1)`, with the same result. Same root cause: `last_updated = turn_zero`, and map.cpp and overmapbuffer.cpp call `on_load()` on freshly built monsters, against the monster.h comment. With `dt` around 60 days, `on_load` zeroes every randomized special cooldown, so a whole spawned group fires its specials on its first turn. The dumped items are partly rotted on creation, and spawns at the bubble edge soften the cooldown effect, hence medium.

```cpp
last_updated = calendar::turn_zero; biosig_timer = calendar::before_time_starts;   // :258-259
if( !biosig_timer ) { biosig_timer.emplace( calendar::turn + *type->biosig_timer ); // :683, dead
biosig_timer = time_point( data.get_int( "biosig_timer", -1 ) );                   // savegame_json.cpp:2521
```

**Fix:** Leave `biosig_timer` as nullopt in the constructor, and on load when the key is missing. Set `last_updated = calendar::turn` in the constructor, or set it before calling `on_load` for fresh spawns, so that `dt` is about 0.

### MONAI-B06 · `monster::knock_back_to` kills every AQUATIC monster and can put a monster on the avatar's tile
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monmove.cpp:2780`, `src/monmove.cpp:2763`, `src/monmove.cpp:2798`, `src/monster.cpp:1402`, `src/creature_tracker.cpp:328`, `src/melee.cpp:1854`, `src/magic_spell_effect.cpp:933`

(a) `if( !die_if_drowning( to ) && has_flag( AQUATIC ) ) die()`: `can_drown()` is false for AQUATIC monsters, so `die_if_drowning()` always returns false. Every AQUATIC monster therefore dies on any knockback, even into deep water, from melee techniques, push spells or chain reactions. The code then still `setpos()`es the dead monster. (b) Only a monster or NPC at `to` is checked. `creature_at<npc>` deliberately skips the avatar, so a monster knocked toward the player is placed on the avatar's tile, stacking two creatures.

```cpp
if( !die_if_drowning( to ) && has_flag( mon_flag_AQUATIC ) ) { die( &here, nullptr ); ...  // :2780
if( npc *const p = creatures.creature_at<npc>( to ) ) { ...                                 // :2763
} else { setpos( here, to ); }                                                              // :2797
```

**Fix:** Kill only when `has_flag(AQUATIC) && !here.has_flag(TFLAG_SWIMMABLE, to)`, and return right after `die()`. Use `creature_at<Character>(to)` (or add an explicit avatar check) so the player gets the same bounce and damage handling as NPCs.

### MONAI-B07 · `refill_udders` dereferences `map::end()` or an erased iterator
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monster.cpp:648`, `src/monster.cpp:655`, `src/monster.cpp:659`, `src/monster.cpp:665`, `src/monstergenerator.cpp:1403`, `src/do_turn.cpp:371`, `src/monster.cpp:4253`, `data/mods/innawood/monsters/insect_spider.json:15`

`refill_udders` looks up `milk_raw`, then `milk`. On the legacy `milk` path it erases `current_milk` and then dereferences it at line 659. If neither key exists, which is true for any MILKABLE monster whose starting ammo is another liquid, it dereferences `end()`. The load check allows this case, and innawood's MILKABLE aphids, which produce `fresh_honeydew`, hit it. It runs daily per MILKABLE monster and on load. With libstdc++ the most likely outcome is a garbage comparison rather than a crash, but it is UB that a sanitizer would flag. The refill also writes to `ammo.begin()` rather than the milk entry.

```cpp
auto current_milk = ammo.find( itype_milk_raw );
if( current_milk == ammo.end() ) { current_milk = ammo.find( itype_milk );
    if( current_milk != ammo.end() ) { ammo[itype_milk_raw] = current_milk->second; ammo.erase( current_milk ); } }
if( current_milk->second == type->starting_ammo.begin()->second )   // :659
```

**Fix:** Key everything on `type->starting_ammo.begin()->first`. Convert the legacy `milk` entry, then find the entry again. Write the refill to `ammo[milk_type]`.

### MONAI-B08 · plan() anger helpers: dead cub guard corrupts target distance and sets aggro without the trigger; debug-mode null deref
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monmove.cpp:511`, `src/monmove.cpp:456`, `src/monmove.cpp:527`, `src/monmove.cpp:529`, `src/monmove.cpp:536`, `src/monmove.cpp:598`, `src/monmove.cpp:632`, `src/monmove.cpp:489`, `src/monmove.cpp:668`

`anger_cub_threatened` has three problems:
1. Its guard `angers_cub_threatened < 0` is never true, because the value is 0 or 8. Every hostile monster that sees the player therefore scans all monsters (the performance cost is MONAI-P02).
2. For any species with a baby type, it overwrites `mon_plan.dist` with the baby's `rate_target()` of the player. That is `FLT_MAX` whenever the baby can't see the player, which corrupts the rest of target selection: in the NPC loop, unseen NPCs then tie with the current target (`rating == mon_plan.dist`) and can steal it.
3. When any baby of that type is within 3 tiles, it sets `aggro_character = true` even without PLAYER_NEAR_BABY. That affects 63 of the 69 baby-producing types (deer, moose, boar, dogs, ...), which become character-aggressive.

Separately, `anger_hostile_seen` evaluates `target->as_npc()->name` when the target is a monster, which is reachable through the call at line 668. That is a null dereference, but only in debug mode with the monster filter on.

```cpp
if( mon_plan.angers_cub_threatened < 0 ) { return; }                                       // :511, never true
mon_plan.dist = tmp.rate_target( *mon_plan.target, mon_plan.dist, mon_plan.smart_planning ); // :527
if( mon_plan.dist <= 3 ) { anger += ...; aggro_character = true; }                         // :529-536
```

**Fix:** Use `if( angers_cub_threatened == 0 ) return;`. Compute the baby's distance in a local variable (`rl_dist` plus a sees check) without touching `mon_plan.dist`. Set `aggro_character` only when the trigger exists. Use `target->disp_name()` in the debug message.

### MONAI-B09 · ABSORB_ITEMS caches `absorb_ml_per_hp` in a function-local static; the oracle and the actor use different material rules
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monattack.cpp:384`, `src/monattack.cpp:405`, `src/monattack.cpp:443`, `src/monstergenerator.cpp:979`, `src/monster_oracle.cpp:47`, `src/monmove.cpp:999`

`ml_per_hp` is a function-local `static const` initialized from `z->type->absorb_ml_per_hp`, so it is set once per process. The first monster type to absorb something fixes the value for every type for the rest of the session, across save reloads and mod changes. The documented per-monster field (MONSTERS.md:173) is therefore ignored for all other types. Vanilla is unaffected only because all three users set 250. The field is loaded without validation, so a 0 would divide by zero at line 443. Separately, with only `no_absorb_material` set, the `items_available` predicate treats an item as available if *any* of its materials is allowed, while `absorb_items` rejects an item if *any* material is blacklisted. On a mixed-material item this wastes a call each turn but gets nothing stuck. Doc drift: MONSTER_SPECIAL_ATTACKS.md:93 names `absorb_materials`/`no_absorb_materials`, while the loader reads `absorb_material`/`no_absorb_material`.

```cpp
static const units::quantity<int, units::volume_in_milliliter_tag> ml_per_hp =
    units::from_milliliter( z->type->absorb_ml_per_hp );                  // :384
z->set_hp( z->get_hp() + std::max( volume_in_ml / ml_per_hp.value(), 1 ) ); // :443
```

**Fix:** Drop the `static`: `const int ml_per_hp = std::max( 1, z->type->absorb_ml_per_hp );`, and validate `absorb_ml_per_hp > 0` in mtype `check()`. Move the material filter into one helper shared by `monster_oracle_t::items_available` and `mattack::absorb_items`. Fix the doc field names.

### MONAI-B10 · PUSH_VEH monsters shove vehicles while still choosing a square, possibly several times per move
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monmove.cpp:1421`, `src/monmove.cpp:1458`, `src/monmove.cpp:2883`, `src/monmove.cpp:2938`, `src/monmove.cpp:2945`

In `monster::move()`, `shove_vehicle()` runs for every candidate square considered, before `can_move_to` and before a square is chosen. It therefore fires even when the monster then attacks or moves elsewhere, and even while pacified (the attack, bash and push actions check pacified; this does not). Each call deducts moves, sets `skidding` and `velocity`, moves the vehicle and smashes it. A car filling 2-3 candidate squares gets shoved 2-3 times in one `move()`, or more for STUMBLES monsters. PUSH_VEH is common: 18 spider types, many zombies and mutants.

```cpp
for( tripoint_bub_ms &candidate : options ) { ...
    // Try to shove vehicle out of the way
    shove_vehicle( destination, tripoint_bub_ms( candidate ) );   // :1421, before can_move_to
```

**Fix:** Shove once, after `next_step` is final, only when that tile has `obstacle_at_part()` and the monster isn't pacified. Skip the shove when the destination itself is the target.

### MONAI-B11 · Hardcoded attacks assume sibling specials exist (`.at()` crash); an empty nursebot still operates when unseen
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monattack.cpp:2521`, `src/monattack.cpp:3925`, `src/monattack.cpp:3939`, `src/monattack.cpp:2449`, `src/bionics.cpp:2337`

`nurse_operate` and `flesh_tendril` call `special_attacks.at("grab")` and `.at("ranged_pull")` without any load-time validation. A mod that gives OPERATE or FLESH_TENDRIL to a monster without those companions gets an uncaught `std::out_of_range`, which crashes the game. The shipped users define both. In `nurse_operate` the no-anesthetic bail-out is gated on `u_see`, so an empty nursebot nobody is watching still grabs and drags its patient. `uninstall_bionic` then refuses, so the drag is pointless.

```cpp
z->type->special_attacks.at( "grab" )->call( *z );          // :2521
if( z->ammo[itype_anesthetic] == 0 && u_see ) { ... return false; }   // :2449
```

**Fix:** Use `find()` with a debugmsg fallback, and check in `check_monster_definitions` that these attacks have their companion specials. Make the anesthetic check independent of `u_see`; only the message should depend on it.

### MONAI-B12 · Behavior-tree specials skip the pacified/CANNOT_ATTACK gates and fire twice when the cooldown is 0
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monmove.cpp:999`, `src/monmove.cpp:1007`, `src/monmove.cpp:1022`, `src/monmove.cpp:1040`, `src/monster.cpp:2947`, `src/monstergenerator.cpp:349`, `data/json/monsters/singularities.json:1440`, `data/json/monsters/nether.json:1447`, `data/mods/Mythos-Creatures/monsters/mythos.json:371`

A special picked by the behavior tree (SPLIT, ABSORB_ITEMS, EAT_CROP) is called directly, without the `!pacified` and CANNOT_ATTACK checks the main loop applies, and `is_dead()` is not checked afterwards. `reset_special()` restores the JSON cooldown, so with cooldown 0 the main loop runs the same attack again in the same `move()`. The singularity blob, for example, absorbs twice per call. The migration to behavior trees is half done; see the TODO in `build_behavior_tree` and MONAI-J09. Impact is small, because cooldown-0 specials already run every turn through the main loop.

```cpp
if( attack != type->special_attacks.end() && attack->second->call( *this ) ) { ... reset_special( action ); }  // :1007
if( local_attack_data.cooldown == 0 && !pacified && !is_hallucination() )                                   // :1040
```

**Fix:** Apply the same gates on the goal path, return if `is_dead()`, and skip the goal's special in the main loop that turn. (The `try_call` gate proposed in MONAI-B04 covers both paths.)

### MONAI-B13 · Behavior node overrides merge old and new children; `copy-from` appends conditions
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/behavior.cpp:160`, `src/behavior.cpp:188`, `src/behavior.cpp:230`, `src/behavior.cpp:233`, `src/generic_factory.h:395`

`node_t::load` appends a `temp_node_data` entry for every definition that has `children`. When a mod redefines a node id, `generic_factory` replaces the object but the old entry stays, and `finalize()` adds the children from both definitions. With `copy-from`, the base's conditions are copied and `load()` appends the new ones, so conditions cannot be replaced; children are never inherited at all. The bug is latent, since no mod redefines these nodes today.

```cpp
if( jo.has_array( "children" ) ) { temp_node_data.emplace_back(); ... }   // :160
conditions.emplace_back( ... );                                          // :188, never cleared
```

**Fix:** Key `temp_node_data` by id (a map), so that a later definition replaces the earlier one. Clear `conditions` when the JSON specifies `conditions`. Optionally support extend/delete for both.

### MONAI-B14 · `move_to` applies SHARP/ROUGH damage and digger state using the tile being left
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monmove.cpp:2224`, `src/monmove.cpp:2337`, `src/monmove.cpp:2366`, `src/monmove.cpp:2370`, `src/monmove.cpp:2373`, `src/monmove.cpp:2377`, `src/monmove.cpp:2389`, `src/monmove.cpp:2393`

`pos` is captured before `setpos()`, but the post-move terrain logic uses it as the current tile. The SHARP/ROUGH damage check and its `bash` look at the origin, while the vehicle-floor guard checks the destination. A monster stepping onto barbed wire takes no damage until it steps off. A burrower's `underwater` flag is set from whether the *origin* is DIGGABLE, so its burrowed state lags one tile behind.

```cpp
const tripoint_bub_ms pos = pos_bub( here );                       // :2224
setpos( here, destination );                                       // :2337
if( here.has_flag( ter_furn_flag::TFLAG_SHARP, pos ) && ...        // :2370
underwater = here.has_flag( ter_furn_flag::TFLAG_DIGGABLE, pos );  // :2389
```

**Fix:** Use `destination` for the SHARP/ROUGH checks, the bash and the digger `underwater` flag. Keep `pos` only for trail fields, which are meant to be left behind.

### MONAI-B15 · Friendly GROUP_MORALE/SWARMS monsters never find their fellow pets
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monmove.cpp:757`, `src/monmove.cpp:766`, `src/monmove.cpp:773`, `src/monster.h:104`, `src/creature_tracker.cpp:414`, `src/creature_tracker.h:190`, `src/monster.cpp:278`

For friendly monsters, `plan()` searches the reachability index with `actual_faction = monfaction_player`. The tracker buckets monsters by `get_monster_faction()`, which is their original faction; taming never changes it. The `player` bucket therefore holds only the avatar and follower NPCs, and the lambda drops non-monsters. A tamed wolf pack never shares morale and friendly swarmers never swarm. The reverse also holds: hostile wolves count tamed wolves as packmates.

```cpp
const mfaction_id actual_faction = friendly == 0 ? faction : monfaction_player;   // :757
mfaction_id get_monster_faction() const override { return faction.id(); }        // monster.h:104
```

**Fix:** For friendly monsters, iterate all factions and filter with `mon->friendly != 0`. For hostile monsters, exclude friendly ones. Alternatively, have `get_monster_faction()` return `player` for friendly monsters after auditing its other callers.

### MONAI-B16 · Integer division in size scaling: `zombie_fuse` growth and `get_weight`/`get_volume`
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monattack.cpp:4579`, `src/monster.cpp:3858`, `src/monster.cpp:3861`, `src/monster.cpp:3869`, `src/monster.cpp:3871`, `src/creature.h:137`, `src/units.h:255`

`zombie_fuse` computes `80 * ( volume / 62500_ml )`, and quantity/quantity is an int. Fusing with anything under 62.5 L (a zombie dog, a child zombie) adds no growth, while it still heals the full HP; volumes from 62.5 to 125 L all floor to 80. `get_weight`/`get_volume` scale by `get_size() / type->size`, which is integer division on `creature_size`. A devourer grown from large to huge (5/4) therefore keeps its weight and volume, and a negative SIZE effect would give 0 (no shipped content has one).

```cpp
int hp_or_vlm = std::min( critter->get_hp_max(), ( 80 * ( critter->get_volume() / 62500_ml ) ) );  // :4579
units::operator*( type->weight, get_size() / type->size )                                         // monster.cpp:3861
```

**Fix:** Use floating-point ratios, for example `80.0 * units::to_milliliter( vol ) / 62500.0` and `type->weight * static_cast<double>( get_size() ) / static_cast<int>( type->size )`, and clamp the size to at least tiny.

## 3. Performance

### MONAI-P01 · Every target-less monster scans all 2,541 bubble submaps for moving vehicles on every plan() step
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monmove.cpp:728`, `src/monmove.cpp:731`, `src/monmove.cpp:732`, `src/map.cpp:1595`, `src/map.cpp:1600`, `src/map.cpp:597`, `src/map.cpp:1636`, `src/mattack_actors.cpp:1374`

`plan()` runs before every move step of every monster. When there is no target, which is the normal state for wandering zombies and wildlife, it calls `get_moving_vehicle_targets(*this, MAX_VIEW_DISTANCE)`. That calls `get_vehicles()`, which walks 11×11×21 = 2,541 submaps and collects every parked car into a new list, and it also builds two `std::set`s. This happens even when nothing is moving, and before the cheap `will_throw_self_at_vehicle` test that may discard the result. 300 idle monsters means roughly 450 calls, about 1.1M submap visits and 450 allocations per turn. While the player drives, the cost grows with every vehicle part, because each part gets a full `sees()` check.

```cpp
if( !mon_plan.target ) {
    std::set<tripoint_bub_ms> target_vehicles =
        here.get_moving_vehicle_targets( *this, MAX_VIEW_DISTANCE );
    const bool will_throw_self_at_vehicle =
        !get_pathfinding_settings().avoid_dangerous_fields;          // monmove.cpp:728-734
```

**Fix:** (1) Test `will_throw_self_at_vehicle` before the call. (2) Once per turn, build a small vector of moving vehicles from `level_cache::vehicle_list`, filtered by `is_moving()` and the `fov_3d_z_range`, and skip the call entirely when it is empty. (3) Have `get_moving_vehicle_targets` iterate that list, with a cheap distance or bounding-box test before the per-part `sees()` loop. `gun_actor::call` benefits too.

### MONAI-P02 · anger_cub_threatened's early return never fires, so each monster that sees the player walks the whole monster list
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monmove.cpp:509`, `src/monmove.cpp:511`, `src/monmove.cpp:516`, `src/monmove.cpp:456`, `src/monmove.cpp:598`, `src/monmove.cpp:600`, `src/game.cpp:10788`, `src/game.cpp:10756`

The `< 0` guard is never true (see MONAI-B08), so every hostile monster that sees the player iterates `g->all_monsters()` on every plan step. Each iteration builds a fresh `std::vector<weak_ptr>` of all monsters and `lock()`s each entry. That is O(N²) per turn with N allocations, and it peaks during horde fights: about 100k lock/unlock operations per turn with 300 zombies in view, atomic on MSVC/libc++ builds. Friendly monsters do the same full scan at line 600 instead of using the reachability index.

```cpp
if( mon_plan.angers_cub_threatened < 0 ) { return; }   // never true: value is 0 or 8
for( monster &tmp : g->all_monsters() ) {               // vector<weak_ptr> copy + lock() per entry
```

**Fix:** Change the guard to `<= 0` (or check `type->baby_type` once). Iterate `get_creature_tracker().get_monsters_list()`, or `for_each_reachable` with a faction filter, instead of `g->all_monsters()`. In the friendly branch at line 600, use `for_each_reachable` with a hostile-faction predicate, as the hostile branch does at 685.

### MONAI-P03 · `monster::attitude(Character*)` allocates two trait vectors on every call, several times per step
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monster.cpp:1792`, `src/monster.cpp:1806`, `src/monster.cpp:1881`, `src/newcharacter.cpp:2106`, `src/monster.cpp:1655`, `src/monster.cpp:1689`, `src/monmove.cpp:577`, `src/monmove.cpp:1123`, `src/monmove.cpp:2814`

Every `attitude(&character)` call runs `u->get_functioning_mutations()`, which allocates a vector and calls `.obj()` on each trait. It runs once at 1792, before the cheap `!aggro_character` early return, and again at 1881. `attitude` is reached through `attack_target()` once per ready special (a basic zombie has three), from `move()`, `is_fleeing()` in plan()'s NPC loop and `will_reach()`. That is about 4-8 calls per chasing monster per step, and roughly 2-5k vector allocations per turn for 300 monsters.

```cpp
for( const trait_id &mut : u->get_functioning_mutations() ) {   // :1792, before early-out
...
for( const trait_id &mut : u->get_functioning_mutations() ) {   // :1881
```

**Fix:** Keep a small per-Character summary of `anger_relations`/`ignored_by` species, invalidated wherever cached mutations are recalculated, and read it in `attitude()`. At minimum, iterate the mutation map in place, and move the anger-relation scan below the cheap early-outs.

### MONAI-P04 · `hear_sound` redoes a 27-tile fire scan around the source for every monster that hears each sound
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monster.cpp:4127`, `src/monster.cpp:4129`, `src/sounds.cpp:497`, `src/sounds.cpp:501`

`sounds::process_sounds` calls `hear_sound` for every sound cluster × every monster in range. Each call scans `points_in_radius(source, 1, 1)` for fire fields and fire containers, a result that depends only on the source, and it does so before the cheaper rejection tests. Clusters are capped at about 10, so a loud fight costs roughly 10 × 300 × 27 lookups, about 1-3 ms per turn.

```cpp
for( const tripoint_bub_ms &pt : here.points_in_radius( source, 1, 1 ) ) {
    const field_entry *fire_fld = here.get_field( pt, fd_fire );   // monster.cpp:4129
```

**Fix:** Compute `probably_a_fire` once per centroid in `process_sounds` (sounds.cpp:501 is the only caller) and pass it in, or fold it into `centroid.provocative`. Iterate the tracker's monster list instead of `g->all_monsters()`.

### MONAI-P05 · The same target is re-checked with `sees()`/`attack_target()` 4-6 times per monster step
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/monmove.cpp:2142`, `src/monmove.cpp:575`, `src/monmove.cpp:389`, `src/monster.cpp:1655`, `src/mattack_actors.cpp:532`, `src/mattack_actors.cpp:538`, `src/mattack_actors.cpp:304`, `src/mattack_actors.cpp:311`, `src/mattack_actors.cpp:326`, `src/mattack_actors.cpp:370`, `src/mattack_actors.cpp:380`, `src/mattack_actors.cpp:386`, `src/mattack_actors.cpp:1366`, `src/mattack_actors.cpp:1368`

For a monster chasing the player, the same visibility question is answered repeatedly in one step:
- `plan()` calls `sees(player)`, and then `rate_target()` calls it again.
- `melee_actor::find_target` calls `attack_target()` (creature lookup + attitude + `sees()`) before the cheap adjacency test, so a distant zombie with grab/bite/scratch pays three `sees()` calls just to learn it isn't adjacent.
- `attack_at()` evaluates `sees(player)` eagerly on every step, even when the destination isn't the player's tile.
- The eoc, spell and gun actors call `attack_target()` two or three times, then `sees()` again.

`Creature::sees(Creature)` is not cheap: flag and effect checks, three `sight_range` evaluations, ambient light and an LOS test.

```cpp
const bool sees_player = sees( here, player_character );   // monmove.cpp:2142, eager
Creature *target = z.attack_target(); ... if( range == 1 && !z.is_adjacent( target, false ) ) return nullptr;  // :532-540
```

**Fix:** Compute `sees_player` lazily in `attack_at`. In `find_target`, look up `creature_at(get_dest())` and test adjacency/range before `sees()`. Cache `attack_target()` in a local in the eoc, spell and gun actors and drop the redundant `sees()`. Longer term, add a per-move (target, sees) cache on `monster` that `move()` resets.

### MONAI-P06 · Reachability zones are re-flood-filled from scratch after every door, wall or vehicle move, and for each new monster
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/creature_tracker.cpp:349`, `src/creature_tracker.cpp:362`, `src/creature_tracker.cpp:364`, `src/creature_tracker.cpp:369`, `src/creature_tracker.h:132`, `src/map.cpp:420`, `src/map.cpp:665`, `src/map.cpp:1948`, `src/lightmap.cpp:299`, `src/creature.h:811`

Hostile-target search uses `for_each_reachable` → `flood_fill_zone`. Any `invalidate_reachability_cache()` clears every zone, and the next planner in each region re-floods its whole connected area, potentially the entire 132×132×Z bubble, with a `creature_at` per tile. Invalidation is very broad. `set_transparency_cache_dirty(p)` with the default `field=false` invalidates the cache, `add_vehicle_to_cache` calls it for every vehicle part, and `displace_vehicle` calls that. So driving re-floods every zone every turn, and so does any door opened or closed. A monster spawned after the last flood has `reachable_zone = 0`, fails `old_zone * zone_tick_ > 0`, and triggers a re-flood on its first `plan()`. Stale per-zone vectors also keep growing until the next invalidation.

```cpp
int old_zone = origin.get_reachable_zone();
if( old_zone * zone_tick_ > 0 ) { return; }
ff::flood_fill_visit_10_connected( origin.pos_bub(), ...        // creature_tracker.cpp:362-366
if( !field ) { get_creature_tracker().invalidate_reachability_cache();   // map.cpp:419-420
```

**Fix:** (1) Give a monster its zone on spawn or add, by reading the zone of an already-zoned neighbor or a per-tile zone array, instead of re-flooding. (2) Make invalidation incremental: a tile becoming passable merges the adjacent zones (union-find), and only tiles becoming impassable mark the cache dirty. (3) Stop invalidating on vehicle cache refreshes. The comment at creature_tracker.h:129-131 says vehicles are excluded, but the non-field dirty path still fires.

### MONAI-P07 · Pathfinding monsters re-run a full A* route on every step when their target moves one tile
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/monmove.cpp:1203`, `src/monmove.cpp:1205`, `src/monmove.cpp:1208`, `src/monmove.cpp:847`, `src/monster.cpp:1620`, `src/monster.h:656`, `src/pathfinding.cpp:530`

`plan()` calls `set_dest(target->pos_abs())` every step, and `move()` discards the path whenever `path.back() != local_dest`. Any monster with `path_settings.max_dist > 0` (about 99-109 JSON entries: dogs, robots, wildlife, ferals; not standard zombies) that is chasing a moving target therefore runs a new `map::route()` every step. The `pathfinding_cd` backoff only applies after a failed route. `route` tries a straight line first, so open-ground chases are cheap, but around obstacles each step pays a full A*.

```cpp
if( pf_settings.max_dist >= rl_dist( pos_abs(), get_dest() ) &&
    ( path.empty() || rl_dist( pos_bub(), path.front() ) >= 2 || path.back() != local_dest ) ) {
    if( can_pathfind() ) { path = here.route( *this, pathfinding_target::point( local_dest ) );  // :1203-1209
```

**Fix:** Repair paths incrementally. If `rl_dist(path.back(), local_dest) <= 1` and the new endpoint passes `can_move_to`, append or replace the last node. Otherwise re-route at most every K turns, or when the next step is blocked. Replace the `path.erase(path.begin())` loop (1198-1200) with an index or a deque.

### MONAI-P08 · `monster::process_one_effect` rebuilds the whole enchantment cache once per effect
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/monster.cpp:3601`, `src/monster.cpp:3614`, `src/monster.cpp:3618`, `src/monster.cpp:3623`, `src/monster.cpp:3569`, `src/creature.cpp:2006`, `src/magic_enchantment.cpp:389`, `src/magic_enchantment.cpp:1488`

At the end of each `process_one_effect`, the enchantment cache is cleared (about 20 containers) and rebuilt from every effect's enchantments, with `is_active()` evaluated each time, which for DIALOG_CONDITION enchantments means building a dialogue. With E effects the work is O(E²) per monster per turn. `has_effect_with_flag(json_flag_BLIND)` is also evaluated once per effect. Only 56 of 527 core effect types have enchantments, and common monster effects have none, so the real cost is small. The same code causes MONAI-B01, and the same restructuring fixes both.

```cpp
enchantment_cache->clear();
for( const auto &elem : *effects ) { for( const enchantment_id &ench_id : elem.first->enchantments ) {
    ... if( ench.is_active( *this ) && ench.is_monster_relevant() ) { enchantment_cache->force_add( ench, *this );  // :3602-3615
```

**Fix:** Move the rebuild and the speed-bonus update into a `recalc_enchantments()` that runs once after the loop in `process_effects()` and once when an effect is added. Hoist the BLIND check out of the per-effect function.

### MONAI-P09 · `creature_tracker::update_pos` does a linear search of `monsters_list` on every monster step
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/creature_tracker.cpp:130`, `src/creature_tracker.cpp:136`, `src/monster.cpp:345`, `src/game.cpp:4604`, `src/game.cpp:4395`

Every position change runs `std::find_if` over the whole `monsters_list` to recover a `shared_ptr` that already sits in `monsters_by_location[old_pos]`. The work is O(N²) per turn, but it is only pointer compares over a contiguous vector: about 90k for 300 monsters, well under 1 ms.

```cpp
const auto iter = std::find_if( monsters_list.begin(), monsters_list.end(),
    [&]( const shared_ptr_fast<monster> &ptr ) { return ptr.get() == &critter; } );  // :130
```

**Fix:** Look up `old_pos` in `monsters_by_location`. If its `.get() == &critter`, move the entry to `new_pos` (with `extract()`). Fall back to the scan plus `rebuild_cache()` only when the entry is missing.

### MONAI-P10 · `monster::has_flag(flag_id)` interns a string and does a factory lookup on every call
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monster.cpp:1373`, `src/monster.cpp:1375`, `src/monster.cpp:1379`, `src/creature.cpp:651`, `src/monmove.cpp:1022`, `src/monmove.cpp:1424`, `src/monmove.cpp:1944`, `src/monmove.cpp:2137`

Each call builds `mon_flag_str_id checked( f.c_str() )`, which means a `std::string`, a global intern-table lookup and a `generic_factory` validity check. For JSON flags that are not monster flags (TRUE_SEEING, CANNOT_ATTACK, CANNOT_MOVE, SNOWWALKING) both lookups miss before the effect scan. `Creature::sees` calls it ahead of the invisibility test. It costs about 100 ns per call, well under 1 ms per turn in total.

```cpp
mon_flag_str_id checked( f.c_str() );
if( checked.is_valid() ) { return has_flag( checked ); } else { return has_effect_with_flag( f ); }  // :1375-1383
```

**Fix:** Memoize `flag_id → optional<mon_flag_id>` in a static map or a table built after mon_flag loading. Call `has_effect_with_flag` directly for flags known not to be monster flags. In `Creature::sees`, test the cheap invisibility conditions first.

### MONAI-P11 · `bash_skill()`/`bash_estimate()` copy a `std::map` on every call, including per A* neighbor
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monster.cpp:4370`, `src/monmove.cpp:2055`, `src/monmove.cpp:2042`, `src/monmove.cpp:1288`, `src/monmove.cpp:1432`, `src/monmove.cpp:1754`, `src/monster.h:340`, `src/pathfinding.cpp:613`

`bash_skill()` returns `type->bash_skill` by value, and the `get_path_avoid` lambda evaluates `!bash_skill().empty()` for blocked neighbors of every A* node, which allocates and frees a map node just to test emptiness. `move()` and `scent_move()` make more copies per step. `can_move_to` dominates the lambda's cost, so the gain is minor. Side note for the bug lens: `!bash_skill().empty() || !is_bashable` is inverted relative to its comment. As written, monsters that *can* bash avoid every blocked tile, and monsters that can't bash path through bashable tiles.

```cpp
if( !can_move_to( p ) && ( !bash_skill().empty() || !here.is_bashable( p ) ) ) {   // monster.cpp:4370
std::map<damage_type_id, int> monster::bash_skill() const { return type->bash_skill; }  // monmove.cpp:2055
```

**Fix:** Return `const std::map<...>&`, and add `bool can_bash() const`. Precompute `bash_estimate` on mtype. Capture `can_bash` and the avoidance radius by value outside the lambda. Re-check the inverted condition.

### MONAI-P12 · `creature_at()` scans every active NPC on empty-tile queries and does an effects lookup before a compile-time check
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/creature_tracker.cpp:323`, `src/creature_tracker.cpp:336`, `src/creature_tracker.h:147`, `src/monmove.cpp:1162`, `src/monmove.cpp:1372`, `src/cata_tiles.cpp:4298`, `src/creature_tracker.cpp:409`

On a hash miss (an empty tile, the common case), `creature_at<T>` compares against the avatar and then walks the `std::list` of active NPCs. On a monster hit, it evaluates `has_effect(effect_ridden)` before the constant `std::is_same_v` test, so `creature_at<monster>` and `creature_at<Creature>` always pay the lookup. Hot callers include draw_critter_at and the flood-fill visitor. The cost matters mainly in NPC-heavy camps.

```cpp
if( !mon_ptr->has_effect( effect_ridden ) || ( std::is_same_v<T, monster> || std::is_same_v<T, Creature> || ... ) ) {  // :323
for( auto &cur_npc : active_npc ) { if( cur_npc->pos_abs() == p && !cur_npc->is_dead() ) {  // :336
```

**Fix:** Put the type test first (`if constexpr`). Keep an `npcs_by_location` map so that empty-tile queries are a single hash miss. Consider a `std::vector` for `active_npc`.

### MONAI-P13 · Per-step overhead in `move_to`/`calc_movecost`: an unused vector, an option lookup by string, a `std::map` temporary, a string flag
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monmove.cpp:2250`, `src/monmove.cpp:2300`, `src/monmove.cpp:1793`, `src/monmove.cpp:1922`, `src/monmove.cpp:2270`, `src/options.cpp:4367`, `src/options.cpp:4278`

On every monster step:
- `move_to` computes `impassable_field_ids` and never reads it.
- `move_to` calls `get_option<bool>("LOG_MONSTER_MOVEMENT")`, which means a heap-allocated key string, two map lookups and a page-name copy.
- `calc_movecost` builds a two-entry `std::map tilecosts`, does a string-keyed `has_flag("BRIDGE")` per tile, and recomputes `flies()`/`swims()` inside the loop.

(`turns_to_reach` has no callers, so `calc_movecost` runs once per step.)

```cpp
const std::vector<field_type_id> impassable_field_ids = here.get_impassable_field_type_ids_at( destination );  // :2250, unused
if( get_option<bool>( "LOG_MONSTER_MOVEMENT" ) ) {                                                            // :2300
std::map<tripoint_bub_ms, int> tilecosts = {{from, 0}, {to, 0}};                                              // :1793
```

**Fix:** Delete the unused vector. Cache the LOG_MONSTER_* options in globals via `update_options_cache`. Use two local ints instead of the map. Add a `TFLAG_BRIDGE` enum value or a static flag id. Hoist the invariants out of the loop.

### MONAI-P14 · `gun_actor` builds items before its cheap early-outs and a full `standard_npc` per shot; the spell actor builds an `avatar` per cast
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mattack_actors.cpp:1455`, `src/mattack_actors.cpp:1478`, `src/mattack_actors.cpp:1486`, `src/mattack_actors.cpp:1493`, `src/mattack_actors.cpp:411`, `src/npc.cpp:313`, `src/monmove.cpp:1040`

`gun_actor::shoot` builds the gun, magazine and ammo items and does `migrate_id` lookups before checking stun and ammo. When those checks fail, `call()` returns false and the cooldown is not reset, so the work repeats on every `move()` while a target is visible. Each real shot also constructs a `standard_npc`, which assigns an npc id, sets every skill and builds a body. `mon_spellcasting_actor::call` constructs `avatar fake_player`, including a `map_memory`, just to call `casting_time()`.

```cpp
item gun( mig_gun_type );                                          // :1456, before the checks at 1478/1486
standard_npc tmp( _( "The " ) + z.name(), z.pos_bub(), {}, 8, ...  // :1493
avatar fake_player;                                                // :411
```

**Fix:** Move the stun checks and a cheap `z.ammo[ammo_type] > 0` test ahead of item construction. Cache the migrated ids at finalize. Reuse one static fake `standard_npc`, resetting it per shot. Give spells a monster casting-time path, or reuse a static dummy Character.

### MONAI-P15 · Behavior-tree tick per `move()` copies vectors, `std::function` objects and strings
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/monmove.cpp:996`, `src/behavior.cpp:43`, `src/behavior.cpp:44`, `src/behavior.h:92`, `src/behavior_strategy.h:20`, `src/behavior_strategy.cpp:27`, `src/monster_oracle.cpp:39`, `src/monster_oracle.cpp:110`

`strategy_t::evaluate` takes `children` by value. `process_predicates` copies each predicate's `std::function` (double-wrapped, beyond the small-buffer size) and its argument string. The oracle copies the material vectors and builds strings from `string_view`s. Only monsters with SPLIT, ABSORB_ITEMS or EAT_CROP have a non-empty tree, about 50 JSON entries; every other monster ticks an empty root without allocating. The real cost is a few small allocations per step for blobs and wildlife.

```cpp
virtual behavior_return evaluate( const oracle_t *subject, std::vector<const node_t *> children ) const = 0;  // .h:20
predicate_type pt = std::get<0>( predicate_tuple ); std::string pargs = std::get<1>( predicate_tuple );      // behavior.cpp:44-46
```

**Fix:** Take `children` by const reference. Bind the tuple elements by const reference. Give `predicate_type` the same `string_view` signature as `predicate_map`. Return the goal and the material vectors by const reference. Skip the tick when the root has no children.

### MONAI-P16 · Special-attack bookkeeping uses string-keyed maps per turn and per move; `reset_special` builds a two-talker dialogue
**Severity:** Low · **Effort:** Medium · **Verified:** plausible
**Where:** `src/monster.cpp:3016`, `src/monster.cpp:3018`, `src/monmove.cpp:1023`, `src/monmove.cpp:1025`, `src/monster.cpp:2947`, `src/monster.cpp:2949`, `src/monster.h:636`, `src/npctalk.cpp:2748`

`process_turn` and `move` each iterate `type->special_attacks` and `find()` every name in the per-monster map: about 6-9 short string-map lookups per monster per turn. Each successful attack's `reset_special` builds a dialogue, which `const_clone()`s both talkers (4 heap allocations), even for constant cooldowns. The maps are tiny and resets happen only on successful attacks, so the gain is marginal. The constant fast-path, though, is trivial: `dbl_or_var::is_constant()` already exists.

```cpp
const auto local_iter = special_attacks.find( special_name );                     // monster.cpp:3018
dialogue d( get_talker_for( this ), get_talker_for( get_avatar() ) );              // :2949
```

**Fix:** In `reset_special`, use `cooldown.constant()` when `is_constant()` is true and skip building the dialogue. Optionally index special attacks at finalize and store per-monster state in a parallel vector, keeping the name map for save/load.

## Appendix: considered and rejected

- **Many hardcoded special attacks are thin wrappers over missing generic actors (sound, polymorph message, summon, dart)** – Rejected: the generic tools already exist. The `spell` attack type supports `noise`, `summon`, `targeted_polymorph` and `revive`, plus `attack` with effects. The `eoc` attack type has `u_make_sound` and `u_spawn_monster`. What remains is cleaning up legacy C++, not a missing modding capability.
- **Reachability flood fill reads the upper tile's terrain as 'down' terrain (copy-paste)** – Rejected: on the -1 step, the upper tile's terrain is the right one to test for NO_FLOOR/GOES_DOWN, and `flood_fill.h` re-checks each tile before visiting it. No existing terrain produces a wrong zone. It is a naming wart, and a literal "fix" would break descent.
- **`kill_marked_for_death` copies the whole monster list on every `cleanup_dead`** – Rejected: on the primary libstdc++ build `shared_ptr_fast` uses a non-atomic refcount, so a few copies of a few hundred pointers per turn cost microseconds and would not show up in a profile.
- **`monster::get_overlay_ids` allocates strings and vectors per visible monster per frame** – Rejected: these allocations are negligible next to the rest of `draw_critter_at` (string copies and a `sees()` per drawn monster), and CDDA redraws on input or turn rather than continuously.
