# 18 · Combat: melee, ranged, martial arts, damage & explosions

> Engine audit – static code reading at commit `a1a646d6`; nothing was compiled or run. Each finding was checked against the code by a separate reviewer: **confirmed** = verified by reading the code; **plausible** = consistent with the code, but reachability or impact not proven. Line numbers refer to that commit.

**Scope:** `src/melee.cpp`, `src/ranged.cpp`, `src/ballistics.cpp`, `src/projectile.cpp`, `src/dispersion.cpp`, `src/damage.cpp`, `src/martialarts.cpp`, `src/character_martial_arts.cpp`, `src/weakpoint.cpp`, `src/explosion.cpp`, `src/turret.cpp`

## Summary

The combat code works for the common avatar-vs-monster case, but much of it was built around that case. Many paths are player-relative or avatar-only: AOE targeting, the reach-attack prompt, knockback_follow, EMP, and the BRAWLER/GUNSHY bans. Several documented JSON features are dead or only partly wired. Technique effects with no condition never apply, generic `MELEE_DAMAGE` enchantments do nothing in creature melee, `HARDTOHIT` has no effect against melee, `knockback_follow` never triggers, and martial-art EOCs get no opponent talker. One shipped mod item can crash the game when it cooks off. Most tuning is hardcoded by damage type, ammotype, bionic or trait id. The main performance problem is per-unit simulation: every shotgun pellet and every shrapnel fragment gets a full trajectory or hit resolution.

| Lens | Critical | High | Medium | Low | Total |
|---|---|---|---|---|---|
| Missing JSON / EOC | 0 | 1 | 11 | 5 | 17 |
| Bugs & quirks | 1 | 2 | 14 | 3 | 20 |
| Performance | 0 | 2 | 5 | 8 | 15 |
| **Total** | **1** | **5** | **30** | **16** | **52** |

**Fix first:**
1. **CMBT-B01** · Null-source crash in `apply_ammo_effects` – a shipped Aftershock item can segfault the game when it burns, and the fix is a few null guards.
2. **CMBT-B02** · Technique tech_effects with no `condition` are never applied – a one-line fix that brings back 33 dead effects, including every disarm against monsters.
3. **CMBT-B03** · `MELEE_DAMAGE` enchantment never applies to melee against creatures – a small fix that brings back 46 mod uses of a documented value.
4. **CMBT-P01** · Shot ammo simulates every pellet – a 24155-pellet flockshot stalls the game, and clustering pellets is a contained change.
5. **CMBT-P02** · Shrapnel resolves every fragment hit in full – thousands of armor resolutions per creature per explosion, fixed by aggregating hits past a small cap.

## 1. Missing JSON bindings & EOC features

### CMBT-J01 · Martial-art and combat EOC hooks run without the opponent talker or context; ongethit is skipped while winded or driving
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/martialarts.cpp:1342`, `src/melee.cpp:845`, `src/melee.cpp:875`, `src/melee.cpp:1798`, `src/melee.cpp:1995`, `src/melee.cpp:2002`, `src/melee.cpp:2361`, `src/creature.cpp:1107`, `src/damage.cpp:403`, `src/weakpoint.cpp:314`

`martialart::activate_eocs` builds `dialogue( u, nullptr )` for all 11 `on*_eocs` lists (onhit, oncrit, onkill, ondodge, onblock, ongethit, onmiss, ...), and it sets no context vars. An on-kill, on-dodge, on-block or on-get-hit EOC therefore cannot read or affect the opponent, even though every call site has it: `melee_special_effects` has `t`, `block_hit` has `source`, and `on_dodge` has `source`. The hooks are also only half wired:
- `ma_ongethit_effects` runs only after `block_hit`'s early return for sleep, narcosis, winded, fearparalyze and driving, so ongethit silently fails in those states.
- `block_hit` is reached only from melee paths, so projectile and explosion hits never fire ongethit.
- `ma_onhit_effects` fires before `deal_melee_hit`, so it triggers even when the target then fully blocks.

Three other hooks do get the opponent as beta but lack context vars:
- Technique `eocs` run in `perform_technique` (melee.cpp:845), before `deal_melee_hit` (:875). They cannot see damage dealt, the weakpoint hit or a kill, and cannot change the damage.
- damage_type `onhit_eocs` get no context vars at all. Only `ondamage_eocs` receive damage_taken, total_damage and bp.
- Weakpoint EOCs get no damage, weakpoint id or crit vars.

This blocks content such as an on-kill heal scaled to the victim, a riposte that debuffs the attacker on dodge, or a technique bleed scaled to damage dealt.

```cpp
// martialarts.cpp:1341-1343
for( ... eoc : eocs ) {
    dialogue d( get_talker_for( u ), nullptr );
    eoc->activate_activation_only( ... );
}
// melee.cpp:1995-2002
if( in_sleep_state() || has_effect( effect_narcosis ) || has_effect( effect_winded ) ||
    has_effect( effect_fearparalyze ) || is_driving() ) { return false; }
... martial_arts_data->ma_ongethit_effects( *this );
```

**Fix:** Thread an optional `Creature *opp` through `character_martial_arts::ma_on*_effects` into `activate_eocs` and use `dialogue d( get_talker_for( u ), opp ? get_talker_for( opp ) : nullptr )`. Set the context vars `_damage`, `_bp`, `_technique`, `_weapon`, `_crit` and `_weakpoint`. Move `ma_ongethit_effects` before the early return, and also call it from projectile and explosion damage paths (e.g. `Character::on_hit`). Split technique EOCs into a pre-hit list and a post-hit list:
```json
"eocs": [ "PRE_HIT_EOC" ],
"post_hit_eocs": [ "POST_HIT_EOC" ]
```
Post-hit EOCs would receive `_damage_dealt` and `_killed`. Expose a writable `_damage_mult` that pre-hit EOCs can set and `perform_technique` reads back. Document the talkers and vars in the EOC talker table (`doc/JSON/EFFECT_ON_CONDITION.md:62-89`).

### CMBT-J02 · Technique `"aoe"` is an unvalidated string with 3 hardcoded shapes; docs name a non-existent option
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/melee.cpp:1556`, `src/melee.cpp:1613`, `src/melee.cpp:1641`, `src/melee.cpp:1679`, `src/martialarts.cpp:341`, `doc/JSON/MARTIALART_JSON.md:165`

`aoe` is a free `std::string` that is compared only against `"wide"`, `"impale"` and `"spin"`. The docs list the options as `aoe`, `impale` and `wide`, so a modder who follows them writes `"aoe"`. `valid_aoe_technique` then always returns false and `evaluate_technique` silently discards the technique, with no load-time error. Shapes are not data-driven: there is no radius, max-targets or include-non-hostiles setting. The targeting and messaging bugs in the same code are covered in CMBT-B11 and CMBT-B17.

```cpp
optional( jo, was_loaded, "aoe", aoe, "" );                         // martialarts.cpp:341
if( !tec_id->aoe.empty() && !valid_aoe_technique( t, tec_id.obj() ) ) { ... return std::nullopt; } // melee.cpp:1556
```

**Fix:** Load `aoe` as an enum (wide/impale/spin), raise a load-time error for unknown values, and fix the docs (`spin`, not `aoe`). Better, make it an object:
```json
"aoe": { "shape": "arc", "radius": 1, "min_targets": 2, "max_targets": 3, "damage_mult": 1.0, "hostile_only": true }
```

### CMBT-J03 · BRAWLER/GUNSHY ranged bans are trait-id checks in avatar-only UI paths; NPCs ignore them
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/ranged.cpp:4574`, `src/ranged.cpp:4578`, `src/turret.cpp:595`, `src/turret.cpp:600`, `src/handle_action.cpp:1749`, `src/handle_action.cpp:1761`, `src/handle_action.cpp:2747`, `src/avatar_action.cpp:800`, `src/melee.cpp:2733`, `src/melee.cpp:2775`

The ranged and firearm bans are tied to two trait ids and checked only in avatar code (`gunmode_checks_common` takes an `avatar &`, plus avatar turret aiming and `handle_action` fire). `evaluate_weapon` has no such check, so an NPC with BRAWLER or GUNSHY fires guns normally. A profession effect, shapeshift form, cursed relic or mutation category cannot apply the ban without adding these exact traits. The turret GUNSHY check has no `is_firearm` test, so it also blocks non-firearm turrets.

A JSON flag for the firearm ban already exists: `HANDS_CANNOT_USE_FIREARMS` (data/json/flags.json:1333, set on paw bodyparts). It is checked only in the `ACTION_FIRE` branch (handle_action.cpp:1761). `ACTION_FIRE_BURST` (:2747), the surroundings-menu fire and the aim activity all skip it and `TEMPORARY_SHAPESHIFT_NO_HANDS`, so a paw mutant can burst-fire.

```cpp
if( you.has_trait( trait_BRAWLER ) ) { ... result = false; }      // ranged.cpp:4578
if( you.has_trait( trait_GUNSHY ) && gmode->is_firearm() ) { ... }
if( player_character.has_trait( trait_GUNSHY ) )                   // turret.cpp:600, no is_firearm check
```

**Fix:** Reuse `HANDS_CANNOT_USE_FIREARMS` (or a general `CANNOT_USE_FIREARMS`) and add `CANNOT_USE_RANGED` for BRAWLER. Grant both flags through the traits in JSON. Check them, together with `TEMPORARY_SHAPESHIFT_NO_HANDS`, in one common can-fire function used by every fire path, and in `evaluate_weapon_internal` (`allowed`, melee.cpp:2775) so NPCs respect them too.

### CMBT-J04 · No event or global hook for dodge/block/crit/miss/monster attacks; melee events fire before the attack resolves
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/melee.cpp:698`, `src/event.h:436`, `src/ranged.cpp:1229`, `src/character.cpp:1877`, `src/melee.cpp:2225`

Outside the selected martial art's `on*_eocs`, a mutation, item, effect or global EOC has no way to react to a dodge, block, critical hit, miss or technique kill. `on_dodge` and `block_hit` only call the martial-art hooks, and `event_type` has no dodge or block events. `character_melee_attacks_*` is sent right after the hit roll, before crit, technique and damage are known, so its only fields are {attacker, weapon, hits, victim_type}. `character_ranged_attacks_*` is sent only for creatures actually hit, with no shot or hit counts. Monster attacks emit no event. Partial workarounds exist (`monster_takes_damage` with a source talker, `character_takes_damage`, the monster_attack `eoc` field, enchantment `hit_me_effect`), but a style-independent "buff on every block" or "achievement on critical headshot" is not possible.

```cpp
// ranged.cpp:1229-1231
for( ... hit_entry : shot.targets_hit ) { if( hit_entry.second.first == 0 ) { continue; } ...
```

