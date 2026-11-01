# 16 · Skills, proficiencies, professions & character creation

> Engine audit – static code reading at commit `a1a646d6`; nothing was compiled or run. Each finding was checked against the code by a separate reviewer: **confirmed** = verified by reading the code; **plausible** = consistent with the code, but reachability or impact not proven. Line numbers refer to that commit.

**Scope:** `src/skill.cpp`, `src/proficiency.cpp`, `src/character_knowledge.cpp`, `src/character_proficiency.cpp`, `src/profession.cpp`, `src/profession_group.cpp`, `src/scenario.cpp`, `src/newcharacter.cpp`, `src/start_location.cpp`, `src/player_difficulty.cpp`, `src/trait_group.cpp`

## Summary

Character creation works for the common case, but several JSON fields that mods depend on do not do what they claim. `chargen_allow_npc` is defeated by a cache that ignores its argument, profession proficiencies are dropped when listed before their prerequisites (Firefighter never gets Burn Care), profession `recipes` look up the wrong recipe, and hobbies silently ignore most profession fields. Skill progression, rust and reading rules are hardcoded C++ with few JSON or EOC hooks, and an EOC can push a skill to level -1 and crash the game. Load-time validation for proficiencies and trait groups is missing or broken. The performance issues are mostly in the character creator and NPC randomization; none of them is in a per-turn hot path except the mutation-vector allocations.

| Lens | Critical | High | Medium | Low | Total |
|---|---|---|---|---|---|
| Missing JSON / EOC | 0 | 0 | 9 | 7 | 16 |
| Bugs & quirks | 0 | 3 | 8 | 8 | 19 |
| Performance | 0 | 0 | 2 | 7 | 9 |
| **Total** | 0 | 3 | 19 | 22 | 44 |

**Fix first:**
1. **PROG-B03** · `permitted_professions`/`permitted_hobbies` cache ignores `is_npc`: a small change that makes `chargen_allow_npc: false` work again for the ~87 mod entries that use it.
2. **PROG-B02** · Profession/hobby proficiencies listed before their prerequisites are never granted: passing `recursive=true` fixes vanilla professions (Firefighter, engineers, EOD) that lose advertised proficiencies today.
3. **PROG-B01** · EOC can push a skill to level -1, then any skill read crashes: a hard SIGFPE crash that a few clamps prevent.
4. **PROG-B04** · `set_bionics_from_hobbies` returns on the first duplicate CBM: a one-word `return` to `continue` fix for lost, paid-for CBMs.
5. **PROG-B06** · Book proficiency bonuses count prerequisites several times: small fix for inflated crafting mitigation and a stack-overflow on cyclic prerequisites.

## 1. Missing JSON bindings & EOC features

### PROG-J01 · Default hobby package is hardcoded; SKIP_DEFAULT_BACKGROUND/NO_BONUS_ITEMS/LONE_START are read from a different object than documented
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/newcharacter.cpp:106`, `src/newcharacter.cpp:793`, `src/newcharacter.cpp:2265`, `src/newcharacter.cpp:2369`, `src/profession.cpp:626`, `src/start_location.cpp:485`, `src/game.cpp:912`, `src/npc.cpp:777`, `src/player_difficulty.cpp:119`, `doc/JSON/JSON_FLAGS.md:1495`

The `profession_group` type has one consumer: the hardcoded id `adult_basic_background` (also used by `npc.cpp:777` and `player_difficulty.cpp:119`). It can only be skipped through the scenario flag SKIP_DEFAULT_BACKGROUND, but JSON_FLAGS.md:1495 documents that flag as a profession flag. Core `churl` and 7 Xedra_Evolved professions set it on the profession, where it is ignored, so a medieval churl still gets Driving License and Computer Literate. The reverse happens for NO_BONUS_ITEMS: documented as a scenario flag, only checked on the profession. LONE_START is a scenario flag, but `place_player` reads it from start_location flags, which no start location defines, so `accommodate_npc` is always true (low impact: an extra free tile is required).

```cpp
// newcharacter.cpp:794-796
if( type != character_type::TEMPLATE && !get_scenario()->has_flag( flag_SKIP_DEFAULT_BACKGROUND ) ) {
    add_default_background();
}
// start_location.cpp:485
const bool accommodate_npc = flags().count( "LONE_START" ) == 0;
```

**Fix:** Add `"default_backgrounds": "<profession_group_id>"` (empty string = none) to scenario, profession and npc_class, resolved profession → scenario → a global EXTERNAL_OPTION. Honor SKIP_DEFAULT_BACKGROUND on professions and re-evaluate it when the profession changes in the creator. Fix the NO_BONUS_ITEMS doc or also honor it on scenarios. Pass the scenario's LONE_START into `place_player`.

### PROG-J02 · No load-time validation for proficiencies; cyclic `required_proficiencies` cause infinite recursion
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/proficiency.cpp:78`, `src/proficiency.cpp:386`, `src/proficiency.h:85`, `src/init.cpp:983`, `src/npc.cpp:774`, `src/proficiency.cpp:481`

Proficiencies only have `finalize_all()`; no check is registered (init.cpp:983 only checks proficiency_migration). Nothing validates `category` ids (bad ones surface later as `obj()` debugmsgs in the UI), `required_proficiencies` ids (an unknown prerequisite makes the proficiency permanently unlearnable), or prerequisite cycles. `proficiency_set::learn(id, recursive=true)` recurses into prerequisites before inserting, so A → B → A overflows the stack. The only recursive caller is NPC class starting proficiencies (`npc.cpp:774`), so the crash needs malformed mod data plus an NPC class; the EOC path is non-recursive. A learnable proficiency without `time_to_learn` silently gets a 9999 h default, which PROFICIENCY.md does not mention.

```cpp
// proficiency.cpp:388-395
for( const proficiency_id &req : learned->required_proficiencies() ) {
    ... else if( recursive ) { learn( req, recursive ); }
}
known.insert( learned );
// proficiency.h:85
time_duration _time_to_learn = 9999_hours;
```

**Fix:** Add `proficiency::check_consistency` registered in init.cpp: validate category and prerequisite ids, detect cycles with a DFS, warn on unknown bonus keys (see PROG-J09), and warn when `can_learn` is true but `time_to_learn` is unset. Guard `learn()` against cycles with a visited set.

### PROG-J03 · Reading and book enjoyment rules hardcode trait ids and one book id
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character_knowledge.cpp:52`, `src/character_knowledge.cpp:395`, `src/character_knowledge.cpp:403`, `src/character_knowledge.cpp:446`, `src/character_knowledge.cpp:593`, `src/npc.cpp:1489`, `src/avatar.cpp:768`

`book_fun_for` makes *To Serve Man* (`cookbook_human`) enjoyable only to trait CANNIBAL, flag PSYCHOPATH or trait SAPIOVORE. Everywhere else the engine uses the CANNIBAL `json_character_flag` (character.cpp:7582, consumption.cpp:1519, character_health.cpp:633, item_info.cpp:699), so Xedra's VAMPIRE, VAMPIRE2 and RENFIELD mutations, which carry the flag but not PSYCHOPATH, enjoy eating people but hate the cookbook. SPIRITUAL+INSPIRATIONAL (x3), LOVES_BOOKS/HATES_BOOKS, ILLITERATE (checked by `has_trait` in about 10 files) and PROF_DICEMASTER (skips the int requirement) are also hardcoded trait checks. No enchantment value or flag covers book fun, so modded traits cannot reuse any of this.

```cpp
// character_knowledge.cpp:395-397
if( ( p.has_trait( trait_CANNIBAL ) || p.has_flag( json_flag_PSYCHOPATH ) ||
      p.has_trait( trait_SAPIOVORE ) ) && book.typeId() == itype_cookbook_human )
// :446
if( book_requires_intelligence && has_trait( trait_ILLITERATE ) )
```

**Fix:** At minimum switch `trait_CANNIBAL` to `has_flag( json_flag_CANNIBAL )`. Then use json_character_flags `ILLITERATE` and `IGNORE_BOOK_INT_REQ`, and enchantment values `BOOK_FUN_ADD` / `BOOK_FUN_MULTIPLY` (with an item-flag condition). Give books an optional `"fun_overrides": [ { "condition": { "u_has_flag": "CANNIBAL" }, "fun": "abs" } ]`, or an item flag such as `ENJOYED_BY_CANNIBALS`.

### PROG-J04 · Hobbies silently ignore most profession fields; profession EOCs never run for NPCs
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/effect_on_condition.cpp:165`, `src/effect_on_condition.cpp:123`, `src/newcharacter.cpp:627`, `src/newcharacter.cpp:836`, `src/newcharacter.cpp:906`, `src/newcharacter.cpp:998`, `src/newcharacter.cpp:1049`, `src/newcharacter.cpp:3825`, `src/npc.cpp:651`, `src/character.cpp:457`

