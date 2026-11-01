# 15 · Health, needs, effects, morale & wounds

> Engine audit – static code reading at commit `a1a646d6`; nothing was compiled or run. Each finding was checked against the code by a separate reviewer: **confirmed** = verified by reading the code; **plausible** = consistent with the code, but reachability or impact not proven. Line numbers refer to that commit.

**Scope:** `src/character_health.cpp`, `src/consumption.cpp`, `src/stomach.cpp`, `src/vitamin.cpp`, `src/addiction.cpp`, `src/disease.cpp`, `src/effect.cpp`, `src/effect_source.cpp`, `src/suffer.cpp`, `src/player_hardcoded_effects.cpp`, `src/sleep.cpp`, `src/morale.cpp`, `src/morale_types.cpp`, `src/wound.cpp`, `src/medical_ui.cpp`, `src/body_status_ui.cpp`

## Summary

The health and needs layer works, but much of its behaviour is still written in C++ against specific trait, effect and item ids: diets, sleep rules, addiction lifecycles, lethal overdose limits, the cold and flu, and about 30 hardcoded effects. The effect, vitamin and morale loaders are hand-rolled rather than `generic_factory`, so mods cannot copy, extend or override them, and validation is thin. Several documented JSON fields do nothing (`respiratory_rate_*`, NPC stat mods, character `dodge_mod`, wound caps, morale `permanent`). There are a number of small arithmetic bugs with real gameplay impact (DISTRIBUTED_DAMAGE, limb mending, BMR rounding, morale stacking). On the performance side, the per-turn effect and needs code repeats expensive lookups (`has_flag`, string-keyed modifier maps, mutation-list rebuilds, option lookups) that could be hoisted or cached.

| Lens | Critical | High | Medium | Low | Total |
|---|---|---|---|---|---|
| Missing JSON / EOC | 0 | 3 | 11 | 2 | 16 |
| Bugs & quirks | 0 | 1 | 10 | 8 | 19 |
| Performance | 0 | 0 | 7 | 7 | 14 |
| **Total** | 0 | 4 | 28 | 17 | 49 |

**Fix first:**
1. **HLTH-B01** · DISTRIBUTED_DAMAGE makes SLIMESPAWNER characters immune to hits under 12 – a high-severity near-immunity bug with a few-line fix.
2. **HLTH-J01** · Effect stat/combat mods apply only to some creature kinds – a small move of one loop fixes every drunk or poisoned NPC and the dead `grabbed` dodge penalty.
3. **HLTH-B02** · `Character::mend` multiplies `healing_factor` across limbs – a one-line local-variable fix that stops a bionic limb from freezing all later mending.
4. **HLTH-P02** · Water-weakness damage runs once per mutation – a small hoist that fixes both wasted work and wrong damage scaling.
5. **HLTH-J02** · effect_type loader has no copy-from/extend/delete and silently overwrites duplicates – it already breaks the portal storm dungeon tracker in core, and it blocks mod tweaks to every effect.

## 1. Missing JSON bindings & EOC features

### HLTH-J01 · Effect stat/combat mods apply only to some creature kinds (NPC stats, player dodge)
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/avatar.cpp:1180`, `src/character.cpp:3102`, `src/character.cpp:3122`, `src/monster.cpp:3535`, `src/effect.cpp:348`, `src/effect.cpp:556`, `data/json/effects.json:4152`, `doc/JSON/EFFECTS_JSON.md:645`

Effect `str_mod`/`dex_mod`/`per_mod`/`int_mod` are applied only in `avatar::reset_stats`. `npc` does not override `reset_stats`, and `Character::reset_stats` never reads these mods, so drunk, hallucinating or poisoned NPCs get none of the JSON stat penalties. The reverse also happens: `dodge_mod`/`hit_mod`/`bash_mod`/`cut_mod`/`size_mod` are read only by `monster::process_one_effect` and ignored for all characters. The core `grabbed` effect, applied to the player, has `"dodge_mod": [-10]`, and that penalty does nothing. The docs list these mods with no creature restriction.

```cpp
// avatar.cpp:1180-1189 (avatar only)
mod_str_bonus( it.get_mod( "STR", reduced ) ); mod_dex_bonus( ... ); ...
// monster.cpp:3536 (monster only)
mod_dodge_bonus( get_effect( "DODGE", reduced ) );
```

**Fix:** Move the STR/DEX/PER/INT effect loop from `avatar::reset_stats` into `Character::reset_stats`. Apply DODGE/HIT to characters through `mod_dodge_bonus`/`mod_hit_bonus`. Decide what bash/cut/size mean for characters, or document them as monster-only. Add a table to EFFECTS_JSON.md showing which mods apply to avatar, NPC and monster.

### HLTH-J02 · effect_type loader: no copy-from/extend/delete, silent duplicate overwrite, weak checks
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/effect.cpp:1535`, `src/effect.cpp:1640`, `src/init.cpp:401`, `src/effect.cpp:758`, `src/effect.cpp:593`, `src/effect.cpp:600`, `data/json/effects.json:5230`, `data/json/effects.json:5958`, `data/json/effects_on_condition/nether_eocs/portal_storm_effect_on_condition.json:1045`, `data/mods/MindOverMatter/effects/effects_edited.json:1`

`load_effect_type` builds a fresh `effect_type` every time, with no `was_loaded`, `copy-from` or extend/delete, and assigns `effect_types[id]` unconditionally. This already breaks core content: `effect_portal_storm_dungeon_time_tracker` is defined twice in effects.json. The second definition has no `max_intensity`, so the cap drops from 250000 to the default 1, and the `EOC_PORTAL_STORM_DUNGEON_CRAWLING_TRACKER` switch cases 5, 10, ... can never fire. Mods cannot tweak an effect: MindOverMatter re-declares the full `taint` and `LIXA_illuminated` just to add `immune_flags`. Its id-only overrides for tindrift/attention/teleglow would wipe core fields if those ever gained any (latent today, since the core entries are stubs). Validation is minimal. An invalid top-level `"rating"` calls `rate.value()` on an empty optional and throws `std::bad_optional_access` instead of a JSON error. `verify()` checks only `death_event` and passes a stray `"lol"` as the first format argument. `removes_effects`, `blocks_effects`, `resist_effects`, `resist_traits`, vitamin ids and limb score ids are never validated.

```cpp
effect_type new_etype;                               // effect.cpp:1537
...
effect_types[new_etype.id] = new_etype;              // effect.cpp:1640
apply_msgs.emplace_back( msg, rate.value() );        // effect.cpp:760
debugmsg( "Invalid death event type %s in effect_type %s", "lol", ... ); // :600
```

**Fix:** Port `effect_type` to `generic_factory<effect_type>`: `optional( jo, was_loaded, ... )` for every field, `"copy-from"`, and `"extend"`/`"delete"` for `flags`, `immune_flags`, `immune_bp_flags`, `removes_effects`, `blocks_effects`, `resist_traits`, `resist_effects` and `enchantments`. Report duplicate ids within one mod. Validate every referenced id in `check()`. Replace `rate.value()` with `jo.throw_error_at( "rating", ... )`. Then delete the duplicate tracker definition.