**Fix:** Add these events with talkers:
- `character_dodges` {character, attacker, attack_type}
- `character_blocks` {character, attacker, damage_blocked, blocked_with}
- `character_melee_hits`, sent after `deal_melee_hit` {attacker, victim, weapon, technique, bp, damage, crit, killed}
- `character_fires_gun` {gun, ammo, shots, hits}
- `monster_melee_attacks_character` / `monster_melee_attacks_monster` {monster, victim, hits, damage}

Alternatively, add `"on_dodge_eocs"`, `"on_block_eocs"` and `"on_crit_eocs"` arrays to mutation, bionic and enchantment JSON.

### CMBT-J05 · Explosion JSON: conflicting defaults, int shrapnel shorthand uses 16x lighter fragments, blast hits fixed human parts
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/explosion.h:22`, `src/explosion.h:42`, `src/explosion.h:85`, `src/explosion.cpp:135`, `src/explosion.cpp:153`, `src/explosion.cpp:162`, `src/explosion.cpp:354`, `src/explosion.cpp:375`, `src/explosion.cpp:397`, `src/explosion.cpp:594`, `src/npctalk.cpp:4780`

The explosion defaults disagree with each other:
- `distance_factor` is 0.8 in the struct and 0.75 in the JSON loader.
- `fragment_mass` is 0.005 g for the documented int shorthand (`"shrapnel": 500`), 0.08 g for the object form (with a `//differs from header?` comment), and 0.05 g in the `explosion()` helper.

So the two 60mm `ammo_effects` that use the shorthand produce 16x more fragments, each much weaker (about 10 damage per fragment instead of about 42 for EXPLOSIVE_60mmHE), although ITEM.md promises "reasonable defaults". The traps.json `shrapnel` values are vehicle_data and go through the helper, not the shorthand.

The blast itself has other limits:
- For characters it damages a fixed list of six human bodyparts, so tails, wings and extra arms are never hit.
- Monsters take untyped `apply_damage` to `"torso"` with bash armor halved, which bypasses resistances, weakpoints and damage-type EOCs.
- `fire` can only lay `fd_fire`.
- `distance_factor >= 1` is caught only at detonation (debugmsg), not at load.
- `u_explosion`'s int `shrapnel` branch reads `casing_mass` from the wrong object (see CMBT-B01).

```cpp
float distance_factor = 0.8f;                                                   // explosion.h:42
optional( jo, was_loaded, "distance_factor", distance_factor, 0.75f );         // explosion.cpp:153
optional( ..., "fragment_mass", fragment_mass, 0.08 ); //differs from header?  // explosion.cpp:162
static const std::array<blastable_part, 6> blast_parts = { { bodypart_id( "torso" ) ... // :397
critter->apply_damage( mutable_source, bodypart_id( "torso" ), actual_dmg );   // :375
```

**Fix:** Use one `constexpr` default for the struct, the loader, the int shorthand and `u_explosion`. Validate `0 < distance_factor < 1` in deserialize. For characters, iterate `get_all_body_parts( get_body_part_flags::only_main )`, weighted by a new bodypart `"blast_exposure"` field. Route monsters through `deal_damage`. Add explosion fields:
```json
"blast_damage_type": "bash",
"blast_field": { "field": "fd_fire", "intensity_scale": 1.0 },
"eoc": [ "ON_BLAST_HIT" ]
```
The `eoc` list runs per creature hit, with context `_force`.

### CMBT-J06 · Martial-art loader gaps: buffs have no condition, EOC arrays append under copy-from, raw-int durations, little validation
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/martialarts.cpp:237`, `src/martialarts.cpp:345`, `src/martialarts.cpp:365`, `src/martialarts.cpp:389`, `src/martialarts.cpp:437`, `src/martialarts.cpp:449`, `src/martialarts.cpp:482`, `src/martialarts.cpp:601`, `doc/JSON/MARTIALART_JSON.md:136`

The loader has six gaps:
- (a) Techniques accept a dialogue `condition`, but `ma_buff` and `ma_requirements` do not, so a buff cannot be gated on night, an undead target or a var.
- (b) Technique `eocs`, all 11 `on*_eocs` arrays and `autolearn` are read with raw `push_back` loops. Under copy-from they append to the parent's list, and `extend`/`delete` do not work. No current content trips this, so it is latent.
- (c) The tech_effect fields are hand-parsed behind a TODO, so `duration` is a raw int of turns rather than a time_duration string. The `tech_effects` array itself does go through `optional()`, so array-level delete works.
- (d) Inline buffs load with src `""`, which loses mod attribution.
- (e) `check()` only verifies that `attack_vectors` is non-empty. It does not validate attack_vector ids, effect ids, aoe values or EOC ids.
- (f) `powerful_knockback` is still in the docs, although it is marked "Currently not hooked up to code".

```cpp
for( JsonValue jv : jo.get_array( "eocs" ) ) { eocs.push_back( ... ); }   // martialarts.cpp:345
ted.duration = e.get_int( "duration", 0 );                                // after "// TODO: update to use optional()?"
ma_buffs.load( jsobj, "" );                                               // :449
```

**Fix:** Add `"condition"`/`"condition_desc"` to ma_buff, evaluated in `ma_buff::is_valid_character`. Load the EOC lists with `optional( jo, was_loaded, "onhit_eocs", onhit_eocs, eoc_reader{} )`. Rewrite tech_effect loading with `optional()` and accept `"duration": "40 s"`, keeping int-as-turns for compatibility. Pass the parent's src to inline buffs. Validate attack_vector, effect, EOC and aoe ids in `check()`. Implement `powerful_knockback` or remove it from the docs.

### CMBT-J07 · Blocking is tuned entirely in C++: WBLOCK_1/2/3 tiers, stamina gate, size gate, elemental /5, counter fragility
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/melee.cpp:1961`, `src/melee.cpp:2015`, `src/melee.cpp:2022`, `src/melee.cpp:2028`, `src/melee.cpp:2174`, `src/melee.cpp:2218`, `src/melee.cpp:2231`

Block strength comes from three dummy technique ids mapped to 4, 6 and 10, with `BLOCK_WHILE_WORN` fixed at 2. A tower shield (12), a buckler that blocks only cut, or a parrying dagger with special elemental blocking cannot be expressed in data. Other values are also hardcoded:
- the stamina cutoff, an absolute 2000 rather than a fraction
- "cannot block attackers 2 sizes bigger" unless `BLOCK_HUGE_ATTACKS`
- the reaction roll (melee skill * 20)
- elemental damage divided by exactly 5 for any item block
- block stamina cost at 6x the burn rate
- no counterattack when the weapon is more than 20% glass

Martial-art buffs already provide `block_effectiveness` and flat block bonuses, and `BONUS_BLOCK` exists for block count. Per-item block strength and damage-type filtering do not.

```cpp
if( shield.has_technique( WBLOCK_3 ) ) { block_bonus = 10; } else if( ...WBLOCK_2 ) { block_bonus = 6; } ...
if( get_stamina() < 2000 )                                     // melee.cpp:2015
elem.amount /= 5;                                              // melee.cpp:2174
```

**Fix:** Add an item field:
```json
"block": { "bonus": 6, "worn": false, "damage_mult": { "cut": 0.5 }, "elemental_mult": 0.2, "stamina_cost_mult": 1.0 }
```
Keep `WBLOCK_n` as legacy aliases resolved in finalize. Move the stamina cutoff, size margin and glass threshold to game_balance options. Add the enchant values `BLOCK_EFFECTIVENESS` and `BLOCK_STAMINA_COST`.

### CMBT-J08 · CQB bionic hardcoded in ~12 combat formulas with inconsistent floor-vs-override semantics
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/melee.cpp:367`, `src/melee.cpp:849`, `src/melee.cpp:1166`, `src/melee.cpp:1301`, `src/melee.cpp:2028`, `src/melee.cpp:2690`, `src/martialarts.cpp:777`, `src/martialarts.cpp:1689`, `src/martialarts.cpp:1694`, `src/martialarts.cpp:1724`, `src/game_constants.h:111`

"Treat my combat skills as level 5" is only available as `has_active_bionic( bio_cqb )`, checked separately in to-hit, crit, damage, block, attack speed, technique requirements, arm/leg block and style learning. No mutation, stim effect or other CBM can grant it, and the level cannot change. The sites also disagree on what it means. To-hit and crit treat it as a floor, while damage, block reaction, attack speed and technique requirements override the real skill, so a level-10 fighter drops to 5. `arm_block_with_bio_armor_arms`/`leg_block_with_bio_armor_legs` are martial-art fields that hardcode two bionic ids. Enchantment `skills` can add to skills but cannot set a floor.

```cpp
if( skill < BIO_CQB_LEVEL && has_active_bionic( bio_cqb ) ) { skill = BIO_CQB_LEVEL; }  // melee.cpp:367, floor
if( u.has_active_bionic( bio_cqb ) ) { skill = BIO_CQB_LEVEL; }                         // melee.cpp:1301, override
float melee_skill = has_active_bionic( bio_cqb ) ? 5 : get_skill_level( skill_melee );  // melee.cpp:2028
```

**Fix:** Add an enchantment field `"skill_floor": { "melee": 5, "unarmed": 5, "bashing": 5 }` and a `Character::effective_combat_skill( skill )` helper that returns `max( real, floor )`. Give bio_cqb that enchantment in JSON. Add a `CQB_LEARNING` character flag plus `"style_learn_chance"`. Replace the bio_armor fields with `"arm_block_char_flags"` and `"leg_block_char_flags"`.

### CMBT-J09 · Gun noise/message, action cycling time and overheat failure effects hardcoded by ammotype/flag/fault id
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/ranged.cpp:700`, `src/ranged.cpp:993`, `src/ranged.cpp:1013`, `src/ranged.cpp:1285`, `src/ranged.cpp:2582`, `src/fault.cpp:272`

`item::gun_noise` computes the JSON loudness and suppressor multiplier, then throws them away for hardcoded ammotypes:
- 40x46/40x53mm: volume 8, "Thunk!"
- 12mm and metal_rail: volume 24
- rockets and flamethrowers: volume 4, "Fwoosh!"

So a suppressor on those types does nothing. Every sound message is hardcoded, while reload already has a JSON `reload_noise`. A modded launcher with a new ammotype gets the generic "bang!". Other hardcoded values:
- Action time is 80/100/120/160 moves for LEVER/SINGLE/PUMP/BOLT, with no per-gun field.
- Overheat faults have fixed consequences keyed to fault ids: a 1200-power explosion, three `fd_nuke_gas` clouds and cooling x4. Fault JSON has no effect fields.
- Pyromania satisfaction uses its own ammotype list.