Hobbies are loaded by `profession::load`, so every profession field parses, but only skills, proficiencies, recipes, CBMs, addictions, spells, traits and missions are applied. Ignored for hobbies: `effect_on_conditions` (only `prof->get_eocs()` is queued), `items` (`add_profession_items` returns early for hobbies), `pets`, `vehicle`, `starting_styles(_choices)`, `starting_cash`, `starting_trauma`, and `forbidden_traits` (trait selection checks only scenario and profession). JSON_INFO.md:2099 says hobby skills are "added on top of" profession skills; the code takes the max (newcharacter.cpp:843-845). On the NPC side, random wanderers (NC_NONE) return at npc.cpp:651-655 before `load_new_character`, and for class NPCs `queue_effects` drops any EOC that is not `run_for_npcs`, which is only legal on global EOCs. So a profession's inline EOCs never run for NPCs with that profession (e.g. XedraWood `pattern_tattoo_mountain_prof`). Impact today is latent: none of the 317 hobbies use an ignored field, only 2 professions use EOCs, global+`run_for_npcs` EOCs work for class NPCs, and recurring EOCs for wanderers are picked up later via `load_existing_character`. The doc mismatch is real now.

```cpp
// effect_on_condition.cpp:165-168 (no hobby loop)
const profession *prof = you.get_profession();
if( prof ) { you.queue_effects( prof->get_eocs() ); }
// newcharacter.cpp:627
if( prof->is_hobby() ) { return; }
// character.cpp:457
if( is_avatar() || eoc.run_for_npcs ) {
```

**Fix:** Apply hobby `effect_on_conditions`, items, pets, cash/trauma deltas and martial arts in `initialize`/`load_new_character`, or reject these fields when `subtype == hobby`. Enforce hobby `forbidden_traits`. Fix the doc to say hobby skills take the max (or change the code to add). Queue profession and hobby EOCs for NPCs unconditionally, or allow `"effect_on_conditions": [ { "id": "EOC_X", "run_for_npcs": true } ]` on professions, and call `load_new_character` on the NC_NONE path.

### PROG-J05 · Scenario docs describe `items`, `cbms`, `map_special` and variant `forced_traits`, none of which the loader supports
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/scenario.cpp:59`, `src/scenario.cpp:94`, `src/scenario.cpp:102`, `src/newcharacter.cpp:2174`, `src/newcharacter.cpp:2354`, `src/profession.cpp:842`, `src/generic_factory.h:913`, `doc/JSON/JSON_INFO.md:4079`

JSON_INFO.md documents scenario fields that `scenario::load` never reads. `items` (:4079) and `cbms` (:4103) are absent; a top-level use errors loudly, but scenarios cannot give starting gear or CBMs except through forced traits or EOCs. `map_special` (:4152) is read as `map_extra`; MindOverMatter and Xedra_Evolved use `"extend": { "map_special": ... }`, which `handle_extend` silently ignores (it calls `allow_omitted_members`). Today this is harmless because those scenarios copy from `heli_crash`, which already sets `map_extra`. `forced_traits` is documented as accepting `{trait, variant}` but is read into a `std::set<trait_id>`, and profession trait variants (loaded as `trait_and_var`) are dropped by `add_traits` (`toggle_trait_deps( tr.trait )`, also :739-741 and :3734-3738); no data uses variants yet. The doc says the first `professions` entry is the default, but `reset_scenario` picks `min_element` under `profession_sorter`. `vehicle`, `surround_groups`, `add_professions` and `blacklist_professions` are undocumented, the doc's male/female `name` object form is unsupported (`get_string`), and `flags` says "TODO: document those flags here".

```cpp
// scenario.cpp:94, :102
optional( jo, was_loaded, "forced_traits", _forced_traits, string_id_reader<::mutation_branch> {} );
optional( jo, was_loaded, "map_extra", _map_extra, map_extra_id::NULL_ID() );
// generic_factory.h:913-914
JsonObject tmp = jo.get_object( "extend" ); tmp.allow_omitted_members();
```

**Fix:** Either implement scenario `"items"` (both/male/female item groups), `"CBMs"`, `"skills"`, `"proficiencies"` and `"starting_cash"` by reusing the profession readers, or remove them from the docs. Rename the doc entry to `map_extra` and warn on unknown members inside `extend`/`delete`. Make `forced_traits` a `std::vector<trait_and_var>` and pass the variant through `add_traits`. Use the first `professions` entry as default, or fix the doc.

### PROG-J06 · EOCs cannot read or train theoretical skill, rust or last-practiced; no practice-based path; ambiguous skill events
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/math_parser_diag.cpp:1146`, `src/math_parser_diag.cpp:1944`, `src/talker_character.cpp:429`, `src/skill.h:173`, `src/character.cpp:2609`, `src/activity_actor.cpp:2329`, `src/activity_actor.cpp:13084`, `src/skill.cpp:483`, `src/skill.cpp:517`

The skill math API is `skill()` (practical level) and `skill_exp()` (practical exercise). Nothing exposes knowledge level/experience, `rustAccumulator`, `isRusty` or `_lastPracticed`, though `SkillLevel` has accessors for most. There is no skill equivalent of `proficiency(..., 'direct': false)`, so EOC skill writes bypass focus, catch-up, caps and the level-up event. `gains_skill_level` is sent both for practical gains (character.cpp:2609) and book theory gains (activity_actor.cpp:2329/2477/13084) with no field saying which; theory gains from practice send nothing, and level loss from rust sends nothing. `training_activity_actor::train_skill` can send the event twice. Modders cannot build a theory-only book item, a focus-respecting training dummy, a rust warning or a "refresh memory" consumable.

```cpp
// math_parser_diag.cpp:1944-1945
{ "skill", { "un", 1, skill_eval, skill_ass } },
{ "skill_exp", { "un", 1, skill_exp_eval, skill_exp_ass, { "format" } } },
// skill.cpp:517-519 (no event)
if( _exercise < 0 ) { ... --_level; }
```

**Fix:** Add `u_skill_knowledge('id')` (read/assign), `u_skill_knowledge_exp('id', 'format': 'percentage'|'raw')`, `u_skill_rust('id')` and `time_since_skill_practiced('id')`. Add `u_skill_exp('id', 'practice': true) += N`, which calls `Character::practice`. Add `"type": "practical"|"theoretical"` to `gains_skill_level`, and a new `loses_skill_level { character, skill, new_level, type }` event fired from rust.

### PROG-J07 · copy-from/extend/delete gaps: scenario dates and surround_groups, profession pets/spells/EOCs, skills, trait groups
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/scenario.cpp:119-160`, `src/scenario.cpp:166-169`, `src/generic_factory.h:268`, `src/profession.cpp:286-311`, `src/skill.cpp:57`, `src/skill.cpp:160`, `src/mutation_data.cpp:1010`, `src/mutation_data.cpp:1051`

Several loaders are hand-written and break inheritance. `scenario::load` reads `start_of_cataclysm` and `start_of_game` only `if( !was_loaded )`, and generic_factory sets `was_loaded=true` for copy-from, so a copy-from scenario (82 in data) silently cannot change its dates. `surround_groups` (scenario) and `pets`, `spells`, `effect_on_conditions` (profession) are appended on every load, so a child gets parent plus child entries and cannot replace or remove them; `spells` is a map `emplace`, so it cannot even change an inherited spell's level. Skills bypass generic_factory: `load_skill` erases and re-pushes, so there is no copy-from/extend/delete and omitting `sort_rank` triggers a debugmsg. Trait groups re-emplace and append; `Trait_group::remove_trait` has no JSON caller. No shipped content trips this today. Profession/scenario `vehicle` can be cleared with `"vehicle": "null"`.

```cpp
// scenario.cpp:120
if( !was_loaded ) { ... if( jo.has_member( "start_of_cataclysm" ) ) { ... } ... }
// scenario.cpp:166-169
for( JsonArray ja : jo.get_array( "surround_groups" ) ) { _surround_groups.emplace_back( ... ) }
// profession.cpp:289-296
if( jo.has_array( "pets" ) ) { ... _starting_pets.push_back( mon ); }
```

**Fix:** Read the date objects whenever present, defaulting to inherited values. Give `surround_groups`, `pets`, `spells` and `effect_on_conditions` proper readers (weighted_string_id_reader / map readers) that clear on assignment and support `extend`/`delete`. Migrate `Skill` to `generic_factory<Skill>` (which also makes lookups O(1), see PROG-P05). Support `"override": true` on `trait_group`.

### PROG-J08 · Skill progression is fully hardcoded: XP curve, catch-up, book gap, rust timing and magnitude
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/skill.cpp:348`, `src/skill.cpp:402`, `src/skill.cpp:417`, `src/skill.cpp:444`, `src/skill.cpp:495`, `src/skill.cpp:516`, `src/skill.cpp:720`, `src/character_knowledge.cpp:255`, `src/character.cpp:2558`

All skills share fixed formulas: XP to level 10000*(L+1)^2, fixed catch-up and 0.9 knowledge cap, a book level-gap formula with a TODO saying it should be per-skill JSON, 24 h rust grace, no rust at MAX_SKILL, base rate 16*level_multiplier with a 3x cap, and `readBook` gain (L+1)*rng(min,max)*100. `Character::practice` hardcodes PACIFIST /3 and SAVANT 0.5. Learning enchantments (READING_EXP, SKILL_RUST_RESIST, LEARNING_FOCUS, THEORETICAL_SKILL_CATCHUP_BONUS) are global only; enchantment `skills` values only change effective level. Combat-skill rust immunity is tied to PRED2/3/4 with a hardcoded message and `mod_stim(1)`. A mod cannot make a skill that never rusts, one that is hard to learn from books, or a faster-levelling magic skill. (The barter curve at :720-725 is also fixed, but it is trade pricing rather than progression.)

