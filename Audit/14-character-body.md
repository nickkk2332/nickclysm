# 14 · Character, avatar & body

> Engine audit – static code reading at commit `a1a646d6`; nothing was compiled or run. Each finding was checked against the code by a separate reviewer: **confirmed** = verified by reading the code; **plausible** = consistent with the code, but reachability or impact not proven. Line numbers refer to that commit.

**Scope:** `src/character.cpp`, `src/avatar.cpp`, `src/avatar_action.cpp`, `src/character_attire.cpp`, `src/character_body.cpp`, `src/character_modifier.cpp`, `src/character_escape.cpp`, `src/bodypart.cpp`, `src/subbodypart.cpp`, `src/anatomy.cpp`, `src/bonuses.cpp`, `src/move_mode.cpp`, `src/grab.cpp`, `src/climbing.cpp`

## Summary

The character and body code has a lot of features, but a large share of its behaviour is still keyed to hardcoded trait, effect, item and terrain ids, even where a JSON mechanism already exists next to it. NPCs fall behind the avatar because much of the per-turn logic lives in `avatar::reset_stats`. There is one vanilla-reachable crash (wound progression), and the frostbite risk zones are inverted. Several state changes are never undone: enchantment body parts, forced move modes and the similar-bodypart maps. Most performance cost comes from work redone every turn that could be cached: `has_flag` scans, rebuilding the enchantment cache, encumbrance passes and effect mod lookups.

| Lens | Critical | High | Medium | Low | Total |
|---|---|---|---|---|---|
| Missing JSON / EOC | 0 | 1 | 10 | 6 | 17 |
| Bugs & quirks | 1 | 3 | 6 | 8 | 18 |
| Performance | 0 | 0 | 8 | 5 | 13 |
| **Total** | **1** | **4** | **24** | **19** | **48** |

**Fix first:**

1. **CHAR-B01** · Wound progression dereferences nullptr: a vanilla crash on ordinary bash hits, and the fix is a `break`.
2. **CHAR-B02** · Frostbite risk zones are inverted: core survival mechanic is backwards (blizzards are safer than calm cold), fix is swapping comparison signs.
3. **CHAR-B03** · Forced move-mode changes are reverted on the next action: broken legs cost 150+ moves and a fall message on every step; small fix.
4. **CHAR-J01** · Body parts gained or lost via effect/item enchantments are never reverted: wings and other temporary parts stay forever and are saved; small fix.
5. **CHAR-B04** · Effect stat mods, pain/hunger/thirst penalties and MA static buffs never apply to NPCs: about 229 vanilla effect stat mods do nothing to NPCs.

## 1. Missing JSON bindings & EOC features

### CHAR-J01 · Body parts gained or lost via effect/item enchantments are never reverted
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character.cpp:4996`, `src/character.cpp:4899`, `src/magic_enchantment.cpp:1369`, `src/magic_enchantment.cpp:1510`, `src/mutation.cpp:714`, `src/bionics.cpp:3014`, `src/character_body.cpp:252`, `data/mods/Xedra_Evolved/effects/effects_changeling_seasonal_magic.json:111`, `data/mods/Xedra_Evolved/mutations/temporary.json:586`

`recalculate_enchantment_cache` only rebuilds the body when the *new* cache still has `modified_bodyparts`. When an effect or worn/relic item whose enchantment had `modified_bodyparts` goes away, the new cache has none, so `recalculate_bodyparts()` is never called. Gained parts stay and lost parts never come back, and the result is saved through the serialized `body`. Only two other paths force a rebuild: losing a mutation that has enchantments, and removing a bionic. No EOC can add or remove a body part directly, so modders have to use this pattern. Xedra Evolved's "Wings of Gossamer" effect (max 12h) grants `wing_gossamer_l/r` this way. Its companion mutations have no enchantments, so the wings stay after the effect expires.

```cpp
// character.cpp:4996
if( enchantment_cache->modifies_bodyparts() ) { recalculate_bodyparts(); }
// magic_enchantment.cpp:1369
return !modified_bodyparts.empty();   // cache is cleared at the start of every recalc (1510)
```

**Fix:** Keep a `bool body_modified_by_enchant` on Character (or a hash of the last applied `body_part_set`). Rebuild when the cache modifies body parts OR the flag was set last time. Also add EOC effects with an optional duration:
```json
{ "u_add_body_part": "wing_gossamer_l", "duration": "12 h" }
{ "u_lose_body_part": "tail_stub" }
```
plus `npc_` variants.

### CHAR-J02 · can_wear hardcodes trait/material bans and duplicates restricts_gear, ignoring allowed_items
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character_attire.cpp:138`, `src/character_attire.cpp:143`, `src/character_attire.cpp:154`, `src/character_attire.cpp:170`, `src/character_attire.cpp:1960`, `src/mutation.cpp:478`, `data/json/mutations/mutations_limbs.json:50`, `data/json/mutations/mutations.json:6062`

`can_wear` hardcodes WOOLALLERGY (wool), VEGAN (seven fixed animal materials plus ANIMAL_PRODUCT) and SQUEAMISH (filthy). A modded animal material is never refused by VEGAN, and a new allergy trait cannot be made in JSON. VEGAN checks `has_own_flag(flag_ANIMAL_PRODUCT)`, which only reads per-instance tags, so an ANIMAL_PRODUCT flag set in an armor type's JSON is ignored, although `JSON_FLAGS.md:808` says vegans can't wear such items. HORNS_POINTED, ANTENNAE and ANTLERS also get a hardcoded "no rigid head item" check. Their JSON already has `restricts_gear: [head_crown]` and `allow_soft_gear`. The C++ check runs after `conflicts_with_item`, so it overrides `allowed_items`, and it is broader: it rejects any head-covering item with any rigid portion. A modded "horned helmet" therefore cannot be whitelisted. Separately, only material id `wool` keeps its warmth when wet.

```cpp
// character_attire.cpp:170
if( it.covers( body_part_head ) && ... it.is_rigid() &&
    ( has_trait( trait_HORNS_POINTED ) || has_trait( trait_ANTENNAE ) || has_trait( trait_ANTLERS ) ) )
// character_attire.cpp:1960
if( !clothing.made_of( material_wool ) )
```

**Fix:** Delete the horns/antennae/antlers branch; JSON already covers it (widen `restricts_gear` if needed). Add mutation fields and a material field:
```json
"forbidden_materials": [ "wool" ],
"forbidden_item_flags": [ "ANIMAL_PRODUCT", "FILTHY" ],
"refuse_message": "You can't wear that, it's made of %s!"
```
Add a material flag `ANIMAL_DERIVED` for VEGAN to check, and `"wet_warmth_retention": 1.0` on materials to replace the `wool` id test. Make VEGAN use `has_flag`, not `has_own_flag`.

### CHAR-J03 · Night vision and sight caps hardcoded; NIGHT_VIS enchant ignored with NV goggles
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character.cpp:2432`, `src/character.cpp:2450`, `src/character.cpp:2517`, `src/character.cpp:2530`, `src/avatar_action.cpp:175`, `src/avatar.cpp:993`

NIGHTVISION/2/3 and BIRD_EYE give hardcoded ranges (+2/+4.5/+10/+1) by trait id, although the NIGHT_VIS enchantment exists. With a GNV_EFFECT item worn or the NIGHT_VISION flag, `range += 10` *replaces* the enchantment path. Every NIGHT_VIS enchantment is then silently ignored, bonuses and penalties alike: vanilla `enchantments.json` (+8 to +14), `night_operations_serum` (+20) and 38 uses in mods. Sight caps are also hardcoded (boomered 1, shell 2, PER_SLIME 8, myopic 12, darkness 10). The SHELL2/SHELL3 "in shell" state is checked by trait id in 16 places across 7 files, instead of using the existing CANNOT_MOVE flag plus a sight cap.

```cpp
// character.cpp:2530
if( worn_with_flag( flag_GNV_EFFECT ) || has_flag( json_flag_NIGHT_VISION ) ) {
    range += 10;
} else {
    range = enchantment_cache->modify_value( enchant_vals::mod::NIGHT_VIS, range );
}
```

**Fix:** Always apply NIGHT_VIS, after the goggle bonus. Move the trait ranges to enchantments, e.g. `{ "condition": "ACTIVE", "values": [ { "value": "NIGHT_VIS", "add": 2 } ] }`. Add an enchantment value `SIGHT_MAX` (combined by min) and an effect/mutation field `"sight_max": 2`. Make SHELL2/3 grant CANNOT_MOVE and a new CANNOT_THROW flag while active.

### CHAR-J04 · Martial-art bonus scaling-stat is a closed enum; unknown names silently become constants
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/bonuses.cpp:57`, `src/bonuses.cpp:85`, `src/bonuses.cpp:162`, `src/bonuses.cpp:400`, `doc/JSON/MARTIALART_JSON.md:224`

`scaling-stat` maps strings to a fixed list of 4 stats and 20 skills keyed by short names (`bash`, `rifle`, `pistol`). Modded skills cannot be used. An unknown string maps to STAT_NULL with no error, and `get()` then returns the raw `scale`, so the bonus quietly becomes a flat constant. The doc's own examples, `bashing` and `rifles`, are not valid keys, so copying the doc produces a bonus that does not scale. Vanilla data only uses valid keys, so this affects new and modded content.

