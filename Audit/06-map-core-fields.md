# 06 · Map core, submaps, terrain/furniture data & fields

> Engine audit – static code reading at commit `a1a646d6`; nothing was compiled or run. Each finding was checked against the code by a separate reviewer: **confirmed** = verified by reading the code; **plausible** = consistent with the code, but reachability or impact not proven. Line numbers refer to that commit.

**Scope:** `src/map.cpp`, `src/submap.cpp`, `src/mapbuffer.cpp`, `src/map_memory.cpp`, `src/level_cache.cpp`, `src/map_field.cpp`, `src/field.cpp`, `src/field_type.cpp`, `src/mapdata.cpp`, `src/map_helpers.cpp`, `src/map_accessories.cpp`, `src/current_map.cpp`, `src/map_iterator.h`

## Summary

The map core works, but much of its content-facing behaviour is still chosen by comparing hardcoded ids. That covers field behaviour (fire, acid, electricity), terrain transitions, appliance cycles and off-screen terrain processes. Tile destruction cannot trigger an EOC, and EOCs cannot read or write tile radiation, set field intensity exactly, or store per-tile state. The terrain, furniture and field loaders mishandle `copy-from`/`extend` in several places. The confirmed runtime bugs include a wind-blocker table that blocks the wrong side, a vehicle double-free in `merge_submaps`, rain decay that never applies, and field-spawned monsters that only appear on the next map shift. On performance, the main costs are work repeated on every submap crossing or tinymap load (item wakeup reconciliation, eager cache allocation, repeated zzip opens) and caches dirtied far more widely than the change requires.

| Lens | Critical | High | Medium | Low | Total |
|---|---|---|---|---|---|
| Missing JSON / EOC | 0 | 2 | 4 | 4 | 10 |
| Bugs & quirks | 0 | 2 | 8 | 8 | 18 |
| Performance | 0 | 3 | 4 | 9 | 16 |
| **Total** | 0 | 7 | 16 | 21 | 44 |

**Fix first:**
1. **MAP-B01** · Wind-blocker table uses math angles: a small table fix that corrects fire and gas spread in every windy outdoor scene.
2. **MAP-B02** · `merge_submaps` double-owns vehicles: a one-loop fix for a use-after-free and double delete that any modder can trigger with cross-z mapgen.
3. **MAP-P01** · Full item reconciliation on every map shift: a small change that removes a multi-millisecond stall from every submap crossing.
4. **MAP-P02** · Temporary maps allocate and clear tens of MB of caches: lazy allocation saves several milliseconds per generated OMT and per tinymap load.
5. **MAP-B03** · Rain field decay is a no-op: a one-line fix that makes 57 documented `decay_amount_factor` settings take effect.

## 1. Missing JSON bindings & EOC features