```cpp
// skill.cpp:417-419
// Some day this should be affected by json specific to the skill ...
level_gap = std::max<float>( unadjustedKnowledgeLevel() - unadjustedLevel(), 1.0f );
// :446, :469
if( ( calendar::turn - _lastPracticed ) < 24_hours )
int rust_amount = level_multiplier * rust_multiplier * 16 / rust_slowdown;
```

**Fix:** Add per-skill fields `"rust": { "grace_period": "24 h", "rate": 16, "max_fraction": 3.0, "enabled": true }`, `"training_multiplier": 1.0`, `"book_learning_multiplier": 1.0`, `"catchup_multiplier": 1.0`. Allow skill-keyed enchantment values, e.g. `"skill_rust_resist": [ { "skill": "melee", "add": 100 } ]` and `"skill_training": [ { "skill": "gun", "multiply": 0.5 } ]`, and replace the PRED special case with such enchantments on the PRED traits (see also PROG-B16).

### PROG-J09 · Proficiencies cannot grant enchantments or flags; `bonuses` limited to 3 hardcoded keys, unvalidated, int-truncated
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/proficiency.h:28`, `src/proficiency.cpp:126`, `src/character_proficiency.cpp:112`, `src/character.cpp:4932`, `src/item_gun_tool_ammo.cpp:4541`, `src/vehicle.cpp:1156`, `src/melee.cpp:982`

Proficiency effects come only from recipes, weakpoints and the `bonuses` map. Bonus keys are free strings consumed in exactly three places ("archery", "melee_attack", "athlete"); unknown or typoed keys are accepted and do nothing (PROFICIENCY.md does say keys are hardcoded). `Character::get_proficiency_bonus` returns `int`, so documented Float values like 0.5 truncate to 0 on the archery and athlete paths (latent: shipped values are 2). `recalculate_enchantment_cache` collects from items, bionics, effects and mutations, not proficiencies. A "Parkour" proficiency that lowers movement cost or one that grants a character flag is impossible without a trait-enchantment workaround conditioned on `has_proficiency()`.

```cpp
// proficiency.h:28-35
enum class proficiency_bonus_type : int { strength, dexterity, intelligence, perception, stamina, last };
// character_proficiency.cpp:112-116 (float -> int)
int Character::get_proficiency_bonus( ... ) const { return _proficiencies->get_proficiency_bonus( category, prof_bonus ); }
```

**Fix:** Add `"enchantments": [ <id or inline> ]` and `"flags": [ <json_character_flag> ]` to proficiency, applied while known, by iterating known proficiencies in `recalculate_enchantment_cache` and `has_flag`. Validate `bonuses` keys against a registered list in a new `proficiency::check()` (PROG-J02), and return `float` from `get_proficiency_bonus`.

### PROG-J10 · EOCs cannot query the scenario or start location, change profession/hobbies, or use a profession math function
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/condition.cpp:463`, `src/condition.cpp:2589`, `src/effect_on_condition.cpp:159`, `src/math_parser_diag.cpp:1919`, `src/savegame_json.cpp:1513`

The only profession-related EOC hook is the read-only condition `u_has_profession` (which also checks hobbies). `get_scenario()` is not used by condition.cpp, math_parser_diag.cpp, npctalk or the talkers, and no `game_start`/`game_avatar_new` event field carries scenario or start location. There is no effect to set the profession or add/remove a hobby, although hobbies are used as species and backgrounds (XedraWood/Xedra species hobbies, vampire conversion) and saves already store them. There is no `has_profession()` math function to mirror `has_proficiency`. The workaround (set a var from the scenario `eoc`, which supports extend/delete) is used by core data and works.

```cpp
// condition.cpp:2589 (only profession entry)
{"u_has_profession", "npc_has_profession", jarg::string, &conditional_fun::f_has_profession },
```

**Fix:** Add conditions `{ "is_scenario": "evacuee" }` (also accepting a var) and `{ "u_has_start_location": "sloc_x" }`; effects `{ "u_set_profession": "id" }`, `{ "u_add_hobby": "id", "apply_starting_bonuses": false }`, `{ "u_remove_hobby": "id" }`; and math `has_profession('id')`.

### PROG-J11 · `scenario::generic()` caches GENERIC_SCENARIO_ID in a function-static, so per-mod overrides stick across worlds
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/scenario.cpp:172`, `src/scenario.cpp:174`, `src/profession.cpp:385`, `src/newcharacter.cpp:750`, `src/newcharacter.cpp:453`

GENERIC_SCENARIO_ID is an EXTERNAL_OPTION that mods override (Aftershock: `augustmoon`, also classic_zombies, Isolation-Protocol, hunvre). `scenario::generic()` reads it into a `static const` on first call and never resets it, while `profession::generic()` re-reads its option each time. After switching worlds in one session, the stale id is used; if it is blacklisted (Aftershock's whitelist excludes `evacuee`), `generic()` falls back to the first whitelisted scenario. That affects the creator's default scenario and sort order, NPC randomization and the missing-scenario fallback on save load.

```cpp
// scenario.cpp:174-175
static const string_id<scenario> generic_scenario_id( get_option<std::string>( "GENERIC_SCENARIO_ID" ) );
// profession.cpp:387-388
const string_id<profession> generic_profession_id( get_option<std::string>( "GENERIC_PROFESSION_ID" ) );
```

**Fix:** Drop the `static` (or reset it in `scenario::reset()`). Emit a debugmsg when the configured id is missing or blacklisted instead of silently using the first entry.

### PROG-J12 · Start-location BOARDED/curtain preparation only knows a handful of vanilla terrain ids
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/start_location.cpp:218`, `src/start_location.cpp:222`, `src/start_location.cpp:286`, `src/start_location.cpp:44`, `data/json/mapgen_palettes/house_general_palette.json:79`, `data/json/mapgen_palettes/house_general_palette.json:95`, `doc/JSON/JSON_FLAGS.md:1501`

`board_up` only recognizes `t_window_domestic`, `t_window` and `t_window_no_curtains` as windows and `t_door_c`, `t_door_locked`, `t_door_c_peep` as doors; `prepare_map` closes curtains only by translating `t_window_domestic` to `t_curtains`. The house palettes used by `sloc_house_boarded` also place electronic-locked doors, `t_door_locked_peep`, alarm doors and windows, open windows and `t_curtains` (house_general_palette.json:79-109), all left unboarded, contradicting JSON_FLAGS.md:1501. Modded doors and windows are never handled.

```cpp
// start_location.cpp:218, :286
if( t == ter_t_window_domestic || t == ter_t_window || t == ter_t_window_no_curtains ) {
m.translate( ter_t_window_domestic, ter_t_curtains );
```

**Fix:** Drive this from terrain data. Add a terrain field such as `"boarded": "t_window_boarded"` (or `boarded_transform` used with the DOOR/WINDOW flags) and a new flag or field marking which `close` transform draws curtains (no CURTAINED flag exists today, and about half of the existing `close` transforms close the window rather than the curtain). Keep the current ids as fallbacks.

### PROG-J13 · Skill `requires_all_traits`/`requires_any_traits` only hide the skill on the @ screen; doc example uses wrong field names
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/skill.cpp:298`, `src/skill.cpp:100`, `src/skill.cpp:119`, `src/newcharacter.cpp:3284`, `src/player_display.cpp:1652`, `src/character.cpp:2542`, `doc/JSON/JSON_INFO.md:2556`, `doc/JSON/JSON_INFO.md:2577`, `data/mods/Xedra_Evolved/skills.json:47`

`can_chr_use` is used only by `get_skills_for_chr_display` (player_display.cpp:1652). The creator lists skills via `get_skills_sorted_by` without the trait filter, and `practice()`, books, NPC teaching and EOC `skill()` ignore it too. So Xedra_Evolved's `gramarye` can be bought at chargen by any character and then disappears from the @ screen. The JSON_INFO.md example uses `required_traits`/`allowed_traits`, which are not loaded (only the table below has the real names), and `obsolete` is undocumented. The table does document the fields as display-only, so the lack of training enforcement is a feature gap.

```cpp
// skill.cpp:118-121 (no can_chr_use)
for( const Skill &sk : skills ) { if( !sk.obsolete() ) { result.push_back( &sk ); } }
```

**Fix:** At least apply the filter in the creator's skill list. Either rename the fields to `display_requires_*`, or add `"trait_requirements_mode": "display"|"train"` checked in `practice()`, `readBook` and the creator. Fix the doc example and document `obsolete`.

### PROG-J14 · Scenario start-up behaviors are hardcoded flags with hardcoded tuning (FIRE_START, HELI_CRASH, BORDERED)
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/game.cpp:949`, `src/game.cpp:952`, `src/game.cpp:983`, `src/start_location.cpp:557`, `src/start_location.cpp:598`, `src/newcharacter.cpp:1919`, `src/scenario.cpp:116`