```cpp
if( iter == scaling_stat_map.end() ) { return STAT_NULL; }   // bonuses.cpp:85
case STAT_NULL: bonus = scale; break;                        // bonuses.cpp:400
```

**Fix:** Accept any `skill_id` (store a skill_id variant next to the 4 stats). Call `jo.throw_error_at( "scaling-stat", ... )` for unknown values and validate in finalize. Optionally allow `"scaling": { "math": "u_skill('melee') * 0.2" }`. Fix the doc example.

### CHAR-J05 · Body-plan JSON: anatomy parts not extendable, validation gaps
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/anatomy.cpp:58`, `src/anatomy.cpp:76`, `src/anatomy.cpp:80`, `src/anatomy.cpp:103`, `src/anatomy.cpp:111`, `src/bodypart.cpp:110`, `src/bodypart.cpp:529`, `src/bodypart.cpp:566`, `src/subbodypart.cpp:95`, `src/subbodypart.cpp:105`

`anatomy.parts` is loaded with `mandatory`, which disables `extend`/`delete` (`generic_factory.h:668`). A mod that adds one part to `human_anatomy` has to replace the whole list and will conflict with other mods. `body_part_type::check` does not validate `effects_on_hit` ids, qualities, `sub_parts`, techniques or `similar_bodypart`, and sub body parts have no `check()` at all. The same files contain two related bugs: the never-cleared similar-bodypart maps (CHAR-B07) and the copy-paste errors in `anatomy::check`/`finalize` (CHAR-B17). The reviewer rated this group medium because of the stale-state crash path in CHAR-B07.

```cpp
mandatory( jo, was_loaded, "parts", unloaded_bps );   // anatomy.cpp:58
```

**Fix:** Use `optional( jo, was_loaded, "parts", unloaded_bps, string_id_reader<body_part_type>{} )` so `"extend": { "parts": [ "tail" ] }` works, and still require the field on first load. Add validation of `effects_on_hit`, qualities, `sub_parts` and `similar_bodypart`, and add `sub_body_part_type::check`.

### CHAR-J06 · Body temperature cannot be shifted per part by effects/enchantments; key sources hardcoded
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/character_body.cpp:617`, `src/character_attire.cpp:1968`, `src/character.cpp:5643`, `src/character_body.cpp:845`, `src/character_body.cpp:134`, `src/character_body.cpp:592`, `src/math_parser_diag.cpp:1810`, `src/newcharacter.cpp:2111`

The data-driven temperature shifts are limited to mutation `bodytemp_modifiers`, BODYTEMP_SLEEP and CLIMATE_CONTROL_HEAT/CHILL. A global shift can be faked from an effect or item by having its enchantment grant a hidden mutation (`get_functioning_mutations()` includes enchantment-granted mutations). Per-part shifts are impossible. So the flu fever (+3C head) and common cold (-1.5C) are hardcoded by effect id, and the heating bionic reads the intensity of the `heating_bionic` effect. Frostbite only checks the hardcoded mouth, hands and feet, so tails, mutant feet and extra arms are immune (see also CHAR-B02). Fur/slime drying multipliers are tied to eight trait ids, and PYROMANIA fire morale is hardcoded. EOCs can read part temperature only through `warmth(bp)`, which is read-only, uses legacy units and has a misleading name. Use cases that need C++ today: a fever disease, a frost aura spell, a heated suit, an ice-elemental race.

```cpp
// character_body.cpp:617
if( bp == body_part_head && has_effect( effect_flu ) ) { mod_part_temp_conv( bp, 3_C_delta ); }
if( has_common_cold ) { mod_part_temp_conv( bp, -1.5_C_delta ); }
// character_attire.cpp:1968
total_warmth[bp] += guy.get_effect_int( effect_heating_bionic, bp );
```

**Fix:** Add enchantment value `BODYTEMP` (`{ "value": "BODYTEMP", "add": 3 }`, in C) and a per-part form `"bodytemp_modifier": [ { "part": "head", "add": "3 C" } ]`. Add effect_type `"warmth_per_intensity": 10` and migrate flu, common_cold and heating_bionic to JSON. Add a body_part flag `FROSTBITE_VULNERABLE` and a mutation field `"drying_rate_mult": 2.0`. Add math `u_body_temp('torso')` (Celsius, with an assign function), and rename or alias `warmth`.

### CHAR-J07 · Movement modes: no JSON gating, no EOC setter/event, hardcoded switch costs, doc drift
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/character.cpp:2178`, `src/character.cpp:2188`, `src/move_mode.cpp:38`, `src/move_mode.cpp:49`, `src/avatar.cpp:1306`, `src/npcmove.cpp:8357`, `src/condition.cpp:2193`, `src/npctalk.cpp:8611`, `doc/JSON/MOVE_MODE.md:115`

`move_mode` JSON defines only messages and multipliers. `can_switch_to` gates only RUNNING (stamina/legs) and steed crouch/prone. Switch costs are hardcoded constants (50/100 moves), halved by DEFT and multiplied by 1.5 for CLUMSY. A modder cannot make a `fly` mode that requires wings, even though `fly` is the example in MOVE_MODE.md, nor a serpent-only `slither` mode. No EOC effect sets the move mode, and no event fires on a mode change. `u_has_move_mode` exists but is not documented in EFFECT_ON_CONDITION.md. MOVE_MODE.md omits the `prone` move_type, calls `panel_color`/`symbol_color` mandatory (the code treats them as optional) and omits `exertion_level_animal_riding`.

```cpp
// character.cpp:2184
// Only running modes are restricted at the moment
return get_steed_type() != steed_type::NONE || mode->type() != move_mode_type::RUNNING || can_run();
const int standing_crouch_base_cost = 50; const int crouch_prone_base_cost = 100;
```

**Fix:** Add move_mode fields `"condition": { ... }` (checked in `can_switch_to`), `"switch_cost": 50`, `"switch_cost_from": { "prone": 150 }` and optional `"enchantments"`/`"flags"` that apply while in the mode. Replace DEFT/CLUMSY with an enchantment value `MOVE_MODE_SWITCH_COST`. Add EOC `{ "u_set_move_mode": "crouch", "force": false }` and event `character_changes_move_mode { character, from, to }`. Fix MOVE_MODE.md and document `u_has_move_mode`.

### CHAR-J08 · Melee retaliation traits hardcoded; hit_me_effect skips ranged hits on characters
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/character.cpp:5141`, `src/character.cpp:5173`, `src/character.cpp:5192`, `src/character.cpp:5210`, `src/character.cpp:5259`, `src/monster.cpp:4080`, `src/event.h:495`, `data/json/mutations/mutations_skin.json:444`

SPINES/QUILLS (stab 1-8 / 1-20), THORNS (cut 1-4, unarmed attackers only) and CF_HAIR (stun and blind) are C++ branches on trait ids, with fixed damage, damage types, messages and the "uncovered body part" rule. The JSON alternative, enchantment `hit_me_effect`, gets no hit body part, so it cannot express the uncovered rule. For characters it never fires on projectile hits, because `Character::on_hit` returns early when `proj != nullptr`, while `monster::on_hit` always casts it. MAGIC.md does not mention this asymmetry. SPINES/QUILLS descriptions say "unarmed opponent", but only THORNS checks that. `character_takes_damage` carries no attacker or damage type, so EOC-based retaliation is not possible either.

```cpp
if( source == nullptr || proj != nullptr ) { return; }        // character.cpp:5141
...
enchantment_cache->cast_hit_me( *this, source );             // character.cpp:5259
```

**Fix:** Add a mutation/enchantment field and migrate the four traits:
```json
"retaliation": [ { "damage": { "damage_type": "stab", "amount": [ 1, 20 ] }, "requires_uncovered": true,
  "attacker_unarmed_only": false, "melee_only": true, "effects": [ { "id": "stunned", "duration": 2 } ],
  "message_u": "Your quills puncture %s!", "message_npc": "..." } ]
```
Give `hit_me_effect` optional `"condition"` and `"melee_only"`, with context vars `hit_bodypart`/`ranged`. Add `attacker` and `damage_type` to `character_takes_damage`.