```cpp
if( at.count( ammo_40x46mm ) || at.count( ammo_40x53mm ) ) { return { 8, _( "Thunk!" ) }; }  // ranged.cpp:2583
if( firing.has_flag( json_flag_LEVER_ACTION ) ) { operation_time += 80.f; }                  // ranged.cpp:703
explosion_handler::explosion( this, this->pos_bub(), 1200, 0.4 );                            // ranged.cpp:1000
```

**Fix:** Add a gun/ammo field `"fire_sound": { "volume": 8, "message": "Thunk!", "burst_message": "...", "ignore_loudness": true }` and move the ammotype special cases into JSON. Add `"cycle_time"` to islot_gun, defaulting from the action flags. Give faults:
```json
"overheat_effect": { "explosion": { "power": 1200, "distance_factor": 0.4 }, "field": { "id": "fd_nuke_gas", "count": 3 }, "heat_mult": 4, "eoc": [] }
```
Add a `PYROMANIA_SATISFYING` ammo/gun flag to replace the ammotype list.

### CMBT-J10 · EOC math/conditions cannot read or write core combat state (to-hit, crit, block, dodges/blocks left, recoil/aim)
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/condition.cpp:2366`, `src/condition.cpp:2434`, `src/talker.h:58`, `src/math_parser_diag.cpp:1877`

The combat accessors available to EOCs are `armor()`, `attack_speed()`, `melee_damage()`, `gun_damage()`, `coverage()` and the legacy `u_val('dodge')`. That last one dereferences `get_const_character()` without a guard, and that returns nullptr for monster talkers, so it crashes on a monster. EOCs cannot read hit roll, crit chance, block score, blocks/dodges left, recoil, aim level or weapon dispersion. They cannot set recoil or aim at all. A "steady aim" spell or a "reset recoil after a technique" effect is impossible. Some of this can be worked around: `u_using_martial_art` tests the selected style, and effects carrying `BONUS_BLOCK`/`BONUS_DODGE` can grant an extra block or dodge.

```cpp
} else if( checked_value == "dodge" ) {
    return d.const_actor( is_npc )->get_const_character()->get_dodge();   // condition.cpp:2434
```

**Fix:** Add math functions `hit_roll()`, `crit_chance('target')`, `block_score()`, and `dodge_chance()` (monster-safe through `Creature::get_dodge`). Add assignable `blocks_left()`, `dodges_left()` and `recoil()`, plus `weapon_dispersion()`. Guard all of them for non-character talkers, and fix `val('dodge')` the same way. Add a `u_set_recoil` / `u_aim` effect.

### CMBT-J11 · EMP blast and flashbang hardcode terrain, item, trait and species ids; NPCs immune to EMP
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/explosion.cpp:648`, `src/explosion.cpp:683`, `src/explosion.cpp:766`, `src/explosion.cpp:796`, `src/explosion.cpp:852`, `src/explosion.cpp:865`, `src/explosion.cpp:908`

`emp_blast` hardcodes several responses:
- Its map effects are three card-reader terrains plus `t_door_metal_locked` -> `t_floor`.
- Its item effect is a special case for `e_handcuffs`.
- The robot deactivation chance is set by size (tiny 1/6, small 1/3, larger never), next to the comment "Maybe export this to json?".
- Power drain and item frying apply only when the avatar is on the tile, and line 908 has the TODO "Drain NPC energy reserves", so NPCs are unaffected.

`flashbang` special-cases the PER_SLIME/PER_SLIME_OK traits and checks `species_ROBOT` next to the existing `FLASHBANGPROOF` flag, so a modder cannot make a flash-vulnerable robot.

```cpp
if( t == ter_t_card_science || t == ter_t_card_military || t == ter_t_card_industrial ) { ... }  // :767
if( player_character.pos_bub() == p ) {                                                          // :852
if( weapon && weapon->typeId() == itype_e_handcuffs && weapon->charges > 0 )                     // :865
```

**Fix:** Add a ter/furn field:
```json
"emp_response": { "transforms_into": "t_card_reader_broken", "chance": 60,
  "radius_transform": { "from": "t_door_metal_locked", "to": "t_floor", "radius": 3, "chance": 20 }, "eoc": [] }
```
Add an item `"emp_action"` (or an EMP_RELEASE flag plus an EOC) and a monster `"emp_deactivate_chance"`. Apply the character branch to every Character on the tile. For flashbangs, use character flags (e.g. `FLASH_RESIST_RETRACTABLE`) and rely on `FLASHBANGPROOF` instead of checking `species_ROBOT`.

### CMBT-J12 · Melee/projectile damage-type mechanics hardcoded to bash/cut/stab; custom melee damage types are second-class
**Severity:** Medium · **Effort:** Large · **Verified:** confirmed
**Where:** `src/melee.cpp:1305`, `src/melee.cpp:1344`, `src/melee.cpp:1366`, `src/ballistics.cpp:148`, `src/weakpoint.cpp:362`, `src/weakpoint.cpp:388`, `src/explosion.cpp:411`, `src/explosion.cpp:491`, `src/damage.h:27`

damage_type JSON covers names, skills, flags, immunity and EOCs, but the combat formulas compare against literal ids:
- Only bash gets the strength bonus, bash cap and low-roll floor, and stab has its own skill curve.
- Crit bonuses (damage mult, armor mult, arpen) exist only for stab, cut and bash. There is no else branch.
- Only cut and stab can embed projectiles.
- The weakpoint attack type resolves only for bash, cut and stab; anything else is `NONE`. `compute_wp_skill` then gives characters zero weakpoint skill.
- Explosion blast damage is always bash and shrapnel is always bullet.

A custom `slash`, `pierce` or `holy` melee type gets no crit bonus, no strength scaling, no embedding and the wrong weakpoint selection. Shipped content has only one custom melee type (`afs_heat_melee`), so the current impact is limited.

```cpp
// FIXME: Hardcoded damage type effects (bash)
if( dt == damage_bash ) { dmg += u.bonus_damage( !average ); ...                // melee.cpp:1305
if( primary == damage_bash ) return MELEE_BASH; ... return attack_type::NONE;   // weakpoint.cpp:362
frag.proj.impact = damage_instance( damage_bullet, damage );                     // explosion.cpp:491
```

**Fix:** Add damage_type fields and port the current constants into data/json/damage_types.json so behaviour is unchanged:
```json
"melee_crit": { "damage_mult": 0.5, "armor_mult": 0.5, "arpen": 0, "scales_with_skill": false },
"strength_scaling": { "bonus": true, "cap_per_str": 2, "cap_per_skill": 2 },
"skill_curve": [ [ 0, 0.66 ], [ 5, 1.16 ], [ 10, 1.46 ] ],
"can_embed": true,
"weakpoint_attack": "cut"
```
Give explosion_data `"blast_damage_type"` and shrapnel_data `"damage_type"`.

### CMBT-J13 · Automatic turret targeting stats, max auto range and heat cutoff hardcoded
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/turret.cpp:750`, `src/turret.cpp:756`, `src/turret.cpp:789`, `src/turret.cpp:818`

Every automatic turret aims through a fake NPC with str 16, dex 8, per 12, gun skill 4 and weapon skill 8. Auto-targeting is capped at 20 tiles (with a TODO), and turrets stop firing at 60% of the overheat threshold. A crude scrap turret or a long-range military fire-control computer cannot be modelled, and veh_type has no targeting fields.

```cpp
brain.set_str_base( 16 ); brain.set_dex_base( 8 ); brain.set_per_base( 12 ); ...
int max_range = 20;            // turret.cpp:818
if( heat > threshold * 0.6 ) { // turret.cpp:789
```

**Fix:** Add a vpart (or turret-controls part) field, with the current values as defaults, and rebuild `cpu.brain` when it changes:
```json
"turret_targeting": { "str": 16, "dex": 8, "per": 12, "gun_skill": 4, "weapon_skill": 8, "max_auto_range": 20, "heat_cutoff": 0.6 }
```

### CMBT-J14 · Aim-based proficiency training hardcoded to archery -> prof_bow_basic/expert/master
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/ranged.cpp:1792`, `src/ranged.cpp:1864`, `src/melee.cpp:940`, `src/melee.cpp:978`, `data/json/weapon_categories.json:6`

Aiming trains proficiencies only when `gun_skill == archery`, along a hardcoded basic -> expert -> master chain at 36 s per ~100 aim moves. weapon_category has a JSON `proficiencies` list, but only melee swings train it. A crossbow, sling or handgun proficiency line cannot be trained from aiming or firing. An event EOC on `character_ranged_attacks_*` can train proficiencies on hits, but misses and aiming cannot be covered that way.

```cpp
if( relevant.gun_skill() != skill_archery ) { return; }   // ranged.cpp:1795
```

**Fix:** Add a weapon_category field, practised in order (the first unlearned entry wins), and train it from `do_aim`/`fire_gun`. Move the bow chain into weapon_categories.json:
```json
"aim_proficiencies": [ { "id": "prof_bow_basic", "time_per_turn": "36 s" }, { "id": "prof_bow_expert", "time_per_turn": "36 s" } ],
"fire_proficiencies": [ ... ]
```

### CMBT-J15 · `damage_info_order` `"verb"` is loaded but never used; melee hit verbs hardcoded per damage type
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/damage.cpp:208`, `src/damage.cpp:299`, `src/melee.cpp:2482`, `src/melee.cpp:2592`, `doc/JSON/JSON_INFO.md:1490`

`verb` is loaded and given a default in finalize, but nothing reads it. JSON_INFO.md says it is used in the item info melee section, but that section uses `dmg_type->name`. `melee_message` has six hardcoded verb arrays for stab, cut and bash (with a FIXME). Every other type gets "You hit %s", so a custom type cannot say "You scorch %s".

```cpp
// FIXME: Hardcoded damage type messages
if( dominant_type.first == damage_stab ) {...} else { message = npc ? _( "<npcname> hits %s" ) : _( "You hit %s" ); }
```

**Fix:** Add a damage_type field (keyed by damage threshold) and port the existing arrays into damage_types.json. Then remove the unused `verb` or wire it into item info:
```json
"melee_hit_messages": { "u": [ [ 30, "You impale %s" ], [ 20, "You pierce %s" ], [ 0, "You poke %s" ] ], "npc": [ ... ] }
```

### CMBT-J16 · Thrown items cannot carry ammo effects/EOCs; throw behaviours keyed to fixed thresholds, flags and glass_shard
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/ranged.cpp:198`, `src/ranged.cpp:1565`, `src/ranged.cpp:1582`, `src/ranged.cpp:1644`, `src/ranged.cpp:1653`, `src/ranged.cpp:1674`, `src/ranged.cpp:1693`, `src/ballistics.cpp:100`

Thrown projectile effects are generated in C++ from fixed rules:
- `HEAVY_HIT` above 500 g and `WIDE` above 500 ml
- `TANGLE` and `ACT_ON_RANGED_HIT` from item flags
- `LIGHTNING` from bio_railgun, when the item is made of a hardcoded "ferric" material list
- `SHATTER_SELF` only from glass

No item field lets a thrown weapon carry arbitrary ammo_effects, so a poisoned throwing knife needs the `ACT_ON_RANGED_HIT` + use_action workaround. Shattering always drops `glass_shard`, scaled by mass, whatever the material.

```cpp
if( weight > 500_gram ) { proj_effects.insert( ammo_effect_HEAVY_HIT ); }       // ranged.cpp:1674
has_active_bionic( bio_railgun ) && thrown.made_of_any( ferric )                  // ranged.cpp:1565
```

**Fix:** Add an item field `"thrown_ammo_effects": [ "BEANBAG", "MY_POISON_EOC_EFFECT" ]` and merge it in `throw_item`. Add a material `"shatter_into": "glass_shard"` and a `"shatters_when_thrown"` property. Replace the railgun special case with an enchantment-granted thrown ammo effect, filtered by a `FERROMAGNETIC` material flag.

### CMBT-J17 · Melee special effects and mutation attacks hardcoded to bionic/trait/bodypart ids
**Severity:** Low · **Effort:** Medium · **Verified:** plausible
**Where:** `src/melee.cpp:254`, `src/melee.cpp:533`, `src/melee.cpp:2282`, `src/melee.cpp:2304`, `src/melee.cpp:2315`, `src/melee.cpp:2319`, `src/melee.cpp:2350`, `src/melee.cpp:2366`

`melee_special_effects` hardcodes bio_shock (2-10 electric), bio_heat_absorb and FLAMING (1-8 heat), and `hardcoded_mutation_attack` special-cases the ARM_TENTACLES attack counts. The reviewer noted that most of this can already be done in JSON: enchantment `melee_damage_bonus` per type, with math and conditions, plus `hit_you_effect`, and mutation attack fields. Two gaps remain. HURT_WHEN_WIELDED and glass shattering hit the literal `hand_r`/`hand_l` and `arm_r`/`arm_l` bodyparts, which is wrong for non-human anatomies. FRAGILE_MELEE breakage also ignores a fixed component blacklist.

```cpp
deal_damage( nullptr, bodypart_id( "hand_r" ), di );                                   // melee.cpp:2319
const std::set<itype_id> blacklist = { itype_sheet_cotton, itype_leather, itype_fur }; // melee.cpp:254
```

**Fix:** Make HURT_WHEN_WIELDED and the shatter path target the actual wielding limbs (bodyparts with the grasp limb score). Replace the blacklist with a `NONSTRUCTURAL_COMPONENT` item flag. Optionally move the two-handed tentacle attack reduction into mut_attack (`"attacks": { "base": 7, "minus_if_two_handed": 1 }`).

## 2. Bugs, oversights & quirks

### CMBT-B01 · Null-source crashes: `apply_ammo_effects` EOC/spell effects, `u_explosion`, undocumented `u_ranged_attack`
**Severity:** Critical · **Effort:** Small · **Verified:** confirmed
**Where:** `src/projectile.cpp:224`, `src/projectile.cpp:225`, `src/projectile.cpp:236`, `src/projectile.cpp:237`, `src/item_gun_tool_ammo.cpp:3590`, `src/iuse_actor.cpp:767`, `src/iuse_actor.cpp:774`, `src/ballistics.cpp:670`, `src/map_field.cpp:1005`, `data/mods/aftershock_exoplanet/items/ammo/recoiless.json:50`, `src/npctalk.cpp:4781`, `src/npctalk.cpp:4807`, `src/npctalk.cpp:5948`

`apply_ammo_effects` accepts `source == nullptr`, and three callers pass it:
- `SPECIAL_COOKOFF` detonation, through `item::detonate`
- `NULL_SOURCE` projectiles
- explosion iuse when the recorded thrower NPC can no longer be found

The `eoc` and `spell_data` branches dereference `*source` unconditionally. This is reachable with shipped data. Aftershock's `84x246mm_cluster` has `SPECIAL_COOKOFF` plus `CLUSTER_84x24` (spell_data). When it burns, map_field.cpp:1005 calls `detonate`, which calls `apply_ammo_effects( nullptr, ..., 1 )`, and `cast_all_effects( *source )` dereferences null. EFFECT_ON_CONDITION.md:89 explicitly promises that cook-off works with EOCs.

There are related EOC problems:
- `u_ranged_attack` is used in data but undocumented, and takes no parameters. It dereferences a null Character for monster talkers and an empty wielded `item_location`.
- `u_explosion`'s int `"shrapnel": N` branch reads `"casing_mass"` instead, which gives a load error.
- `u_explosion` from item, furniture or vehicle talkers does `*get_creature()` on nullptr. That is undefined behaviour, but in practice it passes null to `explosion()`, which tolerates it.

```cpp
dialogue d( get_talker_for( *source ), critter == nullptr ? nullptr : get_talker_for( critter ) ); // projectile.cpp:225
ammo_spell.cast_all_effects( *source, p );                                                       // :236
apply_ammo_effects( nullptr, p, type->ammo->ammo_effects, 1 );                                   // item_gun_tool_ammo.cpp:3590
Character *attacker = d.actor( is_npc )->get_character(); ... attacker->get_wielded_item();      // npctalk.cpp:5948
```

**Fix:** In `apply_ammo_effects`, build `dialogue d( source ? get_talker_for( source ) : nullptr, ... )`. When source is null, cast spells through the existing caster-less `spell::cast_all_effects( const tripoint_bub_ms & )` (magic.cpp:2234), and skip `make_sound( p, *source )` or give it a position-only form. In `u_explosion`, pass `get_creature()` as a nullable source and fix the int branch to `jo_explosion.get_int( "shrapnel" )`. Guard `u_ranged_attack` against a null attacker and an empty weapon, and document it, e.g. `{ "u_ranged_attack": { "target_var": ..., "gun_mode": ..., "shots": ..., "ignore_ammo": ... } }`.

### CMBT-B02 · Technique tech_effects without a `"condition"` are never applied (33 of 142 in data are dead)
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/melee.cpp:1776`, `src/melee.cpp:1777`, `src/martialarts.cpp:248`, `src/martialarts.h:158`, `doc/JSON/MARTIALART_JSON.md:196`, `data/json/techniques.json:83`

`perform_technique` applies an effect only when `eff.has_condition && eff.condition( d )`. `has_condition` defaults to false and is set only when the JSON has a `"condition"` member, although the docs say every field except `id` is optional. Of the 142 tech_effects in core and mods, 33 have no condition: 17 `disarmed`, 7 `downed`, 7 `bleed`, plus `maimed_arm` and `effect_hungry_roots`. Characters are disarmed by a separate path, but monsters can only be disarmed through the `disarmed` effect (monster.cpp:2035). So every disarm technique (DEF_DISARM, WHIP_DISARM, tec_judo_disarm, tec_brawl_disarm_*, ...) does nothing against monsters. Separately, `on_damage` tests the pre-armor `di.total_damage()`, because this runs before `deal_melee_hit`. An effect therefore still applies when armor or a block absorbs everything, although the docs say it means "the attack did damage".

```cpp
if( x_in_y( eff.chance, 100 ) && ( di.total_damage() != 0 || !eff.on_damage ) ) {
    if( eff.has_condition && eff.condition( d ) ) {
        t.add_effect( ... );
```

**Fix:** Use `if( !eff.has_condition || eff.condition( d ) )`, as effect_on_condition.cpp:370 does. Either evaluate `on_damage` against the damage actually dealt, after `deal_melee_hit`, or add an explicit `"on_damage_dealt": true`. Add a unit test that applies a tech_effect with no condition.

### CMBT-B03 · `MELEE_DAMAGE` enchantment never modifies melee attacks against creatures
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/melee.cpp:539`, `src/melee.cpp:550`, `src/melee.cpp:553`, `src/creature.cpp:1090`, `src/magic_enchantment.cpp:1329`

`modify_damage_dealt_with_enchantments` runs on every melee hit, but it applies only the per-damage-type `melee_damage_bonus` to units already in the attack. The generic `MELEE_DAMAGE` value is read only in the branch for damage types missing from the attack, and only if that type also has a non-zero per-type bonus. Even then it is added once per such type, and a multiply-only value gives `0 * (1 + m) = 0`. Its only other uses are terrain smashing (character.cpp:3080/3097). MAGIC.md documents it as "Adds damage to melee attacks", and mods use it 46 times, e.g. Magiclysm `{ "value": "MELEE_DAMAGE", "multiply": 0.6 }` and the shapeshifter `multiply -1`. None of them change creature melee damage.

```cpp
for( damage_unit du : dam ) { du.amount = enchantment_cache->modify_melee_damage( du.type, du.amount ); ... }
...
double dmg_mod = enchantment_cache->modify_melee_damage( dt.id, 0.0f );
if( dmg_mod != 0 ) {
    modified.add_damage( dt.id, dmg_mod );
    modified.add_damage( dt.id, enchantment_cache->modify_value( enchant_vals::mod::MELEE_DAMAGE, 0.0f ) );
}
```

**Fix:** Apply `MELEE_DAMAGE` once to the whole instance after the per-type bonuses. Scale each unit by `(1 + mult)`, and add the flat amount to the dominant or physical type (or as bash). Remove the per-missing-type add.

### CMBT-B04 · Weakpoint sets are merged unsorted, so rare weakpoints get harder to hit as skill rises
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monstergenerator.cpp:454`, `src/monstergenerator.cpp:472`, `src/weakpoint.cpp:576`, `src/weakpoint.cpp:689`, `src/weakpoint.cpp:752`

`select_weakpoint` relies on list order. Earlier entries are boosted by `reweigh()` with `rolls = 1 + skill / 2.5`, so the list must be sorted by ascending coverage, and `weakpoints::load` does sort it. Monster finalization, however, concatenates weakpoint sets and the inline list through `add_from_set`, which appends or replaces in place and never re-sorts. With the common `["wps_animal_quadruped", "wps_vital_organs", "wps_eyes"]`, the eye ends up last. At skill 10 its chance falls to about 0.4%, against about 4.7% with a sorted list. Higher skill lowers the chance to hit eyes and brain, the opposite of the intent. 125 core monsters combine sets this way.

```cpp
std::sort( weakpoint_list..., a.coverage < b.coverage );   // weakpoint.cpp:689, load() only
if( replace_id && iter != end ) weakpoint_list[...] = wp; else weakpoint_list.emplace_back( wp );
```

**Fix:** After merging (at the end of `add_from_set`, or in `weakpoints::finalize`), stable-sort `weakpoint_list` by coverage the same way `load()` does.

### CMBT-B05 · `knockback_follow` is dead: the condition compares a variable to its own copy, and the branch is avatar-only
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/melee.cpp:1745`, `src/melee.cpp:1849`, `src/melee.cpp:1873`, `src/melee.cpp:1879`, `src/melee.cpp:1880`

`prev_pos` is a copy of the const local `t_pos`, captured before the knockback, so `t_pos != prev_pos` is never true and the attacker never follows. The documented field (MARTIALART_JSON.md:158) is used by 2 core and 2 Xedra_Evolved techniques. The branch also calls `g->place_player` and `player_in_control( get_avatar() )`, so fixing only the comparison would teleport the player whenever an NPC lands the technique.

```cpp
const tripoint_bub_ms t_pos = t.pos_bub( here );   // 1745
const tripoint_bub_ms prev_pos = t_pos;            // 1849
if( t_pos != prev_pos ) { g->place_player( prev_pos ); ... }   // 1879
```

**Fix:** Compare against the live position with `t.pos_bub( here ) != prev_pos`. Move non-avatar attackers with `setpos`/`move_to`, and use `player_in_control( here, *this )`.

### CMBT-B06 · Melee from any moving vehicle always misses: velocity (mph*100) is subtracted from to-hit
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/melee.cpp:420`, `src/melee.cpp:421`, `src/melee.cpp:427`, `src/melee.cpp:2965`, `src/vehicle.h:2524`, `src/vehicle_move.cpp:1765`

`hit_roll` subtracts `|forward_velocity()|` straight from accuracy, but vehicle velocity is stored in hundredths of mph. At 1 mph accuracy drops by 100, which `melee_hit_range` scales x5. The other modifiers here are -2 to -10. A player or NPC swinging from a car moving at walking pace always misses, even with the +40 bonus against immobile targets.

```cpp
hit -= std::abs( boarded_vehicle.forward_velocity() );
// vehicle.h:2523-2524: // vehicle current velocity, mph * 100
```

**Fix:** Convert the units, e.g. `hit -= std::abs( forward_velocity() ) / 100.0f`, and possibly cap it. Consider exempting stationary vehicles from the flat -10 (see the existing TODO).

### CMBT-B07 · Prone PSEUDOPOD_GRASP attackers deal 0.24x damage, less than normal characters (missing `else`)
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/melee.cpp:406`, `src/melee.cpp:831`, `src/melee.cpp:835`, `src/melee.cpp:2715`

Every sibling check gives PSEUDOPOD_GRASP a smaller prone penalty: -2 vs -8 to hit, 1.5x vs 4x attack speed, and no stamina malus. The damage block is missing an `else`, so these characters get 0.8 and then 0.3, for 0.24x total, while unmutated characters get 0.3x.

```cpp
if( is_on_ground() ) {
    if( has_flag( json_flag_PSEUDOPOD_GRASP ) ) { d.mult_damage( 0.8 ); }
    d.mult_damage( 0.3 );
}
```

**Fix:** Make `d.mult_damage( 0.3 )` the `else` branch.

### CMBT-B08 · NPC reach attacks show the player a modal "accidental injury" prompt that can veto them
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/melee.cpp:1052`, `src/melee.cpp:1054`, `src/npc_attack.cpp:276`, `src/npc_attack.cpp:284`

`Character::reach_attack` is shared with NPCs, and when a pet or player-friendly NPC is in the path it calls `query_yn` with no `is_avatar()` check. `npc_attack` aborts only when the attacker itself is friendly to the interceptor, so a hostile bandit spearing past the player's dog shows the player "Your attack may cause accidental injury, continue?". Answering N cancels the NPC's attack at no move cost.

```cpp
if( inter->has_effect( effect_pet ) || ( inter->is_npc() && inter->as_npc()->is_friendly( get_player_character() ) ) ) {
    if( query_yn( _( "Your attack may cause accidental injury, continue?" ) ) ) { ... } else { return; } }
```

**Fix:** Show the prompt only when `is_avatar()`. NPCs should decide from their own `attitude_to( *inter )`: either abort with moves spent, or accept the interception without UI.

### CMBT-B09 · Disarm techniques strip NO_UNWIELD bionic weapons and handcuffs
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/melee.cpp:1543`, `src/melee.cpp:1904`, `src/melee.cpp:1916`, `src/melee.cpp:1917`, `src/character_inventory.cpp:1529`, `src/bionics.cpp:1623`

The `disarms` branch of `perform_technique` calls `remove_weapon()` on any armed Character, with no NO_UNWIELD, INTEGRATED or bionic check. `evaluate_technique` only checks `has_weapon()`. When an NPC lands tec_judo_disarm on a player holding a deployed bionic blade, the blade drops as a loose item. `process_bionic` then reports "Wielded weapon doesn't match" and force-deactivates the bionic. Disarming someone in e-handcuffs frees them. The `take_weapon` branch is effectively guarded, because `wield()` -> `can_wield` refuses NO_UNWIELD items, but it still prints "disarms you and takes your weapon".

```cpp
if( technique.disarms && you != nullptr && you->is_armed() && !you->is_hallucination() ) {
    item weap = you->remove_weapon();
    here.add_item_or_charges( you->pos_bub(), weap ); ...
```

**Fix:** In `evaluate_technique`, reject `disarms`/`take_weapon` when the target's weapon has NO_UNWIELD or INTEGRATED, or the target `is_using_bionic_weapon()`. Alternatively, route the disarm through the normal unwield path, which deactivates the bionic.

### CMBT-B10 · Melee hit/dodge roll happens before the cancel prompt; cancelling still triggers the enemy's dodge counter and buffs
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/melee.cpp:635`, `src/melee.cpp:638`, `src/melee.cpp:669`, `src/melee.cpp:674`, `src/creature.cpp:1052`, `src/creature.cpp:1053`, `src/creature.cpp:1072`

`melee_attack_abstract` calls `t.deal_melee_attack` and adds `hit_by_player` before the "too unwieldy" return and the avatar's "this will take a long time" YES/NO popup. `deal_melee_attack` uses up the target's dodge, and on a miss it calls `on_dodge`, which trains dodge, fires ondodge buffs and EOCs, and can launch a counter-attack. If the player answers NO, no attack happens, but the target still dodges, gains buffs and may counter. Answering NO costs no moves, so the player can repeat it to drain the target's dodges.

```cpp
int hit_spread = t.deal_melee_attack( this, hit_roll() );            // 635
if( cur_weap.attack_time( *this ) > move_cost * 20 ) { ... return false; }   // 669
if( is_avatar() && move_cost > 300 && ... ) { ... if( action == "NO" ) { return false; } }
```

**Fix:** Move the unwieldy check and the confirmation popup, together with the attack-speed computation they need, above `deal_melee_attack` and the `hit_by_player` effect.

### CMBT-B11 · AOE technique targeting is player-relative; the "enemies hit" message always says 0 and goes to the victim
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/melee.cpp:1620`, `src/melee.cpp:1630`, `src/melee.cpp:1652`, `src/melee.cpp:1689`, `src/melee.cpp:1948`, `src/melee.cpp:1953`

`valid_aoe_technique` picks secondary victims with `mon->friendly == 0` and `npc->is_enemy()`. Both mean "hostile to the avatar", whoever is attacking. `creature_at<npc>` also skips the avatar. A hostile NPC's wide or spin attack therefore hits other hostile NPCs and never the player, pets or followers. In `perform_technique`, `count_hit` is never incremented, and the message is sent through `t.add_msg_if_player`. A player using AOE sees nothing, and a player hit by an NPC's AOE sees "0 enemies hit!". (The data-format side of `aoe` is CMBT-J02, and the impale geometry is CMBT-B17.)

```cpp
if( mon_l && mon_l->friendly == 0 ) targets.push_back( mon_l );
int count_hit = 0; for( Creature *const c : targets ) { melee_attack( *c, false ); }
t.add_msg_if_player( m_good, n_gettext( "%d enemy hit!", "%d enemies hit!", count_hit ), count_hit );
```

**Fix:** Look up candidates with `creature_at<Creature>` and filter with `attitude_to( *c ) == Attitude::HOSTILE` from the attacker's side. Count the `melee_attack` calls that succeed, and print the message on the attacker with `add_msg_player_or_npc`.

### CMBT-B12 · HARDTOHIT does nothing against melee: "two rolls" of a deterministic value
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/melee.cpp:1213`, `src/melee.cpp:1273`, `src/melee.cpp:1275`, `src/creature.cpp:1052`, `src/creature.cpp:2520`

JSON_FLAGS.md:411 says HARDTOHIT rolls the RNG twice and keeps the better result. `Character::dodge_roll` implements it as `max( get_dodge(), get_dodge() )`, but `get_dodge()` contains no RNG, so both calls return the same value. In melee, the only randomness is the attacker's hit roll, so HARDTOHIT has no effect against melee from characters or monsters. Only the projectile path (ballistics.cpp:274-282) actually re-rolls. Magiclysm, Xedra, MoM and Aftershock effects that grant the flag are affected.

```cpp
if( has_flag( json_flag_HARDTOHIT ) ) {
    // two chances at rng!
    return std::max( get_dodge(), get_dodge() ) * 5;
}
```

**Fix:** In `Creature::deal_melee_attack`, when the target has HARDTOHIT, roll the attacker's hit twice and keep the lower result.

### CMBT-B13 · Character melee fires `hit_you_effect` on misses; ranged and thrown attacks never fire it
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/melee.cpp:718`, `src/melee.cpp:953`, `src/melee.cpp:956`, `src/character.cpp:5132`, `src/monster.cpp:2402`, `doc/JSON/MAGIC.md:751`

`did_hit( t )`, which casts every enchantment `hit_you_effect`, is called after the whole `if( !hits ) {...} else {...}` block. It therefore fires on misses, dodged attacks and hallucination targets. Monsters cast it only when `hitspread >= 0`. `hit_you_effect` is used 34-36 times across 20 mod files, so every whiffed swing procs relics. `did_hit` has no other call site, so guns and thrown weapons never trigger it. MAGIC.md promises only "when you melee_attack a creature", so the ranged part is a missing feature rather than a bug. Items also have no per-item on-hit or on-fire EOC field. The trailing `on_hit` call at :954-957 passes a non-null `&dp`, so `Character::on_hit` returns immediately and that call is dead.

```cpp
check_dead_state( &here );
did_hit( t );                       // melee.cpp:953, outside the hit/miss branches
// monster.cpp:2402
if( hitspread >= 0 && !is_hallucination() ) { enchantment_cache->cast_hit_you( *this, target ); }
```

**Fix:** Move `did_hit( t )` into the hit branch, after `deal_melee_hit` and only for non-hallucination targets. Add ranged support, e.g. a `ranged_hit_you_effect` or `"trigger": [ "melee", "ranged", "thrown" ]` on each fake_spell, called from `fire_gun`/`throw_item` for each creature hit. Optionally add item fields `"melee_hit_eocs"` and `"fire_eocs"` (alpha = wielder, beta = target).

### CMBT-B14 · Melee weakpoint effects lose the attacker: no kill credit, no EOC alpha, no message
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/creature.cpp:1109`, `src/creature.cpp:1603`, `src/creature.cpp:1636`, `src/melee.cpp:873`, `src/weakpoint.cpp:309`, `src/weakpoint.cpp:314`, `src/weakpoint.cpp:320`, `src/weakpoint.cpp:323`

For melee (`accuracy == -1`), `Creature::deal_damage` fills in source and target only on a local `attack_copy`, then passes the original `attack` to `wkpt->apply_effects`. Melee builds that struct without a source, both for characters and for monsters (monster.cpp:2282). As a result, melee weakpoint effects:
- get an empty effect source
- run their EOCs with a null alpha
- call `die( nullptr )` on instant death, so the kill is not credited to the attacker
- never show their message (e.g. "The %s is knocked down!"), which requires the source to be the avatar

Projectiles set the source and work correctly.

```cpp
weakpoint_attack attack_copy = attack; if( attack.accuracy == -1.0 ) { attack_copy.source = source; ... }
if( wkpt != nullptr ) { wkpt->apply_effects( *this, total_damage, attack );   // creature.cpp:1636
```

**Fix:** Pass `attack_copy` to `apply_effects`. Also set `attack.source = this` in `melee_attack_abstract` and `perform_special_attacks`.

### CMBT-B15 · NPC weapon evaluation: `gun_value` double-counts loaded ammo; `melee_value` checks the wrong item for POLEARM
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/ranged.cpp:2750`, `src/ranged.cpp:2755`, `src/ranged.cpp:2757`, `src/ranged.cpp:2759`, `src/ranged.cpp:2760`, `src/item_gun_tool_ammo.cpp:1204`, `src/item_gun_tool_ammo.cpp:1258`, `src/melee.cpp:2810`

`weap.gun_damage()` defaults to `with_ammo = true`, so it already includes the loaded round's damage, and `gun_value` then adds `def_ammo.damage` again. `get_weapon_dispersion( tmp )` already includes the scaled ammo dispersion, and the raw `def_ammo.dispersion` is added on top. Loaded guns therefore score about twice their damage, with ammo dispersion counted twice. `melee_value` tests the member `weapon` (the currently wielded item) for POLEARM instead of the candidate `weap`. An NPC wielding a glaive applies the 0.8 penalty to every candidate, and otherwise no candidate polearm is ever penalised.

```cpp
damage_instance gun_damage = weap.gun_damage();   // with_ammo = true
if( def_ammo_i != nullptr && def_ammo_i->ammo ) { gun_damage.add( def_ammo.damage ); total_dispersion += def_ammo.dispersion; }
if( weapon.has_flag( flag_POLEARM ) ) { my_value *= 0.8; }   // melee.cpp:2810
```

**Fix:** Use `weap.gun_damage( false )` plus the default ammo's damage (through `gun_damage( ammo_type )`, so barrel length is respected), and drop the extra raw dispersion. Use `weap.has_flag( flag_POLEARM )`.

### CMBT-B16 · Martial-art buff to-hit ignores stack count; int accessors truncate fractional scaled bonuses
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/martialarts.cpp:1044`, `src/martialarts.cpp:1052`, `src/martialarts.cpp:1120`, `src/martialarts.cpp:1889`, `src/martialarts.cpp:1893`

Every `mabuff_*` accumulator multiplies by `d.get_intensity()` except `mabuff_tohit_bonus`. The docs (MARTIALART_JSON.md:104) and the in-game "Bonus/stack:" text both say bonuses scale with intensity. 10 stacking buffs carry hit bonuses (tiger, snake, silat, zuiquan, medievalpole, brawling, biojutsu, venom_snake, 2 in MMA), and their extra stacks add no accuracy. Separately, `ma_buff::hit_bonus`, `dodge_bonus`, `block_bonus` and the other accessors return int from the float `get_flat`. Scaled bonuses are truncated before stacking: silat's `0.15 * dex` goes from 1.2 to 1 per stack, and a 0.8 dodge bonus becomes 0.

```cpp
float Character::mabuff_tohit_bonus() const { ... ret += b.hit_bonus( *this ); }
// vs: ret += d.get_intensity() * b.dodge_bonus( *this );
```

**Fix:** Multiply by `d.get_intensity()` in `mabuff_tohit_bonus`. Make the `ma_buff` bonus accessors and `mabuff_*` accumulators return float, and round only where the value is finally used.

### CMBT-B17 · "impale" AOE techniques never hit the target behind; wrong tiles, double hits, null-deref crash when forced
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/melee.cpp:574`, `src/melee.cpp:1645`, `src/melee.cpp:1646`, `src/melee.cpp:1647`, `src/melee.cpp:1806`, `src/melee.cpp:1941`, `src/melee.cpp:1948`, `src/npctalk.cpp:5936`

The impale code has four defects:
1. The left, right and behind candidates are 2 tiles from the attacker, but the secondary hit goes through `melee_attack()`, which rejects non-adjacent targets (:574). IMPALE, tec_barbaran_impale and mma_tec_desert_wind_impale therefore never hit anything behind the target.
2. `right` is copy-pasted as `(offset_b, -offset_b)` instead of `(offset_b, -offset_a)` as in "wide". For north/south attacks it selects the tile beside the attacker. For NE/SW attacks it selects the primary target's own tile, which gives a free extra full attack with moves and stamina refunded.
3. `random_entry` on an empty `targets` vector returns nullptr, and `melee_attack( *nullptr )` crashes (see the live "TODO: what if targets is empty"). Validation is skipped for forced techniques, e.g. EOC `"u_attack": "IMPALE"` with nobody around. No shipped data hits this.
4. On the same forced path, `needs_ammo` techniques dereference the ammo when the attacker is unarmed or unloaded (:1806-1810).

```cpp
tripoint_bub_ms right = t_pos + tripoint_rel_ms( offset_b[lookup], -offset_b[lookup], 0 );
// TODO: what if targets is empty
Creature *const v = random_entry( targets ); targets.clear(); targets.push_back( v );
```

**Fix:** Change `right` to `(offset_b, -offset_a)` and exclude `t_pos`. Resolve impale secondaries with a routine that does not require adjacency (e.g. `deal_melee_attack`/`deal_melee_hit` directly). Return early when `targets` is empty. In `perform_technique`, re-check `needs_ammo` and aoe validity for forced techniques.

### CMBT-B18 · `resonance_cascade` is effectively a no-op: stale 3-submap bound, non-resetting inner loop, invalid explosion factor
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/explosion.cpp:594`, `src/explosion.cpp:595`, `src/explosion.cpp:921`, `src/explosion.cpp:926`, `src/explosion.cpp:927`, `src/explosion.cpp:991`, `src/computer_session.cpp:616`

The function has three defects:
1. `endx`/`endy` are capped at `SEEX * 3 - 1 = 35`, while bubble coordinates run to 132. The cascade point is usually around x ~60, so `startx > endx` and the loops never run.
2. `for( int &j = dest.y(); ... )` is bound to `dest.y()` and never resets, so only the first column would be processed.
3. Case 19 passes the factor `rng( 0, 1 ) * rng( 0, 6 )`. A factor of 0 gives no blast, and 1-6 hits the "factor >= 1.0" debugmsg.

No vanilla data uses the `cascade` computer action, so this only affects legacy or mod content.

```cpp
int endx = p.x() + 8 >= SEEX * 3 ? SEEX * 3 - 1 : p.x() + 8;
for( int &i = dest.x(); i <= endx; i++ ) { for( int &j = dest.y(); j <= endy; j++ ) {
explosion( &player_character, dest, rng( 1, 10 ), rng( 0, 1 ) * rng( 0, 6 ), one_in( 4 ) );
```

**Fix:** Iterate `here.points_in_radius( p, 8 )` with bounds checks, and pass a sane factor (0.5-0.8) or a proper `explosion_data`.

### CMBT-B19 · `damage_instance` merge arithmetic bugs: res_mult lerps toward the wrong field, inverted barrel check, `operator==` typo
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/damage.cpp:314`, `src/damage.cpp:513`, `src/damage.cpp:518`, `src/damage.cpp:523`, `src/damage.cpp:718`, `src/mattack_actors.cpp:1052`

`damage_instance::add( unit )` handles every same-type merge, and it has several defects:
- (a) It lerps `res_mult` toward `added_du.damage_multiplier` instead of `added_du.res_mult`. In vanilla, added units almost always have both at 1, and the only repeat>1 technique is the debug `tec_dbg_sting`.
- (b) The barrel check is inverted, which is harmless in vanilla because ammo is added last.
- (c) `mult = added.dm / du.dm` divides by zero if a technique zeroed the existing unit's multiplier.
- (d) `damage_unit::operator==` compares `unconditional_damage_mult` with `other.unconditional_res_mult`. It currently has no callers.
- (e) `dealt_damage_instance::operator+=` copies `bp_hit` only when `rhs.bp_hit` is null. Its sole caller overwrites `bp_hit` right afterwards.

These are latent rather than visible today.

```cpp
du.res_mult = lerp( du.res_mult, added_du.damage_multiplier, t );
if( added_du.barrels.empty() ) { if( du.barrels.empty() ) { du.barrels = added_du.barrels; } else { debugmsg(...); } }
unconditional_damage_mult == other.unconditional_res_mult;
if( !rhs.bp_hit ) { bp_hit = rhs.bp_hit; }
```

**Fix:** Lerp toward `added_du.res_mult`. Flip the barrel condition to `!added_du.barrels.empty()`. Guard the division when `du.damage_multiplier == 0`. Fix the `operator==` field, and use `if( rhs.bp_hit )`.

### CMBT-B20 · Combat messages misattributed or phantom (NPC counters, NPC flaming weapons, missed mutation attacks)
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/melee.cpp:2233`, `src/melee.cpp:2235`, `src/melee.cpp:2269`, `src/melee.cpp:2271`, `src/melee.cpp:2310`, `src/character.cpp:1887`, `src/turret.cpp:823`

There are four message problems:
1. Block and dodge counter failures use plain `add_msg`, so when any NPC is too exhausted to counter, the log says "You try to counterattack, but you are too exhausted!".
2. FLAMING weapons wielded by NPCs call `add_msg_player_or_npc( fmt, target )`, so the log shows only the target's name.
3. `perform_special_attacks` reuses `dealt_dam` across mutation attacks, so a miss prints a hit message with the previous attack's damage.
4. `cpu.get_name() = ...` assigns to a temporary and does nothing.

```cpp
add_msg( m_bad, _( "You try to counterattack, but you are too exhausted!" ) );
add_msg_player_or_npc( _( "<npcname> burns %s." ), target );
cpu.get_name() = string_format( ... );
```

**Fix:** Use `add_msg_if_player` or `add_msg_player_or_npc` with both strings. Reset `dealt_dam` for each special attack, or print the message only inside the hit branch. Set `cpu.name` directly.

## 3. Performance

### CMBT-P01 · Shot ammo simulates every pellet individually (`projectile_count` up to 24155)
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/ballistics.cpp:394`, `src/ballistics.cpp:396`, `src/ballistics.cpp:399`, `src/ballistics.cpp:421`, `src/ballistics.cpp:449`, `src/ballistics.cpp:625`, `src/ballistics.cpp:668`, `src/ballistics.cpp:670`, `src/map.cpp:5179`, `src/ranged.cpp:2445`, `data/json/items/ammo/exodii.json:146`, `data/json/items/ammo/40x46mm.json:154`, `data/json/items/ammo/20shot.json:112`

`projectile_attack` loops once per pellet. Each iteration:
- deep-copies the projectile and the trajectory
- runs `find_clear_path` when spread applies
- walks every tile, calling `creature_at` and `map::shoot` (which allocates a `std::map` on each call)
- calls `drop_or_embed_projectile` and `apply_ammo_effects`

The pellet count is copied from the ammo without a cap. 27.3x110 flockshot has 24155 pellets over about 11 tiles, which is about 265k tile steps per shot and a stall of hundreds of ms or more. 100-528 pellets is common for 12ga, .410, 20ga and 40mm loads. This applies to the avatar, NPCs and turrets.

```cpp
for( int j = 0; j < proj.count; ++j ) {        // ballistics.cpp:394
    proj = proj_arg;
    std::vector<tripoint_bub_ms> t_copy = trajectory;
    ... double it = here->shoot( tp, proj, ... );
```

**Fix:** Simulate at most K pellet groups (e.g. 16-32), each carrying `count / K` of the shot impact, or sample hit counts binomially along a few spread rays. Hoist the invariants: reset only the mutable fields instead of `proj = proj_arg`, reuse one trajectory buffer, and call `apply_ammo_effects` once per distinct end tile.

### CMBT-P02 · Shrapnel runs a full `deal_projectile_attack` for every fragment hit (thousands per creature)
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/explosion.cpp:433`, `src/explosion.cpp:452`, `src/explosion.cpp:457`, `src/explosion.cpp:474`, `src/explosion.cpp:485`, `src/explosion.cpp:498`, `src/explosion.cpp:500`, `src/explosion.cpp:1080`, `src/creature.cpp:1465`

`shrapnel()` draws the number of hits from Poisson(cloud density) and calls `deal_projectile_attack` once per hit. Each call does a dodge check, weakpoint and bodypart selection, and an armor walk with durability rolls. Fragment count is `casing_mass / fragment_mass`, and real content reaches 10-19k fragments, so a creature next to the blast resolves about that many hits. The loop stops only when the target dies, so armored targets pay the full count. Every shrapnel explosion also rebuilds the obstacle cache for the whole z-level and scans every tile, as the TODO admits.

```cpp
std::poisson_distribution<> d( cloud.density ); int hits = d( rng_get_engine() );
for( int i = 0; i < hits; ++i ) { ... critter->deal_projectile_attack( m, mutable_source, frag, ... );
// TODO: Calculate range based on max effective range for projectiles.
```

**Fix:** Resolve up to N (8-16) hits individually, then apply the remainder as one aggregated damage instance scaled by `remaining / N`, or batch the hits per bodypart. Limit the obstacle cache and the result scan to the explosion's expected range.

### CMBT-P03 · Every `Character::has_flag` miss re-parses martial-art buff effect ids (substr + string_id intern + factory lookup)
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/martialarts.cpp:1010`, `src/martialarts.cpp:1015`, `src/martialarts.cpp:1022`, `src/martialarts.cpp:1178`, `src/martialarts.cpp:1862`, `src/martialarts.cpp:1874`, `src/martialarts.cpp:1991`, `src/martialarts.cpp:1998`, `src/martialarts.cpp:2033`, `src/character.cpp:7560`, `src/avatar.cpp:1178`, `src/player_hardcoded_effects.cpp:1137`

`Character::has_flag` falls through to `has_mabuff_flag` on every miss. For each active mabuff effect, `ma_buff::from_effect` allocates `id.substr( 7 )`, interns a new `mabuff_id`, and does a factory lookup through `.obj()`. The same parse runs in every `mabuff_*` getter, in `has_mabuff`, and in `hardcoded_effects`. `get_effect_id` rebuilds and interns `"mabuff:" + id` every time a buff is applied, and static buffs are applied every turn. The cost is one allocation and two hash lookups per active buff per call, for every martial-arts user.

```cpp
return &mabuff_id( id.substr( 7 ) ).obj();                    // martialarts.cpp:1022
return efftype_id( std::string( "mabuff:" ) + id.str() );     // :1012
```

**Fix:** Store a `const ma_buff *` on the registered effect_type at `finalize_martial_arts`, and cache `eff_id` inside `ma_buff` at load. Implement `has_mabuff( id )` as `has_effect( id->eff_id )`. Optionally keep a per-character set of active buff flags, refreshed when effects change.

### CMBT-P04 · Armor mitigation computes resistances for every damage type to use one
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/damage.cpp:735`, `src/damage.cpp:745`, `src/item_armor.cpp:1218`, `src/item_armor.cpp:1230`, `src/item_degrade.cpp:664`, `src/item_degrade.cpp:698`, `src/character_armor.cpp:246`, `src/character_armor.cpp:284`

`item::mitigate_damage` runs per armor layer, per damage unit, per hit. It builds a full `resistances` object, which calls `item::resist()` for all 10-25 damage types and fills an unordered_map, then reads only `du.type`. Each `resist()` call also copies the material vector and draws an RNG roll per material, so the waste includes RNG draws as well as CPU time. The cost multiplies with shrapnel, pellets and blast bodyparts.

```cpp
for( const damage_type &dam : damage_type::get_all() ) { set_resist( dam.id, armor.resist( dam.id, to_self, bp, roll ) ); }
const resistances res = resistances( *this, false, roll, bp ); const float mitigation = res.get_effective_resist( du );
```

**Fix:** Call `resist( du.type, false, bp, roll )` once and inline the effective-resist formula, keeping the `no_resist` check. This is about a 10-25x cut in resist work per hit.

### CMBT-P05 · Every explosion near the bubble edge constructs and loads a full 11x11 map, with no batching
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/explosion.cpp:1000`, `src/explosion.cpp:1017`, `src/explosion.cpp:1022`, `src/explosion.cpp:1024`, `src/explosion.cpp:1031`, `src/explosion.cpp:1033`, `src/explosion.cpp:1034`, `src/explosion.cpp:1125`, `src/map.cpp:353`, `src/map.cpp:8956`

When an explosion's safe range crosses the bubble edge, `process_explosions` builds a new `map`, loads all grid cells on every z-level, rebuilds vehicle caches, spawns monsters, loads NPCs and swaps the global map. The safe range is about 24 tiles at power 920 and about 38 at power 69200. Each queued explosion repeats this, so a grenade-launcher burst, artillery or cook-off costs tens to hundreds of ms per explosion, even when one loaded map would cover them all.

```cpp
if( bubble_pos.x() - safe_range < 0 || ... ) { map m; ... m.load( origo, true, false ); swap_map swap( m );
    m.spawn_monsters( true, true ); g->load_npcs( &m ); ... _make_explosion( &m, ... );
```

**Fix:** Group off-edge explosions by origin submap and process every explosion that fits in one loaded map before loading the next. Optionally size the temporary map to the safe range.

### CMBT-P06 · Idle auto-turrets redo heavy queries every turn (item copies, `fuels_left` scans, linear `index_of_part`, `gun_all_modes`)
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/vehicle.cpp:3548`, `src/vehicle.cpp:6775`, `src/vehicle.cpp:8288`, `src/turret.cpp:81`, `src/turret.cpp:136`, `src/turret.cpp:208`, `src/turret.cpp:232`, `src/turret.cpp:256`, `src/turret.cpp:388`, `src/turret.cpp:405`, `src/turret.cpp:409`, `src/turret.cpp:773`, `src/turret.cpp:780`, `src/turret.cpp:784`, `src/turret.cpp:785`, `src/projectile.cpp:247`

`vehicle::idle` calls `automatic_fire_turret` for every enabled turret every turn, and nothing exits early before the target search. Each call runs:
- `query()`: `gun_current_mode` -> `gun_all_modes`, which builds a map with prefix strings, and `fuel_left( battery )`, a connected-grid search
- three `gun.base()` calls, each a linear `index_of_part`
- `range()` and `ammo_effects()`, which call `ammo_data()`/`ammo_current()` repeatedly

For tank-fed and battery turrets that adds up to about 9 full `fuels_left()` part scans per turret per turn. Multimag turrets also deep-copy the gun (only Aftershock data uses this). `max_aoe_size` walks every ammo_effect definition, which is minor.

```cpp
item gun_copy( gun ); ... vehicle::prepare_multimag_pockets( *veh, here, gun_copy, mode, ammo_current() );  // turret.cpp:392
for( size_t i = 0; i < parts.size(); i++ ) { if( &parts[i] == part )                                       // vehicle.cpp:3548
```

**Fix:** Resolve `ammo_current`, `ammo_data`, range and aoe once per call and pass them through. Cache `base()` in `turret_data`. Make `index_of_part` O(1) with `part - parts.data()` plus a bounds check. Make `max_aoe_size` iterate `tags`. Optionally skip all of this when no hostile creature is in range.

### CMBT-P07 · NPC weapon scoring copies guns and re-simulates DPS against freshly constructed test monsters for each candidate
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/npcmove.cpp:2562`, `src/npcmove.cpp:2598`, `src/npcmove.cpp:4998`, `src/npcmove.cpp:5004`, `src/npcmove.cpp:5033`, `src/melee.cpp:2802`, `src/melee.cpp:2815`, `src/ranged.cpp:2744`, `src/ranged.cpp:2751`, `src/item_gun_tool_ammo.cpp:548`, `src/character_martial_arts.cpp:134`

Every NPC combat decision runs `evaluate_best_weapon`, which scores every melee and gun item in the NPC's inventory. Only the wielded item's value is cached. `gun_value` deep-copies the gun to read its dispersion. `melee_value` -> `average_dps` constructs 3 monsters and runs `roll_all_damage` plus a resistances build for crit and non-crit hits. It then builds a colorized, translated list of known styles just to test whether it is empty. The cost scales with NPCs x weapons x combat turns. (The scoring correctness bugs are in CMBT-B15.)

```cpp
item tmp = weap; ... int total_dispersion = get_weapon_dispersion( tmp ).max() + ...;   // ranged.cpp:2751
monster test_mon = monster( comp_mon.second.mon_id );                                   // item_gun_tool_ammo.cpp:548
if( !martial_arts_data->enumerate_known_styles( weap.type->get_id() ).empty() )          // melee.cpp:2815
```

**Fix:** Compute dispersion without copying the gun. Replace the string test with `std::any_of( ma_styles, has_weapon )`. Cache melee and gun values per item for each turn. Make the DPS test monsters function-static.

### CMBT-P08 · Melee path deep-copies the wielded item 5+ times per swing
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/melee.cpp:378`, `src/melee.cpp:405`, `src/melee.cpp:635`, `src/melee.cpp:665`, `src/melee.cpp:772`, `src/melee.cpp:811`, `src/melee.cpp:1103`, `src/melee.cpp:1125`, `src/melee.cpp:2259`

The weapon is copied by value in `get_melee_hit_base` and in `hit_roll` (where the copy is unused), and `scored_crit` calls `hit_roll` again. It is also copied in `melee_attack_abstract`, in `stumble` (unused) and in the armor-vector branch. A loaded gun or a container is copied with all its contents. Each copy costs a few microseconds.

```cpp
item cur_weap = cur_weapon ? *cur_weapon : null_item_reference();   // melee.cpp:380, 405, 665, 1103
```

**Fix:** Use `const item &` or `item *`, and delete the dead copies in `hit_roll` and `stumble`. Do not reuse one hit roll for the crit check: `hit_roll` is random, so that would change the mechanics. `melee_special_effects` spills contents on the copy, so switch that call site with care.

### CMBT-P09 · Explosion blast copies a full `ter_t` per bashed tile and uses node-based `std::set`/`std::map` for the flood fill
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/explosion.cpp:214`, `src/explosion.cpp:215`, `src/explosion.cpp:216`, `src/explosion.cpp:232`, `src/explosion.cpp:299`, `src/explosion.cpp:322`, `src/explosion.cpp:339`

The post-pass copies `ter_t` by value for every bashed tile before checking for open air. That copy includes translations, a flag set, bash data and clone_ptr actors, so it allocates many times. The flood fill uses `std::set`/`std::map` keyed by tripoint, and `pow( distance_factor, d )` is recomputed in three passes. `do_blast` runs once per explosion and per-tile bash work dominates, so this is low.

```cpp
const ter_t ter_below = m->ter( below ).obj();   // explosion.cpp:299
std::set<tripoint_bub_ms> closed; std::set<tripoint_bub_ms> bashed{ p }; std::map<tripoint_bub_ms, float> dist_map;
```

**Fix:** Take `const ter_t &` inside the open-air check. Use flat per-z arrays or bitsets for the flood fill, and store the computed force per tile so `pow` runs once.

### CMBT-P10 · `get_valid_technique` deep-copies every `ma_technique` (and returns by value) on each miss and grab check
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/martialarts.cpp:1452`, `src/martialarts.cpp:1481`, `src/martialarts.cpp:1487`, `src/martialarts.cpp:2042`, `src/melee.cpp:722`, `src/melee.cpp:1078`, `src/monattack.cpp:4053`, `src/monattack.cpp:4059`, `src/character_escape.cpp:233`

`get_miss_recovery` and `get_grab_break` rebuild the full technique list, then copy each candidate `ma_technique` by value before testing a bool. Each copy covers translations, sets, std::function conditions and tech_effects. The result is returned by value and copied again by callers. `has_grab_break_tec` does the whole scan only to compare an id, and monster grab attacks scan twice. This runs only on misses and grabs.

```cpp
ma_technique candidate = candidate_id.obj();
if( candidate.*purpose && candidate.is_valid_character( owner ) ) { return candidate; }
```

**Fix:** Use `const ma_technique &`, check the purpose flag before `is_valid_character`, and return `matec_id`.

### CMBT-P11 · `dealt_damage_instance` constructor allocates a map node per damage type and interns `"torso"` on every construction
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/creature.cpp:1602`, `src/damage.cpp:673`, `src/damage.cpp:677`, `src/damage.cpp:679`, `src/damage.h:217`, `src/melee.cpp:868`, `src/melee.cpp:869`, `src/ballistics.cpp:288`, `src/ballistics.cpp:618`

Every construction pre-fills a zero entry for each damage type (10-25 node allocations) and interns `bodypart_id( "torso" )`. That happens on every `deal_damage` call, every pellet and fragment, and per-turn field damage. `type_damage()` already returns 0 for missing keys, so the pre-fill buys nothing. It also has a behaviour side effect. ballistics.cpp:618 iterates the pre-filled map, so every damage_type's `onhit_eocs` fire on every projectile hit, even at zero damage of that type. Melee fires only the types actually present.

```cpp
for( const damage_type &dt : damage_type::get_all() ) { dealt_dams.emplace( dt.id, 0 ); }
bp_hit = bodypart_id( "torso" );
```

**Fix:** Drop the pre-fill and store only non-zero entries, in a flat vector or an array indexed by int_id. Use a function-static `torso`. This also stops zero-damage types from firing onhit EOCs on projectiles.

### CMBT-P12 · `Character::get_dodge` builds a whole `anatomy` object on every call
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/melee.cpp:1258`, `src/melee.cpp:1275`, `src/anatomy.cpp:172`, `src/npcmove.cpp:705`, `src/npcmove.cpp:804`, `src/player_display.cpp:858`

`get_dodge` constructs `anatomy( get_all_body_parts() )` only to sum `hit_size`. It runs on every dodge roll (twice for HARDTOHIT), in NPC threat evaluation for each candidate, and in the character sheet. It also scans worn items three times for roller skates. Each call costs about 1 µs.

```cpp
ret /= anatomy( get_all_body_parts() ).get_size_ratio( anatomy_human_anatomy );
```

**Fix:** Sum `hit_size` directly, or cache the ratio the way `tally_organic_size` does and invalidate it when body parts change. Merge the three `worn_with_flag` scans into one.

### CMBT-P13 · Per-shot overhead in `fire_gun`: `tname()` built for NPC/turret shots, `gun_all_modes` for a VOLLEY flag, fault set builds
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/ranged.cpp:965`, `src/ranged.cpp:974`, `src/ranged.cpp:1020`, `src/ranged.cpp:1205`, `src/ranged.cpp:1277`, `src/ranged.cpp:2561`, `src/item_gun_tool_ammo.cpp:3133`, `src/item_degrade.cpp:1098`, `src/turret.cpp:409`, `src/turret.cpp:495`

Inside the per-shot loop:
- `add_msg_if_player` evaluates `tname()` and an uppercased sound string even for NPC and turret shooters, and the result is thrown away.
- `gun_current_mode()` rebuilds `gun_all_modes` just to test the VOLLEY flag.
- `faults_potential()` builds a `std::set` before the cheap heat comparison.

There is also a side bug: the `threshold < 0` early-out is dead, because `threshold` is clamped to at least 5.0 just before it. Every gun therefore builds the fault set and writes `gun_heat` on every shot. A gun with no overheat threshold that gains heat from a gunmod would overheat at 5.

```cpp
p.add_msg_if_player( _( "You shoot your %1$s.  %2$s" ), weapon->tname( 1, segs ), uppercase_first_letter( data.sound ) );
if( gun.gun_current_mode().flags.count( "VOLLEY" ) ) {
```

**Fix:** Guard the message with `is_avatar()`. Hoist the VOLLEY check out of the loop. Compare heat before the fault check, and add an `item::can_have_fault( fault_id )` that does not build a set. Restore the no-overheat early-out by checking before the clamp.

### CMBT-P14 · Aim window recomputes aim caches and recoil predictions for each aim type on every redraw
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/ranged.cpp:2016`, `src/ranged.cpp:2054`, `src/ranged.cpp:2073`, `src/ranged.cpp:2091`, `src/ranged.cpp:4112`, `src/character.cpp:924`, `src/character.cpp:1040`

Every redraw runs `calculate_ranged_chances`. That calls `predict_recoil` up to twice per aim type, and each call rebuilds `gen_aim_mods_cache` and simulates aiming move by move. `fastest_aiming_method_speed` also recomputes iron-sight parallax through a full limb-score walk on every `aim_per_move`. This only runs on keypresses, so the cost is milliseconds of UI latency per keypress.

```cpp
const aim_mods_cache &aim_cache = you.gen_aim_mods_cache( weapon );   // inside predict_recoil, ranged.cpp:2016
```

**Fix:** Build one aim cache per `calculate_ranged_chances` call. Compute each aim type's prediction once and reuse it for both moves and steadiness. Better, simulate a single descent and record when each threshold is crossed.

### CMBT-P15 · Every melee swing rebuilds the whole enchantment cache
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/melee.cpp:632`, `src/character.cpp:4932`, `src/character_body.cpp:252`, `src/npc.cpp:3557`

`melee_attack_abstract` calls `recalculate_enchantment_cache()` unconditionally. That re-walks relics, bionics, effects and mutations, evaluates the conditions, and runs `update_cached_mutations` and `recalc_hp`. It cannot simply be removed. NPCs run `update_body` only every 10 seconds and wielding never triggers a recalc, so this call is the only refresh for WIELD-conditioned enchantments after a wield-then-attack. The saving is tens of µs per attack.

```cpp
recalculate_enchantment_cache();   // melee.cpp:632
```

**Fix:** Add a dirty flag, set on wield, wear, effect, mutation and bionic changes, and recompute only when it is set. Hooking all of those events is closer to medium effort.

## Appendix: considered and rejected

- Projectile copy always heap-allocates an `explosion_data`; `apply_ammo_effects` deep-copies each ammo_effect – the code is as described, but `explosion_data` is a small POD-like struct and this happens only a few times per shot or impact. It is hygiene, not a measurable cost, and `set_drop` already avoids the copy for null items.