FIRE_START calls `burn(omtstart, 3, 3)`. HELI_CRASH hardcodes skipping head/torso, a 1-8 roll, 6 min bleed and 33-66% HP damage, then searches vehicles whose type contains "helicopter" and renames one "Bird Wreckage". BORDERED places a fixed 100x100 `overmap_special_world` ring. Flag descriptions in the creator are a C++ table with "TODO: Move this to JSON?"; SUR_START exists only as a description string. Scenarios already have an `eoc` list and a `map_extra` hook that cover custom injuries and some map changes, so this is low.

```cpp
// start_location.cpp:600-602
if( bp == bodypart_id( "head" ) || bp == bodypart_id( "torso" ) ) { continue; }
// game.cpp:957
std::string search = std::string( "helicopter" );
```

**Fix:** Keep the flags as presets and add scenario fields `"start_fires": { "count": 3, "safe_radius": 3 }`, `"border": { "special": "world", "size": 100 }`, `"start_in_vehicle": { "vehicle_flag": "CONTROLS", "rename": "..." }`. Express HELI_CRASH injuries as a scenario EOC. Add `"display_flags": [ { "flag": "FIRE_START", "text": "Fire nearby" } ]` on scenarios, or a flag-description JSON type.

### PROG-J15 · Character-creator difficulty rating hardcodes benchmark items, the 'unemployed' baseline and weights
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/player_difficulty.cpp:38`, `src/player_difficulty.cpp:45`, `src/player_difficulty.cpp:250`, `src/player_difficulty.cpp:316`, `src/player_difficulty.cpp:368`, `src/player_difficulty.h:73`

The rating compares every character against an NPC with `profession_unemployed` rather than `profession::generic()` (which Aftershock, Isolation-Protocol and hunvre override), six hardcoded benchmark weapons (knife_combat, machete, bat, swbodyguard, ruger_1022, mossberg_500), fixed armor weights for head/torso/arm_r/leg_r, and fixed category weights and bands. Ratings in total conversions are computed against content that does not fit the setting. The rating is advisory text, so impact is low.

Reviewer note: `player_difficulty` is a function-static singleton, so `average.prof` keeps a pointer into the profession factory's vector from the first world; after switching worlds in one session it may name another profession or dangle.

```cpp
// player_difficulty.cpp:38, :45
static const profession_id profession_unemployed( "unemployed" );
average.prof = &profession_unemployed.obj();
```

**Fix:** Use `profession::generic()` for the baseline and refresh it per world. Move benchmarks and weights into a JSON singleton, e.g. `{ "type": "chargen_rating", "benchmark_weapons": [ "knife_combat", ... ], "armor_parts": { "torso": 0.4, ... }, "bands": { "combat": 0.5, ... } }`.

### PROG-J16 · Chargen numeric rules and starting-state special cases are hardcoded
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/newcharacter.cpp:286`, `src/newcharacter.cpp:236`, `src/newcharacter.cpp:244`, `src/newcharacter.cpp:469`, `src/newcharacter.cpp:716`, `src/newcharacter.cpp:889`, `src/newcharacter.cpp:909`, `src/newcharacter.cpp:915`, `src/character.h:128`, `src/game_constants.h:94`

Fixed in C++: the skill point cost curve `{ 0, 1, 1, 2, 4, 6, 9, 12, 16, 20, 25 }`; double stat cost above HIGH_STAT=12 (the base pool is partly tunable via INITIAL_STAT_POINTS); creator stat range 4-20 and skill range 0-10; random stats `rng(6,10)` and 0-3 random hobbies; fallback cash `rng(-200000, 200000)` (scenarios can override via `eoc` and `u_val('cash')`). `initialize()` special-cases SMELLY/WEAKSCENT (scent 800/300), duplicating mutation `scent_intensity`, though scent converges to that value each turn anyway. It also special-cases the XS/XXXL starting-weight traits for stored kcal (/5, x5), so modded weight traits cannot reproduce that.

```cpp
// newcharacter.cpp:286
static std::array < int, 1 + MAX_SKILL > costs = { 0, 1, 1, 2, 4, 6, 9, 12, 16, 20, 25 };
// :909
cash = prof->starting_cash().value_or( rng( -200000, 200000 ) );
```

**Fix:** Initialize scent from the mutations' `scent_intensity`. Add a mutation field `"starting_kcal_multiplier"`. Add scenario fields `"stat_limits": [4, 20]`, `"skill_limits": [0, 10]`, `"starting_cash"` (overridden by profession), and move the cost curve and random ranges to EXTERNAL_OPTIONs.

## 2. Bugs, oversights & quirks

### PROG-B01 · EOC can push a skill to level -1, then any skill read crashes on integer divide-by-zero
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/skill.h:165-171`, `src/skill.h:189-191`, `src/skill.cpp:509-523`, `src/math_parser_diag.cpp:1152-1156`, `src/math_parser_diag.cpp:1172-1184`, `src/talker_character.cpp:434-447`, `src/character_knowledge.cpp:168-172`, `src/character_knowledge.cpp:207-210`, `src/skill.cpp:600-603`, `src/savegame_json.cpp:519-530`

Nothing clamps skill levels at 0 on the EOC path. `{ "math": [ "u_skill('melee') -= 1" ] }` on a level-0 skill stores `_level = -1` directly. `u_skill_exp('melee') = -5` at level 0 makes `_exercise` negative, and `on_exercise_change` then decrements the level to -1. After that, `SkillLevel::exercise()` divides by `100*(level()+1)^2 == 0`, and it is reached from `Character::get_skill_level` (melee, crafting, display, ...). The game crashes with SIGFPE on the next skill read; only a reload clears it (deserialize clamps), so unsaved progress is lost. Other writers (NPC roll, rust, wish menu) already clamp. No shipped content triggers it. A related quirk: lowering a level via `set_skill_level` keeps the old XP, so the skill levels up again on the next practice.

```cpp
// skill.h:189-190
int exercise( bool raw = false ) const {
    return raw ? _exercise : _exercise / ( 100 * ( level() + 1 ) * ( level() + 1 ) ); }
// skill.cpp:517-519
if( _exercise < 0 ) { _exercise = ( 100 * 100 * pow( unadjustedLevel(), 2U ) ) - 1; --_level; }
// math_parser_diag.cpp:1155 (no clamp)
d.actor( is_beta( scope ) )->set_skill_level( skill_id( params[0].str( d ) ), val );
```

**Fix:** Clamp in `SkillLevel::level(int)` and `knowledgeLevel(int)` to `[0, max]`. In `on_exercise_change`, when `_level == 0 && _exercise < 0`, set `_exercise = 0` instead of lowering the level. Optionally clamp in `skill_ass`/`skill_exp_ass` too, and reset `_exercise` when `set_skill_level` lowers a level.

### PROG-B02 · Profession/hobby proficiencies listed before their prerequisites are silently never granted
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/newcharacter.cpp:952-955`, `src/newcharacter.cpp:860-870`, `src/newcharacter.cpp:1191`, `src/proficiency.cpp:386-396`, `src/character_proficiency.cpp:47-55`, `src/profession.cpp:503-507`, `src/npc.cpp:773-775`

`Character::initialize` grants each profession and hobby proficiency with `add_proficiency(pri)`, which defaults to `recursive=false`. `proficiency_set::learn` then returns without doing anything if a prerequisite is not yet known, so the result depends on JSON order. `profession::check_definition` only checks that ids are valid. Vanilla is affected: Firefighter lists `[ "prof_burn_care", "prof_wound_care" ]` and Burn Care requires Wound Care, so firefighters never get it. Same for engineer_chem, engineer_mech, engineer_ee, explosive_ordnance_disposal, the cooking_expert/meatarian/frying hobbies, and mod professions (bionic_prepper/cykotic, Xedra vampire hunters, Aftershock tribal, XedraWood brewer); a scan finds 14-16 affected entries. Hobbies are a pointer-ordered `std::set`, so cross-hobby prerequisites depend on pointer order. The creator preview (:1191) still lists these as granted. NPC classes already use the recursive form.

```cpp
// newcharacter.cpp:953-954
for( const proficiency_id &pri : prof->proficiencies() ) { add_proficiency( pri ); }
// proficiency.cpp:388-390
if( !has_learned( req ) && !recursive ) { return; }
// npc.cpp:774
add_proficiency( prof, false, true );
```

**Fix:** Use `add_proficiency( pri, false, /*recursive=*/true )` in both the profession and hobby loops. Add a `check_definition` warning when a listed proficiency's `required_proficiencies` are neither listed nor implied. Optionally support `"proficiencies": [ { "id": "prof_x", "include_prereqs": true } ]`.

