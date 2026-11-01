# 26 · Activities & zones

> Engine audit – static code reading at commit `a1a646d6`; nothing was compiled or run. Each finding was checked against the code by a separate reviewer: **confirmed** = verified by reading the code; **plausible** = consistent with the code, but reachability or impact not proven. Line numbers refer to that commit.

**Scope:** `src/activity_actor.cpp`, `src/activity_handlers.cpp`, `src/activity_item_handling.cpp`, `src/activity_type.cpp`, `src/activity_tracker.cpp`, `src/player_activity.cpp`, `src/clzones.cpp`, `src/distraction_manager.cpp`, `src/zone_manager_ui.cpp`

## Summary

The activity system is mid-migration to actors, and the EOC layer has not followed: `u_assign_activity` cannot start any actor-backed activity (so vanilla godco dialogue already fails), the completion hooks miss most real completions, and `activity_type` is not a `generic_factory`, so mods can't attach hooks to vanilla activities. Zone semantics and many activity outcomes are keyed to hardcoded vanilla ids. On the bug side there is a reachable use-after-free in butchery, zone repair that is instant, and zone sorting that carries off the sorter's own matching stacks. The zone multi-activity code is the main performance hotspot: zone tile sets are copied by value on every query, and the scans re-run weather simulation, requirement inventories and A* probes per candidate tile.

| Lens | Critical | High | Medium | Low | Total |
|---|---|---|---|---|---|
| Missing JSON / EOC | 0 | 3 | 9 | 6 | 18 |
| Bugs & quirks | 1 | 2 | 5 | 7 | 15 |
| Performance | 0 | 3 | 6 | 5 | 14 |
| **Total** | **1** | **8** | **20** | **18** | **47** |

**Fix first:**
1. **ACT-B01** · Butchery `do_turn` uses a destroyed `butchery_data` – a crash reachable from "butcher all", auto-butcher and LOOT_CORPSE zones, and the fix is a few lines.
2. **ACT-B02** · Zone vehicle repair is instant and can loop forever – an exploit plus an infinite loop, both fixed by small arithmetic changes.
3. **ACT-P01** · Zone point-set getters return sets by value – returning a const reference removes millions of allocations per sort run.
4. **ACT-J01** · `activity_type` loader is not a `generic_factory` – a small change that lets mods override vanilla activities and attach EOC hooks.
5. **ACT-J02** · `u_assign_activity` can't start actor-backed activities – vanilla godco/Helena/Jeremiah dialogue and the documented example already fail at runtime.

## 1. Missing JSON bindings & EOC features

