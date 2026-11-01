# 08 · Mapgen & map extras

> Engine audit – static code reading at commit `a1a646d6`; nothing was compiled or run. Each finding was checked against the code by a separate reviewer: **confirmed** = verified by reading the code; **plausible** = consistent with the code, but reachability or impact not proven. Line numbers refer to that commit.

**Scope:** `src/mapgen.cpp`, `src/mapgen_functions.cpp`, `src/mapgen_post_process.cpp`, `src/mapgendata.cpp`, `src/mapgenformat.cpp`, `src/map_extras.cpp`

## Summary

JSON mapgen is mature, but it is cut off from the EOC system. Nothing fires when an OMT or map extra is generated, mapgen values can't read variables or location context, map extras can't be gated or spawned by EOCs, and the EOC `mapgen_update` effect exposes only part of what the C++ API supports. Several documented JSON features silently do nothing: nested `rotation`, extra `update_mapgen` variants, `set` point ops with `repeat`, and spawn `patrol`. Parameter finalization has ordering gaps, and load-time `check()` has holes, so bad data can load without any warning. On the performance side, the worst offender is the terrain phase-change pass that runs on every submap load. There is also a cluster of cheap wins: work done even when `verify=false`, `std::list` random access, redundant full-level scans, and a duplicated forest pass on shores.

| Lens | Critical | High | Medium | Low | Total |
|---|---|---|---|---|---|
| Missing JSON / EOC | 0 | 0 | 8 | 3 | 11 |
| Bugs & quirks | 0 | 1 | 9 | 7 | 17 |
| Performance | 0 | 1 | 6 | 6 | 13 |
| **Total** | 0 | 2 | 23 | 16 | 41 |

**Fix first:**
1. **MGEN-B01** · `set` point ops with `repeat` always hit the same tile: high severity, about 116-119 content entries affected, and the fix is moving one line into the loop.
2. **MGEN-P01** · Phase-change pass on every submap load: the most expensive per-load cost in the system, and it also rewrites frozen terrain twice on every reload.
3. **MGEN-B07** · Start-location parameters can throw `bad_optional_access`: a single modder typo crashes new-game creation, and the fix is small.
4. **MGEN-P02** · Vehicle-collision checks run even with `verify=false`: gating them on `verify` saves work on every OMT and nested chunk, with zero behaviour change.
5. **MGEN-B02** · Duplicate `update_mapgen_id` definitions are silently dead: adding a load-time warning is cheap and exposes layouts that can never appear in existing content.

## 1. Missing JSON bindings & EOC features