### HLTH-J03 · Diet, allergy and edibility rules hardcoded per trait; no JSON veto hook for eating
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/consumption.cpp:827`, `src/consumption.cpp:858`, `src/consumption.cpp:898`, `src/consumption.cpp:978`, `src/consumption.cpp:582`, `src/consumption.cpp:621`, `src/consumption.cpp:1577`, `src/consumption.cpp:231`

Most food-related mutation rules are C++. `allergy_type()` maps 6 traits to allergen vitamins and morale types (in an array declared with size 8). `can_eat()` and `fun_for()` hardwire the THRESH_CATTLE/FELINE/LUPINE/RABBIT/MOUSE/RAT/BIRD ↔ food-flag pairs that allow INEDIBLE food and invert bad taste. The VEGAN blacklist, PICKYEATER, PROBOSCIS, RUMINANT/HERBIVORE and carnivore vitamin blacklists, SAPROPHAGE, GOURMAND fun multipliers and PRED1-4 raw-meat morale are also hardcoded. The only generic hooks are `can_only_eat`, `vitamin_absorb_multi` and fixed-behaviour CARNIVORE/HERBIVORE flags. Consumption EOCs run after ingestion, as the code itself notes, so there is no data-driven way to refuse food. A mod adding a reptile threshold or a fish allergy must patch C++.

```cpp
static const std::array<allergy_tuple, 8> allergy_tuples = {{
    std::make_tuple( trait_VEGETARIAN, vitamin_meat_allergen, morale_antimeat ), ...
( food.has_flag( flag_CATTLE ) && has_trait( trait_THRESH_CATTLE ) ) ||
// Consumption EOCs run too late to refuse the food without ingesting it.
```

**Fix:** Add mutation fields:
```json
"food_allergies": [ { "vitamin": "fish_allergen", "morale": "morale_antifish" } ],
"edible_food_flags": [ "REPTILE" ],
"forbidden_food_flags": [ "ANIMAL_PRODUCT" ],
"food_fun_multiplier": { "positive": 1.5, "negative": 0.5 }
```
`edible_food_flags` bypasses INEDIBLE and inverts negative fun; `forbidden_food_flags` takes a message. Add a comestible/mutation `"eat_condition": { "condition": { ... }, "fail_message": "..." }`, evaluated in `can_eat` with the food as the `npc_` talker.

### HLTH-J04 · Wounds: no validation, hardcoded bleeding detection, no events or EOC hooks
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/wound.cpp:95`, `src/wound.cpp:363`, `src/wound.cpp:476`, `doc/JSON/WOUNDS.md:53`

`wound_type::check()` is empty, so `wound_progression` ids, `progress_timer.id`, `mod_values` vitamin ids and limb score ids are never validated. `whitelist_flags` is documented as "any of these flags" but the code requires all of them (latent; current data uses single flags). Whether a wound counts as bleeding is hardcoded to the `blood` and `redcells` vitamins. There are no event_bus events or EOC fields for wound applied, progressed or healed, and `mod_values` supports only PAIN/BLEED/VITAMIN. EOCs have `u_add_wound`/`u_remove_wound`/`u_has_any_wound` but cannot react to wound lifecycle. The broken `cap_min`/`cap_max` logic is covered in HLTH-B09.

```cpp
void wound_type::check() const { }                         // wound.cpp:95-98
for( ... whitelist_flags ) { if( !who->has_flag( fl_wl ) ) { ... return false; // :476-482
```

**Fix:** Validate referenced ids in `check()`. Fix either the docs or the code for `whitelist_flags`, and consider an explicit `"whitelist_flags_any"`. Add an optional `"bleeding": true` override. Add events `wound_gained`/`wound_progressed`/`wound_healed` `{ character, bodypart, wound }`, plus `"effect_on_conditions": { "apply": [ ... ], "heal": [ ... ] }` and a `mod_values` type `"EOC"`.

### HLTH-J05 · Vitamin loader rejects redefinition and lacks copy-from and validation
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vitamin.cpp:53`, `src/vitamin.cpp:92`, `src/vitamin.cpp:104`

`vitamin::load_vitamin` throws `"parsed vitamin overwrites existing definition"` on a duplicate id, so a mod cannot rebalance a core vitamin's rate, min/max, disease thresholds or deficiency/excess effect, and cannot use `copy-from`. `check_consistency` validates only the deficiency and excess effect ids. `decays_into` targets (used at `consumption.cpp:731`), `min < max`, disease tier ordering and range, and `flags` are never checked; a bad `decays_into` id only produces a runtime debugmsg.

```cpp
if( vitamins_all.find( vit.id_ ) != vitamins_all.end() ) {
    jo.throw_error_at( "id", "parsed vitamin overwrites existing definition" );
```

**Fix:** Use `generic_factory<vitamin>` with `was_loaded`/`optional()` so later mods can override and `copy-from` works. In `check()`, validate `decays_into` ids, `min < max`, `disease`/`disease_excess` pairs within range and non-overlapping, and known flags (NO_DISPLAY, OBSOLETE, or registered json_flag).

### HLTH-J06 · Lethal overdose/stimulant thresholds hardcoded instead of effect chance_kill or tunables
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character_health.cpp:1630`, `src/character_health.cpp:1633`, `src/character_health.cpp:1639`, `src/character_health.cpp:1645`, `src/character_health.cpp:1656`, `src/character_health.cpp:1662`, `src/suffer.cpp:1341`, `src/suffer.cpp:1344`, `src/suffer.cpp:1373`, `src/effect.cpp:1600`, `data/json/effects.json:3917`

`check_needs_extremes` kills outright at stim > 250, stim < -200, pkill > 240, jetinjector > 40 min, adrenaline > 50 min and drunk intensity > 4. `from_stimulants` runs fixed symptom ladders at stim 210/170/110/75 and -160/-120/-85/-60, and pkill 200/160/145/130. Effect JSON already supports `chance_kill`, `chance_kill_resist`, `death_msg` and `death_event`, but these deaths bypass it. Drunk is the clearest case: `max_intensity` 5 is set in JSON, but `> 4` makes the top intensity always lethal. A modder cannot move any lethal point without C++.

```cpp
if( get_stim() > 250 ) {                                   // :1633
} else if( get_stim() < -200 || get_painkiller() > 240 ) { // :1639
} else if( get_effect_int( effect_drunk ) > 4 ) {          // :1662
```

**Fix:** Express the drunk, adrenaline and jetinjector deaths with `"chance_kill"` plus `"death_event": "dies_from_drug_overdose"` on their effect JSON. Move the stim/pkill limits into enchantment values (`STIM_LETHAL_HIGH`, `STIM_LETHAL_LOW`, `PKILL_LETHAL`) or a global JSON tunables object. Turn the ladder into data: `"stim_symptoms": [ { "above": 110, "effect": "shakes", "interval": "10 m" } ]`.

### HLTH-J07 · effect_type has no EOC hooks or morale field; content polls with recurring EOCs
**Severity:** Medium · **Effort:** Medium · **Verified:** plausible
**Where:** `src/effect.cpp:1535`, `src/creature.cpp:1981`, `src/creature.cpp:1998`, `src/creature.cpp:2087`, `src/event.h:414`, `src/magic_enchantment.cpp:1417`, `src/effect_on_condition.cpp:121`, `data/json/effects_on_condition/effects_eocs.json:2`

An effect type cannot run an EOC on apply, tick, intensity change or removal, and cannot grant morale. The `character_gains_effect`/`character_loses_effect`/`character_effect_intensity_changed` events fire only for Characters, never monsters, and `character_loses_effect` carries no intensity. Core data polls with recurring EOCs instead (`bad_food_mood_debuff`, `eoc_social_*`/`eoc_asocial_*`, `eoc_conjunctivitis_sting`), each checking `u_has_effect`; these run for the avatar only unless global+`run_for_npcs`, run whether or not anyone has the effect, and lag it. Mitigations exist: EVENT EOCs on those three events already give apply/change/remove hooks for avatar and NPCs, and effect `enchantments` with `intermittent_activation` give a probabilistic tick. What is missing is monster support, a direct morale field and deterministic ticks.

```cpp
if( Character *ch = as_character() ) {
    get_event_bus().send<event_type::character_gains_effect>( ... // creature.cpp:1997
```

**Fix:** Add to effect_type:
```json
"effect_on_conditions": {
  "apply": [ "EOC_A" ],
  "tick": { "interval": "1 m", "eocs": [ "EOC_T" ] },
  "intensity_change": [ ],
  "remove": [ ]
},
"morale": { "type": "morale_social", "bonus": -7, "scaling": -7, "max": -21 }
```
Run them with context vars `effect_id`/`intensity`/`bodypart`/`duration` for avatar, NPC and monster talkers. Keep morale in sync with intensity the way cold/hot already are in `player_morale::on_effect_int_change`.

### HLTH-J08 · Morale: JSON `permanent` flag ignored; trait morale rules hardcoded; no per-type read
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/morale_types.cpp:51`, `src/morale_types.h:29`, `src/morale.cpp:71`, `src/morale.cpp:126`, `src/morale.cpp:332`, `src/morale.cpp:411`, `src/morale.cpp:1084`, `src/morale.cpp:1136`, `src/talker.h:910`, `doc/JSON/NPCs.md:1448`

`morale_type` loads `"permanent"`, but `is_permanent()` has no callers. `player_morale::add` checks a hardcoded set of 8 ids, so `u_add_morale` with duration 0 on any mod morale type hits a debugmsg and does nothing. Trait behaviour is fixed in C++: the morale multipliers for OPTIMISTIC (1.2/0.8), BADTEMPER, NUMB and took_prozac/took_prozac_bad; the clothing-constraint penalties for VANITY/FLOWERS/ROOTS1-3/CHLOROMORPH/LEAVES2-3 with hardcoded body parts and amounts; and the MASOCHIST/CENOBITE pain→morale and RADIOPHILE rad→morale formulas. No condition or math function reads one morale type's value. (The no-op `morale()` assignment for characters is documented as intentional in NPCs.md:1448.)

```cpp
optional( jo, was_loaded, "permanent", permanent, false );   // never read
static const std::set<morale_type> permanent_morale = {{ morale_perm_optimist, ... }};
if( ( duration == 0_turns ) && !is_permanent_morale( type ) ) { debugmsg( ...
static const morale_mult optimist( 1.2, 0.8 );
```

**Fix:** Make `is_permanent_morale()` return `type->is_permanent()` and add `"permanent": true` to the 8 core types. Add enchantment values `MORALE_POSITIVE_MULT`/`MORALE_NEGATIVE_MULT` consumed in `get_temper_mult`; prozac becomes an effect enchantment. Add mutation fields `"covered_morale_penalty": [ { "bodyparts": [ "head" ], "penalty": 10 } ]` and `"pain_to_morale": { "cap": 20 }`. For EOCs add math `u_morale_value('morale_wet')` and condition `{ "u_has_morale": "morale_x" }`.

### HLTH-J09 · Common cold/flu hardcoded outside disease_type; disease_type lacks incubation/contagion/EOC
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/character_health.cpp:356`, `src/character_health.cpp:1841`, `src/player_hardcoded_effects.cpp:167`, `src/monster.cpp:3579`, `src/disease.cpp:33`, `src/disease.cpp:59`, `src/consumption.cpp:1384`, `doc/JSON/JSON_INFO.md:1548`

`get_sick(bool)` hardcodes the rarity formula, DISRESISTANT, incubation (18-36 h), durations (flu 3-10 d, cold 1-14 d) and the queued effect ids as strings. Contagion comes from `fake_common_cold`/`fake_flu`, which only ever infect the avatar ("No NPCs for now"). `disease_type` loads only duration, intensity, health_threshold, symptoms and affected_bodyparts; it is reachable only through comestible `contamination` (no EOC, no monster attack hook) and has no incubation, immunity or contagion. `check_disease_consistency` validates only symptoms, and the doc example uses the invalid bodypart id `"TORSO"`.

```cpp
queue_effect( "pre_flu", incubation_period, warning_period );
queue_effect( "flu", ... ); queue_effect( "flushot", ... );
avatar &you = get_avatar(); // No NPCs for now.
```

**Fix:** Extend disease_type with `"incubation": [ "18 h", "36 h" ]`, `"prodrome_effect": "pre_flu"`, `"immunity_effect": "flushot"`, `"immunity_duration": "1 season"`, `"resist_flags": [ "DISEASE_RESIST" ]` and a `"rarity"` math expression, then re-express cold/flu in JSON. Add an effect field `"contagious": { "disease": "flu", "radius": 1 }` that applies to any nearby character. Add EOC `{ "u_expose_to_disease": "flu" }`, allow diseases in monster `attack_effs`, and validate `affected_bodyparts`.

### HLTH-J10 · EOCs can't read stomach contents, wound progress or single morale types, or wake a character
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/math_parser_diag.cpp:1876`, `src/math_parser_diag.cpp:1880`, `src/condition.cpp:2366`, `src/npctalk.cpp:7549`, `src/character_health.cpp:2453`, `src/event.h:191`, `src/event.h:524`

Stomach and gut volume, capacity, kcal and water are unreadable (`u_fill_stomach` writes only). EOCs cannot read a wound's healing progress or pain, or one morale type's value, and cannot set addiction intensity (`addiction_intensity` has no assigner, while `addiction_turns` does). There is no effect to wake a character; `wake_up` is only an NPC talk_function, and `u_lose_effect: sleep` skips the narcosis guard, lying_down/alarm cleanup and the `character_wakes_up` event. The events are thin: `character_eats_item`/`character_consumes_item` carry only `{ character, itype }`, and `character_wakes_up` carries only `{ character }`.

```cpp
{ "addiction_intensity", { "un", 1, addiction_intensity_eval } },   // no _ass
// event.h:191-196 event_spec_character_item fields = { character, itype }
```

**Fix:** Add math functions `u_stomach_volume()`, `u_stomach_capacity()`, `u_stomach_kcal()`, `u_guts_kcal()`, `u_stomach_water()`, `u_wound_healing('scratch', 'bodypart': 'arm_l')` (read/write), `u_morale_value('morale_x')` and an `addiction_intensity` assigner. Add effect `{ "u_wake_up": true }` calling `Character::wake_up()`. Extend the eat/consume events with `kcal`, `quench`, `fun`, and `character_wakes_up` with `reason` and `duration`.

### HLTH-J11 · Addiction lifecycle constants hardcoded; addiction intensity read-only for EOCs
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/suffer.cpp:428`, `src/suffer.cpp:2095`, `src/addiction.cpp:77`, `src/addiction.cpp:338`, `src/effect.cpp:1423`, `src/character.cpp:6895`, `src/math_parser_diag.cpp:1880`

Every addiction shares one hardcoded lifecycle: a dose sates for 2 h (ADDICTIVE 1 h, NONADDICTIVE 6 h), strength is doubled or halved by those traits, intensity rises only when strength > intensity*rng(2,5), withdrawal recovery ends at -6 h (-10 h / -3 h), and intensity decays 10 min per level and is removed at ≤ 2. There is no ADDICTION enchantment value. Seven addictions still run C++ builtins (their shared dream timers are HLTH-B16). Painkiller tolerance is hardwired to opiate addiction, with a TODO to JSONify it. EOCs cannot set intensity. Withdrawal behaviour itself is moddable via add_type `effect_on_condition`; only the lifecycle is not.

```cpp
time_duration timer = -6_hours;
if( you.has_trait( trait_ADDICTIVE ) ) { timer = -10_hours; } else if( ... NONADDICTIVE ) { timer = -3_hours; }
// TODO: convert this to JSON id's and values once we have JSON'ed addictions
mod = it.get_addict_mod( "PKILL", addiction_level( addiction_opiate ) );
```

**Fix:** Add add_type fields `"dose_satiation": "2 h"`, `"recovery_time": "6 h"`, `"intensity_decay": "10 m"`, `"max_intensity"` and `"remove_below"`. Replace the trait checks with enchantment values `ADDICTION_STRENGTH`/`ADDICTION_RECOVERY`. Add effect field `"addiction_tolerance": { "addiction": "opiate", "stat": "PKILL", "per_intensity": 2.0 }`. Add the `addiction_intensity` assigner and port the 7 builtins to EOCs.

### HLTH-J12 · Sleep rules hardcoded to specific traits and effects (light, alarms, recovery)
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/player_hardcoded_effects.cpp:966`, `src/player_hardcoded_effects.cpp:999`, `src/player_hardcoded_effects.cpp:1048`, `src/player_hardcoded_effects.cpp:1505`, `src/sounds.cpp:652`, `src/character_health.cpp:296`, `src/character_health.cpp:1457`, `src/character_health.cpp:1472`, `src/character_health.cpp:1496`, `src/sleep.cpp:145`

Waking from light is tied to trait ids (HEAVYSLEEPER2 ×1.25, HIBERNATE ×5) with exemptions for CHLOROMORPH, bio_sleep_shutdown and narcosis. Outdoor sleep perks belong to CHLOROMORPH/M_SKIN3/WATERSLEEP, and WATERSLEEP's -3 sleepiness is applied twice (here and in `update_needs`), neither checking for water. Alarm, noise and pain wake chances use dice by HEAVYSLEEPER/HEAVYSLEEPER2/bio_sleep_shutdown. Recovery multipliers are tied to effect ids (disrupted_sleep/recently_coughed ×0.5, melatonin ×0.8). `comfort_data` conditions are a closed set of 7 categories. Note that recovery can already be modded via the `SLEEPINESS_REGEN` enchantment, so disrupted_sleep and melatonin could simply carry it.

```cpp
if( u.has_trait( trait_HEAVYSLEEPER2 ) && !u.has_trait( trait_HIBERNATE ) ) { if( ( tirednessVal * 1.25 ) < ...
( has_trait( trait_HEAVYSLEEPER ) && dice( 3, 15 ) < volume ) || ( has_trait( trait_HEAVYSLEEPER2 ) && dice( 6, 15 ) < volume )
if( has_effect( effect_melatonin ) ) { recovered *= .8; }
```

**Fix:** Add enchantment values `SLEEP_LIGHT_TOLERANCE` (multiplier on tirednessVal) and `WAKE_RESISTANCE` (extra d15 dice for alarms and noise). Move the disrupted_sleep/melatonin multipliers onto `SLEEPINESS_REGEN` enchantments on those effects. Add mutation `"sleep_eocs"` run every N while asleep to replace the CHLOROMORPH/M_SKIN3/THRESH_MYCUS blocks, and fix the double WATERSLEEP. Add a comfort condition `{ "type": "condition", "condition": { ... } }`.

### HLTH-J13 · ~30 effects implemented by id in player_hardcoded_effects.cpp
**Severity:** Medium · **Effort:** Large · **Verified:** confirmed
**Where:** `src/player_hardcoded_effects.cpp:1146`, `src/player_hardcoded_effects.cpp:1176`, `src/player_hardcoded_effects.cpp:399`, `src/player_hardcoded_effects.cpp:1328`, `src/player_hardcoded_effects.cpp:1401`, `src/player_hardcoded_effects.cpp:1273`, `src/player_hardcoded_effects.cpp:287`, `src/player_hardcoded_effects.cpp:495`, `src/player_hardcoded_effects.cpp:1200`, `src/player_hardcoded_effects.cpp:240`, `src/effect.cpp:1519`, `data/json/effects.json:3854`

`Character::hardcoded_effects` dispatches 14 effects through a map and about 19 more through an if-chain. Core teleglow, attention, tindrift and evil are bare `{type,id}` stubs whose whole behaviour is C++. The underlying patterns are generic: per-bodypart/per-intensity stat penalty tables (cold/hot/frostbite), random flavour messages, progression into another effect (bite→infected), cure/slowdown modifiers from other effects (antibiotic tiers, panacea), monster-group spawns, and hallu stages tied to a fixed 6 h duration (see HLTH-B11). Many of these can already be approximated with recurring EOCs, `effect_duration`/`effect_intensity` math, `x_in_y_chance`, `u_spawn_monster` and math-driven enchantments; the movement-impairing id set is intentionally C++.

```cpp
static const std::map<efftype_id, hc_effect_fun> hc_effect_map = {{ { effect_onfire, ...
} else if( has_effect( effect_strong_antibiotic ) ) { recover_factor += 400;
// TODO: Redo this to allow for variable durations
```

**Fix:** Port the trivial cases (tapeworm, bloodworms, paincysts, brainworms, tetanus, meth, evil) to EOCs first. Then add effect fields where EOCs are awkward:
```json
"bp_mods": { "torso": { "base_mods": { "dex_mod": [ -2 ] } } },
"periodic_messages": [ { "text": "Your bowels ache.", "chance": 3072, "intensities": [ 1, 3 ] } ],
"progression": { "after": "6 h", "into": "infected", "keep_bodypart": true },
"recovery": { "chance": [ 100, 648000 ], "modifiers": [ { "effect": "antibiotic", "add": 200, "progress_rate": 0.125 } ] }
```
Combine these with the tick hooks from HLTH-J07.

### HLTH-J14 · Per-turn mutation side effects hardcoded in suffer.cpp though processed_eocs exist
**Severity:** Medium · **Effort:** Large · **Verified:** confirmed
**Where:** `src/suffer.cpp:318`, `src/suffer.cpp:459`, `src/suffer.cpp:617`, `src/suffer.cpp:715`, `src/suffer.cpp:1070`, `src/suffer.cpp:1165`, `src/suffer.cpp:1731`, `src/suffer.cpp:1785`, `src/suffer.cpp:1849`, `src/character_health.cpp:3104`

suffer.cpp hardcodes about 35 trait ids for per-turn behaviour: `while_awake` (NARCOLEPTIC, JITTERY, MOODSWINGS, VOMITOUS, SHOUT1-3, M_SPORES, M_BLOSSOMS, CHEMIMBALANCE), `from_other_mutations` (SHARKTEETH, WINGS_INSECT_active, SORES, WEB_WEAVER, SNAIL_TRAIL, PER_SLIME, MUCUS_SECRETION, UNSTABLE/CHAOTIC, PYROMANIA), RADIOACTIVE1-3 (duplicated in `irradiate`), LEAVES/TROGLO sunlight effects, FRESHWATEROSMOSIS, asthma relief by inhaler/oxygen-tank itype id, and `rooted()` nutrients (with a TODO). `mend()` hardcodes cig/drunk and nicotine/alcohol slowdowns although `MENDING_MODIFIER` exists. Mutations already run `processed_eocs` every turn (suffer.cpp:1731), so mods cannot reuse these behaviours and core carries them needlessly.

```cpp
if( you.has_trait( trait_SHOUT1 ) && one_turn_in( 6_hours ) ) { you.shout(); } ...
bool auto_use = you.has_charges( itype_inhaler, 1 ) || you.has_charges( itype_oxygen_tank, 1 ) ||
```

**Fix:** Migrate the simple cases to `processed_eocs` (SHOUT, SHARKTEETH, MOODSWINGS, VOMITOUS, SPORES, PER_SLIME, web and slime fields). Add primitives: mutation `"radioactivity": 2`, `"photosynthesis": { "vitamins": { "vitC": 1 }, "kcal": 43, "requires_uncovered": [ "arm_l", "arm_r" ] }`, `"rooted_nutrition": { ... }`, enchantment `SUNLIGHT_STAT_PENALTY`, and asthma `"relief_items": [ "inhaler" ]`. Give the cig and drunk effects `MENDING_MODIFIER` enchantments.

### HLTH-J15 · Digestion speed and stomach/gut sizes are hardcoded constants with no enchantment hook
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/stomach.cpp:363`, `src/stomach.cpp:487`, `src/stomach.cpp:494`, `src/stomach.cpp:507`, `src/stomach.cpp:574`, `src/character_body.cpp:939`

The stomach passes 1/6 of capacity per half hour and water at 250 ml per 5 min, with no enchantment hook, so a "fast digestion" mutation or a laxative effect cannot change transit speed. Gut absorption (5% × hunger rate) does scale with HUNGER/METABOLISM, though coupled to hunger. The fixed 24 L gut size is only read in debug displays.

```cpp
rates.solids = capacity( owner ) / 6; rates.water = 250_ml;
rates.percent_kcal = 0.05f * metabolic_rates.hunger;
```

**Fix:** Add enchantment values `DIGESTION_RATE` (multiplier on stomach solids/kcal/vitamin transit), `WATER_ABSORPTION_RATE` and `GUT_ABSORPTION_RATE`, applied in `get_digest_rates`.

### HLTH-J16 · Medical UI status tags (BLEEDING/BITTEN/SEPTIC) hardcoded to three effect ids
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/medical_ui.cpp:85`, `src/medical_ui.cpp:262`, `src/medical_ui.cpp:300`, `src/medical_ui.cpp:313`, `src/display.cpp:977`

Limb headers, effect-list priority and `display::limb_color` single out bleed, bite and infected by id with fixed labels and colours. A modded limb effect (venom, burns) gets no limb tag or priority and appears only in the generic list. Cosmetic.

```cpp
const bool bitten = you.has_effect( effect_bite, part.id() );
detail += string_format( _( "[ %s ]" ), colorize( _( "SEPTIC" ), c_pink ) );
```

**Fix:** Add effect field `"limb_status": { "text": "SEPTIC", "color": "pink", "priority": 2 }` and render any effect on the body part that defines it, sorted by priority.

## 2. Bugs, oversights & quirks

### HLTH-B01 · DISTRIBUTED_DAMAGE: integer division and minor parts make SLIMESPAWNER immune to hits under 12
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character_health.cpp:2657`, `src/character_health.cpp:2665`, `src/character_health.cpp:2667`, `src/character_health.cpp:2675`, `src/string_id.h:340`

`apply_damage` counts every body entry including eyes, mouth, hands and feet (12 for a human and for the slime body) and computes `dam / num_limbs` with integer division, so any hit under 12 deals 0. The `if( bp->main_part )` guard converts a `bodypart_str_id` to bool, which is always true, so shares also land on minor parts whose HP never counts toward death; vital parts effectively get about 12× HP. The `character_takes_damage` event still reports the undistributed damage. Vanilla SLIMESPAWNER (mutations.json:7007) and Xedra undine effects grant this flag, so these characters ignore most zombie hits.

```cpp
const int dam_per_part = dam / num_limbs;
for( const bodypart_id &bp : get_all_body_parts() ) {
    if( bp->main_part ) {
        mod_part_hp_cur( bp, -dam_per_part );
```

**Fix:** Distribute over `get_all_body_parts( get_body_part_flags::only_main )` and carry the remainder (give `dam % n` to random parts, or `roll_remainder( dam / float( n ) )`). Report the actual per-part damage in the event.

### HLTH-B02 · Character::mend multiplies healing_factor across limbs
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/suffer.cpp:1918`, `src/suffer.cpp:1933`, `src/suffer.cpp:1935`

Inside the per-limb loop, `healing_factor *= bp->mend_rate;` mutates the shared factor, so every later broken limb inherits the product of earlier limbs' mend rates. With MEND_ALL (mutations.json:4926 or the regen serum), a broken bionic limb (mend_rate 0) zeroes mending for every later broken limb; a vine limb (mend_rate 5) makes later limbs heal 5× faster.

```cpp
for( const bodypart_id &bp : get_all_body_parts() ) { ... healing_factor *= bp->mend_rate;
    const time_duration dur_inc = 1_turns * roll_remainder( rate_multiplier * healing_factor );
```

**Fix:** Use a per-limb local: `const float limb_factor = healing_factor * bp->mend_rate;` and compute `dur_inc` from it.

### HLTH-B03 · Fracture-healing addiction penalty is inverted and never reaches its maximum
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/suffer.cpp:219`, `src/suffer.cpp:226`, `src/suffer.cpp:1872`, `src/suffer.cpp:1878`, `src/cata_utility.h:234`, `src/addiction.h:79`

`mend()` calls `addiction_scaling( 0.25, 0.75, level )`, which is `lerp( at_min, at_max, ( lvl - 3 ) / 20 )`. A level-3 addict gets 0.25 (75% slowdown) and a level-20 addict about 0.675, so heavier nicotine or alcohol addiction heals fractures faster, contrary to the comment ("scales from 25% slowdown to 75% slowdown"). The divisor is `MAX_ADDICTION_LEVEL`, not `MAX - MIN`, so `t` peaks at 0.85. Can change healing speed up to 3×.

```cpp
return lerp( at_min, at_max, ( add_lvl - MIN_ADDICTION_LEVEL ) / MAX_ADDICTION_LEVEL );
healing_factor *= addiction_scaling( 0.25f, 0.75f, addiction_level( addiction_nicotine ) );
```

**Fix:** Call `addiction_scaling( 0.75f, 0.25f, ... )` and divide by `( MAX_ADDICTION_LEVEL - MIN_ADDICTION_LEVEL )`.

### HLTH-B04 · get_bmr applies ceil to the activity multiplier, so any above-rest activity doubles calorie burn
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character_health.cpp:2119`, `src/game_constants.h:146`, `src/character_body.cpp:932`, `src/character_body.cpp:966`, `src/activity_tracker.cpp:113`

`get_bmr` returns `base_bmr() * ceil( clamp( average_activity, 1.0, max ) )`. Walking is MODERATE_EXERCISE (4.0), so one step in an otherwise idle 5-minute window gives an average of about 1.01, ceil 2, and twice the BMR for that window; 2.01 gives 3×. Calorie burn jumps in whole-BMR steps instead of scaling with exertion. Tests use only integer levels and do not catch it.

```cpp
return base_bmr() * std::ceil( clamp( activity_history.average_activity(), NO_EXERCISE, maximum_exertion_level() ) );
```

**Fix:** Drop the ceil (or apply it to the product) for a continuous multiplier, and re-check balance tests that assume stepped behaviour.

### HLTH-B05 · Effect chances ignore max_effective_intensity; a chance that truncates to 0 fires every tick
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/effect.cpp:1294`, `src/effect.cpp:1326`, `src/effect.cpp:1331`, `src/effect.cpp:1346`, `src/effect.cpp:1382`, `src/rng.cpp:128`, `data/json/effects.json:1194`

`get_mod`/`get_avg_mod`/`get_amount` scale by `get_effective_intensity()`, but `activated()`, `get_percentage()`, `get_min_val()` and `get_max_val()` use raw `intensity`, so chance, tick and bounds keep scaling past the documented cap (EFFECTS_JSON.md:129). `CHANCE_TOP` is truncated to int, and a top of 0 with no bot gives `x_in_y( mod, 0 )` = always true, while top ≤ -1 means never. Vanilla `venom_dmg` (max_effective_intensity 100, max_intensity 125) has `hurt_chance` top `13 - 0.11*(i-1)`, which truncates to 0 from intensity 111: a 2200-turn deathrattle bite then deals HURT every 36-turn tick for about 220 turns, and `pain_chance` reaches 1 above about 121.

```cpp
int top = eff_type->get_mod_value( arg, mod_action::CHANCE_TOP, reduced, intensity );
if( !bot ) { return x_in_y( mod, top ); }
return rng_float( 0.0, 1.0 ) <= x / y;   // rng.cpp:128
```

**Fix:** Use `get_effective_intensity()` in `activated`, `get_percentage`, `get_min_val` and `get_max_val`. Clamp a computed top to ≥ 1 when the effect explicitly defines a chance key, and treat only a missing key as "always".

### HLTH-B06 · Morale stacking decides "same sign" from max_bonus, so uncapped or mismatched adds behave wrongly
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/morale.cpp:182`, `src/morale.cpp:195`, `src/morale.cpp:204`, `src/morale.cpp:238`, `src/consumption.cpp:1595`, `src/activity_actor.cpp:13281`, `src/magic_spell_effect.cpp:1602`

`morale_point::add` computes `same_sign = ( bonus > 0 ) == ( new_max_bonus > 0 )`, comparing against the new cap instead of the new bonus. With max_bonus 0 (uncapped, the default), a positive add to a positive point takes the "opposing" branch: linear stacking, no sqrt nerf, no cap, no duration refresh. A positive add to a negative point takes the sqrt branch times `sgn( bonus )` and makes morale worse. Vanilla `add_morale( morale_meatarian, 5, 0 )` stacks +5 per raw-meat meal without limit; `morale_game` and the morale spell effect hit the same path, as does EOC `u_add_morale` with max_bonus 0.

```cpp
const bool same_sign = ( bonus > 0 ) == ( new_max_bonus > 0 );
} else if( new_cap || !same_sign ) { sqrt_of_sum_of_squares = get_net_bonus() + new_bonus; } else { ... *= sgn( bonus ); }
```

**Fix:** Use `( bonus > 0 ) == ( new_bonus > 0 )`. Decide whether max_bonus 0 means uncapped or a cap of 0, and document it in EFFECT_ON_CONDITION.md.

### HLTH-B07 · Becoming incorporeal deletes worn NO_TAKEOFF items instead of dropping them
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/suffer.cpp:1052`, `src/suffer.cpp:1058`, `src/suffer.cpp:1064`, `src/character_attire.cpp:1629`

`from_item_dropping` copies into `tumble_items` only items without NO_UNWIELD, NO_TAKEOFF or INTEGRATED, then calls `i_rem()` on every item in `inv_dump()`. Worn NO_TAKEOFF items are removed and never dropped, so they are destroyed. Vanilla trigger: the portal storm EOC adds `incorporeal` (portal_storm_effect_on_condition.json:778), erasing e.g. `foodperson_mask_on` or NO_TAKEOFF cloaks.

```cpp
if( !dump_item->has_flag( flag_NO_UNWIELD ) && !dump_item->has_flag( flag_NO_TAKEOFF ) && ... ) { tumble_items.push_back( *dump_item ); }
put_into_vehicle_or_drop( you, item_drop_reason::tumbling, tumble_items );
for( item *i : dump ) { you.i_rem( i ); }
```

**Fix:** Only `i_rem` the items that were copied into `tumble_items`, e.g. via a parallel vector of pointers that passed the filter.

### HLTH-B08 · On-hit effect caps use the wrong baseline and pass negative or zero values to add_effect
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character_health.cpp:1121`, `src/character_health.cpp:1125`, `src/character_health.cpp:1131`, `src/character_health.cpp:1135`, `src/character_health.cpp:1141`, `src/creature.cpp:1963`, `src/effect.h:266`

`on_damage_of_type` caps added intensity and duration using `get_effect( eff.id, bp )` on the hit part, but global on-hit effects are stored on the NULL bodypart, so the lookup returns `null_effect` (intensity 1, duration 0). Global effects therefore exceed `max_duration` (documented in JSON_INFO.md:1160-1163), and fresh effects get an intensity cap one level low. When the existing value already exceeds the cap, `max - prev` ≤ 0 is still passed: a negative duration shortens the effect, and since `add_effect` sets (not adds) intensity on an existing effect, the clamped value can lower it. No vanilla on-hit entry uses `global` or `max_intensity`, so this affects mods using the documented fields.

```cpp
int prev_int = get_effect( eff.id, bp ).get_intensity(); ... scaled_int = eff.max_intensity - prev_int;
scaled_dur = eff.max_duration - prev_dur;
add_effect( source, eff.id, 1_turns * scaled_dur, eff.global ? bodypart_str_id::NULL_ID() : bp, false, scaled_int );
```

**Fix:** Look up the effect on the bp it will be added to (NULL for global), treat a missing effect as 0/0, and skip `add_effect` when the clamped increment is ≤ 0.

### HLTH-B09 · Wound mod_values caps never work as documented; is_bleeding always true
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character_health.cpp:2578`, `src/character_health.cpp:2590`, `src/character_health.cpp:2628`, `src/wound.cpp:268`, `src/condition.cpp:931`, `doc/JSON/WOUNDS.md:44`, `data/mods/MindOverMatter/wounds/telepathic.json:13`

VITAMIN `mod_values` skip only when `v <= cap_min && v >= cap_max`, which is impossible for any cap_min < cap_max (defaults INT_MIN/INT_MAX), so vitamin caps are dead. PAIN `mod_values` add pain only while the character's total `get_pain()` (all wounds plus creature pain) is strictly between the caps, instead of applying cap_min to decreases and cap_max to increases as WOUNDS.md:44-45 says. MindOverMatter headaches (pain 2-5, cap_min 5, cap_max 10) therefore never ramp unless the character already has other pain. `Character::is_bleeding` returns true on both paths (no callers yet). The EOC lambda `f_bodypart_is_bleeding` (condition.cpp:931-945) has no `return false` after its loop, which is undefined behaviour for `u_bodypart_is_bleeding` on a non-bleeding part; that one is live.

```cpp
if( vitamin_get( vit ) <= mod_val.cap_min && vitamin_get( vit ) >= mod_val.cap_max ) { break; }
if( get_pain() > mod_val.cap_min && get_pain() < mod_val.cap_max ) { wd.mod_pain( mod_val.amount ); }
```

**Fix:** VITAMIN: skip when `( amount < 0 && v <= cap_min ) || ( amount > 0 && v >= cap_max )`, and clamp the result. PAIN: apply the same rule to the wound's own pain (`wd.get_pain()`), or document the total-pain behaviour. End `is_bleeding` and `f_bodypart_is_bleeding` with `return false`.

### HLTH-B10 · Effect respiratory_rate_* fields do nothing (key mismatch); vital/stamina min_val ignored; stale `mod`
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/effect.cpp:492`, `src/effect.cpp:527`, `src/character.cpp:6895`, `src/character.cpp:6902`, `src/character.cpp:6905`, `src/character.cpp:6911`, `src/character.cpp:6914`, `src/character.cpp:6919`, `src/character.cpp:6921`, `src/character.cpp:6923`, `src/character.cpp:6947`, `src/character_body.cpp:1564`, `src/character_health.cpp:3174`, `doc/JSON/EFFECTS_JSON.md:598`, `data/json/effects_on_condition/medicine_eocs/speed_serum_eoc.json:107`

The loader stores respiratory modifiers under `"RESPIRATORY_RATE"` but `Character::process_one_effect` reads `"RESPIRATION_RATE"`, so every `respiratory_rate_*` field is silently ignored; vanilla `speed_serum_main`/`speed_serum_crash` use them. The BP, HR and respiration blocks pass a hardcoded 0 as the min bound, so `blood_pressure_min_val` (betablock), `heart_rate_min_val` and `respiratory_rate_min_val` do nothing. `stamina_min_val` is documented but not in the STAMINA extract list, so it is rejected as an unvisited member and `get_min_val( "STAMINA" )` is always 0. The three vital blocks never reset `mod`, so their roll chance inherits the PAIN sizing or opiate PKILL factor. No check catches this because `effect_type::verify` only validates `death_event`. Practical impact is small today: `update_respiration_rate` has no callers, respiration and circulation are never read, and heart rate only feeds the bpm readout; fixing the key alone changes no gameplay.

```cpp
extract_effect( to_extract, "RESPIRATORY_RATE", {            // effect.cpp:527
val = get_effect( "RESPIRATION_RATE", reduced );              // character.cpp:6919
bound_mod_to_vals( get_bp_effect_mod(), val, it.get_max_val( "BLOOD_PRESSURE", reduced ), 0 ) // min hardcoded
```

**Fix:** Use one shared enum or constant for effect stat keys in loader and consumer (see HLTH-P06). Pass `it.get_min_val( ... )` for BLOOD_PRESSURE, HEART_RATE and RESPIRATORY_RATE. Add `{ "stamina_min_val", mod_action::MIN_VAL }`. Set `mod = 1;` before each vital block. Add a test that every documented `base_mods` key has a consumer.

### HLTH-B11 · Hallucination effect only works for 6-hour doses; LSD and short hallu sources do nothing
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/player_hardcoded_effects.cpp:289`, `src/player_hardcoded_effects.cpp:337`, `src/player_hardcoded_effects.cpp:342`, `src/suffer.cpp:1564`, `data/json/items/comestibles/med.json:1361`, `data/json/monster_special_attacks/spells.json:1311`

`eff_fun_hallu` uses absolute thresholds based on a 21600-turn dose. Visuals, stat penalties and spawned hallucinations only occur while 6480 < dur < 17280, and stage messages need exact equality with fixed times. Any hallu shorter than about 1.8 h never enters a symptom window. LSD (3600 turns), the taint stare (25-35 min) and sleep-deprivation hallucinations (5-60 min) produce only the JSON limb-score mods. The in-code TODO at line 289 acknowledges this.

```cpp
constexpr int maxDuration = 21600; ... comedownTime = maxDuration * 0.3; ...
} else if( dur > comedownTime && dur < peakTime ) { // Full symptoms ... g->spawn_hallucination(...)
```

**Fix:** Define stages as fractions of the starting duration (or of `max_duration`), or as intensities via `int_dur_factor`, and use range checks instead of `==`. Better, move the stages to JSON (scaling_mods, limb scores, an EOC that spawns hallucinations).

### HLTH-B12 · Microsleep code: missing CANNOT_SLEEP check; deprivation scaling inverted; redundant timer
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character_health.cpp:1749`, `src/character_health.cpp:1759`, `src/character_health.cpp:1807`, `src/character_health.cpp:1816`, `src/character.cpp:5518`, `src/character_body.cpp:263`

At sleepiness ≥ 700, `check_needs_extremes` calls `fall_asleep( 30_seconds )` on `one_in( 50 + INT )` without the CANNOT_SLEEP guard the EXHAUSTED tier has, so CANNOT_SLEEP characters (meth, learning serum) get "You cannot sleep!" and a cancelled activity roughly every 4-5 hours (the function runs every 5 minutes). The deprivation microsleep uses `one_in( pct*75 + INT )`, so microsleeps get rarer with more deprivation, the opposite of the pass-out roll and of the comment's intent. The pass-out roll nests `once_every( 10_minutes )` inside `once_every( 60_minutes )`, so the inner check is always true.

```cpp
if( one_in( 50 + get_int() ) ) { fall_asleep( 30_seconds ); }            // no CANNOT_SLEEP check
if( one_in( 100 + get_int() ) && !has_flag( json_flag_CANNOT_SLEEP ) ) {
if( one_in( static_cast<int>( sleep_deprivation_pct * 75 ) + get_int() ) ...
```

**Fix:** Add `&& !has_flag( json_flag_CANNOT_SLEEP )` at line 1749. Use `( 1 - pct ) * 75` at line 1807. Remove the redundant inner timer or move the roll out of the hourly block.

### HLTH-B13 · NPC morale reads the avatar's flags and body parts
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/morale.cpp:947`, `src/morale.cpp:1007`, `src/morale.cpp:1052`, `src/morale.cpp:1130`, `src/do_turn.cpp:779`, `src/character_body.cpp:716`

Each NPC owns a `player_morale`, but `player_morale::has_flag` returns `get_player_character().has_flag( flag )`, so a heat-immune avatar suppresses `morale_hot` for every hot NPC. `set_worn` and `on_worn_item_washed` iterate the avatar's body parts, so NPCs with different anatomy get wrong covered/filthy bookkeeping. Related: `has_trait_flag` (mutation.cpp:176) checks active/inactive flags against `get_player_character()` rather than `*this`.

```cpp
bool player_morale::has_flag( const json_character_flag &flag ) { return get_player_character().has_flag( flag ); }
for( const bodypart_id &bp : get_player_character().get_all_body_parts() ) {
```

**Fix:** Pass the owning Character into `decay()`, `set_worn()` and `on_worn_item_washed()` (or store a back-pointer) and drop the `get_player_character()` calls.

### HLTH-B14 · vitamin decays_into skips the final unit and fires on every vitamin removal
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/consumption.cpp:729`, `src/consumption.cpp:732`, `src/character_body.cpp:338`, `doc/JSON/VITAMIN.md:115`

In `Character::vitamin_mod`, decay products (`dcy.second * |qty|`) are produced only if the new level is non-zero, so the step that reaches 0 produces nothing, and a clamp at a non-zero min still credits the full `|qty|`. It also runs on every negative `vitamin_mod` (bleeding, EOCs, `vitamin_set`), although VITAMIN.md:115 says only natural decay. NPC catch-up calls `update_body` in 30-minute chunks, so a whole ethanol dose that reaches 0 in one chunk produces no BAC.

```cpp
it->second = std::max( it->second + qty, v.min() );
for( const std::pair<vitamin_id, int> &dcy : v.decays_into() ) {
    if( it->second != 0 ) { vitamin_mod( dcy.first, dcy.second * std::abs( qty ) ); }
```

**Fix:** Compute `const int decayed = old_level - it->second;` and produce `dcy.second * decayed`. Add a `natural` parameter used only by the rate-based decay in character_body.cpp:338 and gate `decays_into` on it.

### HLTH-B15 · vomit() sizes nausea from the stomach after emptying it
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character_health.cpp:2491`, `src/character_health.cpp:2497`

The nausea duration is meant to scale with stomach volume but is computed after `stomach.empty()`, so it is always `int_dur_factor` (3 min) regardless of how much was thrown up. `stomach_contents_before_vomit` exists but is unused here.

```cpp
stomach.empty();
add_effect( effect_nausea, std::max( dummy_nausea.get_max_duration() * units::to_milliliter( stomach.contains() ) / 21, ... ) );
```

**Fix:** Use `stomach_contents_before_vomit`, but rebalance the formula: as written it hits the 15 min cap from about 21 ml, so nearly every vomit would give maximum nausea.

### HLTH-B16 · Function-local statics leak state between characters: bleed noun and addiction dream timers
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/player_hardcoded_effects.cpp:240`, `src/addiction.cpp:77`, `src/addiction.cpp:78`, `src/addiction.cpp:132`, `src/addiction.cpp:133`, `src/addiction.cpp:176`, `src/addiction.cpp:216`, `src/addiction.cpp:267`, `src/suffer.cpp:443`

In `eff_fun_bleed`, `static const translation blood_str` is fixed by whichever character first gets a bleed message, so "Blood"/"Acid" is wrong for everyone else and never updates after gaining ACIDBLOOD. The addiction builtins keep `static time_point last_*_dream` shared by the avatar and all NPCs and never reset on load: an NPC's withdrawal dream suppresses the player's sleeping withdrawal for 2 h, and loading an older save keeps it suppressed until the clock catches up.

```cpp
static const translation blood_str = !u.has_trait( trait_ACIDBLOOD ) ? to_translation( "bleed_message", "Blood" ) : ...;
static time_point last_alc_dream = calendar::turn_zero;
```

**Fix:** Make `blood_str` a non-static local. Store last-dream timestamps per character, e.g. in the serialized addiction object.

### HLTH-B17 · INEDIBLE plus RABBIT, MOUSE or RAT is never edible, contrary to docs
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/consumption.cpp:859`, `src/consumption.cpp:870`, `doc/JSON/JSON_FLAGS.md:544`

`can_eat` accepts INEDIBLE food for matching THRESH_RABBIT/MOUSE/RAT, then rejects any INEDIBLE item lacking CATTLE, FELINE, LUPINE or BIRD. JSON_FLAGS.md:544 says MOUSE/RABBIT/RAT enable eating. Vanilla grains also carry CATTLE, so only mod content is affected.

```cpp
if( !food.has_flag( flag_CATTLE ) && !food.has_flag( flag_FELINE ) &&
    !food.has_flag( flag_LUPINE ) && !food.has_flag( flag_BIRD ) ) {
    return ret_val<edible_rating>::make_failure( _( "That doesn't look edible." ) );
```

**Fix:** Delete the second check (`has_compatible_mutation` already covers it) or add RABBIT, MOUSE and RAT to it.

### HLTH-B18 · Over-eating vomit check uses pre-meal stomach contents
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/consumption.cpp:1101`, `src/consumption.cpp:1215`, `src/consumption.cpp:1392`, `src/stomach.cpp:429`

`will_vomit` is computed before ingestion, and `rng( cap/2, contains ) > cap` can only pass if the stomach was already over capacity, so the first over-capacity meal never risks vomiting. It also compares against whole-stack `food.volume()` rather than the per-serving volume `will_eat` uses (no behavioural effect, but inconsistent). Whether this is intended design is unproven.

```cpp
const bool will_vomit = you.stomach.stomach_remaining( you ) < food.volume() &&
    rng( units::to_milliliter( you.stomach.capacity( you ) ) / 2,
         units::to_milliliter( you.stomach.contains() ) ) > units::to_milliliter( you.stomach.capacity( you ) );
```

**Fix:** Use the per-serving masticated volume and roll against `contains() + that volume`.

### HLTH-B19 · Debug-mode medical UI calls pop_back on a possibly empty string
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/medical_ui.cpp:178`, `src/medical_ui.cpp:183`, `src/medical_ui.cpp:187`, `src/medical_ui.cpp:188`, `src/bodypart.cpp:559`, `src/wound.cpp:35`

In debug mode, `get_limb_wounds` joins each potential wound's damage types and calls `pop_back()` twice without checking for empty. All vanilla wounds resolve to a non-empty list via copy-from, but a modded wound that omits the optional `damage_types` would make this undefined behaviour. `ImGui::Separator()` is also called from inside the string-building helper, drawing a stray separator under the "Limb Wounds:" heading.

```cpp
for( const damage_type_id dt : wd.first.damage_type ) { damage_types += dt.c_str(); damage_types += ", "; }
damage_types.pop_back();
damage_types.pop_back();
```

**Fix:** Use `string_join`/`enumerate_as_string` or guard with `if( !damage_types.empty() )`. Do not drop such wounds from `potential_wounds`, since the debug add/remove-wound menu lists from it.

## 3. Performance

### HLTH-P01 · Effect processing is O(E²): has_flag(FREEZE_EFFECTS) runs twice for every effect, every turn
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/creature.cpp:2151`, `src/creature.cpp:2243`, `src/creature.cpp:2246`, `src/character.cpp:6690`, `src/character.cpp:7560`, `src/effect.cpp:113`, `src/martialarts.cpp:1015`, `src/monster.cpp:1373`, `src/monster.cpp:3603`

Both `Creature::process_effects` and `Character::process_one_effect` evaluate the full `has_flag( json_flag_FREEZE_EFFECTS )` once per effect, although the result is loop-invariant. `Character::has_flag` walks all effects (each via a `std::map` lookup in `efftype_id::obj()`), all body parts, and all effects again for martial-arts buffs (with `substr` plus intern lookup). `monster::process_one_effect` similarly rebuilds the whole enchantment cache once per effect. Cost is roughly tens of µs per character per turn at typical effect counts, multiplied over long waits and sleep. Related bug seen by the reviewer: `monster.cpp:3614` `set_speed_bonus` appears to overwrite the SPEED effect modifier applied at `monster.cpp:3535`.

```cpp
for( auto &elem : *effects ) { for( auto &_it : elem.second ) {
    if( has_flag( json_flag_FREEZE_EFFECTS ) && !_it.second.has_flag( json_flag_FREEZE_EFFECTS ) )
void Character::process_one_effect( effect &it, bool is_new ) { if( has_flag( json_flag_FREEZE_EFFECTS ) ) { return; }
```

**Fix:** Compute `const bool frozen = has_flag( json_flag_FREEZE_EFFECTS );` once in each `process_effects` and pass it down. In `has_effect_with_flag`, use the effect's stored `effect_type` pointer instead of `efftype_id::obj()`. Make `string_id<effect_type>::obj()` O(1) (generic_factory or the string_id cid cache). Move monster enchantment rebuild into `monster::process_effects`.

### HLTH-P02 · Water-weakness check and water_damage() run once per mutation
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/suffer.cpp:229`, `src/suffer.cpp:1725`, `src/suffer.cpp:1726`

In `Character::suffer`, the `WEAKNESS_TO_WATER` query and `suffer::water_damage` sit inside the loop over functioning mutations, and `once_every( 1_seconds )` is always true. Water damage or healing is therefore multiplied by the mutation count, and a character with no mutations never takes it even if the enchantment comes from an item or bionic. This contradicts MAGIC.md:1108 ("once per second") and affects My_Sweet_Cataclysm and Xedra paraclesians. Mainly a correctness bug.

```cpp
for( const trait_id &mut_id : get_functioning_mutations() ) {
    if( calendar::once_every( 1_seconds ) && enchantment_cache->modify_value( enchant_vals::mod::WEAKNESS_TO_WATER, 0 ) != 0 ) {
        suffer::water_damage( *this ); }
```

**Fix:** Move the check and the `water_damage` call above the mutation loop so they run once per turn.

### HLTH-P03 · mutation_power rebuilds the mutation list 3x and clears trait_flag_cache every turn
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/suffer.cpp:250`, `src/suffer.cpp:252`, `src/suffer.cpp:258`, `src/mutation.cpp:163`, `src/mutation.cpp:732`, `src/mutation.cpp:747`, `src/mutation.cpp:754`, `src/mutation.cpp:761`

For each ticking active mutation (or passive one with timed `processed_eocs`), `mutation_power` calls `get_cost_timer` and `mod_cost_timer` (which calls get and `set_cost_timer`); each builds `get_functioning_mutations()` just for a dead debug fallback. `set_cost_timer` also does `trait_flag_cache.clear()` every turn, although charge never affects flags, so every distinct `has_flag` that turn rebuilds the mutation list and rescans flags.

```cpp
const std::vector<trait_id> &all_mut = get_functioning_mutations();   // mutation.cpp:735, 750
iter->second.charge = set; trait_flag_cache.clear();                  // mutation.cpp:753-754
```

**Fix:** Remove `trait_flag_cache.clear()` from `set_cost_timer`. Drop the `all_mut` fallback (it is unreachable). Have `mutation_power` fetch `trait_data &` once and decrement charge in place.

### HLTH-P04 · update_body recomputes vitamin_rate for every vitamin each turn; each call rebuilds the mutation list
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character_body.cpp:327`, `src/consumption.cpp:644`, `src/consumption.cpp:752`, `src/newcharacter.cpp:2107`, `src/npc.cpp:3557`, `src/npc.cpp:3599`, `src/player_hardcoded_effects.cpp:706`, `src/player_hardcoded_effects.cpp:1325`, `src/vitamin.cpp:24`, `src/do_turn.cpp:605`

`update_body` (every avatar turn, every 10 s per NPC) calls `vitamin_rate` for all 57 vitamins before checking whether any tick boundary was crossed. Each call does `vit.obj()` (a map lookup, though the loop already holds the vitamin) and builds a fresh `get_functioning_mutations()` vector. The anemia/hypovolemia effects call it again every turn, and `vitamin_get`/`get_daily_vitamin`/`reset_daily_vitamin` do a string-keyed `get_option( "NO_VITAMINS" )` per call. About 10 µs per avatar turn and a few ms per NPC load (~400 `update_body` calls in `npc::on_load` catch-up).

```cpp
for( const auto &v : vitamin::all() ) { const time_duration rate = vitamin_rate( v.first );
time_duration res = vit.obj().rate(); for( const auto &m : get_functioning_mutations() ) {
```

**Fix:** Cache effective rates per character and invalidate on mutation change (`update_cached_mutations`/`trait_flag_cache.clear()` hook points). At minimum fetch the mutation list once per `update_body` and use `v.second`. Cache NO_VITAMINS.

### HLTH-P05 · NPC sleep-spot scan default-constructs one item per tile and deep-copies items it finds
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npcmove.cpp:2661`, `src/npcmove.cpp:7778`, `src/npcmove.cpp:7786`, `src/npcmove.cpp:7826`, `src/npcmove.cpp:7842`, `src/character.cpp:7186`, `src/character.cpp:7200`, `src/sleep.cpp:81`, `src/sleep.cpp:99`, `src/sleep.cpp:145`, `src/sleep.cpp:314`, `src/sleep.cpp:325`, `src/item.cpp:169`

`npc::find_sleep_candidates` scores up to ~14.6k tiles via `get_comfort_at`, whose one-entry cache always misses. Per tile it rebuilds `get_functioning_mutations()`, default-constructs a full `item sleep_aid`, deep-copies any SLEEP_AID item just to call `tname()`, and re-interns ter/furn/trap/field/flag/trait ids from strings in each comfort condition. It runs when an NPC (re)acquires a sleep plan, with early exit and a cooldown.

```cpp
item sleep_aid; if( add_sleep_aids && try_get_sleep_aid_at( p, sleep_aid ) ) { ... result.sleep_aid = sleep_aid.tname();
result = *item_it;                                          // sleep.cpp:99
result = ter_id( id ) == here.ter( p );                     // sleep.cpp:155
```

**Fix:** Have `try_get_sleep_aid_at` return `const item *` (or the name). Store typed ids in `comfort_data::condition` at load. Compute the mutation-derived comfort list once per scan or cache it per character.

### HLTH-P06 · Effect modifiers are looked up by std::string in a two-level unordered_map
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/effect.h:244`, `src/effect.cpp:527`, `src/effect.cpp:565`, `src/effect.cpp:1294`, `src/effect.cpp:1384`, `src/character.cpp:6710`, `src/character.cpp:6919`, `src/character.cpp:7766`, `src/avatar.cpp:1185`, `src/monster.cpp:3528`

Every JSON modifier is stored in `unordered_map<std::string, unordered_map<uint32_t, array>>`. Per effect per character per turn, `process_one_effect` makes 16 `get_effect( "..." )` calls (each a std::string temporary and two hashed lookups), plus COUGH/VOMIT, STR/DEX/PER/INT and SPEED elsewhere: about 44 string hashes. `"RESPIRATION_RATE"` exceeds SSO and heap-allocates. Monsters pay 14 more lookups per effect. Most lookups miss. Roughly 1 µs per effect per character per turn. String keys are also what allowed the HLTH-B10 key mismatch.

```cpp
std::unordered_map<std::string, std::unordered_map<uint32_t, modifier_value_arr>> mod_data;  // effect.h:244
val = get_effect( "H_MOD", reduced ); ... get_effect( "RESPIRATION_RATE", reduced ); ...
```

**Fix:** Add `enum class effect_mod : uint8_t { STR, DEX, ..., RESPIRATORY_RATE, ..., NUM }`, store modifiers in a flat array indexed by mod and action, and keep a `std::bitset<NUM> has_mod` per effect_type so `process_one_effect` can skip whole blocks. Keep a string overload only for EOC/debug paths.

### HLTH-P07 · will_eat/can_eat recompute the whole nutrient tree several times per food item
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/item.cpp:2383`, `src/item.cpp:5053`, `src/consumption.cpp:258`, `src/consumption.cpp:281`, `src/consumption.cpp:338`, `src/consumption.cpp:348`, `src/consumption.cpp:840`, `src/consumption.cpp:976`, `src/consumption.cpp:1046`, `src/consumption.cpp:1063`, `src/stomach.cpp:59`, `src/npcmove.cpp:5785`, `src/npcmove.cpp:6656`

`item::has_vitamin` runs `default_character_compute_effective_nutrients` over the full component tree, then copies the vitamin map via `vitamins()` to search one key. `will_eat` calls it for the toxin vitamin, human flesh and (through `allergy_type`) up to 6 more; CARNIVORE_DIET adds `has_any_vitamin` checks in `can_eat`. This sits in NPC food scans (per candidate item in radius 6 and per inventory food, already commented "will_eat is costly") and in `tname()` for the 50 core foods with VITAMIN conditional names.

```cpp
const nutrients food_item = default_character_compute_effective_nutrients( *this );
for( auto const& [vit_id, amount] : food_item.vitamins() ) {
const bool food_is_human_flesh = food.has_vitamin( vitamin_human_flesh_vitamin ) ||
```

**Fix:** Compute `const nutrients n = default_character_compute_effective_nutrients( food );` once in `will_eat`/`can_eat` and query `n.get_vitamin()`. Make `has_vitamin` use `get_vitamin(v)` without copying, with a fast path for items without food components. Hoist `get_functioning_mutations` out of the component recursion. Longer term, index nutrients by vitamin ordinal instead of `std::map`.

### HLTH-P08 · Sleep effect computes dream category every sleeping turn but uses it every 1.5-6 hours
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/player_hardcoded_effects.cpp:1007`, `src/player_hardcoded_effects.cpp:1029`, `src/character_health.cpp:2524`, `src/character.cpp:5262`, `src/character.cpp:5272`

`eff_fun_sleep` computes the threshold category, a weighted list over all 30 categories (two `vitamin_get` per category with NO_VITAMINS option lookups), an RNG pick, a map `operator[]` and `crossed_threshold()` every sleeping turn, only to feed `once_every( 6_hours / strength ) && one_in( 3 )`.

```cpp
mutation_category_id cat = u.get_threshold_category();
weighted_int_list<mutation_category_id> cat_list = u.get_vitamin_weighted_categories();
if( calendar::once_every( 6_hours / strength ) && one_in( 3 ) ) {
```

**Fix:** Gate the dream block with `if( calendar::once_every( 30_minutes ) )` (every period is a multiple of 30 min), or compute category and strength only when the timer fires.

### HLTH-P09 · Needs/weariness recomputed several times per turn but only consumed every 5 minutes
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character_body.cpp:926`, `src/character_body.cpp:932`, `src/character_body.cpp:937`, `src/character_body.cpp:1014`, `src/character_health.cpp:1286`, `src/character_health.cpp:1296`, `src/character_health.cpp:1332`, `src/character_health.cpp:1375`, `src/character_health.cpp:1550`, `src/character_health.cpp:2117`, `src/suffer.cpp:1478`, `src/character.cpp:3334`, `src/character_morale.cpp:193`

`update_stomach` calls `calc_needs_rates()` and `get_bmr()` every call, but uses them only when `five_mins > 0`. Each path reaches `weariness_level()`, which does a full `has_flag`, `base_bmr()`, morale level computation, and several `get_option( "WEARY_*" )` lookups (one inside a loop). `from_exertion` calls `weariness_level()` and `maximum_exertion_level()` again. About 4 weariness evaluations and ~20 option lookups per character per turn (avatar every turn, NPCs every 10 s).

```cpp
const needs_rates rates = calc_needs_rates();          // :926
const float kcal_per_time = get_bmr() / ( 12.0f * 24.0f );  // :932, used only inside if( five_mins > 0 )
```

**Fix:** Move `calc_needs_rates()` and `kcal_per_time` inside `if( five_mins > 0 )`. Derive `max_activity` from `new_weary_level` in `from_exertion`. Hoist WEARY_* options into statics refreshed on option change. A per-turn weariness cache must also key on sleepiness and morale, not only activity history.

### HLTH-P10 · recalc_speed_bonus builds effect::disp_name() for every effect every turn even when SPEED is 0
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character.cpp:2297`, `src/character.cpp:7731`, `src/character.cpp:7740`, `src/character.cpp:7766`, `src/effect.cpp:775`

`disp_name()` (translation plus `string_format` for intensity and body part) is evaluated eagerly for every effect and discarded by `mod_speed_bonus` when the speed mod is 0, which is nearly always.

```cpp
mod_speed_bonus( i.second.get_mod( "SPEED", reduced ), i.second.disp_name() );
```

**Fix:** Compute the mod first and call `disp_name()` only when it is non-zero, or store the effect id in `speed_bonus_effect` and format lazily in the UI.

### HLTH-P11 · Effect accessors return vectors by value; add_effect copies a merged vector per existing effect
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/effect.cpp:1037`, `src/effect.cpp:1163`, `src/effect.cpp:1287`, `src/effect.cpp:1648`, `src/effect.cpp:1653`, `src/creature.cpp:1942`, `src/character_health.cpp:1097`, `src/character.cpp:6724`

`effect::get_blocks_effects()` builds a new vector (removes + blocks) on each call, and `Creature::add_effect` and `effect_is_blocked` call it for every active effect. `get_limb_score_data()` and `get_effect_dur_scaling()` also return copies, and `vit_effects()` allocates per effect per turn. Only types with non-empty lists actually allocate.

```cpp
std::vector<efftype_id> effect::get_blocks_effects() const { std::vector<efftype_id> ret = eff_type->removes_effects;
    ret.insert( ret.end(), eff_type->blocks_effects.begin(), eff_type->blocks_effects.end() ); return ret; }
```

**Fix:** Precompute a combined `blocks_or_removes` vector at load time and return it by const reference; return the other vectors by const reference too.

### HLTH-P12 · wound_type accessors return copies; update_wounds copies them per wound per turn
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/wound.cpp:412`, `src/wound.cpp:417`, `src/wound.cpp:422`, `src/wound.cpp:564`, `src/character_health.cpp:2568`, `src/character_health.cpp:2574`, `src/character_health.cpp:2610`, `src/character_health.cpp:2617`, `src/bodypart.cpp:1055`, `src/character.cpp:2341`

`update_wounds` (every turn, every character) copies `get_modify_values()` and `get_periodic_message()` per wound, and always calls `morale->on_stat_change( "perceived_pain", ... )`, which re-walks all wounds and the masochist logic. `wound_adjusted_limb_value` copies `get_limb_scores()` per wound on every limb-score query, and `get_wound_healing_modifier` recomputes `get_lifestyle` per wound.

```cpp
std::vector<wound_mod_values> wound_type::get_modify_values() const { return mod_values; }
morale->on_stat_change( "perceived_pain", get_perceived_pain() );
```

**Fix:** Return const references. Call `on_stat_change` only when there are wounds or the value changed (check that painkiller changes already notify morale). Compute `get_lifestyle` once per `update_wounds`.

### HLTH-P13 · from_radiation scans worn items every turn though the result is only used with rad_mut
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/suffer.cpp:1179`, `src/suffer.cpp:1184`, `src/suffer.cpp:1203`, `src/suffer.cpp:1794`, `src/suffer.cpp:1814`, `src/character.cpp:2296`, `src/character.cpp:3720`

`from_radiation` always runs `is_wearing_power_armor()` and `worn_with_flag( RAD_RESIST )` but reads them only inside `if( rad_mut > 0 )` (rare RADIOACTIVE traits). In irradiated tiles, `irradiate` repeats both scans, `is_rad_immune` adds two more, and `inv_dump()` is built every turn.

```cpp
const bool power_armored = you.is_wearing_power_armor( &has_helmet );
const bool rad_resist = power_armored || you.worn_with_flag( flag_RAD_RESIST );   // used only at :1184
```

**Fix:** Move the computation inside `if( rad_mut > 0 )`. In `irradiate`, compute these only when `rads > 0` and not bypassed, and pass `rad_mut` in from the caller.

### HLTH-P14 · check_and_recover_morale rebuilds a full player_morale every 9 turns
**Severity:** Low · **Effort:** Medium · **Verified:** plausible
**Where:** `src/morale.cpp:265`, `src/morale.h:182`, `src/character_morale.cpp:256`, `src/do_turn.cpp:775`, `src/do_turn.cpp:784`

Every 9 turns for the avatar (and every minute per NPC) a fresh `player_morale` is built: 16 map entries with two `std::function` handlers each (small-buffer, so little heap), a replay of every worn item, mutation and effect, seven string-compared `on_stat_change` calls, and an O(P²) `consistent_with` scan, mostly to confirm nothing changed. It also calls `apply_persistent_morale`, so it cannot simply be made debug-only. Gain is small and unmeasured.

```cpp
player_morale test_morale; worn.check_and_recover_morale( test_morale );
for( const trait_id &mut : get_functioning_mutations() ) { test_morale.on_mutation_gain( mut ); }
```

**Fix:** Move the per-trait handler table into a static const table and keep only per-instance `active` bits. Lower the check frequency, after moving `apply_persistent_morale` to `update_morale` only.

## Appendix: considered and rejected

- Comestible side effects hardcoded (parasite pool, alcohol flags, poison, tapeworm) – already covered by the comestible `contamination` field, KCAL/VITAMIN_ABSORB_MOD enchantments, `consume_drug` and consumption EOCs; only low-value cleanup remains.
- vomit() purges a hardcoded effect list; mod drugs never purged – false: `stomach.empty()` clears undigested drug vitamins, and the `throws_up` event lets mods opt in via EOC.
- from_sunburn re-evaluates has_flag and worn-item scans per body part every turn – the worn scans short-circuit for SUNBURN_SUPERNATURAL characters; the remaining cost is negligible and short-lived.
- Effect-type loading builds ~30 string-key vectors and ~190 JSON probes per effect – a few milliseconds at startup; unknown keys are already flagged by the unvisited-member check.
