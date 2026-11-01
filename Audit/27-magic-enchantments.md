# 27 · Magic, spells & enchantments

> Engine audit – static code reading at commit `a1a646d6`; nothing was compiled or run. Each finding was checked against the code by a separate reviewer: **confirmed** = verified by reading the code; **plausible** = consistent with the code, but reachability or impact not proven. Line numbers refer to that commit.

**Scope:** `src/magic.cpp`, `src/magic_spell_effect.cpp`, `src/magic_enchantment.cpp`, `src/magic_type.cpp`, `src/magic_teleporter_list.cpp`, `src/magic_ter_fur_transform.cpp`

## Summary

The spell system works for its main path, a player casting a known spell from the menu, but many other paths are second-class. Fake spells (items, enchantments, monsters, EOC `u_cast_spell`, `extra_effects`) skip spellbook adjustments, RANDOM_TARGET and caster conditions. Several hooks only work for the avatar. Spell formulas and the `effect_on_condition` effect get no spell context, so content hardcodes `u_spell_level('own_id')` thousands of times. There are a handful of real correctness bugs: an HP-cost softlock, blasts through walls, kills credited to the victim, dead pierce fields, and a spellbook preview that teaches spells. Load-time validation is almost absent, and shipped data already contains broken references that only show up when cast. Performance problems centre on NPC spell evaluation and the per-turn enchantment rebuild.

| Lens | Critical | High | Medium | Low | Total |
|---|---|---|---|---|---|
| Missing JSON / EOC | 0 | 3 | 7 | 8 | 18 |
| Bugs & quirks | 1 | 3 | 12 | 5 | 21 |
| Performance | 0 | 2 | 1 | 6 | 9 |
| **Total** | **1** | **8** | **20** | **19** | **48** |

**Fix first:**
1. **MAGIC-B01** · HP-cost spells can softlock the game: a game-hanging infinite loop with a small, local fix.
2. **MAGIC-P01** · Spellbook UI builds temporary avatars and fires level-up events: just previewing a scroll can teach the player a spell (windstrike -> Storm Shipwrecker). Small fix.
3. **MAGIC-B03** · Multi-projectile/SPLIT_DAMAGE hits credit the victim: fireball kills count as NPC suicides and skip murder penalties. Three-argument fix.
4. **MAGIC-B04** · `min/max_pierce` are never applied: documented armour piercing is dead on every spell that sets it. Small fix.
5. **MAGIC-J01** · `effect_on_condition` spells get only `spell_location`: about a quarter of all spells use this effect, and a few context vars would let them scale with level.

## 1. Missing JSON bindings & EOC features

### MAGIC-J01 · `effect_on_condition` spell effect passes only `spell_location`; EOC spells can't read their level, damage or duration
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/magic_spell_effect.cpp:2018`, `src/magic_spell_effect.cpp:2031`, `src/magic_spell_effect.cpp:2042`, `doc/JSON/EFFECT_ON_CONDITION.md:69`, `doc/JSON/MAGIC.md:193`

`"effect": "effect_on_condition"` is the main escape hatch for anything C++ doesn't implement; 559 of about 2,380 spells in data use it. The dialogue it builds has the target as alpha and the caster as beta, plus exactly one context var, `spell_location`. The EOC can't see which spell called it, the effective level, or the computed damage, duration, aoe, dot or range. So it can't scale with level or pick up adjustments, and a shared EOC can't tell which spell invoked it. Mods fall back to `n_spell_level('id')` inside the EOC, which is wrong for fake, item and monster casts (see MAGIC-J07).

```cpp
write_var_value( var_type::context, "spell_location", &d, target_abs );
effect_on_condition_id eoc = effect_on_condition_id( sp.effect_data() );
eoc->activate_activation_only( d, "a spell", "spell being cast", "spell" );
```
There is no other `write_var_value` in the function.

**Fix:** Compute once before the area loop and write context vals `spell_id`, `spell_level`, `spell_damage`, `spell_duration` (as a time_duration value), `spell_aoe`, `spell_range`, `spell_dot` and `spell_damage_type`. Content can then use e.g. `{ "u_add_effect": "burning", "duration": { "context_val": "spell_duration" }, "intensity": { "math": [ "_spell_level / 3" ] } }`. Document the new vals in EFFECT_ON_CONDITION.md. The `here.veh_at( target )` side bug in the same loop is tracked as MAGIC-B19.

### MAGIC-J02 · No enchantment values for spell power; mods emulate them with `opens_spellbook` EOCs that miss fake/item/monster casts
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/magic_enchantment.h:36`, `src/magic.cpp:1265`, `src/magic.cpp:1961`, `src/magic.cpp:2748`, `src/magic.cpp:3804`, `src/math_parser_diag.cpp:651`, `src/math_parser_diag.cpp:1941`, `src/handle_action.cpp:1839`, `src/npcmove.cpp:2643`

Only four enchantment values touch magic: CASTING_TIME_MULTIPLIER, MAX_MANA, REGEN_MANA and BIONIC_MANA_PENALTY. Nothing covers spell damage, range, AoE, duration, energy cost, failure chance, difficulty, sound or XP. The only lever is the assign-only, u-scope math function `u_spellcasting_adjustment(...)`. It writes temp multipliers onto spells already in the spellbook, and those are wiped and rebuilt every time the spellbook opens (`clear_opens_spellbook_data`). Fake spells (item `cast_spell`, enchantment hit_you/hit_me/intermittent, monster spells, EOC `u_cast_spell`, `extra_effects`) build a fresh `spell` with multipliers of 1, so they never see the adjustments. NPCs are worse off: `npcmove.cpp:2643` rebuilds the data only when `magic->spells().empty()`, so NPCs that know spells never get them. Data shows the workaround: 169 `spellcasting_adjustment` calls inside 91 `opens_spellbook` EOCs. A "ring of fire mastery: +20% fire damage" can't be built as a normal enchantment.

```cpp
void known_magic::clear_opens_spellbook_data() { ...
    for( spell *sp : get_spells() ) { sp->clear_temp_adjustments(); } }
{ "spellcasting_adjustment", { "u", 1, {}, spellcasting_adjustment_ass, ... // no eval, u only
```

**Fix:** Add a filtered enchantment block, e.g. `"spell_values": [ { "value": "DAMAGE", "school": "KELVINIST", "flag": "SOMATIC", "magic_type": "magiclysm_evocation_magic", "multiply": 0.2 } ]` with values DAMAGE, RANGE, AOE, DURATION, COST, CAST_TIME, FAIL, DIFFICULTY, EXP and SOUND. Apply them inside `spell::damage/range/aoe/duration/energy_cost/spell_fail` through the caster's `enchantment_cache`, so every cast path gets them. Keep `spellcasting_adjustment` for EOC use, but give it an eval side and an `n_` scope.