### MAP-J01 · Destroying terrain/furniture cannot trigger an EOC; the only event fires before the outcome and has no location
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/map.cpp:4550`, `src/map.cpp:4842`, `src/handle_action.cpp:946`, `src/event.h:475`, `src/event.h:588`, `src/mapdata.cpp:386`, `src/map.h:170`

`map::bash_ter_furn` is the single choke point for every tile destruction: player smashing, monsters, vehicles, explosions, fire (`map_field.cpp:1076`), collapse, shooting and `destroy()`. It changes the terrain or furniture, drops items and spawns hit/destroyed fields, but it runs no EOC and sends no event. The only related event, `character_smashes_tile`, is sent from the avatar's smash handling before the bash is resolved. It has no location, no success flag and no resulting ter/furn, and it never fires for monster, explosion, fire or vehicle destruction. `cuts_tree` carries only the character id. Modders cannot write "smashing the cursed altar angers the faction" unless the destruction came from the player's smash key, and even then they cannot tell whether the tile broke or where it was.

```cpp
// handle_action.cpp:946 — sent before here.bash() runs
get_event_bus().send<event_type::character_smashes_tile>( getID(), here.ter( smashp ).id(), here.furn( smashp ).id() );
// event.h:588
struct event_spec<event_type::cuts_tree> : event_spec_character {};
```
`map_common_bash_info::load` (`mapdata.cpp:386-428`) reads no EOC field. `bash_params` (`map.h:170`) carries no basher or cause, so the hook needs some plumbing through callers.

**Fix:** Add `"eoc_on_hit"` and `"eoc_on_destroy"` to the bash object (terrain, furniture and field bash). Run them from `bash_ter_furn` with alpha = the basher when known and context vars `loc`, `old_ter`, `old_furn`, `new_ter`, `new_furn`, `cause` (`smash|explosion|fire|vehicle|collapse|projectile`). Add an event `tile_destroyed { location: tripoint_abs_ms, old_ter, old_furn, new_ter, new_furn, character (optional) }` sent from the same place. Move `character_smashes_tile` after the bash and add `location` and `success` fields.

### MAP-J02 · Field behaviours are tied to hardcoded field ids; `has_fire`/`has_acid`/`has_elec` mostly control immunity
**Severity:** High · **Effort:** Large · **Verified:** confirmed
**Where:** `src/map_field.cpp:2296`, `src/map_field.cpp:2318`, `src/map_field.cpp:2345`, `src/map_field.cpp:1490`, `src/map_field.cpp:1691`, `src/map_field.cpp:1895`, `src/map_field.cpp:2053`, `src/map_field.cpp:814`, `src/map_field.cpp:393`, `src/field.cpp:117`, `src/map.cpp:4056`, `src/map.cpp:7553`

Every non-trivial field behaviour is selected by field id. That includes the per-turn processors (fire spread and fuel, acid falling, electricity arcing, extinguisher, vents, incendiary, fungal haze, fungicide, push_items), creature contact damage in `player_in_field`/`monster_in_field`, fire's special decay curve, `has_nearby_fire` (used for crafting and cooking fire checks) and hot air (only `fd_hot_air1..4`). The JSON flags `has_fire`, `has_acid` and `has_elec` are read by immunity and avoidance checks, plus a few display, lava and EMP checks, but never drive behaviour. A modder's `fd_hellfire` with `has_fire: true` does not spread, burn items, hurt anyone, decay like fire or count as a cooking fire. Per-intensity `effects` can only apply effect types; effect types can deal HURT damage and change speed, but cannot reduce moves directly or run an EOC.

The id dispatch also breaks vanilla behaviour. `map::add_field` turns `fd_electricity` into `fd_electricity_unlit` on non-transparent tiles, including thick smoke, but player and monster contact damage check only `fd_electricity`, and the unlit variant defines no effects. Electricity on a smoky or opaque tile therefore never damages creatures. `fd_push_items` only pushes `itype_rock`.

```cpp
if( ft.id == fd_acid ) { processors.push_back( &field_processor_fd_acid ); }   // map_field.cpp:2296
if( ft == fd_electricity ) {                // map_field.cpp:1691 (player) — unlit variant missing
if( cur_field_type == fd_electricity ) {    // map_field.cpp:2053 (monster)
if( type == field_fd_fire ) { // Legacy handling for fire   (field.cpp:117)
if( has_field_at( pt, fd_fire ) )           // map.cpp:4056
if( pushee->typeId() != itype_rock ||       // map_field.cpp:814
```

**Fix:** Select behaviour from JSON. On `field_type`:
```json
"processors": [
  { "type": "fire", "spread_chance": 25, "smoke_field": "fd_smoke", "ash_item": "ash" },
  { "type": "fall" },
  { "type": "arc", "grounded_weight": 6 },
  { "type": "push_items", "items": [ "rock" ] }
],
"decay_mode": "exponential|linear|fire"
```
On each intensity level: `"contact_damage": [ { "damage_type": "heat", "amount": [ 2, 6 ], "body_parts": "ground_contact|all|[torso]", "monster_mult": 1.0 } ]` and `"eoc_on_contact": "EOC_ID"` (u = the creature, context var `loc`). Make `has_nearby_fire` and `get_heat_radiation` check `has_fire` plus intensity rather than `fd_fire`, and add a per-intensity `"hot_air_field"`. Convert the vanilla fields one at a time. As an immediate fix, add `fd_electricity_unlit` to the two contact-damage checks.

### MAP-J03 · Washer/dishwasher/autoclave/weather-station cycles are hardcoded in map.cpp
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/map.cpp:6261`, `src/map.cpp:6275`, `src/map.cpp:6300`, `src/map.cpp:6333`, `src/map.cpp:6336`, `src/map.cpp:6373`, `src/map.cpp:171`

`process_vehicle_items` drives every timed appliance from C++. Washing machine and dishwasher use a fixed 90-minute cycle that only clears `FILTHY`; the autoclave uses 90 minutes and clears `NO_STERILE`. The mobile weather station uses 60 minutes, compares four portal-storm weather id strings, runs about 19 `overmap_buffer.find_closest` lookups against hardcoded overmap terrain ids, and spawns a hardcoded printout item for each. A modder cannot add a dryer, a faster industrial washer, another sterilizer, or a new MWS report without editing C++, and each new piece of vanilla lore content also needs a C++ edit here. Current workarounds are partial: vpart `activatable_eoc` runs from a separate menu entry, and `map_run_item_eocs` cannot reach vehicle cargo.

```cpp
const time_duration washing_time = 90_minutes;                                // :6275
( current_weather( cur_veh.pos_abs() ).str() == "portal_storm" ) ||           // :6336
overmap_buffer.find_closest( veh_position, "unvitrified_orchard", 10, false ); // :6373
```

**Fix:** Add a vpart field:
```json
"processing_cycle": {
  "duration": "90 m", "unset_item_flags": [ "FILTHY" ], "set_item_flags": [],
  "transform_items": [ { "from": "mws_weather_data_incomplete", "to": "mws_weather_data" } ],
  "progress_message": "...", "finish_message": "...", "eoc_on_finish": "EOC_MWS_REPORT"
}
```
Run the EOC with the vehicle talker and context var `loc`. Move the MWS proximity table to JSON, either through the EOC (`u_near_om_location`/`map_in_city` plus `u_spawn_item`) or a data list `[ { "om_terrain": "unvitrified_orchard", "range": 10, "item": "HEW_printout_data_vitrified" } ]`.

### MAP-J04 · Off-screen terrain behaviours (sap, radiation scorch, root cellar) are keyed to specific ids; no actualize hook
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/map.cpp:9725`, `src/map.cpp:9730`, `src/map.cpp:9790`, `src/map.cpp:9887`, `src/map.cpp:9908`, `src/map.cpp:6646`, `src/map.cpp:9986`

`map::actualize` applies "time passed while unloaded" logic to every tile, but the content-specific parts are C++. `produce_sap` works only on `t_tree_maple_tapped`, always yields `maple_sap` at 56 per season, and only in a hardcoded late-winter/early-spring window. `rad_scorch` has a 7-entry table marked `// TODO: De-hardcode this` and otherwise falls back to `t_dirt` or `t_tree_dead`, so mod trees (aftershock alien flora, MoM mycus trees) die into Earth terrain. Root-cellar item temperature is enabled only for `ter == t_rootcellar`, and terrain or furniture cannot act as an unpowered fridge or freezer at all (`FRIDGE`/`FREEZER` come only from vehicle parts). There is no hook for custom elapsed-time behaviour such as resin taps or regrowing mushrooms. The `NO_SPOIL` flag is a partial stand-in for preservation but not a temperature mode.

```cpp
if( !( ter( p ) == ter_t_tree_maple_tapped ) ) { return; }                    // :9725
static const int maple_sap_per_season = 56;                                    // :9730
// TODO: De-hardcode this
static const std::map<ter_id, ter_str_id> dies_into {{ {ter_t_grass, ter_t_dirt}, ... // :9887
if( ter( map_location ) == ter_t_rootcellar ) { flag = temperature_flag::ROOT_CELLAR; } // :6646
```

**Fix:** Add terrain/furniture fields:
```json
"produces_liquid": { "item": "maple_sap", "per_season": "56 L",
                     "active": [ [ "winter", 0.75, 1.0 ], [ "spring", 0.0, 0.5 ] ], "poison_chance": 10 },
"rad_scorch": "ter_furn_transform_id",
"item_temperature": "root_cellar|fridge|freezer",
"eoc_on_actualize": "EOC_ID"
```
`rad_scorch` reuses `ter_furn_transform`, with a region or global default for SHRUB/TREE. `eoc_on_actualize` is opt-in and runs once per tile on load with context vars `loc` and `elapsed`; collect those tiles like `emissions` so other tiles cost nothing.

### MAP-J05 · Hardcoded terrain transition tables (acid, tree felling, vehicle ruts, roof fallback, fire pits, ash, fungicide)
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/map.cpp:5334`, `src/map.cpp:9843`, `src/map.cpp:9863`, `src/map.cpp:1240`, `src/map.cpp:4513`, `src/map.cpp:4525`, `src/map.cpp:5372`, `src/map_field.cpp:1176`, `src/map_field.cpp:1108`, `src/map_field.cpp:956`

Several "X becomes Y" rules use fixed id tables instead of existing mechanisms such as `ter_furn_transform` or bash `ter_set`:
- `hit_with_acid` (monster acid spit) melts only about 20 named vanilla terrains, so mod and variant doors, windows and bars are immune.
- `cut_down_tree` turns every TREE into `t_trunk` plus `t_stump`, so fungal, alien or palm trees fell into pine stumps.
- A wheel-less vehicle ruts only `t_dirt`/`t_grass` into `t_dirtmound`.
- `get_roof` falls back to `t_dirt` and hacks z=-1 `t_rock_floor` into `t_dirt`.
- The fire pit-edge check compares against `t_pit` only, so the 9 pit variants don't count.
- `FLAMMABLE_ASH` furniture always becomes `f_ash`.
- Fungicidal gas turns all FUNGUS terrain into `t_dirt`.
- `hit_with_fire` (`map.cpp:5372`, declared `map.h:1354`) has no callers.

```cpp
if( t == ter_t_wall_glass || t == ter_t_wall_glass_alarm || t == ter_t_vat ) { ter_set( p, ter_t_floor ); } // map.cpp:5340
ter_set( elem, ter_t_trunk );                          // map.cpp:9843
const bool in_pit = ter.id.id() == ter_t_pit;         // map_field.cpp:1176
pd.here.ter_set( p, ter_t_dirt );                      // map_field.cpp:956
```

**Fix:** Add per-tile JSON: `"damage_transforms": { "acid": "ter_furn_transform_acid" }`; `"felled": { "trunk": "t_trunk", "stump": "t_stump" }` on TREE terrain; `"churned_into": "t_dirtmound"`; a `FIRE_PIT`-style flag or connect group instead of the `t_pit` id; `"ash_result"` on FLAMMABLE_ASH furniture; `"fungicide_result"` (or a transform id) on FUNGUS tiles; region_settings `default_roof`/`solid_earth` for `get_roof`. Delete `hit_with_fire`.

### MAP-J06 · EOCs cannot read or write map radiation or set/reduce fields, and tiles have no variables or talker
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/math_parser_diag.cpp:1911`, `src/npctalk.cpp:8207`, `src/map.cpp:7557`, `src/field.cpp:192`, `src/talker_furniture.h:19`, `doc/JSON/EFFECT_ON_CONDITION.md:5853`

EOCs can test map ids and flags and can set terrain, furniture and fields, but map state beyond ids is missing. No math function reads tile radiation; the only indirect write is `mapgen_update` with a `radiation` piece, which cannot target a tile chosen at runtime. `field_strength()` is read-only and field age is unreadable. `u_set_field` calls `add_field`, which adds to the existing intensity and returns early for intensity <= 0, so EOCs cannot set an exact intensity or weaken a field; only full removal through a `ter_furn_transform` works. The docs describe `age` as "how long the field would last", but it is the starting age, and a larger age makes the field decay sooner. Terrain and furniture have no talker (`talker_furniture` wraps only `computer`) and no per-tile variables, so stateful furniture has to fake its state with global vars keyed by coordinates.

```cpp
{ "field_strength", { "ung", 1, field_strength_eval, {}, { "location" } } },   // math_parser_diag.cpp:1911, no assign
here.add_field( dest, field, dov_intensity.evaluate( d ), dov_age.evaluate( d ), hit_player ); // npctalk.cpp:8207
it->second.set_field_intensity( prev_intensity + new_intensity );              // field.cpp:192
```

**Fix:** Add an assignable math function `map_radiation( loc )` (`map_radiation(loc) = 0`, `+= 50`). Make `field_strength('fd_fire', 'location': loc) = 0` assignable: it sets the intensity exactly and removes the field at 0. Add read-only `field_age('fd_smoke', 'location': loc)`. Add `"mode": "add|set|max"` to `u_set_field` and correct its doc text. Add tile variables backed by submap cosmetics, cleared when the furniture changes (like signage): `{ "set_tile_var": "charges", "loc": ..., "value": 3 }` and `tile_var('charges', 'location': loc)`.

### MAP-J07 · Emission math always runs with the player as `u`, whoever emits
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/map_field.cpp:2173`, `src/emit.h:67`, `src/emit.cpp:45`, `src/monster.cpp:3010`, `src/npctalk.cpp:8231`

`emit_type` loads `chance`, `qty` and `intensity` as `dbl_or_var`, so math is accepted, but `map::emit_field` always builds its dialogue from the avatar. Emissions from monsters, bionics, items, vehicles, furniture and terrain evaluate `u_` against the player, possibly across the map, and no context var gives the emitter's location. Callers pass only a position; even the EOC `emit` effect (`npctalk.cpp:8231`) drops its own dialogue. Downgraded because the docs present emit fields as plain numbers and no vanilla data uses math there.

```cpp
dialogue d( get_talker_for( get_avatar() ), nullptr );   // map_field.cpp:2173
const float chance = src->chance( d ) * mul;
```

**Fix:** Add an optional `const_talker *source` (or `dialogue *`) parameter to `emit_field` and pass the monster, character, item or vehicle talker from each caller. Always set context var `emit_loc` (tripoint_abs_ms). Document the alpha talker in the emit docs.

### MAP-J08 · `make_rubble` "items" spawns a hardcoded loot list for three rubble ids, unlike MAPGEN.md
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/map.cpp:3742`, `src/map.cpp:3721`, `src/map.h:1226`, `doc/JSON/MAPGEN.md:1062`

MAPGEN.md documents `place_rubble` `items` as "place items that result from bashing the structure". Bash drops already happen through `destroy()`/`destroy_furn()` whatever `items` is set to. The flag only adds hardcoded loot for `f_wreckage`, `f_rubble_rock` and `f_rubble`; any other `rubble_type`, such as a mod's `f_rubble_ice`, gets nothing extra. The `map.h` convenience overloads also hardcode `t_dirt`/`f_rubble` by string.

```cpp
//Still hardcoded, but a step up from the old stuff due to being in only one place   // :3742
if( rubble_type == furn_f_wreckage ) {
```

**Fix:** Add a furniture field `"rubble_items": "<item_group>"` (or reuse the rubble furniture's `deconstruct.items`/`bash.items`), move the three vanilla lists into item groups, and correct the MAPGEN.md wording.

### MAP-J09 · Terrain phase-change fields are undocumented and unvalidated; JSON_INFO documents fields nobody loads
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mapdata.cpp:1387`, `src/mapdata.cpp:1418`, `src/mapdata.cpp:1532`, `src/mapdata.h:716`, `src/mapgen.cpp:6472`, `doc/JSON/JSON_INFO.md:3501`

`ter_t` loads `phase_targets`, `phase_temps` and `phase_method` (temperature-driven freezing used by `terrain-liquids.json`), but nothing in `doc/` mentions them. `phase_method` is a free string; only `"water_freeze"` is implemented and any other value silently does nothing. `phase_targets` ids are never validated in `ter_t::check()`. The `mapdata.h` comment lists methods (`"thresholds"`, `"closest"`, `"gradient"`) that don't exist. The arrays are appended on copy-from, so a child that redefines them doubles the arrays and trips the length-mismatch error (`mapdata.cpp:1422`). Conversely, JSON_INFO.md's terrain example documents `"alias"` and `"harvestable"`, which no terrain loader reads.

```cpp
phase_targets.emplace_back( v.get_string() );                  // mapdata.cpp:1391, no clear on copy-from
if( cur_ter.phase_method == "water_freeze" ) { ... } else { return; }   // mapgen.cpp:6472
```

**Fix:** Read `phase_method` as an enum (error on unknown values), validate `phase_targets` in `ter_t::check()`, and clear both arrays when the key is present. Document the three fields in JSON_INFO.md, remove `alias`/`harvestable` from the example, and fix the header comment.

### MAP-J10 · Physical behaviours ignore existing JSON fields (furniture mass, coverage, furniture heat) or use fixed constants
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/map.cpp:3216-3231`, `src/map.cpp:3014-3027`, `src/map.cpp:3048-3051`, `src/map.cpp:5133`, `src/mapdata.cpp:1369`, `src/mapdata.cpp:1620`, `src/game.cpp:1663`, `src/character.cpp:8235`, `src/submap.cpp:486`, `src/mapgen.cpp:3241`

1. `drop_furniture` estimates falling-furniture damage from `move_str_req` (or 20) instead of `mass`. Switching needs data work first: `mass` defaults to 1000 kg and only about 174 of 713 furniture set it.
2. `ranged_target_size` returns a flat 0.1 for any passable tile (`// TODO: Handle cases like shrubs, trees, furniture, sandbags...`) and ignores `coverage`.
3. `heat_radiation` is in `map_data_common_t` but loaded only by `ter_t::load` and read only from terrain, so furniture cannot radiate heat directly. Hot-air `emissions` are a workaround.
4. Roof-collapse damage (`rng(0,40)` over hardcoded body parts, monsters `rng(0,25)`, vehicles `rng(100,1000)`) is fixed. The hardcoded climb difficulties 7 and 10 are effectively dead, because `climbing_cost` returns 0 for any difficulty above 5, and the `LADDER`/`CLIMBABLE`/`climbing_aid` JSON already covers climbing.
5. `merge_submaps` copies computer data only for `f_console`. Every computer placement currently forces `f_console`, so this is latent.

```cpp
weight = frn_obj.is_movable() ? frn_obj.move_str_req : 20;   // map.cpp:3222
// TODO: Handle cases like shrubs, trees, furniture, sandbags...
return 0.1;                                                    // map.cpp:3026
const int dam = rng( 0, 40 );                                  // map.cpp:5133
```

**Fix:** Use `mass` in `drop_furniture` once furniture data sets it. Derive `ranged_target_size` from `max(ter.coverage, furn.coverage) / 100`. Load `heat_radiation` in `map_data_common_t::load` and sum terrain and furniture in `get_heat_radiation`. Add a bash/roof `"collapse_damage": [ min, max ]`. Test the `CONSOLE` flag instead of the `f_console` id.

## 2. Bugs, oversights & quirks

### MAP-B01 · Wind-blocker table uses math angles (0=E, CCW) while the game's winds are compass bearings (0=N, CW)
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/map_field.cpp:2134`, `src/map_field.cpp:2153`, `src/map_field.cpp:339`, `src/map_field.cpp:1168`, `src/weather.cpp:871`, `src/weather.cpp:740`, `src/weather.cpp:765`, `src/vehicle.cpp:1171`

`get_wind_blockers()` returns the three upwind neighbour tiles that fire and gas must not spread into. Everywhere else, `winddirection` is a compass bearing for where the wind comes from: `get_dirstring(0)` is "North", and `convert_wind_to_coord(0)` is `(0,-1)`; the vehicle sail code agrees. The blocker table instead maps 0° to EAST, 60° to NORTH, 150° to WEST, 240° to SOUTH, 121-149° to NW and 301-329° to SE. That reflects it across the NE-SW diagonal: N/E/S/W winds block the wrong side (90° off), NW/SE winds block the downwind side (180° off), and only NE/SW winds are right. In a north wind of 5 or more, outdoor fire and tear gas are kept from spreading east, and drift north into the wind is unrestricted. In addition, the fire test (1168-1171) ORs the three per-blocker conditions, so in diagonal winds only the single diagonal tile is blocked; the gas test (351-353) blocks all three.

```cpp
// get_wind_blockers
{ 330, east, NE, SE }, { 240, south, ... }, { 150, west, ... }, { 60, north, ... }, { 0, east, NE, SE }
// convert_wind_to_coord (weather.cpp)
{ 330, (0,-1) }, { 240, (-1,0) }, { 150, (0,1) }, { 60, (1,0) }, { 0, (0,-1) }
```

**Fix:** Build the blockers from `convert_wind_to_coord( winddirection )`: the upwind offset plus its two 45° neighbours. Use the gas-style tile-equality test for fire (`neigh.pos_ != b1 && != b2 && != b3`). Add a unit test that, for each of the 8 sectors, the blocked set contains `convert_wind_to_coord()`.

### MAP-B02 · `submap::merge_submaps` gives two `unique_ptr`s ownership of each overlay vehicle, then frees them
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/submap.cpp:471`, `src/submap.cpp:472`, `src/submap.cpp:475`, `src/mapgen.cpp:276`, `src/mapgen.cpp:2749`

During multi-z mapgen, content one z-level writes into another is saved as an overlay submap and merged later. For each overlay vehicle the code builds a second `unique_ptr` from the raw pointer, then calls `copy_from->vehicles.clear()`, which deletes the originals. The comment assumes the opposite. The target submap is left with dangling pointers: use-after-free the next time the vehicle is touched, and a double delete when the submap is destroyed. The trigger is a mapgen chunk placed at a negative z offset (as in `reverberations/cabins_nested.json`) that places a vehicle into a not-yet-generated, non-uniform lower submap. Vanilla does not do this today, so the bug is latent, but any modder can hit it.

```cpp
for( const auto &vehicle : copy_from->vehicles ) {
    this->vehicles.emplace_back( vehicle.get() );
}
// Can't let that submap delete the vehicles when it's destroyed
copy_from->vehicles.clear();
```

**Fix:** Move ownership: `for( auto &v : copy_from->vehicles ) { this->vehicles.emplace_back( std::move( v ) ); } copy_from->vehicles.clear();` and update each vehicle's `sm_pos`. Also guard `lum +=` against uint8 overflow.

### MAP-B03 · Rain washing of fields is a no-op: `decay_fields_and_scent` ages a copy of each `field_entry`
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/map.cpp:4005`, `src/map.cpp:4007`, `src/map.cpp:4012`, `src/weather.cpp:474`, `src/field_type.cpp:303`, `src/field_type.h:205`, `src/do_turn.cpp:767`, `doc/JSON/JSON_INFO.md:4468`

`handle_weather_effects()` calls `map::decay_fields_and_scent()` every turn it rains, so outdoor fields age faster by `amount / decay_amount_factor`. The loop copies each entry and ages the copy, so nothing on the map changes. 57 field types, including `fd_fire`, `fd_smoke` and `fd_blood`, set `decay_amount_factor`, and JSON_INFO.md documents it, but it has no effect: rain never puts out fires, clears smoke or washes away blood. The loop also costs a field walk every rainy turn for nothing. Even after a fix, the function only handles `abs_sub.z()` (see the TODOs at 3961/3967).

```cpp
for( const auto &fp : fields ) {
    to_proc--;
    field_entry cur = fp.second;                                // copy
    ...
    cur.set_field_age( cur.get_field_age() + decay_amount );   // modifies the copy only
}
```

**Fix:** Iterate the field through the non-const `submap::get_field` and take `field_entry &cur`; call `on_field_modified` when intensity changes. Skip submaps using the `field_cache` bits, and process every z-level with fields using that level's `outside_cache`.

### MAP-B04 · `field_type` copy-from appends `intensity_levels`; per-level inheritance, `concentration` and `npc_complain` broken; weak validation
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/field_type.cpp:209`, `src/field_type.cpp:210`, `src/field_type.cpp:213`, `src/field_type.cpp:261`, `src/field_type.cpp:277`, `src/field_type.cpp:283`, `src/field_type.cpp:285`, `src/field_type.cpp:291`, `src/field_type.cpp:295`, `src/field_type.cpp:377`, `src/mapdata.cpp:1387`, `src/mapdata.cpp:1398`, `src/mapdata.cpp:1422`, `src/mapdata.cpp:1499`

`field_type::load` has several problems:
1. It pushes every JSON intensity level onto the inherited vector without clearing it. A copy-from that redefines a 3-level field yields 6 levels, max intensity 6, with the parent's levels first.
2. With `was_loaded` true, `optional( jao, was_loaded, ..., fallback )` skips the "inherit from previous level" fallback, so missing keys get struct defaults.
3. `concentration` defaults to the literal 1 at every level instead of the previous level, contrary to JSON_INFO's "each intensity level inherits its properties from the lower one". In shipped data, `fd_gas_vent`/`fd_smoke_vent` drop from 4 to 1 at level 2. This only affects the gas-mask cartridge estimate.
4. In `npc_complain`, `int chance;` is uninitialised, so a partial copy-from override reads garbage.
5. `check()` validates only level names. Effect ids, body parts, monster spawn groups and the field's `bash` block are never checked, because `map_fd_bash_info::check` has no caller.

The terrain `phase_targets`/`phase_temps` loader has the same append-without-clear pattern (see MAP-J09). No shipped data triggers these, but a mod doing `{ "id": "fd_smoke", "copy-from": "fd_smoke", "intensity_levels": [...] }` to retune smoke would.

```cpp
for( const JsonObject jao : jo.get_array( "intensity_levels" ) ) { ... intensity_levels.emplace_back( intensity_level ); }
int chance; ... optional( joc, was_loaded, "chance", chance, 0 );
optional( jao, was_loaded, "concentration", intensity_level.concentration, 1 );
```

**Fix:** Clear `intensity_levels` when the key is present (or support `extend`/`delete`), and pass `false` as `was_loaded` for the per-level reads so the fallback always applies. Default `concentration` to `fallback_intensity_level.concentration`. Initialise the `npc_complain` locals from the existing tuple. In `check()`, validate effect ids, body parts, spawn groups and immune/block mtypes, and call `bash_info->check( id.str() )`.

### MAP-B05 · Ter/furn copy-from drops oxytorch/boltcut/hacksaw/prying; partial `shoot` and bash overrides misbehave
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mapdata.cpp:1427`, `src/mapdata.cpp:1442`, `src/mapdata.cpp:1637`, `src/mapdata.cpp:1652`, `src/mapdata.cpp:1340`, `src/mapdata.cpp:433`, `src/mapdata.cpp:535`, `src/mapdata.cpp:545`, `src/map.cpp:5227`

`ter_t::load` and `furn_t::load` replace the four tool-activity blocks with a fresh `make_value` on every load, so copy-from never inherits them; `bash` and `deconstruct` do inherit. There is no way to delete them on purpose either. A scan of shipped data finds 9 children that lose a parent block, e.g. `t_window_boarded_shutters_closed` loses prying, `t_nl_metal_wall_border` loses oxytorch, and 6 aftershock hull walls lose `t_wall_metal`'s oxytorch. Whether each loss is intended is unknown. `shoot` is rebuilt whenever present and loaded with `was_loaded=true`, so a child that writes only `"shoot": { "chance_to_hit": 50 }` gets `reduce_damage` and `destroy_damage` of `{0,0}`. Then `x_in_y( dmg, 0 )` in `shoot_furn_ter` returns true and every hit destroys the tile. Finally, `ter_set_bashed_from_above` defaults to `ter_set` only on first load, so a child overriding `bash.ter_set` keeps the parent's from-above result. No shipped data uses a partial `shoot`.

```cpp
oxytorch = cata::make_value<activity_data_ter>();
if( jo.has_object( "oxytorch" ) ) { //TODO: ...
shoot = cata::make_value<map_shoot_info>(); shoot->load( jo, "shoot", was_loaded );
mandatory( j, was_loaded, "reduce_damage", reduce_damage );   // local pair stays {0,0}
```

**Fix:** Replace these blocks only when the key is present (`if( jo.has_object("prying") ) { if( !prying ) prying = make_value<...>(); prying->load( obj, was_loaded ); }`), and pass `was_loaded` through so partial overrides work. Support `"delete": { "prying": true }` for intentional removal. For `shoot`, keep the existing value on copy-from and initialise the local pairs from the current members. Resolve `ter_set_bashed_from_above` at finalize time when it was not set explicitly.

### MAP-B06 · `smash_items`: a wheel "miss" re-rolls the same item until it hits; ejected items only move SE
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/map.cpp:4311`, `src/map.cpp:4313`, `src/map.cpp:4379`, `src/map.cpp:4382`, `src/vehicle_move.cpp:1301`

When a wheel misses an item, the code `continue`s without advancing the iterator, so the same item is re-rolled until `hit_probability` passes (it is always above 0 here). Every item is hit: wheel damage, salvage at 10% efficiency, corpses gibbed. Driving over a pile shreds all of it regardless of size. The eject branch uses `rng(0,1), rng(0,1)`, so items only fly E, S or SE. If the destination is out of bounds, `add_item` returns null but the item is still removed, so it is silently lost; NOITEM tiles and walls are not checked.

```cpp
if( vp_wheel != nullptr && vehicle::hit_probability( *i, vp_wheel ) < rng_float( 0.0, 1.0 ) ) {
    // The wheel missed the item.
    continue;      // i not advanced
}
point_rel_ms move_to( rng( 0, 1 ), rng( 0, 1 ) );
```

**Fix:** `++i; continue;` on a miss. Use `rng(-1,1)` for the eject direction, and place ejected items with `add_item_or_charges` after checking the destination accepts items; keep the item if it does not.

### MAP-B07 · `map::add_item(copies > 1)` registers only the first copy as active and counts its light once
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/map.cpp:6139`, `src/map.cpp:6144`, `src/map.cpp:6149`, `src/crafting.cpp:866`, `src/crafting.cpp:3704`, `src/character_inventory.cpp:501`

`add_item` inserts `copies` identical items, but calls `update_lum_add` and `active_items.add` only for the first. Copies 2..N are never in the active item cache, so they get no temperature or rot processing until picked up or until the submap reloads, and their light is not counted. `spawn_items` groups identical non-charge results whenever a location is set, so any batch craft placed at a workbench hits this: craft 10 hot meals and nine stay "hot" indefinitely. `Character::i_add` overflow drops take the same path.

```cpp
current_submap->update_lum_add( l, new_item );
const map_stack::iterator new_pos = current_submap->get_items( l ).insert( new_item );
while( --copies > 0 ) { current_submap->get_items( l ).insert( new_item ); }
if( current_submap->active_items.add( *new_pos, l ) ) { ... }
```

**Fix:** Capture each inserted iterator and call `update_lum_add` and `active_items.add` for every copy.

### MAP-B08 · `mapbuffer::unserialize_submaps` inserts a null entry, which leads to a null dereference in `loadn`
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mapbuffer.cpp:419`, `src/mapbuffer.cpp:424`, `src/mapbuffer.cpp:131`, `src/mapbuffer.cpp:83`, `src/mapgen.cpp:193`, `src/mapgen.cpp:206`, `src/map.cpp:9425`, `src/map.cpp:9446`, `src/map.cpp:9448`, `src/map.cpp:11305`

If a quad file exists but lacks the requested submap (partial or corrupted save, or an OMT that became non-uniform after a mod update), `unserialize_submaps` prints a debugmsg and returns `submaps[ p ].get()`, which inserts a null `unique_ptr`. From then on `submap_exists(p)` returns true: `generate` treats the submap as already generated and never draws it, and `add_submap` refuses the key. Back in `loadn`, `lookup_submap` returns null, `setsubmap` rejects it with a debugmsg, and then `tmpsub->active_items` dereferences null and crashes. This needs a damaged save, which the existing debugmsg anticipates.

```cpp
if( submaps.count( p ) == 0 ) { debugmsg( "file %s did not contain the expected submap ..." ); }
return submaps[ p ].get();   // inserts nullptr
...
tmpsub = MAPBUFFER.lookup_submap( pos );
setsubmap( ..., tmpsub );
if( !tmpsub->active_items.empty() )
```

**Fix:** Use `find()` and return `nullptr` without inserting. Make `submap_exists` require a non-null value. In `loadn`, treat a null lookup as incomplete and regenerate.

### MAP-B09 · Monsters spawned by fields and rotting items wait as static spawns until the next map shift; spawn counts are halved
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/map_field.cpp:788`, `src/map_field.cpp:801`, `src/map.cpp:9575`, `src/mapgen.cpp:6829`, `src/map.cpp:10296`, `src/map.cpp:10204`, `src/game.cpp:10348`, `src/mongroup.cpp:241`

`field_processor_monster_spawn` (the `monster_spawn_*` JSON on `fd_tindalos_rift`, `fd_fatigue`, `fd_meadow_portal`, `fd_portal_cracked_mirror`) and `rotten_item_spawn` both call `map::add_spawn`, which only appends to `submap::spawns`. Those spawns become monsters only in `spawn_monsters_submap()`, which runs on game start, teleport/travel and the `update_map` shift. Nothing realises them per turn. A player who sleeps 8 hours beside a reality tear (1/600 per turn) wakes to about 48 queued monsters, all released on the first step across a submap edge. Separately, `GetResultFromGroup` decrements the count and the `for` loop decrements it again, so `monster_spawn_count: 2` (the documented example) spawns 1. `spawn_monsters_submap_group` does the same (`for( m = 0; m < pop; m++ )`), halving group populations.

```cpp
for( ; monster_spawn_count > 0; monster_spawn_count-- ) {
    ... GetResultFromGroup( int_level.monster_spawn_group, &monster_spawn_count );
    ... pd.here.add_spawn( mgr, pt );
```

**Fix:** For in-bubble runtime spawns, place the monster immediately with `g->place_critter_around`/`place_critter_within`, or flush the current bubble's submap spawns once per turn. Change the loops to `while( count > 0 ) GetResultFromGroup( ..., &count )`. Fixing the group-population halving doubles overmap spawns, so it needs a balance review.

### MAP-B10 · Phase-change ice (fork feature) freezes permanently after a furniture edit or bash, and only updates on load
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/map.cpp:2110`, `src/map.cpp:2626`, `src/map.cpp:2647`, `src/map.cpp:4905`, `src/map.cpp:9497`, `src/map.cpp:9975`, `src/mapgen.cpp:6458`, `src/mapgen.cpp:6467`

Water freezes to ice in `temp_based_phase_change_at()`, which runs only from `map::loadn` and mapgen. It reverts to the recorded `original_terrain` and re-evaluates. `furn_set()` always calls `clear_original_terrain_at(p)`, and `map::bash` clears it whenever `bashed_solid`, which includes bashing a vehicle or a web on the tile. The ice terrains have no `phase_method`, so once the record is gone the function returns early and the tile stays ice forever, even in summer. Triggers: build furniture on a frozen pond tile, or let zombies smash a car parked on ice. Because the check runs only on load, a player who stays near a lake keeps whatever state it had at load time. It also goes through `ter_set` outside mapgen, so every frozen tile sets `player_adjusted_map`, resets map damage and pushes duplicate trap locations on every load (actualize pushes them again).

```cpp
// furn_set
current_submap->set_furn( l, new_target_furniture ); ... clear_original_terrain_at( p );
// bash
if( bsh.did_bash && bsh.bashed_solid ) { clear_original_terrain_at( p ); }
// mapgen.cpp:6467
if( cur_ter.phase_method.empty() && !has_original_terrain_at( p ) ) { return; }
```

**Fix:** Clear `original_terrain` only when the terrain itself changes away from the phase target, not on furniture edits or vehicle/field bashes. Re-evaluate phase state periodically for in-bubble submaps (e.g. on a daily timer). Apply the revert/refreeze with direct submap writes plus explicit cache invalidation instead of `ter_set`.

### MAP-B11 · Acid vents never emit acid
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/map_field.cpp:906`, `src/map_field.cpp:907`, `src/map_field.cpp:1727`, `src/field.cpp:65`, `src/map.cpp:7462`, `src/mapgen.cpp:7433`

`field_processor_fd_acid_vent` adds acid only when `acid && acid->get_field_intensity() == 0`. `get_field()` returns only live entries and `set_field_intensity` clamps intensity to at least 1, so this is never true. An acid vent (from `create_anomaly` or an artifact lab with ARTPROP_SLIMY) cycles its intensity forever and does nothing. Separately, `player_in_field` removes shock and acid vents when stepped on; the comment at 1726 says this is intended, though another comment asks "Why do these get removed???".

```cpp
const field_entry *acid = pd.here.get_field( t, fd_acid );
if( acid && acid->get_field_intensity() == 0 ) {   // never true
```

**Fix:** Use `if( !acid )` (or `!acid || acid->get_field_intensity() < new_intensity`). Optionally move vent removal to a JSON property alongside `decrease_intensity_on_contact`.

### MAP-B12 · Ter/furn `extend`/`delete` of `connects_to` and `rotates_to` edit `connect_groups` instead (affects No_Hope)
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mapdata.cpp:1289`, `src/mapdata.cpp:1292`, `src/mapdata.cpp:1304`, `src/mapdata.cpp:1307`, `src/mapdata.cpp:1071`, `data/mods/No_Hope/terrain.json:13`, `data/mods/No_Hope/terrain.json:45`

In `map_data_common_t::load`, the `extend` and `delete` branches call `set_connect_groups`/`unset_connect_groups` for all three keys. `extend: { connects_to: [X] }` therefore adds the tile to group X instead of making it connect to X, and `delete: { connects_to: [WALL] }` removes the tile from the WALL group so its neighbours stop connecting to it. The correct setters (`mapdata.cpp:1071-1089`) exist and the non-extend path uses them. No_Hope's `t_carpet_red` and `t_rock_floor` use `"extend": { "connect_groups": [ "RAIL" ], "connects_to": [ "RAIL" ] }`, and the `connects_to` part does nothing. The effect is visual only: the active groups are read by tile rendering and wall-symbol selection.

```cpp
if( joe.has_member( "connects_to" ) ) { set_connect_groups( joe.get_as_string_array( "connects_to" ) ); }
if( joe.has_member( "rotates_to" ) ) { set_connect_groups( joe.get_as_string_array( "rotates_to" ) ); }
```

**Fix:** Call `set_connects_to`/`set_rotates_to` in `extend` and `unset_connects_to`/`unset_rotates_to` in `delete`. Add a test that extend/delete on each key changes only its own bitset.

### MAP-B13 · Windy fire spread checks flammable items on the wrong tile
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/map_field.cpp:1404`, `src/map_field.cpp:1371`, `src/map_field.cpp:1344`, `src/map_field.cpp:1164`, `src/map_field.cpp:224`

In the windy branch of `field_processor_fd_fire`, `i` indexes `neighbour_vec`, a filtered list starting at a random rotation, but the item-pile check uses `p + eight_horizontal_neighbors[i]`, which is usually a different tile from `dst_p`. The sheltered branch is correct. Whether fire jumps onto an item pile in wind therefore depends on another tile's contents. The effect is limited to a `one_in(5)` sub-condition.

```cpp
tripoint_bub_ms &dst_p = neighs[neighbour_vec[i]].first;
...
( one_in( 5 ) && dst.get_item_count() > 0 &&
  here.flammable_items_at( p + eight_horizontal_neighbors[i] ) )
```

**Fix:** Use `here.flammable_items_at( dst_p )`, and merge the two near-identical spread loops into one helper.

### MAP-B14 · `drop_items`: fall damage hits the original item after it was copied below; possible divide by zero
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/map.cpp:3338`, `src/map.cpp:3385`, `src/map.cpp:3387`, `src/map.cpp:3393`, `src/map.cpp:3353`, `src/map.cpp:3317`

Items over a floorless tile are copied to the landing tile with `add_item_or_charges( below, i )` (by value). Only afterwards is `i.inc_damage()` rolled on the original, which `i_clear( p )` then destroys, so falling items never take damage. When `damage` is 0 (a 0 g item, or a huge creature below that cancels the fall height), `200 * resist / damage` divides by zero, and `creature_hit_chance /= sqrt( avoid_chance )` with `avoid_chance <= 0` converts inf/NaN to int, which is undefined behaviour (INT_MIN on x86 rather than a crash).

```cpp
add_item_or_charges( below, i );
...
int chance = static_cast<int>( 200 * i.resist( damage_bash, true ) / damage + 1 );
if( one_in( chance ) ) { i.inc_damage(); }   // original, cleared below
i_clear( p );
```

**Fix:** Damage a copy before placing it (`item fallen = i; if( one_in(...) ) fallen.inc_damage(); add_item_or_charges( below, fallen );`), and guard on `damage > 0` and `avoid_chance > 0`.

### MAP-B15 · Emitter and trap location lists only grow: `ter_set`/`furn_set` append duplicates and never remove stale entries
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/map.cpp:2155`, `src/map.cpp:2647`, `src/map.cpp:2650`, `src/map.cpp:9963`, `src/map.cpp:9975`, `src/map.cpp:9984`, `src/mapgen.cpp:6540`, `src/map_field.cpp:372`, `src/weather.cpp:339`

`furn_set` and `ter_set` push `p` onto `field_furn_locs`, `field_ter_locs` and `traplocs` whenever a tile becomes an emitter or trap terrain, without deduplicating and without removing the entry when the tile changes away. `furniture_terrain_emit_fields()` emits once per entry, so a tile that toggles states emits more each cycle; an apiary restocked by actualize gets duplicate entries. Phase change plus actualize in `loadn` gives each ice tile two trap entries. A deconstructed `t_gutter_downspout` keeps acting as a funnel because `fill_funnels` never re-checks the trap. Growth is bounded per load (`shift_traps` prunes, `map::load` clears), so the practical effect is 2-3x emitter rates and stale funnels.

```cpp
// furn_set
if( !new_f.emissions.empty() ) { field_furn_locs.push_back( p ); }
// ter_set
if( new_t.trap != tr_null ) { traplocs[new_t.trap.to_i()].push_back( p ); }
if( !new_t.emissions.empty() ) { field_ter_locs.push_back( p ); }
```

**Fix:** In `ter_set`/`furn_set`, remove `p` from the old type's list and push only if not already present, or make these per-submap sets rebuilt in actualize.

### MAP-B16 · `monster_in_field`: FIREPROOF check for flame burst/incendiary uses `return`, skipping later fields on the tile
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/map_field.cpp:2035`, `src/map_field.cpp:2067`, `src/map_field.cpp:1916`

For `fd_fire` the FIREPROOF/FIREY check uses `continue`; for `fd_flame_burst` and `fd_incendiary` it uses `return`, which leaves the per-field loop. A fireproof monster in flame burst skips electricity, fatigue, dazzle, relax gas, fungal haze, fungicidal and insecticidal gas if present on the same tile; in incendiary it skips relax gas, fungal haze, fungicidal and insecticidal gas. Fields that sort earlier (acid, tear gas) and generic JSON effects still apply.

```cpp
if( cur_field_type == fd_flame_burst ) {
    if( z.has_flag( mon_flag_FIREPROOF ) || z.has_flag( mon_flag_FIREY ) ) {
        return;
    }
```

**Fix:** Replace both `return;` with `continue;`.

### MAP-B17 · Minor field-processor slips: wandering fields saturate to max; push_items hits NPCs with the avatar's body part
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/map_field.cpp:636`, `src/map_field.cpp:639`, `src/map_field.cpp:839`

`field_processor_wandering_field` (gas, smoke and tindalos vents) means to add a field or bump a weaker one by 1. When an equal or stronger field exists it falls into the `else` and calls `add_field`, which adds intensity, so every tile in the vent's radius climbs to max intensity whatever the vent's strength. In `fd_push_items`, an NPC hit by a flying rock takes damage on `pd.player_character.get_random_body_part()` rather than its own.

```cpp
if( tmpfld && tmpfld->get_field_intensity() < cur.get_field_intensity() ) { mod_field_intensity( pnt, type, 1 ); }
else { pd.here.add_field( pnt, wandering_field_type_id, cur.get_field_intensity() ); }
const bodypart_id hit = pd.player_character.get_random_body_part();
```

**Fix:** `if( !tmpfld ) add_field(...); else if( tmpfld->intensity < cur ) mod +1;`. Use `n->get_random_body_part()`.

### MAP-B18 · Field processing has a scan-order bias; gas spreads horizontally with probability 1/N
**Severity:** Low · **Effort:** Medium · **Verified:** plausible
**Where:** `src/map_field.cpp:497`, `src/map_field.cpp:1348`, `src/map_field.cpp:769`, `src/map_field.cpp:264`, `src/map_field.cpp:337`, `src/map_field.cpp:164`

`process_fields_in_submap` skips a newborn field only when its age is exactly 0. Fire spread creates fires aged 2 minutes, electricity spawns age+1, and gas spread passes on a share of the age, so a field created later in the x/y scan order can be processed again in the same turn. Fire on webs and electricity arcs could then travel several tiles per turn toward +x/+y but only one toward -x/-y. Separately, `spread_gas` spreads horizontally only `one_in( spread.size() )`, so gas in an open room (8 neighbours) spreads sideways 12.5% of the time versus 50% in a corridor. The gameplay effect is not proven: new gas fields have intensity 1 and bail out early, and the 1/N rule may be intended dilution.

```cpp
if( cur.get_field_age() == 0_turns ) { cur.do_decay(); ... continue; }
here.add_field( dst_p, fd_fire, 1, 2_minutes, true, ... )
if( !spread.empty() && one_in( spread.size() ) ) {
```

**Fix:** Mark fields created during the pass with a turn stamp (or process from a snapshot) instead of relying on age 0. Replace `one_in(spread.size())` with a JSON-tunable horizontal/vertical spread chance (which also covers the heavy/light gas TODO at 311).

## 3. Performance

### MAP-P01 · Every map shift and tinymap load re-walks all bubble items plus every character inventory
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/map.cpp:9290`, `src/map.cpp:9004`, `src/map.cpp:9008`, `src/map.cpp:9050`, `src/map.cpp:9077`, `src/map.cpp:9086`, `src/item_wakeup.cpp:347`, `src/item_wakeup.cpp:239`

`map::shift` runs each time the avatar crosses a submap boundary: every 12 tiles on foot, several times per turn when driving. Each time it calls `reconcile_loaded_items(additive)`, which walks all 11x11x21 submaps x 144 tiles, builds an `item_location` for every top-level and nested item and calls `item_wakeup_manager::rebuild_for_item` on each. That call does a vector allocation and four `std::map` erases even when nothing is scheduled. The pass then walks every vehicle's cargo, the avatar's items and every NPC's items. Only the newly loaded column changed, and inventories cannot change during a shift. `map::load` runs the same pass for every tinymap/smallmap load. In a town bubble with tens of thousands of items this is a multi-millisecond stall per crossing.

```cpp
if( !loaded_grids.empty() ) { ... reconcile_loaded_items( reconcile_scope::additive ); }   // :9290
item_location( cur, &it ).visit_items( reconcile_recursive );                              // per item
for( wrapped_vehicle &wv : get_vehicles() )                                                // :9077
get_avatar().visit_carried( reconcile_recursive ); for( npc &n : g->all_npcs() ) { ... }   // :9086
```

**Fix:** Pass `reconcile_loaded_items` the list of grids to walk (`map::shift` already has `loaded_grids`; `map::load` passes its own) plus vehicles whose `sm_pos` lies in them. Walk character inventories only for a full rebuild of the main map (game load, teleport, dimension change).

### MAP-P02 · Temporary maps allocate 1.46 MB of pathfinding caches and memset ~1.7 MB level caches per z-level
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/map.cpp:363`, `src/map.cpp:389`, `src/map.cpp:488`, `src/map.cpp:2632`, `src/map.cpp:2686`, `src/map.cpp:9417`, `src/map.cpp:9441`, `src/map.h:2432`, `src/level_cache.cpp:16`, `src/lightmap.cpp:1161`

The map constructor eagerly creates 21 `pathfinding_cache` objects of about 70 KB each (about 1.46 MB per map), including every temporary tinymap and smallmap; `loadn` builds a smallmap per newly generated OMT. The dirty setters call `get_cache(z)`, which lazily takes a level cache from the pool and memsets about 1.6 MB through `level_cache::clear()`. `loadn` dirties all 21 levels, and `set_lightmap_cache_dirty_below()` walks from z down to -10, so a single transparency change during mapgen clears up to 11 levels. `ter_set`/`furn_set` also call `avatar.sees(*this, p)` on the temporary map, which goes through `ensure_light` → `set_sunlight_only` and fills more cache. A freshly allocated level cache is already dirty, so this work is not needed.

```cpp
for( auto &ptr : pathfinding_caches ) { ptr = std::make_unique<pathfinding_cache>(); }   // :363
for( int z = zlev; z >= -OVERMAP_DEPTH; z-- ) { if( inbounds_z( z ) ) { get_cache( z ).lightmap_dirty = true; // :490
if( player_character.sees( *this, p ) )                                                  // :2686, runs during mapgen
```

**Fix:** Allocate pathfinding caches lazily in `get_pathfinding_cache`. Make the `set_*_dirty` helpers use `get_cache_lazy` and skip levels with no cache; have `level_cache::clear()` set `floor_cache_dirty = true` so later allocation is complete. In `ter_set`/`furn_set`, skip map-memory, `sees`, reachability, pathfinding and support bookkeeping when `mapgen_in_progress` or `this != &reality_bubble()`. Expected: several milliseconds and tens of MB of memory traffic saved per generated OMT and tinymap load.

### MAP-P03 · Compressed-world save/load reopens the segment zzip per quad, even for uniform quads and missing files
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/mapbuffer.cpp:256`, `src/mapbuffer.cpp:282`, `src/mapbuffer.cpp:354`, `src/mapbuffer.cpp:383`, `src/mapbuffer.cpp:134`, `src/mmap_file.cpp:322`, `src/map.cpp:9408`, `src/mapgen.cpp:191`, `src/options.cpp:1876`

Compression is on by default (`WORLD_COMPRESSION2`). On save, `save_quad` opens the segment zzip (mmap plus an XXH64 hash of the footer) before it checks whether the quad is all-uniform, and uniform quads (sky, rock) are the large majority and return without writing. The file is opened with `O_CREAT`, so every touched segment at every z-level gets a `.zzip`. On load, `unserialize_submaps` reopens the zzip on every lookup miss, and there is no negative cache (the comment at 134 suggests adding one). `map::generate` probes all 84 submaps of an OMT, so after a reload each OMT entering the bubble costs roughly 75-85 zzip opens. Each written quad also calls `compact_to(..., 2.0)`, though that only rewrites when the file exceeds twice its content.

```cpp
if( ...has_compression_enabled() ) { z = zzip::load( zzip_name..., maps.dict ); ... }  // :256, before uniform check
if( all_uniform ) { ... if( !reverted_to_uniform ) { return; } }                        // :282-296
std::optional<zzip> z = zzip::load( ... );                                              // :383, every miss
```

**Fix:** In `save_quad`, compute `all_uniform`/reverted first and open the zzip only when something must be written or deleted. In `mapbuffer::save`, group quads by segment, open each zzip once and compact once. On load, keep a small LRU of open zzip handles per segment and an `unordered_set<tripoint_abs_omt>` of quads known to be absent, updated in `save_quad`.

### MAP-P04 · Every field add or intensity change dirties the whole z-level lightmap and visibility cache
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/map.cpp:7634`, `src/map.cpp:7571`, `src/map.cpp:7587`, `src/map.cpp:7801`, `src/map.cpp:7845`, `src/map.cpp:7867`, `src/map_field.cpp:414`, `src/map_field.cpp:1442`, `src/lightmap.cpp:597`, `src/field_type.cpp:344`

`on_field_modified()` always marks the level's lightmap dirty and bumps `field_revision`, whatever the field type, and `map::add_field` calls it even when it only topped up an existing entry. A burning `fd_fire`, including contained campfires, fireplaces and stoves, calls `create_hot_air` on most turns, which makes 5 `add_field` calls. Hot air is transparent and emits no light, so this unconditional call is the only reason it dirties anything. As a result `generate_lightmap`'s early-out never fires while any fire burns, and the new `field_revision` forces the 17,424-tile `update_visibility_cache` pass and `set_draw_cache_dirty()` each turn. `field_revision` only matters for the clairvoyance field. `level_reads_as_blank` also refuses to skip any level with any field, including old blood. Smoke and other gases legitimately dirty the lightmap through the transparency path, so the saving is for fire, hot air and non-opaque fields.

```cpp
invalidate_max_populated_zlev( p.z() ); set_lightmap_cache_dirty( p.z() );
get_cache( p.z() ).field_revision = next_cache_generation();       // map.cpp:7636-7638, no type test
for( int counter = 0; counter < 5; counter++ ) { ... add_field( dst, hot_air, 1 ); }   // map_field.cpp:414
if( sm != nullptr && sm->field_count > 0 ) { return false; }       // map.cpp:7867
```

**Fix:** In `field_type::finalize`, precompute `affects_light` (any level has `light_emitted > 0` or `local_light_override >= 0`) and `affects_visibility` (the clairvoyant type). Dirty the lightmap and bump `field_revision` only when those are set. Skip `on_field_modified` in `add_field` when the intensity did not change. In `level_reads_as_blank`, test only for the clairvoyant field.

### MAP-P05 · Each vehicle tile-step scans the whole grid and dirties full-level caches
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/map.cpp:1829`, `src/map.cpp:1817`, `src/map.cpp:1979`, `src/map.cpp:835`, `src/map.cpp:1276`, `src/map.cpp:11352`

To find the vehicle's index, `displace_vehicle` iterates all 2541 submap pointers in `grid` and dereferences each, although `src_submap` (computed at 1817) holds the vehicle in the normal case. A vehicle near the bubble centre costs about 1,260 dereferences per tile step, and `vehproceed` calls this for every tile a moving vehicle advances, so highway driving pays it dozens of times per turn. After the move, `on_vehicle_moved()` marks the outside, transparency, floor and pathfinding caches of each occupied level fully dirty. Those flags are lazy, so rebuilds coalesce to about one per turn, but they are still level-wide.

```cpp
for( submap *&smap : grid ) { for( size_t i = 0; i < smap->vehicles.size(); i++ ) {
    if( smap->vehicles[i].get() == &veh ) {...                      // :1829-1841
```

**Fix:** Check `src_submap->vehicles` first and fall back to the grid scan only if that fails. Replace level-wide invalidation with per-submap dirty bits over the vehicle's old and new bounding boxes (transparency already supports this; add the equivalent for outside and floor, and use point-wise pathfinding invalidation).

### MAP-P06 · `map::liquid_from` deep-copies the whole `ter_t` for every tile it inspects
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/map.cpp:6168`, `src/map.cpp:6886`, `src/map.cpp:7051`, `src/temp_crafting_inventory.cpp:433`, `src/crafting.cpp:1582`, `src/iexamine.cpp:4827`

`ter_t source_terrain = ter( p ).obj();` copies the full terrain definition by value: a `std::set<std::string>` of flags, translations, cloned examine actors, emissions, bash/deconstruct data and value_ptr activity blocks, which means dozens of heap allocations. The function reads two fields. Callers loop it per tile: building the crafting inventory (about 169 tiles), `enumerate_admitted_providers`, `map::use_charges` over reachable points, and `use_amount_square`. The crafting inventory is cached per turn/position, so the cost repeats on each rebuild rather than per frame.

```cpp
ter_t source_terrain = ter( p ).obj();   // map.cpp:6168
```

**Fix:** `const ter_t &source_terrain = ter( p ).obj();`. Optionally return `std::optional<item>` so callers skip building a null item.

### MAP-P07 · Adding an item costs O(items already on the tile), so mass drops and loot spawning are quadratic
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/map.cpp:5973`, `src/map.cpp:5922`, `src/map.cpp:5960`, `src/map.cpp:5965`, `src/map.cpp:5990`, `src/map.cpp:5832`, `src/map.cpp:6113`, `src/map.cpp:6117`, `src/item_stack.cpp:99`, `src/item_stack.cpp:142`

For every item added, `_add_item_or_charges` calls `how_many_copies_fit` → `free_volume(e)` → `item_stack::stored_volume()`, which sums `item::volume()` (recursing into contents, with a `get_var("volume")` string lookup) over every item already on the tile. There is no per-tile volume cache. `spawn_items` calls this per item, so mass drops, zone sorting into large piles, butchering, disassembly and mapgen loot are O(k·n). The same path checks `DESTROY_ITEM` and `SWIMMABLE` twice (in `valid_tile` and `add_item`), and `support_dirty()` inserts into a `std::set` per placed item.

```cpp
std::min( { copies_remaining, obj.volume() == 0_ml ? INT_MAX : free_volume( e ) / obj.volume(),
            static_cast<int>( MAX_ITEM_IN_SQUARE - i_at( e ).size() ) } )      // map.cpp:5973
for( const item &it : *items ) { ret += it.volume(); }                        // item_stack.cpp:99
```

**Fix:** Compute free volume once per batch and decrement it locally, or keep a per-tile stored-volume cache invalidated on stack mutation. Pass the tile flags already checked in `valid_tile` into `add_item`. Call `support_dirty` only for tiles without floor or support.

### MAP-P08 · Vehicle item processing scans parts by string flag and re-filters cargo parts per active item
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/map.cpp:6714`, `src/map.cpp:6719`, `src/map.cpp:6725`, `src/map.cpp:6731`, `src/map.cpp:6753`, `src/map.cpp:6494`, `src/item.cpp:4207`, `src/active_item_cache.cpp:115`

Each turn, for every vehicle in the bubble, `has_part("E_HEATER", true)` walks all parts with `std::set<std::string>` lookups before testing `engine_on`, which is false for nearly every vehicle. `process_vehicle_items()` runs for every cargo part, even empty ones. Inside the per-active-item loop, `empty(cargo_parts)` and `std::find_if` re-filter the lazy part range, and `item::find_type(base_item)` is looked up again. Comestibles are processed only every 10 minutes and spread across turns, so the per-turn item count is small; the gain is modest.

```cpp
const bool engine_heater_is_on = cur_veh.has_part( "E_HEATER", true ) && cur_veh.engine_on;   // :6714
std::find_if( begin( cargo_parts ), end( cargo_parts ), ...                                   // :6731, per item
```

**Fix:** Test `engine_on` first (or give E_HEATER a `VPFLAG`). Materialise the cargo parts once per vehicle per turn and cache insulation and temperature flag per part. Keep a per-vehicle list of parts that need `process_vehicle_items` (washer, dishwasher, autoclave, MWS, RECHARGE).

### MAP-P09 · Hot tile paths use `std::set<std::string>` flag lookups (BRIDGE, CONTAINER, TREE, EMPTY_SPACE)
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/map.cpp:2765`, `src/map.cpp:2773`, `src/map.cpp:2776`, `src/map.cpp:6814`, `src/map.cpp:10027`, `src/map.cpp:10038`, `src/map.cpp:10044`, `src/map.cpp:7327`, `src/map.cpp:4789`, `src/mapdata.h:322`, `src/mapdata.h:358`, `src/mapdata.h:387`, `src/mapdata.h:388`, `src/pathfinding.cpp:289`

`move_cost_internal` does `furniture.has_flag("BRIDGE")` (string lookup) whenever furniture is present and computes `field.total_move_cost()` twice. `could_see_items`, called per item tile per frame from rendering, looks up `"CONTAINER"` by string on terrain and furniture although `TFLAG_CONTAINER` exists. `add_tree_tops`, run for every tile of every submap loaded on a shift, converts to a string id and looks up `EMPTY_SPACE`, `TREE` and `NATURAL_UNDERGROUND` by string; enum values exist for the last two. `bash` and `remove_trap` use `"PHASE_BACK"` although `TFLAG_PHASE_BACK` exists. Each lookup is tens of nanoseconds, and A* mostly short-circuits before `move_cost_internal`.

```cpp
if( furniture.has_flag( "BRIDGE" ) )                                     // :2776
static const std::string container_string( "CONTAINER" );                // :6814
if( !ter_here.id()->has_flag( "EMPTY_SPACE" ) )                          // :10027
```

**Fix:** Add `TFLAG_BRIDGE` and `TFLAG_EMPTY_SPACE`, and switch all listed call sites to the enum `has_flag`. Compute `total_move_cost` once. In `could_see_items`, fetch the maptile once. Optionally assert in debug builds when a string lookup targets a flag that has an enum value.

### MAP-P10 · Fire and gas processors do neighbour, wind and shelter work before cheap early-outs
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/map_field.cpp:279`, `src/map_field.cpp:306`, `src/map_field.cpp:972`, `src/map_field.cpp:990`, `src/map_field.cpp:1154`, `src/map_field.cpp:1333`, `src/map_field.cpp:414`, `src/map.cpp:7543`, `src/map.cpp:7548`

`spread_gas` computes `g->is_sheltered(p)` and `get_local_windpower` for every gas tile every turn before returning on `current_intensity <= 1`, which covers most hot-air tiles, and calls `is_outside` twice. `field_processor_fd_fire` builds 8 neighbour maptiles and 3 wind-blocker maptiles and runs both spread loops with `find_field` per neighbour even for contained fires (stoves, fireplaces, braziers), where `can_spread` is false; those are exactly the long-burning base fires. Each `add_field` does two separate full `has_flag` tile lookups before the cheap phase check. Each saved step is small; the total matters only for large fires.

```cpp
const bool sheltered = g->is_sheltered( p ); ... const int windpower = get_local_windpower(...);  // :279-284
if( current_intensity <= 1 || rng( 1, 100 - windpower ) > percent_spread ) { return; }           // :306
```

**Fix:** In `spread_gas`, test intensity first and reuse one `is_outside` result. In `fd_fire`, build neighbours and blockers lazily and skip the spread loops when `!can_spread` (wind is still needed for the smoke roll). In `add_field`, fetch the maptile once. Reduce hot-air churn, e.g. one `add_field` per turn, or skip tiles that already hold equal or stronger hot air.

### MAP-P11 · `ter_set`/`furn_set` store a zero-damage `std::map` node for every tile ever set
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/map.cpp:2109`, `src/map.cpp:2624`, `src/map.cpp:4653`, `src/submap.h:139`, `src/submap.h:331`, `src/submap.cpp:400`

Both setters call `set_map_damage(l, 0)`, implemented as `ephemeral_data[p] = { dmg }`, so every terrain or furniture change, including mapgen writes, allocates a `std::map` node holding 0 that is never erased. `merge_submaps` and a successful bash do the same. The data is not serialized and resets on reload, and the nodes are small, so the cost is allocation churn.

```cpp
void set_map_damage( const point_sm_ms &p, int dmg ) { ephemeral_data[p] = { dmg }; }   // submap.h:139
```

**Fix:** `if( dmg == 0 ) { ephemeral_data.erase( p ); return; }`.

### MAP-P12 · Curses redraw runs full `draw_maptile` on every lit off-screen tile just to memorize it
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/map.cpp:8031`, `src/map.cpp:8054`, `src/map.cpp:8266`

In curses builds, `map::draw`'s second loop calls `draw_maptile` with `output(false)` on every CLEAR tile of the bubble outside the window, every redraw. That runs trap visibility, `partial_con_at`, building the field symbol string, `sees_some_items` and vehicle lookups. The only lasting effect is `memorize_symbol`, and only when `memory_cache_ter_is_dirty(p)`.

```cpp
drawsq_params mm_params = drawsq_params().memorize( true ).output( false );
... draw_maptile( w, p, curr_maptile, mm_params );                         // :8054
if( param.memorize() && memory_cache_ter_is_dirty( p ) ) { ... }           // :8266
```

**Fix:** Check `memory_cache_ter_is_dirty(p)` before calling `draw_maptile`, or run the loop only when a memory sweep is pending for the level.

### MAP-P13 · `map_memory` tile lookups go through `std::map<int, vector>` and copy tiles on every write
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/map_memory.cpp:436`, `src/map_memory.cpp:443`, `src/map_memory.cpp:221`, `src/map_memory.cpp:263`, `src/map_memory.cpp:305`, `src/map_memory.h:174`

`get_submap`, behind `avatar::get_memorized_tile` (called for every non-visible tile drawn in tiles mode), does `cached[sm_pos.z()]` twice on a `mutable std::map<int, std::vector<...>>`, and `operator[]` inserts empty vectors for unprepared levels. The `set_tile_*` functions copy the whole `memorized_tile`, including its `dec_id` string, and write it back; `clear_tile_decoration` writes back even when nothing changed, which allocates 144 tiles for an empty submap. Only 3-4 levels are prepared, so each lookup is a few comparisons; the gain is likely small.

```cpp
mutable std::map<int, std::vector<shared_ptr_fast<mm_submap>>> cached;     // map_memory.h:174
memorized_tile mt = sm.get_tile( p.loc ); if( string_starts_with(...) ) {...} sm.set_tile( p.loc, mt );
```

**Fix:** Store `cached` as `std::array<std::vector<...>, OVERMAP_LAYERS>` indexed by `z + OVERMAP_DEPTH` and look it up once. Give `mm_submap` a mutable tile accessor. In `clear_tile_decoration`, write only when something changes.

### MAP-P14 · Tile accessors do up to four bounds checks and two division passes per call, out of line
**Severity:** Low · **Effort:** Medium · **Verified:** plausible
**Where:** `src/map.cpp:2259`, `src/map.cpp:2061`, `src/map.cpp:3523`, `src/map.cpp:10408`, `src/map.cpp:11265`, `src/map.cpp:11288`, `src/map.cpp:11340`, `src/map.h:2215`, `Makefile:128`

`map::ter(p)` checks `inbounds(p)`, then `unsafe_get_submap_at(p, l)` runs `project_remain`, discards its submap result and calls `unsafe_get_submap_at(p)`, which asserts `inbounds` (asserts stay live because `NDEBUG` is commented out at `Makefile:128`) and divides again. `get_nonant` re-checks the range and `getsubmap_helper` checks the grid size. `furn()`, `field_at()`, `has_flag*()`, `i_at()`, `move_cost()`, `tr_at()` and others share the chain, and all are out of line for other translation units without LTO. Branches are well predicted and the divisions compile to multiplies, and the hottest loops already index submaps directly, so the gain is unmeasured.

```cpp
std::tie( sm, offset_p ) = project_remain<coords::sm>( p ); return unsafe_get_submap_at( p );   // map.h:2215
cata_assert( inbounds( p ) ); return get_submap_at_grid( tripoint_rel_sm{ p.x() / SEEX, ... } ); // map.cpp:11290
```

**Fix:** Add inline `nonant_unchecked(smx, smy, z)` and `submap_at_unchecked(const tripoint_bub_ms &, point_sm_ms &)` helpers in `map.h` that do one division pair and index `grid[]` directly, and use them after the single `inbounds()` check. Move `inbounds()`, `ter()`, `furn()` and `has_flag_ter_or_furn()` into the header. Profile before and after.

### MAP-P15 · Any one-tile transparency change, including smoke, re-caches every vehicle on every level
**Severity:** Low · **Effort:** Medium · **Verified:** plausible
**Where:** `src/map.cpp:412`, `src/map.cpp:7644`, `src/map.cpp:8417`, `src/map.cpp:10891`, `src/map.cpp:10912`, `src/map.cpp:10975`, `src/map.cpp:12287`, `src/do_turn.cpp:736`, `src/do_turn.cpp:746`

Any change to an opaque field, or one with `dirty_transparency_cache`, sets `scene_build_pending`. The next `map::sees()` or `has_potential_los()` then runs `build_scene_caches()`, which refills `vehicle_opaque_cache` and reruns `do_vehicle_caching` over every part of every vehicle on all 21 levels, even when no vehicle moved. `build_map_cache` already calls `build_scene_caches` unconditionally after `process_fields` every turn, so field processing itself adds no extra rebuild; the extra cost comes from changes made during monster moves or field emission.

```cpp
get_cache( smp.z() ).transparency_cache_dirty.set( ... ); scene_build_pending = true;   // :416-417
for( int z = minz; z <= maxz; z++ ) { do_vehicle_caching( z ); }                      // :10920-10929
```

**Fix:** Track vehicle-cache dirtiness on its own flag (set by `on_vehicle_moved`, `add_vehicle_to_cache`, part install/removal, door toggles). When only per-submap transparency bits are dirty, skip the vehicle reset and re-caching.

### MAP-P16 · `vehmove` rebuilds every power grid and the power-network registry each turn
**Severity:** Low · **Effort:** Medium · **Verified:** plausible
**Where:** `src/map.cpp:844`, `src/map.cpp:861`, `src/map.cpp:872`, `src/map.cpp:906`, `src/map.cpp:1044`, `src/power_network.cpp:80`, `src/power_network.cpp:94`, `src/power_network.cpp:108`

Every turn, `resolve_off_map_grid_generation()` collects all bubble vehicles, runs `search_connected_vehicles` (a Dijkstra returning `std::map`) per grid and rebuilds `power_network_manager` from scratch, including a node per vehicle that scans its solar, wind, water, accessory and battery parts. `resolve_appliance_grid_power()` then scans every part of every appliance. Topology only changes on cable or part changes or vehicle load/unload. The per-vehicle work is small and `vehmove` already walks the same set, so the real cost is likely tens to a few hundred microseconds per turn, unmeasured.

```cpp
resolve_off_map_grid_generation(); resolve_appliance_grid_power();          // map.cpp:1044-1045, unconditional
std::map<vehicle *, float> grid = veh->search_connected_vehicles( *this );  // :872
```

**Fix:** Cache grid membership behind a dirty flag (cable connect/disconnect, part install/removal, vehicle load/unload/move), and run `catchup_off_map_renewables` on a coarse timer. Keep appliances with `power_disabled` parts in a set.
