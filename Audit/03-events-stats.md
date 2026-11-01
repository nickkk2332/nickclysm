# 03 · Events, statistics, achievements & timed events

> Engine audit – static code reading at commit `a1a646d6`; nothing was compiled or run. Each finding was checked against the code by a separate reviewer: **confirmed** = verified by reading the code; **plausible** = consistent with the code, but reachability or impact not proven. Line numbers refer to that commit.

**Scope:** `src/event.cpp`, `src/event_bus.cpp`, `src/event_field_transformations.cpp`, `src/event_statistics.cpp`, `src/achievement.cpp`, `src/stats_tracker.cpp`, `src/kill_tracker.cpp`, `src/memorial_logger.cpp`, `src/scores_ui.cpp`, `src/timed_event.cpp`, `src/diary.cpp`, `src/past_games_info.cpp`, `src/past_achievements_info.cpp`, `src/text_snippets.cpp`, `src/speech.cpp`

## Summary

The event bus works, but the layers built on it are fragile. Avatar death events fire repeatedly during the deathcam. Event names are saved as strict enum strings, so any removed or foreign event type makes saves and memorial files unloadable. Statistics are recomputed from raw history instead of from the incrementally maintained state, and on every load that recompute reaches into the overmap buffer and loads every overmap the player ever visited. For modders, the event surface is incomplete (missing events and payload fields, monster-blind effect events, `u_var_changed` that misses `math` writes), the EOC-side docs have drifted from `event.h`, and the scripted timed events remain hardcoded C++ with several dead enum values.

| Lens | Critical | High | Medium | Low | Total |
|---|---|---|---|---|---|
| Missing JSON / EOC | 0 | 0 | 10 | 5 | 15 |
| Bugs & quirks | 0 | 2 | 8 | 7 | 17 |
| Performance | 0 | 1 | 2 | 7 | 10 |
| **Total** | 0 | 3 | 20 | 19 | 42 |

**Fix first:**
1. **EVT-B01** · Avatar death events re-sent on every `is_game_over()` call: high severity, small fix (a once-only flag). It corrupts the memorial, kill_tracker and stats, and re-runs EOCs.
2. **EVT-P01** · Every game load pulls in every overmap the avatar ever visited: high load-time and memory cost that grows with play; adding the special to the event at emit time is a contained fix.
3. **EVT-B02** · Unknown `event_type` names make saves, memorials and achievement files unloadable: this blocks any event cleanup in this fork, and one foreign memorial file breaks character creation and the scores UI.
4. **EVT-J01** · Triffid-heart death is a silent no-op: a one-word data fix plus a load-time check restores a broken core encounter.
5. **EVT-B06** · Memorial log carries over between characters: a one-line fix for history that leaks across characters into saved memorial files.

## 1. Missing JSON bindings & EOC features

### EVT-J01 · Triffid-heart death is a silent no-op: `effect_str "ROOTS_DIE"` never matches, and timed_event spells are never validated
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/magic_spell_effect.cpp:1313`, `src/magic_spell_effect.cpp:1319`, `src/magic_spell_effect.cpp:1329`, `src/magic.cpp:2182`, `src/magic.cpp:560`, `src/timed_event.cpp:197`, `data/json/monster_special_attacks/monster_deaths.json:691`, `doc/JSON/MAGIC.md:221`

`spell_effect::timed_event` finds the event with an exact, case-sensitive lookup in a local map whose keys are lower case. `death_triffid_heart` (cast by `monster::die`, monster.cpp:3208) uses `"effect_str": "ROOTS_DIE"`, so the lookup fails and an inert `NONE` event is queued. Killing the triffid heart therefore never turns root walls into underbrush, and `destroys_triffid_grove` is never sent, so its memorial entry is missing. `spell_type::check_consistency` validates `summon_vehicle` effect_str but not `timed_event`, so typos fail silently. MAGIC.md also lists `robot_attack` and `wanted` as valid values; they are not in the map and become `NONE`. `help` maps to HELP, which only shows a debugmsg.

```cpp
{ "roots_die", timed_event_type::ROOTS_DIE },              // magic_spell_effect.cpp:1319
timed_event_type spell_event = timed_event_type::NONE;     // :1327
const auto iter = timed_event_map.find( sp.effect_data() ); // :1329, raw effect_str, no case folding
// monster_deaths.json:691
"effect_str": "ROOTS_DIE",
```

**Fix:** Change the data to `"roots_die"`, or move the map to file scope and look it up case-insensitively. In `check_consistency` add `if( effect_name == "timed_event" && !timed_event_map.count( effect_str ) ) debugmsg( "spell %s: unknown timed_event '%s'" )`. Remove `help`/`wanted`/`robot_attack` from MAGIC.md or implement them (see EVT-J10). Longer term, replace `"effect": "timed_event", "effect_str": "roots_die"` with `"effect": "effect_on_condition"` plus `run_eocs` with `time_in_future`.

### EVT-J02 · `trigger_event` is undocumented, its positional args are unchecked at load, and the doc's field order differs from `event.h`
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctalk.cpp:8488`, `src/npctalk.cpp:8748`, `src/event_bus.cpp:142`, `src/event.h:350`, `src/event.h:414`, `doc/JSON/EFFECT_ON_CONDITION.md:1870`, `doc/JSON/EFFECT_ON_CONDITION.md:1876`

`trigger_event` is used by core marloss content but appears nowhere in `doc/`. Its `args` are positional strings matched to the `event_spec` field order, which is only visible in `event.h`. The EVENT doc table lists some fields in a different order: `character_gains_effect` and `character_loses_effect` are documented as character, effect, bodypart but the spec is character, bodypart, effect. The argument count is checked only at runtime (debugmsg), and types are never checked. Wrong order produces events with an invalid bodypart/efftype that are stored (and saved) by stats_tracker; memorial_logger's `.obj()` on a bad efftype logs an error. `trigger_event` also uses plain `send`, so no alpha/beta talkers can be passed. The crash side of the missing validation is EVT-B10.

```cpp
// event.h:351-355
{ "character" }, { "bodypart" }, { "effect" }, { "intensity" }
// EFFECT_ON_CONDITION.md:1870
{ "character" },{ "effect" },{ "bodypart" }, { "intensity" }
```

**Fix:** Document `trigger_event`. Accept named args validated at load against `event::get_fields(type)`:
```json
{ "trigger_event": "character_gains_effect",
  "args": { "character": { "u_val": "id" }, "effect": "onfire", "bodypart": "torso", "intensity": 1 },
  "alpha": "u", "beta": "npc" }
```
Route optional `alpha`/`beta` through `send_with_talker`. Generate the EVENT doc table from `event.h`, or add a test comparing them.

### EVT-J03 · EVENT doc table has drifted from `event.h`, and five event types are never emitted
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `doc/JSON/EFFECT_ON_CONDITION.md:1849`, `:1857`, `:1861`, `:1873`, `:1899`, `:1924`, `:1938`, `src/event.h:207`, `src/event.h:289`, `src/event.h:394`, `src/event.h:728`, `src/event.h:889`, `src/memorial_logger.cpp:441`

Modders write EOC conditions against the context-variable names in this table, and wrong names silently compare against empty values. Drift: `game_avatar_death`/`game_avatar_new` document `avatar_is_male`, which does not exist; `player_levels_spell` documents `spell_class` but the field is `school`; `reads_book` omits `itype`; `character_kills_character` omits `victim_class`; `camp_taken_over` says `new_Owner` instead of `new_owner`. Dead events: `activates_artifact`, `becomes_wanted`, `dermatik_eggs_hatch` and `dermatik_eggs_injected` have no sender anywhere in `src/` or `data/` (only `event.h`, memorial_logger and memorial_test reference them), yet the docs describe them as live. `telefrags_creature` is also never sent but is already marked "(Unimplemented)".

```cpp
// event.h:894
{ "school", cata_variant_type::trait_id }   // doc: { "spell_class", trait_id }
```

