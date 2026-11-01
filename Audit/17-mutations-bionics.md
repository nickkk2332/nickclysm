# 17 · Mutations & bionics

> Engine audit – static code reading at commit `a1a646d6`; nothing was compiled or run. Each finding was checked against the code by a separate reviewer: **confirmed** = verified by reading the code; **plausible** = consistent with the code, but reachability or impact not proven. Line numbers refer to that commit.

**Scope:** `src/mutation.cpp`, `src/mutation_data.cpp`, `src/mutation_type.cpp`, `src/mutation_ui.cpp`, `src/bionics.cpp`, `src/bionics_ui.cpp`

## Summary

Both systems work for the base game, but their state handling is fragile. Mutation activation keeps a reference into `cached_mutations` across EOC calls, and shipped Xedra content turns that into a use-after-free write. Deactivation updates only one of the two powered-state maps. Removing a weapon CBM from paths other than the normal UI can leave the weapon stuck in the player's hands. `mutate_towards` prunes traits before running checks that can still fail. Most JSON gaps come from behaviour keyed to hardcoded ids (22 bionics, 13 active mutations, surgery, progression tuning) and from bionic state that EOCs cannot read or change. Performance is mostly fine. The one per-turn cost worth fixing is `set_cost_timer` clearing the trait-flag cache every turn.

| | Critical | High | Medium | Low | Total |
|---|---|---|---|---|---|
| Missing JSON / EOC | 0 | 3 | 9 | 4 | 16 |
| Bugs & quirks | 0 | 5 | 7 | 7 | 19 |
| Performance | 0 | 0 | 1 | 5 | 6 |

**Fix first:**
1. **MUT-B01** · Use-after-free when an activation EOC removes its own trait: shipped Xedra traits trigger it on every dismissal, and the fix is a re-lookup.
2. **MUT-B02** · Deactivation never clears `my_mutations.powered`: ACTIVE-granted traits such as `WINGS_INSECT_active` stay forever, for free, and survive save/load. The fix is a small helper.
3. **MUT-B03** · Removing an extended weapon CBM leaves an un-unwieldable weapon: this soft-locks the player through mutation conflicts or nurse bots. The fix is a few lines in `remove_bionic`.
4. **MUT-J01** · `u_mutate_towards` default category is `""` and `get_category()` dereferences `end()`: this is UB for anyone who follows the docs, and the fix is a one-line default plus a guard.
5. **MUT-B04** · POWER_TRICKLE is applied once per unpowered bionic: five mods get power that depends on unrelated CBM counts. The fix is to move one block.

## 1. Missing JSON bindings & EOC features

### MUT-J01 · `u_mutate_towards`' documented default category "ANY" is really `""`; `get_category()` dereferences `map::end()`
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctalk.cpp:3671`, `src/npctalk.cpp:3672`, `src/dialogue_helpers.cpp:185`, `src/mutation_data.cpp:126`, `src/mutation_data.cpp:137`, `src/mutation_data.cpp:1164`, `src/mutation.cpp:1702`, `doc/JSON/EFFECT_ON_CONDITION.md:3479`

`f_mutate_towards` reads `"category"` into a default-constructed `str_or_var`, which evaluates to an empty string, not `"ANY"` as the docs say. `Character::mutate_towards` then calls `mutation_category_trait::get_category(mut_cat)`, which is `find(id)->second` with no `end()` check. That is undefined behaviour (garbage `base_removal_chance`/vitamin, or a crash) for any EOC that leaves out `"category"`. A typo or a runtime variable that yields an unknown id reaches the same path through `u_mutate_category`, `use_vitamins` and `can_cross_threshold`. `mutation_category_is_valid()` exists but has no callers. Category check_consistency validates only `threshold_mut`, and trait `category` entries are never validated. All 17 shipped uses pass `"category": "ANY"` explicitly, so the form the docs describe is the one that breaks.

```cpp
// npctalk.cpp:3671
str_or_var mut_cat;
optional( jo, false, "category", mut_cat );          // no default -> ""
// mutation_data.cpp:129
return mutation_category_traits.find( category_id )->second;
// mutation.cpp:1702
const mutation_category_trait category = mutation_category_trait::get_category( mut_cat );
```

**Fix:** Default the field: `optional( jo, false, "category", mut_cat, "ANY" )`. Make `get_category()` return a static null category and debugmsg when the id is unknown. Validate every trait's `category`, `allowed_category` and dream `category` in `mutation_branch::check_consistency` with `mutation_category_is_valid()`. In the EOC effects, `is_valid()`-check the evaluated id and debugmsg on failure.

### MUT-J02 · `activated_eocs` cannot control activation: state is overwritten after the EOC, and there is no veto
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/mutation.cpp:866`, `src/mutation.cpp:909`, `src/mutation.cpp:917`, `src/mutation.cpp:392`, `src/mutation.cpp:1041`, `src/bionics.cpp:785`, `src/bionics.cpp:813`, `src/bionics.cpp:963`

Mutations: `activate_cached_mutation` runs the `activated_eocs` and then unconditionally writes `tdata.powered = activated_is_setup`. A `u_deactivate_trait` inside the activation EOC is therefore overwritten. Xedra Evolved works around this with deferred EOCs (`werewolf_trait_eocs.json:33`: "calling u_deactivate_trait from within the trait EoC ... does not work"). If the EOC removes the trait, the write lands on freed memory (see MUT-B01). Bionics: power is deducted at :785 and `activated_eocs` run at :813, before the hardcoded validation branches (lighter, EMP, water extractor, lockpick, teleport while mounted). Those branches can still refund and return false after the EOC side effects have already happened (see MUT-B19). Neither system lets JSON veto an activation or show a failure message.

```cpp
// mutation.cpp:910-917
for( const effect_on_condition_id &eoc : mut->activated_eocs ) { ... eoc->activate_activation_only( d, ... ); }
tdata.powered = mut->activated_is_setup;
// bionics.cpp:970-973
refund_power(); add_msg_if_player( m_info, _( "There's nothing to light there." ) ); return false;
```

**Fix:** Re-look up the cache entry after the EOCs, and only force `powered` if the EOC did not change it (compare with a snapshot taken before the EOC). Add to both mutations and bionics:
```json
"activation_condition": { "not": { "u_has_wielded_with_flag": "CONDUCTIVE" } },
"activation_fail_message": "Your shield fizzles around the metal in your hand."
```
Check it before any cost is paid. Also let an EOC cancel through a context var, e.g. `{ "math": [ "_cancel_activation = 1" ] }`, which the engine reads after the EOCs so it can refund the cost. Run bionic `activated_eocs` after the hardcoded success path.

### MUT-J03 · Bionic state is not reachable from EOCs: no active check, no (de)activate effect, no `this`
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/bionics.cpp:813`, `src/bionics.cpp:1272`, `src/bionics.cpp:1671`, `src/bionics.cpp:1617`, `src/bionics.cpp:720`, `src/condition.cpp:848`, `src/condition.cpp:2617`, `src/npctalk.cpp:8662`

EOCs can test `u_has_bionics` and add or remove bionics. They cannot check whether a bionic is active, activate or deactivate one (mutations have `u_activate_trait`/`u_deactivate_trait`), or read per-instance state such as `incapacitated_time`, the charge timer or the safe-fuel threshold. Bionic EOCs get no `this` context var, unlike mutation EOCs (`mutation.cpp:913`), so a shared EOC cannot tell which CBM fired it. `processed_eocs` and `deactivated_eocs` get no variables at all. `process_bionic` returns early for unpowered bionics, so passive CBMs never run `processed_eocs`. Non-activatable mutations do run theirs (`suffer.cpp:1731`). NPC weapon-CBM activation (`npc::check_or_use_weapon_cbm`) sets `bio.powered = true` directly, skipping `activated_eocs` and the enchantment recalc. NPC deactivation does run `deactivated_eocs`.

```cpp
// bionics.cpp:1617-1620
if( !bio.powered ) { burn_fuel( bio ); return; }   // processed_eocs never reached
// bionics.cpp:724-727 (NPC)
set_wielded_item( cbm_weapon ); mod_power_level( -bio.info().power_activate ); bio.powered = true;
```

**Fix:** Add a condition `u_has_active_bionic` / `npc_has_active_bionic` and math `has_active_bionic('id')` / `bionic_count('id')`. Add effects `{ "u_activate_bionic": "bio_x", "ignore_cost": true }` / `u_deactivate_bionic` that go through `Character::activate_bionic` / `deactivate_bionic`. Set `this` (the bionic id) plus `_react_cost`/`_trigger_cost` on all three bionic EOC lists. Run `processed_eocs` for passive bionics, with an optional `"processed_interval": "1 m"`. Route the NPC weapon path through `activate_bionic(eff_only)`.

### MUT-J04 · Mutation internals hidden from EOC math: instability, category level, cooldown, active state
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mutation.cpp:214`, `src/mutation.cpp:731`, `src/mutation.cpp:2451`, `src/math_parser_diag.cpp:1916`, `src/condition.cpp:501`, `src/talker.h:261`, `src/character.cpp:5262`