### ACT-J01 · `activity_type` loader is not a `generic_factory`: no overrides of vanilla activities, EOC ids unvalidated
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/activity_type.cpp:88`, `src/activity_type.cpp:92`, `src/activity_type.cpp:101`, `src/activity_type.cpp:108`, `src/activity_type.cpp:112`, `src/activity_type.cpp:116`, `src/activity_type.cpp:122`, `src/activity_type.cpp:125`, `doc/PLAYER_ACTIVITY.md:88`

`load_all` builds each `activity_type` from scratch and rejects a second definition of an id with a "Redefinition" debugmsg. A mod therefore cannot add `do_turn_eoc`/`completion_eoc` to `ACT_READ` or `ACT_BUTCHER`, or change `activity_level`, `interruptable` or `rooted` on a vanilla activity; `copy-from`, `extend` and `delete` don't work either. This is the main reason the EOC hooks are only usable on mod-created activities. `completion_eoc` and `do_turn_eoc` are loaded as bare ids (no inline EOCs, unlike bionics, items and martial arts) and `check_consistency` never validates them, so a typo surfaces only at runtime. The `verb` default is the non-empty `"THIS IS A BUG"`, so the `verb_.empty()` check can never fire. `PLAYER_ACTIVITY.md` does not document `ignored_distractions`, `fetch_items_to_zone` or `mute_npc_completion`.

```cpp
optional( jo, was_loaded, "verb", verb_, to_translation( "THIS IS A BUG" ) );   // :92
optional( jo, was_loaded, "completion_eoc", completion_EOC );                     // :101
// FIXME: use generic factory
if( activity_type_all.count( result.id_ ) ) {
    debugmsg( "Redefinition of %s", result.id_.c_str() );                         // :112-117
```

**Fix:** Move to `generic_factory<activity_type>` (the loader already uses `mandatory`/`optional` with `was_loaded`), e.g. `{ "type": "activity_type", "id": "ACT_READ", "copy-from": "ACT_READ", "do_turn_eoc": { "id": "MY_READ_TICK", "effect": [ ... ] } }`. Load the EOC fields with `effect_on_conditions::load_inline_eoc`, validate them in `check_consistency`, make `verb` mandatory (or drop the fake default), and document the three missing fields.

### ACT-J02 · `u_assign_activity` can't start any actor-backed activity; vanilla JSON and the doc example already break
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/npctalk.cpp:5685`, `src/npctalk.cpp:5693`, `src/character.cpp:5338`, `src/character.cpp:5342`, `src/activity_actor.cpp:15569`, `src/activity_actor.cpp:15608`, `src/activity_actor.cpp:15686`, `data/json/npcs/godco/godco_missions.json:1437`, `data/json/npcs/godco/members/NPC_Helena_Misinter.json:321`, `data/json/npcs/godco/members/NPC_Jeremiah_Weaver.json:138`, `doc/JSON/EFFECT_ON_CONDITION.md:4800`, `src/npctalk.cpp:3241`, `src/npctalk_funcs.cpp:231`

`f_assign_activity` always calls `Character::assign_activity( activity_id, moves )`, which debugmsgs and returns for any id that has an actor. About 124 of 139 vanilla activity types are actor-based (`ACT_WAIT`, `ACT_WAIT_NPC`, `ACT_READ`, `ACT_GAME`, `ACT_MEDITATE`, every `ACT_MULTIPLE_*`). Only `ACT_TARGET_PRACTICE` is special-cased. Vanilla content hits this today: the godco mission end and Helena's dialogue use `ACT_WAIT_NPC`, Jeremiah uses `ACT_GAME`, and the documented `{ "u_assign_activity": "ACT_GAME" }` example fails the same way. The string talk effects for zone jobs (`sort_loot`, `do_farming`, ...) only act on a beta NPC, so no EOC can put the avatar on a zone job, and none can start reading, waiting or crafting for anyone.

```cpp
if( a == ACT_TARGET_PRACTICE ) { target->assign_activity( target_practice_activity_actor() ); }
else { target->assign_activity( a, to_moves<int>( dov.evaluate( d ) ) ); }   // npctalk.cpp:5693
// character.cpp:5342
if( actor_ptr.first == type ) { debugmsg( "Tried to assign generic activity %s to activity that has an actor!" ... ); return;
```

**Fix:** Add an optional static `from_eoc( const JsonObject &, dialogue & )` factory per actor, registered next to `deserialize_functions`, and use it in `f_assign_activity`. Zero-argument actors (all `multi_*`, wait, meditate) can be covered immediately. Syntax: `{ "u_assign_activity": "ACT_WAIT_NPC", "duration": "30 m" }`, `{ "u_assign_activity": "ACT_READ", "item": { "context_val": "book" } }`, `{ "npc_assign_activity": "ACT_MULTIPLE_FARM" }`. Fix the godco JSON and the doc example in the same change, and let the string talk effects fall back to the alpha talker when there is no beta NPC.

### ACT-J03 · Activity completion hooks miss most real completions and carry no context
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/player_activity.cpp:273`, `src/player_activity.cpp:350`, `src/player_activity.cpp:360`, `src/player_activity.cpp:392`, `src/player_activity.cpp:398`, `src/activity_actor.cpp:6970`, `src/activity_actor.cpp:7578`, `src/crafting.cpp:5469`, `src/activity_actor.cpp:5400`, `src/event.h:331`, `data/mods/aftershock_exoplanet/EOC/hacking_eoc.json:9`

`completion_eoc` and `character_finished_activity(canceled=false)` are only sent from the `moves_left <= 0` branch of `player_activity::do_turn`. A successful craft ends via `crafter.cancel_activity()` (event arrives with `canceled=true`, no `completion_eoc`); drop does the same; disassembly calls `activity.set_to_null()` (no event, no EOC); every multi-zone job assigns `you.activity = player_activity()` (no event). When `completion_eoc` does run, it runs before `actor->finish()`, so it can't see the result. `completion_eoc` and `do_turn_eoc` get a bare dialogue with no context vars, target or beta talker, and there is no start or cancel EOC. Content already works around this (aftershock hacking sets an effect and schedules its real logic with `time_in_future`, which still fires if the player cancels).

```cpp
if( *this && moves_left <= 0 ) {
    if( !type->completion_EOC.is_null() ) { dialogue d( get_talker_for( you ), nullptr ); ... }
    get_event_bus().send<event_type::character_finished_activity>( you.getID(), type, false );
    ... actor->finish( *this, you );                          // player_activity.cpp:350-360
crafter.cancel_activity(); crafter.complete_craft( craft_copy, location );   // activity_actor.cpp:6970
```

**Fix:** Route every end of an activity through one function, e.g. `player_activity::end( who, outcome )` with outcome completed/canceled/interrupted, which fires the event (with a new `outcome` string field) and the matching EOC after `actor->finish`. Add `start_eoc` and `cancel_eoc` next to `completion_eoc`, all inline-capable, and pass context vars `activity`, `moves_total`, `progress`, `target_pos`, plus a beta item talker for `targets.front()`. Let `u_assign_activity` accept `"context": { ... }`.

### ACT-J04 · `u_has_activity` silently ignores its string argument; no EOC access to the current activity
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/condition.cpp:539`, `src/condition.cpp:546`, `src/condition.cpp:2597`, `src/condition.cpp:132`, `src/talker_character.cpp:770`, `src/talker.h:481`, `doc/JSON/NPCs.md:1050`, `src/event.h:331`, `src/event.h:485`

The condition is registered with `jarg::string`, so `{ "u_has_activity": "ACT_READ" }` loads, but the 3-arg overload discards the id and returns true for any activity. Modders get a silent wrong answer. Apart from the `activity` context var on the start/finish events, EOCs cannot query the current activity id, its progress or `moves_left` at an arbitrary time; `math_parser_diag.cpp` has no activity function.

```cpp
conditional_t::func f_has_activity( const JsonObject &, std::string_view, bool is_npc )
{ return f_has_activity( is_npc ); }                         // condition.cpp:546
{"u_has_activity", "npc_has_activity", jarg::string, &conditional_fun::f_has_activity },   // :2597
```

**Fix:** Honour the argument: `{ "u_has_activity": "ACT_READ" }` or `{ "u_has_activity": [ "ACT_BUTCHER", "ACT_BUTCHER_FULL" ] }`, keeping the bare form for "any". Add math functions `u_activity_id()`, `u_activity_progress()` (0-1) and an assignable `u_activity_moves_left()`, plus a `u_activity_target_location` location-variable effect, with `npc_` variants.

### ACT-J05 · Mapgen `zones` can't set zone options (farm seed, blueprint, unload, study)
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/clzones.cpp:2045`, `src/clzones.cpp:2051`, `src/mapgen.cpp:3404`, `src/mapgen.cpp:3411`, `doc/JSON/MAPGEN.md:1157`, `src/clzones.cpp:552`

`jmapgen_zone` reads only `type`, `faction`, `name` and `filter`, and `mapgen_place_zone` only sets a loot filter for `LOOT_CUSTOM`/`LOOT_ITEM_GROUP`. Everything else gets `zone_options::create` defaults: a mapgen `FARM_PLOT` tills and harvests but never plants, a `CONSTRUCTION_BLUEPRINT` has a default construction id (useless or wrong). Mod camps and prebuilt bases can't ship working farm or blueprint zones, and since EOCs place zones through update mapgen, the gap carries over to EOCs.

```cpp
auto options = zone_options::create( type );
if( type == zone_type_LOOT_CUSTOM || type == zone_type_LOOT_ITEM_GROUP ) { ... set_mark( filter ); }   // clzones.cpp:2051-2058
```

**Fix:** Accept an `"options"` object and pass it to the existing `zone_options::deserialize`: `{ "type": "FARM_PLOT", "faction": "your_followers", "options": { "seed": "seed_corn", "fertilizer": "fertilizer_commercial" } }`, `{ "type": "CONSTRUCTION_BLUEPRINT", "options": { "group": "constr_wall_log" } }`. Validate in `check()`.

### ACT-J06 · Hacking ignores the card reader's JSON and forces modded readers to disable hacking
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/activity_actor.cpp:1047`, `src/activity_actor.cpp:1106`, `src/activity_actor.cpp:1110`, `src/iexamine_actors.cpp:284`

`cardreader_examine_actor` is data-driven (`terrain_changes`, `furn_changes`, `radius`, `mapgen_id`), but `hacking_activity_actor::finish` ignores it: it sets `t_card_reader_broken` and opens `t_door_metal_locked` within a fixed radius of 3. To keep the two consistent, the reader's `finalize()` debugmsgs for any hackable reader whose changes differ from that pair, including every `mapgen_id` reader. A hacked safe always becomes `f_safe_o`, and hacking a furniture reader turns the terrain beneath into `t_card_reader_broken`.

```cpp
here.ter_set( examp, ter_t_card_reader_broken );
for( ... points_in_radius( examp, 3 ) ) {
    if( here.ter( tmp ) == ter_t_door_metal_locked ) { here.ter_set( tmp, ter_t_door_metal_c ); } }   // :1110-1114
```

**Fix:** For `hack_type::DOOR`, look up the target's `cardreader_examine_actor` and call its open/regen routine (exposed as a public method). Add actor fields `"hacked_ter"`/`"hacked_furn"` (default `t_card_reader_broken`) and drop the finalize restriction. For safes, add a furniture `"hack_result"` or reuse `lockpick_result`.

### ACT-J07 · Lockpicking: difficulty is random, alarm and jam outcomes tied to vanilla ids
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/activity_actor.cpp:3472`, `src/activity_actor.cpp:3474`, `src/activity_actor.cpp:3496`, `src/activity_actor.cpp:3528`, `src/mapdata.cpp:1384`

Terrain and furniture already have `lockpick_result`/`lockpick_message`, but difficulty is a random 3d5-3 roll for every lock (with a TODO to make it per-door). Only `t_door_locked_alarm` can trigger an alarm and only `f_gunsafe_ml` can jam. Mods can't make a vault harder than a padlock, nor alarmed or jammable doors and safes. XP gain also follows the random roll.

```cpp
// Lock_roll should be replaced with a flat value defined by the door, soon.
int lock_roll = rng( 0, 4 ) + rng( 0, 4 ) + rng( 0, 4 );                       // :3472-3474
} else if( furn_type == furn_f_gunsafe_ml && lock_roll > ( 3 * pick_roll ) ) {  // :3496
if( !perfect && ter_type == ter_t_door_locked_alarm && ...                     // :3528
```

**Fix:** Add ter/furn fields `"lockpick_difficulty": 8` (int or `[min,max]`, fallback 3d5-3), `"lockpick_alarm": true`, `"lockpick_jam_result": "f_gunsafe_mj"`. The alarm check can switch to the existing `ALARMED` flag (`mapdata.h:368`, already on `t_door_locked_alarm`) right away.

### ACT-J08 · Washing only accepts five hardcoded water and soap itypes
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/activity_actor.cpp:13404`, `src/activity_actor.cpp:13443`, `src/iuse.cpp:8322`, `src/iuse.cpp:8399`, `src/iuse.cpp:8466`

`wash_activity_actor::finish` and `iuse::wash_items` check and consume exactly `water`/`water_clean` and `soap`/`detergent`/`liquid_soap`, with ratios fixed in `washing_requirements_for_volume`. A mod's lye soap, saltwater or magic cleanser can't be used, and mods can't change what washing costs.

```cpp
!crafting_inv.has_charges( itype_soap, ... ) && !crafting_inv.has_amount( itype_detergent, ... ) &&
!crafting_inv.has_charges( itype_liquid_soap, ...                               // :13410-13413
```

**Fix:** Define JSON requirements `washing_water` and `washing_cleanser` (scaled by the computed multiplier) and use `requirement_data::can_make_with_inventory`/consume. Mods then extend them with `"extend"`.

### ACT-J09 · EOC access to zones is minimal: position only, no zone CRUD, docs over-promise
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/talker_zone.h:15`, `src/talker_zone.cpp:5`, `src/npctalk.cpp:4610`, `src/clzones.h:655`, `src/clzones.cpp:1262`, `src/npctalk.cpp:7012`, `src/npctalk.cpp:7017`, `src/npctalk.cpp:7032`, `doc/JSON/EFFECT_ON_CONDITION.md:2850`, `doc/JSON/EFFECT_ON_CONDITION.md:2867`, `doc/JSON/EFFECT_ON_CONDITION.md:4090`

`talker_zone` implements only `pos_abs`. EOCs can't read a zone's name, type, faction, enabled state or bounds, can't test "is this location in a zone of type X", and can't remove, enable or disable zones (creation is possible only through `mapgen_update` with `place_zones`). The `u_location_variable` `"zone"` search always uses `your_fac`, and the documented `""` = "any" matches nothing for zones. `u_run_fixed_zone_eocs` reads `zone_range` only as an int (a variable object produces a load error and falls back to unlimited range), can't filter by type or faction, and does not implement the documented "that you can see".

```cpp
if( mgr.get_zone_at( here.get_abs( search_loc ), zone_type_id( cur_search_target ) ) )   // npctalk.cpp:4612, fac defaults to your_fac
if( jo.has_int( "zone_range" ) ) { zone_range = jo.get_int( "zone_range" ); }            // :7017
```

**Fix:** Add a condition `{ "u_in_zone": "LOOT_FOOD", "faction": "your_followers" }` (also for location variables); talker_zone accessors for type, name and enabled state; effects `{ "create_zone": "FARM_PLOT", "start": loc, "end": loc, "faction": ..., "options": { ... } }`, `{ "remove_zones": "X", "at": loc }`, `{ "set_zone_enabled": false, ... }`. Give `u_run_fixed_zone_eocs` `"zone_type"`/`"faction"` filters and read `zone_range` with `dbl_or_var`. Make `"zone": ""` match any zone and accept `"faction"` in `u_location_variable`.

### ACT-J10 · Chopping, plank cutting, tilling and planting outputs are hardcoded to vanilla ids
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/activity_actor.cpp:10451`, `src/activity_actor.cpp:10492`, `src/activity_actor.cpp:10531`, `src/activity_actor.cpp:10707`, `src/activity_actor.cpp:7755`, `src/activity_item_handling.cpp:2281`, `src/activity_item_handling.cpp:3525`, `src/activity_item_handling.cpp:3913`, `src/iuse.cpp:4352`, `src/iuse.cpp:2575`, `src/map.cpp:9843`

Only `t_trunk` and `t_stump` can be chopped (iuse filter, zone `can_do` and actor all compare ids), yielding fixed `log`/`stick_long`/`splinter` and `t_dirt`. Plank cutting accepts only itype `log`, takes 20 minutes and yields 2x4 and splinters. Tilling any PLOWABLE terrain gives `t_dirtmound` after a fixed 18000 moves; planting resets to `t_dirt`/`t_greenhouse`. MindOverMatter's `t_trunk_fungal` can't be chopped, alien soil tills into Earth dirt, and `map::cut_down_tree` always lays a vanilla `t_trunk`.

```cpp
if( t == ter_t_trunk ) { log_quan = rng( 2, 3 ); ... } else if( t == ter_t_stump ) { ... }   // :10451
here.ter_set( pos, ter_t_dirt );                                                             // :10492
churn_activity_actor( 18000, item_location() )                                // activity_item_handling.cpp:3913
```

**Fix:** Add a ter/furn `"chop"` object modelled on `"bash"`: `"chop": { "ter_set": "t_dirt", "items": [ { "item": "log", "count": [ 2, 3 ] } ], "time": "60 m" }`, and pick chop targets by its presence (or a `CHOPPABLE` flag). Give items a `"lumber"` result (item group plus time) instead of the `itype_log` check, and add terrain `"till_result"`, `"till_time"` and `"plant_result"`.

### ACT-J11 · Fire-starting and smoker/kiln handling detect furniture by vanilla id
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/activity_actor.cpp:12957`, `src/iuse_actor.cpp:1490`, `src/iexamine.cpp:3066`, `src/iexamine.cpp:6402`, `src/iexamine.cpp:6476`, `src/iexamine.cpp:3108`

Whether lighting a fire starts `iexamine::smoker_fire`/`kiln_fire` or just places `fd_fire` depends on five hardcoded furniture ids, duplicated in `prep_firestarter_use`. Beyond that, `kiln_prep` and `smoker_prep` reject non-vanilla ids with a debugmsg and the active/full transitions are hardcoded, so a modded rack using the `smoker_options` examine action is broken even when examined directly. The reviewer raised effort to medium because detecting by examine action alone is not enough.

```cpp
const bool is_smoking_rack = f_id == furn_f_metal_smoking_rack || f_id == furn_f_smoking_rack;
const bool is_kiln = f_id == furn_f_kiln_empty || f_id == furn_f_kiln_metal_empty ||
                     f_id == furn_f_kiln_portable_empty;                          // :12958-12961
```

**Fix:** Detect by examine action or by `SMOKER`/`KILN` furniture flags via one shared helper, and replace the hardcoded empty/active/full transitions in `iexamine` with furniture fields (e.g. `"smoker_active": "..."`, `"kiln_full": "..."`).

### ACT-J12 · Zone jobs and zone semantics are a closed C++ set; `zone_type` JSON is cosmetic
**Severity:** Medium · **Effort:** Large · **Verified:** confirmed
**Where:** `src/clzones.cpp:184`, `src/clzones.cpp:194`, `src/clzones.cpp:1122`, `src/clzones.cpp:1133`, `src/clzones.cpp:1529`, `src/clzones.cpp:1234`, `src/clzones.cpp:1441`, `src/activity_item_handling.cpp:2639`, `src/activity_item_handling.cpp:2656`, `src/activity_item_handling.cpp:2692`, `src/activity_item_handling.cpp:3635`

`zone_type` (type `LOOT_ZONE`) loads only name, id, display_field, description, can_be_personal and hidden. Its options class, the multi-activity that uses it, whether that activity works in the dark or must stay in the zone, and whether it fetches tools are all hardcoded id chains. Loot *sources* (fetch, NPC trade) are decided by an id prefix of `"LOOT"`. Modders can't add working "milk animals" or "water plants" zones. Custom zones do work as sort destinations via `item_category` `zone`/`priority_zones`; `has_loot_dest_near` is dead code. `LOOT_ZONE` is undocumented.

```cpp
if( fac == z_fac && type.str().substr( 0, 4 ) == "LOOT" )                    // clzones.cpp:1122
return act_id == ACT_MULTIPLE_MOP || act_id == ACT_MOVE_LOOT || act_id == ACT_FETCH_REQUIRED;   // activity_item_handling.cpp:2639
```

**Fix:** Step 1 (small): zone_type fields `"loot_source"`, `"loot_destination"`, `"options": "plot"|"blueprint"|"loot_filter"|"unload"|"study"|"ignorable"`; activity_type fields `"zone_type"`, `"work_in_dark"`, `"must_be_in_zone"`; replace the id/prefix checks; document `LOOT_ZONE`. Step 2 (large): a generic JSON zone job run by `multi_zone_activity_actor`, e.g. `{ "type": "zone_job", "id": "MILK_ZONE", "zone_type": "WORK_MILK", "target": { "monster_flag": "MILKABLE" }, "requirements": "milking_standard", "duration": "5 m", "tile_eoc": "EOC_MILK_TILE" }`.

### ACT-J13 · Per-activity behaviours are keyed by hardcoded id lists
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/player_activity.cpp:140`, `src/player_activity.cpp:163`, `src/player_activity.cpp:179`, `src/player_activity.cpp:306`, `src/character.cpp:5338`, `src/npctalk.cpp:5685`

The "% progress" sidebar text appears only for 8 ids (all of which now have actors whose progress message overrides this branch, so the list is effectively dead), `ACT_AIM`/`ACT_INVOKE_ITEM` suppress the progress message by id, and only the four `ACT_WORKOUT_*` ids are exempt from the "You struggle to continue" stamina popup. EOC-driven mod activities can't show percent progress, and a modded training activity that drains stamina gets interrupted. Mostly cosmetic.

```cpp
bool excluded = act_id == ACT_WORKOUT_HARD || act_id == ACT_WORKOUT_ACTIVE ||
                act_id == ACT_WORKOUT_MODERATE || act_id == ACT_WORKOUT_LIGHT;   // :307-310
```

**Fix:** Add activity_type fields `"show_progress": "percent"|"none"|"verb"` and `"stamina_rest_prompt": false`, set them in `player_activities.json`, and remove the id checks.

### ACT-J14 · EOC popup `interrupt_type` only handles `portal_storm_popup`
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctalk.cpp:5571`, `src/npctalk.cpp:5665`, `src/generic_factory.h:957`, `doc/JSON/EFFECT_ON_CONDITION.md:4661`, `src/npctalk.cpp:6004`

Docs describe `interrupt_type` as any distraction type (typed "boolean", default "neutral"). The code handles only `"portal_storm_popup"` and `"default"`; any other value does nothing at all, and the log message is skipped because it sits in the `else` branch. The `"default"` branch is dead too: when the field is omitted (vanilla `reverberations.json`, `anomalous_mp3_eocs.json`) the value is empty, so both interrupt and message are dropped. `u_cancel_activity` exists but ignores the distraction manager.

```cpp
if( interrupt_type.evaluate( d ) == "portal_storm_popup" ) { g->portal_storm_query(...) }
else if( ... == "default" ) { debugmsg(...) }
// Would probably need an else-if for every possible distraction type ...      // :5666-5676
```

**Fix:** Parse with `io::string_to_enum<distraction_type>` at load (fail on unknown) and call `g->cancel_activity_or_ignore_query( type, msg )` for every type. Add `{ "u_interrupt_activity": "hostile_spotted_near", "message": "..." }`. Fix the doc type, default and value list.

### ACT-J15 · Target practice: hearing protection and "quiet gun" use hardcoded ids
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/activity_actor.cpp:4913`, `src/activity_actor.cpp:4929`

Toggle-on hearing protection is recognised from a fixed set of 6 itype ids, and "quiet" weapons are `PRIMITIVE_RANGED_WEAPON` or gun_type `bow`/`crossbow`. Impact is small: mods can add `PRIMITIVE_RANGED_WEAPON`, and toggleable earmuffs work once switched on. Only the off-state shortcut and loudness-based quietness are missing.

```cpp
gun_loc->gun_type() == gun_type_type( "bow" ) || gun_loc->gun_type() == gun_type_type( "crossbow" )   // :4929-4931
```

**Fix:** Accept items whose transform target has `DEAF`/`PARTIAL_DEAF` (or a `HEARING_PROTECTION_TOGGLE` flag); decide quietness from computed loudness or a `QUIET_PRACTICE` flag.

### ACT-J16 · Fishing: fallback catch ignores the water body; rod quality above 2 is "invalid"
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/activity_actor.cpp:4757`, `src/activity_actor.cpp:4785`, `src/activity_item_handling.cpp:4076`, `src/iuse.cpp:1458`, `src/iuse.cpp:1588`

With no visible fish, every FISHABLE tile falls back to the global `GROUP_FISH` (fish traps too). The quality switch handles only `FISHING_ROD` 1 and 2; a modded level-3 rod debugmsgs every `do_turn` for the full 5 hours. Both zone and manual fishing are hardcoded to 5 hours. No vanilla rod exceeds 2.

```cpp
switch( fishing_rod->get_quality( qual_FISHING_ROD ) ) { case 1: ... case 2: ...
    default: debugmsg( "ERROR: Invalid FISHING_ROD tool quality on %s" ...     // :4785-4797
```

**Fix:** Terrain field `"fishing_group": "GROUP_FISH_SALTWATER"` (default `GROUP_FISH`); a quality-scaled formula instead of the switch (e.g. `dice( quality^2, 6 )`); make the duration an activity_type/option value.

### ACT-J17 · Auto-eat/auto-drink zones are avatar-only with magic thresholds; raw weariness invisible to EOCs
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/player_activity.cpp:222`, `src/activity_item_handling.cpp:4512`, `src/activity_item_handling.cpp:4545`, `src/activity_item_handling.cpp:4589`, `src/activity_tracker.h:27`, `src/suffer.cpp:1436`

`auto_needs` runs only for the avatar, with hardcoded thresholds (stomach <= 1/4, kcal < 95%, thirst > 130, food < 50 kcal and drinks < 15 quench rejected, fun < -5 rejected). NPCs have their own food seeking, so their exclusion matters less. The weariness level can be read via the `weary_N` effects, but the raw tracker and intake values are not exposed. Faucets can already be added through `pseudo_tools`.

```cpp
if( calendar::once_every( 2_minutes ) && *this && !you.is_npc() ) { ...
    you.stomach.contains() <= you.stomach.capacity( you ) / 4 && you.get_kcal_percent() < 0.95f ... you.get_thirst() > 130
```

**Fix:** Move thresholds to game_balance options or `"auto_needs": { "thirst_threshold": 130, ... }` on activity_type; optionally let NPCs use auto_needs; add `u_weariness()` and an assignable `u_weariness_tracker` math function.

### ACT-J18 · Assorted content-id hardcodes (vats/compost, tree communion, troglodyte mining)
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/activity_handlers.cpp:253`, `src/activity_handlers.cpp:264`, `src/iexamine.cpp:3680`, `src/iexamine.cpp:3691`, `src/iexamine.cpp:3915`, `src/iexamine.cpp:3924`, `src/activity_actor.cpp:9738`, `src/activity_actor.cpp:9758`, `src/activity_actor.cpp:9763`, `src/activity_actor.cpp:11586`, `src/activity_actor.cpp:11597`

Draining any `fvat_full` furniture turns it into `f_fvat_empty` unless it is exactly `f_fvat_wood_full`; filling is hardcoded the same way in `iexamine` (`fvat_set_*`, `compost_set_*`), so a modded vat becomes vanilla on both transitions. Compost resets only for `f_compost_full`. Tree communion compares the translated overmap name to `_("field")` (FIXME) and checks the `SPIRITUAL` trait by id instead of its json flag. Mining strain only affects avatars and only `STOCKY_TROGLO` reduces it.

```cpp
// FIXME: this is terrible and should be a property instead of a name check...
return ter.obj().is_wooded() || ter.obj().get_name( om_vision_level::full ) == _( "field" );   // :9757-9758
```

**Fix:** Add furniture fields such as `"emptied_into"`/`"filled_into"` (furniture has no `transforms_into`). Use the existing oter `travel_cost_type` instead of the name check, the `SPIRITUAL` json flag instead of the trait id, and an enchantment value like `MINING_STRAIN_MULT` applied to NPCs too.

## 2. Bugs, oversights & quirks

### ACT-B01 · Butchery `do_turn` keeps using a `butchery_data` pointer after `bd.pop_back()` destroys it
**Severity:** Critical · **Effort:** Small · **Verified:** confirmed
**Where:** `src/activity_actor.cpp:13909`, `src/activity_actor.cpp:13913`, `src/activity_actor.cpp:13921`, `src/activity_actor.cpp:13942`, `src/activity_actor.cpp:13966`, `src/butchery.cpp:279`, `src/butchery.cpp:294`

`do_turn` takes `this_bd = &bd.back()`. If `initiate_butchery()` fails, it pops the vector and, when corpses remain, keeps reading `this_bd->corpse`, writing `this_bd->progress` and dereferencing `*this_bd->corpse`. The popped `item_location` has already released its impl (refcount 1), so this is a use-after-free through a virtual `valid()` call, most likely a crash. `set_up_butchery` fails often: missing tools, already bled/skinned/dressed, corpse too small, human-corpse refusal. Reachable from "butcher all", auto-butcher and LOOT_CORPSE multi-butcher whenever a corpse other than the last one fails. Minor related bug: `canceled()` binds `*target` before checking `if( target )`.

```cpp
butchery_data *this_bd = &bd.back();
if( this_bd->progress == 0_seconds ) {
    if( !initiate_butchery( act, you, *this_bd ) ) {
        bd.pop_back();
        if( bd.empty() ) { ...return; }
    }
}
const item_location &target = this_bd->corpse;   // dangling after pop_back
```

**Fix:** After `pop_back`, either `return` (with `moves_left = 1` so the next turn uses the new back), or re-take `this_bd = &bd.back()` and loop `initiate_butchery` until one succeeds or the queue is empty. In `canceled()`, move the dereference inside `if( target )`.

### ACT-B02 · Zone vehicle repair is instant (integer division) and can loop forever on a requirement mismatch
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/activity_item_handling.cpp:780`, `src/activity_item_handling.cpp:781`, `src/activity_item_handling.cpp:2194`, `src/activity_item_handling.cpp:2865`, `src/activity_item_handling.cpp:2855`, `src/veh_utils.cpp:111`, `src/veh_utils.cpp:122`, `src/veh_interact.cpp:194`

(1) `vehicle_activity()` computes `frac` by integer division of damage over max damage. Zone repair only picks `is_repairable()` parts, which are never broken, so `frac` is always 0, `moves_left` is 0, and `repair_part()` restores the part to full immediately. The manual UI charges `repair_time * repairable_levels()`. VEHICLE_REPAIR zones repair a whole vehicle for free. (2) `can_do` and `vehicle_work_requirements` check single-level `repair_requirements()`, but `repair_part` needs `* repairable_levels()` and uses a different inventory origin/radius. With materials for one level, the repair fails, the multi-activity resumes from the backlog with the same cached source, and the loop repeats at ~1 move per pass with message spam. (3) Typo: `activity_vehicle_part_index = 1` instead of `-1` in the (nearly unreachable) `!veh` branch.

```cpp
const int frac = ( vp.damage() - vp.degradation() ) / ( vp.max_damage() - vp.degradation() );
time_to_take = vpi.repair_time( you ) * frac;
// veh_utils.cpp: vp.repair_requirements() * pt.get_base().repairable_levels();
if( !veh ) { you.activity_vehicle_part_index = 1; return std::nullopt; }
```

**Fix:** Use `repair_time( you ) * part.get_base().repairable_levels()` as `veh_interact` does. Multiply the checked/fetched requirements by `repairable_levels()` and use the same inventory radius as `repair_part`. Set the index to `-1`. On `repair_part` failure, mark the part skipped so the multi-activity stops retrying it.

### ACT-B03 · Zone sorting carries off the sorter's own matching charge stacks (ammo, thread, pills)
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/activity_actor.cpp:15086`, `src/activity_actor.cpp:14705`, `src/activity_actor.cpp:14711`, `src/activity_actor.cpp:14722`, `src/activity_actor.cpp:14623`, `src/activity_actor.cpp:14637`, `src/character_inventory.cpp:339`, `src/item_pocket.cpp:400`, `src/item_pocket.cpp:2188`

`stage_do` picks up with `you.try_add( copy )`. `best_pocket` prefers a pocket that already holds a stack the item merges with, and `item_pocket::add` returns the merged stack. That location is stored in `picked_up_stuff`; at drop-off the code copies the whole stack to the destination and `i_rem`s the whole stack. A player carrying 50 loose 9mm who sorts a pile with 5 more puts all 55 in the ammo zone. `return_items_to_source` does the same, and cart pickup likely too (`vehicle::add_item` merges charges).

```cpp
thisitem_loc = you.try_add( copy_thisitem );        // 15086 -> merged stack
item copy( **iter );                                // 14705/14711 whole stack
iter->carrier()->i_rem( iter->get_item() );         // 14722
```

**Fix:** Record the picked quantity per entry (e.g. `pair<item_location, int charges>`) and split only that many charges at drop-off, or pick up through a non-merging path (temporary marker var). Apply the same to `return_items_to_source` and cart pickup.

### ACT-B04 · Terrain NO_NPC_PICKUP zones are ignored when NPC multi-activities gather or fetch materials
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/clzones.cpp:1142`, `src/clzones.cpp:1143`, `src/activity_actor.cpp:5673`, `src/activity_item_handling.cpp:3100`, `src/npcmove.cpp:4388`

The only NO_NPC_PICKUP filter on the multi-activity path is in `get_point_set_loot(..., npc_search=true, ...)`, and it iterates `vzone_cache` (vehicle zones) only, never `area_cache`, and ignores faction. `check_requirements`, `requirements_map`, `fetch_activity` and zone sort have no other check, while normal NPC AI checks the zone on every pickup. A follower told to construct, farm or chop will fetch planks, seeds or tools from a protected stash.

```cpp
if( npc_search ) {
    for( const auto &cache : vzone_cache ) {      // vehicle zones only
        if( type == zone_type_NO_NPC_PICKUP ) { ... res.erase(...) }
```

**Fix:** When `npc_search` is set, also filter by `has( zone_type_NO_NPC_PICKUP, p, fac )` over terrain zones with the faction matched. Skip NO_NPC_PICKUP tiles in zone_sort `stage_init`/`stage_think` when `!you.is_avatar()`.

### ACT-B05 · `revert_vzones()` erases from `added_vzones` while range-iterating it; personal-zone counter never reset
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/clzones.cpp:2040`, `src/clzones.cpp:1662`, `src/clzones.cpp:1820`, `src/clzones.cpp:105`, `src/zone_manager_ui.cpp:867`, `src/zone_manager_ui.cpp:485`

Quitting the zone manager without saving calls `revert_vzones()`, which loops over `added_vzones` while `remove()` erases from it. With 3+ vehicle zones added and discarded, one survives; with 2 it reads past the end (UB). Separately, `deserialize()` increments `num_personal_zones` per personal zone and nothing resets it (not `clear()`, not before reading), so it inflates on each discard-reload or world load. That only costs time: `has_personal_zones()` stays true and NPC sorting runs the O(zones) `has_nonpersonal()` scan per tile per item.

```cpp
for( zone_data *zone : added_vzones ) {
    remove( *zone );            // remove(): added_vzones.erase( it );
}
```

**Fix:** Iterate a moved-out copy (`auto added = std::move( added_vzones ); for( zone_data *z : added ) remove( *z );`). Recompute `num_personal_zones` with `count_if` after deserialize and reset it in `clear()`.

### ACT-B06 · `zone_manager` snapshots zone types once per process; mod zone types break when switching worlds
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/clzones.cpp:98`, `src/clzones.h:601`, `src/clzones.cpp:105`, `src/clzones.cpp:1824`, `src/clzones.cpp:1997`, `src/clzones.cpp:712`, `src/do_turn.cpp:221`

The singleton copies `zone_type::get_all()` into `types` only in its constructor, and `clear()` deliberately keeps it, while the zone_type factory is reloaded per world. Load world A, return to menu, load world B whose mods add a `LOOT_ZONE`: `has_type()` fails, `deserialize()` erases every zone of that type with "Invalid zone type" (lost on the next save), and `query_type()` doesn't offer the type. A's mod types linger in B's menu. Only third-party mods are affected today.

```cpp
zone_manager::zone_manager() { for( const zone_type &zone : zone_type::get_all() ) { types.emplace( zone.id, zone ); } }
void zone_manager::clear() { ... // Do not clear types since it is needed for the next games.
```

**Fix:** Drop the `types` copy and query the factory directly (`zone_type_id::is_valid` / `get_all`), or rebuild `types` in `clear()` and after `zone_type::finalize_all`.

### ACT-B07 · Fetch counts vehicle-cargo items as available but can never fetch them, so tasks abort
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/activity_item_handling.cpp:1983`, `src/activity_item_handling.cpp:3121`, `src/activity_item_handling.cpp:3128`, `src/activity_item_handling.cpp:3326`, `src/activity_item_handling.cpp:3392`, `src/activity_item_handling.cpp:3406`, `src/activity_actor.cpp:5700`, `src/activity_actor.cpp:5475`, `src/activity_actor.cpp:5811`

For non-tool-pickup tasks (construction, disassembly), `fill_requirement_inventory` counts vehicle cargo on loot tiles, so the requirement check passes and a fetch is dispatched. `requirements_map` scans only `here.i_at()` ground stacks, so the source set is empty, `last_plan = cannot_supply`, and the task is called off with "can't bring what the task still needs". The vehicle branch in `fetch_activity` only matches entries from the ground-only map and is limited to `ACT_MULTIPLE_CONSTRUCTION`. Trigger: planks in a cart or trunk tagged as a loot zone beyond pickup range of a blueprint.

```cpp
// fill_requirement_inventory
if( !in_loot_zones ) { if( ovp = here.veh_at( elem ).cargo() ) { for( item &it : ovp->items() ) temp_inv.add_item_loc(...) } }
// requirements_map
for( item &stack_elem : here.i_at( point_elem ) ) { ... }   // ground only
```

**Fix:** Make `requirements_map` and `fetch_activity` iterate vehicle cargo (`vehicle_cursor`) for every activity type and move items through the same `move_item` path, or exclude cargo from `fill_requirement_inventory` so the two agree.

### ACT-B08 · Item claim tags (`activity_var`) set inside `can_do` predicates are never cleaned up
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/activity_item_handling.cpp:2383`, `src/activity_item_handling.cpp:2623`, `src/activity_item_handling.cpp:4011`, `src/activity_item_handling.cpp:996`, `src/activity_item_handling.cpp:186`, `src/activity_actor.cpp:5513`, `src/activity_actor.cpp:5553`, `src/activity_item_handling.cpp:4716`

`multi_study` and `multi_disassemble` `multi_activity_can_do` stamp `activity_var = you.name` on the book/item while being evaluated. `simulate_turn` calls them for every candidate tile, then `continue`s on route failure or picks another tile. Only `multi_study`'s `do` registers the item in the cleanup list (disassemble never does), so stamps persist in the save. `is_valid_study_book` then rejects the book for other characters, and `sort_skip_item` stops sorting the item. Mitigations: the same character still accepts its own stamp, disassemble clears own-name stamps, and manual pickup clears it.

```cpp
if( book ) { book->set_var( "activity_var", you.name ); return ok(...); }   // can_do, :2383
i.set_var( "activity_var", you.name );                                      // :2623
if( it->has_var( "activity_var" ) ) return true;                            // sort_skip_item
```

**Fix:** Don't mutate items in `can_do`; return the chosen item in `activity_reason_info` and tag it in `multi_activity_do`, registering it immediately. Better: a transient claim table keyed by item uid and `character_id`. Add a load-time sweep for stale stamps.

### ACT-B09 · Stamina prompt "Continue after a break" runs the actor's `canceled()` hook but keeps the activity
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/player_activity.cpp:320`, `src/player_activity.cpp:334`, `src/player_activity.cpp:336`, `src/player_activity.cpp:340`, `src/player_activity.cpp:392`, `src/activity_actor.cpp:7129`, `src/crafting.cpp:5014`

Option 1 falls through to `default: canceled( you );`, which runs `actor->canceled()`, clears `activity_var` claims and posts `character_finished_activity(canceled=true)`, then pushes the activity behind `wait_stamina` to auto-resume. Real triggers are pulping (spurious cancel event and claim cleanup) and mod crafts with stamina `character_resources`, where a no-component practice recipe loses its in-progress craft. EOCs see a cancel that never happened.

```cpp
default:
    canceled( you );
    break;
}
if( !ignoreQuery && auto_resume ) { you.assign_activity( wait_stamina_activity_actor( you.get_stamina_max() ) );
```

**Fix:** For option 1 do nothing extra (auto_resume already queues the wait). Call `canceled()` only for the real cancel options; add a separate "paused" event if needed.

### ACT-B10 · Activity `ignored_distractions` aren't saved, and resume wipes even the JSON defaults
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/player_activity.h:38`, `src/savegame_json.cpp:412`, `src/player_activity.cpp:484`, `src/character.cpp:5426`, `src/character.cpp:5454`, `src/activity_type.cpp:103`, `src/activity_actor.cpp:2792`

`ignored_distractions` is `NOLINT(cata-serialize)` and `deserialize()` never reseeds it from `type->default_ignored_distractions()`. `allow_distractions()`, called on cancel and backlog resume, clears it entirely. After load or resume, an activity loses its JSON `ignored_distractions`, the player's "ignore" choices and spellcasting's list. Small in practice: core only uses the field on `ACT_AIM`, `ACT_CAMP_MEAL` and `ACT_CONSUME`.

```cpp
std::set<distraction_type> ignored_distractions; // NOLINT(cata-serialize)
void player_activity::allow_distractions() { ignored_distractions.clear(); }
```

**Fix:** Serialize the set as enum strings; make `allow_distractions()` reset to the type defaults; seed from defaults in `deserialize` when absent.

### ACT-B11 · `WEARY_RECOVERY_MULT` > 1 (Isolation-Protocol uses 2) gives NaN and a UB float-to-int cast
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/activity_tracker.cpp:57`, `src/activity_tracker.cpp:24`, `data/mods/Isolation-Protocol/game_balance.json:16`, `data/core/external_options.json:263`

`try_reduce_weariness` computes `pow( 1 - recovery_mult, 1/12 )`; a negative base gives NaN, and `static_cast<int>( intake * NaN )` is UB (INT_MIN on x86, then clamped to 0). The in-repo Isolation-Protocol mod sets 2, so intake is zeroed every tick. Gameplay impact is minor because the same multiplier makes weariness recover very fast anyway; the issues are the UB and the unvalidated option.

```cpp
intake = static_cast<int>( intake * std::pow( 1 - recovery_mult, ( 1.0f / 12.0f ) ) );
```

**Fix:** `std::pow( std::max( 0.0f, 1.0f - recovery_mult ), 1.0f / 12.0f )`, or define the option as a [0,1] fraction and validate it on load. Check the mod's intent.

### ACT-B12 · `zone_manager::remove()` never refreshes `area_cache`; `distribute_food_auto` iterates invalidated refs
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/clzones.cpp:1634`, `src/clzones.cpp:1646`, `src/npctalk_funcs.cpp:1376`, `src/npctalk_funcs.cpp:1380`, `src/clzones.cpp:1745`

Removing a terrain zone erases it without `cache_data()`, so `has`, `has_near`, `get_point_set` and `get_point_set_loot` keep seeing it until something else rebuilds the cache (the zone UI hides this by rebuilding on exit). `talk_function::distribute_food_auto` (Smokes dialogue) adds temporary CAMP_FOOD/CAMP_STORAGE zones, removes them, and leaves phantom tiles that `faction_camp.cpp:5226` reads. It also removes inside a loop over `reference_wrapper`s into the vector it erases from (UB, benign in practice) and removes all zones of those types for the faction.

```cpp
if( &zone == &*it ) { ... zones.erase( it ); return true; }   // no cache_data()
for( zone_data &a_zone : p_zones ) { if( a_zone.get_type() == type_to_remove ) { mgr.remove( a_zone ); } }
```

**Fix:** Call `cache_data()` (or mark the cache dirty) in `remove()` for terrain zones. In `distribute_food_auto`, remember the two created zones and remove only those, iterating a snapshot or via a `remove_if`-style API.

### ACT-B13 · Unload-zone "max stack size" prompt validates the old value instead of the input
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/clzones.cpp:315`, `src/clzones.cpp:318`, `src/clzones.cpp:321`, `src/activity_item_handling.cpp:1369`

`query_unload()` checks `sparse_threshold < 1` (the stored value) instead of the entered `threshold`. Entering 0 or a negative number (`query_int` accepts `-`) is stored as-is, and the zone then never unloads anything. The prompt always pre-fills 20 instead of the current value.

```cpp
int threshold = 20;
if( query_int( threshold, true, _( "What is the maximum stack size to unload?" ) ) ) {
    if( sparse_threshold < 1 ) { sparse_threshold = 1; } else { sparse_threshold = threshold; }
```

**Fix:** `int threshold = sparse_threshold; ... sparse_threshold = std::max( 1, threshold );`

### ACT-B14 · `wait_stamina` overwrites `initial_stamina` every turn; threshold not serialized
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/activity_actor.cpp:14102`, `src/activity_actor.cpp:14104`, `src/activity_actor.cpp:14118`, `src/activity_actor.cpp:14129`, `src/activity_actor_definitions.h:4227`, `src/player_activity.cpp:340`

Despite the comment, `do_turn` assigns `initial_stamina` on every call, so the "not regaining stamina" sanity check compares against last turn. That check also prints `act.values.front()` on an empty vector (unreachable UB in practice). `deserialize()` default-constructs the actor, dropping `stamina_threshold`. Current impact is near zero (the only threshold used is max stamina), but if max stamina drops below the stored threshold the wait never ends on its own and the broken check can't catch it.

```cpp
if( stamina_threshold != -1 ) {
    // Record intial stamina only when waiting until a given threshold.
    initial_stamina = you.get_stamina();
}
return wait_stamina_activity_actor().clone();   // deserialize
```

**Fix:** Guard with `if( initial_stamina == -1 )`, print `stamina_threshold`, serialize both fields, and cap the threshold at current max stamina.

### ACT-B15 · Multi-farm emits the "too cold to plant" message from a `can_do` predicate, once per tile
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/activity_item_handling.cpp:2457`, `src/activity_item_handling.cpp:2518`, `src/activity_item_handling.cpp:2520`, `src/activity_actor.cpp:5513`, `src/messages.cpp:142`

`multi_farm_activity_actor::multi_activity_can_do` is evaluated for every FARM_PLOT tile per scan and calls `add_msg_if_player` whenever `warm_enough_to_plant` fails. Message coalescing usually collapses the output to one line ending "x 100", so this is a cosmetic quirk; the cost of the computation itself is ACT-P02.

```cpp
if( !can_plant.success() ) { you.add_msg_if_player( can_plant.c_str() ); }
return activity_reason_info::fail( do_activity_reason::ALREADY_DONE );
```

**Fix:** Return a dedicated reason (e.g. `TOO_COLD`) and emit the message once in `activity_failure_message`, or dedupe per scan like `missing_items_reported`.

## 3. Performance

### ACT-P01 · Zone point-set getters return whole `unordered_set`s by value; `has_near`/`get_near` copy every tile, several times per item
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/clzones.h:612`, `src/clzones.cpp:1097`, `src/clzones.cpp:1156`, `src/clzones.cpp:1188`, `src/clzones.cpp:1332`, `src/clzones.cpp:1367`, `src/clzones.cpp:1405`, `src/activity_actor.cpp:14951`, `src/activity_actor.cpp:15345`, `src/activity_actor.cpp:14697`, `src/activity_item_handling.cpp:1024`, `src/activity_item_handling.cpp:1224`

`get_point_set()` and `get_vzone_set()` return by value; `has_near`, `get_near` and `get_nearest` bind the result as `const auto &`, which just extends the life of a fresh copy of every world tile of that type and faction, even when they exit early. `get_near_zone_type_for_item()` makes roughly 3-12 such calls per item, and zone sorting calls it per item at least twice in THINK and twice in DO, plus once per item per dropoff coordinate (14951, 15345) and per dropped item (14697). Range-0 queries still copy and scan the whole set. With ~2,000 category-zone tiles and 1,000 items, that is on the order of 10-30 million node allocations per sort run. Other callers: `unload_item`, NPC retreat (`npcmove.cpp:480`), `npctrade_utils.cpp:153`.

```cpp
std::unordered_set<tripoint_abs_ms> get_point_set( const zone_type_id &type, const faction_id &fac = your_fac ) const;   // clzones.h:612
return type_iter->second;                                    // clzones.cpp:1105 (copy)
const auto &point_set = get_point_set( type, fac );          // clzones.cpp:1191
```

**Fix:** Return `const std::unordered_set<tripoint_abs_ms> &` (with a function-local static empty set for misses). Answer range-0 `has_near`/`get_near` with `has( type, where )`. Optionally keep a per-key tile count and bounding box so `has_near` can reject without iterating.

### ACT-P02 · Farm scan runs `warm_enough_to_plant` (~500 weather-noise evaluations) on every farm tile
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/activity_item_handling.cpp:2456`, `src/activity_item_handling.cpp:2500`, `src/activity_item_handling.cpp:2519`, `src/weather.cpp:959`, `src/weather.cpp:893`, `src/activity_actor.cpp:5513`, `src/activity_actor.cpp:5440`

`can_plant` is computed at the top of `multi_activity_can_do` for every seeded farm tile, before the overgrown/harvest/fertilize/till branches that don't use it. `warm_enough_to_plant` copies the `weather_generator` (with its string vectors) and calls `highest_temp_on_day` once for now and per growth stage, each sampling `get_weather()` 97 times: about 580 calls per tile for vanilla seeds. Tiles that are ALREADY_DONE charge no moves, so the whole plot is evaluated in one turn, and the source cache resets every time the worker moves. A 10x10 plot is ~50,000 weather evaluations per scan. `get_weather` also calls `rng()` and can change the static `current_winddir`, so the scan disturbs RNG and weather state.

```cpp
ret_val<void>can_plant = !seed.is_empty() ?
                         warm_enough_to_plant( src_loc, seed ) : ret_val<void>::make_success();   // :2456
const weather_generator weather_gen = get_weather().get_cur_weather_gen();                      // weather.cpp:977
```

**Fix:** Memoize per scan keyed by `(tripoint_abs_omt, seed)`; the temperature part depends only on the OMT and seed, so do the per-tile sunlight/greenhouse checks separately. Compute lazily in the planting branch. Take the generator by const reference.

### ACT-P03 · Multi-activity requirement check rebuilds a full loot-zone inventory for every candidate tile
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/activity_actor.cpp:5511`, `src/activity_actor.cpp:5671`, `src/activity_actor.cpp:5532`, `src/activity_item_handling.cpp:1910`, `src/activity_item_handling.cpp:2775`, `src/activity_item_handling.cpp:3055`, `src/activity_item_handling.cpp:3388`, `src/activity_actor.cpp:5812`

For every candidate tile with a "continue" reason (NEEDS_MINING, NEEDS_TILLING, NO_COMPONENTS, ...), `simulate_turn` runs `check_requirements`, which invalidates the crafting inventory, runs `get_point_set_loot`, builds a 13x13x13 PICKUP_RANGE cube, rebuilds the crafting inventory in `remove_met_requirements` (copying every carried item), runs `fill_requirement_inventory` (inv_dump, a string-keyed `mgr.has(LOOT_IGNORE)` per loot tile, a 1,331-tile welder scan) and finally `can_make_with_inventory`. Each skip costs 1 move, so ~100 tiles are re-evaluated per turn, although the loot-zone part doesn't depend on the tile. The fetch path runs `requirements_map` twice per step (5812 and 3388), each with two inventory passes, a linear `std::find` over `already_there_spots`, and a temporary `item` per entry just to call `has_quality`.

```cpp
you.invalidate_crafting_inventory();                                                   // :5511
for( const tripoint_bub_ms &elem : mgr.get_point_set_loot( you.pos_abs(), MAX_VIEW_DISTANCE, ... ) )   // :5673
remove_met_requirements( *activity_requirements, you );                                // :5695
```

**Fix:** Memoize `check_requirements` per scan by `(requirement_id, reason)` and build the loot-zone `temp_crafting_inventory` once per scan. Pass the `requirements_map` result from `multi_activity_locations` into `fetch_activity`. Use an `unordered_set` for `already_there_spots` and itype quality data instead of temp items. (Dropping the per-tile invalidate alone won't help: the crafting cache also keys on moves.)

### ACT-P04 · Construction `can_do` runs `route_adjacent` (up to 8 A*) per blueprint tile before cheap checks
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/activity_item_handling.cpp:2409`, `src/activity_item_handling.cpp:2429`, `src/activity_item_handling.cpp:1666`, `src/activity_item_handling.cpp:1857`, `src/activity_item_handling.cpp:4274`, `src/activity_item_handling.cpp:3786`, `src/activity_item_handling.cpp:3753`

`route_adjacent()` runs one `map::route` per passable neighbour until one succeeds, and it is called for every non-adjacent tile before the `zones.empty()` and `already_done` checks. Finished blueprint tiles return ALREADY_DONE (no move charged) and blueprint zones persist, so every finished wall is re-routed on each rescan. The chosen tile is routed again in `route()` (4274). `multi_activity_locations` also probes all 14,641 tiles in view with `partial_con_at` (adding candidates that are routed and then rejected with NO_ZONE), and runs the dark/dangerous prune passes twice.

```cpp
} else { std::vector<tripoint_bub_ms> const route = route_adjacent( you, src_loc ); ... }
if( !zones.empty() ) {                                                             // :2426-2435
```

**Fix:** Check zones, `already_done` and the partial construction first; route only when `nearest_src_loc` is needed. Implement `route_adjacent` as a single A* with `pathfinding_target::adjacent(dest)` as zone sorting does, cache the route for `route()`, iterate `submap->partial_constructions` instead of probing every tile, and drop the duplicate prune.

### ACT-P05 · Dark-tile pruning recomputes `active_light()` per tile; multi-read scans 14,641 tiles
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/activity_item_handling.cpp:3708`, `src/activity_item_handling.cpp:3716`, `src/activity_item_handling.cpp:3808`, `src/activity_item_handling.cpp:2364`, `src/character.cpp:2657`, `src/character.cpp:3820`, `src/activity_actor.cpp:5532`, `src/npcmove.cpp:2074`

`prune_dark_locations` calls `fine_detail_vision_mod(tile)` per tile, which recomputes `active_light()` (emissive items, functioning mutations by value, enchantment lookups) although only `ambient_light_at` depends on the tile. `multi_read` inserts the whole 121x121 bubble, prunes it this way, then runs a location-independent `can_do` per tile (`items_with` over the inventory, plus `get_zone_at` and `get_vehicle_zones`). With nothing to read, ALREADY_DONE charges no moves, so all 14,641 tiles are processed in one turn; NPCs retry every 10 minutes.

```cpp
if( you.fine_detail_vision_mod( set_pt ) > LIGHT_AMBIENT_DIM ) {           // :3716
for( const tripoint_bub_ms &elem : here.points_in_radius( localpos, MAX_VIEW_DISTANCE ) ) { src_set.insert( ... ); }   // :3807
```

**Fix:** Compute own light once per prune pass (skip the loop if nothing can be pruned) and compare only `ambient_light_at` per tile. For multi-read, evaluate `can_do` once and return only the current or nearest lit tile.

### ACT-P06 · Sort DO checks capacity of every destination tile per item, summing every item's volume
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/activity_actor.cpp:14864`, `src/activity_actor.cpp:14949`, `src/activity_item_handling.cpp:1147`, `src/item_stack.cpp:99`, `src/map.cpp:5922`, `src/activity_actor.cpp:15411`

For every item that passes `sort_skip_item`, including ones skipped later for not sharing the current destination, `stage_do` filters the whole `dest_set` through `dest_has_capacity()`: two string-keyed lookups, `i_at().size()` and `map::free_volume`, which sums uncached `item::volume()` over every item on the tile. `sample.volume()` is recomputed per tile. A 30-tile pantry with 200 items per tile means ~6,000 `volume()` calls per sorted item.

```cpp
for( auto dit = dest_set.begin(); dit != dest_set.end(); ) {
    if( zt_id != zone_type_id::NULL_ID() && !zone_sorting::dest_has_capacity( *dit, zt_id, thisitem, fac_id ) ) {   // :14864
return ... here.free_volume( dest_bub ) >= sample.volume();                                                         // :1165
```

**Fix:** Cache free volume and item count per destination tile for the `stage_do` call and update on drop; check capacity lazily on the few chosen destinations; hoist `sample.volume()`; do the shares-dest skip before the capacity filter.

### ACT-P07 · An item's destination zone type is computed twice; item-independent `has_near` checks repeat per item
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/activity_item_handling.cpp:1024`, `src/activity_item_handling.cpp:1224`, `src/activity_item_handling.cpp:1248`, `src/activity_actor.cpp:14844`, `src/activity_actor.cpp:14849`, `src/activity_item_handling.cpp:1344`

`sort_skip_item()` calls `get_near_zone_type_for_item()` internally, and both callers compute the same value at the same position next to it (`has_items_to_sort` 1224/1248, `stage_do` 14844/14849), doubling the most expensive per-item query (see ACT-P01). `unload_item()` runs `has_near(UNLOAD_ALL)` and `has_near(STRIP_CORPSES)` per item though neither depends on the item.

```cpp
if( zone_sorting::sort_skip_item( you, it->first, ... ) ) { continue; }
const zone_type_id zt_id = mgr.get_near_zone_type_for_item( thisitem, abspos, ...   // :14844-14850
```

**Fix:** Compute `zt_id` once and pass it into `sort_skip_item`; compute the UNLOAD_ALL/STRIP_CORPSES flags once per `stage_do` and pass them to `unload_item`.

### ACT-P08 · Zone sort runs an A* probe to every destination tile before the first pickup; route cache thrashes
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/activity_actor.cpp:14969`, `src/activity_actor.cpp:14984`, `src/activity_actor.cpp:15117`, `src/activity_item_handling.cpp:848`, `src/activity_item_handling.cpp:879`, `src/activity_item_handling.cpp:1489`

For the first item of each batch, `stage_do` calls `zone_sorting::route_length()` for every non-adjacent destination tile with no cutoff and no move charge (unlike `stage_think`). The route cache holds 200 entries with FIFO eviction and is cleared whenever the sorter's position changes, which happens every batch. A 20x10 LOOT_FOOD zone means ~200 A* searches per batch. Each search is bounded (padded box, straight-line shortcut), so the cost is per batch, not per turn.

```cpp
for( const auto &[cheb, possible_dest] : dest_candidates ) { ...
    const int dist = zone_sorting::route_length( you, dest_bub ); if( dist == INT_MAX ) { continue; } ... }   // :14984-14996
static constexpr size_t max_entries = 200;                                                                  // :848
```

**Fix:** One Dijkstra/flood fill from the sorter to all candidates, or stop after the first K (e.g. 3) reachable destinations in Chebyshev order; charge moves per probe as `stage_think` does.

### ACT-P09 · Filtered `get_near` does an all-zones scan plus two vector allocations per tile; per-tile lookups return copies
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/clzones.cpp:1332`, `src/clzones.cpp:1262`, `src/clzones.cpp:1281`, `src/clzones.cpp:1506`, `src/clzones.cpp:1546`, `src/map.cpp:1555`, `src/activity_item_handling.cpp:2417`, `src/activity_item_handling.cpp:2452`, `src/cata_tiles.cpp:4559`

`get_near()` for `LOOT_CUSTOM`/`LOOT_ITEM_GROUP` calls `custom_loot_has()` per zone tile in range; each call runs `get_zones_at()` (scan of every zone, result vector) and `map::get_vehicle_zones()` (another vector). The memo only skips re-running the filter, so it is O(tiles x zones) per item whenever a custom zone is near. `get_bottom_zone()` (per visible tile per redraw while the zone UI is open) has the same pattern, and `get_zones(type, where)` returns `std::vector<zone_data>` by value from farm/construction `can_do`, `get_first_fertilizer_itype` and farm `do`.

```cpp
std::vector<zone_data const *> const zones = get_zones_at( where, ztype, fac );     // :1286
auto vzones = here.get_vehicle_zones( here.get_abs_sub().z() );                    // :1266-1272
```

**Fix:** Iterate the zones of the requested type and faction once, evaluate the filter per zone, and add the in-range rectangle directly. Fetch vehicle zones once per query. Return `std::vector<const zone_data *>` from `get_zones(type, where)`.

### ACT-P10 · Zone cache fully rebuilt on each add/move/rotate; vehicle-zone cache rebuilt every turn
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/clzones.cpp:1019`, `src/clzones.cpp:1617`, `src/clzones.cpp:1718`, `src/clzones.cpp:792`, `src/clzones.cpp:1724`, `src/clzones.cpp:1074`, `src/map.cpp:1048`, `src/map.cpp:1555`, `src/activity_actor.cpp:14271`

`cache_data()` reinserts every tile of every enabled zone in the world. It runs from every `add()` (each mapgen-placed zone), every `set_position`, and for each zone in `rotate_zones` (`_rotate_zone` passes `skip_cache_update=false`). `map::vehmove()` calls `cache_vzones()` every turn even when nothing moved, and `update_vehicle_zone_cache` can trigger a nested rebuild. All cold or cheap paths in practice.

```cpp
zones.push_back( new_zone ); cache_data();                                              // :1617-1619
zone.set_position( std::make_pair( first, second ), false, true, false, true );        // :1718
zone_manager::get_manager().cache_vzones( this );                                       // map.cpp:1048
```

**Fix:** Insert only the new zone's tiles in `add()`; rebuild once at the end of `rotate_zones`; call `cache_vzones` only when a zone vehicle moved or `zones_dirty` is set.

### ACT-P11 · `try_fuel_fire` runs every turn of 13 long activities and scans before checking whether fuel is needed
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/player_activity.cpp:213`, `src/activity_item_handling.cpp:4633`, `src/activity_item_handling.cpp:4356`, `src/activity_item_handling.cpp:4392`, `src/activity_item_handling.cpp:4677`

For activities with `refuel_fires` (read, craft, wait, disassemble, build, repair, ...), each turn with a fire and fuel source nearby builds a 168-point `closest_points_first` list, calls `get_field` and `clear_path`, runs `find_refuel_spot_zone` (a set copy, sort, `clear_path` per tile) and possibly a trap scan, and only then returns early if the fire already has enough fuel. Tens of microseconds per turn.

```cpp
std::vector<tripoint_bub_ms> adjacent = closest_points_first( pos, 1, PICKUP_RANGE );   // :4636
if( !fire_target && ( fd.fuel_produced >= 1.0f || fire_age < 10_minutes ) ) { return true; }   // :4677
```

**Fix:** Check the fire's fuel state first and look up the refuel spot only when needed; cache the fire and spot across turns; throttle to `calendar::once_every( 1_minutes )`.

### ACT-P12 · Butchery scans copy an entire `mtype` (and the corpse item) per corpse
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/activity_item_handling.cpp:2304`, `src/activity_item_handling.cpp:2297`, `src/activity_item_handling.cpp:2310`, `src/activity_item_handling.cpp:3498`

`multi_butchery` `can_do` copies `*i.get_mtype()` (about 46 container/string members) to read `size`, and deep-copies each corpse into a `std::vector<item>` to read the species later; `butcher_corpse_activity` repeats the mtype copy. Runs once per butchery job rather than per turn, so the cost is small.

```cpp
const mtype corpse = *i.get_mtype();    // :2304
corpses.push_back( i );                 // :2310 (std::vector<item>)
```

**Fix:** `const mtype &corpse = *i.get_mtype();` and store `const mtype *`/`const item *`. Behaviour is identical.

### ACT-P13 · Item move/drop helpers deep-copy each item several times, including a throwaway in `drop_on_map`
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/activity_item_handling.cpp:572`, `src/activity_item_handling.cpp:573`, `src/activity_item_handling.cpp:798`, `src/activity_item_handling.cpp:815`, `src/activity_item_handling.cpp:817`, `src/activity_item_handling.cpp:327`, `src/item.cpp:1412`

`drop_on_map()` deep-copies each dropped item just to call `handle_pickup_ownership` on the temporary (which can scan for theft witnesses). `move_item()` always copies into `leftovers` even for whole-stack moves, then `{ it }` costs two more copies into the `std::list`, and `try_to_put_into_vehicle` copies again. Up to four extra deep copies per item moved by sort, fetch or refuel. The temporary is also passed to `npc::witness_thievery`, which stores the dangling pointer (only null-checked).

```cpp
// Create a new temporary, handle pickup ownership on it, immediately throw away the temporary??
item( it ).handle_pickup_ownership( you );                                      // :572-573
item leftovers = it;                                                             // :798
```

**Fix:** Call `handle_pickup_ownership` on the placed item via `dropped_loc` (don't just delete it: the witness side effect matters). Create `leftovers` only when splitting, and add `std::list<item> &&` overloads so callers can move items in. The copy at 327 is intentional (the item is mutated).

### ACT-P14 · Zone caches are keyed by heap-allocated strings; every `has()` concatenates one
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/clzones.h:496`, `src/clzones.h:609`, `src/clzones.cpp:1114`, `src/clzones.cpp:1168`, `src/npcmove.cpp:4387`, `src/game.cpp:5900`, `src/activity_item_handling.cpp:1957`, `src/activity_actor.cpp:14845`, `src/activity_actor.cpp:15333`

`area_cache` and `vzone_cache` are keyed by `type + "__FAC__" + faction`, typically longer than the SSO limit, so each lookup allocates, concatenates and hashes; `has()` does it twice. `get_point_set_loot` re-parses keys and re-interns ids per entry. Callers include `npc::find_item` (169 tiles per allied NPC, under a "TODO: Optimize that zone check"), `fill_requirement_inventory`, `dest_has_capacity`, and the loop-invariant `mgr.has(LOOT_IGNORE_FAVORITES, src)` at 14845 and 15333. Real but minor next to the costs in ACT-P01 and ACT-P06.

```cpp
static std::string make_type_hash( const zone_type_id &_type, const faction_id &_fac ) {
    return _type.c_str() + type_fac_hash_str + _fac.c_str(); }                 // clzones.h:496
```

**Fix:** Key both caches by a POD `{zone_type_id, faction_id}` with a combined int-id hash; precompute `is_loot` on `zone_type`; hoist the loop-invariant `has()` calls in `stage_do` and the batching loop.

## Appendix: considered and rejected

- Forage loot is one global seasonal table, not per-terrain – the global groups only serve `shrub_wildveggies` (one terrain); per-terrain seasonal foraging is already data-driven via `harvest_by_season` plus `harvest_ter`/`harvest_furn` examine actions, and `eoc_examine_actor` covers custom loot.
- `craft_activity_actor::start` dereferences a lost craft after the check fails (missing `return`) – the missing `return` is real, but all four callers validate the `item_location` first and `start()` isn't called on backlog resume, so no null dereference is reachable; worth a one-line hardening fix only.
- Multi-activity driver copies the whole activity and source set every call; cache invalidates on every step – not a hot path (`simulate_turn` runs a few times per dispatched task), the copy is deliberate, and regenerating on position change is required for correctness because candidate sets and dark pruning depend on the worker's position and current light.
