# 02 · Math expressions & the talker interface

> Engine audit – static code reading at commit `a1a646d6`; nothing was compiled or run. Each finding was checked against the code by a separate reviewer: **confirmed** = verified by reading the code; **plausible** = consistent with the code, but reachability or impact not proven. Line numbers refer to that commit.

**Scope:** `src/math_parser.cpp`, `src/math_parser_diag.cpp`, `src/math_parser_diag_value.cpp`, `src/math_parser_jmath.cpp`, `src/math_parser_lex.cpp`, `src/talker_avatar.cpp`, `src/talker_character.cpp`, `src/talker_furniture.cpp`, `src/talker_item.cpp`, `src/talker_monster.cpp`, `src/talker_npc.cpp`, `src/talker_topic.cpp`, `src/talker_vehicle.cpp`, `src/talker_zone.cpp`, `src/talker.h`, `src/math_parser_func.h`, `src/math_parser_impl.h`, `src/math_parser_type.h`

## Summary

The math language and the talker layer cover characters well, but their semantics and coverage are uneven. Three themes stand out. First, assignment is inconsistent: several "assignable" functions add instead of set, ignore their read kwargs, or quietly do nothing on some talker types, and vanilla content already depends on some of this. Second, there is drift between `doc/JSON/NPCs.md` and the function registry. Third, the non-character talkers (monster, item, vehicle, zone, "furniture") are thin, and some of their accessors report wrong data. One crash is reachable in vanilla play (`limb_score()` without `type`). The performance issues are steady per-evaluation allocation overhead, not hot spots.

| Lens | Critical | High | Medium | Low | Total |
|---|---|---|---|---|---|
| Missing JSON / EOC | 0 | 0 | 11 | 4 | 15 |
| Bugs & quirks | 1 | 1 | 10 | 7 | 19 |
| Performance | 0 | 0 | 3 | 6 | 9 |
| **Total** | **1** | **1** | **24** | **17** | **43** |

**Fix first:**
1. **MATH-B01** · `limb_score()` without `type` crashes the game. Vanilla speed serum content triggers it, and the fix is a few lines.
2. **MATH-B02** · Assignable functions add instead of set. Vanilla addiction and pain EOCs already misbehave, and each setter needs only a small fix. The core EOCs that rely on the current behaviour have to be migrated at the same time.
3. **MATH-B03** · `faction_*()` dereferences a null pointer for unknown ids. Any content typo crashes the game, and one helper fixes all 13 call sites.
4. **MATH-J01** · Documented math features are rejected or ignored. Six small mismatches make the official docs break mods, and an example-parsing test would stop them coming back.
5. **MATH-B12** · `weather('windpower')` assignment has no gameplay effect. Vanilla scarlet storm content silently does nothing.

## 1. Missing JSON bindings & EOC features