### PROG-B03 · `permitted_professions`/`permitted_hobbies` cache ignores `is_npc`, so `chargen_allow_npc: false` has no effect
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/scenario.cpp:422-462`, `src/scenario.cpp:464-498`, `src/scenario.cpp:281`, `src/scenario.cpp:532-536`, `src/scenario.h:47-51`, `src/newcharacter.cpp:453-454`, `src/newcharacter.cpp:706`, `src/newcharacter.cpp:1892`, `src/newcharacter.cpp:2357`, `src/newcharacter.cpp:3169`, `src/newcharacter.cpp:3181`, `src/npc.cpp:651-653`, `src/game.cpp:1335`

Each scenario stores one mutable cache per list, filled by whichever `is_npc` value calls first and never invalidated. The creator (`recalc_profession_list`, `recalc_hobby_list`, `draw_scenario_details`) and `scenario::check()` always call with `is_npc=false`, including on `scenario::generic()`. Random NPCs (game.cpp:1335, and periodic wanderers) then call `generic()->weighted_random_profession(true)` and `get_scenario()->permitted_hobbies(true)` and get the player lists. About 87 entries in MindOverMatter, Xedra_Evolved and XedraWood set `chargen_allow_npc: false` (psionic knacks, vampire/changeling professions, `hobby_species_lizardfolk`); none of those filters apply to random NPCs. If an NPC call fills the cache first, player-only choices vanish from the creator instead. NPC professions come from `scenario::generic()` while NPC hobbies come from the player's scenario, which is inconsistent.

```cpp
// scenario.cpp:424-426 (no is_npc key)
if( !cached_permitted_professions.empty() ) { return cached_permitted_professions; }
// scenario.cpp:431 (only applied when filling)
( is_npc && !p.chargen_allow_npc() )
// newcharacter.cpp:453-454
const scenario *scenario_from = is_avatar() ? get_scenario() : scenario::generic();
prof = scenario_from->weighted_random_profession( is_npc() );
```

**Fix:** Keep two caches indexed by `is_npc`, or cache the unfiltered list and apply the `chargen_allow_npc` filter per call. Pick one scenario source for NPC professions and hobbies. Add a test that generates an NPC after viewing the avatar profession list.

### PROG-B04 · `set_bionics_from_hobbies` returns on the first duplicate CBM, skipping all remaining hobby CBMs
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/newcharacter.cpp:872-883`, `src/newcharacter.cpp:944-948`, `src/npc.cpp:781`, `src/bionics.cpp:2854-2858`

When a hobby CBM is already installed and does not allow duplicates, the function returns instead of continuing, so every remaining CBM of that hobby and of every later hobby (pointer-ordered set) is skipped. In aftershock_exoplanet every character gets the default `bionic_interface` hobby (bio_cable), which `afs_wraitheon_executive` also grants; `espatier`/`captain_bearman` share `bio_adrenaline` with the 3-point `afs_speed_freak` hobby, so paid-for CBMs can be lost. The profession CBM loop (:944) has no duplicate check and would hit `add_bionic`'s "already installed" debugmsg.

```cpp
// newcharacter.cpp:876-880
if( has_bionic( bio ) && !bio->dupes_allowed ) { return; } else { add_bionic( bio ); }
```

**Fix:** Replace `return` with `continue`. Apply the same duplicate check in the profession CBM loop.

### PROG-B05 · Profession/hobby `recipes` are looked up by result item, not recipe id
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/newcharacter.cpp:957-961`, `src/newcharacter.cpp:850-858`, `src/recipe_dictionary.cpp:111-115`, `src/recipe.cpp:288-311`

Both grant loops call `recipe_dictionary::get_craft( id->result() )`, which finds the recipe whose id equals the result item id. Recipes with `id_suffix`/variants teach the base recipe instead; recipes defined only by `result_eocs` (practice recipes) have no result, so `null_recipe` is "learned". There is no `check_definition` for recipe ids. A scan found 38 mismatches: 36 in MindOverMatter (masked by game-start EOCs that re-teach them) and XedraWood's `wise_one_apprentice`/`wise_one_forest`, which silently never get `learn_hedge_magic_spells_wise`.

```cpp
// newcharacter.cpp:958-960
for( const recipe_id &id : prof->recipes() ) {
    const recipe &r = recipe_dictionary::get_craft( id->result() );
    learn_recipe( &r ); }
```

**Fix:** Use `learn_recipe( &id.obj() )` after `id.is_valid()` in both loops, and validate recipe ids in `profession::check_definition`.

### PROG-B06 · Book proficiency bonuses count prerequisites several times (and recurse forever on cyclic prerequisites)
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/proficiency.cpp:611-642`, `src/proficiency.cpp:656`, `src/item.cpp:3431-3447`, `src/crafting.cpp:756-767`, `src/recipe.cpp:1517`, `src/recipe.cpp:1659`, `data/json/items/book/survival.json:41-44`

`book_proficiency_bonuses::add(bonus, already_included)` recurses through the one-argument `add()`, which starts with a fresh empty set, so de-duplication only covers one level. `operator+=` then calls `add()` again on entries that are already expanded (they keep `include_prereqs=true`). `item::get_book_proficiency_bonuses` expands each book, and `book_bonuses_nearby` does `result += ...`, expanding again; e-readers add another round. Since `fail_factor`/`time_factor` combine entries as a root-sum-square of `ln(1-f)`, duplicates inflate mitigation: for *Through the Lens* (prof_knapping and prof_knapping_speed at 0.15 each), knapping appears 4 times and gives about 0.28 instead of 0.15. This contradicts the "copies should not compound" comment in `book_bonuses_nearby`. The vectors also grow on every `+=`, which `fail_factor`/`time_factor` scan linearly. A modder-made prerequisite cycle recurses forever and overflows the stack (there is no proficiency check pass; see PROG-J02).

```cpp
// proficiency.cpp:623-627 (drops already_included)
if( !already_included.count( prereqs ) ) { already_included.emplace( prereqs ); ... add( inherited_bonus ); }
// proficiency.cpp:636-638 (re-expands)
for( const book_proficiency_bonus &bonus : rhs.bonuses ) { add( bonus ); }
// crafting.cpp:765
result += it.get_book_proficiency_bonuses();
```

**Fix:** Call `add( inherited_bonus, already_included )` and insert `bonus.id` itself into the set. Make `operator+=` append `rhs.bonuses` verbatim, skipping duplicates by (id, source), or store a map from proficiency to best bonus. Return `required_proficiencies` by const reference. Note the separate `add()` calls in item.cpp still duplicate inherited prerequisites, so de-duplicate at that level too.

### PROG-B07 · Profession and hobby addictions of the same type become duplicate entries that are never sated
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/newcharacter.cpp:932-942`, `src/suffer.cpp:2108-2140`, `src/suffer.cpp:425-455`, `src/newcharacter.cpp:3209-3240`

`Character::initialize` `push_back`s every addiction from the profession and each hobby without merging by type. `add_addiction` updates only the first entry of a type, `suffer::from_addictions` runs withdrawal for every entry, and `rem_addiction` erases the first match. Core combinations hit this: Detective + Smoker (nicotine), Office Worker + Caffiend/Caffeine Junkie (caffeine), Heroin Addict + Pillhead (opiate); `hobby_conflict_check` only looks at traits. Office Worker + Caffiend is the worst: the intensity-10 entry is sated while the intensity-50 entry stays in withdrawal.

```cpp
// newcharacter.cpp:933-941
for( const addiction &iter : prof_addictions ) { addictions.push_back( iter ); }
for( const profession *profession : hobbies ) { ... addictions.push_back( iter ); }
```

**Fix:** Merge on insert: if an addiction of the same type exists, keep the maximum (or capped sum) intensity. Add a load-time migration that merges duplicate addiction types in existing saves.

### PROG-B08 · Switching profession keeps hobbies and traits that the new profession excludes
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/newcharacter.cpp:3675-3713`, `src/newcharacter.cpp:3177-3185`, `src/newcharacter.cpp:2171-2185`, `src/newcharacter.cpp:792-796`

On confirming a new profession, the creator only removes the old profession's locked traits and traits conflicting with the new locked traits. It never drops hobbies the new profession's `hobbies`/`whitelist_hobbies` disallow, or traits in its `forbidden_traits`, and nothing re-validates at finish. The disallowed hobby also drops out of the displayed list, so the player cannot deselect it. With MindOverMatter: take `knack_*` hobbies on a non-psionic profession, then switch to a psionic profession (which blacklists knacks). The PSYCHIC_KNACK trait is cancelled with a popup, but the knack hobby, its spells and its point cost stay. `add_default_background` also ignores `allows_hobby`.

```cpp
// newcharacter.cpp:3691-3712 (no allows_hobby / is_forbidden_trait pass)
u.prof = &*selected_profession; ...
for( new_trait : selected_profession->get_locked_traits() ) { if( u.has_conflicting_trait( ... ) ) ... }
u.add_traits(); cc_uistate.recalc_hobbies = true;
```

**Fix:** After a profession change, drop every hobby where `!u.prof->allows_hobby( hobby->ident() )` (reverting its traits as hobby deselection does) and toggle off traits where `u.prof->is_forbidden_trait( t )`, with a popup. Run the same validation before finishing creation.