The mutation pipeline depends on per-category instability (`get_instability_per_category`), `mutation_category_level` (the "breach power" that gates thresholds and dreams) and per-trait cost timers. Math can read none of them. The only mutation math functions are `has_trait`, `sum_traits_of_category` and `sum_traits_of_category_char_has`. No condition asks whether a trait is *active*, e.g. "is my Shell retracted?". Cooldowns cannot be read or reset, so a "refresh cooldowns" spell or a remaining-time readout cannot be built. (Which threshold was crossed is already answerable via `has_trait('THRESH_X')`.)

```cpp
// mutation.cpp:2451
int breach_power = mutation_category_level[mutation_category];
```

**Fix:** Add math `mutation_instability('ALPHA')`, `mutation_category_level('ALPHA')`, `trait_cooldown('ID')` (readable and assignable, in turns) and `has_active_trait('ID')`. Example: `{ "math": [ "u_trait_cooldown('SHELL2') = 0" ] }`.

### MUT-J05 · `known_ma_styles` is hardcoded to `bio_cqb` and only works while powered; Xedra datacore grants nothing
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/bionics.cpp:914`, `src/bionics.cpp:1294`, `src/character.cpp:8452`, `data/mods/Xedra_Evolved/player/cbms.json:90`

The style picker on activation and `selected_style_check()` on deactivation only run for `bio.id == bio_cqb`. `pick_style()` only offers styles from bionics that are currently active. A modded martial-arts CBM therefore gets no picker on activation, and the player keeps the style after deactivating it. Xedra's "Datacore: Muay Thai" uses `activate_remove_cbm`, so activating it uninstalls it. It can never be powered, and its promised style never applies. `martialarts.cpp` hardcodes `bio_cqb` in four more places (755, 876, 1689, 2347).

```cpp
// character.cpp:8452
if( !bio_ma_list.empty() && you.has_active_bionic( bio.id ) )
```

**Fix:** Key the picker and the style check on `!bio.info().ma_styles.empty()`. Add `"learned_ma_styles": [ "style_muay_thai" ]`, applied on install and removed on uninstall like `learned_spells`/`learned_proficiencies`, or a `"ma_styles_passive": true` flag.

### MUT-J06 · 13 active-mutation effects and 2 passive effects are keyed to trait ids in C++
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/mutation.cpp:929`, `src/mutation.cpp:932`, `src/mutation.cpp:948`, `src/mutation.cpp:982`, `src/mutation.cpp:1011`, `src/mutation.cpp:587`, `src/mutation.cpp:691`, `src/mutation.cpp:695`, `src/character_health.cpp:396`

`activate_cached_mutation` hardcodes WEB_WEAVER, LONG_TONGUE2/GASTROPOD_EXTREMITY2/3 (pull_creature activity), SNAIL_TRAIL, BURROW/BURROWLARGE, SLIMESPAWNER, NAUSEA/VOMITOUS, M_FERTILE/M_BLOOM/M_PROVENANCE, ECHOLOCATION and TREE_COMMUNION/ARVORE_FOREST_MAPPING. Tree communion's startup times are also keyed to the ROOTS2/ROOTS3/CHLOROMORPH/TREE_COMMUNION_PLUS flags. `mutation_effect`/`mutation_loss_effect` hardcode ACIDBLOOD (blood type) and GLASSJAW (HP recalc, plus a 0.8 head-HP factor in `character_health.cpp:396`). Modded traits cannot reuse these. Most already have EOC equivalents (`u_vomit`, `u_spawn_monster`, `u_set_field`). Only pull, burrow and tree communion, plus blood type, need new bindings. The SLIMESPAWNER no-room and TREE_COMMUNION no-tree failures also keep the cost already charged at :882-899.

```cpp
// mutation.cpp:948
} else if( mut == trait_SLIMESPAWNER ) { monster *const slime = g->place_critter_around( mon_player_blob, ...
```

**Fix:** Move vomit/spores/blossoms/slime/web/sludge into the traits' `activated_eocs`/`processed_eocs`. Add EOC effects `{ "u_pull_creature": { "range": 3, "moves": 100 } }`, `u_burrow`, `u_tree_communion` (or a generic activity-actor assignment). Add mutation fields `"blood_type": "acid"` and `"hp_mod": [ [ "head", 0.8 ] ]` (or a per-bodypart MAX_HP enchantment) to replace ACIDBLOOD/GLASSJAW.

### MUT-J07 · Mutation/CBM events fire on some code paths only; EOC add/lose bypasses install semantics
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/mutation.cpp:2059`, `src/mutation.cpp:1986`, `src/mutation.cpp:2511`, `src/mutation.cpp:316`, `src/npctalk.cpp:3539`, `src/talker_character.cpp:1253`, `src/bionics.cpp:2251`, `src/bionics.cpp:2610`, `src/bionics.cpp:2851`, `src/bionics.cpp:2954`

`gains_mutation` is sent only from `mutate_towards` when `!mutation_replaced`. Evolutions (Strong -> Very Strong) send `evolves_mutation` + `loses_mutation` but never `gains_mutation`. Threshold crossing, `u_add_trait`, `toggle_trait` and `set_mutation` never send it either, so an event EOC listening for `gains_mutation` of `VERY_STRONG` never fires. Core `mutation_effect_eocs.json` relies on that event. On the bionic side, `installs_cbm`/`removes_cbm` and the install semantics (removing `canceled_mutations`, granting `give_mut_on_removal`, cascade-removing dependent CBMs) all live in `perform_install`/`perform_uninstall`. `u_add_bionic`/`u_lose_bionic` call `add_bionic`/`remove_bionic` directly and skip all of it. No event exists for bionic or mutation (de)activation.

```cpp
// mutation.cpp:2511-2512
set_mutation( mutation_thresh );
get_event_bus().send<event_type::crosses_mutation_threshold>( ... );   // no gains_mutation
```

**Fix:** Send `gains_mutation` from one place (`set_mutation_unsafe` when the trait is new) and `loses_mutation` from `unset_mutation`, removing the scattered sends. Add `{ "u_add_bionic": "bio_x", "as_surgery": true }` (also for `u_lose_bionic`) to apply the full install/uninstall semantics and events. Add events `activates_bionic`/`deactivates_bionic` and `activates_mutation`/`deactivates_mutation` with `{ character, bionic|trait }`.

### MUT-J08 · copy-from appends to inherited EOC/enchantment/armor lists; shipped overrides double enchantments
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/mutation_data.cpp:429`, `src/mutation_data.cpp:445`, `src/mutation_data.cpp:530`, `src/mutation_data.cpp:545`, `src/mutation_data.cpp:549`, `src/mutation_data.cpp:557`, `src/mutation_data.cpp:379`, `src/bionics.cpp:374`, `src/bionics.cpp:387`, `src/magic_enchantment.cpp:340`, `src/character.cpp:4984`

The mutation and bionic loaders push_back onto the copied parent's containers instead of replacing them: activated/processed/deactivated_eocs, enchantments, integrated_armor and attacks. `armor` uses `+=`. Map fields (vitamin_rates, spells_learned, lumination, anger_relations, craft_skill_bonus, moncams) use `emplace`, which cannot override a parent key. None of these support `extend`/`delete`. Inline enchantment ids restart at `_0`, so a same-id override replaces the enchantment object but pushes its id a second time. MindOverMatter's `mutation_overrides.json:10-30` does this for INTERSTICE_RESONANCE_2/3, so ARTIFACT_RESONANCE +1250 is summed twice. Xedra's UNKNOWING_CHANGELING_COMMONER_* inherit NOBLE's MAX_MANA enchantment on top of their own. Bionic inline enchantment ids are built from the translated display name, so two bionics with the same name would collide.

```cpp
// mutation_data.cpp:445-447
int enchant_num = 0;
for( JsonValue jv : jo.get_array( "enchantments" ) ) {
    std::string enchant_name = "INLINE_ENCH_" + id.str() + "_" + std::to_string( enchant_num++ );
    enchantments.push_back( ... );
```

**Fix:** When a field is present, clear the container first (as `bionics.cpp:393-410` already does for encumbrance/occupied_bodyparts). Add `extend`/`delete` support, e.g. `"extend": { "enchantments": [ ... ], "processed_eocs": [ ... ] }`. Build bionic inline ids from `id.str()`. Warn in check_consistency on duplicate enchantment ids within one trait or bionic.