### MATH-J01 · Documented math features are rejected by the parser or silently ignored
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/math_parser_diag.cpp:846`, `src/math_parser_diag.cpp:1886`, `src/math_parser_diag.cpp:1897`, `src/math_parser_diag.cpp:1902`, `src/math_parser_diag.cpp:1947`, `src/math_parser_diag.cpp:1196`, `src/math_parser.cpp:218`, `src/math_parser.cpp:831`, `src/math_parser.cpp:973`, `src/condition.cpp:2366`, `src/condition.cpp:2472`, `src/condition.cpp:2534`, `src/talker_character.cpp:508`, `doc/JSON/NPCs.md:1412`, `doc/JSON/NPCs.md:1420`, `doc/JSON/NPCs.md:1426`, `doc/JSON/NPCs.md:1459`, `doc/JSON/NPCs.md:1464`, `doc/JSON/NPCs.md:1473`

A modder who follows `doc/JSON/NPCs.md` gets parse errors, runtime errors or wrong results in six places:

1. `characters_nearby('allow_hallucinations': 1)` is documented, and the eval function reads it, but the kwarg is missing from the allowed list, so parsing fails with "Unused kwarg". MATH-B07 covers this together with the radius bug.
2. `effect_duration()` and `spell_difficulty()` are documented as assignable, but neither has an assign function, so the parser rejects assignments to them.
3. The `val()` example `u_val('strength') = 2` throws "Invalid aspect". The setter map has only `*_base` and `*_bonus`.
4. `val('weight')` and `val('volume')` are documented but are missing from `f_get_vals`.
5. `spell_level_sum('level': N)` ignores `level` unless `school` is also given. The doc example also passes `'level': '10'` as a string, which throws a type mismatch.
6. `energy()` is documented with u/n scopes but is registered as global-only, so `u_energy(...)` is a syntax error. This one is only a wrong doc column, because `energy()` ignores the actor anyway.

Most of these fail loudly rather than silently.

```cpp
// math_parser_diag.cpp:1886
{ "characters_nearby", { "ung", 0, characters_nearby_eval, {}, { "radius", "attitude", "location" } } },
// math_parser_diag.cpp:1897 – no assign function
{ "effect_duration", { "un", 1, effect_duration_eval, {}, ...
// talker_character.cpp:508 – level filter only applies inside the school branch
if( school.is_null() || ( sp->spell_class() == school && sp->get_effective_level() >= min_level ) )
```

**Fix:** Add `"allow_hallucinations"` to the kwargs list. Implement `effect_duration_ass` and `effect_intensity_ass` through `Creature::get_effect(...).set_duration()/set_intensity()`, keeping the `bodypart` kwarg. Either implement spell_difficulty assignment or mark it ❌ in the docs. Add `strength`/`dexterity`/`intelligence`/`perception` setters mapped to `set_*_base`, or fix the docs and the example. Add `weight`/`volume` to `f_get_vals`, or point the docs at `weight()`/`volume()`. Change the condition in `get_spell_sum` to `( school.is_null() || sp->spell_class() == school ) && sp->get_effective_level() >= min_level`. Then add a test that parses every example string in the NPCs.md function table.

### MATH-J02 · NPC personality has no EOC read or write access
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/talker_npc.cpp:255`, `src/npc.h:220`, `src/npctalk.cpp:9597`, `src/npctalk_funcs.cpp:1088`, `doc/JSON/NPCs.md:795`

NPC personality (aggression, bravery, collector, altruism) drives NPC attitude and patience. JSON can set it only at spawn, through npc_class or npc definitions, and can read it only as an integer modifier inside a dialogue trial `mod` list. No condition or math function exposes it. Modders cannot write "brave NPCs accept dangerous missions" as a condition, and cannot lower bravery after a traumatic event.

```cpp
// talker_npc.cpp:255 – the only consumer
} else if( attribute == "AGGRESSION" ) { modifier = me_npc->personality.aggression; } ...
```

**Fix:** Add talker virtuals `get_personality(std::string_view)` and `set_personality`, and a math function `personality('bravery')` with eval and assign, scopes `"un"`, clamped to -10..10. For example `{ "math": [ "n_personality('bravery') -= 2" ] }` and `"condition": { "math": [ "n_personality('altruism') > 3" ] }`.

### MATH-J03 · Talk trials: fixed int difficulty, closed string 'mod' list, hardcoded social formulas
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/npctalk.cpp:3215`, `src/npctalk.cpp:3228`, `src/npctalk.cpp:2229`, `src/talker_npc.cpp:241`, `src/talker_npc.cpp:242`, `src/talker_npc.cpp:284`, `src/talker_avatar.cpp:41`, `src/talker_avatar.cpp:58`

Only small parts of LIE/PERSUADE/INTIMIDATE checks are data-driven:

- `difficulty` is read as a plain int.
- `mod` entries are `[string, int]` pairs resolved by if/else chains, with a substring hack for `npc_has_trait: X`.
- The base formulas are C++ constants: lie = -lie_skill + trust\*3, intimidate = -intimidation + fear\*2 - bravery\*2, OWED_VAL = 1000.

A mod cannot add a trial type, scale difficulty with a variable, or use a math function as a modifier. Only talker_avatar and talker_npc implement `parse_mod`/`trial_chance_mod`, so trials with any other alpha talker silently get 0. Partial workarounds exist: CONDITION trials (no % shown and no speech practice), SOCIAL_* enchantments, and mutation social_mods.

```cpp
difficulty = jo.get_int( "difficulty" );                       // npctalk.cpp:3215
this_modifier.first = jmod.next_string(); this_modifier.second = jmod.next_int();
} else if( attribute.find( "npc_has_trait" ) != std::string::npos ) { // shim/hack
```

**Fix:** Load `difficulty` as `dbl_or_var`. Let `mod` accept `{ "math": [ "..." ] }` entries alongside the legacy strings. Move the base chances into a new type: `{ "type": "talk_trial_type", "id": "LIE", "chance": { "math": [ "u_val('lie_skill') - n_npc_trust()*3" ] }, "practice_skill": "speech" }`.

### MATH-J04 · Dialogue-function id arguments are never validated; typos silently return 0/-1
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/math_parser_diag.h:30`, `src/math_parser.cpp:393`, `src/math_parser_diag.cpp:497`, `src/math_parser_diag.cpp:334`, `src/math_parser_diag.cpp:1146`, `src/mutation.cpp:135`, `src/skill.cpp:57`, `src/effect_on_condition.cpp:549`

Id-taking math functions (has_trait, skill, effect_intensity, vitamin, proficiency, armor, coverage and others) build their `string_id` at evaluation time with no validity check. A typo such as `u_has_trait('FEEBEL')` loads cleanly and evaluates to false/0/-1 forever. `dialogue_func` carries no argument type information, so nothing can validate literal arguments at load time. `effect_on_condition::check()` is empty. Only event_statistic, spell_difficulty and spell_exp_for_level call `is_valid()`, and only at runtime.

```cpp
return d.const_actor( is_beta( scope ) )->has_trait( trait_id( params[0].str( d ) ) ); // :500
return target.is_null() ? -1 : target.get_intensity();                                 // :341
```

**Fix:** Add a per-parameter type descriptor, for example `{ "has_trait", { "un", 1, has_trait_eval, {}, {}, { arg::trait_id } } }`. In func_diag's constructor, where `_update_diag_args<false>` already resolves static strings, record (id_type, literal) pairs. Validate them in a deferred pass after all JSON has loaded and report `math: unknown trait_id "FEEBEL" in EOC_foo`.

### MATH-J05 · Character state that EOCs can write but not read, or read but not write
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/npctalk.cpp:5703`, `src/math_parser_diag.cpp:1811`, `src/math_parser_diag.cpp:1885`, `src/math_parser_diag.cpp:1896`, `src/math_parser_diag.cpp:1897`, `src/math_parser_diag.cpp:594`, `src/condition.cpp:546`, `src/condition.cpp:2597`, `src/talker.h:481`, `src/talker.h:910`, `doc/JSON/NPCs.md:1420`, `doc/JSON/NPCs.md:1448`

Several Character properties have only half an interface:

- Wetness can be added (`u_add_wet`) but never read.
- `warmth()`, `speed()`, `effect_intensity()` and `effect_duration()` are read-only.
- The current activity is exposed only as a bool. `{"u_has_activity": "ACT_X"}` is accepted, but the id is ignored, so it is true for any activity.
- `morale()` assignment is a no-op for characters. The docs say it is monster-only.

An exact effect duration can be set today with `u_lose_effect` followed by `u_add_effect`, but this is clumsy.

```cpp
// condition.cpp:546 – the string argument is discarded
conditional_t::func f_has_activity( const JsonObject &, std::string_view, bool is_npc ) { return f_has_activity( is_npc ); }
```

**Fix:** Add `wetness('bp')` (read and assign), assign variants for `warmth('bp')`, `speed_bonus()`, `effect_intensity` and `effect_duration` (with the `bodypart` kwarg), and a string mutator `{ "mutator": "u_activity" }`. Make `{ "u_has_activity": "ACT_READ" }` compare the id. Either implement character morale assignment or reject `u_morale() = x` at parse time for character scopes.

### MATH-J06 · Monster talker: faction, hallucination and attack cooldowns unreadable; some writes no-op
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/talker_monster.h:84`, `src/talker.h:904`, `src/talker.h:906`, `src/math_parser_diag.cpp:634`, `src/math_parser_diag.cpp:1078`, `src/math_parser_diag.cpp:914`, `src/math_parser_diag.cpp:932`, `src/math_parser_diag.cpp:1886`, `src/math_parser_diag.cpp:1928`, `src/monster.h:567`, `src/monster.h:589`, `src/monster.h:636`

No EOC can read or write a monster's instance faction, hallucination flag, special-attack cooldowns, upgrade or reproduction timers, nickname, or ammo. The `mon_faction` mutator reads the mtype default, not the instance faction.

Some generic writes silently do nothing on monsters. `n_hp('torso') = 10` calls `set_part_hp_cur` and raw `n_pain() = 0` calls `set_pain`, and talker_monster overrides neither (`n_hp('ALL')` works).

`monsters_nearby()` counts hallucinations, unlike `characters_nearby()`, and has no kwarg to change that. Its `'hostile'` filter is just `friendly == 0`, so it also counts neutral wildlife.

```cpp
d.actor( is_beta( scope ) )->set_part_hp_cur( bodypart_id( bp_str ), val ); // :635 -> base no-op
return mon->friendly == 0;                                                  // :923 'hostile'
```

**Fix:** In talker_monster, override `set_part_hp_cur` and `set_pain`. Add `mon_attack_cooldown('bite')` (read and assign), `is_hallucination()`, a string mutator `npc_mon_faction` and an effect `npc_set_mon_faction: "zombie"`. Give `monsters_nearby` an `allow_hallucinations` kwarg defaulting to false. Base `'hostile'` on `attitude_to(player) == HOSTILE`.

### MATH-J07 · Item talker: faults write-only, count read-only, power hardcoded to 'battery'
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/talker_item.cpp:18`, `src/talker_item.cpp:100`, `src/talker_item.cpp:105`, `src/talker_item.cpp:119`, `src/talker_item.cpp:129`, `src/talker_item.cpp:159`, `src/npctalk.cpp:6282`, `src/condition.cpp:2367`

Item EOCs can set a fault (`u_set_fault`), but nothing can read or remove one. `val('count')` can be read but not written. There are no accessors for temperature, rot, contents or loaded ammo.

The power accessors disagree. `power_cur` counts any loaded ammo, while `power_max` and `set_power_cur` hardcode the `battery` ammotype and itype, so on a gun `n_val('power')` returns the round count and power_max returns 0. Item encumbrance is always computed against the avatar's body, not the holder's.

```cpp
return 1_mJ * me_it_const->get_item()->ammo_remaining( );  // :102 power_cur, any ammo
... ammo_capacity( ammo_battery )                            // :107 power_max
get_encumber( get_player_character(), id )                   // :131
```

**Fix:** Add a `has_fault('fault_id')` math function and condition, and an `npc_remove_fault` effect. Make `val('count')` assignable. Add `item_temp()`, `rot('format':'relative')`, `ammo_count('ammo_id')` and `ammo_type()`. Derive power from the item's own `ammo_types()`/magazine instead of hardcoded ids. Pass the actual holder to `get_encumber`.

### MATH-J08 · Zone and 'furniture' talkers are stubs
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/talker_zone.cpp:5`, `src/talker_zone.h:38`, `src/npctalk.cpp:7016`, `src/npctalk.cpp:7033`, `src/npctalk.cpp:6978`, `src/computer.cpp:451`, `src/condition.cpp:1300`, `src/talker_furniture.cpp:42`, `src/talker.h:115`, `src/iexamine_actors.cpp:296`

`talker_zone` implements only `pos_abs`. EOCs run through `u_run_fixed_zone_eocs` cannot read a zone's name, type, extent or variables, and receive every non-personal, non-vehicle zone in range with no type filter. `zone_range` (and `vehicle_range` at `npctalk.cpp:6978`) is read only as a literal int, although the docs promise a variable object. There is no `u_is_zone` condition.

The "furniture" talker actually wraps `computer`, so real furniture and terrain have no talker. examine_action EOCs get a null beta and only the `this` and `pos` context vars, and `this` is always the furniture id, even for terrain. `u_is_furniture` really tests for a computer. The wrong `pos_abs_omt` on these talkers is covered in MATH-B09.

```cpp
if( jo.has_int( "zone_range" ) ) { zone_range = jo.get_int( "zone_range" ); } // npctalk.cpp:7016
return std::make_unique<talker_furniture>( &me );                              // computer.cpp:453
```

**Fix:** In talker_zone, add `get_name`, a zone-type accessor, a var map on `zone_data` with `maybe_get_value`/`set_value`, and an end-point mutator. Give `u_run_fixed_zone_eocs` a `zone_types: [...]` filter, read `zone_range`/`vehicle_range` as `dbl_or_var`, and add a `u_is_zone` condition. Introduce a real furniture/terrain talker keyed by tripoint and pass it as beta to examine_action EOCs. Rename the current talker to `talker_computer` and add a `u_is_computer` condition.

### MATH-J09 · Vehicle talker lacks fuel, battery and HP; weight() rejects vehicles; avatar-only checks
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/talker_vehicle.h:34`, `src/math_parser_diag.cpp:1744`, `src/talker_vehicle.cpp:94`, `src/talker_vehicle.cpp:104`, `src/talker_vehicle.cpp:175`, `src/vehicle.h:1574`, `src/vehicle.h:1672`, `src/condition.cpp:2412`

Vehicle EOCs cannot read fuel, battery charge, structural or part HP, engine state or cruise velocity, even though `vehicle::fuel_left` and `battery_left` exist. They can write little beyond vars and effects. `n_weight()` throws "talker is not character nor item" for vehicles, although `talker_vehicle` implements `get_weight`, and `val()` offers only `unloaded_weight`, so a vehicle's loaded weight cannot be read at all.

`get_weight` also returns int milligrams, which overflows above roughly 2147 kg. `is_driven` and `is_remote_controlled` check only the avatar. That has little impact today, because NPCs never take the controls.

```cpp
throw math::runtime_error( "For weight(), talker is not character nor item" ); // math_parser_diag.cpp:1755
//When/if npcs can drive this will need updating
return me_veh_const->player_in_control( get_map(), get_avatar() );           // talker_vehicle.cpp:107
```

**Fix:** In talker_vehicle, override `power_cur`/`power_max` (battery), `charges_of` (fuel_left) and `get_cur_hp`/`get_hp_max`. Add `fuel('gasoline')`, `vehicle_hp('part_id')` and an assignable `cruise_speed()`. Make `weight_eval` fall through to `get_weight()` for any talker, and widen the return type to avoid the overflow. Check `player_in_control` for any Character.

### MATH-J10 · Forced dialogue topics hardcoded to specific traits, items and effects
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/talker_npc.cpp:115`, `src/talker_npc.cpp:160`, `src/talker_npc.cpp:195`, `src/talker_npc.cpp:208`, `src/talker_avatar.cpp:30`, `src/npctalk.cpp:1728`, `src/npctalk.cpp:2189`, `src/npctalk.cpp:1674`

`talker_npc::get_topics` pushes topics based on hardcoded ids:

- PROF_CHURL (with an INT >= 9 check) gives the TALK_CHURL variants.
- PROF_FOODP plus foodperson_mask gives TALK_NPC_NOFACE.
- npc_suspend gives TALK_REBOOT.
- Sleep or narcosis gives TALK_SEDATED.
- deaf/mute give TALK_DEAF/TALK_MUTE.

The dynamic lines for these topics are hardcoded C++ strings as well. A mod cannot add an equivalent override, such as a language barrier or mind control, and cannot disable the existing ones.

Side bug: `avatar::talk_to` calls `d.actor(true)->get_topics()` twice (`npctalk.cpp:1674` and `1677`). `talker_avatar::get_topics` is never called, so TALK_NOFACE is dead code, and the NPC side effects (mission `on_talk_with_npc`, `mod_moves(-1s)`, `make_angry`, `decide_needs`) run twice.

```cpp
if( me_npc->has_effect( effect_npc_suspend ) ) { add_topics.emplace_back( "TALK_REBOOT" ); // :161
( me_npc->get_int() >= 9 ) ) { add_topics.emplace_back( "TALK_CHURL_TRADE" );             // :200
```

**Fix:** Add a JSON type such as `{ "type": "dialogue_topic_override", "id": "churl", "condition": { "u_has_trait": "PROF_CHURL" }, "topic": "TALK_CHURL", "priority": 50, "replace": false }` and evaluate it in `get_topics` with the dialogue's talkers. Port the existing rules to data. Remove the duplicate `get_topics()` call, and call the avatar's `get_topics` for the alpha side.

### MATH-J11 · jmath functions are numbers-only, unscoped and unvalidated
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/math_parser.cpp:665`, `src/math_parser.cpp:669`, `src/math_parser.cpp:633`, `src/math_parser.cpp:84`, `src/math_parser_jmath.cpp:57`, `src/math_parser_jmath.cpp:82`, `src/dialogue_helpers.cpp:57`

JSON-defined math functions are much weaker than the built-ins:

- They cannot take string arguments, because the jmath arity frame is not "stringy". `armor_total('torso')` is a syntax error.
- They cannot take a scope prefix. `n_my_func()` parses as an npc variable followed by `(` and fails.
- They cannot be assignment targets.
- `num_args` is not checked against the `_N` references in the body, so an out-of-range `_2` reads 0, or leaks the outer caller's `_2` when calls are nested.
- The shadowing check compares only against unscoped diag function names, and parse order is builtins, then jmath, then diag. A jmath named `min` is silently ignored, and one named `u_skill` silently overrides the built-in.

```cpp
} else if( jmath_func_id jmfid( token ); jmfid.is_valid() ) {
    ops.emplace( jmfid, token );
    arity.emplace( token, jmfid->num_params, arity_t::type_t::func ); // not stringy (cf. :727)
```

**Fix:** Make the jmath arity stringy and pass `diag_value`s into `_0`..`_n`, so `my_func('torso', 3)` works. Support `u_`/`n_` prefixes by swapping alpha and beta in the child dialogue. Add an optional `"assign": "u_foo = _val"` body. In finalize, reject `_N` with N >= num_args. Extend the shadow check to `functions[]`, `constants[]` and scoped names.

### MATH-J12 · u_var_changed fires only for legacy u_add_var strings, not math or other var writes
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctalk.cpp:3925`, `src/condition.cpp:288`, `src/talker_character.cpp:617`, `doc/JSON/EFFECT_ON_CONDITION.md:1956`

The only C++ send site for `u_var_changed` is the non-time branch of `u_add_var`/`npc_add_var`. Writes through `math`, `set_string_var` or any talker `set_value` never emit it, and the event carries no talker identity. Authors can work around this with `trigger_event` after the write, as `portal_storm_effect_on_condition.json:218` does.

```cpp
actor->set_value( var_name, possible_values[index] );
get_event_bus().send<event_type::u_var_changed>( var_base_name, possible_values[index] ); // only send site
```

**Fix:** Emit a `talker_var_changed` event `{ "talker": character_id, "scope": "u|npc|global", "var": string, "value": string }` from the `set_value` overrides and the global setter. To bound the cost, emit only for variable names that some EVENT EOC listens for, collected at finalize from a `"var_filter"` field.

### MATH-J13 · NPC training prices and times hardcoded; style price duplicated as literal $8
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctalk.cpp:571`, `src/npctalk.cpp:596`, `src/npctalk.cpp:605`, `src/npctalk.cpp:631`, `src/talker_character.cpp:1372`, `src/npctalk_funcs.cpp:1272`

The training cost and time formulas for skills, proficiencies, martial arts and spells are C++ constants:

- skill: 1000\*(1+lvl)² cents
- style: 800 cents and 30 minutes for every style
- spell: 100\*difficulty\*level

A TODO admits this. `style_training_text` prints a literal `8` instead of calling `calc_ma_style_training_cost`. The two agree today, so this is a maintenance hazard, not a live bug.

```cpp
// TODO: all styles cost the same and take the same time to train, maybe add values to the ma_style class
return string_format( _( "%s ( cost $%d )" ), style.obj().name, 8 );
```

**Fix:** Add optional `"training_cost"` and `"training_time"` fields (`dbl_or_var` / `duration_or_var`, teacher = u, student = n) to martial_art, skill, proficiency and spell JSON, falling back to the current formulas. In `style_training_text`, use `calc_ma_style_training_cost()` and `format_money()`.

### MATH-J14 · Math language lacks && / ||, string comparison, and non-numeric assignment
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/math_parser_impl.h:328`, `src/math_parser_lex.cpp:113`, `src/math_parser.cpp:607`, `src/math_parser.cpp:636`, `src/math_parser.cpp:211`, `src/math_parser.cpp:285`, `src/math_parser.cpp:295`

The language has no logical AND/OR: `a > 1 && b < 2` fails with "Unknown operator". String literals are legal only as dialogue-function arguments, so `_this == 'f_bed'` is a syntax error. Variables can hold strings and tripoints, but math can assign only doubles, so `u_dest = n_home_loc` throws at runtime.

Every one of these capabilities already exists outside the math language: and/or combinators, `compare_string`, `copy_var` and `set_string_var`. This is a matter of ergonomics.

```cpp
throw math::syntax_error( "String arguments can only be used in dialogue functions" ); // :636
void var::assign( dialogue &d, double val ) const { write_var_value( varinfo.type, varinfo.name, &d, val ); }
```

**Fix:** Add `&&` and `||` (two-char tokens, precedence 0.5, short-circuit). Allow string operands for `==`/`!=`. Add a value-copy assignment node for var = var, string literal or `tripoint(x,y,z)`. Examples: `{ "math": [ "u_flee_to = n_home" ] }`, `{ "math": [ "_ok = _this == 'f_bed' && u_sleepiness() > 300" ] }`.

### MATH-J15 · No way to read a talker's type id (mtype/itype/vehicle/zone/npc class)
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/talker.h:84`, `src/condition.cpp:2245`, `src/iexamine_actors.cpp:301`, `src/mutation.cpp:913`, `src/talker_character.cpp:390`, `src/math_parser_diag.cpp:952`

`const_talker` has no type-id accessor. EOCs learn what a talker is only through context vars that some hooks inject (`this`, `id`, `victim_type`). Monster ids can be tested indirectly with `n_monsters_nearby('mon_x', 'radius': 0, 'attitude': 'both') > 0`, and item ids with run_inv_eocs filters. Item, vehicle and NPC-class string ids cannot be read at all. Separately, `bodytype` is hardcoded to "human" for every Character.

```cpp
// talker_character.cpp:394 – All characters are human-bodytyped for now
return bt == "human";
```

**Fix:** Add `virtual std::string get_type_id() const`, returning the mtype, itype, vehicle prototype, zone type, npc class or "avatar". Expose it as `{ "mutator": "u_type_id" }` and as a condition `{ "npc_is_type": [ "mon_zombie_brute", "mon_zombie_hulk" ] }`. Source bodytype from JSON instead of the hardcoded string.

## 2. Bugs, oversights & quirks

### MATH-B01 · limb_score() without 'type' throws a non-math exception and crashes the game
**Severity:** Critical · **Effort:** Small · **Verified:** confirmed
**Where:** `src/math_parser_diag.cpp:348`, `src/math_parser_diag.h:23`, `src/enum_conversions.h:79`, `src/condition.cpp:2572`, `src/math_parser_type.h:22`, `src/main.cpp:883`, `data/json/effects_on_condition/medicine_eocs/speed_serum_eoc.json:69`

The docs mark `type` as optional (NPCs.md:1421). When it is left out, `kwarg_or("type").str(d)` is `""`, and `io::string_to_enum<bp_type>("")` throws `io::InvalidEnumString`. That is a `std::runtime_error`, not a `math::exception`. `eoc_math::act` catches only `math::exception`, and nothing up to `main`'s `do_turn` loop catches `std::exception`, so the process terminates.

Vanilla content reaches this: `EOC_SERUM_SPEED_PERIODIC_SIDE_EFFECTS` (the `rand(5) == 5` branch, every 25–65 minutes while the player is awake on speed serum) evaluates `u_limb_score('balance')` with no type.

```cpp
const bp_type bp_t = io::string_to_enum<bp_type>( kwargs.kwarg_or( "type" ).str( d ) ); // :348
} catch( math::exception const &re ) {                                                 // condition.cpp:2572
while( !g->do_turn() ) {}                                                              // main.cpp:883, no try
```

**Fix:** If the type string is empty, default to all limb types (`bp_type::num_types`). Use `io::string_to_enum_optional` and throw `math::runtime_error` for an unknown non-empty value. Also have `eoc_math::act` catch `std::exception`, so one bad EOC cannot kill the process.

### MATH-B02 · Assignable math functions add instead of set, breaking `=` and doubling `+=`
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/math_parser.cpp:381`, `src/math_parser_impl.h:353`, `src/math_parser_diag.cpp:167`, `src/math_parser_diag.cpp:1047`, `src/math_parser_diag.cpp:1069`, `src/math_parser_diag.cpp:1075`, `src/talker_character.cpp:213`, `src/talker_character.cpp:882`, `src/talker_character.cpp:1094`, `src/talker_character.cpp:1098`, `src/character.cpp:3384`, `src/condition.cpp:2494`, `data/json/effects_on_condition/addictions_eocs.json:45`, `data/json/effects_on_condition/medicine_eocs/regen_serum_eoc.json:43`, `data/mods/MindOverMatter/powers/pyrokinesis_eoc.json:62`, `doc/JSON/NPCs.md:1452`, `doc/JSON/NPCs.md:1535`

The math layer passes the assign function the absolute new value. For `+=` and `++`, that value is old + rhs, computed through `mhs`. Several setters treat it as a delta instead:

- `set_addiction_turns` does `sated += amount`. `= 0` never clears the counter, `+=` roughly doubles it, and a missing addiction can never be created.
- The perceived branch of `pain_ass` calls `mod_pain(val)`. The doc's own example, `u_pain('type':'perceived') += 22`, therefore adds current pain + 22, and `= 10` adds 10. The raw branch correctly uses `set_pain`.
- `set_str/dex/int/per_bonus` call `mod_*_bonus` even though `Character::set_*_bonus` exists. This part has low impact, because bonuses are reset every turn and no content assigns them.

Vanilla is affected. `EOC_SLEEP_ADDICTION` runs `u_addiction_turns('sleeping pill')++` when sated < 0, which roughly doubles the deficit, so withdrawal decays in about a minute instead of being prolonged. Regen serum, MindOverMatter and Magiclysm EOCs use perceived `+=` and add the current pain on top.

```cpp
double const val = op( mhs->eval( d ), rhs->eval( d ) ); v.assign( d, val );   // math_parser.cpp:381
add.sated += time_duration::from_turns( amount );                                // talker_character.cpp:1098
if( format == "perceived" ) { d.actor( is_beta( scope ) )->mod_pain( val );      // math_parser_diag.cpp:1075
void talker_character::set_str_bonus( int value ) { me_chr->mod_str_bonus( value ); }
```

**Fix:** Make every assign function treat `val` as the target value:

- `add.sated = time_duration::from_turns(amount)`, adding the addiction if it is missing.
- `mod_pain( val - perceived_pain_cur() )`.
- `me_chr->set_*_bonus(value)`.

Core EOCs that rely on perceived `= 90` / `= -40` meaning "add" (`effects_eocs.json:282`, `marloss.json:154/166`) must be migrated at the same time. Add a round-trip test over `get_all_diag_funcs()`: for every function with an assign function, `f() = x` must give `f() == x`, and `f() += 1` must increase the value by exactly 1.

### MATH-B03 · faction_*() math functions dereference nullptr when given an unknown faction id
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/math_parser_diag.cpp:363`, `src/math_parser_diag.cpp:370`, `src/math_parser_diag.cpp:407`, `src/math_parser_diag.cpp:419`, `src/math_parser_diag.cpp:454`, `src/faction.cpp:584`

Each of the 13 faction_like/respect/trust/food_supply/wealth/power/size eval and assign functions dereferences `faction_manager::get(id)` without a check. For an unknown id, `get()` shows a debugmsg and returns nullptr, and the game then segfaults. Ids can come from variables, and nothing validates them at load time, so a typo, a cross-mod reference or a stale string var crashes the game.

```cpp
faction *fac = g->faction_manager_ptr->get( faction_id( params[0].str( d ) ) );
return fac->likes_u;
```

**Fix:** Add a helper `get_fac_or_throw()` that calls `get(id, false)` and throws `math::runtime_error("Unknown faction \"%s\"")` on nullptr, and use it in all 13 functions.

### MATH-B04 · Assignment ignores read-format kwargs: time('now','unit':..) += N rewinds the calendar; calories('format':'percent') = X sets raw kcal
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/math_parser_diag.cpp:1344`, `src/math_parser_diag.cpp:1347`, `src/math_parser_diag.cpp:1353`, `src/math_parser_diag.cpp:1733`, `src/math_parser_diag.cpp:1953`, `src/math_parser_diag.cpp:1968`, `src/math_parser.cpp:381`, `src/math_parser.cpp:966`

For compound operators, `mhs` reads the function with its kwargs, but `time_ass` and `calories_ass` ignore those kwargs.

- `time('now', 'unit':'hours') += 1` sets `calendar::turn` to turn_zero plus about 2201 turns. The calendar jumps back months, which breaks queued EOCs, effect start times and seasons.
- `u_calories('format':'percent') = 100` sets stored kcal to 100, which starves the character.

Both parse cleanly. No vanilla content uses them this way.

```cpp
calendar::turn = calendar::turn_zero + time_duration::from_turns<double>( val ); // :1353
int difference = val - current_kcal;                                             // :1740
```

**Fix:** In `time_ass`, convert `val` back from the unit. In `calories_ass`, honour `'format':'percent'`. As a general rule, every `*_ass` should either apply the same kwargs as its eval or throw when a read-only kwarg is present.

### MATH-B05 · time_until_eoc() returns the absolute turn number, not time remaining, and ignores per-character queues
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/math_parser_diag.cpp:1423`, `src/math_parser_diag.cpp:1429`, `src/math_parser_diag.cpp:1435`, `src/effect_on_condition.cpp:238`, `src/character.h:328`, `src/character.cpp:440`

The function returns `to_turn(it->time)`, the absolute turn since epoch, so a check like `time_until_eoc('X', 'unit':'minutes') < 10` is always false. It also searches only the global queue, while non-global ACTIVATION and RECURRING EOCs are queued on the Character, so those always report -1. When several instances are queued, it returns the first one inserted, not the soonest. No vanilla data uses the function.

```cpp
return it != list.end() ? _time_in_unit( to_turn<double>( it->time ), unit_val.str( d ) ) : -1;
```

**Fix:** Return `_time_in_unit(to_turns<double>(it->time - calendar::turn), unit)`. Add u/n scopes that search the actor's `queued_effect_on_conditions`, and take the minimum over all matches.

### MATH-B06 · time_since('noon') and time_until('noon') both return |time_of_day - 12h|
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/calendar.cpp:337`, `src/math_parser_diag.cpp:1377`, `src/math_parser_diag.cpp:1408`

Before midday, `calendar::noon()` returns the wrong time: at 10:00 it returns 08:00. `time_until('noon')` also reuses the `time_since` formula, `turn - noon`. As a result, both functions return |t − 12h|. At 10:00, `time_since('noon')` returns 2h instead of 22h. Xedra Evolved (`cryptids.json:42`, `:98`) uses these functions, and its day/night split currently depends on the symmetric result, so that content must be checked alongside the fix.

```cpp
const time_duration till_noon = time_of_day - 12_hours;
return ( till_noon > 0_seconds ) ? p - till_noon : p + till_noon;          // calendar.cpp:340
ret = to_turns<double>( calendar::turn - noon( calendar::turn ) );          // time_until, :1408
```

**Fix:** Make `noon()` return today's noon (`p - time_past_midnight(p) + 12_hours`). Use `noon - turn` for time_until and `turn - noon` for time_since, each wrapped into [0, 1 day). Add tests at 10:00 and 14:00.

### MATH-B07 · characters_nearby(): documented 'allow_hallucinations' kwarg is a load error, and non-Character actors skip the radius/attitude filter
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/math_parser_diag.cpp:823`, `src/math_parser_diag.cpp:846`, `src/math_parser_diag.cpp:880`, `src/math_parser_diag.cpp:1886`, `src/math_parser.cpp:218`, `src/math_parser.cpp:831`

There are two separate problems:

1. `allow_hallucinations` is documented and read at line 846, but it is missing from the registry's kwargs, so `_validate_unused_kwargs` rejects any expression that uses it. MATH-J01 also lists this.
2. `_filter_character` returns `beta == nullptr || (...)`. When the scoped talker is not a Character (a monster, an item, or a global scope with a monster alpha), every non-hallucination character in the bubble is counted, regardless of radius and attitude. `u_characters_nearby('radius': 3)` on a monster returns every NPC loaded.

```cpp
return beta == nullptr || ( _friend_match_filter_character( *beta, guy, filter ) && radius >= rl_dist( guy.pos_abs(), loc ) );
```

**Fix:** Add `"allow_hallucinations"` to the kwargs list. Always apply the radius check, and apply the attitude filter only when beta is non-null, or throw for a non-`any` attitude when there is no Character.

### MATH-B08 · '%' is right-associative at the same precedence as * and /
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/math_parser_impl.h:341`, `src/math_parser_impl.h:47`, `src/math_parser_impl.h:236`, `src/math_parser.cpp:694`

An incoming `*`, `/` or `%` does not pop a stacked `%`, so:

- `10 % 4 * 2` evaluates to 2 instead of 4.
- `x % 10 / 2` evaluates as `x % 5`.
- `20 % 6 % 4` evaluates to 0 instead of 2.

There is no warning, and the tests cover only `50 % 3`. The current data uses only unaffected forms, so this is a latent miscalculation waiting for modders.

```cpp
binary_op{ "%", 3, binary_op::associativity::right, math_opers::mod },
```

**Fix:** Change `%` to `associativity::left`, and add tests for `10 % 4 * 2 == 4` and `20 % 6 % 4 == 2`.

### MATH-B09 · Item, furniture/computer and zone talkers report the wrong overmap location
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/talker_item.cpp:57`, `src/talker_furniture.cpp:42`, `src/talker_zone.h:30`, `src/talker.h:115`, `src/condition.cpp:978`, `src/condition.cpp:1028`, `src/condition.cpp:1612`, `src/npctalk.cpp:7046`

`talker_item_const::pos_abs_omt` and `talker_furniture_const::pos_abs_omt` return the avatar's OMT, although their `pos_abs()` is correct. `talker_zone_const` does not override `pos_abs_omt`, so it returns the base `{}`. As a result, `npc_at_om_location`, `npc_near_om_location` and `is_safe` give wrong answers for items in other buildings, items carried by NPCs, computers, and every zone that `run_zone_eocs` iterates.

```cpp
tripoint_abs_omt talker_item_const::pos_abs_omt() const { return get_player_character().pos_abs_omt(); }
```

**Fix:** Use `return project_to<coords::omt>( pos_abs() );` in all three classes, and consider making that the `const_talker` default.

### MATH-B10 · Item talker is not invalidated by u_die/npc_die: later effects null-deref the deleted item
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/talker_item.cpp:177`, `src/talker_item.cpp:29`, `src/talker_item.cpp:64`, `src/talker_item.cpp:153`, `src/item_location.cpp:1401`, `src/item_location.cpp:1446`, `src/npctalk.cpp:2380`, `src/iuse_actor.cpp:6032`

`talker_item::die()` calls `item_location::remove_item()`, which swaps in an `impl::nowhere`, so `get_item()` returns nullptr from then on. The talker stays in the dialogue, and every accessor dereferences `get_item()` unchecked. Any later use in the same EOC crashes, including a second EOC in the same use_action `eocs` list. Examples: a `<npc_name>` message, `npc_add_var`, `n_val()`, `has_flag`. A natural pattern triggers it: `"effect": [ "npc_die", { "u_message": "<npc_name> crumbles" } ]`. No vanilla content currently does this.

```cpp
void talker_item::die( map * ) { me_it->remove_item(); }   // talker_item.cpp:177
return me_it_const->get_item()->type_name();               // talker_item.cpp:29
```

**Fix:** Guard the accessors with a helper that returns an empty value or throws `math::runtime_error` when `!*me_it`. Alternatively, after `die()` replace the dialogue's talker with a base `talker`. At minimum, null-check `get_name`, `disp_name`, `maybe_get_value`, `set_value` and `has_flag`.

### MATH-B11 · val('pos_x/y/z') assignment does a raw setpos: no map shift, no vehicle/impassable handling, monsters ignore it
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/talker_character.cpp:183`, `src/condition.cpp:2514`, `src/creature.cpp:234`, `src/talker.h:810`, `src/teleport.cpp:215`

`talker_character::set_pos` calls `Creature::setpos` directly. Unlike `teleport::teleport_to_point`, it does not shift the map for the avatar, unboard vehicles, or check for impassable tiles. Consequences:

- `u_val('pos_x') += 80` leaves the avatar outside the loaded bubble.
- Moving a vehicle passenger desyncs the vehicle.
- `u_val('pos_z') = -3`, which `TALK_TEST.json:1128` uses, drops the avatar into rock.

Monsters inherit the empty base `set_pos`, so the same write silently does nothing. `u_teleport` is the supported alternative.

```cpp
void talker_character::set_pos( tripoint_abs_ms new_pos ) { me_chr->setpos( new_pos ); }
```

**Fix:** Route `set_pos(abs)` through `teleport::teleport_to_point(..., force=false)`, or at least handle the out-of-bounds and vehicle cases. Do the same for talker_monster, or throw `math::runtime_error` for talkers that cannot move.

### MATH-B12 · weather() assignment is transient and 'windpower' never reaches the effective wind speed
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/math_parser_diag.cpp:1846`, `src/math_parser_diag.cpp:1849`, `src/weather.cpp:1037`, `src/weather.cpp:1054`, `src/weather.h:223`, `src/weather.h:232`

`weather_ass` writes the `weather_precise` fields, but `update_weather()` regenerates them at the next weather roll, so temperature, humidity and pressure overrides are lost. `windpower` writes only `weather_precise->windpower`, which nothing in gameplay reads. Every gameplay consumer (fire, sails, turbines, wind chill) reads `weather_manager::windspeed`, which is copied only during `update_weather`.

Vanilla content depends on the write: the scarlet storm EOCs (`scarlet_storm_effect_on_condition.json:164`, `:173`) and the desert_region mod (`weather_eoc.json:158`, `:179`) set windpower and silently get no effect. The existing `forced_temperature` and `windspeed_override` hooks are unused.

```cpp
} else if( aspect == "windpower" ) { get_weather().weather_precise->windpower = val;      // :1849
windspeed = windspeed_override ? *windspeed_override : w.windpower;                      // weather.cpp:1054
```

**Fix:** Also set `get_weather().windspeed` when assigning windpower. Either document that assignments last only until the next weather change, or expose `forced_temperature`/`windspeed_override` with an explicit duration.

### MATH-B13 · 'RANDOM' target_part uses the avatar's anatomy for NPCs; monsters treat 'RANDOM' as an invalid bodypart id
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/talker_character.cpp:593`, `src/talker_character.cpp:605`, `src/talker_monster.cpp:82`, `src/talker_monster.cpp:89`

`talker_character::add_effect` and `remove_effect` roll `get_player_character().random_body_part(true)` even when the talker is an NPC, so effects can land on parts that NPC does not have. `talker_monster::add_effect` has no RANDOM branch: `bodypart_str_id("RANDOM")` triggers an "invalid body_part id" debugmsg and falls back to id 0. `talker_monster::remove_effect` rolls a body part from the player. The EOC docs (line 3517) document RANDOM for every `add_effect` user.

```cpp
if( "RANDOM" == bp ) { target_part = get_player_character().random_body_part( true );
```

**Fix:** Use `me_chr->random_body_part(true)`. For monsters, handle RANDOM in both functions with `me_mon->random_body_part(true)`, or map it consistently to the null id.

### MATH-B14 · Setting val('age') double-counts years elapsed since the cataclysm
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/talker_character.cpp:1153`, `src/character.cpp:4097`, `src/condition.cpp:2473`

`Character::age()` returns `init_age + years_since_cataclysm`, and `set_age` stores `amount + years_since_cataclysm`, so reading the value back gives amount + 2 × years. Two years in, `u_val('age') = 30` reads back as 34. The bug only shows after the first full in-game year.

```cpp
me_chr->set_base_age( amount + years_since_cataclysm );
```

**Fix:** `me_chr->set_base_age( amount - years_since_cataclysm );`

### MATH-B15 · u_buy_monster charges the full price before spawning, with no refund when placement fails
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/talker_avatar.cpp:85`, `src/talker_avatar.cpp:91`, `src/talker_avatar.cpp:111`

`pay_npc(cost)` runs before any monster is placed. The spawn loop stops at the first failed `place_critter_around` (radius 3) and only logs a debug message. The popup still reports the requested count, and the function returns true. Vanilla uses this effect (the animal shelter dog, the Isherwood horse and cow), and a large animal can fail to place in a cramped interior.

```cpp
if( !mon_ptr ) { add_msg_debug( ... ); break; }
popup( _( "%1$s gives you %2$d %3$s." ), seller_guy->get_name(), count, ...
```

**Fix:** Count the monsters actually placed, refund `cost * (count - placed) / count` (or check placement before paying), and report the placed count.

### MATH-B16 · get_is_alive() is only on the mutable talker_monster, so const contexts report live monsters as dead
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/talker_monster.h:116`, `src/talker_monster.cpp:228`, `src/talker.h:153`, `src/creature.cpp:3723`, `src/condition.cpp:1243`

`get_const_talker_for(const Creature&)` builds a `talker_monster_const`, which does not override `get_is_alive`, so `u_is_alive`/`npc_is_alive` return false for a live monster. Those const dialogues are used for monster spellcasting, special-vision enchantment conditions and cata_tiles display conditions. Content uses this condition only on the avatar today.

```cpp
class talker_monster: public talker_monster_const { ... bool get_is_alive() const override;
```

**Fix:** Move `get_is_alive()` into `talker_monster_const`, implemented with `me_mon_const->is_dead()`.

### MATH-B17 · Inconsistent truthiness: conditions use != 0, ternary uses > 0, kwargs use >= 1
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/math_parser.cpp:365`, `src/math_parser_diag.cpp:93`, `src/math_parser_diag.cpp:874`, `src/math_parser_impl.h:321`, `src/condition.cpp:1980`

The parser uses four different rules for "true":

- A math condition: non-zero.
- A ternary: > 0.
- `!`: tests == 0, so any non-zero value counts as true.
- Boolean kwargs (`raw`, `strict`, `baseline`, ...): >= 1.

`allow_hallucinations` adds a fifth rule: it is truncated to an int, so 0.5 counts as false. In practice, `u_effect_intensity('x')` returns -1 for an absent effect. That value is true as a condition and false in a ternary. NPCs.md never defines the rule.

```cpp
return cond->eval( d ) > 0 ? mhs->eval( d ) : rhs->eval( d );   // ternary
return dbl >= 1 || float_equals( dbl, 1 );                      // is_true
```

**Fix:** Standardise on non-zero is true. Use `!float_equals(x, 0)` in the ternary and in `is_true()`, and document the rule in NPCs.md.

### MATH-B18 · Read/write capability errors: faction_food_supply documented as assignable but read-only; write-only spellcasting_adjustment fails only at runtime
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/math_parser_diag.cpp:1907`, `src/math_parser_diag.cpp:1941`, `src/math_parser.cpp:190`, `src/math_parser.cpp:440`, `src/math_parser.cpp:832`, `src/math_parser.cpp:966`

NPCs.md marks `faction_food_supply` as writable and says its vitamin kwarg is for "getting/setting", but the function has no assign function, so assigning to it is a load-time error. In the other direction, `spellcasting_adjustment` has no eval function, yet `x = u_spellcasting_adjustment(...)` and `+= 1` parse fine and only fail at runtime with an internal error.

```cpp
{ "faction_food_supply", { "g", 1, faction_food_supply_eval, {}, { "vitamin" } } },
throw math::internal_error( "math called eval() on unexpected function that cannot evaluate" );
```

**Fix:** Add `faction_food_supply_ass`, or fix the docs. In `new_func`/`new_oper`, throw a `syntax_error` when a func_diag with no eval function is used anywhere other than as a plain `=` target.

### MATH-B19 · NPC 'How much further?' distance text prints garbage decimals and has inconsistent units
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/talker_npc.cpp:69`, `src/talker_npc.cpp:70`, `src/talker_npc.cpp:71`, `src/talker_npc.cpp:72`, `src/talker_npc.cpp:77`, `src/talker_npc.cpp:79`, `src/npctalk.cpp:1836`, `data/json/npcs/common_chat/TALK_COMMON_OTHER.json:213`

`distance_to_goal` passes the full rounded value as the decimal part, so 30 OMT prints "1.100 miles." and 40 OMT prints "1.150 miles.". The units are also inconsistent: feet use 100 ft per OMT and miles use 25 OMT per mile, so 12 OMT reads "1200 feet." and 13 OMT reads "0.50 miles.". Vanilla shows this text through TALK_HOW_MUCH_FURTHER.

```cpp
int miles = dist / 25; miles -= miles % 25; int fullmiles = ( miles - miles % 100 ) / 100;
response = string_format( _( "%d.%d miles." ), fullmiles, miles );
```

**Fix:** Use `string_format( _( "%d.%02d miles." ), fullmiles, miles % 100 )`, and derive feet and miles from one OMT length.

## 3. Performance

### MATH-P01 · math_exp_impl keeps the parser's scratch stacks alive for every parsed expression
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/math_parser.cpp:503`, `src/math_parser.cpp:504`, `src/math_parser.cpp:525`, `src/math_parser.cpp:1045`, `src/math_parser.cpp:1051`

The parser's working state (`ops`, `output` and `arity` stacks, plus `parse_state` and `parse_position`) lives in `math_exp_impl` next to the tree, so it survives for the lifetime of the expression and is copied with it. With libstdc++, each empty `std::deque` already holds a map and a ~512-byte node. That is about 1.5 KB of dead heap per expression. With core, Xedra Evolved and MindOverMatter loaded (about 15k `"math"` keys), it comes to roughly 20 MB and about 90k extra allocations during load. This costs memory and load time; it is not on a per-turn path.

```cpp
std::stack<op_ctxt> ops; std::stack<thingie> output;   // :503
std::stack<arity_t> arity; thingie tree{ 0.0 }; std::string_view parse_position; parse_state state;
```

**Fix:** Keep only `tree` and `type` in `math_exp_impl`, and move the parse state into a transient parser object on the stack inside `parse()`. If stacks are still wanted, use `std::stack<T, std::vector<T>>`, which allocates nothing until first use.

### MATH-P02 · jmath function calls copy the whole dialogue (talker clones + context maps) on every call
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/math_parser_jmath.cpp:82`, `src/math_parser.cpp:276`, `src/npctalk.cpp:2707`, `src/magic_enchantment.cpp:944`, `src/magic_enchantment.cpp:968`, `src/character_body.cpp:252`, `src/npc.cpp:3557`

`jmath_func::eval` builds `const_dialogue d_next( d )` on every call, even with zero arguments. The copy constructor clones each present talker, copies the context and conditionals maps and the callstack, and then inserts each argument through `std::to_string(i)`. Nested calls multiply this: MindOverMatter's `psionic_power_modifiers()` copies the dialogue three times. The hot path is `update_body` → `recalculate_enchantment_cache` → `dbl_or_var::evaluate` → jmath, which runs every turn for the avatar and every 10 s for NPCs. Enchantment dialogues have a null beta, so each copy costs a handful of mallocs: real overhead, not a measured hotspot.

There is also a side issue: the copy constructor raises the debugmsg "Constructed a dialogue with no actors!" when a jmath function runs in an actor-less dialogue (`game.cpp:4995` revive conditions, `magic_enchantment.cpp:963`).

```cpp
double jmath_func::eval( const_dialogue const &d, std::vector<double> const &params ) const {
    const_dialogue d_next( d );
    for( ... ) { d_next.set_value( std::to_string( i ), params[i] ); }
```

**Fix:** Add a lightweight argument frame to `const_dialogue` (a pointer and count), saved and restored with RAII in `jmath_func::eval`. Have the parser turn `_0`..`_N` into an `arg` node that reads from the frame. Interim fixes:

- When `num_params == 0`, call `_exp.eval( d )` directly.
- Use static `"0"`..`"15"` key strings.
- Borrow talkers through non-owning pointers.

### MATH-P03 · Constant string arguments are re-interned as string_id/int_id on every evaluation
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/math_parser_diag.cpp:124`, `src/math_parser_diag.cpp:338`, `src/math_parser_diag.cpp:500`, `src/math_parser_diag.cpp:1149`, `src/math_parser_diag.cpp:1790`, `src/string_id.cpp:27`, `src/condition.cpp:433`, `src/condition.cpp:2426`

Eval functions build `trait_id(...)`, `skill_id(...)`, `efftype_id(...)`, `vitamin_id(...)` and `bodypart_id(...)` from the argument string on every call. Each construction hashes into the global intern table. With libstdc++ 13, `emplace` allocates and frees a node even when the key already exists. The new id starts with a cold `_cid` cache, so `.obj()` or int_id conversion does a second lookup. JSON conditions such as `u_has_trait` follow the same pattern (`condition.cpp:433`), so the issue is broader than the math parser. Kwarg enums are compared as strings on every call.

```cpp
return d.const_actor( is_beta( scope ) )->has_trait( trait_id( params[0].str( d ) ) );
const auto &pair = get_intern_map().emplace( std::forward<S>( s ), next_id );  // string_id.cpp:27
```

**Fix:** Resolve static arguments once. One option is a per-function `prepare` hook that converts static params and kwargs into typed values at parse time. Another is a `cached_id<T>(diag_value const&)` helper that keeps the constructed id in a mutable slot, as `legacy_value::converted` already does. Separately, call `find()` before `emplace()` in the intern function, so lookups of existing ids never allocate.

### MATH-P04 · func_diag copies every argument and kwarg pair by value on every evaluation
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/math_parser.cpp:240`, `src/math_parser.cpp:409`, `src/math_parser.cpp:426`, `src/math_parser.cpp:438`, `src/math_parser_diag.h:23`, `src/math_parser_diag.cpp:337`

The visitor lambdas in `_update_diag_args<true>` and `_update_diag_kwargs<true>` capture `thing` and `blorg` by value. Every parameter and kwarg is copied on every evaluation, including static literals whose branch is compiled out. `kwarg_or` returns `diag_value` by value, and `effect_intensity_eval` copies `bp_str` again. Most strings fit in the small-string buffer, so the real cost comes from long ids and nested arguments. Callers include sidebar widgets (about 400 math clauses per redraw), enchantments and every EOC math condition.

```cpp
std::visit( overloaded{ [thing, this, i, d]( auto const & v ) { ... } }, thing.data );   // :409
for( ... const &blorg : kwargs_dyn ) { std::visit( overloaded{ [this, blorg, d]( ... // :426
```

**Fix:** Capture by reference (`[&thing, this, i, d]` and `[&blorg, this, d]`). Record the dynamic indices at construction and loop over only those. Make `kwarg_or` return `diag_value const &`, and use a transparent comparator (`std::less<>`) for the kwargs map. Bind `bp_str` by const reference.

### MATH-P05 · Builtin and jmath function calls heap-allocate a std::vector<double> on every evaluation
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/math_parser.cpp:198`, `src/math_parser.cpp:271`, `src/math_parser.cpp:276`, `src/math_parser_func.h:20`

`_eval_params` allocates a fresh vector for every `func` and `func_jmath` evaluation, because `math_func::f_t` takes `std::vector<double> const &`. `min(`, `max(`, `rand(`, `rng(` and `clamp(` appear thousands of times in data, often in per-turn or per-attack formulas.

```cpp
using f_t = double ( * )( std::vector<double> const & );
```

**Fix:** Change `f_t` to take a span or `(const double*, size_t)`, and evaluate into a stack buffer such as `std::array<double, 8>`. Let `min`/`max` fold incrementally.

### MATH-P06 · monsters_nearby/characters_nearby scan every creature and run filters before the radius check
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/math_parser_diag.cpp:824`, `src/math_parser_diag.cpp:879`, `src/math_parser_diag.cpp:905`, `src/math_parser_diag.cpp:932`, `src/math_parser_diag.cpp:970`, `src/math_parser_diag.cpp:986`, `src/game.cpp:10705`, `src/mongroup.cpp:276`

`_monsters_nearby_eval` rebuilds and re-interns its id vector on every call. It wraps a capturing lambda in a `std::function` and walks every creature through `get_creatures_if`, filling a vector only to return its size. The id and attitude filters run before the cheap radius test, and `mon_groups_nearby` scans each group linearly. `_filter_character` re-fetches the beta Character for every creature. Callers include per-attack technique conditions with `'radius': 1`. The gain from reordering is small, because the id check is an int compare, so the impact is unmeasured.

```cpp
return id_filter && _matches_attitude_filter( critter, filter ) && radius >= rl_dist( critter.pos_abs(), loc );
std::vector<Creature *> const targets = g->get_creatures_if( ... ); return static_cast<double>( targets.size() );
```

**Fix:** Test the radius first and count instead of collecting. Hoist the beta lookup out of the lambda. For small radii, iterate the tiles in range through the creature tracker. Build the ids once from static arguments (see MATH-P03), and precompute a set per monster group.

### MATH-P07 · u_val('body_temp'/'body_temp_delta') rescans all body parts several times per call
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/talker_character.cpp:1215`, `src/talker_character.cpp:1232`, `src/talker_character.cpp:1237`, `src/condition.cpp:2449`, `src/widget.cpp:344`, `src/creature.cpp:2567`, `src/creature.cpp:2612`

`temp_delta()` calls `get_all_body_parts()` twice, and `get_body_temp_delta()` calls `temp_delta()` twice. One delta read costs about 4 vector allocations and about 190 map lookups. These values back 22 sidebar clauses that are evaluated on every redraw.

```cpp
return me_chr_const->get_part_temp_conv( temp_delta( me_chr_const ).second ) - me_chr_const->get_part_temp_cur( temp_delta( me_chr_const ).first );
```

**Fix:** Compute both extremes in a single pass over one body-part list, and call `temp_delta()` once. Optionally cache the result per turn.

### MATH-P08 · talker_character accessors copy heavy objects per call (wielded item, spells, ammo sets)
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/talker_character.cpp:158`, `src/talker_character.cpp:449`, `src/talker_character.cpp:919`, `src/magic.cpp:2801`, `src/melee.cpp:2687`

Three accessors copy more than they need:

- `attack_speed()` deep-copies the wielded item. It is evaluated on every enchantment recalculation and every melee attack for BombasticPerks.
- `get_spell_level(trait_id)` iterates `spells_known_of_class()`, which returns spell copies.
- `wielded_with_item_ammotype()` builds the `ammo_types()` set twice and scans it linearly.

```cpp
item cur_weap = cur_weapon ? *cur_weapon : null_item_reference();
```

**Fix:** Use `const item &cur_weap = ...`. Loop over `magic->get_spells()` and filter on `spell_class()`. Call `ammo_types()` once and use `.count()`.

### MATH-P09 · Every dialogue allocates each talker twice; sidebar widgets build a dialogue per clause per redraw
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/talker.h:944`, `src/talker.h:953`, `src/npctalk.cpp:2707`, `src/npctalk.cpp:2748`, `src/widget.cpp:344`, `src/widget.cpp:352`

The `dialogue` constructor `const_clone()`s each talker into the `const_dialogue` base, on top of the talker it already owns, so every actor costs two allocations. Copying a dialogue clones all four. `widget_clause::meets_condition` builds a fresh dialogue and writes the `widget` context var for every clause and every bodypart, even when the clause has no condition, which costs about 4 allocations per clause per redraw. Keeping two talker objects per actor also leaves room for the mutable talker and its const clone to drift apart.

```cpp
: const_dialogue( alpha_in ? alpha_in->const_clone() : nullptr,
                  beta_in ? beta_in->const_clone() : nullptr, cond, ctx ),
```

**Fix:** Have `const_dialogue` hold non-owning `const_talker *` pointers, plus an optional owning pointer for pure const dialogues, so `dialogue` points them at its own talkers. In widget.cpp, build one dialogue per widget draw, check `has_condition` first, and update only the context var for each clause.

## Appendix: considered and rejected

- Math parsing: locale reset, istringstream per number and string_id interning per identifier – parsing only happens at load time. Measured, the locale guard costs about 1.4 ms per 15k expressions and number parsing about 10 ms per 30k, which is negligible next to a multi-second load (the guard is dead code but harmless).