### PROG-B09 · Skills tab shows bought + max(profession, hobby), but the character gets max(bought + profession, hobby)
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/newcharacter.cpp:171-201`, `src/newcharacter.cpp:836-848`, `src/newcharacter.cpp:918-922`, `src/newcharacter.cpp:1132-1156`

`get_skill_entry_text` adds the hobby level on top of purchased points, but `initialize()` applies hobby skills as a floor. Everyone gets the default background (driving 2; mechanics, cooking, computer, speech, chemistry, electronics, fabrication, first aid 1), so buying 1-2 levels in those skills shows e.g. "Driving (2 + 2)" in green but ends at Driving 2. The points are still charged. The Summary tab (`draw_character_skills`) uses the correct formula, so the two tabs disagree.

```cpp
// newcharacter.cpp:183, :198
prof_skill_level = std::max( prof_skill_level, hobby_skill.second );
string_format( "%s (%d + %d)", ..., my_skill, prof_skill_level )
// newcharacter.cpp:843-844
if( get_skill_level( e.first ) < e.second ) { set_skill_level( e.first, e.second ); }
```

**Fix:** Compute `final = max( my_skill + prof, hobby )` in `get_skill_entry_text` and show the hobby part only when it raises the result, or decide hobby skills stack and change `set_skills_from_hobbies` and `draw_character_skills` (and JSON_INFO.md, see PROG-J04) to match.

### PROG-B10 · EOC direct proficiency set to 100% wipes progress and learns nothing when prerequisites are missing
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/proficiency.cpp:363-384`, `src/proficiency.cpp:386-395`, `src/proficiency.cpp:415-444`, `src/character_proficiency.cpp:33-40`, `src/math_parser_diag.cpp:1477-1514`

`u_proficiency('prof_x', 'format': 'percent', 'direct': true) = 100` goes through `set_time_practiced`, which erases the in-progress entry and then calls non-recursive `learn()`. If a prerequisite is unknown, `learn()` returns silently, so the character neither knows the proficiency nor keeps its progress. NPCs.md describes `direct` as "bypassing other factors". Setting a value below 100% on a known proficiency calls `remove()`, which cascades to every dependent proficiency, undocumented. MoM, Magiclysm and Xedra_Evolved use `'direct': true += 100` heavily; Magiclysm's druid form proficiencies, for example, depend on a prerequisite granted by a separate recurring EOC, so whether they stick may depend on EOC ordering (not verified).

```cpp
// proficiency.cpp:366-375
if( amount >= practicing->time_to_learn() ) {
    for( ... ) { it = learning.erase( it ); }
    learn( practicing ); return; }
```

**Fix:** In the direct path call `learn( practicing, true )`, or keep the learning entry when `learn()` fails and debugmsg the missing prerequisites. Document the cascading removal, or limit it to the proficiency itself when `direct` is set.

### PROG-B11 · Trait-group validation is dead and inverted; profession `npc_background` only checked at runtime
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/trait_group.cpp:168-196`, `src/trait_group.cpp:218-223`, `src/mutation_data.cpp:660-790`, `src/profession.cpp:278`, `src/profession.cpp:737`

`Trait_group::check_consistency` has no caller in src/ or tests/; `mutation_branch::check_consistency` and init.cpp never walk the trait-group map. So invalid trait ids (whose `Single_trait_creator` check is correct but never runs) and nested group ids pass loading and only show up as runtime debugmsgs when the group is rolled. The nested-group check is also inverted: it reports "unknown" when the id **is** valid, so just hooking it up would misfire. In `create()`, `rec.push_back(id)` runs before the validity early-return with no `pop_back`, so the parent's `pop_back` removes the wrong id and later siblings can get a false "recursion" error. A profession's `npc_background` is never validated in `check_definition`; `pick_background` reports it only when such an NPC is generated. Top-level npc_class groups are checked (npc_class.cpp:125).

```cpp
// trait_group.cpp:193-195
if( id.is_valid() ) { debugmsg( "trait group id %s is unknown", id.c_str() ); }
// trait_group.cpp:176-181 (no pop_back)
rec.push_back( id ); if( !id.is_valid() ) { debugmsg( ... ); return result; }
```

**Fix:** Change the check to `!id.is_valid()`, call `check_consistency` for every group from `mutation_branch::check_consistency`, move `rec.push_back` after the validity check (or `pop_back` before returning), and validate `npc_background` in `profession::check_definition`.

### PROG-B12 · `set_skills_from_hobbies(true)` skips every skill, so class-based NPCs never get background skills
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/newcharacter.cpp:836-848`, `src/npc.cpp:776-781`, `src/newcharacter.cpp:2265-2272`, `data/json/profession_groups.json:5`

`npc::randomize` gives class NPCs the default background hobbies and calls `set_skills_from_hobbies(true)`, commented "Only trains skills that are still at 0 at this point". Inside, `if( no_override ) continue;` runs before any check, so nothing is applied, contradicting profession_groups.json ("Also applied to randomized NPCs"). Class NPCs miss driving 2, mechanics 1 and the level-1 background skills. Class skill rolls dominate, so impact is minor.

```cpp
// newcharacter.cpp:839-845
for( const profession::StartingSkill &e : profession->skills() ) {
    if( no_override ) { continue; }
    if( get_skill_level( e.first ) < e.second ) { set_skill_level( e.first, e.second ); } }
```

**Fix:** `if( no_override && get_skill_level( e.first ) > 0 ) { continue; }`.

### PROG-B13 · CHARACTER_CREATION_HARD_LIMIT is checked against stale ratings when the General Info bar is collapsed
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/newcharacter.cpp:2784-2791`, `src/newcharacter.cpp:2882-2885`, `src/newcharacter.cpp:3463-3486`, `src/player_difficulty.cpp:496-533`

Ratings are recomputed only inside `draw_top_bar` (via `difficulty_to_string`), which runs only while the "General Info" header is open. The finish handler reads the cached `get_ratings()`. A player can collapse the header, max out the character, and finish past the hard limit; the confirmation dialog also shows stale numbers, and a player who lowered stats while collapsed can be wrongly blocked. The option defaults to false and only No_Hope enables it.

```cpp
// newcharacter.cpp:2786-2790
if( ( top_bar_is_open = ImGui::CollapsingHeader( ... ) ) ) { draw_top_bar( pc ); }
// :3466
ratings_map = player_difficulty::getInstance().get_ratings();
```

**Fix:** Recompute ratings in the finish handler before `get_ratings()`, or move the `recalc_rating` handling from `draw_top_bar` into the main loop.

### PROG-B14 · Random characters toggle hobby traits, so a second hobby with the same trait removes it
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/newcharacter.cpp:729-742`, `src/newcharacter.cpp:475`, `src/mutation.cpp:296-322`, `data/json/hobbies.json:93`, `data/json/hobbies.json:112`, `data/json/hobbies.json:151`

`add_random_hobby` calls `toggle_trait` for each locked trait without a `has_trait` guard or conflict check (the UI path has both). `toggle_trait` erases an existing trait, so crackhead, tweaker and caffiend (all lock STIMBOOST) cancel each other when two are rolled. Conflicting traits from different hobbies can also be combined, because `hobby_conflict_check` is UI-only. NPCs reach this through `npc::randomize`.

```cpp
// newcharacter.cpp:739-741
for( const trait_and_var &cur : hobby->get_locked_traits() ) { toggle_trait( cur.trait ); }
```

**Fix:** `if( !has_trait( cur.trait ) && !has_conflicting_trait( cur.trait ) ) toggle_trait_deps( cur.trait );`, and skip hobbies that fail `hobby_conflict_check`.

### PROG-B15 · Starting-item substitution inside a container deletes the container and its other contents
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/profession.cpp:1001-1012`, `src/profession.cpp:634-642`

When a top-level item has no substitution rule, `get_substitution` recurses into its contents and returns only the substituted inner items. `profession::items` treats any non-empty result as a replacement for the whole top-level item. A profession giving `{ "item": "backpack", "contents-item": [ "sweater", "flashlight" ] }` to a WOOLALLERGY or VEGAN character ends up with a loose sweatshirt; backpack and flashlight are gone. No current data triggers it.

```cpp
// profession.cpp:1006-1011
if( iter == substitutions.end() ) { for( const item *con : it.all_items_top() ) { ... ret.insert( ... ) } return ret; }
// profession.cpp:636-638
if( !sub.empty() ) { result.insert( result.begin(), sub.begin(), sub.end() ); iter = result.erase( iter ); }
```

**Fix:** Substitute contents in place inside the container's pockets (falling back to the top-level list if a replacement does not fit) and keep the container. Keep the current behavior only for the `in_its_container` wrapper case.

### PROG-B16 · PRED2/3/4 rust cadence is dead code, and the bio_memory power drain never fires
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character_knowledge.cpp:45`, `src/character_knowledge.cpp:255-288`, `src/character_body.cpp:379-381`, `src/calendar.cpp:756-759`, `src/skill.cpp:444-488`, `data/json/bionics.json:942-952`

`do_skill_rust` runs only when `once_every( 24_hours )` is true, and on those turns `once_every` for 8 h, 4 h and 3 h is always true too, so PRED2, PRED3 and PRED4 all give full immunity to combat-skill rust, contrary to the tiered design comment. Separately, `SkillLevel::rust()` returns false on every path, including after rust is applied, so `mod_power_level( -bio_memory->power_trigger )` is unreachable and bio_memory's 25 J `trigger_cost` does nothing. If it were reachable, it would charge anyone with SKILL_RUST_RESIST from any source, since there is no `has_bionic` check, and the cost is tied to one hardcoded bionic id.