### CHAR-J09 · Cardiovascular model is dead code: BP/heart-rate/respiration effect fields do nothing
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/effect.cpp:527`, `src/character.cpp:6902`, `src/character.cpp:6904`, `src/character.cpp:6919`, `src/character_body.cpp:1390`, `src/character_body.cpp:1528`, `src/character_body.cpp:1545`, `src/character_body.cpp:1565`, `src/character_health.cpp:1542`, `src/character_health.cpp:3174`, `src/character.h:4375`, `src/iuse.cpp:3932`

EFFECTS_JSON.md documents `blood_pressure_*`, `heart_rate_*` and `respiratory_rate_*`, but none affect gameplay. (1) effect.cpp registers respiratory data under `"RESPIRATORY_RATE"`, while `process_one_effect` reads `"RESPIRATION_RATE"`, so it is always 0. (2) `update_respiration_rate()` and `get_respiration_rate()` have no callers. (3) `circulation` and `circulation_resistance` are computed every minute but read by nothing, so blood pressure has no effect. (4) `heart_rate_index` only feeds `heartrate_bpm()`, whose only caller is one item's readout. Nicotine, adrenaline and beta-blocker heart-rate changes are hardcoded by effect id even though `heart_rate_*` exists. The BP/HR/RR branches also do not reset `mod`, so their `*_chance` rolls use the PKILL opiate-addiction multiplier left over from the previous branch. `heart_rate_effect_mod`, `bp_effect_mod` and `resp_rate_effect_mod` are not serialized. No EOC can read these values. Vanilla effects and serum EOCs still set these fields, expecting them to work.

```cpp
extract_effect( to_extract, "RESPIRATORY_RATE", { ... } );   // effect.cpp:527
val = get_effect( "RESPIRATION_RATE", reduced );             // character.cpp:6919
```

**Fix:** Use one key constant. Either remove the dead fields from the docs, or make them matter: call `update_respiration_rate()`, have circulation affect stamina or limb scores, reset `mod = 1` before each block, and serialize the three mods. Add read-only math `u_heart_rate()`, `u_blood_pressure()`, `u_respiration_rate()`, and an event such as `character_heart_rate_critical`. Migrate adrenaline/betablock/cig to `heart_rate_*` JSON.

### CHAR-J10 · Body-state EOC gaps: per-part temperature/wetness unwritable, no fall/climb/NPC-movement events
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/math_parser_diag.cpp:1811`, `src/math_parser_diag.cpp:1971`, `src/math_parser_diag.cpp:1901`, `src/math_parser_diag.cpp:1923`, `src/npctalk.cpp:5703`, `src/weather.cpp:387`, `src/condition.cpp:2449`, `src/event.h:24`, `src/avatar_action.cpp:712`

Missing reads: per-part wetness, frostbite timer, heart rate. `warmth(bp)` is read-only (and actually returns temp_cur in legacy 1/100 units). The only wetness write is `u_add_wet`, which has no bodypart argument, and it usually does nothing: `wet_character` returns early unless `calendar::once_every(6_seconds)` holds, then rolls `rng(0, 100-amount+warmth) > 10`. Missing events: falling a Z-level, climbing, and any NPC movement (only `avatar_moves` exists). Grab, mount and body-part changes can already be observed through `character_gains_effect`/`loses_effect` on the grab and `riding` effects and through `gains_mutation`. Read-only `encumbrance` and `hp_max` are derived values and not real gaps. Use cases: wading that wets only the legs, an ankle injury roll on falling.

```cpp
{ "warmth", { "un", 1, warmth_eval } },        // math_parser_diag.cpp:1971, no assign
wet_character( *target, dov.evaluate( d ) );   // npctalk.cpp:5707, no bodypart
```

**Fix:** Add `u_wetness('bp')` (read and assign), `u_body_temp('bp')` (assign, Celsius), `u_frostbite('bp')`. Give `u_add_wet` `bodypart` and `amount` options, and make it deterministic for EOC callers. Add events `character_falls { character, height }`, `character_climbs { character, aid, direction }` and a character-generic movement event.

### CHAR-J11 · Climbing aids are down-only; climbing up and its aids are hardcoded
**Severity:** Medium · **Effort:** Large · **Verified:** confirmed
**Where:** `src/climbing.h:157`, `src/climbing.h:186`, `src/climbing.cpp:201`, `src/climbing.cpp:355`, `src/game.cpp:9462`, `src/game.cpp:9469`, `src/game.cpp:9493`, `src/game.cpp:9495`, `src/game.cpp:10165`

`climbing_aid` JSON has only a `down` block. Climbing up uses the hardcoded `furn_CLIMBABLE` and `ability_WALL_CLING` aids with hardcoded costs (`cost == 0 ? 1000 : cost + 500`, `burn_energy_all(-750)`). Climb-up abilities can come from JSON through the WALL_CLING, CLIMB_NO_LADDER and CLIMB_FLYING character flags, but item- or aid-based ascent and custom costs cannot. Conditions are limited to one flag, trait, item or ter_furn, and there is no dialogue `condition`. The `veh` condition ignores its flag (a TODO in the code). CLIMBING.md says `max_height: 0` disables an aid, but `enabled()` returns `max_height >= 0`, so such an aid still needs menu text, stays listed, and drops you the full height. `find_or_make_stairs` has a separate hardcoded grapnel/rope_30/vine descent path that skips `climbing_aid` entirely.

```cpp
bool enabled() const noexcept { return max_height >= 0; }   // climbing.h
climbing_aid = climbing_aid_furn_CLIMBABLE;                  // game.cpp:9462
climbing_aid = climbing_aid_ability_WALL_CLING;              // game.cpp:9493
```

**Fix:** Add an `"up": { "max_height": 1, "cost": { ... }, "move_cost": 500, "menu_text": "...", "msg_after": "..." }` block, and make `game::vertical_move` pick the safest matching aid via `climbing_aid::list` in both directions. Add a condition `{ "type": "condition", "condition": { ... } }` and an `"eoc"` run after a successful climb. Change `enabled()` to `max_height > 0`, skip disabled aids in `list()`, and move the grapnel/rope/vine path into JSON aids.

### CHAR-J12 · Floor/bedding warmth eligibility is a hardcoded list of 17 activity ids
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character.cpp:5553`, `src/activity_type.cpp:89`, `src/character_body.cpp:452`, `src/character.cpp:5634`, `src/npc_arbitration.cpp:155`

`can_use_floor_warmth` compares the current activity against 17 hardcoded ids. `activity_type` JSON has no matching field, so JSON-only resting activities (e.g. MindOverMatter's `ACT_PSI_*` meditation, run via `do_turn_eoc`) get no bedding, floor or BODYTEMP_SLEEP warmth. `npc_arbitration.cpp:155-158` duplicates the list and adds ACT_CRAFT.

```cpp
return in_sleep_state() || has_activity( ACT_WAIT ) || has_activity( ACT_WAIT_FOLLOWERS ) || ...
```

**Fix:** Add activity_type field `"uses_floor_warmth": true`, set it on the 17 vanilla activities, and use `activity.id()->uses_floor_warmth()` in both places.

### CHAR-J13 · Parasite/infection purges hardcoded by trait and effect id lists
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/character.cpp:6963`, `src/character.cpp:6969`, `src/character.cpp:6982`, `src/character.cpp:6991`, `src/character.cpp:6995`, `src/character.cpp:3699`

`process_effects` removes fixed effect-id lists when the character has PARAIMMUNE, EATHEALTH or M_IMMUNE, or the ACIDBLOOD/INFECTION_IMMUNE flags, with hardcoded messages. effect_type `immune_flags` only blocks new applications, so a modded parasite is not purged by PARAIMMUNE unless this list is edited. The capability gap is small: mutation `processed_eocs` with `u_lose_effect`, or an effect with `removes_effects`, can already clear parasites. This is a data-driven cleanup.

```cpp
if( has_trait( trait_PARAIMMUNE ) && ( has_effect( effect_dermatik ) || has_effect( effect_tapeworm ) || ... ) ) {
    remove_effect( effect_dermatik ); ...
```

**Fix:** Add effect_type `"purged_by_flags": [ "PARAIMMUNE" ]` with optional `"purge_message"` (or `"remove_when_immune": true` on `immune_flags`). Tag the vanilla parasites and give EATHEALTH/M_IMMUNE character flags.