### MUT-J09 · Surgery model is hardcoded: skill weights, PROF_MED/PROF_AUTODOC, anesthesia, failures
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/bionics.cpp:2048`, `src/bionics.cpp:2066`, `src/bionics.cpp:2076`, `src/bionics.cpp:2081`, `src/bionics.cpp:2589`, `src/bionics.cpp:1959`, `src/bionics.cpp:1965`, `src/bionics.cpp:2664`, `src/bionics.cpp:2690`, `src/bionics.cpp:434`

Install success uses a fixed formula: INT×4 plus skill weights 4/3/1 over firstaid/computer/electronics (autodoc) or electronics/firstaid/mechanics (manual). Only two hardcoded traits give a bonus (PROF_MED +3, PROF_AUTODOC +7). Medical-training mitigation is passed as the string sentinel `"NOT_MED"`. Anesthetic amount, failure damage tiers and the single global faulty-bionic pool are fixed. Some JSON hooks already exist: event EOCs on `installs_cbm`/`fails_to_install_cbm`/`removes_cbm`, the PAIN_IMMUNE/MANUAL_CBM_INSTALLATION flags, the `assisted` effect, furniture `surgery_skill_multiplier` and the BIONIC_FAULTY flag. Modders still cannot add a surgeon profession trait, a surgery-boosting CBM or drug, per-line faulty variants, or an autodoc that uses different skills.

```cpp
// bionics.cpp:2076-2083
if( has_trait( trait_PROF_MED ) ) { pl_skill += 3; } ... if( has_trait( trait_PROF_AUTODOC ) ) { pl_skill += 7; }
```

**Fix:** Add enchantment values `BIONIC_SURGERY_SKILL` and `ANESTHETIC_NEEDED_MULT`. Replace the PROF_MED/PROF_AUTODOC checks with a `SURGICAL_TRAINING` character flag. Add bionic fields `"faulty_pool": [ "bio_dis_shock" ]` and optionally a per-mode `"surgery_skill_weights": { "autodoc": { "firstaid": 4, "computer": 3, "electronics": 1 } }`.

### MUT-J10 · Mutation progression tuning is hardcoded; categories have no fields for it and no copy-from
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/mutation.cpp:1144`, `src/mutation.cpp:1158`, `src/mutation.cpp:218`, `src/mutation.cpp:1322`, `src/mutation.cpp:1375`, `src/character.cpp:633`, `src/mutation.cpp:2460`, `src/mutation.cpp:2487`, `src/mutation.cpp:2506`, `src/mutation_data.cpp:78`

The bad-mutation chance is `0.5*instability/positive_count`, capped at 0.67. The comment "or, if we have Robust, cap it lower" was never implemented. The upgrade bias is `one_in(2)` with a +4 weight. Breaching a threshold needs category level >= 30 and then `x_in_y(level,100)`. A character can cross only one threshold, and the threshold message is a fixed string. The CHAOTIC escalation is dead code (MUT-B14). `mutation_category` uses a hand-written loader rather than generic_factory, so mods can only replace a category wholesale and cannot copy-from it to tweak e.g. `threshold_min`. (`ROBUST_GENETIC` is a JSON flag and `MUT_INSTABILITY_MOD` exists, so some tuning is already possible.)

```cpp
// mutation.cpp:1144, 1158, 2487
float MAX_BAD_CHANCE = 0.67;
float chance = 0.5 * ... insta_actual / ... muts_max;
if( breach_power >= 30 && ...
```

**Fix:** Move category loading to generic_factory and add per-category fields: `"bad_mutation_chance": { "math": [ "..." ] }` (or `bad_chance_scale`/`bad_chance_cap`), `"threshold_breach_min_level": 30`, `"threshold_message": "..."`, `"allows_multiple_thresholds": false`. Add an enchantment value `MUT_OUT_OF_TREE_INSTABILITY_MULT`.