```cpp
// character_body.cpp:379-380
if( calendar::once_every( 24_hours ) ) { do_skill_rust(); }
// character_knowledge.cpp:262-264
( has_flag( json_flag_PRED2 ) && calendar::once_every( 8_hours ) ) || ...
// character_knowledge.cpp:285-287
if( skill_level_obj.rust( rust_resist, rust_resist_mult ) ) { mod_power_level( -bio_memory->power_trigger ); }
```

**Fix:** Replace the `once_every` checks with explicit chances (e.g. PRED2 skips rust with probability 1/3, PRED4 always), or move the mechanic to SKILL_RUST_RESIST enchantments on the traits (PROG-J08). Have `rust()` return true when rust was resisted and charge power only for characters with the bionic, ideally via a generic hook such as a `skill_rust_resisted { character, skill, amount }` event or a bionic field `"power_per_rust_resisted"`. Otherwise delete the dead drain.

### PROG-B17 · Deselecting a hobby removes a trait the scenario forces (or that the player picked separately)
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/newcharacter.cpp:3733-3752`, `src/newcharacter.cpp:2171-2185`, `data/json/scenarios.json`

On hobby deselection, its locked traits are toggled off unless the profession or another hobby also locks them. Scenario forced traits are not counted, and nothing calls `add_traits` afterwards. Core data triggers it: the medieval scenario forces SPIRITUAL and STRONGSTOMACH, and the meditation and gourmand_alt hobbies lock them, so select-then-deselect strips a scenario-forced trait that the traits tab refuses to remove. Xedra, Aftershock ESPER and MoM PSYCHIC_KNACK overlap the same way.

```cpp
// newcharacter.cpp:3742-3751
int from_other_hobbies = u.prof->is_locked_trait( trait ) ? 1 : 0;
for( hby : u.hobbies ) { ... }
if( from_other_hobbies > 0 ) continue;
u.toggle_trait_deps( trait );
```

**Fix:** Also count `get_scenario()->is_locked_trait( trait )`. Ideally record which traits the hobby actually added and remove only those.

### PROG-B18 · Starting items that can't be wielded, worn or stowed are silently thrown away
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/newcharacter.cpp:644-693`, `src/character_inventory.cpp:2958-3000`, `src/character_inventory.cpp:1508-1586`, `tests/new_character_test.cpp:167-185`

`add_profession_items` ignores the return value of `wield()`, which can fail in `can_wield` even with no wield conflict (one arm or RESTRICT_HANDS with a two-handed item, INTEGRATED, NO_UNWIELD), and builds a non-null `item_location` to the temporary list element, so a failed wield counts as success and the item vanishes. After the second placement pass only a single leftover is retried; others are dropped with no message. The test meant to catch overflow counts the same inventory twice and can never fail.

```cpp
// newcharacter.cpp:649-652
if( !has_wield_conflicts( it ) ) { wield( it ); wield_or_wear = &it; success = item_location( *this, wield_or_wear ); }
// newcharacter.cpp:684-692: only prof_items.size() == 1 is retried
```

**Fix:** Check the return values of `wield()` and `wear_item()`. Spawn items that still fail at the start position after `place_player`, or at least message the player. Fix the test to compare against the expected item list.

### PROG-B19 · `weighted_random_profession` loops forever if no permitted profession passes `can_pick()`
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/scenario.cpp:532-548`, `src/scenario.cpp:422-458`, `src/profession.cpp:769-800`, `src/newcharacter.cpp:452-454`

The function rerolls until a candidate passes `can_pick()`, but `permitted_professions` never filters by achievement locks, and its fallback to `generic` only applies when the list is empty. A modded scenario whose whitelist contains only locked professions (META_PROGRESS on, the default, or any with `hard_requirement`) hangs `Character::randomize` ("Play Now", or Random with Keep Scenario). No shipped scenario does this.

```cpp
// scenario.cpp:541-546
while( true ) {
    const string_id<profession> &candidate = random_entry_ref( choices );
    if( candidate->can_pick().success() && x_in_y( 2, 2 + std::abs( candidate->point_cost() ) ) ) {
        return &candidate.obj(); } }
```

**Fix:** Filter `choices` by `can_pick()` first; if none remain, debugmsg and return `profession::generic()`. Alternatively cap the iterations.

## 3. Performance

### PROG-P01 · `Character::randomize` can burn ~10,000 no-op iterations, each recomputing every point pool twice
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/newcharacter.cpp:547`, `src/newcharacter.cpp:597`, `src/newcharacter.cpp:302`, `src/newcharacter.cpp:309`, `src/newcharacter.cpp:276`, `src/newcharacter.cpp:253`, `src/newcharacter.cpp:234`

The second randomize loop only lets the skill branch spend once `loops > 10000`. When stat and trait points are spent but pure skill points remain, every iteration up to 10000 changes nothing, yet runs `has_unspent_points()` and `multi_pool()`, i.e. two full pool evaluations: 6 `get_option` calls with heap-allocated keys, `get_mutations(true)` plus locked-trait checks against scenario, profession and hobbies, and ~27 `get_skill_level` calls. A simulation with real cost data hits this path in about 23% of randomizations (~2300 wasted iterations on average), an occasional ~100 ms hitch. Callers are cold: random NPC spawns, camp recruits, game start, reroll keypresses.

```cpp
// newcharacter.cpp:547-550
for( int loops = 0; has_unspent_points( *this ) && loops <= 100000; loops++ ) { multi_pool p( *this );
// newcharacter.cpp:597
if( level < p.skill_points_left && level < MAX_SKILL && loops > 10000 ) {
```

**Fix:** Let the skill branch run when `loops > 10000 || ( !allow_stats && !allow_traits )`; the final state is identical. Compute `multi_pool` once per iteration and use `p.skill_points_left > 0` as the loop condition. Read pool options and MAX_TRAIT_POINTS once before the loops. Optionally keep a running trait-point total.