### CHAR-J14 · Trap/restraint escape logic hardcoded per effect id with hardcoded item drops
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/character_escape.cpp:94`, `src/character_escape.cpp:135`, `src/character_escape.cpp:156`, `src/character_escape.cpp:186`, `src/character_escape.cpp:295`, `src/character_escape.cpp:350`, `src/character_escape.cpp:369`, `src/character_escape.cpp:392`

beartrap, lightsnare, heavysnare, webbed, crushed and in_pit each have their own C++ escape routine, with hardcoded difficulty (100, 12, 32-DEX, 6*intensity, 40-DEX/2), limb score, mount rules, messages and dropped items (`beartrap`, `rope_6`, `snare_trigger`). The generic EFFECT_IMPEDING path has one fixed formula (STR*grip vs 6*intensity, tunable only via intensity), generic messages and no item drop. `character_loses_effect` can serve as an on-escape hook, but it cannot tell an escape from expiry. Grab escape special-cases the SLIMY/VISCOUS traits. A minor quirk: the mounted bear-trap branch checks `is_avatar() && is_mounted()`, while the other traps check only `is_mounted()`.

```cpp
if( can_escape_trap( 32 - get_dex(), true ) ) { item rope( itype_rope_6, ... ); item snare( itype_snare_trigger, ... );
if( has_trait( trait_SLIMY ) || has_trait( trait_VISCOUS ) )   // character_escape.cpp:295
```

**Fix:** Add an effect_type block and migrate the six effects:
```json
"escape": { "difficulty": { "math": "6 * intensity" }, "stat": "str", "limb_score": "grip", "mount_difficulty": 32,
  "drop_items": [ "rope_6", "snare_trigger" ], "success_message": "...", "fail_message": "...",
  "success_eoc": "EOC_ID", "blocks_movement": true }
```
Replace the SLIMY/VISCOUS check with an enchantment value `GRAB_ESCAPE_BONUS`.

### CHAR-J15 · Locomotion and swim tuning hardcoded to trait ids, item ids and magic numbers (incl. GILLS mismatch)
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/character.cpp:1381`, `src/character.cpp:1389`, `src/character.cpp:6231`, `src/character.cpp:6252`, `src/character.cpp:6256`, `src/character.cpp:6296`, `src/character.cpp:6319`, `src/character.cpp:6324`, `src/character.cpp:4730`, `src/suffer.cpp:328`, `src/avatar_action.cpp:655`, `src/avatar_action.cpp:681`

Swim speed uses PAWS/PAWS_LARGE trait formulas, the FIN flag only on `foot_l/foot_r` and a hand bonus only from `hand_l/hand_r`. Run cost hardcodes skate multipliers, the swim-fin 1.5x on land, barefoot +16/+8, ROOTS3/CHLOROMORPH +10, an M_IMMUNE fungus cap and snow penalties 20/50/100. Swimming washes off onfire/glowing/bile_irritant by id. One real bug: shouting underwater drains oxygen unless you have the GILLS/GILLS_CEPH *trait*, while drowning checks the GILLS *flag*. GILLS-flag effects and mutations in MindOverMatter and Xedra Evolved therefore still lose oxygen when shouting. The "sink like a rock" save checks item id `swim_fins` instead of the FIN flag. Many cases are already possible: `MOVECOST_SWIM_MOD`, the `swim` limb score, MOVE_COST enchantments with conditions, and SNOWWALKING. Partial snow reduction is the main thing that is missing.

```cpp
if( !has_trait( trait_GILLS ) && !has_trait( trait_GILLS_CEPH ) )   // character.cpp:4730
if( !you.has_flag( json_flag_GILLS ) )                              // suffer.cpp:328
const int penalty = snow_mm >= 500 ? 100 : ( snow_mm >= 250 ? 50 : 20 );
```

**Fix:** Use `json_flag_GILLS` at character.cpp:4730 and the FIN flag in avatar_action. Add enchantment `SNOW_MOVE_COST`, a `SWIM_HANDS` limb flag, an effect_type flag `REMOVED_BY_WATER` with `"water_remove_message"`, and optionally `"movement_modifiers": [ { "terrain_flag": "ROAD", "move_mode_type": "running", "multiplier": 0.5 } ]`.

### CHAR-J16 · Grazing (eat_here) hardcodes traits, yields, transforms, and is avatar-only
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/avatar_action.cpp:934`, `src/avatar_action.cpp:937`, `src/avatar_action.cpp:944`, `src/avatar_action.cpp:964`, `src/avatar_action.cpp:972`, `src/avatar_action.cpp:979`

Grazing requires the active RUMINANT/GRAZER trait ids. It yields hardcoded items (underbrush, small_plant, grass), turns shrubs into `t_grass`, and has refusal messages per terrain id. New edible terrain already works through the GRAZABLE/SHRUB/FLOWER flags and JSON `transforms_into`, but custom yields and new grazer traits need C++. The code lives in avatar_action, so NPC herbivores cannot graze.

```cpp
if( ( you.has_active_mutation( trait_RUMINANT ) || you.has_active_mutation( trait_GRAZER ) ) && ...
    here.ter_set( you.pos_bub(), ter_t_grass ); item food( itype_underbrush, calendar::turn, 1 );
```

**Fix:** Add a terrain/furniture field `"graze": { "item": "underbrush", "transforms_into": "t_grass", "requires_flag": "GRAZER", "fail_message": "This grass is too short to graze." }`. Have the traits grant character flags (GRAZER, BROWSER), and move the logic to Character.

### CHAR-J17 · character_mod is JSON-defined but consumers are hardcoded; negative min/max/subtract ignored
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/character.cpp:155`, `src/character_modifier.cpp:109`, `src/character_modifier.cpp:317`, `src/character_modifier.cpp:371`, `src/character_modifier.cpp:380`

Character modifiers are loaded from JSON, but each one is consumed only through a static C++ id, and `builtin` is a closed map of five functions. A new modifier affects nothing. EOCs cannot read a modifier's value, even though `limb_score` is exposed. `subtract`, `max`, `min` and `nominator` are applied only when `> epsilon`, so negative values are silently ignored. There is no `check()`.

```cpp
if( subtractor > std::numeric_limits<float>::epsilon() ) { score -= subtractor; }
if( max_val > ...epsilon() ) {...} if( min_val > ...epsilon() ) {...}
```

**Fix:** Add math `u_character_mod('slip_prevent_mod')`. Store min/max/subtract as `std::optional<float>`. Allow `"value": { "math": "..." }` as an alternative to `builtin`, and add a `check()` that validates limb score ids.

## 2. Bugs, oversights & quirks

### CHAR-B01 · Wound progression dereferences nullptr when a wound has 2+ progression entries (crash)
**Severity:** Critical · **Effort:** Small · **Verified:** confirmed
**Where:** `src/bodypart.cpp:1139`, `src/bodypart.cpp:1143`, `src/bodypart.cpp:1145`, `src/bodypart.cpp:1158`, `src/bodypart.cpp:1202`, `data/json/wounds/bash_wounds.json:65`, `src/character_health.cpp:2561`, `src/character_health.cpp:2768`

`add_or_worsen_wound` loops over every `wound_progression` entry. Each successful roll fetches the old wound, removes it and adds the progressed wound. If a second entry also succeeds, the old wound is already gone: `get_wound` shows a debugmsg and returns nullptr, and `new_wound += *old_wound` dereferences it. Vanilla `bruise_bad` has two entries (70% and 50%), and every hit reaches this code via `deal_damage -> apply_random_wound_from_damage -> apply_wound` (WOUND_CHANCE defaults to 1.0). A 15-25 bash hit on a limb with exactly one bad bruise crashes about 17.5% of the time. With two bad bruises, one hit consumes both.

```cpp
for( const wound_progress &wdp : wound_progression ) {
    if( x_in_y( wdp.chance, 100 ) ) {
        const wound *old_wound = get_wound( wd.type );
        wound new_wound = wound( wdp.id );
        new_wound += *old_wound;   // nullptr on 2nd success
        ...
        remove_wound( *old_wound );
        add_wound( new_wound );
```

**Fix:** Apply at most one progression per hit (first success or a weighted pick), then `break`. Also guard with `if( !old_wound ) { break; }`.

### CHAR-B02 · Frostbite risk zones are inverted: more wind or colder air gives less frostbite
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character_body.cpp:844`, `src/character_body.cpp:857`, `src/character_body.cpp:867`, `src/character_body.cpp:880`, `src/character_body.cpp:828`

The zone boundaries use `-4*F + 3*W - c`. That value grows as it gets colder or windier, so the `>= 0` side is the harsher side, yet the code puts it in the lower-risk zone. Examples: 9F calm is medium risk (+8/turn), but 9F at 19 mph is low risk (+3/turn, capped, never frostbite). At 10 mph wind, -6F is high risk (+72/turn) and -40F only medium. The `F < -35 && W >= 10` high-risk clause is unreachable because the medium branch always matches first. The comment table contains the same inversion. Players lose fingers on calm, mild days and are safer in blizzards. Frostbite also checks only hardcoded parts (see CHAR-J06).

```cpp
if( temp_after < BODYTEMP_COLD && ( ( F < 30 && F >= 10 ) ||
    ( F < 10 && F >= -5 && W < 20 && -4 * F + 3 * W - 20 >= 0 ) ) ) { mod_part_frostbite_timer( bp, 3 ); // LOW
} else if( ... -4 * F + 3 * W - 20 < 0 ) || ... || ( F < -5 && W >= 10 && -4 * F + 3 * W - 170 >= 0 ) ) ) { // MEDIUM
```

**Fix:** Swap the comparison signs in both line tests and fix the comment table. Better, pick the zone from windchill (`get_local_windchill` already exists).

### CHAR-B03 · Forced move-mode changes are reverted on the next action (broken-legs prone loop)
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/avatar.cpp:1306`, `src/avatar.cpp:1374`, `src/avatar.cpp:1388`, `src/avatar_action.cpp:184`, `src/handle_action.cpp:2280`, `src/character.cpp:1569`, `src/melee.cpp:627`, `src/npcmove.cpp:8357`

`avatar::set_movement_mode` changes `move_mode` but not `desired_move_mode`. Before every action that is not a move-mode action (movement keys included), `do_regular_action` switches back to the desired mode and charges the switch cost. Engine-forced modes are therefore undone on the next keypress: broken legs (prone), mounting (trot), dismount and wake-up (walk), and tree communion (prone). With broken legs, each step pays prone->walk (150+ moves), then `avatar_action::move` knocks you prone again with a "you fall down in pain" message. Toggling prone or crouch does not fix it, because the toggles test the current mode rather than the desired one. Related: `npc::set_movement_mode` recalculates the enchantment cache before it assigns the new mode.

```cpp
void avatar::set_movement_mode( const move_mode_id &new_mode ) { if( can_switch_to( new_mode ) ) { ... move_mode = new_mode; } }
// handle_action.cpp:2280
if( player_character.is_waiting_to_change_mode_mode() && !is_actions_move_mode ) { ... set_movement_mode( desired_move ); mod_moves( -desired_move_mode_cost );
void avatar::toggle_prone_mode_desired() { if( is_prone() ) { set_desired_movement_mode( move_mode_walk ); } ...
```

**Fix:** Add a forced path (e.g. `set_movement_mode( mode, /*sync_desired=*/true )`) and use it for every engine-initiated change. Make `toggle_crouch/prone_mode_desired` test `is_*_mode_desired()`, as `toggle_run` does. In `npc::set_movement_mode`, assign `move_mode` before `recalculate_enchantment_cache()`.

### CHAR-B04 · Effect stat mods, pain/hunger/thirst penalties, trait buffs and MA static buffs never apply to NPCs
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/avatar.cpp:1037`, `src/avatar.cpp:1043`, `src/avatar.cpp:1089`, `src/avatar.cpp:1090`, `src/avatar.cpp:1120`, `src/avatar.cpp:1123`, `src/avatar.cpp:1147`, `src/avatar.cpp:1149`, `src/avatar.cpp:1178`, `src/avatar.cpp:1181`, `src/avatar.cpp:1192`, `src/character.cpp:3102`, `src/character.cpp:3120`, `src/character.cpp:7766`, `src/creature.cpp:478`, `src/npc.cpp:1643`

`process_turn_no_moves` calls `reset_bonuses()`, `process_effects()`, then `reset_stats()`. Only avatar overrides `reset_stats`. NPCs run `Character::reset_stats`, which only updates focus, DODGE_CHANCE and the strength dodge tweak. As a result NPCs never get:
- the `str_mod`/`dex_mod`/`per_mod`/`int_mod` of any JSON effect (about 229 uses in vanilla `effects.json`);
- pain, starvation and thirst stat penalties (only pain's speed penalty reaches them, through `recalc_speed_bonus`);
- martial-arts static buffs and `mabuff_dodge_bonus` (NPCs only call `ma_static_effects` on wield, and static buffs expire after 2 turns);
- the WHISKERS/WHISKERS_RAT/CHITIN_FUR3 dodge, INSECT_ARMS_OK/ARACHNID_ARMS_OK dex, mounted dodge penalty and trait miss reasons.

`speed_mod` does reach NPCs, which makes the gap inconsistent. EFFECTS_JSON.md has no avatar-only caveat. Effect `enchantments` with STRENGTH and similar values do work on NPCs, which is the only workaround. Example: an NPC with an "exhausted" effect carrying `"dex_mod": [-3]`, or an NPC that is starving or in heavy pain, fights at full stats.

```cpp
void Character::reset_stats() { if( calendar::once_every( 1_minutes ) ) update_mental_focus();
    mod_dodge_bonus( enchantment_cache->modify_value( DODGE_CHANCE, 0 ) ); ... }
// avatar.cpp:1181 – the only get_mod( "STR" ) call in src/
for( const auto &maps : *effects ) { ... mod_str_bonus( it.get_mod( "STR", reduced ) ); mod_dex_bonus( ... ); }
```

**Fix:** Move everything except the display-only fake effects (pkill, irradiated, happy, sad, stim) and miss-reason UI text into `Character::reset_stats`. Keep a small avatar override for the display parts. Separately, convert the trait buffs to mutation enchantments (e.g. `{ "condition": { "not": { "u_has_worn_with_flag": "..." } }, "values": [ { "value": "BONUS_DODGE", "add": 1 } ] }`) and add a mutation field `"miss_reasons": [ { "text": "Your chitin gets in the way.", "weight": 1 } ]`.

### CHAR-B05 · can_fly() runs move_effects(), so checking for wings rolls trap/grab/downed escapes
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character.cpp:8045`, `src/character.cpp:8057`, `src/character_escape.cpp:392`, `src/character_escape.cpp:428`, `src/trapfunc.cpp:1090`, `src/trapfunc.cpp:1094`, `src/trapfunc.cpp:1154`, `src/trapfunc.cpp:1243`, `src/iexamine.cpp:5554`, `src/map.cpp:11674`

`can_fly()` is used as a query, but its first line calls `move_effects(false)`. That rolls escapes from downed, webs, snares, bear traps, rubble, pits and grabs, removes effects on success, prints messages, burns stamina and can tear pockets. The pit traps add `effect_in_pit` and then call `can_fly()`, so every character falling into a pit gets an immediate free escape roll, with or without wings. The ledge menu calls it twice (for GLIDE/WING characters), so opening the menu while grabbed makes two grab-break attempts. `100 * weight_carried() / weight_capacity()` is integer division, and `weight_capacity()` can be clamped to 0, so a winged character with very low strength can crash with a divide by zero.

```cpp
bool Character::can_fly() {
    if( !move_effects( false ) || has_effect( effect_stunned ) ) { return false; }
    ...
        if( 100 * weight_carried() / weight_capacity() > 50 ) {
```

**Fix:** Make `can_fly()` const and free of side effects. Use a read-only `is_immobilized()` that checks the same effects and the GRAB flag without rolling. Compare `weight_carried() * 2 > weight_capacity()`.

### CHAR-B06 · Fall damage multiplier reaches exactly 0, giving total immunity from any height
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character.cpp:7861`, `src/character.cpp:7864`, `src/character.cpp:7868`, `src/character.cpp:7878`, `src/character.cpp:7981`, `src/character.cpp:7987`, `data/json/character_modifiers.json:138`, `data/json/mutations/limbs_body_parts.json:190`

`fall_damage_mod` is linear, `(100 - 4*dex_dodge)/100`, clamped at 0. `impact()` softens falls with `pow(mod, 30/force)`, but `pow(0, x)` is 0, so `mod * force < 5` always takes the "perfect landing" branch. This is reachable in vanilla: Feline Eyes raise the reaction score to about 1.55, and with dex 18 and dodge 8, `17*1.55 = 26 >= 25` makes the character immune to falls from any height. The code comment ("75% at 10, 50% at 20") shows the curve is steeper than intended.

```cpp
ret *= ( 100.0f - ( dex_dodge * 4.0f ) ) / 100.0f;
return std::max( 0.0f, ret );
// impact(): if( !slam && mod < 1.0f && mod * force < 5 ) { // Perfect landing
```

**Fix:** Use an asymptotic curve (e.g. `1/(1 + dex_dodge/k)`) or a floor such as 0.1 before enchantments, so only FEATHER_FALL or explicit FALL_DAMAGE enchantments give full immunity.

### CHAR-B07 · Similar-bodypart lookup maps are never cleared, so stale ids leak into the next world
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/bodypart.cpp:110`, `src/bodypart.cpp:529`, `src/bodypart.cpp:566`, `src/subbodypart.cpp:16`, `src/subbodypart.cpp:97`, `src/subbodypart.cpp:105`, `src/item_factory.cpp:1004`, `src/init.cpp:658`

`combined_similar_bodyparts` and `combined_similar_sub_bodyparts` are file-level statics filled in `finalize()`. The `reset()` functions called by `unload_data` only reset the factories. Every data reload duplicates entries, because vanilla itself uses `similar_bodypart`. Loading a Magiclysm, Xedra Evolved or Limb_WIP world and then a vanilla world leaves entries such as `eyes -> eyes_goblin`. Armor finalization (`item_factory.cpp:1004-1021`) then dereferences the invalid ids, emits debugmsgs, falls back to int id 0 and can add coverage of an unrelated body part.

```cpp
std::unordered_map<bodypart_str_id, std::vector<bodypart_str_id>> combined_similar_bodyparts;
void body_part_type::reset() { body_part_factory.reset(); }   // map not cleared
```

**Fix:** Clear both maps in their `reset()` functions (or at the start of `finalize_all()`), and de-duplicate on insert.

### CHAR-B08 · try_remove_impeding_effect: use-after-free and auto-escape for limb-targeted impeding effects
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character_escape.cpp:371`, `src/character_escape.cpp:376`, `src/character_escape.cpp:382`, `src/character_escape.cpp:385`, `src/creature.cpp:2159`, `src/creature.cpp:2091`, `src/magic_spell_effect.cpp:523`, `src/npctalk.cpp:3456`

The loop iterates references into the effects map, one per (type, bodypart). On success it calls `remove_effect(eff_id)` with no bodypart, which erases every instance of that type. If the effect is on two limbs, the next iteration reads a freed map node. The difficulty uses `get_effect_int(eff_id)`, which only looks up the `bp_null` instance, so a limb-targeted effect has difficulty 0, and `x_in_y(chance, 0)` always succeeds. No vanilla content puts impeding effects on limbs, but EOC `u_add_effect` with `target_part` and spells with `affected_body_parts` can. `try_remove_webs` and `try_remove_lightsnare` make the same whole-body assumption.

```cpp
for( const effect &eff : get_effects_with_flag( flag_EFFECT_IMPEDING ) ) {
    ...
    } else if( can_escape_trap( 6 * get_effect_int( eff_id ) ) ) {   // bp_null -> 0 for limb effects
        remove_effect( eff_id );   // removes all bps; later iterations dangle
```

**Fix:** Copy (id, bp, intensity) into a local vector first, use `eff.get_intensity()` for difficulty, and remove only `eff.get_bp()`.

### CHAR-B09 · Grab escape treats non-adjacent grabbers as orphans
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character_escape.cpp:234`, `src/character_escape.cpp:253`, `src/character_escape.cpp:320`, `src/mattack_actors.cpp:542`, `src/monster.cpp:2872`, `src/monster.cpp:3032`, `data/json/monsters/zed_tentacle.json:66`

`try_remove_grab` only looks for the grabber in `points_in_radius(pos, 1, 0)`, i.e. adjacent tiles on the same z-level. Monster grabs support range > 1 (the vanilla zombie strider grabs at range 2 with no pull), and the pull loop can stop early or cross z-levels. In those cases any move silently removes the grab as an "orphan", while the monster's `grabbed_limbs` still lists the limb. `monster::remove_grab` also calls `add_effect(effect_grabbing)` where it should call `remove_effect`. That is mostly harmless, because the monster's next `process_turn` cleans up, but it is wrong.

```cpp
const tripoint_range<tripoint_bub_ms> &surrounding = here.points_in_radius( pos_bub(), 1, 0 );
... if( grabber == nullptr ) { remove_effect( eff.get_id(), eff.get_bp() ); add_msg_debug( ..., "Orphan grab found and removed" ); continue; }
```

**Fix:** Find the grabber with a reach-sized creature_tracker search across adjacent z-levels, filtered by `mon->is_grabbing(bp)`, or store the grabber in the effect source. When removing a true orphan, clear it from any monster that still lists the limb. Make `monster::remove_grab` call `remove_effect`.

### CHAR-B10 · Dragging a vehicle across stairs skips collision, harness, cost and down-strength checks
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/grab.cpp:32`, `src/grab.cpp:51`, `src/grab.cpp:84`, `src/grab.cpp:122`, `src/grab.cpp:144`, `src/game.cpp:9696`, `src/map.cpp:1789`

`grabbed_veh_move_stairs` displaces the grabbed vehicle to `new_rel + dir`, where `dir` is chosen by the player. It never runs `vehicle::collision()`, and `displace_vehicle` checks no terrain, so a cart can end up inside walls, furniture, vehicles or creatures on the new level. The strength check applies only when going up. There is no harness check and no move or stamina cost. The theft check is already done when grabbing. Only 1xN vehicles (carts, bikes) pass the "too wide" test. Trigger: grab a cart, climb stairs, pick a direction toward a wall, and the cart ends up embedded in the wall.

```cpp
if( str < max_str_req && going_up_stairs ) { ... return false; }   // nothing going down
here.displace_vehicle( *grabbed_vehicle, new_dp );                  // no collision(), no mod_moves
```

**Fix:** Reuse `grabbed_veh_move`'s checks (harness, strength in both directions, move/stamina cost), and run `grabbed_vehicle->collision( here, colls, new_dp, true )` before displacing. Abort with a "blocked by X" message on collision.

### CHAR-B11 · Trap-escape gaps: waiting never frees you from rubble, and very high DEX makes the heavy snare inescapable
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character_escape.cpp:446`, `src/character_escape.cpp:171`, `src/character_escape.cpp:430`, `src/character_escape.cpp:104`, `src/character_escape.cpp:186`, `src/map.cpp:5151`, `src/rng.cpp:126`

`wait_effects` has no `crushed` (or `in_pit`) branch, and `effect_crushed` is permanent, so a player who waits under rubble never gets an escape roll. The heavy-snare difficulty `32 - get_dex()` is not clamped. With dex >= 33, which Magiclysm, MindOverMatter and Xedra Evolved allow (cap 35), it goes negative and `x_in_y` never succeeds. The pit formula has the same issue in theory (dex >= 82). A mount with `dice*sides < 18` never even attempts a bear-trap escape.

```cpp
void Character::wait_effects( bool attacking ) { /* downed, beartrap, snares, webbed, IMPEDING, GRAB – no crushed */ }
if( can_escape_trap( 32 - get_dex(), true ) ) {
```

**Fix:** Add crushed handling to `wait_effects`, ideally by sharing one table with `move_effects`. Clamp difficulties with `std::max( 1, ... )`. Give weak mounts a small chance, or fall back to the rider's own roll.

### CHAR-B12 · uncanny_dodge unboards the character before checking for room and skips gravity
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character.cpp:1920`, `src/character.cpp:1928`, `src/character.cpp:1931`, `src/character.cpp:1932`, `src/character.cpp:1956`, `src/character.cpp:4758`, `src/creature.cpp:316`, `src/character.cpp:2790`

In a vehicle without a seatbelt, `uncanny_dodge` calls `unboard_vehicle()` before checking whether `adjacent_tile()` found space. If there is no room, the character is left standing on a vehicle tile while no longer boarded. On success the move uses `set_pos_bub_only`, which skips `on_move()` and `gravity_check()`. `adjacent_tile()` accepts open air, so the character can dodge onto it and hover.

```cpp
if( in_vehicle && veh_part ) { here.unboard_vehicle( pos_bub( here ) ); }
if( adjacent.xy() != pos_bub().xy() ) { set_pos_bub_only( here, adjacent ); ... }
... add_msg( _( "You try to dodge, but there's no room!" ) ); return false;
```

**Fix:** Check `adjacent != pos` before unboarding, then use `setpos( here, adjacent )` (runs `on_move` and `gravity_check`) and re-board when appropriate.

### CHAR-B13 · most_staunchable_bp divides by zero for characters with a single arm
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character.cpp:8265`, `src/character.cpp:8281`, `src/character.cpp:8282`, `src/character.cpp:8294`, `src/creature.cpp:3187`

Arm bleeding is weighted by `intensity /= (1 - 1/num_arms)`. With exactly one arm-type part the divisor is 0, and converting +inf back to int is undefined behaviour. On x86 the result is INT_MIN, so the arm is always staunchable; on ARM64 it is INT_MAX, so it never is. Core data never produces one arm, but Limb_WIP can: `ENCH_LEFT_ARM_COSMETIC_PROSTHETIC` (`data/mods/Limb_WIP/enchantments.json:97`) gains `arm_prosthetic_cosmetic_r` instead of `_l`. The debug line at 8294-8295 also has its "base" and "modified" labels swapped.

```cpp
if( bp->has_type( bp_type::arm ) ) { intensity /= ( 1.0f - 1.0f / num_arms ); }
```

**Fix:** Skip the penalty when `num_arms <= 1` (or define single-arm staunching explicitly), and do the math in float with a clamp. Fix the Limb_WIP `_r`/`_l` typo and the debug labels.

### CHAR-B14 · has_nv_goggles() keeps its result in a function-static shared by every Character
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character.cpp:3133`, `src/character.cpp:3135`, `src/character.cpp:568`, `src/character.cpp:579`, `src/character.cpp:1352`, `src/do_turn.cpp:671`, `src/character.cpp:2472`, `src/character.cpp:2530`

`static bool nv` is shared by the whole process, while `nv_cached` is per instance. Every Character constructor (NPC spawn or load, temporary NPCs) overwrites the static with false. The avatar's cache is only reset after a keypress action, so during activities the green NV tint can switch off when an NPC loads in. NPCs mirror whatever value was computed last. The impact is cosmetic: the actual night-vision range checks `GNV_EFFECT` directly.

```cpp
bool Character::has_nv_goggles() {
    static bool nv = false;
    if( !nv_cached ) { nv_cached = true; nv = worn_with_flag( flag_GNV_EFFECT ) || ...; }
    return nv;
}
```

**Fix:** Make the cached value a member next to `nv_cached` and invalidate it on wear/takeoff/activation, or drop the cache.

### CHAR-B15 · Companions who die on remote missions leave sterile, packaged, unfaulted CBMs
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character.cpp:6368`, `src/character.cpp:6374`, `src/character.cpp:6381`, `src/character.cpp:6389`, `src/character.cpp:6422`, `src/faction_camp.cpp:4367`, `src/butchery.cpp:121`, `src/butchery.cpp:690`

The local `place_corpse` marks harvested CBMs FILTHY, NO_STERILE and NO_PACKED, applies `fault_bionic_salvaged` and sets a 37C temperature. The overmap overload, used when a camp patrol companion dies, puts plain `item(bio_itype)` into the corpse. Dissection moves the item to the map unchanged, so the player can recover install-ready, sterile CBMs.

```cpp
// remote overload
for( const bionic &bio : *my_bionics ) { ... body.put_in( item( bio_itype, calendar::turn ), pocket_type::CORPSE ); }
```

**Fix:** Build the corpse in one shared helper used by both overloads.

### CHAR-B16 · "Very encumbered" warning on wear is computed before encumbrance is recalculated
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character_attire.cpp:384`, `src/character_attire.cpp:385`, `src/character_attire.cpp:420`, `src/creature.cpp:2692`, `src/bodypart.cpp:1006`

`outfit::wear_item` prints "Your %s is very encumbered!" using cached encumbrance, and `calc_encumbrance()` only runs later. The item that pushes a limb past 40 produces no warning; the next garment on that limb does, even if it adds nothing.

```cpp
if( to_wear.covers( bp ) && guy.encumb( bp ) >= 40 ) { ... very encumbered ...
...
if( do_calc_encumbrance ) { guy.recalc_sight_limits(); guy.calc_encumbrance();
```

**Fix:** Recalculate encumbrance before the message block, or move the messages after it.

### CHAR-B17 · anatomy::check/finalize copy-paste errors: wrong id validated and wrong field printed
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/anatomy.cpp:80`, `src/anatomy.cpp:103`, `src/anatomy.cpp:112`

In `check()`, the "skip invalid parts" guard tests the anatomy's own `id` (always valid) instead of `bp`, so invalid parts fall through and trigger more debugmsgs (no crash, because of dummy objects). The `connected_to` error prints `bp->main_part`. In `finalize()`, the loop variable `id` shadows the member, so the message prints the part id twice and never names the anatomy. These are load-time diagnostics only.

```cpp
if( !id.is_valid() ) {   // should be bp
debugmsg( "... with connected_to part %s ...", id.str(), bp.str(), bp->main_part.str() );
for( const bodypart_str_id &id : unloaded_bps ) ... debugmsg( "Anatomy %s has invalid body part %s", id.c_str(), id.c_str() );
```

**Fix:** Test `bp.is_valid()`, print `bp->connected_to`, and rename the loop variable.

### CHAR-B18 · climbing fall_scan double-counts height_until_creature
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/climbing.cpp:405`, `src/climbing.cpp:406`, `src/climbing.cpp:410`, `src/climbing.h:137`, `src/climbing.h:140`, `src/game.cpp:11062`

`height_until_creature` is incremented unconditionally and then again when no creature is found, so it ends up about 2x the real height and is never 0. `crea_just_below()` can never be true. Nothing calls these helpers yet, so the bug is latent. Only the debug message at game.cpp:11062 shows the wrong value.

```cpp
if( !hit_crea ) {
    ++height_until_creature;
    if( creatures.creature_at( lower, false ) ) { hit_crea = true; } else { ++height_until_creature; }
}
```

**Fix:** Remove the first unconditional increment, matching the furniture and vehicle counters.

## 3. Performance

### CHAR-P01 · Body-modifying mutations trigger a full calc_encumbrance every turn
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character_body.cpp:252`, `src/character.cpp:4996`, `src/character.cpp:4899`, `src/character.cpp:4919`, `src/character_inventory.cpp:1334`, `src/character_attire.cpp:960`, `src/npc.cpp:3555`, `src/npc.cpp:3598`

`update_body()` rebuilds the enchantment cache every avatar turn and every 10 turns per NPC, plus up to about 400 times during NPC load catch-up. For any character with a `modified_bodyparts` enchantment, `modifies_bodyparts()` is true on every rebuild. `recalculate_bodyparts()` then always runs `tally_organic_size()`, `recalc_limb_energy_usage()` and the full outfit encumbrance pass, even though the body has not changed. Core has 77 such sources (Lupine Muzzle, Beak, Compound Eyes, paws, Exodii limbs), so this is the normal case for mutant characters.

```cpp
if( enchantment_cache->modifies_bodyparts() ) { recalculate_bodyparts(); }   // character.cpp:4996
tally_organic_size(); recalc_limb_energy_usage(); ... calc_encumbrance();    // character.cpp:4920
```

**Fix:** In `recalculate_bodyparts()`, compare the computed set with the current keys of `body` and return early when nothing would be added or removed. Better, store the last applied `body_part_set` and skip the call when it matches. This also covers the rebuild condition needed for CHAR-J01.

### CHAR-P02 · Per-turn enchantment rebuild copies relic enchantment vectors and walks the whole inventory per enchantment
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character.cpp:4936`, `src/character.cpp:4942`, `src/relic.cpp:681`, `src/item.cpp:3386`, `src/magic_enchantment.cpp:347`, `src/visitable.cpp:88`, `src/visitable.cpp:678`, `src/item_location.cpp:1011`, `src/character.cpp:4970`, `src/magic_enchantment.cpp:841`

For each carried relic, the rebuild iterates `get_proc_enchantments()`/`get_defined_enchantments()`, which return vectors by value. That deep-copies enchantments containing many maps, vectors and std::functions. `is_active()` then starts with `guy.has_item(parent)`, a full `visit_items` walk that allocates an `item_location` per node. With 4 enchanted items and 300 carried items that is 1,000+ allocations per turn. The first mutation loop calls `is_active()` only to collect `mutations`, so DIALOG_CONDITION enchantments are evaluated twice. Caveats: the item cache does not guarantee the item is still carried, so the `has_item` check is guarding against stale entries. `iuse.cpp:8763` and `talker_item.cpp:113` bind non-const references to the returned vector.

```cpp
std::vector<enchant_cache> relic::get_proc_enchantments() const { return proc_passive_effects; }
if( !guy.has_item( parent ) ) { return false; }   // magic_enchantment.cpp:349
```

**Fix:** Return `const std::vector<...> &` (use a static empty vector for non-relics) and update the two non-const callers. Add an `is_active_carried()` that skips `has_item()` once the cache is reliably invalidated. In the mutation loop, `continue` when `ench.mutations.empty()`.

### CHAR-P03 · Loop-invariant has_flag() inside per-effect and per-bodypart loops
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character.cpp:6688`, `src/character.cpp:7010`, `src/character.cpp:3506`, `src/character_body.cpp:624`, `src/character_body.cpp:425`, `src/character_body.cpp:190`

The uncached `has_flag()` (CHAR-P08) is called inside loops where the answer cannot change. `process_one_effect` begins with `has_flag(FREEZE_EFFECTS)` and `has_flag(INFECTION_IMMUNE)`, and it runs once per effect instance, so each turn costs O(E*(E+B)). `temp_equalizer` checks `CANNOT_CHANGE_TEMPERATURE` twice per body part, but its only caller already returned early on that flag at line 425. `update_body_wetness` repeats a `has_flag` per body part.

```cpp
if( has_flag( json_flag_FREEZE_EFFECTS ) ) { return; }               // first line of process_one_effect
if( has_flag( json_flag_CANNOT_CHANGE_TEMPERATURE ) ) { return; }    // temp_equalizer, always false here
```

**Fix:** Check the flags once in `process_effects` and pass the results in. Remove the check from `temp_equalizer`. Hoist the wetness flag into a local. These are mechanical changes with no behaviour change.

### CHAR-P04 · update_body recomputes vitamin_rate() for every vitamin every turn, each call allocating the full mutation list
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character_body.cpp:327`, `src/consumption.cpp:643`, `src/newcharacter.cpp:2107`

`update_body` loops over all 57 core vitamins and calls `vitamin_rate(v)` for each. Each call builds a fresh `get_functioning_mutations()` vector and does a map find per mutation. With 30 mutations that is about 57 allocations and 1,700 lookups per character-turn, for a value that only changes when mutations change.

```cpp
for( const auto &v : vitamin::all() ) { const time_duration rate = vitamin_rate( v.first );
for( const auto &m : get_functioning_mutations() ) { ... mut.vitamin_rates.find( vit );
```

**Fix:** Cache per-character effective vitamin rates and rebuild them where `trait_flag_cache` is cleared. Minimal alternative: fetch the mutation list once before the loop, filtered to mutations with non-empty `vitamin_rates`.

### CHAR-P05 · outfit::absorb_damage builds armor.tname() for every covering armor piece on every damage unit
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character_attire.cpp:1863`, `src/character_attire.cpp:1894`, `src/character_armor.cpp:193`

For each damage unit of every hit, every worn item that covers the body part builds `armor.tname()`, which is expensive (translations, prefixes, and contents for worn containers). The name is only used if the armor is destroyed, which is rare. That is about 5-15 discarded names per hit, and hundreds per turn in horde fights.

```cpp
const std::string pre_damage_name = armor.tname();   // character_attire.cpp:1863
if( destroy ) { ... destroyed_armor_msg( guy, pre_damage_name );
```

**Fix:** Build the name inside the destroy branch (`armor.tname( 1, false )` is fine for a "destroyed" message).

### CHAR-P06 · process_one_effect does about 36 string-hashed mod_data lookups per effect instance per turn
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character.cpp:6710`, `src/character.cpp:6732`, `src/character.cpp:6943`, `src/character.cpp:7012`, `src/effect.cpp:565`, `src/effect.cpp:1294`, `src/effect.h:244`

Each effect instance queries 17 stat keys, with MIN and MAX lookups for each, plus COUGH/VOMIT. Every lookup builds a temporary `std::string` (`"RESPIRATION_RATE"` overflows SSO) and searches `mod_data`. 322 of 527 core effect types define no mods at all, so these lookups always return 0. Together with the per-instance `has_flag` calls (CHAR-P03), this is the main per-effect cost.

```cpp
val = get_effect( "H_MOD", reduced ); ... val = get_effect( "STAMINA", reduced );
std::unordered_map<std::string, std::unordered_map<uint32_t, modifier_value_arr>> mod_data;   // effect.h:244
```

**Fix:** Quick win: add `effect_type::has_mods()` and skip the stat block when it is false. Proper fix: an enum of mod keys stored in an array, with a bitmask of the keys present.

### CHAR-P07 · manipulator_score copies whole bodypart objects into a map of vectors and sorts them on every call
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character_modifier.cpp:168`, `src/character_modifier.cpp:171`, `src/character_modifier.cpp:179`, `src/character_modifier.cpp:192`, `src/character_modifier.cpp:237`, `src/character.cpp:1972`, `src/character_inventory.cpp:1516`, `src/npcmove.cpp:2616`, `src/npc_attack.cpp:405`, `src/crafting.cpp:256`

`get_limb_score(manip)` copies each `bodypart` by value (7 layer_details with 2 vectors each, plus wounds) into a `std::map<bp_type, std::vector<...>>`, then sorts. Each call also materialises `get_effects_with_flag(EFFECT_LIMB_SCORE_MOD)` and does an uncached `has_flag`. The hottest caller is NPC combat AI: `evaluate_best_attack -> can_wield -> has_min_manipulators` runs per inventory item, and the `npc_attack` evaluations call it again, which adds up to hundreds of calls per NPC move. Crafting and aim are milder callers.

```cpp
std::map<bp_type, std::vector<std::pair<bodypart, float>>> bodypart_groups;
bodypart_groups[ bp_type.first ].emplace_back( id.second, bp_type.second );
```

**Fix:** Store `const bodypart *` in a small fixed array keyed by `bp_type`, and compute the weighted score before sorting. Iterate the effects directly instead of building the vector.

### CHAR-P08 · Character::has_flag rescans all effects, mabuffs and body parts on every call
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/character.cpp:7560`, `src/character.cpp:7508`, `src/creature.cpp:2151`, `src/martialarts.cpp:1991`, `src/martialarts.cpp:1874`, `src/martialarts.cpp:1015`, `src/mutation.cpp:163`

`has_flag` is called very often (sight limits, every `add_effect`, `process_one_effect`, bodytemp, run cost, limb scores). Only the trait and bionic parts are cached. The effect, body-part (including conditional flags) and mabuff parts are linear scans. The mabuff scan string-compares every effect id against `"mabuff:"`, and for real buffs runs `substr(7)` and re-interns the result. The code itself suggests a flag map. Related bug: `has_trait_flag` evaluates `active_flags` against `get_player_character()` and stores the result in the *NPC's* cache. Fix that before extending the cache. The call-count estimates were not measured, so this is rated medium.

```cpp
// If this is a performance problem create a map of flags stored for a character...
return has_trait_flag( flag ) || has_bionic_with_flag( flag ) || has_effect_with_flag( flag ) ||
       has_bodypart_with_flag( flag ) || has_mabuff_flag( flag );
```

**Fix:** Add a per-character flag cache for the effect, mabuff and static body-part parts, validated by a generation counter bumped on effect add/remove/expiry and in `recalculate_bodyparts`. Precompute the union of `conditional_flags`. Store the `mabuff_id` on the effect_type at load. Fix `has_trait_flag` to evaluate against `*this`.

### CHAR-P09 · recalc_speed_bonus formats effect::disp_name() for every effect instance every turn
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character.cpp:7766`, `src/character.cpp:7740`, `src/character.cpp:7731`, `src/effect.cpp:775`, `src/character.cpp:2297`, `src/player_display.cpp:969`

`disp_name()` is evaluated as an argument before `mod_speed_bonus` drops zero values, so a string (plus `string_format` for intensity and body part) is built and thrown away for each effect. The descriptions are only read by the character sheet. Translations are cached, so the cost is a few microseconds per character-turn.

```cpp
mod_speed_bonus( i.second.get_mod( "SPEED", reduced ), i.second.disp_name() );
```

**Fix:** Compute the mod first and build the name only when it is non-zero, or store id+bp and render lazily in `player_display`.

### CHAR-P10 · Bodytemp and wetness refresh existing per-bodypart effects through full add_effect() every turn
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character_body.cpp:216`, `src/character_body.cpp:711`, `src/character_body.cpp:908`, `src/creature.cpp:1911`, `src/creature.cpp:1942`, `src/creature.cpp:1955`, `src/do_turn.cpp:772`, `src/npc.cpp:3557`

wet, cold, hot, hot_speed and frostbite are re-added per body part every turn. `Creature::add_effect` runs immunity checks, an uncached `has_flag(CANNOT_GAIN_EFFECTS)` and an O(E) blocker scan before it reaches the "already present" branch. That is about 30 calls per turn in winter rain. The cost is real but small, and only applies in cold, hot or wet conditions.

```cpp
add_effect( effect_wet, 1_turns, bp, true, effect_level );   // character_body.cpp:216
auto matching_map = effects->find( eff_id );                 // creature.cpp:1955, checked last
```

**Fix:** Add a `refresh_bp_effect()` helper that updates intensity in place when the effect already exists. Reordering `add_effect` instead would change semantics for effects that have since become blocked or immune.

### CHAR-P11 · Outfit layering pass (layer_item) reallocates sub-part lists, layer vectors and maps in its innermost loops
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character_attire.cpp:876`, `src/character_attire.cpp:898`, `src/character_attire.cpp:901`, `src/character_attire.cpp:910`, `src/character.cpp:4996`, `src/character_body.cpp:252`

For each covered body part, `layer_item` copies `get_layer(bp)`. For each layer it builds a `std::map<layer_level,bool>` and calls `get_covered_sub_body_parts()` again, which returns a vector by value. For each sub-part it allocates a one-element vector just to call `has_layer`. This is normally a per-equip cost, but it runs every turn for body-modifying mutants (CHAR-P01) and on each keypress in the armor-layers UI.

```cpp
std::map<layer_level, bool> bpcovered;
for( sub_bodypart_id &sbp : it.get_covered_sub_body_parts() ) {
    std::vector<layer_level> ll; ll.push_back( item_layer ); if( !it.has_layer( ll, sbp ) ) {
```

**Fix:** Hoist `get_covered_sub_body_parts()` per item, use `std::array<bool, NUM_LAYER_LEVELS>`, and add a `has_layer(layer_level, sub_bodypart_id)` overload.

### CHAR-P12 · Character::use_charges deep-copies the crafting inventory on every call, before its early returns
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character.cpp:5796`, `src/character.cpp:5798`, `src/character.cpp:5817`, `src/crafting.cpp:678`, `src/temp_crafting_inventory.cpp:149`

The first statement copies the cached `temp_crafting_inventory` by value, which copies every owned item and the location colony. That happens even on the `qty <= 0`, fire and UPS early returns, and the copy is only read by one USE_UPS visit. Callers fire once per action (seeding, fertilizing, crafting components), so the impact is low.

```cpp
temp_crafting_inventory inv = crafting_inventory( pos_bub(), radius, true );   // copy
```

**Fix:** Move the line below the early returns and bind it as `const temp_crafting_inventory &`.

### CHAR-P13 · get_overlay_ids rebuilds dozens of strings and a multimap per visible Character on every tiles redraw
**Severity:** Low · **Effort:** Medium · **Verified:** plausible
**Where:** `src/character.cpp:2916`, `src/character.cpp:2922`, `src/character.cpp:2936`, `src/character_attire.cpp:1788`, `src/cata_tiles.cpp:4727`, `src/cata_tiles.cpp:2552`

Every draw of every visible avatar or NPC rebuilds overlay id strings for effects, mutations, bionics and worn items, plus a multimap for ordering. The gain from caching is unproven, because redraws are per input or turn and `find_overlay_looks_like` costs more per overlay. There is a real cosmetic bug here: `variant` is declared outside the mutation loop and never reset, so a mutation without a variant inherits the previous mutation's variant (mostly wasted sprite lookups, since the base sprite is used as a fallback).

```cpp
rval.emplace_back( "mutation_" + mutorder.second.first, mutorder.second.second );
```

**Fix:** Reset `variant` on each iteration (trivial). Optionally cache the list per Character behind an appearance generation counter.

## Appendix: considered and rejected

- run_cost() builds translated description strings and a result vector per step – `_()` uses a per-call-site translation cache, most labels are conditional and fit in SSO, and run_cost runs only a few times per step, so the cost is negligible.
- Creature scans run LOS and path checks before cheap distance/attitude filters – the avatar's `sees()` is a precomputed vision-cache lookup, and `get_targetable_creatures` is only called per autoattack keypress or target-UI refresh, so this is a cold path.
- process_turn re-enumerates all martial arts every turn for autolearn – only `style_brawling` has autolearn among 32 styles, and skill interning only happens until brawling is learned, so the cost is negligible.
- is_immune_damage converts string flags to string_ids on every call – that is two intern lookups per call, dwarfed by the `has_flag` scan that follows, and it is not called in a per-turn loop.
- update_bodytemp per-bodypart loop repeats loop-invariant map queries – `body` is a small int-keyed map, the heavy inputs are already hoisted, and the temperature lookup is memoized, so the cost is a few microseconds per turn.