### MAGIC-J03 · Almost no load-time validation for spells, magic_type, enchantments or ter_furn_transform
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/magic.cpp:560`, `src/magic.cpp:563`, `src/magic.cpp:456`, `src/magic.cpp:506`, `src/magic.cpp:535`, `src/magic_type.cpp:106`, `src/init.cpp:878`, `src/init.cpp:1037`, `src/magic_enchantment.cpp:205`, `src/magic_ter_fur_transform.cpp:93`

`spell_type::check_consistency` validates `effect_str` only for `summon_vehicle`. It never checks it against the target type for attack/remove_effect (effect type), summon/spawn_item (mtype, itype, groups), effect_on_condition, ter_transform, emit, add_trap, remove_field or recover_energy. `learn_spells` (a `std::map<std::string,int>`), the `channel_*` spell fields (raw strings), `field_id` and `magic_type` are never checked. `magic_type::check_consistency` exists but is never registered in `init.cpp` (only `finalize_all` is), so its formula param-count checks never run. Enchantments and ter_furn_transform have no check function at all. Shipped data already has broken refs that surface only as `Invalid effect` debugmsgs when cast: changeling_summer_protect_from_hot_weather_spell (an `attack` whose effect_str is an EOC id), changeling_winter_walk_on_the_snow (missing effect), aftershock protection_from_cold (`afs_cold_protect` missing), and XE ominous_wind (effect_str `"morale"`).

```cpp
// magic.cpp:563 – the only effect_str check
if( sp_t.effect_name == "summon_vehicle" ) {
    if( ... !vproto_id( sp_t.effect_str ).is_valid() ) debugmsg( ... );
```

**Fix:** Add a per-effect validator table in `spell_type::check_consistency` (effect name -> expected id type, honouring SPAWN_GROUP / POLYMORPH_GROUP / MUTATE_TRAIT). Store `learn_spells` and `channel_*` as `spell_id` and validate them. Register `magic_type::check_consistency`. Add `enchantment::check_consistency` (ench_effects, mutations, emitter, skills, hit_you/hit_me spell ids) and `ter_furn_transform::check`. `tests/json_test.cpp` uses `attack` with effect_str `"string"` and will need its data adjusted. See also MAGIC-J06.

### MAGIC-J04 · `timed_event` spell effect uses a hardcoded case-sensitive map; triffid heart `ROOTS_DIE` silently does nothing
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/magic_spell_effect.cpp:1313`, `src/magic_spell_effect.cpp:1319`, `src/magic_spell_effect.cpp:1327`, `data/json/monster_special_attacks/monster_deaths.json:691`, `data/json/monsters/triffid.json:338`, `src/monster.cpp:3208`, `src/timed_event.cpp:197`, `doc/JSON/MAGIC.md:221`

The effect looks `effect_str` up in a local map with lowercase keys and falls back to `timed_event_type::NONE` for anything unknown; nothing validates it. Vanilla `death_triffid_heart` uses `"effect_str": "ROOTS_DIE"`, so killing a triffid heart queues a NONE event: the root walls don't die, and the `destroys_triffid_grove` event and its memorial entry never fire. The roots can still be bashed, so this isn't a softlock. The docs also list `robot_attack` and `wanted`, which are enum values but are missing from the map.

```cpp
{ "roots_die", timed_event_type::ROOTS_DIE },
...
timed_event_type spell_event = timed_event_type::NONE;
const auto iter = timed_event_map.find( sp.effect_data() );
```

**Fix:** Parse with `io::string_to_enum<timed_event_type>` (case-normalised) at load time and debugmsg on unknown values in `check_consistency`. Fix the triffid data. For custom delayed events, point modders at EOC `queue_eocs`.

### MAGIC-J05 · Spell levelling hooks are avatar-only; `learn_spells` ignored for NPCs; event doc names the wrong field
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/magic.cpp:1454`, `src/magic.cpp:2545`, `src/magic.cpp:3843`, `src/magic.cpp:3854`, `src/magic.cpp:2499`, `src/magic.cpp:2507`, `src/magic.cpp:3496`, `src/magic.cpp:2773`, `src/npcmove.cpp:2646`, `src/event.h:889`, `doc/JSON/EFFECT_ON_CONDITION.md:1943`

`spell::gain_exp` and `set_spell_exp` only emit `player_levels_spell` when `guy.is_avatar()`, so NPC and follower level-ups can't trigger EOCs. `learn_spells` hangs off that event, and its handler hardcodes `get_player_character()`, so NPC casters never unlock follow-up spells (the same handler is what MAGIC-P01 trips over). `evaluate_opens_spellbook_data` clamps NPC adjustments against the avatar's max_level, yet is called for NPCs. The EOC doc names the event field `spell_class`; the code field is `school`, so `{ "context_val": "spell_class" }` is always empty. The wrong-character `forget_spell` event is covered in MAGIC-B12.

```cpp
if( guy.is_avatar() && oldLevel != get_level() ) { get_event_bus().send<event_type::player_levels_spell>( ...
get_player_character().magic->learn_spell( learn_spell_id, get_player_character(), true );
```

**Fix:** Add a `character_levels_spell` event (character, spell, old_level, new_level, school) fired for every Character, keeping `player_levels_spell` as an avatar-filtered alias. Apply `learn_spells` to the character in the event. Pass the owning Character into `evaluate_opens_spellbook_data`. Fix the doc field name to `school`.

### MAGIC-J06 · ter_furn_transform: no validation, broken shipped data, no field flag matching or per-entry chance
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/magic_ter_fur_transform.cpp:85`, `src/magic_ter_fur_transform.cpp:128`, `src/magic_ter_fur_transform.cpp:224`, `src/magic_ter_fur_transform.cpp:240`, `src/init.cpp:501`, `src/init.cpp:924`, `src/magic.cpp:560`, `data/json/npcs/EOC_talkers/vitrified_farm.json:97`, `data/mods/Xedra_Evolved/mutations/paraclesians/arvore_mutation_spells.json:865`

Spells (86 ter_transform spells), EOC `u_transform_radius` and mapgen all share this type, but it has no `check()`: only `load_transform` and `finalize_all` are registered, and no test covers it. Bad ids surface only as runtime debugmsgs or silent no-ops. Shipped data is already affected. `vitrified_farm.json:97` and `:102` list furniture ids (`f_vitrified_doll`, `f_vitrified_bookcase_talker`) under `"terrain"`, so the item EOCs that use them never match. The bramble transforms put `t_grass_*` in `valid_furniture`. Xedra's arvore spell references `ter_arvore_wood_branches` (the real id is `ter_arvore_wood_branch`). MindOverMatter's result `f_teleporter_attuned_ground` is undefined. On the feature side, field entries can only match by explicit id, and there is no per-entry chance or condition.

The field block (lines 128-137) reads only `valid_field`; `init.cpp` registers only `load_transform` (501) and `finalize_all` (924).

**Fix:** Add `ter_furn_transform::check()` that validates every result and `valid_*` id, and fix the data above. Add optional per-entry `"chance"` and `"condition"` (location as context) and a field `"intensity"` override. Field flag matching would first need a flag set on `field_type`. Messages going only to the avatar are not a bug, since only the avatar has a log.

### MAGIC-J07 · Spell numeric formulas get no spell context; content hardcodes `u_spell_level('own_id')` about 5,300 times
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/magic.cpp:676`, `src/magic.cpp:708`, `src/magic.cpp:728`, `src/magic.cpp:935`, `src/magic.cpp:995`, `src/magic.cpp:1076`, `src/magic.cpp:1374`, `src/talker_character.cpp:458`, `src/talker.h:212`

Every `dbl_or_var` spell field (damage, range, aoe, duration, dot, pierce, energy cost, cast time, field intensity, difficulty, max_level, multiple_projectiles) is evaluated with `const_dialogue d( get_const_talker_for( caster ), nullptr )`: no beta and no context vars. Only the magic_type failure formula gets `spell_id`. So modders write the spell's own id into its formula, e.g. aftershock `telekinesis.json:273` `"( (u_spell_level('afs_telekinetic_hammer') * 1.25) + 18) * ..."`; a grep of data/mods finds 5,304 such literals. That is wrong whenever the caster doesn't know the spell at the cast level. `get_spell_level` returns -1 for unknown spells and the base talker returns 0 for monsters, while item, enchantment, monster and EOC casts build a fresh spell at a forced level. It also breaks under copy-from, because the child inherits the parent's id. Formulas also can't depend on the target. Plain linear scaling already works through `min_X + X_increment * level`, so the real gap is non-linear and target-aware formulas plus the fake-cast and copy-from cases.

```cpp
const_dialogue d( get_const_talker_for( caster ), nullptr );
return type->min_damage.evaluate( d ) + std::round( get_effective_level() * type->damage_increment.evaluate( d ) );
```

**Fix:** Build spell dialogues in one helper (`spell::make_dialogue( caster, target = nullptr )`) that sets context vals `spell_id`, `spell_level` (effective), `spell_max_level` and `spell_school`, and passes the target as beta where one exists (damage_targets, add_effect, heal). Content then writes `"min_damage": { "math": [ "_spell_level * 1.25 + 18" ] }`. Document `_spell_level` in MAGIC.md and the EOC docs.

### MAGIC-J08 · Item, effect and bionic enchantments can't read their source (item vars, effect intensity)
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/magic_enchantment.cpp:347`, `src/magic_enchantment.cpp:368`, `src/magic_enchantment.cpp:943`, `src/magic_enchantment.cpp:967`, `src/character.cpp:4927`

Enchantment conditions and math values are evaluated with only the wearer as alpha. `is_active( guy, parent )` passes just `parent.active` down, and `Character::recalculate_enchantment_cache` calls `force_add( ench, *this )` for every source. An item enchantment can't reference that item's vars, charges, damage or ammo, which blocks designs like a sword whose bonus grows with a `kills` item var, or a relic that weakens as its charges drain. Effects and mutations have a workable but non-generic workaround (`u_effect_intensity('id')`, about 638 uses in data), so the hard gap is items.

```cpp
void enchant_cache::force_add( const enchantment &rhs, const Character &guy ) {
    const_dialogue d( get_const_talker_for( guy ), nullptr );
    force_add_with_dialogue( rhs, d ); }
```

**Fix:** Add overloads that take the source. For items, use the item_location talker as beta so `"values": [ { "value": "MELEE_DAMAGE", "add": { "math": [ "n_kills / 10" ] } } ]` works. For effects, bionics and mutations, set context vals `source_effect`, `source_intensity`, `source_bionic` and `source_trait`. `recalculate_enchantment_cache` already knows each source.

### MAGIC-J09 · Core spellcasting tuning hardcoded: psionic fail caps tied to MoM proficiencies, encumbrance penalties, mana pool/regen, learn time
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/magic.cpp:85-88`, `src/magic.cpp:1345-1358`, `src/magic.cpp:1095-1109`, `src/magic.cpp:1268-1281`, `src/magic.cpp:1383-1398`, `src/magic.cpp:2366`, `src/magic.cpp:2613-2623`, `src/magic.cpp:2627-2634`, `src/magic.cpp:2783-2789`, `src/magic.cpp:2044-2063`, `src/magic_type.h:53-66`

magic_type exposes failure, XP and level formulas, failure cost and EOCs, and energy source, but a lot still lives in C++. The PSIONIC failure path clamps effective skill to 24/31/37/45 using three hardcoded `prof_concentration_*` ids, which is Mind Over Matter content in the engine; it is mostly dead now, since every player-castable MoM power uses a magic_type formula. The hand, leg, arm and mouth encumbrance thresholds and multipliers on cost, cast time and failure aren't moddable. Neither are `mana_base = 1000` with the INT formula, 8-hour full regen, the 30-minute x difficulty learn time, or the 75 base XP. Per-spell cost and cast time already take math, and enchantments plus `spellcasting_adjustment` give partial workarounds, but a mod with a different magic system has to fight these constants.

```cpp
mana_base = 1000;
const float int_bonus = ( ( 0.2f + guy.get_int() * 0.1f ) - 1.0f ) * mana_base;
const double full_replenish = to_turns<double>( 8_hours );
const time_duration base_time = 30_minutes;
```

**Fix:** Extend magic_type with jmath hooks `energy_cost_formula_id`, `casting_time_formula_id`, `learn_time_formula_id`, `max_energy_formula_id` and `regen_formula_id`, plus an `"encumbrance": { "hands_free": 5, "hands_per_point": 10, "legs_free": 10, ... }` object. Delete the PSIONIC/proficiency branch once MoM and Aftershock are confirmed on magic_type formulas.

### MAGIC-J10 · EOCs can't read most spell or enchantment state; cast events lack target and level, and monster casts emit none
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/math_parser_diag.cpp:1938-1952`, `src/talker_character.cpp:133-136`, `src/magic.cpp:2219-2230`, `src/event.h:309-318`, `src/event.h:915-924`, `src/activity_actor.cpp:2847-2855`, `src/magic.cpp:1913-1920`, `src/condition.cpp:2389-2390`

Math exposes spell level, XP, difficulty, counts and school level, but no reader for a spell's failure chance, energy cost, cast time, damage, range, aoe, duration or max level. The only enchantment reader is ARTIFACT_RESONANCE through a dedicated talker method. `character_casts_spell` and `spellcasting_finish` carry no target or level (they do carry cost, cast time and damage). `cast_spell_effect` sends an event only for Character casters, so monster casts are invisible. `failure_eocs` exist only on magic_type and run with just the caster talker and no `spell_id` context; per-spell on-fail logic is possible only through an EVENT EOC on `spellcasting_finish` filtered by id.

```cpp
Character *caster = source.as_character();
if( caster ) { ... send<event_type::character_casts_spell>( ... ) }  // no else
```

**Fix:** Add `u_spell_stat('id', 'damage'|'range'|'aoe'|'duration'|'cost'|'cast_time'|'fail'|'max_level')` and `u_enchantment_value('MAX_MANA', 'type': 'add'|'multiply')`, and give `spellcasting_adjustment` an eval side. Add `target`, `target_char` and `level` to both cast events, add a `monster_casts_spell` event, and add per-spell `"on_fail_eocs"` / `"on_cast_eocs"` with `spell_id` context.

### MAGIC-J11 · `caster_condition` is only enforced for spellbook casts
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/magic.cpp:1156`, `src/handle_action.cpp:1892`, `src/magic_enchantment.cpp:1457`, `src/mattack_actors.cpp:376`, `src/npctalk.cpp:5901`, `src/iuse_actor.cpp:2876`

`valid_caster_condition` is only called from `spell::can_cast`, `Character::cast_spell` and the UI; `cast_all_effects` doesn't check it. Monster, item, enchantment, EOC `u_cast_spell` and `extra_effects` casts all bypass it. This matches how fake casts already skip mana, components and the VERBAL check, and each of those paths has its own gate (the monster actor's `condition`, the enchantment's `condition`, the EOC's condition), so it is largely a design choice. The real gap is that a single fake_spell entry (hit_you_effect, intermittent_activation, extra_effects) can't be gated on its own.

`cast_enchantment_spell` (magic_enchantment.cpp:1457-1486) checks only `one_in( sp.trigger_once_in )`, `is_valid_target` and range.

**Fix:** Add `"condition"` to fake_spell, evaluated at cast time. Optionally document that caster_condition applies only to spellbook casts, or check it in `cast_all_effects` with an opt-out `"ignore_caster_condition": true`.

### MAGIC-J12 · fake_spell levels are int-only; `once_in` and `message` silently ignored outside enchantment hits
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/magic.cpp:3767`, `src/magic.cpp:2321`, `src/magic_enchantment.cpp:1418`, `src/magic_enchantment.cpp:536`, `src/npctalk.cpp:5862`, `src/npctalk.cpp:5886`

`fake_spell::load` reads `min_level`, `max_level` and `once_in` as plain ints, while EOC `u_cast_spell` takes `dbl_or_var` levels, so an artifact's hit_you spell can't scale with skill or an item var. The loader accepts `once_in`, `message` and `npc_message` everywhere, but `cast_extra_spell_effects` ignores all three. `intermittent_activation` ignores `once_in`, `hit_self` and `message`, and its `frequency` is a fixed duration. `u_cast_spell` stores `trigger_once_in` and never tests it. None of this warns. No shipped content currently depends on the ignored fields.

```cpp
for( const fake_spell &extra_spell : type->additional_spells ) {
    spell sp = extra_spell.get_spell( source, get_effective_level() );   // no once_in / message
fake.trigger_once_in = trigger_once_in;   // npctalk.cpp:5886, never tested
```

**Fix:** Make `min_level`, `max_level` and `once_in` `dbl_or_var`, evaluated with the caster dialogue. Add one `fake_spell::try_cast( caster, target )` that honours `once_in`, `hit_self`, `message` and the proposed `condition` (MAGIC-J11), and use it from every fake-spell path. Allow `"frequency": { "math": [ ... ] }`.

### MAGIC-J13 · Spell sound, field lifetime, effect intensity and explosion parameters are derived or hardcoded
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/magic.cpp:1557`, `src/magic.cpp:1552`, `src/magic_spell_effect.cpp:510`, `src/magic_spell_effect.cpp:560`, `src/magic_spell_effect.cpp:1542`

Loudness is always |damage|/3 (plus 1 + damage/3 with LOUD). Spawned fields last `duration_turns`, the same number used for the applied effect, so a 1-hour buff leaves 1-hour fields. `effect_str` effects are applied without an intensity. IGNITE_FLAMMABLE always places `fd_fire` intensity 1 for 10 minutes. The `explosion` effect hardcodes `fire = true` and derives distance factor from aoe/10. Workarounds exist (SILENT plus an extra_effects `noise` spell, a child spell with its own field, EOC `u_explosion` / `u_add_effect`), so these are quality-of-life gaps, made worse because EOC spells get no level context (MAGIC-J01).

```cpp
here.add_field( target, fd_fire, 1, 10_minutes );
explosion_handler::explosion( &caster, target, sp.damage( caster ), sp.aoe( caster ) / 10.0, true );
```

**Fix:** Add `dbl_or_var` fields `min_sound`/`max_sound`/`sound_increment` (defaulting to the current formula), `field_duration`, `effect_intensity` (min/max/increment) and `ignite_field`/`ignite_intensity`/`ignite_duration`. Add an `"explosion": { "power": ..., "distance_factor": ..., "fire": false, "shrapnel": { ... } }` object that reuses `explosion_data::load`.

### MAGIC-J14 · summon / spawn_item effects lack the spawn hooks that `u_spawn_monster` has
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/magic_spell_effect.cpp:1338-1379`, `src/magic_spell_effect.cpp:1217-1271`, `src/magic_spell_effect.cpp:2018-2047`, `src/npctalk.cpp:7765-7958`, `doc/JSON/EFFECT_ON_CONDITION.md:5683-5703`

Summoned monsters always get `friendly = INT_MAX` or 0, with HOSTILE_50 as a hardcoded 50% roll. There is no way to set monster vars, run an EOC per spawned creature or item, or print a spawn message. EOC `u_spawn_monster` covers most of this (mon_variables, lifespan, spawn_message, friendly), which is why severity is low, but neither path runs a per-spawn EOC. Related quirk: summon friendliness ignores the caster, so a hostile monster casting a summon without HOSTILE_SUMMON creates player-friendly summons.

```cpp
if( is_summon_friendly( sp ) ) { spawned_mon.friendly = INT_MAX; } else { spawned_mon.friendly = 0; }
```

**Fix:** Add `"spawn_eocs": [ ... ]` (alpha = spawned thing, beta = caster, context `spell_level`), `"spawn_variables": { "var": "value" }` and `"friendly_chance": { "math": [ ... ] }` to replace HOSTILE_50. Base default friendliness on the caster's attitude. Share the code with `u_spawn_monster`.

### MAGIC-J15 · MAGIC.md drift: phantom fields, flags and values, and undocumented ones
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `doc/JSON/MAGIC.md:271`, `doc/JSON/MAGIC.md:319`, `doc/JSON/MAGIC.md:209`, `doc/JSON/MAGIC.md:364`, `doc/JSON/MAGIC.md:251`, `doc/JSON/MAGIC.md:220`, `doc/JSON/MAGIC.md:1118-1129`, `src/magic.cpp:415-424`, `src/magic.cpp:430-447`, `src/magic_spell_effect.cpp:1289-1303`, `src/magic_spell_effect.cpp:1313-1335`, `src/magic_spell_effect.cpp:573-596`, `src/magic_enchantment.cpp:426-470`, `src/magic.cpp:2952-2975`

The docs describe things that don't exist and skip things that do:
- A spell `condition` field that the loader never reads (the real fields are `caster_condition` / `target_condition`).
- A `LIQUID` spell flag with no implementation.
- Vitamin support for recover_energy, which doesn't exist, while the implemented `HEALTH` mode is missing (code bug: MAGIC-B18).
- SLEEPINESS and PAIN as energy types; they aren't valid, and HP/VITAMIN are omitted.
- Accuracy described as "currently doesn't work"; it is used, just broken (MAGIC-B06).
- `robot_attack` and `wanted` timed events, which aren't accepted (MAGIC-J04).
- A monster enchantment table that omits ARMOR_ALL and the damage/armor mods.
- No mention of HIDDEN_SPELL, NO_CORPSE_QUIET or the eight Magiclysm `*_SPELL` school flags. Those flags exist in the engine enum only to print a description line, so other mods can add string flags but not that description.

**Fix:** Correct the docs. Implement or remove `LIQUID`. Replace the eight school flags with a JSON `spell_category`, or use the magic_type name/description in the spell info.

### MAGIC-J16 · `hit_you_effect` fires only on melee (including misses); no ranged, kill or dodge triggers
**Severity:** Low · **Effort:** Medium · **Verified:** plausible
**Where:** `src/character.cpp:5132`, `src/melee.cpp:953`, `src/monster.cpp:2403`, `src/magic_enchantment.cpp:1457`, `src/ranged.cpp:1236`, `src/event.h:463`

`Character::did_hit` is the only caller of `cast_hit_you`, and it is only called from `melee.cpp:953` (monsters likewise only from monster melee). Guns, bows, throws and attack spells never trigger an item's hit_you_effect. This is documented as melee-only (MAGIC.md:751), and EVENT EOCs on `character_ranged_attacks_*` (which carry weapon and ammo ids) are a workaround. There are no kill, dodge, block or crit triggers, and `once_in` is the only gate. A related bug: `did_hit(t)` sits outside the hit/miss block (718-926), so a character's hit_you_effect also fires on melee misses; monsters guard this with `hitspread >= 0`.

```cpp
void Character::did_hit( Creature &target ) { enchantment_cache->cast_hit_you( *this, target ); }
if( !one_in( sp.trigger_once_in ) ) { return; }   // the only gate
```

**Fix:** Move `did_hit` inside the hit branch. Add enchantment triggers `hit_you_ranged_effect`, `on_kill_effect`, `on_dodge_effect` and `on_block_effect`, or a single `"triggers": [ { "on": "ranged_hit", "spell": { ... }, "condition": { ... } } ]` list, with alpha = owner, beta = other creature and context `damage` / `damage_type`.

### MAGIC-J17 · Spell effects hardcode vanilla content (slime_split, guilt, fd_fatigue, polymorph exclusions, push damage)
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/magic_spell_effect.cpp:98-107`, `src/magic_spell_effect.cpp:2049-2120`, `src/magic_spell_effect.cpp:1708-1794`, `src/magic_spell_effect.cpp:991-1059`, `src/magic_spell_effect.cpp:742-744`, `src/magic_spell_effect.cpp:1106-1108`, `src/magic_spell_effect.cpp:1182-1185`, `src/magic_spell_effect.cpp:1677`, `src/magic_spell_effect.cpp:1627`, `doc/JSON/MAGIC.md:185-186`, `doc/JSON/MAGIC.md:215`

- slime_split hardcodes `mon_blob*`, the SLIME species, mass = speed and the size odds.
- guilt hardcodes the PRED1-4, PSYCHOPATH and NUMB flags, a x5 multiplier for PACIFIST, the -5 x level malus and the threshold text.
- remove_field special-cases `fd_fatigue` with fixed teleglow durations, 10 hurtall and a random teleport.
- Random polymorph excludes `mon_generator` and the HALLUCINATION species by id.
- directed_push collision damage is an untyped 10 or 4 per tile.
- revive_dormant uses a fixed radius of 3, and area_pull/push move exactly one tile.

Most of this is documented as vanilla-specific, with configurable alternatives such as directed_push, so the impact is low.

```cpp
if( caster_monster && caster_monster->type->id == mon_blob_brain ) { mass += mass; }
if( fd.first.id() == fd_fatigue ) { handle_remove_fd_fatigue_field( field_removed, caster ); }
mon->apply_damage( &caster, bodypart_id(), dist_left * 10 );
```

**Fix:** Drive slime_split from JSON (`"split_data": { "mass_stat": "speed", "tiers": [ { "monster": "mon_blob_large", "min_mass": 70, "chance": 33 } ] }`). Give field_type an `on_dispel_eoc`. Add a monster `NO_RANDOM_POLYMORPH` flag, character flags such as GUILT_IMMUNE / GUILT_REDUCED, and spell fields `collision_damage`, `collision_damage_type`, `pull_distance` and `revive_radius`.

### MAGIC-J18 · Monsters get a narrow enchantment subset; ench_effects/emitter/intermittent are Character-only; emitters don't stack
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/magic_enchantment.cpp:426-470`, `src/monster.cpp:3601-3614`, `src/magic_enchantment.cpp:1388-1425`, `src/magic_enchantment.cpp:921-925`, `src/magic_enchantment.cpp:1036-1040`, `src/mtype.h:448`

Monsters only gather enchantments from their effects, filtered by `is_monster_relevant()`. mtype has no `enchantments` field. `ench_effects`, `emitter` and `intermittent_activation` are applied only in `activate_passive( Character & )`, so a "burning aura" enchantment does nothing on a monster. Native `emit_fields` on mtype and spell special attacks soften this. Within a Character's cache, `emitter` is a single optional (the last one wins) and `ench_effects` is a map where the first one wins, so two items that grant an emitter or the same effect don't stack.

```cpp
ench_effects.insert( ... );
if( rhs.emitter ) { emitter = rhs.emitter; }
```

**Fix:** Add an `"enchantments"` field to MONSTER, plus `activate_passive( monster & )` for ench_effects, emitter and intermittent_activation. Store emitters as a vector, and take the max (or optionally the sum) for ench_effects intensity.

## 2. Bugs, oversights & quirks

### MAGIC-B01 · HP-cost spells can softlock the game: the blood_magic menu loops forever when no limb qualifies
**Severity:** Critical · **Effort:** Small · **Verified:** confirmed
**Where:** `src/magic.cpp:2734`, `src/magic.cpp:1862`, `src/magic.cpp:1869`, `src/magic.cpp:1878`, `src/magic.cpp:1905`, `src/character_health.cpp:2653`, `src/activity_actor.cpp:2873`

`has_enough_energy()` checks HP cost against every part in `get_body()`, including the minor parts (eyes, mouth, hands, feet). Those have base_hp 60 and are never damaged, because `apply_damage` only hurts `main_part`. `blood_magic()` lists only main parts, enables a part only if `hp_cur > cost`, and loops `while( action < 0 )`. When every main limb is at or below the cost, every entry is disabled and Escape returns UILIST_CANCEL (-1027), so the loop never ends. Example: a Magiclysm druid casts druidic_healing (35 HP) while every main limb is at 35 HP or less, which is exactly when you'd heal. HP lost during the cast can trigger it too. `consume_spell_cost` also runs for NPC casters, so an NPC blood caster opens the modal menu for the player.

```cpp
for( ... elem : guy.get_body() ) { if( elem.second.get_hp_cur() > cost ) return true;   // 2735
uilist_entry entry( i, hp_cur > cost, ...                                                 // 1869
while( action < 0 ) { action = uilist( ... ); }                                           // 1878
```

**Fix:** In `has_enough_energy`, check only main parts (`get_all_body_parts( only_main )` with `get_part_hp_cur( bp ) > cost`). Make `blood_magic` return a bool and allow cancel. For non-avatar casters, pick the healthiest eligible part automatically. Have `consume_spell_cost` fail cleanly when no part qualifies.

### MAGIC-B02 · Blast spells hit through walls: the computed projectile epicenter is thrown away
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/magic_spell_effect.cpp:457`, `src/magic_spell_effect.cpp:472`, `src/magic_spell_effect.cpp:477`, `src/magic_spell_effect.cpp:257`, `src/magic.cpp:835`, `src/ranged.cpp:3404`

For blast spells without NO_PROJECTILE, `calculate_spell_effect_area` walks the line from caster to target and moves the epicenter to the last tile before a wall. That epicenter is used only when aoe < 1; for any real AoE, line 477 calls `effect_area` on the original target, and blast LOS is then checked from that target. Ground-targetable spells skip the sight check in `select_target`, and the targeting UI doesn't clamp at walls, so a fireball (blast, ground target, aoe 3-5) aimed past a wall detonates in the room on the far side.

```cpp
if( sp.shape() == spell_shape::blast && !sp.has_flag( spell_flag::NO_PROJECTILE ) ) { ... epicenter = *( iter - 1 );
std::set<tripoint_bub_ms> targets = { epicenter };                 // 472
targets = sp.effect_area( caster.pos_bub(), target, caster );      // 477 – epicenter unused
```

**Fix:** Pass `epicenter` instead of `target` to `effect_area` at line 477 (cone and line already start at the caster). Apply the same truncation in `target_ui` so the preview matches.

### MAGIC-B03 · Multi-projectile, SPLIT_DAMAGE and PERCENTAGE_DAMAGE hits on characters credit the victim
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/magic_spell_effect.cpp:621`, `src/magic_spell_effect.cpp:629`, `src/magic_spell_effect.cpp:635`, `src/character_health.cpp:3035`, `src/npc.cpp:3379`

In `damage_targets`, the character branch passes `cr` (the target) as the damage source on the multishot, SPLIT_DAMAGE and PERCENTAGE_DAMAGE paths. Only the plain path and the monster branch pass `&caster`. `apply_damage` then sets the victim as its own killer. `npc::die` emits `character_kills_character` with the NPC as killer and skips `apply_murder_penalties`, damage events and ondamage EOCs see the wrong source, and `npc::on_attacked` never fires against the caster. About 125 data entries use SPLIT_DAMAGE, including fireball, so killing an NPC with Fireball counts as a suicide: no hostility, no faction or morale consequence.

```cpp
cr->deal_projectile_attack( &here, cr, atk, atk.missed_by, true );   // 621
cr->deal_damage( cr, bp_id, atk.proj.impact );                       // 629, 635
// vs 639 / 649: deal_projectile_attack( &here, &caster, ...
```

**Fix:** Pass `&caster` in all three calls. While there, apply the mitigation multiplier and bodypart filter consistently: the PERCENTAGE path overwrites the mitigated amount and calls `deal_damage` once per damage unit inside the inner loop.

### MAGIC-B04 · `min_pierce` / `max_pierce` / `pierce_increment` are loaded and documented but never applied
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/magic.cpp:485`, `src/magic.h:317`, `src/magic.cpp:2149`

MAGIC.md (lines 99-101, 253) documents pierce as armour of the spell's damage type that the spell ignores, and about 11 shipped spells (Magiclysm druid, technomancer and earthshaper, monster spells, XedraWood) set `max_pierce`. The fields are only ever read at load time. `get_damage_instance()` adds damage with no armour penetration, so every armour-piercing spell does full armour-mitigated damage.

```cpp
dmg.add_damage( dmg_type(), damage( caster ) );   // magic.cpp:2152 – no res_pen
```

**Fix:** Add `spell::armor_penetration( caster )` using the same min/increment/max clamp as the other stats, call `dmg.add_damage( type, amount, armor_penetration )`, and show it in the spell info UI.

### MAGIC-B05 · Spells whose damage falls with level sit at max_damage from level 0 when min_damage >= 0
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/magic.cpp:738`, `src/magic.cpp:760`, `src/magic.cpp:779`, `src/magic.cpp:675`

`spell::damage()` takes the "increasing" branch whenever `min_damage >= 0`, even if `max_damage < min_damage`, so it returns `max_damage` at every level. range, aoe and duration test only `max >= min` and handle decreasing curves correctly. `accuracy()` and `damage_dot` share the bug, and `field_intensity()` always uses `min()`. Shipped content affected: Magiclysm move_earth (100 -> 1) always transforms with a 1/1 chance, earthshaper_pillar_side_effect is stuck at 20, and Aftershock afs_telekinetic_force_shove_toward (0 -> -30) always pulls 30 tiles from level 0.

```cpp
if( type->min_damage.evaluate( d ) >= 0 ||
    type->max_damage.evaluate( d ) >= type->min_damage.evaluate( d ) ) {
    return std::min( leveled_damage, max_damage ...
```

**Fix:** Use the range/aoe rule: if max >= min, `min( leveled, max )`, else `max( leveled, max )`. Drop the `min >= 0 ||` term in `damage()`, `accuracy()` and `damage_dot`, give `field_intensity()` the same two-way clamp, and add a unit test for decreasing curves.

### MAGIC-B06 · Spell accuracy mitigation is broken: precedence bug plus inverted formula
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/magic_spell_effect.cpp:582`, `src/magic_spell_effect.cpp:584`, `src/magic_spell_effect.cpp:587`, `src/magic_spell_effect.cpp:593`, `src/creature.cpp:1820`

`const int spell_block = cr->get_block_bonus() - spell_accuracy > 0` parses as `(block - acc) > 0`, so the value is 0 or 1 and `max( roll, spell_block )` is always the d20 roll; spell_resist has the same bug. The formula `(1 - 0.05 * x) / 3` also shrinks as the score grows, and goes negative (increasing damage) once dodge minus accuracy exceeds 20. In practice, with accuracy defaulting to 20, the block and resist branches rarely fire. The main live effect is that `dodge_check()` runs uncanny dodge and `on_try_dodge` side effects on spell hits.

```cpp
if( const int spell_block = cr->get_block_bonus() - spell_accuracy > 0 ) {
    damage_mitigation_multiplier -= ( 1 - 0.05 * std::max( roll, spell_block ) ) / 3.0;
```

**Fix:** Compute `const int margin = score - accuracy;` separately. If margin > 0, mitigate by `0.05 * std::min( roll, margin ) / 3.0`. Use a side-effect-free dodge comparison instead of `dodge_check()`, then update the MAGIC.md note.

### MAGIC-B07 · `spell_migration` can never migrate a removed spell: the save loader discards unknown ids first
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/magic.cpp:2399`, `src/magic.cpp:2409`, `src/magic.cpp:3902`, `src/magic.cpp:3907`, `src/savegame_json.cpp:797`, `src/savegame_json.cpp:798`, `doc/JSON/OBSOLETION_AND_MIGRATION.md:188`

The doc says `from` is "the spell that has been removed". `known_magic::deserialize` skips any invalid spell id before `migrate_spells()` runs, and `migrate_spells` iterates only the filtered spellbook. A migration from a truly removed spell therefore never fires, and the player loses the spell and its XP. It works only if the old spell JSON is kept, which the doc's "Spells" section (line 377) happens to tell modders to do. Hotkeys and favourites are also not remapped. Nothing in data uses spell_migration yet.

```cpp
if( !sp.is_valid() ) { DebugLog(...) << "Tried to load bad spell"; continue; }   // 2399
data.read( "magic", magic ); magic->migrate_spells();                             // savegame_json.cpp:797
```

**Fix:** In `deserialize`, call `spell_migration::find_migration( sp )` before the validity check, insert the new id with the saved XP (or drop it if there's no target), and remap invlets and favourites.

### MAGIC-B08 · pain_split averages in never-damaged minor body parts, so it acts as a large heal
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/magic_spell_effect.cpp:209`, `src/magic_spell_effect.cpp:216`, `src/magic_spell_effect.cpp:220`, `src/magic_spell_effect.cpp:228`, `src/character_health.cpp:2653`, `src/creature.cpp:2876`

`pain_split` sums HP over `get_body()`, which includes minor parts that always stay at 60 HP. With all six main limbs at 10 HP, the average is (6*10 + 6*60)/12 = 35, and the unclamped `set_all_parts_hp_cur` heals about 150 HP. The data comment says the spell "does not actually heal anything", and the code comment that broken limbs are excluded is false. The message uses plain `add_msg`, so the player sees it when an NPC casts. Magiclysm only (learnable, and an extra effect of Cleansing Flame).

```cpp
for( ... elem : you->get_body() ) { ... num_limbs++; total_hp += elem.second.get_hp_cur(); }
you->set_all_parts_hp_cur( hp_each );
```

**Fix:** Iterate `get_all_body_parts( only_main )`, skip broken limbs if that is the intent, set each part with `set_part_hp_cur` clamped to its max, and use `add_msg_if_player`.

### MAGIC-B09 · Summon spells raise "failed to place monster" debugmsgs when the AoE includes the caster's tile
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/magic_spell_effect.cpp:1381`, `src/magic_spell_effect.cpp:1396`, `src/magic_spell_effect.cpp:472`, `src/magic.cpp:1663`, `src/game.cpp:4432`, `src/game.cpp:4446`, `src/magic.cpp:2262`

`spawn_summoned_monster` picks random tiles from the area filtered by `is_valid_target`. With `valid_targets: [ "ground", "self" ]` (small_raptor_spawn cast by pupating zombies, dermatik_emergence, wraith_summon_shadow and others), the living caster's tile counts as valid. `place_critter_at` refuses occupied tiles and tiles that fail `will_move_to` / `know_danger_at` (fire, deep water), and each refusal raises a player-visible debugmsg. With random picks over 50-80 tiles, this hits a few percent of casts in normal play.

```cpp
if( add_summoned_mon( *iter, summon_time, sp, caster ) ) { ... } else { debugmsg( "failed to place monster" ); }
```

**Fix:** Before picking, drop tiles that hold a creature or can't take the monster, treat a failed placement as a silent skip (`add_msg_debug`), and optionally fall back to `place_critter_around` with radius 1.

### MAGIC-B10 · Mana regeneration is floored per 3-minute tick, so low max mana never regenerates
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/magic.cpp:2627`, `src/magic.cpp:2632`, `src/character_body.cpp:253`, `src/character_body.cpp:254`, `src/magic.cpp:2615`

`update_mana` runs every 3 minutes with 180 turns against an 8-hour refill (28,800 turns), and gains `std::floor( regen_max / 160 )`. A REGEN_MANA-adjusted max below 160 never regenerates; between 160 and 319 up to half the regen is lost. With `mana_base` 1000, INT 4 loses about 25% of its regen and INT 1 about 47%. This also hits characters with many bionics and anyone whose enchantments lower MAX_MANA or REGEN_MANA.

```cpp
const double full_replenish = to_turns<double>( 8_hours );
mod_mana( guy, std::floor( ratio * std::max( 0.0, guy.calculate_by_enchantment( ... REGEN_MANA ) ) ) );
```

**Fix:** Use `roll_remainder()`, or carry a float remainder between ticks.

### MAGIC-B11 · Channeling follows `magic->last_spell`, which only the player's cast menu sets and which isn't saved
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/magic.cpp:2658`, `src/magic.cpp:2660`, `src/magic.cpp:2678`, `src/magic.cpp:2705`, `src/magic.h:789`, `src/activity_actor.cpp:2913`, `src/handle_action.cpp:1877`

The spellcasting activity adds `magic_channeling` for any caster and any channeling spell, but `channel_magic()` and `break_channeling()` look the spell up through `last_spell`. Only the player's cast menu sets it, and it isn't serialized. So NPCs never actually channel. A channeling spell cast from an item, scroll or EOC fires whatever menu spell was cast last. Save/load stops channeling silently. Casting another menu spell while channeling overwrites `last_spell` before `break_channeling` runs, so the original spell's interrupt penalty never fires, a free way to dodge it. Channeling is currently used only by Isolation-Protocol.

```cpp
spell_id sp_id = guy.magic->last_spell;                       // 2660
spell_id last_spell = spell_id::NULL_ID(); // NOLINT(cata-serialize)   magic.h:789
```

**Fix:** When the activity applies `magic_channeling`, store a separate serialized `channeled_spell` (id plus effective level) on `known_magic`, and use it in `channel_magic` and `break_channeling`.

### MAGIC-B12 · Trait and bionic spell grants and removals: player-attributed messages and events, debugmsg cascades, levels past max
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/magic.cpp:2499`, `src/magic.cpp:2505`, `src/magic.cpp:2507`, `src/magic.cpp:3477`, `src/magic.cpp:3482`, `src/magic.cpp:3488`, `src/magic.cpp:3491`, `src/bionics.cpp:2982`

(1) `forget_spell()` always calls the global `add_msg` and sends `character_forgets_spell` with the avatar's id. When an NPC loses a spell-granting trait or CBM, the player gets the message and EOCs run on the wrong character. (2) `on_mutation_loss` calls `get_spell()` without a `knows_spell` guard, so an already-forgotten spell triggers "Tried to get unknown spell" and then "Can't forget a spell you don't know!". (3) `on_mutation_gain` adds levels with raw `gain_level()`, which has no cap, so a trait granting 5 levels of a spell already at 18/20 leaves it at 23.

```cpp
get_event_bus().send<event_type::character_forgets_spell>( get_player_character().getID(), sp->id );
spell &temp_sp = get_spell( sp.first );                                         // no knows_spell guard
for( int level = 0; level < sp.second; level++ ) { temp_sp.gain_level( guy ); }
```

**Fix:** Give `forget_spell` a `Character &` and use `add_msg_if_player` and `guy.getID()`. Guard `on_mutation_loss` with `knows_spell`. Use the capped `gain_levels()` in `on_mutation_gain`.

### MAGIC-B13 · Targeted spell casts by NPCs assume the NPC knows the spell
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/magic.cpp:847`, `src/magic.cpp:849`, `src/magic.cpp:860`, `src/npc_attack.cpp:134`, `src/magic.cpp:2577`, `src/magic.cpp:2580`, `src/npctalk.cpp:5903`

The NPC branch of `select_target` builds `npc_attack_spell( id() )`, whose evaluate, can_use and evaluate_tripoint all call `source.magic->get_spell( attack_spell_id )`. For a spell the NPC doesn't know, such as an EOC `npc_cast_spell` with `"targeted": true`, this pops "Tried to get unknown spell", rates the static null spell, and cancels the cast. Shipped EOCs do this: base-game EOC_GENERIC_SPELL_MUTATION_ACT and the `*_real` spells in MoM and Aftershock. Monsters fall through to a TODO and always target their own tile.

```cpp
npc_attack_spell npc_spell( id() );                                      // magic.cpp:849
const spell &attack_spell = source.magic->get_spell( attack_spell_id );  // npc_attack.cpp:134
```

**Fix:** Let `npc_attack_spell` take a spell instance (or evaluate `*this`) instead of looking the id up. For monsters, default to `attack_target()`.

### MAGIC-B14 · SWAP_POS dereferences a null creature and does a raw double setpos that collides for monster casters
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/magic_spell_effect.cpp:195`, `src/magic_spell_effect.cpp:199`, `src/magic_spell_effect.cpp:200`, `src/magic_spell_effect.cpp:704`, `src/creature_tracker.cpp:42`, `src/creature_tracker.cpp:124`, `src/game.cpp:4767`

`attack()` calls `swap_pos( caster, epicenter )` after damage with no check that a creature is still there. `creature_at` returns nullptr for dead monsters, hallucinations and empty tiles, so a SWAP_POS spell that kills its target, allows ground, or is cast by an EOC at an empty tile crashes. A monster-on-monster swap moves onto an occupied tile; `update_pos` then debugmsgs and returns false, leaving the tracker inconsistent. No shipped SWAP_POS spell hits these cases, so it currently needs modder content.

```cpp
Creature *const critter = get_creature_tracker().creature_at<Creature>( target );
critter->setpos( caster.pos_abs() );   // no null check
```

**Fix:** Null-check `critter`, skip if it is dead or the caster itself, and use `g->swap_critters( caster, *critter )`, which already handles the tracker and view updates.

### MAGIC-B15 · RANDOM_TARGET is ignored by most cast paths (monster, enchantment, relic, non-targeted EOC)
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/magic.cpp:2268`, `src/magic.cpp:2299`, `src/magic.cpp:861`, `src/magic.cpp:2325`, `src/magic_enchantment.cpp:1423`, `src/mattack_actors.cpp:413`, `src/relic.cpp:507`, `src/npctalk.cpp:5911`

Only `select_target()`, WONDER sub-spells and `cast_extra_spell_effects` re-pick a target for RANDOM_TARGET. The non-WONDER path of `cast_all_effects` casts at whatever target it is given, so monster mattacks, intermittent_activation (always `guy.pos_bub()`), hit_me/hit_you, relic activation and non-targeted `u_cast_spell` cast "zap a random nearby enemy" spells onto the caster's tile. `legacy_artifact_active.json` even notes a RANDOM_TARGET spell is disabled from procgen "as it always casts on the player's position", yet procgen still uses AEA_SLEEPINESS. RANDOM_CRITTER on its own does nothing anywhere, since every check tests only RANDOM_TARGET; this breaks `mutation_spells.json:180` and MoM `electrokinesis.json:546`.

```cpp
cast_spell_effect( source, target ); cast_extra_spell_effects( source, target );   // no RANDOM_TARGET check
fake.get_spell( guy, 0 ).cast_all_effects( guy, guy.pos_bub() );                   // magic_enchantment.cpp:1423
```

**Fix:** Handle RANDOM_TARGET and RANDOM_CRITTER at the top of `cast_all_effects( Creature &, target )`: replace the target with `random_valid_target( source, target )`, or abort with a message if there is none. Remove the duplicate handling from callers.

### MAGIC-B16 · Enchantment and spell JSON loading breaks copy-from, extend and duplicate entries; shipped values silently dropped
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/magic_enchantment.cpp:223`, `src/magic_enchantment.cpp:229`, `src/magic_enchantment.cpp:529`, `src/magic_enchantment.cpp:553`, `src/magic_enchantment.cpp:563`, `src/magic_enchantment.cpp:566`, `src/magic_enchantment.cpp:567`, `src/magic_enchantment.cpp:573`, `src/magic_enchantment.cpp:921`, `src/magic_enchantment.cpp:924`, `src/magic_enchantment.cpp:1412`, `src/magic.cpp:431`, `src/magic_type.cpp:62`, `src/magic_type.cpp:75`, `src/generic_factory.cpp:15`

Enchantment `values`, skills and damage maps load with `std::map::emplace`, which never overwrites. Two entries for the same value in one enchantment (inline ones included) keep only the first, and a copy-from child can't override an inherited value. Shipped data already loses entries. `effect_hound_of_tindalos_slobber` (data/json/effects.json:5660) lists 7 per-bodypart ARTIFACT_RESONANCE adds and only the torso one applies. Magiclysm dark_sight loses its second NIGHT_VIS add, and XE_REVENANT_TRAITS loses its "cut" multiply -0.5. `has` (default HELD) and `condition` (default ALWAYS) are reset on every load, so a copy-from child loses WORN/WIELD and dialog conditions. hit_you, hit_me and emitter use raw `jo.read` (no extend/delete), ench_effects is first-wins and the cached emitter is last-wins. Spell `flags` use raw `get_string_array`, so `"extend": { "flags": [...] }` is silently ignored: 8 Magiclysm Void_Mage `horizon_vortex_*` spells lose RANDOM_DAMAGE this way. magic_type reads `failure_eocs` only when `!was_loaded`, and `cannot_cast_flags` is raw. No data enchantment uses copy-from, which is a symptom of this.

```cpp
add_map.emplace( value, add );                                                          // 223
active_conditions.first = io::string_to_enum<has>( jo.get_string( "has", "HELD" ) );    // 553
} else { active_conditions.second = condition::ALWAYS; }                                // 565
if( jo.has_array( "flags" ) ) { for( auto &flag : jo.get_string_array( "flags" ) ) { flags.insert( flag );
```

**Fix:** Sum duplicate entries within one object (or debugmsg on them) and use `operator[]` assignment for inherited values. Use `optional( jo, was_loaded, ... )` with enum and container readers for has, condition, flags, cannot_cast_flags, failure_eocs, hit_you_effect, hit_me_effect and ench_effects, so `extend`/`delete` work. Store emitters as a vector. Document copy-from/extend support for enchantments.

### MAGIC-B17 · area_expander has no limit at aoe 0, expands highest-cost first, and remove_field skips the target tile
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/magic_spell_effect.cpp:843`, `src/magic.h:1010`, `src/magic_spell_effect.cpp:967`, `src/magic_spell_effect.cpp:975`, `src/magic_spell_effect.cpp:982`, `src/magic_spell_effect.cpp:1055`, `tests/magic_spell_effect_test.cpp:90`

(1) `run()` checks range only when `max_range > 0`, so an aoe-0 area_pull, area_push or remove_field flood-fills the whole reality bubble (about 17k nodes); every live spell currently has aoe >= 1. (2) The frontier comparator `cost <` makes `std::priority_queue` a max-heap, so the "from" tree isn't shortest-path and IGNORE_WALLS is never consulted. (3) remove_field skips the centre node, so the field on the targeted tile is never removed. It also copies the tile's whole field and reports every field type there as "dissipated". Removing a single tile per cast is intentional and tested.

```cpp
if( max_range > 0 && center_range > max_range ) { continue; }   // 843
return area[a].cost < area[b].cost;                             // magic.h:1010
if( node.from == node.position ) { continue; }                  // 967
```

**Fix:** Treat max_range 0 as radius 0, use `>` for a min-heap, honour IGNORE_WALLS, include the centre tile in remove_field, and copy only the removed `field_entry`.

### MAGIC-B18 · recover_energy: negative BIONIC drains stamina instead of power
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/magic_spell_effect.cpp:1292`, `src/magic_spell_effect.cpp:1296`, `src/magic_spell_effect.cpp:1307`, `doc/JSON/MAGIC.md:209`

A copy-paste bug: with `effect_str` BIONIC and negative damage (a power-drain spell), the else branch calls `mod_stamina`, so the target loses stamina and keeps its bionic power. `mod_power_level` handles negatives fine. The vitamin mode in MAGIC.md:209 doesn't exist and hits the "Invalid effect_str" debugmsg, and `HEALTH` works but is undocumented (see MAGIC-J15). The only shipped BIONIC user (Magiclysm `technomancer.json:94`) uses positive damage, so the bug is latent.

```cpp
} else if( energy_source == "BIONIC" ) { if( healing > 0 ) { you->mod_power_level( ... ); } else { you->mod_stamina( healing ); }
```

**Fix:** Use `mod_power_level( units::from_kilojoule( healing ) )` for both signs. Optionally add a `VITAMIN:<id>` form that calls `vitamin_mod`, and document HEALTH.

### MAGIC-B19 · AoE loops use the epicenter `target` instead of the per-tile variable (EOC vehicles, fertilize_plant, vomit)
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/magic_spell_effect.cpp:2031`, `src/magic_spell_effect.cpp:1471`, `src/magic_spell_effect.cpp:1472`, `src/magic_spell_effect.cpp:1531`

In `effect_on_condition`, `veh_at( target )` is evaluated per AoE tile, so vehicle tiles away from the centre talk to the centre vehicle or fall to the null-alpha branch. In `fertilize_plant`, the missing-seed branch clears items and furniture on `target` (possibly a healthy plant) instead of `tile`, then `return`s and abandons the rest of the AoE. `vomit` plays its sound at `target` for every victim. No current data hits the vehicle case, and the seed branch needs an already-corrupt map.

```cpp
optional_vpart_position veh = here.veh_at( target );   // inside for( potential_target : area )
debugmsg( "Missing seed for plant at %s", target.to_string() ); here.i_clear( target ); here.furn_set( target, furn_str_id::NULL_ID() ); return;
```

**Fix:** Use `potential_target` and `tile` respectively, and replace `return` with `continue`.

### MAGIC-B20 · Every "attack" spell hit adds a damage-over-time record even when the spell has no DoT
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/magic_spell_effect.cpp:665`, `src/magic_spell_effect.cpp:692`, `src/magic_spell_effect.cpp:695`, `src/magic.cpp:787`, `src/creature.cpp:3501`, `src/savegame_json.cpp:4244`

The DoT block in `damage_targets` runs for every creature hit with no `damage_dot` check. Buffs, heals and plain nukes all append a zero-amount record that lasts as long as the spell's duration, is processed each turn and is saved. The per-turn cost is trivial, so the real effect is minor save bloat from long-duration buff-style attack spells.

```cpp
cr->add_damage_over_time( sp.damage_over_time( { bodypart_str_id::NULL_ID().id() }, caster ) );   // no damage_dot != 0 guard
```

**Fix:** Wrap the block in `if( sp.damage_dot( caster ) != 0 && sp.duration( caster ) > 0 )`. Also guard SPLIT_DAMAGE against an empty `target_bdpts`; the float division by zero there doesn't crash, but `roll_remainder` then converts inf to int.

### MAGIC-B21 · `field_id` and IGNITE_FLAMMABLE only work for "attack" and "dash"
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/magic.cpp:1534`, `src/magic_spell_effect.cpp:548`, `src/magic_spell_effect.cpp:558`, `src/magic_spell_effect.cpp:703`, `src/magic_spell_effect.cpp:1896`, `doc/JSON/MAGIC.md:257`

MAGIC.md presents `field_id`, `field_chance` and the intensity fields as generic, but `spell::create_field` and IGNITE_FLAMMABLE are reached only from `damage_targets` (attack, dash) and dash itself. Every other effect ignores them without a warning. Even for attack, fields spawn only on tiles that pass `is_valid_target`, so a hostile-only spell can't leave fields on empty ground. The only live case is MindOverMatter pyrokinetic_intensify_flames, whose IGNITE_FLAMMABLE flag is dead. The cited Xedra sylph spell is unreferenced, and chaining an "attack" extra effect works around the problem.

`create_field` call sites: `magic_spell_effect.cpp:558` (damage_targets) and `:1896` (dash) only.

**Fix:** Move the field/ignite step into a shared post-effect pass in `spell::cast_spell_effect` over the effect area for every effect type, or warn in `check_consistency` when `field_id` is set on an effect that ignores it.

## 3. Performance

### MAGIC-P01 · Spellbook UI builds three temporary avatars and fires up to ~85 level-up events per redraw, which can teach the player spells
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/magic.cpp:306`, `src/magic.cpp:323`, `src/magic.cpp:3709`, `src/magic.cpp:3714`, `src/magic.cpp:1450`, `src/magic.cpp:3840`, `src/magic.cpp:3846`, `src/magic.cpp:2209`, `src/magic.cpp:2238`, `src/magic.cpp:2313`, `src/avatar.h:129`, `data/mods/Magiclysm/Spells/stormshaper.json:161`

On every redraw of the "Study a spell" menu, `draw_spellbook_info` handles RANDOM_DAMAGE spells (about 450 in data) by building three `avatar` objects, each with full Character construction and a `map_memory`, and calling `gain_levels( guy, 84 )`. The temp avatar's `is_avatar()` is true, so every level sends `player_levels_spell`. stats_tracker records it, EVENT EOCs run with the real avatar as alpha, and `spell_events::notify` copies the spell_type twice and calls `learn_spell` on the real player. Windstrike is RANDOM_DAMAGE with max_level 15 and `learn_spells: storm_shipwrecker: 15`, and it is the pre-selected entry of its scroll, so opening that scroll teaches Storm Shipwrecker to any player without an opposing class trait. Severity reflects this gameplay bug; the perf cost alone would be low. Casterless `cast_all_effects( target )` similarly builds a fresh `avatar fake_avatar` at each level of extra effects.

```cpp
avatar guy; const_dialogue d( get_const_talker_for( guy ), nullptr ); spell cur_spell( id ); cur_spell.gain_levels( guy, 84 );
if( guy.is_avatar() && oldLevel != get_level() ) { get_event_bus().send<event_type::player_levels_spell>( ...
```

**Fix:** Compute the RANDOM_DAMAGE row arithmetically from min/max/increment at max_level using the real player's dialogue, as `draw_minmax_row` already does, or memoize it per spell_type while the menu is open. Never route display math through `gain_exp`; add a silent `set_level`. Build a single fake caster in `cast_all_effects( target )` and pass it down.

### MAGIC-P02 · NPC spell evaluation is O(range^2 x AoE cost) with per-tile allocations every combat turn
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/magic.cpp:949`, `src/magic.cpp:968`, `src/npc_attack.cpp:130`, `src/npc_attack.cpp:140`, `src/npc_attack.cpp:186`, `src/npc_attack.cpp:218`, `src/magic_spell_effect.cpp:450`, `src/magic.cpp:716`, `src/npcmove.cpp:2648`, `src/magic.cpp:851`

`npc::method_of_attack -> evaluate_best_attack` runs every NPC combat turn and calls `npc_attack_spell::evaluate` for every known spell. `spell::targetable_locations` visits every tile in the range square and allocates a `line_to` vector per tile for the obstruction check. For ground-targetable spells (Magiclysm fireball, cone_cold, which NPCs get from `npc/classes.json`), every candidate tile then runs `calculate_spell_effect_area`, including aoe/range dialogue evaluations and the shape function (MAGIC-P04, MAGIC-P05). For each creature in each area it calls `spell::dps()`, which ignores its target and recomputes cast time, failure, damage, dot and duration with about 10 dialogue allocations. Casting re-runs the whole evaluation. The realistic cost is roughly 1M `passable()` checks and over 100k allocations per spell, per NPC, per combat turn.

```cpp
for( const tripoint_bub_ms &query : here.points_in_radius( char_pos, range( source ) ) ) { if( !ignore_walls && has_obstruction( query ) ) ...
for( const tripoint_bub_ms &targetable_point : targetable_points ) { ... evaluate_tripoint( source, target, targetable_point );
float spell::dps( const Character &caster, const Creature & ) const
```

**Fix:** (a) Replace `line_to` + `impassable` with an allocation-free check (the bresenham functor in `line.h:151`, or the cached-transparency `map::sees`/`clear_path`). (b) Compute `dps` once per spell evaluation. (c) Skip candidate tiles with no creature within aoe range. (d) For cone and line shapes, evaluate one candidate per distinct direction.

### MAGIC-P03 · Per-turn enchantment rebuild walks the whole inventory once per relic enchantment (`has_item`)
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/magic_enchantment.cpp:347`, `src/character.cpp:4932`, `src/character.cpp:4936`, `src/character_body.cpp:252`, `src/melee.cpp:632`, `src/visitable.cpp:88`, `src/item.cpp:3386`, `src/item.cpp:3394`, `src/relic.cpp:681`, `src/npc.cpp:3557`

`recalculate_enchantment_cache()` runs every turn for the avatar, every 10 seconds for NPCs, and on every melee swing and movement-mode change. For each relic found via the `is_relic` cache, it calls `enchantment::is_active( guy, parent )` per enchantment. That starts with `guy.has_item( parent )`, a full `visit_items` traversal that allocates an `item_location` impl per nested node, even though the item just came from this character's own inventory. The cost is relics x enchantments x carried items. The caller also copies `get_proc_enchantments()` / `get_defined_enchantments()` by value; each element is a large object with about 24 maps and std::functions. The cost applies only while carrying relics, but magic mods hand out many of them.

```cpp
bool enchantment::is_active( const Character &guy, const item &parent ) const {
    if( !guy.has_item( parent ) ) { return false; }
std::vector<enchant_cache> relic::get_proc_enchantments() const { return proc_passive_effects; }
```

**Fix:** Add an `is_active( guy, parent, /*known_held=*/true )` path for `recalculate_enchantment_cache` and keep `has_item` only for external callers. Return `const std::vector<...> &` from the item and relic getters (a static empty vector for non-relics).

### MAGIC-P04 · Cone AoE traces a fresh line to every interior ray sample
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/magic_spell_effect.cpp:263`, `src/magic_spell_effect.cpp:274`, `src/magic_spell_effect.cpp:286`, `src/ranged.cpp:3407`

`spell_effect_cone_range_override` samples each 1-degree ray at every range step, then runs `line_to` (a new vector) plus `passable()` checks to each distinct sample. A 60°, range-20 cone gives about 220 distinct endpoints after `std::set` deduplication (about 2.7k passable checks and 220 allocations). Interior traces aren't fully redundant, because they fill Bresenham holes, so the realistic gain is about 2-3x. Per cast and per cursor move this is trivial; it matters only inside NPC evaluation (MAGIC-P02).

```cpp
for( units::angle angle = start_angle; angle <= end_angle; angle += 1_degrees ) {
    for( int range = 1; range <= params.range; range++ ) { calc_ray_end( ... ); end_points.emplace( potential ); }
```

**Fix:** Walk each ray once, stopping at the first impassable tile, using the allocation-free bresenham functor. Return a sorted `std::vector` instead of a `std::set`.

### MAGIC-P05 · Blast AoE allocates a `line_to` vector per tile (O(r^3)), also used with radius = range
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/magic_spell_effect.cpp:231`, `src/magic_spell_effect.cpp:250`, `src/magic.cpp:2340`, `src/magic.cpp:2346`, `src/ranged.cpp:3403`

`spell_effect_blast` runs `in_spell_aoe` per tile in the radius, and each call builds a full `line_to` vector; results go into a `std::set`. The code carries its own "TODO: Make this breadth-first". `random_valid_target` reuses it with `aoe_radius = range`. Data radii are typically 1-5 and RANDOM_TARGET ranges are 12 or less, so per-cast cost is negligible; it matters as a multiplier inside NPC evaluation (MAGIC-P02).

```cpp
const std::vector<tripoint_bub_ms> trajectory = line_to( start, end );
std::set<tripoint_bub_ms> targets; // TODO: Make this breadth-first
blast_params.aoe_radius = range( caster );
```

**Fix:** Use the allocation-free bresenham functor with early exit, or one shadowcast/BFS from the centre, and return a `std::vector`. For `random_valid_target`, pick from candidate creatures (reservoir sampling) instead of materialising the disc.

### MAGIC-P06 · `spell::get_level()` is recomputed from XP on every call
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/magic.cpp:1922`, `src/magic.cpp:1927`, `src/magic.cpp:2019`, `src/math_parser_jmath.cpp:82`, `src/magic.cpp:2801`, `src/talker_character.cpp:503`

Every leveled getter recomputes the level from experience. With a `get_level_formula_id` (only XE chronomancer, XE dream and Isolation Protocol sorcery), each call allocates a talker and dialogue, and `jmath_func::eval` copies the dialogue again. Everything else does one `std::log`. The getters already build their own dialogues, which cost more, so caching saves only a small fraction. `spells_known_of_class` also copies every matching spell object, but it is rarely used.

```cpp
return std::max( static_cast<int>( std::floor( level_formula.value()->eval( dialogue( std::make_unique<talker>(), nullptr ), params ) ) ), 0 );
ret.push_back( magic->get_spell( sp ) ); // copies spell
```

**Fix:** Cache the level with the XP it was computed from, and invalidate it in `set_exp` / `gain_exp` / `set_level`. Use one static empty dialogue for formula evaluation. Return `std::vector<const spell *>` from `spells_known_of_class`.

### MAGIC-P07 · Special-vision lookups copy structs and allocate two talkers per unseen creature per frame
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/magic_enchantment.cpp:1222`, `src/magic_enchantment.cpp:1251`, `src/magic_enchantment.h:412`, `src/character.cpp:3856`, `src/cata_tiles.cpp:1013`, `src/cata_tiles.cpp:4339`, `src/ranged.cpp:2287`, `src/character.cpp:6461`

`sees_with_specials` and the `draw_critter_at` paths build a two-talker `const_dialogue` before calling `get_vision`, even when `special_vision_vector` is empty, which is the common case. `get_vision` and `get_vision_description_struct` return by value, copying a vector of description structs with strings and std::functions. This runs for every creature on a tile the player can't see, on every redraw and in targeting.

```cpp
const const_dialogue d( get_const_talker_for( *this ), get_const_talker_for( critter ) );
const enchant_cache::special_vision foo = enchantment_cache->get_vision( d );
```

**Fix:** Add `bool has_special_vision() const { return !special_vision_vector.empty(); }` and return early before building the dialogue. Return `const special_vision *` / `const special_vision_descriptions *` instead of copies.

### MAGIC-P08 · `spell::can_cast` copies `cannot_cast_flags` strings and re-interns them on every call
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/magic.cpp:1147`, `src/magic.cpp:1148`, `src/magic_type.h:61`, `src/magic_type.cpp:62`, `src/magic.cpp:3461`, `src/npc_attack.cpp:173`, `src/string_id.cpp:24`

For spells with a magic_type, each `can_cast()` iterates a `std::set<std::string>` by value and builds a `json_character_flag` (an interning map lookup) per flag. It runs for every spell when the cast menu opens and from `npc_attack_spell::can_use` each NPC combat turn. The saving is likely small next to the `has_flag` call that follows and next to MAGIC-P02, but the fix is trivial, and converting at load also lets `check_consistency` validate the flags.

```cpp
for( std::string cannot_cast_flag_string : type->magic_type.value()->cannot_cast_flags ) {
    json_character_flag cannot_cast_flag( cannot_cast_flag_string );
```

**Fix:** Store `cannot_cast_flags` as `std::vector<json_character_flag>`, converted once in `magic_type::load`, iterate by const reference, and validate the flags in `check_consistency`.

### MAGIC-P09 · Per-tile spell effects recompute caster-invariant stats and emit one sound per tile
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/magic_spell_effect.cpp:541`, `src/magic_spell_effect.cpp:548`, `src/magic_spell_effect.cpp:557`, `src/magic_spell_effect.cpp:558`, `src/magic_spell_effect.cpp:711`, `src/magic.cpp:1534`, `src/magic.cpp:1557`, `src/magic.cpp:728`, `src/sounds.cpp:322`

For every AoE tile, `damage_targets` re-runs `is_valid_target` (already filtered in `calculate_spell_effect_area`). `make_sound` calls `damage()` once or twice and pushes one `sounds::sound` event per tile. `create_field` calls `field_intensity()` three times, and `accuracy()` and `damage()` run again per creature, each building fresh dialogues. The same pattern repeats in mod_moves, morale, charm, bash, mutate, pickup and effect_on_condition. This is a per-cast cost, typically sub-millisecond; the more noticeable effect is the per-tile sound flood that `process_sound_markers` handles one by one.

```cpp
for( const tripoint_bub_ms &target : targets ) {
    if( !sp.is_valid_target( caster, target ) ) { continue; }
    ... sp.make_sound( target, caster ); sp.create_field( target, caster );
```

**Fix:** Compute per-cast invariants (one dialogue, accuracy, field intensity/variance/chance, duration, sound volume) once before the loop. Keep per-target damage rolls where RANDOM_DAMAGE needs them, since `damage()` re-rolls on every call. Drop the redundant `is_valid_target` checks, emit one sound at the epicenter, and evaluate each `dbl_or_var` once per getter.

## Appendix: considered and rejected

- enchant_cache stores ~125 enum-keyed values in std::map and is rebuilt every turn – the maps hold only the few keys active enchantments touch, monster and vehicle caches are usually empty, and NPC rebuilds are throttled. A fixed `std::array` would add about 2 KB per monster and likely be a regression.
- Every enchantment rebuild translates name/description and deep-copies vision/spell data – `translation::translated()` is cached; what remains is two small string copies per enchantment, dwarfed by the dialogue work in the same function.
- Monster/vehicle enchantment passes evaluate conditions before cheap relevance checks – the default condition is ALWAYS (a single compare), the conditioned effects actually applied to monsters are monster-relevant anyway, and the relevance scan is a handful of enum compares.
- ter_furn_transform lookups return result data by value – the copy only happens on a match and is followed by far more expensive map writes; trap flag transforms are unused in data; not a hot path.
- Startup spell extra_effects loop check copies sets and re-walks shared subgraphs – a simulation over all loaded data showed about 3,200 recursive calls in total (at most 9 per root), microseconds of startup time.