### MGEN-J01 · No EOC hook or event_bus event when an OMT is generated or a map extra spawns
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/mapgen.cpp:170`, `src/mapgen.cpp:283`, `src/mapgen.cpp:3457`, `src/mapgen.cpp:7734`, `src/map_extras.cpp:1145`, `src/map_extras.cpp:1180`, `src/event.h:29`, `src/overmap_special.cpp:274`

None of these finish with an EOC or event_bus notification: `map::generate`, `mapgen_function_json::generate`, `MapExtras::apply_function` and the pp_generators. The only path from mapgen to EOCs is `place_variables`. It can only write an absolute location into a global variable: no value, no talker, and x/y ranges are ignored (MAPGEN.md:708 documents a range). The oter `entry_EOC`/`exit_EOC` and `avatar_enters_omt` fire when the avatar walks in, not when the map is created. Most OMTs are generated before the avatar enters them, and NPC travel can generate maps the avatar never visits. So mods can't register generated locations or react when a particular map extra spawns. Overmap specials do have an `"eoc"` that runs when the special is placed (`overmap_special.cpp:274-277`), but it gets no location context.

```cpp
// map_extras.cpp:1180 – the only side effect after an extra applies
overmap_buffer.add_extra( project_to<coords::omt>( abs_sub ), id );
// mapgen.cpp:3456 – jmapgen_variable ignores the x/y range
queued_points[name] = dat.m.get_abs( tripoint_bub_ms( int( x.val ), int( y.val ), ...
```

**Fix:** Add a `place_eocs` mapgen piece and a palette key `"eocs"`. Queue the entries the same way `queued_points` are queued, and run them where `set_queued_points` runs, with context vars `_loc`, `_omt` and `_oter_id`:
```json
"place_eocs": [ { "eoc": "EOC_REGISTER_SHRINE", "x": 12, "y": 12 } ]
```
Add `"eocs"` to `map_extra` and run them from `apply_function` on success. Add the event_bus events `omt_generated {pos, oter_id}` and `map_extra_spawned {pos, map_extra_id}`. Extend `place_variables` with a `"value"` (str_or_var / math) and make it honour x/y ranges.

### MGEN-J02 · EOC `mapgen_update` lacks the parameters, rotation, mirror, tile offset and result that faction camps get
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/npctalk.cpp:5073`, `src/npctalk.cpp:5094`, `src/npctalk.cpp:5115`, `src/mapgen.cpp:7708`, `src/mapgen_functions.h:51`, `src/timed_event.cpp:329`, `src/faction_camp.cpp:3863`, `src/mapgen.cpp:6259`

`run_mapgen_update_func` supports `mapgen_arguments`, horizontal/vertical mirroring, rotation and a vehicle-collision result, and `update_map()` accepts a tile offset. The EOC effect passes `{}` args, no rotation or mirror and a zero offset, and it discards the `ret_val`. `target_var` is projected to an OMT, so the update always lands at the OMT origin. `cancel_on_collision` defaults to true, so a vehicle in the way makes the update silently do nothing (`apply_mapgen_in_phases` returns early at 6259-6262), and the EOC has no way to find out. The delayed `timed_event` path also passes `{}`. Mitigations: the update id is a str_or_var, and parameter defaults are rolled through `get_args`.

```cpp
// npctalk.cpp:5115
run_mapgen_update_func( update_mapgen_id( mapgen_update_id.evaluate( d ) ), omt_pos, {},
                        d.actor( d.has_beta )->selected_mission() );   // result ignored
```

**Fix:** Extend the effect:
```json
{ "mapgen_update": "hut_5x5", "target_var": { "context_val": "loc" }, "offset_from_target": true,
  "parameters": { "banner": { "global_val": "faction_banner" } },
  "rotation": { "math": [ "rng(0,3)" ] }, "mirror_horizontal": false,
  "ignore_vehicle_collision": false, "success_eocs": [ ], "failure_eocs": [ ] }
```
Map the parameters to `mapgen_arguments`, pass the ms offset to `update_map`, and set `_failure_reason` from `ret_val::str()`. Store the new fields in `timed_event` so the delayed path keeps them.

### MGEN-J03 · Map extras cannot be gated by condition, spawned by EOC, or queried by EOC
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/map_extras.cpp:1300`, `src/regional_settings.h:388`, `src/regional_settings.cpp:854`, `src/mapgen.cpp:286`, `src/timed_event.cpp:315`, `src/condition.cpp:1800`

`map_extra::is_valid_for` checks only the z range and the region's feature-flag black/whitelists. Collection weights are plain ints, and `chance` is an unsigned `one_in`. So an extra can't depend on time since the cataclysm, global vars or game progress. No EOC effect places an extra; the C++ needs this too, as `timed_event.cpp:315` hardcodes `mx_dsa_alrp`. No condition or math function asks whether an OMT has an extra, even though `overmap_buffer.add_extra` records it. Partial mitigation: 68 of the 80 core extras use the `update_mapgen` generator, so an EOC `mapgen_update` with the same id can apply the content, but it won't register the extra on the overmap. Side note: `mapgen.cpp:296/301` calls `ex.values.pick()` twice, so the pick that gets applied is not the one that was null-checked.

```cpp
// regional_settings.h:391-392
unsigned int chance = 1;
weighted_int_list<map_extra_id> values;
```

**Fix:** Add `"condition"` to `map_extra` and evaluate it in `is_valid_for` with `_omt`/`_oter_id` context. Allow dbl_or_var weights in collections, for example `["mx_helicopter", {"math":["max(0, 50 - time_since('cataclysm','unit':'days'))"]}]`. Add the effect `{ "place_map_extra": "mx_portal_in", "target_var": {...}, "time_in_future": ... }` and the condition `{ "map_extra_at": {"context_val":"loc"}, "id": "mx_crater" }`. Use the picked value consistently in `mapgen.cpp:296-301`.

### MGEN-J04 · Built-in river/lake/ocean/subway/forest mapgen hardcodes terrain ids and tuning that regions cannot override
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/mapgen_functions.cpp:104`, `src/mapgen_functions.cpp:112`, `src/mapgen_functions.cpp:586`, `src/mapgen_functions.cpp:657`, `src/mapgen_functions.cpp:189`, `src/mapgen_functions.cpp:1569`, `src/mapgen_functions.cpp:1594`, `src/mapgen_functions.cpp:2063`, `src/regional_settings.h:253`, `src/mapgen_functions.cpp:2248`

The terrain is fixed in each of these builtins:
- River tiles always use `t_water_moving_dp`/`_sh`, and the banks use `grass_or_dirt()`/`clay_or_sand()` instead of the region's groundcover.
- Lake shores always use `t_water_dp`/`t_water_sh`.
- Ocean shores always use `t_swater_*` and `t_sand`.

`region_settings_river` has only noise and branching fields, so a desert, swamp, alien or frozen region gets temperate grass banks. `resolve_regional_terrain_and_furniture` only remaps REGION_PSEUDO terrain, so `t_grass`/`t_dirt` banks are never regionalised. The forest generator has nine constexpr tuning values with the comment "would do well to be json-ized". The subway is five hardcoded ASCII layouts plus a literal `"subway"` string comparison.

```cpp
// mapgen_functions.cpp:657
// The following constexpr terms would do well to be json-ized.
// mapgen_functions.cpp:189
if( dat.t_nesw[dir]->get_type_id().str() != "subway" && ...
```

**Fix:** Add terrain fields to the region settings:
```json
"river": { "deep_water": "t_water_moving_dp", "shallow_water": "t_water_moving_sh",
           "bank_groundcover": [["t_grass",1],["t_dirt",3]], "bank_detail": [["t_clay",15],["t_sand",1]] }
```
Do the same for lakes (`deep_water_ter`, `shallow_water_ter`) and oceans (also `surf_ter` and `beach_ter`), and move the forest constants into `region_settings_forest_mapgen`. A cheap first step is to switch the banks to the existing pseudo-terrains; `ravine_edge` already uses `t_region_groundcover_ravine`. Move the subway to JSON linear mapgen and handle SUBWAY_CONNECTION with place_nested `flags`.

### MGEN-J05 · Post-process generators: mod-specific aftershock_ruin, burn/blood results and city-road dispatch hardcoded
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/mapgen_post_process.cpp:41`, `src/mapgen_post_process.cpp:593`, `src/mapgen_post_process.cpp:699`, `src/mapgen_post_process.cpp:727`, `src/mapgen_post_process.cpp:142`, `src/mapgen_post_process.h:84`, `src/mapgen.cpp:353`, `src/mapgen.cpp:371`, `data/json/post_process_generators.json:24`

`pp_generator` is JSON-driven, but each sub-generator's output is hardcoded:
- `pre_burn` always writes `t_wall_burnt`, `t_floor_burnt` or `t_dirt`.
- Blood is always `fd_blood` and fire is always `fd_fire`.
- `aftershock_ruin` is a core copy of Aftershock mod content (`afs_ruins_dynamic`, `t_wall_prefab_metal`, `t_snow_metal_floor`).

City roads always run `riot_damage_road` through a hardcoded branch that ignores the oter's `post_process_generators`, and two legacy oter flags still dispatch generators. Timing is in raw integer days. `sub_generators` is cleared on redefinition, so copy-from can't extend or delete entries. Mitigations: a mod can redefine `riot_damage_road` with an empty list, and a generic `ter_furn_transform` sub-generator already exists.

```cpp
// mapgen.cpp:353-355
if( omt->has_flag( oter_flags::road ) && overmap_buffer.is_in_city( omt_point ) ) {
    // Roads use a dedicated generator; not yet migrated ...
    pp_generator_riot_damage_road.obj().execute( ... );
```

**Fix:** Add per-sub-generator fields: `"burnt_wall_ter"`, `"burnt_floor_ter"`, `"burnt_ground_ter"`, `"field"` (field_type_id), `"condition"` (with `_omt`), and `"scaling_start"`/`"scaling_end"` as time_duration (keep the `*_days` aliases). Move the city-road choice to region settings (`"city_road_post_process": ["riot_damage_road"]`). Replace `aftershock_ruin` with JSON built from `ter_furn_transform`, a new `set_oter` sub-generator and an `above_oter` condition. Support `extend`/`delete` on `sub_generators`.

### MGEN-J06 · Mapgen-spawned monsters and NPCs cannot get vars, effects or spawn EOCs; `patrol` does nothing
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/mapgen_primitives.h:42`, `src/mapgen.cpp:427`, `src/mapgen.cpp:442`, `src/mapgen.cpp:2033`, `src/map.cpp:10331`, `src/submap.h:43`, `src/monster.cpp:1595`, `src/savegame_json.cpp:5282`

`spawn_data` carries only `ammo` and `patrol`, and `place_npcs` reads only class, target, traits and unique_id. Nothing can tag a mapgen-placed monster or NPC with dialogue vars, give it effects, or run an EOC on it when it materializes, so there is no per-instance quest-boss tagging. Per-type workarounds exist: NPC template `death_eocs` and the mtype death_function EOC. Three more problems:
- `patrol` is broken. `monster::set_patrol_route` has no callers, and `spawn_monsters_submap` applies only ammo, so the documented `patrol` field does nothing.
- Patrol ranges are rolled once at load (`mapgen.cpp:435-437`).
- `spawn_data` is not saved with submap spawns (`savegame_json.cpp:5282-5293`), so it is lost if the submap is saved before the spawn materializes.

```cpp
// mapgen_primitives.h:42-47
struct spawn_data { std::map<itype_id, jmapgen_int> ammo; std::vector<point_rel_ms> patrol_points_rel_ms; ... };
```

**Fix:** Extend `spawn_data`, serialize it with the spawn point, and apply it on materialization (u_ = the new monster). Add the same `vars`/`eocs` to `place_npcs`. Store patrol points as jmapgen_int pairs, roll them at spawn time, and actually call `set_patrol_route`.
```json
"vars": { "boss_tag": "cult_leader" }, "effects": [ { "id": "docile", "duration": "1 h" } ], "eocs": [ "EOC_ON_MAPGEN_SPAWN" ]
```

### MGEN-J07 · JSON mapgen has no condition support and mapgen values cannot read EOC variables
**Severity:** Medium · **Effort:** Large · **Verified:** confirmed
**Where:** `src/mapgen.cpp:1648`, `src/mapgen.cpp:1043`, `src/mapgen.cpp:510`, `src/weighted_dbl_or_var_list.h:277`, `src/mapgen.cpp:3792`, `src/mapgen.cpp:3834`, `src/mapgen.cpp:1223`

World state reaches mapgen only through the oter `weight` and the place_nested chunk weights. Those are evaluated with an (avatar, null talker) dialogue and no context, so they can't reference the location being generated. `mapgen_value` accepts only `param`, `distribution` and `switch`. `jmapgen_int` (x/y/repeat/chance) is a plain int range. No piece takes a `"condition"`. A dbl_or_var chunk weight that evaluates to 0 already gives conditional placement on global vars (undocumented). The real gaps are location context and condition ergonomics.

```cpp
// mapgen.cpp:510
dialogue d( get_talker_for( get_avatar() ), std::make_unique<talker>() );
```

**Fix:**
1. Add an optional `"condition"` to `jmapgen_place`. Evaluate it in `jmapgen_objects::apply` with context `_omt`, `_loc`, `_oter_id` and `_z`, and use the same context for the weight dialogues.
2. Add a `mapgen_value` source `{ "var": { "global_val": "wall_style" }, "fallback": "t_wall" }`.
3. Make `chance`, `repeat` and `amount` accept dbl_or_var.

Document the zero-weight trick.
```json
{ "chunks": ["cult_shrine"], "x": 5, "y": 5, "condition": { "math": ["cult_active == 1"] } }
```

### MGEN-J08 · Twelve map extras hardcoded in C++ with content IDs and tuning; JSON mapgen lacks the placement predicates to replace them
**Severity:** Medium · **Effort:** Large · **Verified:** confirmed
**Where:** `src/map_extras.cpp:1113`, `src/map_extras.cpp:249`, `src/map_extras.cpp:710`, `src/map_extras.cpp:1017`, `src/map_extras.cpp:1066`, `src/map_extras.cpp:375`, `src/map_extras.cpp:585`, `src/mapgen.cpp:1223`, `data/json/overmap/map_extras.json:78`

`builtin_functions` maps 12 vanilla extras to C++: roadworks, helicopter, portal_in, grove, shrubbery, pond, casings, looters, corpses, reed, fungal_zone and sandy_beach. They hardcode about 80-90 content ids:
- a fixed dead-vegetation map
- the `f_purple_loosestrife` literal
- `"thug"`/`"bandit"` NPC templates
- a dice(1,8) helicopter crew
- a DIGGABLE scan

Mods can't retheme them; Innawood reuses them as-is. Grove and shrubbery are generic flag-driven clones (`:585`), so they are less of a problem. JSON can't replace the rest yet: `jmapgen_place` has no per-tile predicate such as "has terrain flag", "passable" or "near water".

```cpp
// map_extras.cpp:1017
m.place_npc( pos_->xy(), string_id<npc_template>( one_in( 2 ) ? "thug" : "bandit" ) );
```

**Fix:** Add a placement filter that rerolls x/y until it matches:
```json
"place_on": { "ter_flags": ["DIGGABLE"], "passable": true, "near_ter_flags": { "flags": ["LIQUID"], "radius": 1 }, "attempts": 20 }
```
Move the dead-vegetation terrain map to a `ter_furn_transform` (that already supports flags). Move the vegetation lists to region settings or palettes. Then port corpses, looters, fungal_zone, casings, grove, shrubbery, reed and sandy_beach to `update_mapgen`, and keep only pond and helicopter in C++.

### MGEN-J09 · Gas pump, liquid, vending and toilet pieces hardcode fuel whitelist, furniture ids and ransack tuning
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mapgen.cpp:2308`, `src/mapgen.cpp:2371`, `src/mapgen.cpp:6616`, `src/mapgen.cpp:6626`, `src/mapgen.cpp:6640`, `src/mapgen.cpp:6664`, `src/mapgen.cpp:6634`, `doc/JSON/MAPGEN.md:946`, `doc/JSON/MAPGEN.md:1074`

These convenience pieces have fixed content:
- **place_gaspumps:** `check()` rejects any fuel other than gasoline, diesel, jp8 and avgas, even though the pump terrain already comes from `item::fuel_pump_terrain()`. A default pump is a fixed diesel/gasoline mix.
- **place_liquids:** multiplies `amount` by 100 only for those four ids. This undocumented quirk is the most surprising part, because the unit of `amount` then depends on the item id.
- **Vending machines:** fixed furniture ids, a `one_in(days+4)` ransack curve and glass shard debris.
- **Toilets:** always `f_toilet` and water.

Mods can build equivalents from generic pieces.

```cpp
// mapgen.cpp:2371
if( migrated == itype_gasoline || migrated == itype_avgas || migrated == itype_diesel || migrated == itype_jp8 ) { newliquid.charges *= 100; }
```

**Fix:**
- **Gas pump check:** require `!item(fuel).fuel_pump_terrain().is_null()` instead of the whitelist, and add a `"default_fuel"` distribution.
- **Liquids:** accept `"volume": "50 L"`.
- **Vending:** add `"furniture"`, `"furniture_off"`, `"ransack_chance"` (dbl_or_var) and `"debris"` (item group).
- **Toilets:** add `"furniture"` and `"liquid"`.

### MGEN-J10 · Terrain phase-change system is undocumented, single-method and hardcoded
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mapgen.cpp:6472`, `src/mapgen.cpp:6478`, `src/mapgen.cpp:6485`, `src/mapdata.cpp:1387`, `src/mapdata.cpp:1418`, `src/map.cpp:9500`, `src/mapgen.cpp:383`

`phase_method`, `phase_targets` and `phase_temps` drive ice/thaw conversion in `map::temp_based_phase_change_at`, which runs on every submap load (`map.cpp:9500`). Only the literal `"water_freeze"` does anything, with a hardcoded 10-day average sampled at 08:00. Any other value is silently ignored, because `phase_method` is an unvalidated string. The loader appends to `phase_targets`/`phase_temps` without clearing them, so a copy-from that redefines both fails the size check. No current JSON does that. None of these fields are documented. See also MGEN-B16 and MGEN-P01.

```cpp
if( cur_ter.phase_method == "water_freeze" ) { ... } else { return; }   // mapgen.cpp:6472-6486
```

**Fix:** Make `phase_method` an enum read via `get_enum_value`, so unknown values are a load error. Add `"phase_sample_days": 10` and `"phase_sample_time": "8 h"`. Clear the arrays when the member is present. Document the fields in doc/JSON.

### MGEN-J11 · Palette "extending" is additive only; mods cannot remove or replace a key's placements
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/mapgen.cpp:4435`, `src/mapgen.cpp:4462`, `src/mapgen.cpp:4420`, `src/mapgen.cpp:4477`, `src/mapgen.cpp:4770`

`"extending": true` re-adds the original palette and appends new placings. Nothing can delete a key's item group, monster or nested mapping. For terrain and furniture, a later mapping already overwrites the earlier one (last wins), so the gap is in additive types: items, monsters, nested and so on. Deleting furniture isn't possible either. An overhaul mod has to copy the whole palette. Palettes also have no key mappings for variables, remove_npcs or faction_owner.

```cpp
these_placings.insert( these_placings.end(), constrained_placings.begin(), constrained_placings.end() );  // :4420
```

**Fix:** Tag placings with their piece type at load, so they can be filtered. Then support the following when extending, and add the palette keys `"variables"` and `"faction_owner"`:
```json
"delete": { "items": ["d"], "monster": ["M"] }, "replace": { "furniture": { "c": "f_new" } }
```

## 2. Bugs, oversights & quirks

### MGEN-B01 · "set" point ops with "repeat" always hit the same random tile
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mapgen.cpp:5908`, `src/mapgen.cpp:5909`, `src/mapgen.cpp:5912`, `doc/JSON/MAPGEN.md:513`, `doc/JSON/MAPGEN.md:521`

`jmapgen_setmap::apply` rolls `target_pos` once, before the repeat loop, and every point op uses it: terrain, furniture, trap, bash, burn, radiation and remove. The docs say a ranged point with repeat is "randomly repeated in the area". Line and square ops do re-roll on each repeat. About 116-119 content entries are affected. For example, gym rollmats (2-4 requested, 1 placed), bash damage in collapsed_tower/abandoned01 that hits one tile repeatedly, and the doc's own `tr_beartrap` example.

```cpp
const int trepeat = repeat.get();
const tripoint_bub_ms target_pos( x_get(), y_get(), dat.zlevel() + z_get() );
for( int i = 0; i < trepeat; i++ ) {
    switch( op ) { case JMAPGEN_SETMAP_TER: m.ter_set( target_pos, ...
```

**Fix:** Compute `target_pos` inside the loop.

### MGEN-B02 · Duplicate `update_mapgen_id` definitions are silently dead: only `funcs()[0]` ever runs
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mapgen.cpp:4961`, `src/mapgen.cpp:7718`, `src/mapgen.cpp:7730`, `src/mapgen.cpp:7779`, `src/mapgen.cpp:7817`, `src/mapgen.cpp:834`, `src/mapgen.cpp:849`, `src/map_extras.cpp:1394`, `src/recipe.cpp:739`, `data/json/mapgen/map_extras/prison_bus.json:4`, `data/json/mapgen/portal_storm.json:128`

`update_mapgen::add` pushes every definition with a given id into a vector. Every consumer uses only `funcs()[0]`: EOC `mapgen_update`, map extras, traps, construction markers and changed-ids previews. Nothing warns. So the 2nd and 3rd `mx_prison_bus` layouts and the 2nd `portal_dungeon` layout can never appear. A mod that redefines a non-blueprint `update_mapgen` to override it is silently ignored. (The two `MISSION_PYROMANIAC_BURN` copies are identical. Camp blueprints already debugmsg on duplicates at `recipe.cpp:739`.) Weighting is also inconsistent: a nested mapgen's own `weight` is a plain `get_int`, while oter mapgen weight is dbl_or_var, so `{"math": ...}` throws on a nested definition.

```cpp
return update_function->second.funcs()[0]->update_map( omt_pos, args, ... );   // :7718
int weight = jio.get_int( "weight", 1000 );                                     // :834 (nested)
```

**Fix:** Now: have `check_mapgen_definitions()` debugmsg when an `update_mapgen_id` has more than one definition. Then pick a semantics. Either (a) keep a weighted list (read `"weight"` as dbl_or_var, default 1000) and pick one per run, storing the pick in mapgen args for camps so the preview and the build agree, or (b) let later definitions replace earlier ones. Read nested `weight` through `get_dbl_or_var` as well.

### MGEN-B03 · Mapgen check() gaps let bad JSON load silently
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mapgen.cpp:3873`, `src/mapgen.cpp:3876`, `src/mapgen.cpp:3898`, `src/mapgen.cpp:684`, `src/mapgen.cpp:4913`, `src/mapgen.cpp:3201`, `src/mapgen.cpp:2397`, `src/mapgen.cpp:3496`, `src/mapgen.cpp:1104`, `src/mapgen.cpp:1203`, `src/mapgen.cpp:6178`, `doc/JSON/MAPGEN.md:1239`, `src/mapgen.cpp:3798`

Several separate holes:
- **(a)** `jmapgen_nested::check` iterates `entries` twice and never `else_entries`, a copy-paste error. Invalid constant ids are still caught indirectly, via `add_coords_from`, but only with a context-free message. Undefined or wrong-type params in `else_chunks` surface only at runtime as "No such parameter".
- **(b)** `check_common` returns from the whole check on the first PLANT furniture in `set`. That skips `objects.check()` and the nested-overlap checks.
- **(c)** Several fields are never validated: `jmapgen_computer` `eocs`/`chat_topics`, `jmapgen_corpse` `group`, the `faction` strings on item and vehicle pieces, and `remove_npcs` `class`.
- **(d)** `"line"`/`"square"` combined with `bash`/`burn`/`variable` produce op values 110-112 and 210-212, which have no enum case, so `apply()` silently does nothing.
- **(e)** The doc places `check_z` inside `neighbors`, but the code reads it at top level.

```cpp
for( const auto &p : entries ) { p.first.check( oter_name, parameters ); }   // :3873
for( const auto &p : entries ) { p.first.check( oter_name, parameters ); }   // :3876 – should be else_entries
furn_id id( setmap.val.get() ); if( check_furn( id, context_ ) ) { return; } // :4913
```

**Fix:** Iterate `else_entries` in the second loop. Replace the `return` with a one-shot flag. Add `check()` overrides that validate EOC, talk topic, mongroup, faction and npc_template ids. `throw_error` on unsupported line/square ops in `setup_setmap`. Fix the `check_z` doc example, and document that `place_variables` ignores ranges.

### MGEN-B04 · map::rotate miscomputes queued mapgen variable points and drops points from other OMTs
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mapgen.cpp:7153`, `src/mapgen.cpp:7209`, `src/mapgen.cpp:7216`, `src/mapgen.cpp:7220`, `src/mapgen.cpp:7221`

Queued variable points come from `place_variables` and set `variable`. Content that uses them includes LIXA, refugee_center, portal_storm, unvitrified_farm and Xedra. `map::rotate` computes each point's local offset with `reality_bubble().get_bub(...) % SEEX`. For points west or north of the bubble origin, the remainder is negative, so the rotated point ends up a submap off. Points from another OMT are dropped after `queued_points.clear()`. That is reachable when leftovers remain, for example on `update_mapgen` paths that never call `set_queued_points`. The block also sits inside the 21-level z loop, so points are rotated 21 times. That only works because 21 % 4 == 1.

```cpp
const point_bub_ms queued_point_bub( reality_bubble().get_bub( queued_point.second.xy() ) );
point_bub_ms old( queued_point_bub.x() % SEEX, queued_point_bub.y() % SEEY );
```

**Fix:** Move the block out of the z loop. Use `coords::project_remain<coords::omt>(...)` for the local position, and re-insert non-matching points unchanged.

### MGEN-B05 · Item category spawn rates: quadratic overshoot above 1.0, ignored outside place_items
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mapgen.cpp:6757`, `src/mapgen.cpp:6770`, `src/mapgen.cpp:6773`, `src/mapgen.cpp:6779`, `src/map.cpp:6072`, `src/mapgen.cpp:2507`, `src/mapgen.cpp:2872`, `src/vehicle.cpp:6639`

When a category rate is above 1, `map::place_items` re-rolls the whole group for each such item, keeps the same-category results, and adds them `rate` more times. That is quadratic: N items at rate 2 give N + 2N² instead of 2N. No vanilla content sets a rate above 1, so this part is latent. The coverage gap is not latent. `place_loot`, `item`/`place_item`, sealed items, liquids, corpses and vehicle cargo never apply category rates, so No_Hope's `set_item_category_spawn_rates` EOC reduces only `items`/`place_items` spawns.

```cpp
Item_list extra_spawn = item_group::items_from( group_id, ... );   // re-rolled per item
for( int i = 0; i < item_cat_spawn_rate; i++ ) { for( const item &it : extra_spawn ) { ... add_res_itm( it ); } }
```

**Fix:** Apply the rate per spawned item, once. Below 1, keep the item with probability `rate`. Above 1, add `roll_remainder(rate)-1` copies, without re-rolling the group. Use one helper shared by all spawn paths, or put the logic in `Item_group::create`.

### MGEN-B06 · Variable-weight mapgens skip finalize_parameters, checks and special-param collection
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mapgen.cpp:536`, `src/mapgen.cpp:545`, `src/mapgen.cpp:551`, `src/mapgen.cpp:553`, `src/mapgen.cpp:558`, `src/mapgen.cpp:563`, `src/mapgen.cpp:569`, `src/mapgen.cpp:1212`

`mapgen_basic_container::setup()` moves mapgens with a non-constant weight (global_val or math) into `mapgens_to_recalc_`, then clears `mapgens_`. Four functions iterate only `weights_`: `finalize_parameters`, `check_consistency`, `check_consistency_with` and `get_mapgen_params`. So these mapgens:
- never merge nested-chunk parameters;
- never run `check()`;
- never report overmap_special-scope or palette parameters to specials.

Affected vanilla content includes roads, lakes, bridges, trails and both survivor camps. These get no load-time validation. The trail example shows no visible symptom today, but the bug hits any mod that uses the documented variable-weight feature.

```cpp
if( ptr->weight.is_constant() ) { weights_.add( ptr, weight ); } else { mapgens_to_recalc_.push_back( ptr ); }
mapgens_.clear();
void finalize_parameters() { for( auto &mapgen_function_ptr : weights_ ) ...
```

**Fix:** Iterate the union of `weights_` and `mapgens_to_recalc_`, or keep an `all_mapgens_` vector that is never cleared.

### MGEN-B07 · Start-location parameters can throw bad_optional_access during first map generation
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/start_location.cpp:350`, `src/start_location.cpp:352`, `src/start_location.cpp:365`, `src/start_location.cpp:376`, `src/mapgendata.cpp:95`, `src/mapgendata.cpp:103`, `src/mapgendata.cpp:104`, `src/start_location.cpp:145`, `src/game.cpp:854`

`start_location::set_parameters` sets the global `overmap_buffer.externally_set_args = true` before validating anything. It can then return early or `continue` past every parameter, with only a debugmsg, and leave the optional empty. The next `mapgendata` for a special OMT calls `maybe_args->value()` on `nullopt`. `game::start_game` has no try/catch, so new-game creation dies. `start_location::check()` doesn't validate parameter names or values. Vanilla data is valid, so triggering this takes a modder typo. Even when nothing throws, the flag is consumed by whichever special generates first.

```cpp
if( overmap_buffer.externally_set_args ) {
    maybe_args->value().map.merge( internally_set_args.map );   // mapgendata.cpp:104, value() on nullopt
```

**Fix:** In `mapgendata`, merge if the optional has a value, otherwise assign. Set the flag only after a value has actually been written, and store the target OMT/special instead of a global bool. Validate parameters in `start_location::check()`.

### MGEN-B08 · Nested mapgen "rotation" is parsed but never applied (1540 chunks ask for [0,3])
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/mapgen.cpp:4557`, `src/mapgen.cpp:4566`, `src/mapgen.cpp:4814`, `src/mapgen.cpp:6380`, `src/mapgen.cpp:6383`, `src/mapgen.h:592`, `doc/JSON/MAPGEN.md:1483`, `doc/JSON/JSON_Mapping_Guides/Guide_for_intermediate_mapgen.md:77`, `data/json/mapgen_palettes/mil_surplus.json:7`

`mapgen_function_json_nested::setup_internal` reads `"rotation"`, but `nest()` has the code that would use it commented out under a TODO. `check()` (4814) only calls `check_common`, so nothing warns. A data scan found 1540 nested objects with `"rotation": [0, 3]`, plus 4 with `1` and 1 with `[0, 2]`. That includes the intermediate-mapgen guide's own example. Authors expect random orientation and get none, and MAPGEN.md doesn't say rotation is a no-op for nested chunks. Turning rotation on wholesale is risky, though. Many of these chunks are orientation-specific: `indoor_tent_3x3_N`, `room_9x9_recroom_E`, and the street-facing `city_block_9x9_vitamin_shop_front`.

```cpp
if( jo.has_member( "rotation" ) ) { rotation = jmapgen_int( jo, "rotation" ); }   // :4566
// TODO: Make rotation work for submaps, then pass this value into elem & objects apply.
//int chosen_rotation = rotation.get() % 4;                                         // :6383-6384
```

**Fix:** First step (small): in `mapgen_function_json_nested::check()`, warn when rotation is non-zero, and document the limitation. Then implement rotation behind an opt-in. Nested sizes are already forced square (4557), so a `rotate(size, r)` helper next to `jmapgen_place::offset` can rotate placements, setmap endpoints, directional ids and vehicle facing. Gate it behind a new field such as `"allow_rotation": true`, or clean up the existing data before honouring `"rotation"`.

### MGEN-B09 · Parameters from nested-in-nested chunks never reach the top mapgen (finalize order)
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/mapgen.cpp:735`, `src/mapgen.cpp:736`, `src/mapgen.cpp:972`, `src/mapgen.cpp:1214`, `src/mapgen.cpp:3813`, `src/mapgen.cpp:3818`

`calculate_mapgen_weights()` finalizes top-level OMT mapgens before nested mapgens. `merge_parameters_into` copies only a child's current parameters, which don't yet include its own children's. So an omt- or overmap_special-scope parameter that is declared only by a grandchild chunk or its palette is never rolled. Fallback-guarded values always resolve to the fallback, and unguarded ones give "No such parameter". Example: `city_block_9x9_vitamin_shop_front` is always white linoleum. A scan found 34 such chains on special terrains. Separately, `merge_from` uses `return` instead of `continue` on a null or unknown id, so every id after a `"null"` entry is skipped.

```cpp
oter_mapgen.finalize_parameters();                          // :735 top level first
for( auto &pr : nested_mapgens ) { ... finalize_parameters(); }   // :736-742
if( id.is_null() ) { return; }                              // :3813 aborts loop
```

**Fix:** Finalize bottom-up. Memoize finalization, and have `merge_from` finalize the child (with a recursion guard) before reading its parameters. Change both `return`s in `merge_from` to `continue`.

### MGEN-B10 · map::mirror leaves vehicle caches, spawns, zones and variables unmirrored
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/mapgen.cpp:7240`, `src/mapgen.cpp:7600`, `src/mapgen.cpp:7605`, `src/mapgen.cpp:6259`, `src/submap.cpp:281`, `src/map.cpp:1691`

`rotate()` updates surrounding state that `mirror()` leaves alone. `mirror()` doesn't rebuild vehicle caches, doesn't update `veh->sm_pos`, and doesn't mirror zones, NPCs or `queued_points`. `submap::mirror` skips vehicles and spawns. `update_map` mirrors the map and then runs `apply_mapgen_in_phases` with `verify=true`; faction camps pass the mirror flags. The collision check calls `veh_at()`, which reads the stale cache. So with a car parked on the camp OMT, a `MAP_MIRROR_*` expansion is either refused because of a car on the wrong side, or it builds walls under the car. 133 blueprints use mirror flags.

```cpp
update_smap.mirror( mirror_horizontal, mirror_vertical );   // :7600, then update_map( md, offset, verify )
```

**Fix:** Mirror the same state that `rotate()` handles: clear and rebuild the vehicle caches, set `sm_pos`, mirror vehicle positions and facing, spawns, zones and queued points, and move NPCs. Add a unit test that mirrors a map containing a vehicle and checks `veh_at()`.

### MGEN-B11 · map_extra "delete" handling reads the "extend" object
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/map_extras.cpp:1362`, `src/map_extras.cpp:1363`, `src/map_extras.cpp:1369`, `src/map_extras.cpp:1370`, `src/generic_factory.h:1597`, `src/flexbuffer_json-inl.h:880`, `doc/JSON/MAPGEN.md:1659`

A hand-rolled block after `optional( jo, was_loaded, "flags", flags_, string_reader{} )` handles `"delete"` by reading `jo.get_object( "extend" )`. The impact is narrow. The generic reader already applies extend and delete, and `get_object` on a missing key returns an empty object, so copy-from with delete alone works fine. The bug shows only when one object has both `extend` and `delete`: the manual block then erases the flags that extend just added. Separately, the MAPGEN.md example uses `"generator_id": "mx_minefield"` with `map_extra_function`, but no such builtin exists, so copying the example fails `check()`.

```cpp
if( was_loaded && jo.has_member( "delete" ) ) {
    JsonObject joe = jo.get_object( "extend" );
```

**Fix:** Delete the redundant extend and delete blocks (1363-1374) and rely on the generic reader. Fix the doc example.

### MGEN-B12 · mapgen_forest corner-unification is dead code, and its quadrant mapping and math are wrong
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mapgen_functions.cpp:853`, `src/mapgen_functions.cpp:874`, `src/mapgen_functions.cpp:879`, `src/mapgen_functions.cpp:897`, `src/mapgen_functions.cpp:759`, `src/mapgen_functions.cpp:762`

`self_biome` is a by-value copy, so the `== &self_biome` comparisons against factory pointers are never true, and `unify_continuous_border` never runs. Even if it ran, two more bugs are waiting:
- three of the four quadrants pass the wrong corner triples;
- the second branch subtracts `*cw_weight` where it should subtract `*ccw_weight`.

The corner seeds also use `std::hash<uint32_t>`, which is the identity function. So border depths come out nearly constant (about 10 and 5). Corners where `x^(y<<1)==0` produce `log(0)`, which gives NaN cast to int. See MGEN-P05: fixing the copy turns this dormant logic on.

```cpp
forest_biome_mapgen self_biome = *current_biome_def_it->second;   // copy
if( ccw == corner && cw == &self_biome ) {                        // never true
```

**Fix:** Bind `self_biome` by const reference (or compare ids). Use SW=(2,6,3), NE=(0,4,1), SE=(1,5,2), and subtract `*ccw_weight`. Use a real integer mixer, such as a splitmix or murmur finalizer, for the seeds.

### MGEN-B13 · "z" offsets ignored by place_monsters, place_npcs and faction_owner
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mapgen.cpp:2066`, `src/mapgen.cpp:2111`, `src/mapgen.cpp:2542`, `src/mapgen.cpp:6610`, `src/mapgen.cpp:6682`, `src/mapgen.cpp:6691`, `src/mapgen_functions.cpp:2253`

These pieces accept `"z"` and then drop it:
- `jmapgen_npc` passes `dst.xy()`, and `place_npc` uses `abs_sub.z()`.
- `place_spawns` uses `z_level` only in its debug message.
- `apply_faction_ownership` is 2D.
- `resolve_regional_terrain_and_furniture` resolves only one z, so pseudo-terrain at z+1 from an update_mapgen stays unresolved.

`place_monster` does honour z. The bug is latent: no current JSON uses a non-zero z on these pieces, although about 170 update_mapgens use z:1 for other pieces.

```cpp
character_id npc_id = dat.m.place_npc( dst.xy(), chosen_id );   // :2066
add_spawn( mgr.id, mgr.pack_size, { p, abs_sub.z() }, ... );    // :6610
```

**Fix:** Make `place_npc`, `place_spawns` (including the `impassable()` retry) and `apply_faction_ownership` take a z. Resolve regional terrain on every z level that the pieces touched.

### MGEN-B14 · Camp construction markers miss non-ground parts (fake map z=0 vs omt z)
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mapgen.cpp:7757`, `src/mapgen.cpp:7780`, `src/mapgen.cpp:7818`, `src/mapgendata.cpp:53`

`apply_construction_marker` runs the blueprint on a fake map whose position is zero, then scans only `z == omt_pos.z()`, and `update_tmap` is single-level. So the z:1 parts of about 170 multi-z blueprints get no `fd_construction_site` marker. A camp at z != 0 would scan the wrong level, but such camps are rare. `get_changed_ids_from_update` loops to `OVERMAP_DEPTH` instead of `OVERMAP_HEIGHT`, which is harmless while both are 10. The impact is cosmetic markers only.

```cpp
for( const tripoint_omt_ms &pos : tmp_map.points_on_zlevel( omt_pos.z() ) ) {   // :7780
```

**Fix:** Scan every fake z, map it to `omt_pos.z() + z_fake`, and mark through a smallmap. Fix the loop bound.

### MGEN-B15 · mapgen_river_curved_not overwrites its land corner with shallow water
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mapgen_functions.cpp:555`, `src/mapgen_functions.cpp:558`, `src/mapgen_functions.cpp:560`

There is a missing `else`. A tile with `circle_edge <= 8` is set to land and then immediately falls into `else if( circle_edge <= 36 )`, which overwrites it with `t_water_moving_sh`. So the inner-corner land tip of `river_c_not_*` tiles never appears. The effect is cosmetic.

```cpp
if( circle_edge <= 8 ) { m->ter_set( ..., grass_or_dirt() ); }
if( circle_edge == 9 && one_in( 25 ) ) { ... } else if( circle_edge <= 36 ) { m->ter_set( ..., ter_t_water_moving_sh ); }
```

**Fix:** Chain the conditions into one if / else-if sequence.

### MGEN-B16 · Phase-change block at the end of map::generate is dead and loops over the wrong range
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mapgen.cpp:226`, `src/mapgen.cpp:227`, `src/mapgen.cpp:383`, `src/mapgen.cpp:384`, `src/mapgen.cpp:386`

When the z loop ends, `abs_sub.z()` is `-OVERMAP_DEPTH`, so `if( abs_sub.z() >= 0 )` is always false. Even if the block ran, it would loop `i, j < my_MAPSIZE` (2) as tile coordinates and touch only 4 tiles. Phase change actually happens in `map::loadn` (`map.cpp:9500`). The block is misleading but harmless, and it predates the fork.

```cpp
if( abs_sub.z() >= 0 ) { for( int i = 0; i < my_MAPSIZE; i++ ) { for( int j = 0; j < my_MAPSIZE; j++ ) {
```

**Fix:** Delete the block. The load path already covers phase changes (see MGEN-P01).

### MGEN-B17 · place_monster uses SPAWN_DENSITY even for animals
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mapgen.cpp:2637`, `src/mapgen.cpp:6584`, `src/mapgen.cpp:309`

`place_spawns` and the OMT static spawns switch to `SPAWN_ANIMAL_DENSITY` for animal groups. `jmapgen_monster` always uses `SPAWN_DENSITY` and truncates the result to int. As a result, the difficulty sliders (0.8 to 3) scale mapgen farm animals that have a chance below 100, and the `SPAWN_ANIMAL_DENSITY` setting (set to 1.5 by XedraWood) is ignored for `place_monster`.

```cpp
int odds_after_density = raw_odds * get_option<float>( "SPAWN_DENSITY" );   // :2637
```

**Fix:** Choose the density option by whether the mtype or group is an animal, and keep the math in float until the `x_in_y` roll.

## 3. Performance

### MGEN-P01 · Phase-change pass runs on every tile of every loaded submap and re-freezes ice via two full ter_set calls
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/map.cpp:9490`, `src/map.cpp:9498`, `src/mapgen.cpp:6455`, `src/mapgen.cpp:6458`, `src/mapgen.cpp:6467`, `src/mapgen.cpp:6478`, `src/mapgen.cpp:6538`, `src/map.cpp:2619`, `src/map.cpp:2626`, `src/map.cpp:2686`, `src/mapgen.cpp:383`

`map::loadn` calls `temp_based_phase_change_at` on all 144 tiles of each submap, inside the 21-level z loop. That covers every bubble shift and every tinymap or smallmap load. Every tile pays two `has_original_terrain_at` map lookups. Each `water_freeze` tile, which includes uniform lake surfaces, takes 10 `get_weather_temperature` samples, each with cos() and 4D simplex noise. That is about 1,440 noise evaluations per lake submap on every load. The samples snap to 08:00, so the result changes only once a day. Frozen tiles are worse: they are reverted and then re-frozen through two full `ter_set` calls. Each call does an avatar `sees()` query, dirties caches, sets `player_adjusted_map`, and breaks the uniformity of uniform submaps. Driving along water hits this constantly. The generate-time pass is dead code (MGEN-B16).

```cpp
for( int sx = 0; sx < SEEX; ++sx ) { for( int sy = 0; sy < SEEX; ++sy ) {
    temp_based_phase_change_at( sm_origin + point( sx, sy ), wgen );          // map.cpp:9498-9501
if( has_original_terrain_at( p ) ) { ter_set( p, get_original_terrain_at( p ) ); }   // mapgen.cpp:6458
```

**Fix:**
1. Compute the target terrain first, and call `ter_set` only when it differs from the current terrain. Use a guard so the change doesn't mark the submap as player-adjusted.
2. Store a last-checked day, or a `has_phase_terrain` flag, on each submap. Skip unchanged submaps, dry submaps and uniform submaps.
3. Hoist the per-call constants (common weather data, season, cos) out of the tile loop.

### MGEN-P02 · Every mapgen pass checks vehicle collisions across its whole nested tree, even with verify=false
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mapgen.cpp:6259`, `src/mapgen.cpp:6278`, `src/mapgen.cpp:3941`, `src/mapgen.cpp:6437`, `src/mapgen.cpp:6186`, `src/mapgen.cpp:6359`, `src/mapgen.cpp:6391`, `src/mapgen.cpp:7674`

`apply_mapgen_in_phases` always calls `objects.has_vehicle_collision()` and the per-setmap check, but it reads the result only when `verify` is true. Only `update_map` passes true; `generate` and `nest` never do. `jmapgen_nested::has_vehicle_collision` walks every chunk entry and every variant, and each `nest()` repeats the walk, so the work adds up roughly as depth × tree size. It also consumes RNG for no benefit. Realistic cost is tens to hundreds of µs per OMT: `veh_at` exits early, constrained palette pieces are no-ops, and fixed positions don't roll RNG.

```cpp
const ret_val<void> has_vehicle_collision = objects.has_vehicle_collision( md, offset );
if( verify && !has_vehicle_collision.success() ) {          // :6259-6263
```

**Fix:** Wrap both checks in `if( verify )`. Camps, which pass `verify=true`, are unaffected. Optionally, on the verify path check only the chunk that is actually picked.

### MGEN-P03 · Post-process generators pick random tiles from a 576-node std::list (O(n) random_entry)
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mapgen_post_process.cpp:956`, `src/mapgen_post_process.cpp:674`, `src/mapgen_post_process.cpp:506`, `src/mapgen_post_process.cpp:493`, `src/mapgen_post_process.cpp:702`, `src/mapgen_post_process.cpp:774`, `src/rng.h:138`, `src/mapgen.cpp:347`

`pp_generator::execute` runs for every z-level of every city building and road OMT. It builds a `std::list` of 576 tiles. The sub-generators then call `random_entry` on it, which does `std::advance`:
- place_blood 576 times (before its skip roll)
- move_items 250 times
- bash_damage about 25 times

That is about 245k dependent pointer hops per city OMT z-level, roughly 0.3-0.5 ms. `execute_aftershock_ruin` is quadratic in the same way.

```cpp
std::list<tripoint_bub_ms> all_points_in_map;                                    // :956
for( int i = 0; i < sg.attempts; i++ ) { tripoint_bub_ms current_tile = random_entry( all_points_in_map );  // :674
```

**Fix:** Use a `std::vector` with `reserve(576)`, or index math from a single rng draw. It draws the same `rng(0, size-1)`, so output for a given seed doesn't change.

### MGEN-P04 · resolve_regional_terrain_and_furniture rescans the full z-level after every nested chunk
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mapgen.cpp:6289`, `src/mapgen.cpp:6424`, `src/mapgen.cpp:6391`, `src/mapgen.cpp:6560`, `src/mapgen_functions.cpp:2248`, `src/regional_settings.cpp:870`

`apply_mapgen_in_phases` always ends with a full-level resolve, and every `nest()` goes through it. There are two more resolves: at the first vehicle (6424) and in `draw_map` (6560). Each scan does ter and furn lookups and a flag test for all 576 tiles, about 10-15 µs. A nest touches only its own footprint, so a building with N nests pays N+2 scans, a few hundred µs. The linear `find_if` in `resolve` is minor.

```cpp
resolve_regional_terrain_and_furniture( md, offset.z() );   // :6289, reached from nest() at 6391
```

**Fix:** Pass a rectangle `[offset, offset + mapgensize)` from `nest()`, or drop the per-nest call and rely on the final resolve. Compare against `t_null` instead of calling `.id().is_null()`. Optionally precompute a ter/furn → region id map.

### MGEN-P05 · mapgen_forest makes per-tile heap allocations and copies the whole biome per OMT
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mapgen_functions.cpp:853`, `src/mapgen_functions.cpp:874`, `src/mapgen_functions.cpp:879`, `src/mapgen_functions.cpp:980`, `src/mapgen_functions.cpp:1029`, `src/mapgen_functions.cpp:1081`, `src/weighted_list.h:33`

`mapgen_forest` runs for the most common surface OMTs. For each tile it builds two `weighted_float_list`s, and each one's vector grows 1→2→4→8. That is about 8 mallocs and 8 frees per tile, roughly 4.6k malloc/free pairs per OMT. It also copies the whole `forest_biome_mapgen` once per OMT, and `forest_biome_terrain_dependent_furniture_new` once per tile that needs it. The biome copy is also what makes the border-unification code dead (MGEN-B12).

```cpp
forest_biome_mapgen self_biome = *current_biome_def_it->second;                       // :853
const forest_biome_terrain_dependent_furniture_new tdf = terrain_dependent_furniture_it->second;  // :1081
```

**Fix:** Use a stack `std::array<std::pair<int,double>,6>` with an inline cumulative pick, keeping the same RNG formula. Bind the biome and `tdf` by const reference. Fix MGEN-B12 at the same time, because binding by reference turns the dormant border logic on.

### MGEN-P06 · Lake and ocean shores generate the adjacent forest twice
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mapgen_functions.cpp:1174`, `src/mapgen_functions.cpp:1175`, `src/mapgen_functions.cpp:1636`, `src/mapgen_functions.cpp:1637`

When a lake or ocean shore extends an adjacent forest or field, it calls `mapgen_forest()` directly and then calls `run_mapgen_func()` on the same data. The second call reruns the builtin forest, or the field's `fill_ter` JSON, and overwrites every tile; the items are cleared afterwards anyway. The first pass is always wasted.

```cpp
mapgendata forest_mapgen_dat( dat, most_common_adjacent->first );
mapgen_forest( forest_mapgen_dat );
did_extend_adjacent_terrain = run_mapgen_func( most_common_adjacent->first.id().str(), forest_mapgen_dat );
```

**Fix:** Delete the direct `mapgen_forest()` call in both functions. If the lookup fails, `fill_groundcover()` already runs.

### MGEN-P07 · Mapgen parameters and palette constraints resolved per tile via string-keyed lookups and re-interning
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/mapgen.cpp:1965`, `src/mapgen.cpp:4411`, `src/mapgen.cpp:4346`, `src/mapgen.cpp:4734`, `src/mapgen.cpp:1427`, `src/mapgendata.h:259`, `src/cata_variant.h:152`, `src/generic_factory.h:167`

Parameterized palettes are flattened at setup. Every piece of every branch is wrapped in a `jmapgen_constrained` and added once per tile, so house tiles carry several constrained duplicates. At generation time, each duplicate checks every constraint:
1. it does an `unordered_map<std::string>` find;
2. it re-interns the value through `cata_variant::get<T>` (`T(v)`);
3. only then does it compare.

`param_source::get` also builds a fresh `string_id`, which misses the factory cache. The estimate (not measured) is 0.3-1 ms per house z-level.

```cpp
for( const mapgen_constraint<Value> &constraint : constraints ) {
    Value param_value = dat.get_arg<Value>( constraint.parameter_name );   // :1968
```

**Fix:** Give each parameter a slot index at finalize, and have `mapgendata` hold resolved, typed values per slot. As an interim fix, compare `get_string() == constraint.value.str()`. Longer term, pick the palette branch once per `generate()` and skip whole groups of objects whose constraints don't match.

### MGEN-P08 · map::rotate rebuilds vehicle caches and re-rotates queued points once per z-level, and rotations aren't merged
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mapgen.cpp:7129`, `src/mapgen.cpp:7148`, `src/mapgen.cpp:7153`, `src/mapgen.cpp:7207`, `src/mapgen.cpp:7209`, `src/mapgen.cpp:6342`, `src/mapgen.cpp:6345`, `src/mapgen.cpp:6362`, `src/mapgen.cpp:6365`, `src/map.cpp:632`, `src/mapgen_functions.cpp:544`, `src/mapgen_functions.cpp:2132`

`rebuild_vehicle_level_caches()` and the `queued_points` copy sit inside the 21-level z loop. The rebuild costs O(Z × parts) on OMTs with vehicles; with no vehicles it exits early, and `queued_points` is normally empty. In `generate()`, the explicit `rotation` and the terrain rotation are applied as two separate `rotate` calls. That matters for about 58 files.

```cpp
const int rot = rotation.get() % 4; m->rotate( 4 - rot );
if( ter.is_rotatable() || ter.is_linear() ) { m->rotate( ( -ter.get_rotation() + 4 ) % 4 ); }   // :6341-6346
```

**Fix:** Hoist the cache rebuild and the points rotation out of the z loop. This also fixes the 21-fold rotation in MGEN-B04. Compose the two calls into one `rotate( ( 8 - rot - ter_rot ) % 4 )`, and skip it when the sum is 0 mod 4.

### MGEN-P09 · place_items and spawn_items copy every generated item instead of moving it
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mapgen.cpp:6750`, `src/mapgen.cpp:6757`, `src/mapgen.cpp:6773`, `src/mapgen.cpp:2509`, `src/map.cpp:5839`, `src/map.cpp:5991`, `src/map.cpp:6144`

`add_res_itm` and `spawn_items` pass `const item&` into the by-value `add_item_or_charges`, which deep-copies each item. Two more copies happen downstream: in the `place_item` lambda (map.cpp:5991) and in `colony::insert` (6144). So a move at the outer call removes one of about three copies.

```cpp
auto add_res_itm = [this, &p, &res]( const item & itm ) { item &it = add_item_or_charges( p, itm );   // :6750
```

**Fix:** Iterate a local `Item_list` by non-const reference and `std::move` each item, reading its category before the move. Add a `spawn_items(..., std::vector<item>&&)` overload. Remove the inner copies at map.cpp:5991 and 6144 as well.

### MGEN-P10 · map::generate does full per-level setup even for uniform or skipped z-levels
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mapgen.cpp:200`, `src/mapgen.cpp:251`, `src/mapgen.cpp:263`, `src/mapgen.cpp:284`, `src/mapgen.cpp:289`, `src/regional_settings.cpp:854`

On all 21 levels, `map::generate` does four kinds of wasted work:
- It sums mondensity over 49 OMTs and constructs a full `mapgendata`, even when `draw_map` is skipped for uniform terrain.
- It copies `region_settings_map_extras` by value, although the getter returns a const reference.
- It filters the extras list before the usually-failing `one_in(chance)` roll.
- It repeats the uniform-terrain lookup inside the x/y loops.

The total is tens of µs per OMT.

```cpp
const region_settings_map_extras settings_mx = overmap_buffer.get_settings( p ).get_settings_map_extras();   // :284
map_extra_collection ex = this_ex.filtered_by( dat );                                                       // :289
```

**Fix:** Build the density and `mapgendata` only when they are needed. Bind `settings_mx` by const reference. Roll `one_in` before filtering. Hoist the uniform lookup out of the x/y loops. See also the double `pick()` noted in MGEN-J03.

### MGEN-P11 · Startup mapgen setup pumps input events once per nested/update mapgen (~8k times)
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/mapgen.cpp:608`, `src/mapgen.cpp:724`, `src/mapgen.cpp:730`, `src/mapgen.cpp:740`, `src/mapgen.cpp:746`, `src/sdltiles.cpp:7083`, `src/sdltiles.cpp:5784`, `src/ncurses_def.cpp:399`

`calculate_mapgen_weights` calls `inp_mngr.pump_events()` after each setup and finalize of about 3.4k nested and 0.8k update mapgens, and `mapgen_factory::setup` calls it once per OMT key. In tiles builds each call is an SDL poll; repaint is throttled to 25 ms, so each call costs microseconds. This is a one-time startup cost. It was never measured and is probably small next to JSON parsing. The same pattern appears in `init.cpp:198`, `recipe_dictionary.cpp:706` and `item_factory.cpp:3037`.

```cpp
for( auto &pr : nested_mapgens ) { for( ... ) { ptr.first->setup(); inp_mngr.pump_events(); } }
```

**Fix:** Throttle by time inside `input_manager::pump_events` itself, for example at most once every ~50 ms. That covers every caller.

### MGEN-P12 · Load-time nested-overlap check is quadratic in sibling nests and recomputes terrain maps uncached
**Severity:** Low · **Effort:** Medium · **Verified:** plausible
**Where:** `src/mapgen.cpp:5561`, `src/mapgen.cpp:5622`, `src/mapgen.cpp:5641`, `src/mapgen.cpp:5775`, `src/mapgen.cpp:5783`, `src/mapgen.cpp:3883`, `src/mapgen.cpp:4937`, `src/init.cpp:948`

`check_nested_overlaps` runs on every world load unless SKIP_VERIFICATION is set. It has three costs:
- Phase 1 rebuilds `collect_terrain_data` for every chunk variant without caching, and copies a set for each overlap.
- Phase 2 compares every sibling pair, which is O(S² × |T| × |F|).
- `jmapgen_nested::check` rebuilds `all_placement_coords()` for each placement.

The real cost is lower than it looks. Pairs exit early when terrain reliably clears furniture, most road and sidewalk chunks set ERASE_ALL flags, and empty siblings are skipped. A separate problem: the function-local static caches (5171/5241/5344/5451) are never cleared between world loads, and they are keyed only by nest id.

```cpp
terrain_coord_map nest_terrain; variant.collect_terrain_data( nest_terrain );   // :5621-5622
```

**Fix:** Use `std::bitset<24*24>` coordinate sets, so a pair test becomes a shift plus AND. Memoize the collected data per nested mapgen for the duration of a check run. Hoist `all_possible_results()` out of the loops. Replace the statics with caches owned by `check_mapgen_definitions` and cleared after each run.

### MGEN-P13 · Each nested chunk deep-copies mapgendata and builds its arguments eagerly
**Severity:** Low · **Effort:** Medium · **Verified:** plausible
**Where:** `src/mapgen.cpp:6386`, `src/mapgen.cpp:1837`, `src/mapgen.cpp:1845`, `src/mapgen.cpp:1861`, `src/mapgendata.cpp:148`, `src/mapgen.cpp:1762`

Every `nest()` builds a `mapgendata` with `get_args(...)`, which costs several copies:
- `get_args` evaluates defaults eagerly (`param.get(md)`), even when the argument already exists.
- It then copies its result into `mapgen_arguments`.
- The `mapgendata` copy constructor deep-copies the parent's args, joins, predecessors and groundcover, then merges the new args.

`nest()` also concatenates a context string that is used only in debug messages. The default parameter scope is `overmap_special`, and only about 5 files use `nest` scope, so `get_args(nest)` is usually empty. What remains is mostly one deep copy per `nest()`. The overall cost is unmeasured.

```cpp
mapgendata md_with_params( md, get_args( md, mapgen_parameter_scope::nest ), flags_ );   // :6386
cata_variant value = md.get_arg_or( param_name, param.get( md ) );                        // :1845
```

**Fix:** Evaluate defaults only when the argument is missing, and move the temporary into `mapgen_arguments`. Skip the args copy entirely when `get_args` is empty. Consider giving `mapgendata` a shared parent-args pointer plus a small overlay. Build the context string lazily.