### PROG-P02 · `get_functioning_mutations()`/`get_mutations()` allocate a fresh vector on every call, including per monster per turn
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/newcharacter.cpp:2107`, `src/newcharacter.cpp:2072`, `src/monster.cpp:1792`, `src/monster.cpp:1881`, `src/npcmove.cpp:1021`, `src/cata_tiles.cpp:4677`, `src/character_armor.cpp:59`, `src/character.cpp:2272`

Both accessors walk the mutation map, do `obj()` and a `std::function` filter per trait, and return a new `std::vector<trait_id>`; `get_mutations()` also de-duplicates with an O(N*M) search. Callers almost always just iterate. `monster::attitude()` calls it up to twice and is reached each monster move (monmove.cpp:578, 1123) and from `npc::assess_danger` for every monster in LOS per NPC turn, so a busy bubble with NPCs does thousands of allocations per turn. Other callers: `cata_tiles::draw_entity_with_overlays` per drawn character per frame, `mutation_armor` per body part per damage instance, `process_turn` twice. Each call is sub-µs.

```cpp
// newcharacter.cpp:2110-2123
std::vector<trait_id> result; const auto &test = ignore_enchantments ? my_mutations : cached_mutations;
result.reserve( test.size() ); for( ... : test ) { ... result.push_back( t.first ); }
// monster.cpp:1792
for( const trait_id &mut : u->get_functioning_mutations() ) {
```

**Fix:** Keep a member `functioning_mutations_cache`, rebuilt where `trait_flag_cache` is already cleared (set/unset_mutation, `recalculate_enchantment_cache`, `mutation_loss_effect`), and return it as `const std::vector<trait_id>&` from a no-filter overload. In `monster::attitude`, cache per-character "has any anger_relations/ignored_by" bits.

### PROG-P03 · `has/exceeds_recipe_requirements` copy the whole skill map and build a temporary `item()` per call
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/newcharacter.cpp:923`, `src/character_knowledge.cpp:149`, `src/skill.cpp:634`, `src/skill.cpp:650`, `src/skill.cpp:671`, `src/character_crafting.cpp:59`, `src/crafting.cpp:213`, `src/recipe.cpp:93`, `src/character.h:2599`

Both functions call `get_all_skills()`, which copies the full `SkillLevelMap` with enchantment adjustments to read one or two skills, and the one-argument `SkillLevelMap` overloads construct a default `item()`. `compare_skill_requirements` builds a `std::map` that `exceeds_recipe_requirements` only takes the minimum of. The main caller is `Character::initialize`'s loop over ~5,600 recipes (once per avatar or null-class NPC, about 10-20 ms); others are the dark-crafting penalty (1-2 calls per craft turn), practice-recipe `get_difficulty`, and the creator (PROG-P06).

```cpp
// character_crafting.cpp:61
return get_all_skills().has_recipe_requirements( rec );
// skill.cpp:636
return meets_skill_requirements( req, item() );
```

**Fix:** Hoist `const SkillLevelMap skills = get_all_skills();` out of the `initialize` loop. Use a `static const item null_item;` (or context-free implementations) in the one-argument overloads, and rewrite `exceeds_recipe_requirements` as a direct min-loop. Add `Character::get_enchanted_skill_level( id )` for `recipe::get_difficulty` and crafting_gui_helpers.cpp:581.

### PROG-P04 · `knows_recipe()` re-validates the autolearn cache with O(skills) lookups on every call
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/skill.cpp:699`, `src/character_crafting.cpp:82`, `src/character_crafting.cpp:31`, `src/newcharacter.cpp:925`, `src/skill.cpp:547`

Every `knows_recipe()` goes through `get_learned_recipes()`, which runs `has_same_levels_as`: for each of ~27 skills, `count()` plus `get_skill_level_object()` (another find, a linear `obj()` scan and a `std::set<std::string>` lookup). The only per-recipe caller is `Character::initialize` (~150k lookups, ~10 ms, once); other callers are single or per-book calls.

```cpp
// skill.cpp:704-712
for( const auto &entry : *this ) { ... if( other.count( entry.first ) == 0 ) { ... }
    const SkillLevel &other_level = other.get_skill_level_object( entry.first );
```

**Fix:** Compare the two ordered maps in lockstep (`std::equal` with a predicate on key, `level()` and `knowledgeLevel()`), or keep a version counter bumped by the `SkillLevel` setters and compare one integer.

### PROG-P05 · Skill lookups do a linear `obj()` scan plus a `std::set<string>` contextual check on every read
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/skill.cpp:57`, `src/skill.cpp:325`, `src/skill.cpp:547`, `src/skill.cpp:565`, `src/character_knowledge.cpp:168`, `src/skill.cpp:594`, `src/item.cpp:5171`, `src/character.cpp:2542`

`string_id<Skill>::obj()` scans the `Skill::skills` vector linearly. Both `get_skill_level_object` overloads call `is_contextual_skill()` (that scan plus a `set<string>` count) before the map find. `Character::get_skill_level` reads the same `SkillLevel` twice, and the item-context overload calls `contextualize_skill` (another scan). `practice()` does about 14 lookups. Each read costs ~100 ns instead of ~30 ns; called from ~318 sites, so a cheap cleanup with minor impact (there is a TODO at skill.cpp:46).

```cpp
// skill.cpp:59-63
for( const Skill &skill : Skill::skills ) { if( skill.ident() == *this ) { return skill; } }
// skill.cpp:327-328
static const std::string contextual_skill( "contextual_skill" ); return _tags.count( contextual_skill ) > 0;
```

**Fix:** Cache `_contextual` and `_combat` bools at `load_skill`. Make `obj()` O(1) via generic_factory (PROG-J07) or an index map. In the const getter, do the map find first and only check contextuality on a miss. Have `get_skill_level` fetch the `SkillLevel` once.

### PROG-P06 · Character creator redraws at ~30 Hz and rescans every recipe per frame on the Skills tab
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/newcharacter.cpp:2934`, `src/newcharacter.cpp:3068`, `src/newcharacter.cpp:1788`, `src/newcharacter.cpp:1121`, `src/newcharacter.cpp:1232`, `src/ui_manager.cpp:482`

The creator redraws every loop with a 33 ms input timeout (ImGui adaptors always redraw). `draw_skill_details` copies the skill map, walks ~5,600 recipes with string-temp `has_flag("SECRET")` checks, calls the `item()`-constructing `meets_skill_requirements` for ~2,549 autolearn recipes, then sorts and formats a long string, a few ms per idle frame. Also per frame: `draw_character_skills` sorts with a comparator that translates names and does skill lookups, `draw_stat_details` calls `recalc_hp()`/`set_stored_kcal()`, and traits are re-sorted. No visible lag; wasted CPU on a menu.

```cpp
// newcharacter.cpp:1797, :1804-1814
SkillLevelMap with_prof_skills = u.get_all_skills();
for( const auto &e : recipe_dict ) { ... r.has_flag( "SECRET" ) ... meets_skill_requirements( r.autolearn_requirements );
// newcharacter.cpp:1233-1234
u.recalc_hp(); u.set_stored_kcal( u.get_healthy_kcal() );
```

**Fix:** Cache the recipe text in `cc_uistate` and rebuild it in `confirm()`, `mod_skill()` and on selection change (reuse the existing `recalc_*` dirty flags). Do the same for the summary skill and trait lists, and move `recalc_hp`/`set_stored_kcal` into `mod_stat_base()`.

### PROG-P07 · `profession_sorter`/`scenario_sorter` redo `generic()` and achievement checks in every comparison
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/profession.cpp:842`, `src/profession.cpp:385`, `src/profession.cpp:769`, `src/scenario.cpp:172`, `src/scenario.cpp:709`, `src/newcharacter.cpp:1891`, `src/newcharacter.cpp:2926`, `src/newcharacter.cpp:3166`, `src/newcharacter.cpp:3178`

Each `profession_sorter` comparison calls `profession::generic()` (string option lookup plus id interning) and 2-4 `can_pick()`, which with META_PROGRESS on builds colorized, translated failure messages for professions with requirements. `scenario_sorter` calls `scenario::generic()`, which rebuilds a vector of all scenarios per call (O(N² log N) but only 62 scenarios). `draw_scenario_details` copies `permitted_professions()` and runs `min_element` under this sorter every frame at ~30 Hz, roughly 1 ms per frame, more with mods.

```cpp
// profession.cpp:846-858
const profession *gen = profession::generic(); ...
if( !a->can_pick().success() && b->can_pick().success() ) ...
// newcharacter.cpp:1891-1893
const auto default_prof = *std::min_element( ..., psorter );
```

**Fix:** Decorate-sort-undecorate: compute `{is_generic, can_pick, cost/name}` once per element and resolve `generic()` once. Cache the scenario's default profession. Make `scenario::generic()` a direct lookup plus blacklist check. Add a message-free `can_pick_fast()` for sorting and enabling entries.

### PROG-P08 · `player_difficulty` recomputes the constant 'average NPC' and rolls profession items 4x per keypress
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/player_difficulty.cpp:501`, `src/player_difficulty.cpp:244`, `src/player_difficulty.cpp:312`, `src/player_difficulty.cpp:343`, `src/player_difficulty.cpp:415`, `src/player_difficulty.cpp:475`, `src/newcharacter.cpp:3636`, `src/newcharacter.cpp:3878`, `src/newcharacter.cpp:2882`

Every handled creator action, including scroll, toggle-top-bar and each +/- key repeat, sets `recalc_rating`. `difficulty_to_string` then builds a new npc (`reset_npc`, `npc_from_avatar`, `initialize`), recomputes every metric for the never-changing `average` NPC, generates the profession item list 4 times (armor and DPS for both characters), and evaluates 12 test weapons. The same actions clear `cached_profession_inventory`, forcing another `items()` roll on the Professions tab. Limited to one recompute per frame, in the creator only.

```cpp
// player_difficulty.cpp:504-512
npc n = npc(); reset_npc( n ); npc_from_avatar( u, n ); ...
// player_difficulty.cpp:353
double npc_dps = calc_dps_value( average ) * average.get_speed();
// newcharacter.cpp:3636-3637
cc_uistate.recalc_rating = true; cc_uistate.cached_profession_inventory.clear();
```

**Fix:** Compute the average NPC's metrics once and store them. Generate the profession item list once per rating and share it (or reuse `cached_profession_inventory`). Set `recalc_rating` and clear the inventory cache only for actions that change the character.

### PROG-P09 · `rate_location` flood fill re-pushes visited tiles, overcounting area by up to ~4x
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/start_location.cpp:438`, `src/start_location.cpp:456`, `src/start_location.cpp:452`, `src/start_location.cpp:432`, `src/start_location.cpp:425`, `src/start_location.cpp:538-546`

`maybe_add` checks `checked[] >= attempt`, but `checked` is written on pop, not push, so a tile can be pushed by each neighbour before it is popped. Each duplicate pop increments `area` and repeats 8 neighbour probes. A simulation shows open rooms overcounted 3-4x (20x20 → 1483 instead of 400) while 1-wide corridors are exact, so the shape-dependent bias skews which start spot `place_player` chooses. Each flood also reserves a full-map tripoint vector (~209 KB). This runs only at game start, and most floods return early at a door or outdoors.

```cpp
// start_location.cpp:438 vs :456
if( checked[add_p.x()][add_p.y()] >= attempt ) { return; }
checked[cur.x()][cur.y()] = attempt;   // set on pop
```

**Fix:** Set `checked[pt] = attempt` at push time in `maybe_add` (and for the seed before the loop). Drop the full-map reserve, or reuse one vector across calls.

## Appendix: considered and rejected

- Proficiency/skill practice: 4 option string lookups and a `std::set` copy per practice call – real, but runs once per turn or attack for a handful of proficiencies; microseconds against a multi-ms turn, so negligible.
- Trait tab rebuild copies profession traits per trait and translates names per sort comparison – only runs on scenario/profession/hobby change or trait confirm, sorting ~192 traits; a few ms on a discrete UI event.
- Read menu re-derives per-reader state (light, glasses, helpers) for every book – cell text and denial are cached per entry per menu build, and the expensive paths short-circuit for normal readers; tens of µs per book once per menu open.
- Copies of entire profession/scenario vectors just to collect ids – `permitted_hobbies` returns its cache early and `scen_blacklist::finalize` runs once per data load over ~60-150 scenarios; not hot. (The real issue with that cache is PROG-B03.)