### MUT-J11 · Mutation activation costs: one shared `cost`, no bionic-power option, three inconsistent gates
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/mutation_data.cpp:317`, `src/mutation.cpp:420`, `src/mutation.cpp:870`, `src/mutation_ui.cpp:456`, `src/mutation_ui.cpp:633`, `src/suffer.cpp:261`

A mutation has one int `cost` applied to every enabled resource (kcal, thirst, sleepiness, mana, stamina). A trait cannot cost 10 mana + 50 kcal, and cannot draw bionic power at all. Bionics can only cost power. The gates disagree with each other. The menu requires kcal >= 80% and thirst/sleepiness <= 400. `can_power_mutation` requires kcal >= 50%, thirst < 260 and sleepiness < EXHAUSTED. Upkeep in `suffer` uses BMI < underweight. The menu path re-checks `can_power_mutation`, so the visible effect is the stricter 80% kcal gate plus misleading messages (see MUT-B13). Extra costs can already be charged, but not gated, through `activated_eocs` math.

```cpp
// mutation.cpp:422-423 vs mutation_ui.cpp:457-458
bool hunger = mut->hunger && get_kcal_percent() < 0.5f;
( !mut_data.hunger || get_kcal_percent() >= 0.8f ) && ...
```

**Fix:** Add `"costs": { "kcal": 50, "mana": 10, "stamina": 200, "power": "5 kJ", "thirst": 5 }`, keeping the legacy bool+cost form, and use one `can_power_mutation()` everywhere (UI, triggers, upkeep). Expose gating through `activation_condition` (MUT-J02). Bionics could accept the same `costs` object.

### MUT-J12 · 22 bionic behaviours are hardcoded by id; copy-from silently loses them
**Severity:** Medium · **Effort:** Large · **Verified:** confirmed
**Where:** `src/bionics.cpp:95`, `src/bionics.cpp:890`, `src/bionics.cpp:1193`, `src/bionics.cpp:1294`, `src/bionics.cpp:1677`, `src/bionics.cpp:1719`, `src/bionics.cpp:320`, `doc/JSON/JSON_INFO.md:1428`, `src/character.cpp:5155`, `src/game.cpp:8535`, `src/ranged.cpp:1689`

Long if/else id chains implement bio_evap, cqb, resonator, teleport, blood_anal, torsionratchet, jointservo, lighter, geiger, radscrubber, emp, water_extractor, magnet, lockpick, flashbang, shockwave, meteorologist and remote (activation), cqb/remote (deactivation), and remote, nanobots, painkiller, gills, evap and afs_bio_dopamine_stimulators (per turn). A bionic that copy-froms `bio_teleport` under a new id does nothing, with no warning. Several of these are already expressible with existing EOCs and spells (dopamine = `u_add_morale`, geiger = `u_message` + `u_val('rad')`, teleport/flashbang/emp/shockwave = spells). `auto_deactivates` is only honoured in the final generic `else` branch. `trigger_cost` is only read for hardcoded ids (painkiller, gills, ods, probability_travel, railgun, uncanny_dodge), never generically.

```cpp
// bionics.cpp:1782-1784
} else if( bio.id == afs_bio_dopamine_stimulators ) {
    add_morale( morale_feeling_good, 20, 20, 30_minutes, 20_minutes, true );
```

**Fix:** Move `auto_deactivates` out of the else-chain (see also MUT-B19). Port the easy ones to `spell_on_activation`/`activated_eocs`/`processed_eocs` in `data/json/bionics.json`. For the rest, add generic actors: `"activation_target": "adjacent" | "ranged"` (sets a `_target` location context var), `"magnet_pull": { "radius": 10, "materials": [ "iron", "steel" ] }`, `"heals_bleeding": { "kcal": 15 }`. Pass `trigger_cost` to `processed_eocs` as `_trigger_cost`. Update the outdated sentence at `JSON_INFO.md:1428`.

### MUT-J13 · Docs drift: misspelled, missing and unloaded fields
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `doc/JSON/MUTATIONS.md:138`, `doc/JSON/MUTATIONS.md:146`, `doc/JSON/MUTATIONS.md:150`, `doc/JSON/MUTATIONS.md:164`, `doc/JSON/MUTATIONS.md:202`, `doc/JSON/MUTATIONS.md:209`, `doc/JSON/MUTATIONS.md:321`, `doc/JSON/JSON_INFO.md:1338`, `doc/JSON/JSON_INFO.md:1344`, `doc/JSON/JSON_INFO.md:1371`, `doc/JSON/JSON_INFO.md:1375-1377`, `doc/JSON/JSON_INFO.md:1421`, `doc/JSON/JSON_INFO.md:1428`, `src/mutation_data.cpp:291`, `src/mutation_data.cpp:313-314`, `src/mutation_data.cpp:356`, `src/mutation_data.cpp:373`, `src/mutation_data.cpp:416`, `src/mutation_data.cpp:545`, `src/bionics.cpp:305-441`, `src/character.cpp:4955`, `src/mutation.h:284`

MUTATIONS.md spells `strict_thresreq` (the code reads `strict_threshreq`) and `apped_desc` (the code reads `append_desc`). It shows `personality_score` as an array, but only an object is accepted. It says armor `parts` overrides, but the code adds with `+=`. JSON_INFO.md documents bionic `stat_bonus`, `fuel_capacity` and `"active"` in its example, and the loader reads none of them, so copying the example fails with an unvisited-member error. Fields that load but are undocumented: mutation `inactive_flags`, `attacks` (+`hardcoded_effect`), `bionic_slot_bonuses`, bionic `inactive_flags`, `deactivated_close_ui` and `activate_remove_cbm`. MAGIC.md says ACTIVE bionic enchantments apply while the bionic is active, but the code also requires BIONIC_TOGGLED. `hide_on_activated`/`hide_on_deactivated` are declared in `mutation.h` and never loaded or read.

```cpp
// character.cpp:4955-4956
ench.is_active( *this, bio.powered && bid->has_flag( json_flag_BIONIC_TOGGLED ) )
```

**Fix:** Fix the spellings and the `personality_score` shape. Correct the armor `parts` wording. Remove `stat_bonus`/`fuel_capacity`/`active` from the bionic docs and point to enchantments instead. Document the undocumented fields. Either drop the BIONIC_TOGGLED requirement or document it. Delete the dead `hide_on_*` members.

### MUT-J14 · Load-time validation gaps let mutation/bionic typos misbehave silently
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mutation_data.cpp:660`, `src/mutation_data.cpp:777-779`, `doc/JSON/MUTATIONS.md:337`, `src/bionics.cpp:493`, `src/bionics.cpp:203`, `src/bionics.cpp:481`, `src/string_formatter.cpp:33-41`

`mutation_branch::check_consistency` does not validate category ids, `spawn_item`/`ranged_mutation` itypes, `integrated_armor`, `spells_learned`, `moncams`, `vitamin_rates`, enchantment ids or `can_heal_with`/`can_only_heal_with`. It also does not check `starts_active` without `active` or `prereqs2` without `prereqs`, although MUTATIONS.md says both "will throw errors". The "is not displayed but set as active" debugmsg has a `%s` and no argument, so it prints a format error instead of the mutation id. `check_bionic_consistency` only checks that `fake_weapon` is non-empty. `initialize_pseudo_items` silently skips invalid ids, so a typo only shows up at activation. `learned_spells`, `known_ma_styles`, `give_mut_on_removal`, `fuel_options` and `spell_on_activation` are not validated either.

```cpp
// mutation_data.cpp:777-778
if( mdata.activated && !mdata.player_display ) {
    debugmsg( "mutation %s is not displayed but set as active" );
```

**Fix:** Add `is_valid()` loops for the listed id fields in both checkers. Add `if( starts_active && !activated )` and `if( !prereqs2.empty() && prereqs.empty() )` debugmsgs. Pass `mid.c_str()` to the existing debugmsg. Check `item::type_is_defined( fake_weapon )`.

### MUT-J15 · Bionic fuel model is hardcoded: first fuel only, three named perpetual fuels, fixed rates
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/bionics.cpp:1340`, `src/bionics.cpp:1346`, `src/bionics.cpp:1438`, `src/bionics.cpp:1486`, `src/bionics.cpp:1501`, `src/bionics.cpp:1510`, `src/bionics.cpp:1521`, `src/bionics.cpp:1554`

Material `fuel_data` has a generic `perpetual` flag, but `burn_fuel` only generates energy from perpetual fuels whose id is exactly `sunlight`, `metabolism` or `wind`, and only when that fuel is the first `fuel_options` entry. A modded perpetual fuel passes the fuel check and silently generates 0 J. Rates are literals: metabolism stops below 0.8× healthy kcal and burns 1 kcal = 4184 J per tick, vehicles give 1 kJ per tick, wind gives 1 kJ per windpower unit. Exothermic heat builds an emit id from a string. No shipped bionic hits the modded-perpetual case, and `processed_eocs` with `u_val('power')` math already covers custom generation.

```cpp
// bionics.cpp:1554
emit_id( "emit_hot_air" + std::to_string( heat_level ) + "_cbm" )
```

**Fix:** Let fuel materials define their own generation, e.g. `"perpetual": { "energy": { "math": [ "weather('irradiance') * 1" ] }, "consumes": { "kcal": 1 } }`, falling back to the three legacy ids. Add bionic fields `"heat_emission": [ { "emit": "emit_hot_air1_cbm", "threshold": "10 kJ" } ]` and `"metabolism_reserve": 0.8`. Iterate all `fuel_options`.

### MUT-J16 · Mutation attacks: `hardcoded_effect` only works for 3 tentacle ids; no per-attack conditions or effects
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/mutation_data.cpp:148`, `src/mutation_data.cpp:155`, `src/mutation.h:59`, `src/melee.cpp:2244`, `src/melee.cpp:2366`, `src/melee.cpp:2368`, `src/melee.cpp:2398`, `src/melee.cpp:2461`, `src/melee.cpp:2465`

The `attacks` object supports only texts, required/blocker mutations, body part, chance and damage. `"hardcoded_effect": true` routes to `hardcoded_mutation_attack`, which recognises only ARM_TENTACLES/_4/_8 (as a damage multiplier on a single hit) and debugmsgs at combat time for any other trait. Nothing checks this at load time. The strength scaling cap is a flat 15. Enchantment `hit_you_effect` gives on-hit effects, but it fires on every melee attack, not on the mutation attack's own roll. Mutation attacks are not documented in MUTATIONS.md.

```cpp
// melee.cpp:2368
if( id == trait_ARM_TENTACLES || id == trait_ARM_TENTACLES_4 || id == trait_ARM_TENTACLES_8 ) {
```

**Fix:** Extend `mut_attack` with `"condition": <dialogue condition>`, `"hits": 3`, `"effects": [ { "id": "poison", "duration": "10 s", "chance": 50 } ]`, `"eocs": [ ... ]` (alpha = attacker, beta = target) and `"strength_scaling_cap": 15`. Re-express the tentacles in JSON and drop `hardcoded_effect`, or at least reject it at load time for other ids.

## 2. Bugs, oversights & quirks

### MUT-B01 · Use-after-free: an activation EOC that removes its own trait leaves a dangling `trait_data&` that is then written to
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mutation.cpp:866`, `src/mutation.cpp:910-918`, `src/mutation.cpp:392`, `src/character.h:4235`, `src/npctalk.cpp:3562-3568`, `data/mods/Xedra_Evolved/mutations/temporary.json:58`, `data/mods/Xedra_Evolved/spells/spell_eocs.json:111-126`

`activate_cached_mutation()` takes a reference into the `std::unordered_map` `cached_mutations`, runs the enchantment recalc and the `activated_eocs`, then writes `tdata.powered`. If an EOC runs `u_lose_trait` on the same trait, `unset_mutation` erases the node and line 917 writes to freed memory. This ships in Xedra Evolved. `ethereal_wings`, `karma_arms`, `devil_tail` and `stalker_eyes` have activation EOCs whose `false_effect` is `u_lose_trait` of the same trait, so dismissing the wings from the mutation menu triggers it every time. Only one bool is written, so release builds rarely show it, but ASan and debug-heap builds abort. An enchantment-granted trait dropped by the recalc at :907 reaches the same path.

```cpp
trait_data &tdata = cached_mutations[mut];                      // :866
for( ... activated_eocs ) { eoc->activate_activation_only( d, ... ); }  // may erase mut
tdata.powered = mut->activated_is_setup;                        // :917, dangling
```

**Fix:** Do not hold the reference across code that can change the map. After the recalc and after each EOC, re-look up with `auto it = cached_mutations.find( mut ); if( it == cached_mutations.end() ) return;` and write through `it->second`. Do the same before the hardcoded `powered = false` writes. In `activate_mutation()`, re-check `cached_mutations.count( mut )` before copying into `my_mutations`.

### MUT-B02 · Deactivating a mutation never clears `my_mutations.powered`, so ACTIVE enchantment-granted traits stay forever
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mutation.cpp:853-858`, `src/mutation.cpp:1039-1041`, `src/mutation.cpp:415`, `src/character.cpp:4971-4980`, `src/character_body.cpp:251`, `data/json/mutations/mutations_limbs.json:775`

`activate_mutation()` copies the cache entry (powered = true) into `my_mutations`. `deactivate_mutation()` and `switch_mutations()` only update `cached_mutations`. `recalculate_enchantment_cache()` runs every turn from `update_body` and reads `my_mutations[..].powered` to decide which enchantment-granted mutations to add. Once a mutation has been activated, its `"condition": "ACTIVE"` grants never go away. Example: Insect Wings (`WINGS_INSECT` -> `WINGS_INSECT_active`, MOVE_COST ×0.75) keeps the speed bonus and noise after deactivation. Stamina upkeep reads the cache and stops, so the bonus is free. Both maps are serialized, so the stale state survives save/load. Activation is also one recalc late, because the recalc runs before the copy. INACTIVE grants are inverted in the same way.

```cpp
// mutation.cpp:1041
cached_mutations[mut].powered = false;          // my_mutations not updated
// character.cpp:4971-4977
for( mut_map : my_mutations ) ... if( ench.is_active( *this, mut.activated && mut_map.second.powered ) )
    enchantment_cache->force_add_mutation( ench );
```

**Fix:** Keep powered state in one place. Add a `set_mutation_powered( mut, bool )` helper that updates both maps, and use it in deactivate, switch_mutations, `starts_active` and the UI paths. Alternatively have the recalc read `powered` from `cached_mutations`. Recalculate after the state change, not before.

### MUT-B03 · Removing an extended weapon CBM (mutation conflict, cross-character surgery, nurse bot) leaves an un-unwieldable weapon
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/bionics.cpp:2953-3016`, `src/mutation.cpp:2553-2558`, `src/bionics.cpp:2219-2224`, `src/bionics.cpp:2331-2396`, `src/monmove.cpp:1576-1593`, `src/character_inventory.cpp:1296-1305`, `data/json/bionics.json:275-290`

`remove_bionic()` never checks whether the removed bionic, or an included child, is the active weapon bionic. The NO_UNWIELD fake weapon stays wielded and `weapon_bionic_uid` points at a bionic that no longer exists. `can_unwield()` then always fails, and nothing cleans this up per turn, so the player is stuck. Three triggers: (1) mutating toward ARM_TENTACLES or BENDY3 with Bionic Claws out (`bio_claws` `mutation_conflicts` -> `handle_bio_mut_conflict` -> `remove_bionic`). (2) `uninstall_bionic()` retracts weapons on the *installer's* bionics, not the patient's, so an ally operating on the player, or the player on an NPC, misses them. (3) The nurse-bot path never retracts weapons at all.

```cpp
// bionics.cpp:2220-2222
for( bionic &it : *installer.my_bionics ) {
    if( it.powered && it.info().has_flag( json_flag_BIONIC_WEAPON ) ) installer.deactivate_bionic( it );
// character_inventory.cpp:1300-1302
bio_opt = find_bionic_by_uid( get_weapon_bionic_uid() ); if( ... || !bio_opt || ... ) return make_failure( ... );
```

**Fix:** At the top of `remove_bionic()`, if the removed uid (or a removed child uid) equals `weapon_bionic_uid`, call `force_bionic_deactivation`, or clear the wielded item and reset `weapon_bionic_uid = 0`. In `uninstall_bionic()`, iterate the patient's `*my_bionics`.

### MUT-B04 · POWER_TRICKLE is applied once per unpowered bionic per turn, and never if there are none
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/bionics.cpp:1395-1411`, `src/bionics.cpp:1565`, `src/bionics.cpp:1613-1620`, `src/suffer.cpp:1721-1723`, `doc/JSON/MAGIC.md:1069`, `doc/JSON/JSON_INFO.md:1340`, `src/game.cpp:9012`, `src/vehicle.cpp:5446`

The character-wide POWER_TRICKLE value is read only inside `burn_fuel()`'s `!bio.powered` branch, and `suffer()` calls `process_bionic()` for every installed CBM. A character with 10 passive or switched-off CBMs gets 10× the documented "millijoules each second". One whose CBMs are all on, or who has none, gets nothing. Mods depend on this value: XedraWood tattoo traits (+100/+1000/+3500), Aftershock `bio_microgen` (+6 per copy, dupes allowed), MindOverMatter, BombasticPerks and Isolation-Protocol. In each, generated power depends on unrelated CBM counts. The same branch computes `passive_fuel_efficiency` and throws it away, so the passive solar/wind generation described in JSON_INFO.md does not exist (only muscle works, through separate code). It also costs one enchantment lookup and one dead `get_effective_efficiency` call per passive CBM per turn.

```cpp
// bionics.cpp:1398-1410
if( !bio.powered ) {
    units::energy trickle = enchantment_cache->modify_value( enchant_vals::mod::POWER_TRICKLE, 0_J );
    if( trickle != 0_J ) { mod_power_level( trickle ); }
    efficiency = get_effective_efficiency( bio, bio.info().passive_fuel_efficiency );
    if( efficiency == 0.f ) { return; }
    return;
}
```

**Fix:** Apply POWER_TRICKLE once per turn in `Character::suffer()` (before the bionic loop) or in `update_bionic_power`. In `process_bionic`, return immediately for unpowered bionics. Either implement passive generation for perpetual fuels or remove `passive_fuel_efficiency` from the docs. Optionally add a `BIONIC_POWER_GEN_MULT` enchantment value. Make the exclusion flag list in `get_effective_efficiency` a `static const` vector.

### MUT-B05 · `mutate_towards` strips conflicting and base traits before threshold, bionic and vitamin checks that can still fail
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/mutation.cpp:1704-1764`, `src/mutation.cpp:1766-1773`, `src/mutation.cpp:1838-1853`, `src/mutation.cpp:1856-1880`, `src/mutation.cpp:1886-1903`, `src/mutation.cpp:2032-2060`, `src/mutation.cpp:1247-1252`, `src/mutation.cpp:1092-1137`

Cancelled and same-type traits are removed, and base traits erased from `my_traits`, at the start of `mutate_towards`. The threshold, bionic-conflict and vitamin checks come later and can return false. `mutate()` and `mutation_ok` do not filter on `threshreq`. For example, INSECT_ARMS has types ARMS/HANDS and threshreq THRESH_INSECT. A pre-threshold mutant can therefore lose a mutation to the same-type prune, see "You feel something straining...", and gain nothing. If a base trait was removed, the cost is multiplied by `base_removal_cost_mul` (default 3.0) only after `mutation_ok` checked the unmultiplied cost. The vitamin check can then fail, and the starting trait is lost permanently for nothing. The canceltrait handling (:1766-1773, :2032-2060) is dead code. Every entry is still held, so the `cancel_recheck` loop returns true first, and a mutation blocked by an unpurifiable trait is reported as a success.

```cpp
:1750       remove_mutation( removed );                       // before validation
:1758-1762  for( trait : cancel_recheck ) if( has_permanent_trait( trait ) ) return true;
:1877-1879  if( !c_has_threshreq && !threshreq.empty() ) { ...; return false; }
:1888-1889  if( removed_base_trait ) vitamin_cost *= category.base_removal_cost_mul;
```

**Fix:** Split `mutate_towards` into a validate phase (prereq resolution, threshold, bionic conflicts, final vitamin cost including the base-removal multiplier) and a commit phase that prunes and grants. Filter candidates with unmet `threshreq` in `mutate()`/`mutation_ok`. Return false for the unpurifiable-conflict case, or delete the dead canceltrait code.

### MUT-B06 · `has_trait_flag`/`count_trait_flag` check the avatar's mutation activation state for every character
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mutation.cpp:175-181`, `src/mutation.cpp:196-201`, `src/character.cpp:7560-7567`

For activated mutations with `active_flags`/`inactive_flags`, both functions call `get_player_character().has_active_mutation( mut )` instead of `this->has_active_mutation( mut )`. An NPC that activates WEB_RAPPEL, or a Xedra werewolf trait such as WEREWOLF_NO_SCENT, does not get the flag unless the player has the same trait active. An NPC can also gain or lose `inactive_flags` based on the player's state. The result goes into the NPC's own `trait_flag_cache`, which the player's toggles never invalidate, so the wrong value persists. NPCs only toggle these traits through `npc_activate_trait` EOCs, which limits the impact, but this breaks u_/npc_ symmetry for every mod using `active_flags`.

```cpp
// mutation.cpp:176-178
Character &player = get_player_character();
if( ( mut_data.active_flags.count( b ) > 0 && player.has_active_mutation( mut ) ) ||
    ( mut_data.inactive_flags.count( b ) > 0 && !player.has_active_mutation( mut ) ) ) {
```

**Fix:** Replace `player.has_active_mutation( mut )` with `has_active_mutation( mut )` in both functions.

### MUT-B07 · `u_activate_trait`/`u_deactivate_trait` on a trait you don't have creates a phantom trait
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctalk.cpp:3543`, `src/mutation.cpp:866`, `src/mutation.cpp:1041`, `src/mutation.cpp:137`, `src/character.cpp:5012`, `src/newcharacter.cpp:2111`

Neither EOC effect checks ownership. `cached_mutations[mut]` (activation) and `cached_mutations[mut].powered = false` (deactivation) insert a default `trait_data` with `corrupted == 0`, so `has_trait()` and `get_functioning_mutations()` now report a trait the character never had. `update_cached_mutations` only prunes enchantment-granted ids, so the entry stays. Activation also charges the cost and runs the effects. Deactivation runs `deactivated_eocs` and, if the trait has a `transform`, calls `switch_mutations`, which grants the target trait. Mods often deactivate several form traits blindly in a false_effect (e.g. Xedra `werewolf_trait_eocs.json:274-275`).

```cpp
// mutation.cpp:137-140
auto mutit = cached_mutations.find( b );
if( mutit != cached_mutations.end() ) { return mutit->second.corrupted == 0; }
```

**Fix:** Use `find()` in `activate_cached_mutation`/`deactivate_mutation` and return early (with `add_msg_debug`) if the trait is absent. Make the EOC effects no-ops when `!has_trait`.

### MUT-B08 · Self-installed CBMs skip slot-capacity and compatibility checks
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/bionics.cpp:2468-2492`, `src/bionics.cpp:2510-2522`, `src/iuse_actor.cpp:4597-4651`, `src/bionics.cpp:2403-2466`, `src/bionics.cpp:2602-2631`, `data/mods/Xedra_Evolved/player/cbms.json:12-21`, `data/mods/Xedra_Evolved/recipes/requirements.json:21-25`

In `can_install_bionics()`, the self-install branch (`!autodoc && installer.is_avatar()`) returns after checking only anesthetic and `installation_requirement`. It never runs `bionic_installation_issues` (slot limits) or `is_installable` (mutation conflicts, `no_cbm_on_bp`, required bionic, replaced body parts). The MANUAL_CBM_INSTALLATION and DEBUG paths return even earlier. Xedra Evolved datacores are balanced around `data_shunt` slots (9 datacore types needing 1-3 slots each, against 3 slots), and self-install lets a player install all of them.

```cpp
// bionics.cpp:2489-2491
if( !autodoc && installer.is_avatar() ) {
    return installer.has_enough_anesth( type ) && installer.has_installation_requirement( bioid );
}
```

**Fix:** Run `is_installable()` and `bionic_installation_issues()` (and the `canceled_mutations` prompt) on the patient before the early return. Ideally use one validation function for the autodoc, NPC and self-install paths.

### MUT-B09 · `burn_fuel` drains every matching fuel container each tick but credits only the last
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/bionics.cpp:1429-1484`, `src/bionics.cpp:1540`, `src/bionics.cpp:3444-3466`, `src/bionics.cpp:1334-1336`, `data/mods/aftershock_exoplanet/player/bionics.json:98-108`, `data/json/bionics.json:162-172`

`get_bionic_fuels()` returns every BIONIC_FUEL_SOURCE item holding any of the bionic's fuel materials, sometimes the same item once per matching material. `burn_fuel()` consumes a charge from each, overwrites `energy_gain` on every iteration and calls `mod_power_level` once after the loop. The safe-fuel `return` inside the loop also discards fuel already taken from earlier sources. In Aftershock, `bio_batteries` plus `afs_bio_beavy_batteries` burn about 4 charges per turn for 2 charges of credit, each draining the other's compartment. Vanilla `bio_cable` with several plugged-in UPS units over-drains the same way. The comment above the loop ("Avoid draining multiple sources") states the opposite intent.

```cpp
// bionics.cpp:1449-1475, 1540
for( item *fuel_source : result.connected_fuel ) {
    ... energy_gain = fuel->fuel_energy();
    if( safe_fuel && ... ) return;
    ... fuel->charges--; ...
}
mod_power_level( energy_gain * efficiency );
```

**Fix:** Check the safe-fuel threshold before consuming anything. Consume from the first usable source and `break`. Deduplicate `get_bionic_fuels()` results.

### MUT-B10 · Removing a CBM strips proficiencies and spells the character had independently or from another CBM
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/bionics.cpp:2878-2903`, `src/bionics.cpp:2958-2988`, `src/proficiency.cpp:415-445`, `data/mods/Xedra_Evolved/player/cbms.json:54`, `data/mods/Magiclysm/items/bionics.json:37`

`add_bionic()` only teaches spells and proficiencies the character lacks, but `remove_bionic()` calls `lose_proficiency()` for every listed proficiency. `proficiency_set::remove` also drops partial progress and cascades to dependent proficiencies. Spells are protected only if another installed CBM grants them. A Xedra player who trained `prof_lockpicking` loses it when swapping out the lockpicking datacore. `prof_helicopter_pilot` from an Aftershock hobby is lost when `afs_bio_neurosoft_aeronautics` is removed. A Magiclysm caster who learned `overcharge_eyes` from a scroll forgets it and its XP. Included child bionics are never cleaned up at all. The reviewer notes the full fix needs saved per-bionic state, so effort may be closer to medium.

```cpp
// bionics.cpp:2986-2987
for( const proficiency_id &lost : bio.id->proficiencies ) { lose_proficiency( lost ); }
```

**Fix:** Record per bionic instance which proficiencies and spells (and spell levels) it actually granted, e.g. in `bionic_tags`. Revoke only those, and skip any still granted by another installed bionic. Apply the same cleanup to removed child bionics.

### MUT-B11 · Nurse-bot uninstall failure is evaluated on the avatar, not the patient; success skips standard cleanup
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monmove.cpp:1591-1592`, `src/bionics.cpp:1862-1951`, `src/bionics.cpp:2331-2398`, `src/bionics.cpp:2241-2318`, `src/monattack.cpp:2524`

monmove calls `get_player_character().uninstall_bionic( real_target, *this, *dragged_foe, ... )`, so inside `bionics_uninstall_failure`, `this` is the avatar rather than the patient. The PAIN_IMMUNE, `under_operation` and `roll_critical_bionics_failure` checks run on the wrong character. Nobody has `under_operation` on this path, so failure types 2-5 do nothing. On success, the code calls bare `patient.remove_bionic()` and skips the `removes_cbm` event, removal of dependents on `replaced_bodyparts` (vanilla prosthetic limbs have these), `give_mut_on_removal` and weapon retraction (MUT-B03).

```cpp
// monmove.cpp:1592
get_player_character().uninstall_bionic( real_target, *this, *dragged_foe, adjusted_skill );
// bionics.cpp:1907, 1916
if( !has_flag( json_flag_PAIN_IMMUNE ) ) ... if( has_effect( effect_under_operation, bp.id() ) )
```

**Fix:** Call this on the patient (`patient.uninstall_bionic( ..., monster & )`) or use `patient.` consistently in the failure code. Apply `under_operation` to the patient's occupied parts and route success through `perform_uninstall()`.

### MUT-B12 · Mutation selector's bionic-conflict filter is a no-op
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mutation.cpp:1532-1537`, `src/mutation.cpp:1610-1618`, `src/mutation.cpp:1838-1841`, `src/mutation.cpp:2515-2569`

In `mutation_selector`, the `continue` sits inside the inner loop over bionics, so it only skips to the next bionic and never excludes the trait. Players using SHOW_MUTATION_SELECTOR or MUT_ADDITIONAL_OPTIONS are offered traits that conflict with installed CBMs, with no warning. Picking one runs `handle_bio_mut_conflict`: at least 14 h of internal damage, or the CBM is destroyed with 12-36 damage per body part.

```cpp
// mutation.cpp:1533-1536
for( const bionic_id &bid : get_bionics() ) {
    if( bid->mutation_conflicts.count( trait ) != 0 ) { continue; }
}
```

**Fix:** Use `std::any_of` and `continue` the outer loop, or keep the entry but disable it with a "conflicts with <CBM>" note.

### MUT-B13 · Mutation menu activation limits disagree with `can_power_mutation`; "You activate" prints, then it fails
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mutation_ui.cpp:457-468`, `src/mutation_ui.cpp:567`, `src/mutation_ui.cpp:633-642`, `src/mutation.cpp:420-429`, `src/mutation.cpp:861-875`

The menu allows activation at thirst <= 400 and mana/stamina >= cost. `can_power_mutation` refuses at thirst >= 260 and at mana/stamina <= cost. With thirst at 260-400, or mana/stamina exactly equal to the cost, the menu prints "You activate your X.", closes, and then prints "You feel like using your X would kill you!" without activating. The cursor+Enter path also omits the stamina clause that the hotkey path has. Only messages are wrong, since the activation is still refused. Related design issue: MUT-J11.

```cpp
// mutation.cpp:423-426
thirst = mut->thirst && get_thirst() >= 260; ... mana = ... available_mana() <= mut->cost;
```

**Fix:** Have both UI paths call `can_power_mutation(mut)`. Make `activate_mutation` return bool and print `activation_msg` only on success.

### MUT-B14 · Genetic Chaos -> Downward Spiral escalation is unreachable dead code
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mutation.cpp:1375-1390`, `src/character.cpp:625-639`

`count_threshold_substitute_traits()` does `++count; break;` and so returns at most 1, which means `sub_count > 2` is never true and the whole block is dead. Even if it were reached, the CHAOTIC -> CHAOTIC_BAD `else if` is nested inside `if( !has_trait( CHAOTIC ) ... )` and can never run. The reachable-looking branch also ignores the result of `mutate_towards( trait_CHAOTIC )` and prints "You will mutate uncontrollably from now on!" regardless. The counter only counts a threshold held together with its substitute, which `can_cross_threshold` forbids, so fixing the nesting alone will not bring the mechanic back. CHAOTIC/CHAOTIC_BAD can still be gained through the CHIMERA category.

```cpp
:1378  if( !has_trait( trait_CHAOTIC ) && !has_trait( trait_CHAOTIC_BAD ) ) {
:1379      if( rng( 1, 100 ) <= chance ) { mutate_towards( trait_CHAOTIC ); add_msg_if_player( ... ); }
:1382      else if( has_trait( trait_CHAOTIC ) && !has_trait( trait_CHAOTIC_BAD ) ) {   // unreachable
```

**Fix:** Delete the block, or redesign it (e.g. as an EOC on `gains_mutation`, see MUT-J10). If you keep it, put the two branches at the same level, fix the counter, and print the message only if `mutate_towards` succeeded.

### MUT-B15 · Documented "remove" `bionic_migration` (no `to`) is deleted right after insertion
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/bionics.cpp:457-470`, `src/bionics.cpp:3276-3286`, `src/savegame_json.cpp:887-891`, `doc/JSON/OBSOLETION_AND_MIGRATION.md:118-131`

The docs say a migration with null or missing `to` removes the bionic. `load_bionic_migration` emplaces `(from, NULL)` and then immediately erases that same entry because its value is null. The bionic is still stripped on load, through the invalid-id fallback, but only after a "doesn't exist and has no migration" debugmsg. `emplace` also stops a later mod from overriding an earlier migration's target. No shipped migration uses the removal form.

```cpp
// bionics.cpp:463-468
migrations.emplace( from, to );
auto iter = migrations.find( from );
if( iter != migrations.end() && ( *iter ).second.is_null() ) { migrations.erase( iter ); }
```

**Fix:** Use `migrations[from] = to;` and keep null targets. `deserialize` already handles `is_null()` silently.

### MUT-B16 · Mutation/CBM conflict wounds: off-by-one intensity, inverted odds formula, NPC messages shown to the player
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mutation.cpp:2516-2530`, `src/mutation.cpp:2540-2552`, `src/mutation.cpp:2561`, `src/mutation.cpp:2564`

`fail_mutation_wounds` counts with `emplace( main_part, 1 ).first->second += 1`, so the first insert already gives 2, and every wound gets intensity 2 (14 h) instead of 1 (7 h). The destruction formula does the opposite of its own comment: more occupied parts raise the destruction chance and more slots lower it. The destroyed and strain messages use the global `add_msg`, so an NPC's conflict prints "The changes in your body destroy your %s" to the player.

```cpp
:2520  main_bps.emplace( bp.first->main_part, 1 ).first->second += 1;
:2544  int deviation = bio_parts.size() + std::abs( mut->points ) - std::sqrt( bio_slots );
```

**Fix:** Use `emplace( ..., 0 )`. Decide which relationship is intended and fix either the formula (e.g. `sqrt(slots) + |points| - parts`) or the comment. Use `add_msg_player_or_npc`.

### MUT-B17 · Bionic safe-fuel "Disabled" setting is not saved and reverts to 100% on reload
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/bionics.cpp:3172-3211`, `src/bionics.cpp:3219-3222`, `src/bionics.cpp:3242-3244`, `src/bionics.cpp:3295-3297`, `src/bionics.h:258`, `src/bionics_ui.cpp:877-887`

"Disabled" sets `safe_fuel_threshold = -1`, but `serialize()` only writes the threshold when `is_safe_fuel_on()` (threshold > -1). On load the default 1.0 comes back and fuel saving is silently re-enabled at 100%. The toggle is also offered for any remote-fueled bionic, while `is_safe_fuel_on()` requires BIONIC_POWER_SOURCE. All vanilla fueled bionics have that flag, so the second part only affects mods.

```cpp
// bionics.cpp:3242-3244
if( is_safe_fuel_on() ) { json.member( "safe_fuel_threshold", safe_fuel_threshold ); }
```

**Fix:** Always serialize `safe_fuel_threshold`, or serialize it whenever it differs from the default. Only offer the toggle where `is_safe_fuel_on()` can be true.

### MUT-B18 · Mutation "interrupt on mutate" is suppressed for normal visible->visible evolutions
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mutation.cpp:1963-1985`, `src/mutation.cpp:2003-2026`, `src/mutation.cpp:2061-2088`

In the prereqs-1 replacement block, the visibility cases are separate `if`s, and the trailing `else { do_interrupt = false; }` binds only to the third. When both traits are visible, or only the new one is, the message prints and then `do_interrupt` is cleared, so the `distraction_mutation` interrupt never fires for most upgrades. The prereqs2 block below correctly uses `else if`.

```cpp
:1964 if( mdata.player_display && replace_mdata.player_display ) { ... }
:1971 if( ... ) { ... }
:1978 if( !mdata.player_display && replace_mdata.player_display ) { ... } else { do_interrupt = false; }
```

**Fix:** Change the `if`s at :1971 and :1978 to `else if`, matching :2011/:2018.

### MUT-B19 · `activate_bionic` runs `activated_eocs` before failable checks; `auto_deactivates` bypasses `deactivate_bionic`
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/bionics.cpp:736-738`, `src/bionics.cpp:813-818`, `src/bionics.cpp:824-848`, `src/bionics.cpp:870-882`, `src/bionics.cpp:890-921`, `src/bionics.cpp:1188-1192`, `src/bionics.cpp:1193-1208`, `src/bionics.cpp:1251-1318`

`activated_eocs` fire before the gun power check, the "Stop wielding X?" prompt, target selection (lighter, EMP, lockpick) and remote-cable validation. All of those can abort afterwards, so EOC side effects happen for a bionic that never activated. Some aborts (bio_cqb style cancel, bio_evap, the BIONIC_GUN drain) do not refund the activation cost. `auto_deactivates` sets `it.powered = false` directly, skipping `deactivated_eocs`, weapon withdrawal and the enchantment recalc. An auto-deactivated BIONIC_WEAPON would get stuck as in MUT-B03. The "shorting out" message uses `add_msg` and so reports an NPC's bionic as the player's. Vanilla impact is small; this mostly catches modders out (see MUT-J02).

```cpp
// bionics.cpp:1201-1203
if( it.powered ) { it.powered = false; add_msg_if_player( ... "automatically turn off" ); }
```

**Fix:** Move the `activated_eocs` loop to just before the final `return true`. Replace the direct write with `deactivate_bionic( it, true )` and apply `auto_deactivates` on every branch. Use `add_msg_if_player` for the shorting message.

## 3. Performance

### MUT-P01 · Timed active mutations clear `trait_flag_cache` every turn and rebuild the mutation vector three times
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mutation.cpp:732`, `src/mutation.cpp:747`, `src/mutation.cpp:754`, `src/mutation.cpp:761`, `src/suffer.cpp:250`, `src/suffer.cpp:1731`, `src/mutation.cpp:163`, `src/mutation.cpp:176`, `src/character.cpp:7560`, `src/newcharacter.cpp:2107`

Every turn, `suffer::mutation_power` calls `get_cost_timer` and then `mod_cost_timer`/`set_cost_timer` for each active mutation, and for passive ones with `processed_eocs`. Both getter and setter build the full `get_functioning_mutations()` vector before checking `cached_mutations`, so that is up to three vector builds per timed mutation per turn. `set_cost_timer` also calls `trait_flag_cache.clear()`, although the timer has no effect on flags. Any active mutation with a nonzero `time` (9 in core, about 39 in mods, e.g. WINGS_INSECT, SNAIL_TRAIL, BIOLUM1/2) therefore wipes the cache every turn. After that, the first `has_flag()` call for each distinct flag that turn (about 100, including `monster::attitude`'s per-monster checks) rebuilds the vector and walks every mutation's flag sets. This applies to the player and every NPC.

```cpp
// mutation.cpp:735
const std::vector<trait_id> &all_mut = get_functioning_mutations();   // before the cache hit check
// mutation.cpp:753-754
iter->second.charge = set;
trait_flag_cache.clear();
```

**Fix:** Call `get_functioning_mutations()` only on the miss branch of `get_cost_timer`/`set_cost_timer`, and remove `trait_flag_cache.clear()` from `set_cost_timer`. Have `has_trait_flag` iterate `cached_mutations` directly (skipping corrupted entries) instead of building a vector. Fix the avatar check while there (MUT-B06).

### MUT-P02 · Trait "type" checks scan every mutation in the game; `are_same_type_traits` can be a set intersection
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mutation_type.cpp:27`, `src/mutation_type.cpp:55`, `src/mutation.cpp:2592`, `src/mutation.cpp:2597`, `src/mutation.cpp:2180`, `src/mutation.cpp:1696`, `src/character.cpp:5029`, `src/character.cpp:5079`, `src/character.cpp:5098`, `src/newcharacter.cpp:2694`

`get_mutations_in_type()` walks all traits (1076 in core, about 2650 with mods) with a string-set lookup on each. `are_same_type_traits(a,b)` builds that vector for every type of `a` just to test membership of `b`, then passes it by value to `contains_trait()`. `contains_trait` takes its vector by value, so `are_opposite_traits` and the higher/lower helpers also copy on every call. Callers: `update_cached_mutations` (nested loops whenever an enchantment grants or removes a trait), `get_same_type_traits` -> `has_conflicting_trait` (character creation calls it for all 192 starting traits on every keypress), and `mutate_towards`. These are cold or UI paths, so the cost is a few ms on rare events.

```cpp
// mutation.cpp:2594, 2597
return contains_trait( get_mutations_in_types( trait_a->types ), trait_b );
bool contains_trait( std::vector<string_id<mutation_branch>> traits, const trait_id &trait )
```

**Fix:** Implement `are_same_type_traits` as an intersection of `a->types` and `b->types` (identical result, including a == b). Have `get_same_type_traits` iterate the character's own mutations. Build a static type -> traits index in `mutation_branch::finalize_all()` and return const references. Make `contains_trait` take `const std::vector<trait_id> &`.

### MUT-P03 · `has_bionic`, `has_any_bionic` etc. copy all installed bionic ids into a new vector per call
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/bionics.cpp:2764`, `src/bionics.cpp:2773`, `src/bionics.cpp:2799`, `src/bionics.cpp:2804`, `src/bionics.cpp:3408`, `src/bionics.cpp:3576`, `src/bionics.cpp:3587`, `src/suffer.cpp:1270`, `src/character.cpp:641`, `src/character_health.cpp:2271`, `src/character.cpp:7479`, `src/character.cpp:7498`

`get_bionics()` allocates an unreserved vector of every installed CBM id. `has_bionic()` uses it for a linear search and `has_any_bionic()` just tests it for emptiness. The same pattern appears in `get_used_bionics_slots`, `bionic_armor_bonus`, `update_bionic_power_capacity`, `get_bionic_fueled_with_muscle`, `get_fueled_bionics` and `get_remote_fueled_bionic`. `has_bionic_with_flag`/`count_bionic_with_flag` call `has_active_bionic(bio.id)`, itself a linear scan, inside a loop over `my_bionics`, which is O(B²). Ids are small and no call site is very hot, so this is tens of microseconds per turn at most.

```cpp
// bionics.cpp:2801
return !get_bionics().empty();
```

**Fix:** Iterate `*my_bionics` directly in these helpers (`std::any_of`, `!my_bionics->empty()`). In the flag helpers, test the bionic in hand with a `bionic_is_active( const bionic & )` helper.

### MUT-P04 · Fuel bionics rebuild the worn-item list every turn (even for metabolism); `bio_cable` scans the whole inventory for a UPS
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/bionics.cpp:1320`, `src/bionics.cpp:1336`, `src/bionics.cpp:1424`, `src/bionics.cpp:1449`, `src/bionics.cpp:3444`, `src/bionics.cpp:3471`, `src/bionics_ui.cpp:236`, `src/bionics_ui.cpp:257`, `src/character_inventory.cpp:624`

`bionic_fuel_check()` calls `get_bionic_fuels()` for any bionic with `fuel_opts`, including metabolism, sunlight, wind and muscle, which never come from items. That builds `top_items_loc()` (one heap-allocated `item_location` per worn item). `get_cable_ups()` runs `visit_items()` over the whole nested inventory. The bionics menu titlebar repeats these for every installed bionic and constructs an `item(itype_battery)` on every redraw. The default safe-fuel threshold of 1.0 makes `burn_fuel` return early most of the time, which limits the real cost. (The over-drain in the same loop is MUT-B09.)

```cpp
// bionics.cpp:3448-3449
for( item_location it : top_items_loc() ) { if( !it->has_flag( flag_BIONIC_FUEL_SOURCE ) ) {
```

**Fix:** Skip `get_bionic_fuels` when every fuel option is a non-item fuel, and return early when `fuel_opts` is empty. Use `cache_get_items_with( flag_BIONIC_FUEL_SOURCE, ... )` and `cache_get_items_with( flag_IS_UPS, ... )` instead of the full scans. In the UI, only query fueled or remote-fueled bionics and cache the battery name.

### MUT-P05 · Each deserialized bionic copies the full `bionic_data` twice and builds a throwaway `bio_batteries` pseudo-item
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/bionics.h:214`, `src/bionics.h:218`, `src/bionics.cpp:194`, `src/bionics.cpp:3331`, `src/flexbuffer_json-inl.h:388`, `src/savegame_json.cpp:887`

`initialize_pseudo_items()` starts with `bionic_data bid( info() );`, a deep copy of a struct with about 20 containers. A const reference would do. The JSON vector reader default-constructs each element, and the default constructor delegates to `bionic( bionic_id( "bio_batteries" ), 'a', 0 )`, which copies bio_batteries' data and builds an `internal_battery_compartment` item that is then discarded. `deserialize()` then initializes again. This happens for every CBM, including NPCs', on every load. Load-time only.

```cpp
// bionics.h:214 / bionics.cpp:196
bionic() : bionic( bionic_id( "bio_batteries" ), 'a', 0 ) { }
bionic_data bid( info() );
```

**Fix:** Use `const bionic_data &bid = info();`. Give `bionic` a default constructor that only sets `id = NULL_ID` and skips `initialize_pseudo_items`.

### MUT-P06 · EOC math `sum_traits_of_category[_char_has]` builds full category vectors and inserts into a global map via `operator[]`
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/mutation.cpp:248`, `src/mutation.cpp:256`, `src/mutation.cpp:269`, `src/mutation.cpp:276`, `src/math_parser_diag.cpp:524`, `src/math_parser_diag.cpp:548`, `src/magic_enchantment.cpp:967`, `src/character_body.cpp:252`

`get_total_in_category()` builds a vector of every non-threshold trait in the category only to return its size. The `_char_has` variant then calls `has_permanent_trait` on each entry. Both use `mutations_category[categ]`, which inserts an empty entry for unknown ids. These functions appear about 156 times in Magiclysm and Xedra, mostly inside enchantment values that `recalculate_enchantment_cache` re-evaluates every turn. The categories are small (20-70 traits), so the gain is tens of microseconds per turn per affected character.

```cpp
// mutation.cpp:251-252
std::vector<trait_id> list = get_in_category( categ, count_type );
return list.size();
```

**Fix:** Use `find()` and count in place. For `_char_has`, iterate the character's mutations and test `mut->category`. Optionally precompute per-category totals in `finalize_all()`.

## Appendix: considered and rejected

- Mutation rolls rebuild the character's mutation and bionic vectors for every candidate trait – `mutate()` only runs on rare events (hours of game time apart), and no data uses `mutation_rounds`, so the NPC-spawn path never runs. A few ms per event is negligible.
- `process_bionic` copies the bionic's entire weapon item every turn while a weapon CBM is out – Bionic weapons are simple items with no contents, so the copy costs about a microsecond. It is dwarfed by NPC weapon evaluation, and `set_wielded_item` needs a copy anyway.
- Reflex-trigger mutations build a dialogue and talker per trigger and copy translation pairs every turn – an early out handles mutations without triggers. Only 14 trigger definitions exist, and a character typically has 0-1 of them. Negligible.
- `heat_emission` builds an `emit_id` from a string every turn for exothermic power CBMs – one short concat and intern per turn, at most one per character. Sub-microsecond next to the field emission itself.
- Applying traits one at a time triggers a full `reset()` and encumbrance/sight recalculation per trait – `set_mutations()` is debug-menu only. The other per-trait loops are one-time character-creation paths, and saved NPCs load mutations directly.