**Fix:** Correct the table and mark never-emitted events as "not currently emitted", or wire them up (relic activation for `activates_artifact`, the JSON parasite EOCs via `trigger_event` for the dermatik events, the eyebot path for `becomes_wanted`, see EVT-J10). Add a unit test asserting every `event_type` has at least one sender or is on an allowlist of JSON-only events.

### EVT-J04 · Effect gain/loss/intensity events are character-only; monsters never trigger them and effects have no per-type EOC hooks
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/creature.cpp:1980-1984`, `src/creature.cpp:1997-1998`, `src/creature.cpp:2080-2087`, `src/creature.cpp:2743-2747`, `src/creature.cpp:2802-2806`, `src/magic.cpp:2220-2231`, `src/effect.cpp`

`Creature::add_effect`/`remove_effect` send `character_gains_effect`, `character_effect_intensity_changed` and `character_loses_effect` only when `as_character()` is non-null, with a plain `send` (no source talker). `effect_type` has no apply/remove EOC fields, so modders cannot react to a monster gaining an effect. `broken_bone` fires only for `is_avatar()`, although the docs say "any body part", so NPC limb breaks are invisible. Monster-cast spells emit no event at all. Characters are covered; the gap is monsters, the effect source, and per-effect hooks.

```cpp
if( Character *ch = as_character() ) {
    get_event_bus().send<event_type::character_gains_effect>( ch->getID(), bp.id(), eff_id, intensity );
// creature.cpp:2745
if( !was_broken && is_avatar() && as_character()->is_limb_broken( id ) ) {
```

**Fix:** Add `creature_gains_effect`/`creature_loses_effect` events `{effect, bodypart, intensity}` sent with `send_with_talker( this, source_creature )`. Alternatively add effect_type fields run with `u` = holder, `npc` = source:
```json
{ "type": "effect_type", "id": "onfire", "apply_eocs": [ "EOC_IGNITE_NEARBY" ], "remove_eocs": [], "intensity_change_eocs": [] }
```
Drop the `is_avatar()` gate on `broken_bone`.

### EVT-J05 · Achievements and scores are frozen to the set present at `game_start`; `give_achievement` silently no-ops and EOCs cannot read achievement state
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/achievement.cpp:783-792`, `src/achievement.cpp:897-905`, `src/achievement.cpp:917-925`, `src/achievement.cpp:831-835`, `src/stats_tracker.cpp:329-338`, `src/stats_tracker.cpp:373-378`, `src/npctalk.cpp:5816-5833`, `src/profession.cpp:773`, `src/scenario.cpp:687`, `src/debug_menu.cpp:3977`

`valid_achievements()` and `valid_scores()` return only ids captured when `game_start` fired. Achievements, conducts or scores from a mod added to an existing save are never tracked or shown, and `give_achievement` on such an id returns without any message. EOCs cannot read achievement state: there is no condition or math function for completed/failed/pending, or for past-game completion, although professions and scenarios use that in C++. There is no `fail_conduct` effect, and `give_achievement` on a conduct marks it "completed", a state conducts never otherwise reach. The arcade-mode unlock is hardcoded by id in four files. Freezing the set is deliberate, and listening to `player_gets_achievement`/`player_fails_conduct` is a partial workaround.

```cpp
if( initial_achievements_.count( ach.id ) ) {                 // achievement.cpp:787
if( e.type() == event_type::game_start ) { ... initial_achievements_.insert( ach.id );
```

**Fix:** Opt-in late tracking per achievement (`"allow_late_tracking": true`) or a world option. Make `give_achievement` debugmsg when the id is untracked or is a conduct. Add:
```json
{ "math": [ "achievement_state('ach_id') == 1" ] }   // 0 pending, 1 completed, 2 failed
{ "math": [ "past_achievement('ach_id')" ] }
{ "fail_conduct": "conduct_id" }
```
Replace the arcade-mode id checks with an achievement flag such as `"unlocks_all_content": true`.

### EVT-J06 · Major gameplay moments emit no event: construction finished, non-character monster deaths, mission success/fail, NPC recruited
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/construction.cpp:1550-1560`, `src/activity_actor.cpp:9533-9537`, `src/player_activity.cpp:350-360`, `src/monster.cpp:3123-3136`, `src/npc.cpp:3327`, `src/game.cpp:2869`, `src/mission.cpp:429-440`, `src/mission.cpp:473-485`, `src/npctalk_funcs.cpp:911`

Cross-cutting mods (reputation, story trackers, achievements) need global hooks. (1) `complete_construction` sends no event and has no JSON EOC field; `post_special` is a closed C++ map. It also skips `character_finished_activity` and the activity `completion_EOC`, because the activity is nulled inside `do_turn` so the `moves_left <= 0` branch never runs. (2) `monster::die` sends `character_kills_monster` only when the killer resolves to a Character; kills by pets, monsters, turrets, vehicles or the environment are invisible. (3) `character_dies` carries no killer or cause. (4) `mission::fail`, `mission::wrap_up` and `talk_function::follow` send nothing. Per-type hooks exist (mission end/fail, recipe `result_eocs`, death functions), but a mod cannot attach them to content it does not own.

```cpp
add_msg( m_info, _( "%s finished construction: %s." ) ...
for( const auto &special : built.post_specials ) { special( terp, you ); }   // no event
```

**Fix:** Add events: `character_finishes_construction {character, construction_group, terrain/furniture}` sent with the builder as talker; `monster_dies {victim_type, killer_type?}` sent with `(monster, killer-or-null)`; a `killer` field on `character_dies`; `mission_ends {character, mission_type, success}`; `npc_joins_faction {npc, faction}`. Add a construction field:
```json
{ "type": "construction", "id": "constr_wall", "post_eocs": [ "EOC_ON_WALL_BUILT" ] }
```
run with `u` = builder and a location context var.

### EVT-J07 · `u_var_changed` only fires from legacy `u_add_var`/`npc_add_var`, so `math` assignments are invisible and it misreports the owner
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/npctalk.cpp:3906`, `src/npctalk.cpp:3919`, `src/npctalk.cpp:3925`, `src/npctalk.cpp:8615`, `src/event.h:968`, `src/effect_on_condition.cpp:602`, `data/json/effects_on_condition/nether_eocs/portal_storm_effect_on_condition.json:215`, `data/json/statistics.json:2455`

The only emitter of `u_var_changed` is `f_add_var`, which serves both `u_add_var` and `npc_add_var`, and skips the event in the `time: true` branch. The event has only `{var, value}`, so an `npc_add_var` on an NPC, monster or item looks like an avatar variable change, and EOC alpha falls back to the avatar. Writes through `math` (the recommended path, about 3,700 uses in data/json versus 682 `u_add_var`) emit nothing. The core `portal_storm_happened` statistic only works because the portal-storm EOC re-sends the event by hand. Modders who move to `math` silently break statistics and achievements that watch variables. (Merges a JSON-lens and a bug-lens finding.)

```cpp
actor->set_value( var_name, possible_values[index] );
get_event_bus().send<event_type::u_var_changed>( var_base_name, possible_values[index] );
```
```json
{ "math": [ "u_counter_portal_storm_counter++" ] },
{ "trigger_event": "u_var_changed", "args": [ "portal_storm_counter", { "u_val": "counter_portal_storm_counter" } ] }
```

**Fix:** Sending on every write would flood stats_tracker. Add an opt-in declaration and send a new `var_changed {owner, scope, var, value}` event from the central `talker::set_value`/global setter only for watched names:
```json
{ "type": "watched_variable", "var": "portal_storm_counter", "scope": "u" }
```
Until then, send `u_var_changed` only when the actor is the avatar. Deprecate it or give it an owner field.

### EVT-J08 · Event payloads lack damage type, location, monster type and item talkers
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/event.h:505`, `src/event.h:495`, `src/event.h:474`, `src/event.h:514`, `src/event.h:375`, `src/event.h:308`, `src/consumption.cpp:2102`, `src/consumption.cpp:1390`, `src/handle_action.cpp:946`, `src/activity_actor.cpp:10658`, `src/cata_variant.h`

`monster_takes_damage` is `{damage, dies}` (no victim mtype, damage type, bodypart), so statistics cannot filter by monster or damage type. `character_takes_damage` has no damage type. `character_smashes_tile`, `character_triggers_trap` and `cuts_tree` carry no location. `character_heals_damage` has no bodypart; `character_casts_spell` and `spellcasting_finish` have no target. `character_consumes_item`/`character_eats_item` use plain `send`, unlike wear/wield, so an "on eat" EOC cannot read the item. `cata_variant_type` has no damage type, material or item category entry. Mitigations: `monster_takes_damage` does carry talkers when there is a source, and per-item consumption EOCs get an item talker.

```cpp
send<event_type::character_smashes_tile>( getID(), here.ter( smashp ).id(), here.furn( smashp ).id() ); // no point
get_event_bus().send<event_type::character_consumes_item>( getID(), target.typeId() );                // no item talker
```

**Fix:** Add `cata_variant` types `damage_type_id` and `item_category_id` (or carry them as strings). Extend specs append-only (saved events must still load): `monster_takes_damage += {victim_type, damage_type, bodypart}`, `character_takes_damage += {damage_type}`, smash/trap/cut-tree `+= {pos}` (map-square units), `casts_spell += {target}`. Send consume/eat with `send_with_talker( this, &item_loc )`.

### EVT-J09 · The statistics DSL is closed: 8 hardcoded field transformations, no distinct-count or time-based stat types
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/event_field_transformations.cpp:92`, `src/event_statistics.cpp:1241`, `src/event_statistics.cpp:930`, `src/event_statistics.cpp:983`, `src/stats_tracker.cpp:126`, `src/stats_tracker.cpp:142`

New event fields can only come from 8 C++ functions (flags_of_itype, flags_of_terrain, is_mounted, is_swimming_terrain, math_abs, oter_type_of_oter, overmap_special_at, species_of_monster). Ideas like "eat 50 items of category X", "kill a FLIES monster" or "smash furniture with flag X" cannot be expressed in the DSL. `stat_type` accepts only count/total/minimum/maximum/unique_value/first_value/last_value: no distinct-value count and no time-based stat, though every `event_summary` already stores first/last time. The 0 seeding of minimum/maximum is covered in EVT-B08. An event EOC plus a counter plus `give_achievement` is a workaround, but the DSL itself stays closed.

```cpp
} else if( type == "last_value" ) { ... } else { jo.throw_error( "Invalid stat_type '" + type + "'" ); }
```

**Fix:** Add field transformations `category_of_itype`, `materials_of_itype`, `flags_of_monster`, `flags_of_furniture`, `mutation_category_of_trait`, `melee_skill_of_itype` (each needs a cata_variant type). Add stat types:
```json
{ "type": "event_statistic", "id": "distinct_zombie_species", "stat_type": "count_distinct", "field": "victim_type", "event_transformation": "avatar_zombie_kills" }
```
plus `first_time`/`last_time` (turns as int) and `average`.

### EVT-J10 · Scripted timed events are hardcoded C++ with fixed content ids; ROBOT_ATTACK, WANTED, HELP and DSA_ALRP_SUMMON are dead, so eyebot photographs do nothing
**Severity:** Medium · **Effort:** Large · **Verified:** confirmed
**Where:** `src/timed_event.cpp:38`, `src/timed_event.cpp:115`, `src/timed_event.cpp:122`, `src/timed_event.cpp:288`, `src/timed_event.cpp:304`, `src/timed_event.cpp:343`, `src/timed_event.h:17`, `src/monattack.cpp:2703`, `src/magic_spell_effect.cpp:1313`, `doc/JSON/MAGIC.md:221`, `doc/JSON/MONSTER_SPECIAL_ATTACKS.md:147`

SPAWN_WYRMS, AMIGARA, ROOTS_DIE, TEMPLE_OPEN/FLOOD/SPAWN and DSA_ALRP_SUMMON are hardwired to about 20 static ids (`t_orifice`, `t_fault`, `t_root_wall`, `mon_amigara_horror`, the 4-entry temple monster array, ...) and fixed tuning (rng(3,5) horrors, one_in(3) roots, 2-3 turn flood ticks). A mod cannot reuse them with its own content, though the spell effect `timed_event` can queue them. `mattack::photograph` queues ROBOT_ATTACK and tracks `eyebot_assisted`/`eyebot_depleted`, but `actualize()` has no ROBOT_ATTACK or WANTED case, so nothing follows the "Citizen ...!" message (this affects the aftershock_exoplanet mod; no core monster uses PHOTOGRAPH). WANTED has no producer, HELP only shows a debugmsg, and DSA_ALRP_SUMMON is only referenced from the debug console. EOCs can reschedule keyed events (`alter_timed_events`) but cannot query or cancel them, and the hardcoded events have no key. (Merges a JSON-lens and a bug-lens finding.)

```cpp
case timed_event_type::HELP: debugmsg( "Currently disabled while NPC and monster factions are being rewritten." );
...
default: // Nothing happens for other events
// monattack.cpp:2703
get_timed_events().add( timed_event_type::ROBOT_ATTACK, calendar::turn + rng( 15_turns, 30_turns ), 0, player_character.pos_abs() );
```

**Fix:** Migrate each scripted event to JSON EOCs queued with `run_eocs` + `time_in_future` (e.g. a self-requeuing `EOC_TEMPLE_FLOOD`, `EOC_AMIGARA_SPAWN` using `u_spawn_monster` with a monstergroup), and have iexamine/computer actions run EOCs instead of `timed_events.add`. Give PHOTOGRAPH a JSON `"on_photograph_eoc"`, or stop queueing ROBOT_ATTACK. Add:
```json
{ "timed_event_queued": "key" }
{ "cancel_timed_events": "key" }
```
Remove the dead enum values with a save migration.

### EVT-J11 · `avatar_enters_omt.pos` is an OMT coordinate, but EOC context turns every tripoint into `tripoint_abs_ms`
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/math_parser_diag_value.cpp:152-153`, `src/game.cpp:1075`, `src/game.cpp:11557`, `src/event.h:231-236`, `src/event_field_transformations.cpp:75`

`pos` is the only tripoint event field and is sent as `abs_omt.raw()`; the stats code reads it back as OMT. `diag_value(cata_variant)` wraps every tripoint as `tripoint_abs_ms`, so an EVENT EOC using `{ "context_val": "pos" }` as a location gets a point 24x closer to the origin, with no error. Latent: no EOC in data/ reads it.

```cpp
} else if( cv.type() == cata_variant_type::tripoint ) {
    blorg = diag_value( tripoint_abs_ms{ cv.get<cata_variant_type::tripoint>() } );
```

**Fix:** Add `cata_variant_type::tripoint_abs_omt` (or send a map-square `pos` plus a separate OMT field) and convert by declared unit (`project_to<coords::ms>`) in the diag_value conversion. Document units in the EVENT table.

### EVT-J12 · EOCs cannot read kill_tracker counts
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/kill_tracker.cpp:24`, `src/kill_tracker.cpp:33`, `src/kill_tracker.cpp:104`, `src/mission.cpp:411`, `src/mission.cpp:697`, `src/math_parser_diag.cpp:1903`

`kill_count(mtype_id)`, `kill_count(species_id)`, `npc_kill_count()` and `guilt_kill_count()` back missions, the scores UI and the guilt mechanic, but no condition or math function exposes them (only the `<total_kills>` dialogue tag). EVENT EOCs on `character_kills_monster` can count kills from a point onwards, but the tally since game start is unreadable.

```cpp
return g->get_kill_tracker().kill_count( monster_type ) >= kill_count_to_reach;   // mission.cpp:697
```

**Fix:** Add math functions `kill_count('mon_zombie')`, `kill_count_species('ZOMBIE')`, `kill_count_npc()`, `kill_count_total()`.

### EVT-J13 · Memorial log text is wired to hardcoded effect, trait and bodypart ids and thresholds
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/memorial_logger.cpp:65`, `src/memorial_logger.cpp:266`, `src/memorial_logger.cpp:576`, `src/memorial_logger.cpp:619`, `src/memorial_logger.cpp:719`, `src/memorial_logger.cpp:900`

Data-driven memorial text already exists (effect apply/remove logs, trap `memorial_msg`), but overdose deaths map four hardcoded effects to messages, the NPC-kill line branches on hardcoded CANNIBAL/PSYCHOPATH/SAPIOVORE traits, "Final HP" lists six literal bodyparts and skips extra JSON limbs, monster kills are logged only at difficulty >= 30, and skill gains every 4 levels. Cosmetic; mods can use `u_add_memorial`.

```cpp
if( effect == effect_datura ) {...} else if( effect == effect_jetinjector ) {...} else if( effect == effect_adrenaline ) {...}
```

**Fix:** Add effect_type `"death_memorial_log": { "male": "...", "female": "..." }`, a mutation field `"npc_kill_memorial_log"`, an mtype flag `MEMORIAL_ON_KILL` replacing the 30 threshold, and iterate `get_all_body_parts( only_main )` for Final HP.

### EVT-J14 · Lore/snippet hooks are one-way: `effect_on_examine` gets no item talker, and EOCs cannot grant or check a specific snippet
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_info.cpp:313`, `src/text_snippets.cpp:84`, `src/npctalk.cpp:5616`, `src/avatar.cpp:948`, `src/diary.cpp:148`, `doc/JSON/JSON_INFO.md:694`

A snippet's `effect_on_examine` runs with only the avatar, so it cannot touch the note it came from, and no event fires for lore discovery. EOCs can store a random snippet from a category in the lore tab but cannot add a specific id, check `has_seen_snippet`, or count lore, and cannot add diary entries. JSON_INFO says the snippet `name` is unused, but the diary and item names show it. Passing the item as beta is not trivial: `item::basic_info` is const and has no `item_location`.

```cpp
dialogue d( get_talker_for( get_avatar() ), nullptr );
examine_effect.value().apply( d );
```

**Fix:** Add `{ "u_learn_lore": "snippet_id" }`, condition `{ "u_knows_lore": "snippet_id" }`, math `lore_count('category')`, `{ "u_add_diary_entry": "text" }`, and a `learns_lore {character, snippet}` event. Pass the item as beta where an `item_location` is available. Fix the doc line about `name`.

### EVT-J15 · `event_statistic` and `event_transformation` rebuild from scratch on load, so copy-from cannot inherit
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/event_statistics.cpp:437`, `src/event_statistics.cpp:722`, `src/event_statistics.cpp:1241`, `src/event_statistics.h:64`

Both loaders read into locals and replace `impl_` wholesale. With `copy-from`, a transformation that does not restate its fields loses them, `event_source::load` always demands `event_type`/`event_transformation`, and a statistic without `stat_type` throws "Invalid stat_type ''" because `mandatory` is a no-op when `was_loaded`. Redefining the same id works, so impact is low.

```cpp
std::string type;
mandatory( jo, was_loaded, "stat_type", type );
```

**Fix:** Keep source, new_fields, constraints, drop_fields, stat_type and field as members loaded with `optional( jo, was_loaded, ... )` and build `impl_` in `finalize()`.

## 2. Bugs, oversights & quirks

### EVT-B01 · Avatar death events are re-sent on every `is_game_over()` call during deathcam, and sent after the game-over decision
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/game.cpp:2863`, `src/game.cpp:2872`, `src/game.cpp:2904`, `src/game.cpp:2905`, `src/game.cpp:2922`, `src/do_turn.cpp:535`, `src/do_turn.cpp:674`, `src/handle_action.cpp:328`, `src/handle_action.cpp:3220`, `src/end_screen.cpp:75`, `src/debug_menu.cpp:5014`, `src/npc.cpp:3327`, `src/memorial_logger.cpp:496`, `src/kill_tracker.cpp:120`

`is_game_over()` sends `character_kills_character`, `character_dies` and `avatar_dies` whenever `uquit` is QUIT_DIED or QUIT_WATCH, with no once-only guard. In deathcam ("watch", reachable from the default "ask" option) it runs at least twice per turn, and input uses a 125 ms timeout, so the events repeat several times a second. Effects: one "Died" memorial line per call; `character_dies`/`character_kills_character` EOCs re-run (Xedra listens to the latter); a follower killer re-adds the avatar to `npc_kills` each time; stats counts inflate. Each call also dereferences `u.get_killer()` without a liveness check, so if the killer is cleaned up during deathcam a use-after-free is plausible. Ordering problems: the avatar's `character_dies` is sent after `uquit` is set, so the documented `u_prevent_death` pattern works for NPCs but never the avatar; `bury_screen()` sends `game_avatar_death` before the AVATAR_DEATH EOCs, so a revive leaves a death memorial entry; the debug "Test end screen" emits a real `game_avatar_death`; `npc::die` sends `character_dies` before the hallucination/lifespan check.

```cpp
if( uquit == QUIT_DIED || uquit == QUIT_WATCH ) {
    ... events().send<event_type::character_kills_character>( ... );
    events().send<event_type::character_dies>( u.getID() );
    events().send<event_type::avatar_dies>();
}
if( uquit == QUIT_WATCH ) { ... return false; }
```

**Fix:** Add a `death_events_sent` flag (reset in `setup()`) and send once, when death becomes final. Send the avatar's `character_dies` before PREVENT_DEATH/AVATAR_DEATH processing, as `npc::die` does. Call `bury_screen()`/`game_avatar_death` only after the AVATAR_DEATH EOCs. Pass `actually_dead=false` from the debug preview. Skip `character_dies` for hallucinations and lifespan_end NPCs.

### EVT-B02 · Unknown `event_type` names make saves, memorials and achievement files unloadable
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/event.h:198`, `src/savegame_json.cpp:4983`, `src/flexbuffer_json-inl.h:580`, `src/json.h:83`, `src/savegame.cpp:229`, `src/cata_utility.cpp:460`, `src/past_games_info.cpp:42`, `src/past_games_info.cpp:157`, `src/past_achievements_info.cpp:56`, `src/profession.cpp:773`

stats_tracker data is keyed by event_type name and parsed with `io::string_to_enum`, which throws `InvalidEnumString` (a `std::runtime_error`, not `JsonError`). The JSON reader, `game::unserialize`, `past_games_info::ensure_loaded` and `past_achievements_info::load` catch only `JsonError` or nothing. So: (1) removing or renaming any event type, or loading a save from a build with extra events, aborts the load with a debugmsg (FIXME at event.h:199). (2) Memorial files are global across worlds and versions; one foreign memorial throws out of `past_games_info`, which runs from profession/scenario `can_pick` in character creation, the scores UI and the "first" achievement popup. (3) Both past_* loaders read `int version;` uninitialised when missing and set `loaded_ = true` before their loops, so one bad file stops the rest. This blocks any event cleanup in this fork.

```cpp
// event.h:199-201
// ... It's currently unsafe to ever remove any of these. Removal will cause any save file
// with one of the saved events to be unable to load. // FIXME
key_type key = key_from_json_string<key_type>()( jm.name() );   // inside catch( const JsonError & )
```

**Fix:** Deserialize the stats map manually, skipping (optionally keeping raw) unknown event types, with a migration table for renames. Catch `std::exception` per file in both past_* loaders and log the file. Initialise `version` and set `loaded_` after the loop.

### EVT-B03 · EVENT EOCs silently fall back to the avatar as `u` when the event has no resolvable actor
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/effect_on_condition.cpp:600-622`, `src/monster.cpp:2518-2523`, `src/magic.cpp:2212`, `src/iuse.cpp:490`, `src/construction.cpp:2048`, `src/event_bus.h:27`, `src/creature.cpp:3736`, `doc/JSON/EFFECT_ON_CONDITION.md:1840`, `doc/JSON/EFFECT_ON_CONDITION.md:1886`

`eoc_events::notify` picks alpha from a character-id field and otherwise uses the avatar. `monster::apply_damage` uses talkers only when there is a source; sourceless damage (bleeding, fire, falls, traps) goes through plain `send`, and `{damage, dies}` has no character field, so every `monster_takes_damage` EOC runs with the player as `u`, contrary to the doc ("monster / attacker if exists"). An unguarded `u_add_effect` lands on the player on every bleed tick. Casterless spells (`character_id(-1)` fails `is_valid()`) also fall back. `eats_sewage` and `digs_into_lava` have no character field although NPCs can trigger them; they are documented as avatar/NONE, as are the other empty events, so that part is intended. There is no alpha-only `send_with_talker`, and passing `nullptr` hits a debugmsg in `get_talker_for`. Existing content mostly guards with `has_beta`. (Merges a JSON-lens and a bug-lens finding.)

```cpp
const std::vector<std::string> potential_alphas = { "avatar_id", "character", "attacker", "killer", "npc" };
...
alpha = get_talker_for( get_avatar() );
// monster.cpp:2518-2523
if( source ) { get_event_bus().send_with_talker( this, source, e ); } else { get_event_bus().send( e ); }
```

**Fix:** Add `send_with_talker( Creature *alpha, std::nullptr_t, const cata::event & )` and use it in the sourceless branch. Add a `character` field to `eats_sewage`/`digs_into_lava`. Send no event (or a `has_caster` flag) for casterless spells. In `notify`, skip the EOC or run it with no alpha when an actor-specific event cannot be resolved, and document the fallback per event.

### EVT-B04 · Timed EXPLOSION events lose their `explosion_data` on save/load (mortar shells become duds)
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/savegame.cpp:1835`, `src/savegame.cpp:1916`, `src/timed_event.cpp:102`, `src/timed_event.cpp:298`, `src/iexamine_actors.cpp:401`, `src/explosion.h:41`, `src/explosion.cpp:596`

The mortar queues a timed EXPLOSION carrying the ammo's explosion data, landing up to ~205 s later, and spends the charge immediately. `serialize_all` never writes `expl_data`, and the loader rebuilds every event through the submap-revert constructor, leaving `power = -1.0f`. `_make_explosion` then skips the blast. A save/load (including autosave) in that window turns the shell into a dud.

```cpp
get_timed_events().add( static_cast<timed_event_type>( type ), when, faction_id, map_square,
                        strength, string_id, std::move( revert ), key );   // no expl_data
float power = -1.0f;                                                       // explosion.h:41
```

**Fix:** Add `explosion_data::serialize()` (deserialize exists), write it for EXPLOSION events and restore it on load. Alternatively store the ammo_effect id and re-resolve at fire time.

### EVT-B05 · Diary calls `known_magic::get_spell` for spells the avatar has since forgotten
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/diary.cpp:211`, `src/diary.cpp:514`, `src/diary.cpp:568`, `src/diary.cpp:595`, `src/magic.cpp:2499`, `src/magic.cpp:2577`

`spell_change` reads each recorded spell's name and description from the avatar's current spellbook. If the spell was forgotten (`forget_spell`, negative level/exp, mutation loss), `get_spell` raises "ERROR: Tried to get unknown spell". `death_entry()` exports every page on death, so the popup appears in the death flow. Error popup only; no data damage.

```cpp
const spell s = u->magic->get_spell( elem.first );   // diary.cpp:211
if( !knows_spell( sp ) ) { debugmsg( "ERROR: Tried to get unknown spell" ); ...
```

**Fix:** Build the text from the `spell_type` (`elem.first->name`, `elem.first->description`), or check `knows_spell()` first.

### EVT-B06 · Memorial log carries over between characters after save-and-quit in the same session
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/game.cpp:739`, `src/memorial_logger.cpp:117`, `src/do_turn.cpp:146`, `src/game_io.cpp:391`, `src/main.cpp:933`

The game object lives for the whole process. `game::setup()` clears stats, kills, achievements and EOC events but not the memorial logger, which is cleared only on death/suicide or when a save's `.log` file is loaded. Play character A, save and quit, create character B: B's memorial starts with A's entries, which are then saved to B's `.log` and memorial file.

```cpp
stats().clear(); kill_tracker_ptr->clear(); achievements_tracker_ptr->clear(); eoc_events_ptr->clear();
// no memorial clear
```

**Fix:** Call `memorial_logger_ptr->clear()` in `game::setup()`.

### EVT-B07 · Achievements earned while achievements are disabled are written to the global achievements folder
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/achievement.cpp:794`, `src/achievement.cpp:851`, `src/game_io.cpp:693`, `src/game_io.cpp:747`, `src/do_turn.cpp:134`, `src/debug_menu.cpp:5068`, `src/past_achievements_info.cpp:65`

Any debug-menu use calls `achievements.set_enabled( false )`, but trackers keep running and `report_achievement` still stores completions; the flag only gates the message. `write_json_achievements` writes every completed achievement without checking `enabled_`, on every save and on death. Debug-earned achievements thus reach `user/achievements`, which drives profession/scenario unlocks. `past_games_info` does filter disabled games. The arcade/unlock_all debug option relies on this leak.

```cpp
for( const auto &kv : achievements_status_ ) {
    if( kv.second.completion == achievement_completion::completed ) { ach_ids.push_back( kv.first ); }
}
```

**Fix:** Record in `achievement_state` whether achievements were enabled at completion, and write only enabled completions plus explicitly manually-given meta achievements such as `achievement_arcade_mode`.

### EVT-B08 · minimum/maximum statistics are seeded with 0; unique_value's watched and computed paths disagree
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/stats_tracker.cpp:126`, `src/stats_tracker.cpp:142`, `src/event_statistics.cpp:984`, `src/event_statistics.cpp:1030`, `src/event_statistics.cpp:1056`

`event_multiset::minimum()`/`maximum()` start at 0 and the watched states seed from them, so a minimum over an always-positive field reads 0, as does a maximum over an always-negative one. `unique_value::value()` tests for one distinct data map, while the watched state counts raw events, so after a reload with repeated identical events it reports a value and then never updates. No vanilla stat is affected; it is a modder trap.

```cpp
int minimum = 0;   // stats_tracker.cpp:128
int maximum = 0;   // :144
if( summaries.size() != 1 )            // value()
++count; if( count == 1 ) ... else if( count == 2 ) { value = cata_variant(); }   // watched state
```

**Fix:** Track whether any value was seen; return void (or a JSON `default`) for no events and seed from the first event otherwise. Use the same distinct-map rule in both unique_value paths.

### EVT-B09 · Location-based timed events are not dimension-scoped; REVERT_SUBMAP can null-deref or overwrite another dimension
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/timed_event.cpp:322`, `src/timed_event.cpp:329`, `src/timed_event.cpp:336`, `src/timed_event.cpp:337`, `src/timed_event.h:41`, `src/game.cpp:9866`, `src/game.cpp:9869`, `src/game.cpp:9874`, `src/savegame.cpp:1916`, `src/mapbuffer.cpp:410`, `data/json/effects_on_condition/nether_eocs/portal_storm_effect_on_condition.json:214`

`timed_event` stores absolute coordinates with no dimension. The manager is global and untouched by `travel_to_dimension`, which swaps the prefix and clears MAPBUFFER and overmap_buffer. REVERT_SUBMAP, UPDATE_MAPGEN and TRANSFORM_RADIUS fired after a dimension change hit the same coordinates in the wrong dimension, and REVERT_SUBMAP dereferences `lookup_submap()` without a null check (it can return nullptr). Vanilla path: the portal-storm end EOC fires the `portal_dungeon_entrance` reverts, and the storm can end while the player is in the dungeon dimension. Usually this stamps the dungeon dimension and leaves the default entrance unreverted; a crash needs a dimension with no submap at those coordinates.

```cpp
case timed_event_type::REVERT_SUBMAP: {
    submap *sm = MAPBUFFER.lookup_submap( map_point );
    sm->revert_submap( revert );
```

**Fix:** Add a serialized `dimension_id` to `timed_event`, set from `g->get_dimension()` when queued. In `process()`, defer location-based events from other dimensions (or apply them through a dimension-aware mapbuffer). Always null-check `lookup_submap()`.

### EVT-B10 · `trigger_event` / `make_dyn` accept unvalidated strings; bad args crash later via `std::stoi` and can poison the save
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/event_bus.cpp:123`, `src/event_bus.cpp:176`, `src/npctalk.cpp:8488`, `src/cata_variant.h:226`, `src/cata_variant.h:242`, `src/cata_variant.h:302`, `src/math_parser_diag_value.cpp:109`, `src/math_parser_diag_value.cpp:148`, `src/effect_on_condition.cpp:611`, `src/stats_tracker.cpp:374`, `src/kill_tracker.cpp:110`

`make_dyn` stores each argument string without parsing. int_, bool_ and character_id fields are parsed later with `std::stoi`, from `eoc_events::notify` context building, kill_tracker, memorial_logger or statistics. A non-numeric value (`"true"` for a bool, a name for an int) throws `std::invalid_argument` with no catch around the game loop. stats_tracker, subscribed first, has already stored the bad event, so it is saved and can crash later loads. Numbers are stringified with default ostream precision, so 1000000 becomes `"1e+06"` and `stoi` reads 1. `trigger_event` can also fake engine events (`game_start` trips `cata_assert( initial_scores.empty() )`; `game_avatar_new` retargets avatar statistics). See EVT-J02 for the documentation side.

```cpp
cata_variant::from_string( Spec::fields[I].second, std::move( args[I] ) )   // event_bus.cpp:127
static int from_string( const std::string &v ) { return std::stoi( v ); }   // cata_variant.h:302, "TODO: check for int-ness"
```

**Fix:** Validate each argument in `make_dyn` with `convert<Type>::is_valid` (implement the int-ness TODOs) and reject with a debugmsg naming the EOC and field. Format ints with full precision. Deny engine-only types (`game_start`, `game_load`, `game_save`, `game_over`, `game_avatar_new`, `player_gets_achievement`) in `trigger_event`.

### EVT-B11 · Diary "New/Lost Bionic" and "New mission" diffs are inverted
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/diary.cpp:156`, `src/diary.cpp:232`, `src/diary.cpp:245`, `src/diary.cpp:264`, `src/diary.cpp:275`

`bionic_change` returns an entry when the bionic IS on the previous page, so "New Bionics" lists bionics already owned and the swapped "Lost" pass lists bionics still installed. `mission_change` has the same inverted test for active missions. `trait_change` and `prof_change` use `== end()`, pointing to a copy-paste slip. Display only.

```cpp
if( !prevpage || std::find( prevpage->bionics.begin(), prevpage->bionics.end(), elem ) != prevpage->bionics.end() ) {
```

**Fix:** Use `== end()` in `bionic_change`. Split `mission_change` into new (absent from previous active list) and resolved missions.

### EVT-B12 · A score's own `description` is never displayed
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/event_statistics.cpp:1298`, `src/event_statistics.cpp:1308`, `src/event_statistics.cpp:1312`, `src/event_statistics.cpp:1321`, `doc/JSON/JSON_INFO.md:2402`

`score::description` only checks whether `description_` is empty, then formats the statistic's description in both branches. The documented `"description": "Headshots: %s"` therefore shows the statistic's text, usually without the value. The doc also says the default is "<statistic description>: <value>", but the code prints value first. `value(stats)` is computed twice. No shipped score sets a description. (Merges a JSON-lens and a bug-lens finding.)

```cpp
if( description_.empty() ) { return string_format( _( "%2$s %1$s" ), desc, value_string ); }
else { return string_format( desc, value_string ); }
```

**Fix:** Use `string_format( description_.translated(), value_string )` in the else branch, reuse `val`, fix the doc, and warn at load when the description lacks `%s`.

### EVT-B13 · `report_achievement` dereferences `trackers_.find()` without checking; debug "unlock all" twice is UB
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/achievement.cpp:797`, `src/achievement.cpp:799`, `src/achievement.cpp:808`, `src/debug_menu.cpp:3977`, `src/npctalk.cpp:5822`

The already-reported case is guarded only by `cata_assert` (gone in release builds), and the tracker iterator is dereferenced and erased without an `end()` check. Debug unlock_all calls it with no checks, so running it twice, or in a save that predates `achievement_arcade_mode`, is undefined behaviour. The EOC `give_achievement` path checks properly.

```cpp
cata_assert( !achievements_status_.count( a->id ) );
auto tracker_it = trackers_.find( a->id );
achievements_status_.emplace( a->id, achievement_state{ comp, calendar::turn, tracker_it->second.current_values() } );
trackers_.erase( tracker_it );
```

**Fix:** Return early if a status already exists, use empty final values when there is no tracker, and have unlock_all check `is_completed()` first.

### EVT-B14 · Snippet library keeps stale `EOC_by_id`/`name_by_id` across world loads; category override cannot reuse ids
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/text_snippets.cpp:25`, `src/text_snippets.cpp:77`, `src/text_snippets.cpp:84`, `src/text_snippets.cpp:187`, `src/text_snippets.h:129`, `src/item_info.cpp:310`, `src/init.cpp:770`

`clear_snippets()` clears the category and id maps but never `EOC_by_id` or `name_by_id`. Since `EOC_by_id` is only written when `effect_on_examine` is present, a snippet with the same id in a later world can run the previous world's examine EOC (contrived). Deterministically, `"override": true` clears only the category's id list while `snippets_by_id` keeps the ids, so redefining them throws "Duplicate snippet id".

```cpp
hash_to_id_migration = std::nullopt; snippets_by_category.clear(); snippets_by_id.clear();   // no EOC_by_id / name_by_id
```

**Fix:** Clear `EOC_by_id` and `name_by_id` in `clear_snippets()`. On override, erase the category's previous ids from all three maps.

### EVT-B15 · Memorial entries misattribute or misstate what happened
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/memorial_logger.cpp:348`, `src/memorial_logger.cpp:577`, `src/memorial_logger.cpp:603`, `src/memorial_logger.cpp:806`, `src/memorial_logger.cpp:1126`, `src/character_health.cpp:630`, `src/npc.cpp:3380`

(1) `triggers_alarm` ignores its `character` field, so an NPC's alarm is logged as the avatar's; `eats_sewage` has no character field. (2) Every NPC the avatar kills is logged as an innocent killed "in cold blood", including hostile bandits; `apply_murder_penalties` distinguishes via `hit_by_player` and `has_flag(CANNIBAL)`, the logger uses `has_trait`. (3) The text memorial's "Ongoing Effects:" section only prints pain (and lacks an eol). Cosmetic.

```cpp
case event_type::triggers_alarm: { add( ... "Set off an alarm." ...); break; }   // no ch == avatar_id check
```

**Fix:** Check the character field for `triggers_alarm`; add one to `eats_sewage`. Add a `victim_was_hostile` bool to `character_kills_character` (useful to EOCs too) and choose the message from it. List `u.get_effects()` in the text memorial.

### EVT-B16 · Delayed `mapgen_update` (UPDATE_MAPGEN) drops the mission context the immediate path passes
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/timed_event.cpp:329`, `src/npctalk.cpp:5105`, `src/npctalk.cpp:5109`, `src/npctalk.cpp:5115`, `src/mapgen.cpp:2070`, `src/mapgen.cpp:2646`, `src/mapgen.cpp:3251`

With `time_in_future <= 0`, `mapgen_update` passes the talker's `selected_mission()`, so mapgen can bind mission targets. When delayed, it is queued as UPDATE_MAPGEN and run with `mission == nullptr`. No data/json combines a delayed update with `"target": true`, and the docs don't mention the difference.

```cpp
run_mapgen_update_func( update_mapgen_id( ... ), omt_pos, {}, d.actor( d.has_beta )->selected_mission() ); // immediate
run_mapgen_update_func( update_mapgen_id( string_id ), project_to<coords::omt>( map_point ), {}, nullptr ); // delayed
```

**Fix:** Store the mission uid (and optionally context variables) in `timed_event`, serialize it, and resolve it with `mission::find()` when the event fires.

### EVT-B17 · `watcher_set::send_to_all` tolerates only self-erasure; re-entrant dispatch can invalidate the prepared iterator
**Severity:** Low · **Effort:** Medium · **Verified:** plausible
**Where:** `src/stats_tracker.h:150`, `src/stats_tracker.h:159`, `src/stats_tracker.cpp:280`, `src/achievement.cpp:686`, `src/achievement.cpp:808`, `src/game.cpp:442`, `src/effect_on_condition.cpp:602`

`send_to_all` precomputes `next` before each callback. When an achievement completes, `report_achievement` destroys its watchers and then synchronously sends `player_gets_achievement` through the bus, including EVENT EOCs. If such an EOC triggers another event that completes a second achievement whose watcher is the outer loop's `next`, the iterator dangles. Not reachable in vanilla (no consumers of those events, no achievement with two requirements on one statistic); mod-dependent.

```cpp
auto current = watchers_.begin();
while( current != watchers_.end() ) { auto next = current; ++next; (...)( args... ); current = next; }
```

**Fix:** Iterate over a snapshot and skip watchers removed meanwhile, or queue achievement callbacks and their events until the outermost `stats_tracker::notify` returns.

## 3. Performance

### EVT-P01 · Every game load pulls in every overmap the avatar ever visited (`overmap_special_at` recompute)
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/event_field_transformations.cpp:70`, `src/overmapbuffer.cpp:100`, `src/overmapbuffer.cpp:384`, `src/overmapbuffer.cpp:1202`, `src/event_statistics.cpp:407`, `src/event_statistics.cpp:479`, `src/event_statistics.cpp:1171`, `src/event_statistics.cpp:1191`, `src/stats_tracker.cpp:240`, `src/achievement.cpp:928`, `src/savegame.cpp:325`, `data/json/statistics.json:846`

On load, `init_watchers` creates a state for each pending achievement. The 16 core achievements on `last_overmap_special_avatar_entered` build a last_value state whose `init()` calls `stat->value()`, which rebuilds `avatar_enters_overmap_special` from the whole `avatar_enters_omt` history (one summary per distinct OMT ever entered, keyed on `pos`). For each, the `overmap_special_at` transformation calls `overmap_buffer.overmap_special_at` -> `get_om_global` -> `overmapbuffer::get`, which loads and populates any overmap not in memory. Every overmap the player crossed is read from disk on each load and stays resident, and the pass repeats when the transformation state is built (see EVT-P03). In multi-dimension saves it is worse: the overmap buffer is cleared on dimension travel but stats are not, so positions from another dimension cause new overmaps to be generated in the current one.

```cpp
const tripoint_abs_omt p( v.get<tripoint>() );
std::optional<overmap_special_id> special = overmap_buffer.overmap_special_at( p );
// overmapbuffer.cpp:111-114: "That constructor loads an existing overmap or creates a new one." ... new_om.populate();
```

**Fix:** (1) Record the special when the event is emitted (game.cpp:11557, where the overmap is loaded) as an `overmap_special` field of `avatar_enters_omt`, and read it directly in statistics.json; old saved events without it are skipped. (2) Give last_value/first_value stats a fast path that walks summaries by time and transforms lazily. (3) Make `overmap_special_at` return `{}` for unloaded overmaps via a non-creating lookup, so a statistic can never cause overmap I/O.

### EVT-P02 · `past_games_info` parses every memorial file in full, but only legacy v0 data is used
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/past_games_info.cpp:40`, `src/past_games_info.cpp:56`, `src/past_games_info.cpp:155`, `src/past_games_info.cpp:167`, `src/past_games_info.h:59`, `src/past_achievements_info.cpp:19`, `src/past_achievements_info.cpp:24`, `src/past_achievements_info.cpp:76`, `src/profession.cpp:773`, `src/scenario.cpp:687`, `src/game.cpp:426`

The first `get_past_achievements()` call (character creation `can_pick`, the scores UI, or the first achievement with the "first" popup option, mid-game) parses every memorial JSON in full: log, a whole stats_tracker, an achievements_tracker and scores, all kept in memory for the session. Only version-0 files feed `legacy_achievements_`, and nothing else reads `info_` (the header says as much). For players with many long-game memorials this is a visible hitch and wasted memory. `is_completed` also copies a vector of names to test emptiness (trivial).

```cpp
jo.read( "log", log_ ); stats_ = std::make_unique<stats_tracker>(); jo.read( "stats", *stats_ );
achievements_ = ...; jo.read( "achievements", *achievements_ ); jo.read( "scores", scores_ );
legacy_achievements = version == 0;
```

**Fix:** Read `memorial_version` first (it is the first member written) and skip v1+ files entirely. For v0 files keep only the avatar name and achievement ids. Make `is_completed()` a lookup without copies.

### EVT-P03 · Stat/transformation values recomputed from raw history: O(depth²·N) at load, full copies on `value()`
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/event_statistics.cpp:372`, `src/event_statistics.cpp:407`, `src/event_statistics.cpp:479`, `src/event_statistics.cpp:526`, `src/event_statistics.cpp:631`, `src/event_statistics.cpp:643`, `src/event_statistics.cpp:766`, `src/event_statistics.cpp:1052`, `src/event_statistics.cpp:1123`, `src/event_statistics.cpp:1300`, `src/stats_tracker.cpp:240`, `src/stats_tracker.cpp:246`, `src/stats_tracker.cpp:280`, `src/math_parser_diag.cpp:147`

`event_source::get()` returns `event_multiset` by value and never uses the incrementally maintained state: event types are deep-copied, transformations are rebuilt from raw events. At load, each of ~250 stat states calls `value()` (full chain recompute) and then `add_watcher`, which builds parent states that each run their own from-scratch `initialize()`, so a depth-d chain costs about d² passes over N distinct summaries. Core data has 158 transformations (15 on `vehicle_moves` up to depth 4, 9 on `avatar_moves`); 106 use `equals_statistic: avatar_id`, whose temporary constraints register a watcher, compute a value, and unregister through an O(n) scan. `unique_value`/`first_value` call `get()` twice, and `initialize()` copies each key map twice. The same path serves the `event_statistic()` math function, `score::description` and the memorial writer. Cost is mostly at load and scales with distinct summaries.

```cpp
event_multiset get( stats_tracker &stats ) const override { return stats.get_events( type ); }  // by value
data_( trans->initialize( stats ) ), ... trans->source_->add_watcher( stats, this );
return initialize( source_->get( stats ).counts(), cached_value_constraints, stats );
```

**Fix:** Return `const event_multiset &` from `event_source::get`; for transformations ensure the parent state exists first and return its `data_` (O(d·N) total). Have `stats_tracker::value_of` return the existing state's value. Read null-parent constraint values without registering a watcher. Compute counts once. Store each watcher's key so `unwatch` is O(1).

### EVT-P04 · Transformation pipeline deep-copies event maps before filtering, on every avatar step and vehicle tile
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/event_statistics.cpp:340`, `src/event_statistics.cpp:490`, `src/event_statistics.cpp:580`, `src/event_statistics.cpp:601`, `src/event_field_transformations.cpp:30`, `src/stats_tracker.cpp:363`, `src/game.cpp:11547`, `src/vehicle.cpp:6377`

`match_and_transform` copies the whole input map, copies again per new_field output, and only then applies constraints and erases drop_fields. 151 of 158 core transformations are constraint/drop-only and most reject the event (e.g. `avatar_on_board == true` for other vehicles), but still pay the copy. Per avatar step that is roughly 20-30 small map copies; per vehicle tile about 15 transformations up to depth 4. Roughly 10-20 µs per step: a cheap cleanup, not a hotspot.

```cpp
EventVector result = { input_data };   // event_statistics.cpp:580, before any constraint check
```

**Fix:** Check constraints on input fields against `input_data` before copying and return early; copy only accepted events, skipping drop_fields during the copy; build constraint-only outputs directly.

### EVT-P05 · `vehicle_moves` fires per tile for every vehicle and is stored keyed by raw velocity
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/vehicle.cpp:3717`, `src/vehicle.cpp:6360`, `src/vehicle.cpp:6377`, `src/map.cpp:998`, `src/map.cpp:1274`, `src/event.h:977`, `src/event_bus.cpp:110`, `src/stats_tracker.cpp:189`, `src/stats_tracker.cpp:363`

`vehicle::on_move` sends `vehicle_moves` for every moving vehicle on every tile step (up to 100 steps per turn), with 11 stringified fields and talkers for all 6 subscribers (EVT-P08). Every core vehicle statistic requires avatar involvement, yet all moves are stored forever, keyed by the unquantized `velocity`, and `vehicle_moves_with_abs_velocity` materializes them all. Save and memory size grow with the number of distinct velocity/flag/z combinations. NPC-driven and runaway vehicles are rare, and velocity cannot simply be dropped (max_velocity_* stats back core achievements). The bloat is real but unmeasured.

```cpp
cata::event e = cata::event::make<event_type::vehicle_moves>( is_passenger( pc ), player_in_control( here, pc ), ...
    skidding, velocity, sm_pos.z() );
get_event_bus().send_with_talker( this, &pc, e );
```

**Fix:** Check first whether the avatar is on board, driving or remote-controlling; otherwise dispatch only to EOC listeners when any exist (expose `has_listeners(event_type)`). Bucket velocity in the stored key (e.g. 5 mph). Optionally let `event_spec` mark high-frequency types as not recorded in stats history.

### EVT-P06 · `eoc_events::notify` rebuilds per-event context and dialogue inside the per-EOC loop
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/effect_on_condition.cpp:602`, `src/effect_on_condition.cpp:603`, `src/effect_on_condition.cpp:607`, `src/effect_on_condition.cpp:613`, `src/effect_on_condition.cpp:624`, `src/effect_on_condition.cpp:633`, `src/effect_on_condition.h:45`, `src/overmapbuffer.cpp:1534`, `src/math_parser_diag_value.cpp:145`

Alpha resolution (including the `find_npc` scan over all loaded overmaps) runs once per event, not per EOC, as the reviewer corrected. What does repeat per EOC is rebuilding the context map from every event field (with int parsing), default-constructing then reassigning the dialogue, and cloning two talkers. `event_EOCs` stores full copies of each EOC (memory, not time). Minor next to `eoc.activate` itself.

```cpp
dialogue d; global_variables::impl_t context;
for( const auto &val : e.data() ) { context[val.first] = diag_value{ val.second }; }
d = dialogue( alpha->clone(), beta ? beta->clone() : nullptr, {}, context );
```

**Fix:** Build the context once per event, construct the dialogue directly, compare against `get_avatar().getID()` before `find_npc`, and store `const effect_on_condition *` in `event_EOCs`.

### EVT-P07 · Stats save/load deep-copies every multiset and writes a verbose variant encoding
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/savegame_json.cpp:4931`, `src/savegame_json.cpp:4939`, `src/savegame_json.cpp:4956`, `src/savegame_json.cpp:4964`, `src/savegame.cpp:144`, `src/memorial_logger.cpp:413`, `src/stats_tracker.h:60`, `src/event.h:231`

Every save serializes the full stats_tracker. `event_multiset::serialize` first copies all summaries into a vector; `deserialize` reads into a vector and copies again. Each field is written as `["int","5"]`, although the reader already accepts plain ints and bools. Stats grow with play (one `avatar_enters_omt` entry per OMT visited), so save time and size grow too. The copy is a small part of total save time.

```cpp
std::vector<summaries_type::value_type> copy( summaries_.begin(), summaries_.end() );
jsout.start_array(); jsout.write_as_string( type_ ); jsout.write( value_ ); jsout.end_array();
```

**Fix:** Iterate `summaries_` directly when writing; `reserve`/move on load; write int_/bool_ as JSON scalars; optionally drop the type tag and recover it from `event::get_fields(type)` on load.

### EVT-P08 · `event_bus::send_with_talker` allocates two talkers per subscriber; 5 of 6 discard them
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/event_bus.cpp:32`, `src/event_bus.cpp:92`, `src/event_bus.cpp:100`, `src/event_bus.cpp:108`, `src/event_subscriber.h:22`, `src/effect_on_condition.h:41`, `src/effect_on_condition.cpp:580`, `src/game.cpp:480`, `src/monster.cpp:2520`, `src/vehicle.cpp:6382`

`get_talker_for` is called inside the subscriber loop: about 12 heap allocations per event across 6 subscribers. Only `eoc_events` uses the talkers, and it clones them again. Hot callers include `monster::apply_damage` and `vehicle::on_move`.

```cpp
for( event_subscriber *s : subscribers ) { s->notify( e, get_talker_for( alpha ), get_talker_for( beta ) ); }
```

**Fix:** Make the virtual overload take non-owning pointers and build talkers once outside the loop, or pass the raw Creature/vehicle/item pointers and build talkers only in `eoc_events` when an EOC exists for that type.

### EVT-P09 · AMIGARA_WHISPERS scans the whole z-level every turn before its 10-second gate
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/timed_event.cpp:223`, `src/timed_event.cpp:371`, `src/timed_event.cpp:399`, `src/do_turn.cpp:565`, `src/computer_session.cpp:897`

While the 5-minute AMIGARA_WHISPERS event is queued, `per_turn` walks all `points_on_zlevel()` (~17k tiles) for `t_fault` every turn, then checks `once_every( 10s )`, wasting about 90% of the scans. Rare event, trivial fix.

```cpp
for( const tripoint_bub_ms &p : here.points_on_zlevel() ) { if( here.ter( p ) == ter_t_fault ) { faults = true; break; } }
if( calendar::once_every( time_duration::from_seconds( 10 ) ) && faults ) {
```

**Fix:** Check `once_every` first, or cache the fault position when the event is queued.

### EVT-P10 · Event data stored as `map<string, cata_variant>`: values stringified, re-parsed and copied
**Severity:** Low · **Effort:** Large · **Verified:** plausible
**Where:** `src/event.h:1002`, `src/event.h:1044`, `src/event.cpp:260`, `src/cata_variant.h:163`, `src/cata_variant.h:297`, `src/cata_variant.h:500`, `src/cata_variant.h:507`, `src/cata_variant.h:539`, `src/stats_tracker.cpp:16`, `src/stats_tracker.cpp:189`, `src/event_statistics.cpp:540`, `src/event_statistics.cpp:882`

Every event field is a map node plus a string key; ints, bools and int_ids are stringified and parsed back on each read (`std::stoi`, factory lookups). `get_variant` returns by value, and cata_variant `==`, `<` and hash go through `as_pair()`, which copies the value string. These run when hashing summaries, matching constraints and updating stats. Hotness is not shown: events fire about once per action or vehicle tile with short strings, so the per-event cost is microseconds.

```cpp
using data_type = std::map<std::string, cata_variant>;
std::pair<cata_variant_type, std::string> as_pair() const { return std::make_pair( type_, value_ ); }
```

**Fix:** Cheap steps: return `const cata_variant &` from `get_variant`, use `std::less<>` for string_view lookups, and implement `==`/`<`/hash directly on `type_`/`value_`. A redesign (array indexed by spec position, native int/bool storage) is not justified by current evidence.

## Appendix: considered and rejected

- kill_tracker::guilt_kill_count rescans all kills and allocates on every morale query – `kills` is keyed by distinct mtype (dozens to low hundreds), each step is a cached lookup plus a bitset test, and only the avatar calls it a few times per turn; the neighbouring `has_flag` calls cost more, so caching would not measurably help.
