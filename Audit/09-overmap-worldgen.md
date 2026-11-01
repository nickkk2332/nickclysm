# 09 · Overmap & world generation

> Engine audit – static code reading at commit `a1a646d6`; nothing was compiled or run. Each finding was checked against the code by a separate reviewer: **confirmed** = verified by reading the code; **plausible** = consistent with the code, but reachability or impact not proven. Line numbers refer to that commit.

**Scope:** `src/overmap.cpp`, `src/overmap_city.cpp`, `src/overmap_connection.cpp`, `src/overmap_highway.cpp`, `src/overmap_location.cpp`, `src/overmap_special.cpp`, `src/overmap_special_fixed.cpp`, `src/overmap_special_mutable.cpp`, `src/overmap_terrain.cpp`, `src/overmap_water.cpp`, `src/overmap_worldgen.cpp`, `src/overmapbuffer.cpp`, `src/overmap_noise.cpp`, `src/overmap_map_data_cache.cpp`, `src/regional_settings.cpp`, `src/city.cpp`, `src/simplexnoise.cpp`, `src/omdata.h`

## Summary

Overmap generation works for vanilla data, but most of its tuning is fixed in C++. Ocean, forest, ravine and river terrain ids, worldgen monster groups, radio stations, bridge and sewer rules, city shape and special density cannot be set per region. EOCs can reveal the map but cannot write to the overmap, even though `overmapbuffer` already has the APIs they would need. There are also several small, clear-cut bugs: swapped arguments, a `std::min` that should be `std::max`, misindexed direction tables, and an exit EOC that runs the wrong terrain's hook. Many cost only a line to fix. On performance, the overmap search (`find_closest`/`find_all`) and special placement (`can_place_special`) both do far more work than needed. Saves rewrite every overmap ever loaded.

| Lens | Critical | High | Medium | Low | Total |
|---|---|---|---|---|---|
| Missing JSON / EOC | 0 | 1 | 12 | 3 | 16 |
| Bugs & quirks | 0 | 0 | 8 | 8 | 16 |
| Performance | 0 | 2 | 6 | 8 | 16 |
| **Total** | 0 | 3 | 26 | 19 | 48 |

**Fix first:**

1. **OMAP-P01** · `required_locations()`/`get_terrains()` return by value in special-placement loops – a small signature change that removes thousands of heap allocations from the innermost worldgen loop.
2. **OMAP-P02** · `find_closest`/`find_all` spiral and string-compare cost – every mission, EOC location search and NPC goal search pays this, with multi-second hitches when nothing matches. While in there, apply the one-line fix in OMAP-B02.
3. **OMAP-B01** · `place_special_attempt` swaps `must_be_unexplored` and `force` – a one-line fix that stops mission and EOC specials from routing roads through map the player has already explored.
4. **OMAP-J02** · `exit_eoc` runs the new terrain's EOC – a one-line fix that makes a documented field work at all.
5. **OMAP-J01** · EOCs cannot write overmap state – the biggest modding gap, and the C++ APIs already exist, so the work is mostly thin wrappers.

## 1. Missing JSON bindings & EOC features

### OMAP-J01 · EOCs can barely read and cannot write overmap state, though overmapbuffer already has the C++ APIs
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/npctalk.cpp:8692`, `src/npctalk.cpp:8235`, `src/npctalk.cpp:8265`, `src/condition.cpp:2624`, `src/condition.cpp:2668`, `src/condition.cpp:2672`, `src/overmapbuffer.cpp:285`, `src/overmapbuffer.cpp:299`, `src/overmapbuffer.cpp:511`, `src/overmapbuffer.cpp:875`, `src/overmapbuffer.cpp:905`, `src/overmapbuffer.cpp:977`, `src/overmapbuffer.cpp:1202`, `src/overmapbuffer.cpp:1801`, `src/overmapbuffer.cpp:2184`, `src/overmapbuffer.cpp:2308`, `src/mission_util.cpp:200`, `src/mission_util.cpp:225`

Here is everything EOCs can do with the overmap. Effects: `reveal_map`, `reveal_route`, `closest_city`, `signal_hordes`, plus `u_location_variable`/`target_params`. Conditions: `u_at_om_location`, `u_near_om_location`, `overmap_at_point`, `map_in_city`. `math_parser_diag.cpp` has no overmap, city, region or horde function. `overmapbuffer` already exposes `ter_set`, `place_special` (at a point, with rotation), `add_note`/`delete_note`/`mark_note_dangerous`, `set_seen`/`seen`, `spawn_mongroup`, `get_horde_size`/`has_horde`, `overmap_special_at`, `is_explored` and `get_settings` (region), but none of them can be called from JSON. As a result, modders cannot:

- turn an OMT into a burnt variant after an event
- drop a special at a chosen point
- write a map note
- reveal only partially (`reveal_map` always sets `om_vision_level::full`)
- spawn an overmap horde
- check whether a location has been seen
- test which region or special a tile belongs to
- copy an OMT id into a variable

`closest_city` has no size or name filter, and it only searches a fixed radius of one overmap (`overmapbuffer.cpp:1803`). There are partial workarounds through `target_params`: `om_terrain_replace` reaches `ter_set` (`mission_util.cpp:225`), and an `om_special` search reaches `place_special` (`mission_util.cpp:200`). Neither one works at a chosen point.

```cpp
// npctalk.cpp:8243 – reveal_map: always full vision
overmap_buffer.reveal( omt, radius.evaluate( d ) );   // -> set_seen( p, om_vision_level::full )
// grep overmap_buffer in math_parser_diag.cpp: 0 hits
```

**Fix:** Add thin wrappers over the existing APIs.
```jsonc
// effects
{ "set_overmap_terrain": { "context_val": "loc" }, "oter": "house_burnt_north" }
{ "place_overmap_special": "my_special", "target_var": { ... }, "rotation": "random", "force": false, "result_var": { ... } }
{ "add_overmap_note": { "context_val": "loc" }, "text": "Bandit camp", "symbol": "B", "color": "red", "danger_radius": 2 }
{ "delete_overmap_note": { "context_val": "loc" } }
{ "reveal_map": { "context_val": "loc" }, "radius": 3, "level": "outlines" }   // outlines|vague|details|full|unseen
{ "spawn_overmap_mongroup": "GROUP_ZOMBIE_HORDE", "target_var": { ... }, "population": 50, "horde": true, "target": { ... } }
{ "closest_city": { ... }, "search_range": 3, "min_size": 4, "max_size": 20, "name_var": { ... } }
// conditions
{ "map_omt_is": "lab", "match_type": "PREFIX", "loc": { ... } }
{ "map_in_overmap_special": "Necropolis", "loc": { ... } }
// math
omt_seen_level(loc), horde_size(loc, radius), region_is("desert"), city_size_at(loc), distance_to_city(loc)
```

### OMAP-J02 · overmap_terrain exit_eoc never works: game.cpp checks the old OMT's exit EOC but runs the new OMT's
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/game.cpp:11560`, `src/game.cpp:11562`, `src/overmap_terrain.cpp:590`, `src/omdata.h:580`, `src/overmap_terrain.cpp:652`, `doc/JSON/OVERMAP.md:336`, `doc/JSON/OVERMAP.md:386`

`exit_eoc` is a documented `overmap_terrain` field. When the avatar crosses an OMT boundary, the code checks whether the terrain being *left* has an exit EOC, but then activates `cur_ter->get_exit_EOC()`, the exit EOC of the terrain being *entered*. Leaving an `exit_eoc` terrain for one without an exit EOC passes a null id to `generic_factory::obj`, which raises the debugmsg "invalid effect_on_condition id", and the intended EOC never runs. If both terrains define exit EOCs, the wrong one runs. No vanilla data uses `exit_eoc`, which is probably why nobody has noticed. Related gaps:

- entry and exit EOCs fire only for the avatar
- `oter_type_t::check()` validates neither id
- the doc example misspells the key as `"exity_eoc"`

```cpp
if( !past_ter->get_exit_EOC().is_null() ) {                 // game.cpp:11560
    effect_on_condition_id eoc = cur_ter->get_exit_EOC();   // game.cpp:11562 – wrong terrain
    eoc->activate_activation_only( d, "OMT movement" );
```

**Fix:** Use `past_ter->get_exit_EOC()`. Pass context vars to both hooks (`omt_pos` as a location var, `oter_id`, and the other side's oter id) so that one EOC can serve many terrains. Validate both ids in `oter_type_t::check()` and fix the doc typo. Optionally add `"entry_eoc_npcs": true` to fire the hooks for NPCs too.

### OMAP-J03 · forest_biome_component "sequence" is loaded but ignored; documented "clear_types" does not exist
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/regional_settings.cpp:390`, `src/regional_settings.h:158`, `src/regional_settings.cpp:764`, `src/string_id.h:234`, `doc/JSON/REGION_SETTINGS.md:281`, `doc/JSON/REGION_SETTINGS.md:283`

Forest mapgen rolls biome components in order, and the first success wins, so the order decides the tree/shrub/clutter mix. The docs say `sequence` sets that order. The loader reads `sequence` and nothing uses it. `biome_components` is a `std::set<string_id>`, and a set of string_ids is ordered by interned int, which is effectively the first time each id string was parsed. This already affects shipped data. In core `biome_forest_thick_default`, `water_forest` was interned earlier, so it is rolled before `trees_forest_thick`. `desert_region` asks for the order [clutter, shrubs, trees, water], but its ids were already interned in core's order, so the mod's order is ignored. Nothing reads the documented `clear_types` field.

```cpp
optional( jo, was_loaded, "sequence", sequence );          // regional_settings.cpp:390 – never used
std::set<forest_biome_component_id> biome_components;      // regional_settings.h:158
bool operator<( const This &rhs ) const { return _id._id < rhs._id._id; }  // string_id.h:234
```

**Fix:** In `forest_biome_mapgen::finalize`, copy the components into a `std::vector` sorted by `(sequence, id)` and iterate that vector in `pick()`. Either remove `clear_types` from the docs or implement it. `types` already supports `extend`/`delete`.

### OMAP-J04 · City layout and special-density tuning are compile-time constants
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/overmap_city.cpp:35`, `src/overmap_city.cpp:88`, `src/overmap_city.cpp:144`, `src/overmap_city.cpp:174`, `src/overmap_city.cpp:198`, `src/overmap_city.cpp:407`, `src/overmap_city.cpp:415`, `src/overmap_city.cpp:455`, `src/omdata.h:693`, `src/overmap.cpp:3215`

`region_settings_city` exposes size, spacing, shop/park radius and sigma, and the building bins. Most of the city's shape is still fixed in code:

- the 1-in-4 empty-lot chance (`BUILDINGCHANCE`)
- the size distribution (×1/3, ×2/3, ×3/2, ×2, clamped to 2..55)
- the spacing cap of 10
- block widths `rng(3,5)`
- side-street shortening `rng(1,3)`
- `one_in(5)` extra branches
- a megacity mode that is always five cities of size 40 at fixed points

Special density is fixed by `OMSPEC_FREQ = 15` (144 sectors per overmap) and 10 attempts per sector. A region can therefore be neither "sparse wilderness" nor "dense sprawl" for specials. The `road_nesw` seed is overwritten later by `build_connection`. Its only lasting effect is that the intra-city connection must accept a `road` tile.

```cpp
static constexpr int BUILDINGCHANCE = 4;            // overmap_city.cpp:35
size = std::min( size, 55 );                        // overmap_city.cpp:174-185
constexpr int OMSPEC_FREQ = 15;                     // omdata.h:693
const size_t attempts = 10;                         // overmap.cpp:3215
```

**Fix:**
```jsonc
// region_settings_city
"empty_lot_chance": 4, "size_variation": [[0.33,1],[0.66,1],[1.5,0.5],[2.0,0.5]],
"min_size": 2, "max_size": 55, "block_width": [3,5], "street_shrink": [1,3],
"extra_branch_chance": 5, "seed_terrain": "road_nesw",
"megacity": { "seeds_per_overmap": 5, "size": 40 }
// region_settings
"special_sector_size": 15, "special_attempts_per_sector": 10, "special_occurrence_multiplier": { ... }
```

### OMAP-J05 · Region settings, dimensions and region layouts have no check()
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/regional_settings.cpp:849`, `src/init.cpp:1007`, `src/regional_settings.cpp:806`, `src/regional_settings.cpp:924`, `src/overmap_city.cpp:268`, `src/overmap_location.cpp:66`

None of the 15 `region_settings_*` factories, `dimension` or `dimension_region_layout` has a `check()`, and `init.cpp` registers no consistency check for any of them. Bad ids only show up during worldgen, as debugmsgs or crashes. That covers bad oter ids (lake/ravine terrain, `default_oter`), connection ids, highway special ids (finalize dereferences `clockwise_slant`) and a layout's region ids. The only validation is `building_bin::finalize`, which checks special ids. The concrete failures this lets through are listed separately: OMAP-B05 (empty bins crash, unplaceable bins hang), OMAP-B09 (spacing clamped into `city_size`), OMAP-B14 (unknown location flags) and OMAP-B15 (resolve cycles).

```cpp
// init.cpp:1007-1013 – consistency list covers overmap connections/terrain/locations/cities/specials,
// but no region settings, dimensions or region layouts
```

**Fix:** Add `check()` to every `region_settings_*` type. It should validate referenced ids, check that bins are non-empty when `city_size > 0`, and check that each bin has at least one non-unique entry whose `city_sizes` cover `[1, max city size]`. Register region settings, dimensions and layouts in `init.cpp`'s consistency checks.

### OMAP-J06 · target_params location search: broken and unexposed knobs (search_range, exclude_seen, z-range, create_if_necessary)
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/mission_util.cpp:385`, `src/mission_util.cpp:391`, `src/mission.h:137`, `src/mission_util.cpp:217`, `src/mission_util.cpp:222`, `src/mission_util.cpp:237`, `src/overmapbuffer.cpp:1346`, `src/overmapbuffer.cpp:1411`, `src/overmapbuffer.cpp:1498`, `src/overmapbuffer.h:146`, `doc/JSON/MISSIONS_JSON.md:219`

Missions, `u_location_variable` and `mapgen_update` all find their targets through `parse_mission_om_target`, which has six problems.

1. A non-random search with `search_range` is rejected at load time, but `find_closest` does use `search_range` as its maximum distance. Modders therefore cannot write "nearest X within 20 OMTs, else fail". The ban is documented (`MISSIONS_JSON.md:219`), and that doc gives the wrong default: it says 2520, the code uses `OMAPX*3 = 540`.
2. The undocumented key `exclude_seen` reads the nonexistent key `exclude` with no fallback, so using it throws. Even if it read the right key, it would set `random`.
3. `create_if_necessary` is never read from JSON.
4. JSON exposes only one `om_terrain` and no z-range. `find_closest` scans z −10..10, while `find_all`/`find_random` scan only the origin z, so `z` means a different thing depending on `random`.
5. The `om_terrain_replace` retry sets `existing_only = true` directly under a comment saying it allows creating overmaps. The outer retry at :237 hides this, but it has a worse problem: it reuses `find_params` after `types.front()` was overwritten with the replacement terrain (:222) and `must_see` was cleared, so it can return an unconverted replacement tile.
6. There is no way to match by OMT flag or land-use code.

```cpp
if( jo.has_member( "exclude_seen" ) ) { p.random = jo.get_bool( "exclude" ); }   // mission_util.cpp:385
// We didn't find it, so allow this search to create new overmaps and try again.
find_params.existing_only = true;                                                // mission_util.cpp:217-219
bool create_if_necessary = true;                                                 // mission.h:137, no JSON reader
```

**Fix:** Accept `search_range` for closest searches, and delete or fix `exclude_seen` (`cant_see` already covers that use). Add:
```jsonc
"create_if_necessary": false, "min_z": -3, "max_z": 0,          // "z" = exact level in both modes
"om_terrain": [ "lab", "lab_stairs" ], "om_terrain_flag": "SOURCE_FOOD",
"must_see_level": "outlines", "result_list_var": { ... }
```
Also change the inner retry to `existing_only = false`, and rebuild `find_params` before the outer retry.

### OMAP-J07 · overmap_special "eoc" runs with an avatar talker, no placement context, is re-tested per candidate, and is documented under overmap_terrain
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/overmap.cpp:2981`, `src/overmap.cpp:2945`, `src/overmap.cpp:3042`, `src/overmap.cpp:3058`, `src/overmap_special.cpp:274`, `src/overmap_special.cpp:301`, `src/overmap_special.cpp:302`, `doc/JSON/OVERMAP.md:334`, `doc/JSON/OVERMAP.md:681`

The special's EOC condition is tested in `can_place_special` with `dialogue(get_talker_for(get_avatar()), nullptr)` and no context. The effect runs in `overmap_special::place()` in a new dialogue of the same kind, and that happens before `data_->place()`, including for forced and debug placements. The EOC is never told the special's origin, rotation, id or nearest city. `u_*` location conditions refer to the avatar, who may be several overmaps away. Because `random_special_rotation` calls `can_place_special` once per candidate rotation, the condition is re-evaluated on every rotation and attempt. That happens before the cheap location checks, so a random condition becomes a per-attempt filter. In debug builds, `cata_assert` at :3042 evaluates it again, so an rng condition can abort the game. The loader has a FIXME: there is no generic reader, so `copy-from` cannot override or delete an inherited `eoc`. In the docs, the `eoc` row sits in the `overmap_terrain` table, which does not load the field, and the `overmap_special` table lists neither `eoc` nor `spawns`.

```cpp
if( special.has_eoc() ) { dialogue d( get_talker_for( get_avatar() ), nullptr );       // overmap.cpp:2981
    if( !special.get_eoc()->test_condition( d ) ) return false; }
if( has_eoc() ) { dialogue d( get_talker_for( get_avatar() ), nullptr );               // overmap_special.cpp:274
    get_eoc()->apply_true_effects( d ); }
```

**Fix:** Evaluate the condition once per overmap batch in `place_specials`, and drop the special from the batch if it fails. Run the effect after placement, with context vars `special_id`, `special_pos` (an abs location var), `rotation`, `city_name`, `city_size`, `city_distance` and `overmap_pos`. Add an `overmap_special_placed {special_id, pos}` event so global EOCs can react. Move the doc row into the `overmap_special` table, document `spawns`, and state that alpha is the avatar.

### OMAP-J08 · Radio stations come from hardcoded OMT id prefixes with hardcoded messages
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/overmap.cpp:3662`, `src/overmap.cpp:3673`, `src/overmap.cpp:3681`, `src/overmap.cpp:3685`, `src/overmap.h:169`

`place_radios` scans z=0 and creates broadcasts only for OMTs whose id starts with `radio_tower` (one in three becomes a weather radio), `lmoe` or `fema_entrance`. The LMOE and FEMA messages are fixed (translatable) strings, and the signal strength comes from `constexpr` constants. A mod cannot make its own outpost broadcast or choose its message or strength. Any mod OMT that happens to be named `radio_tower_*` gets a tower. Nothing else adds to `radios` except loading a save, so EOCs cannot add, remove or change a station either.

```cpp
if( is_ot_match( "radio_tower", ter( pos_omt ), ot_match_type::prefix ) ) {   // overmap.cpp:3673
    if( one_in( 3 ) ) { radios.emplace_back( pos_sm, strength(), "", radio_type::WEATHER_RADIO );
constexpr int RADIO_MIN_STRENGTH = 120; constexpr int RADIO_MAX_STRENGTH = 360; // overmap.h:169
```

**Fix:**
```jsonc
// overmap_terrain
"radio": { "chance": 100, "type": "broadcast", "strength": [120, 360], "snippet_category": "radio_archive" }
// EOC effects
{ "add_radio_station": { "context_val": "loc" }, "message": "...", "strength": 200, "frequency_var": { ... } }
{ "remove_radio_station": { "context_val": "loc" } }
```
Move the vanilla prefixes into JSON.

### OMAP-J09 · Worldgen wildlife and city-horde monster groups and densities are hardcoded, not set per region
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/overmap.cpp:63`, `src/overmap.cpp:3444`, `src/overmap.cpp:3479`, `src/overmap.cpp:3523`, `src/overmap.cpp:3531`, `src/overmap.cpp:3550`, `src/overmap.cpp:3575`, `src/overmap.cpp:3591`, `src/overmap.cpp:1137`, `data/core/external_options.json:214`

`place_mongroups` and `generate_sub` use the static ids `GROUP_ZOMBIE_HORDE`, `GROUP_SWAMP`, `GROUP_RIVER`, `GROUP_OCEAN_DEEP`, `GROUP_OCEAN_SHORE` and `GROUP_SUBWAY_CITY` with fixed rules:

- swamp, river and ocean groups use 7×7 windows with a 25-tile minimum and `rng(count*8, count*25)` population
- the deep-ocean threshold is a constant 1.25
- city hordes are placed only on `road` tiles, in packs of 10
- subway groups spawn only at z=−2, with `size*size*2` population

`regional_settings` has no mongroup fields. Mods therefore override the global groups (Magiclysm, TropiCataclysm and Aftershock override `GROUP_SWAMP`/`GROUP_RIVER`/`GROUP_SUBWAY_CITY`), and those overrides leak into every region and dimension. The overmap-level swamp pass only runs when `DISABLE_ANIMAL_CLASH` is *true*, while the option's description reads "Disable additional spawn…". That looks inverted, but the intent cannot be proven. Per-OMT `GROUP_SWAMP` spawns on `forest_water` still apply either way.

```cpp
if( overmap_buffer.ter( target_omt )->get_type_id() == oter_type_road ) {           // overmap.cpp:3479
spawn_mongroup( s, GROUP_ZOMBIE_HORDE, desired_zombies > 10 ? 10 : desired_zombies ); // :3523
if( get_option<bool>( "DISABLE_ANIMAL_CLASH" ) ) { // place swamp monsters             // :3531
if( z == -2 ) { spawn_mon_group( mongroup( GROUP_SUBWAY_CITY, abs_pos, i.size * i.size * 2 ), i.size * 2 ); // :1137
```

**Fix:**
```jsonc
// region_settings
"worldgen_mongroups": [ { "group": "GROUP_SWAMP", "locations": ["swamp"], "z": 0, "window": 7,
                          "min_tiles": 25, "population_per_tile": [8, 25], "radius": 3 } ],
"city_hordes": { "group": "GROUP_ZOMBIE_HORDE", "locations": ["road"], "pack_size": 10,
                 "scalar": 80, "spread": 3.5, "size_threshold": 4 }
```
The world options would become multipliers. Turn the subway group into a per-z rule, and fix or rename the `DISABLE_ANIMAL_CLASH` gate.

### OMAP-J10 · Forest, swamp, ocean, river and ravine terrain ids and noise shapes are hardcoded, while lakes are configurable
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/overmap_water.cpp:431`, `src/overmap_water.cpp:32`, `src/overmap.cpp:73`, `src/overmap.cpp:1877`, `src/overmap.cpp:2064`, `src/overmap.cpp:2158`, `src/overmap.cpp:2431`, `src/regional_settings.cpp:493`, `src/regional_settings.h:222`, `src/regional_settings.h:294`, `src/regional_settings.h:315`, `src/overmap_noise.cpp:13`

Lake terrain is configurable per region: `surface_ter`, `shore_ter`, `interior_ter`, `bed_ter`. The rest is not:

- ocean uses literal `ocean_*` ids
- forest, swamp and trail code compares against static `forest`/`forest_thick`/`forest_water`
- for ravines, only `ravine_bottom_terrain_id` is exposed, while `ravine`, `ravine_edge` and `ravine_floor_edge` are literals
- river pieces come from a static table
- noise octaves, scale and exponents are constants, so a region can move thresholds but cannot change feature size

A desert or jungle region therefore cannot place its own OMTs at overmap level. `desert_region` works around this by globally redefining the vanilla forest OMTs, so names, symbols and travel costs cannot differ between regions.

```cpp
const oter_id ocean_surface( "ocean_surface" ); ...                        // overmap_water.cpp:431-434
const oter_id rift( "ravine" ); const oter_id rift_edge( "ravine_edge" );  // overmap.cpp:2431-2434
scaled_octave_noise_3d( 4, 0.5, 0.03, 0, 1, p.x(), p.y(), get_seed() );   // overmap_noise.cpp:13
```

**Fix:** Mirror the lake fields:
```jsonc
// region_settings_forest
"forest_ter": "forest", "forest_thick_ter": "forest_thick", "swamp_ter": "forest_water",
// region_settings_ocean
"surface_ter": ..., "shore_ter": ..., "interior_ter": ..., "bed_ter": ...,
// region_settings_ravine
"ravine_ter": ..., "edge_ter": ..., "floor_edge_ter": ...,
// region_settings_river
"river_ter_prefix": "river",
// every noise-backed setting
"noise": { "octaves": 4, "persistence": 0.5, "scale": 0.03, "exponent": 2 }
```
The trail and swamp passes should test `overmap_location`s, not ids.

### OMAP-J11 · Overmap connections cannot be extended, and special connections always route to the city center
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/overmap_connection.cpp:102`, `src/generic_factory.h:665`, `src/overmap_special_fixed.cpp:311`, `src/overmap_special_mutable.cpp:901`, `src/overmap.cpp:2499`, `doc/JSON/OVERMAP.md:624`, `data/mods/desert_region/desert_overmap_connections.json:4`

`overmap_connection::load` reads `subtypes` with `mandatory( jo, false, ... )`, so `copy-from`, `extend` and `delete` do not work. To let `local_road` cross a new terrain, a mod must redefine the whole connection. Several mods do this (aftershock_exoplanet, TropiCataclysm, hunvre, desert_region), and their copies go stale. Desert's copy has 6 subtypes against core's 9 and is missing stream, highway `PERPENDICULAR_CROSSING` and `bridgehead_ground`. `overmap_location` `terrains` cannot be extended either.

Separately, both special subtypes route every connection to the nearest city center or a fallback point. That includes trails, sewers and subways. The code carries the TODO "JSONification of logic + don't treat non roads like roads", and the docs promise "nearest matching terrain". In practice, the city-center road tile is not a valid `forest_trail` location, so `greedy_path` returns an empty path. About 22 vanilla `forest_trail` specials therefore get no trail at all whenever the overmap has a city.

```cpp
void overmap_connection::load( const JsonObject &jo, std::string_view ) { mandatory( jo, false, "subtypes", subtypes ); }
// overmap_special_fixed.cpp:311-318
// TODO: JSONification of logic + don't treat non roads like roads
if( cit ) { target = cit.pos; } else { target = om.get_fallback_road_connection_point(); }
```

**Fix:** Load `subtypes` with `optional( jo, was_loaded, ... )` and a reader that supports `extend`/`delete`, or key subtypes so `"extend": { "subtypes": [...] }` works. On `overmap_special_connection` add `"target": "city" | "nearest_existing" | "point" | "none"` and `"max_length"`. `nearest_existing` should search for the nearest tile where `connection.has(oter)` holds.

### OMAP-J12 · OMT flags are a closed C++ enum: mods cannot tag terrain for searches, locations or EOCs
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/overmap_terrain.cpp:607`, `src/generic_factory.h:1861`, `src/omdata.h:279`, `src/overmap_location.cpp:66`, `src/overmapbuffer.h:146`, `src/condition.cpp:2624`, `src/mission_util.cpp:369`

`oter_type_t` flags are read with `typed_flag_reader<oter_flags>`, which throws on any name outside the roughly 50 values of a hardcoded enum. A mod cannot tag its OMTs (`MOD_SAFEHOUSE`) and use the tag in `overmap_location` `flags`, in mission or EOC searches, or in conditions. No EOC condition tests OMT flags at all, not even the built-in ones (`SOURCE_FOOD`, `RISK_HIGH`, `WATER`). `u_at_om_location` and `overmap_at_point` compare id strings. Modders fall back to fragile prefix/contains id matching. `overmap_location` `terrains` lists already let mods group OMTs for special placement, so the real gap is in EOC conditions and target searches.

```cpp
const auto flag_reader = typed_flag_reader<oter_flags>( "overmap terrain flag" );          // overmap_terrain.cpp:607
if( iter == flag_map.cend() ) { jv.throw_error( string_format( "invalid %s: \"%s\"", ... ) ); } // generic_factory.h:1861
```

**Fix:** Keep the enum bitset for engine flags, and also accept unknown strings into a `cata::flat_set<std::string>` (or `flag_id`) with `has_flag(std::string)`. Let `overmap_location` `flags` and `target_params` `"om_terrain_flag"` match either kind. Add the condition `{ "map_omt_has_flag": "SOURCE_FOOD", "loc": { ... } }`.

### OMAP-J13 · Bridges, manhole-to-sewer links and subway stations are hardcoded vertical terrain rules
**Severity:** Medium · **Effort:** Large · **Verified:** confirmed
**Where:** `src/overmap.cpp:1065`, `src/overmap.cpp:1085`, `src/overmap.cpp:1091`, `src/overmap.cpp:1097`, `src/overmap.cpp:1170`, `src/overmap.cpp:1189`, `src/overmap_city.cpp:432`, `src/overmapbuffer.cpp:1092`

Every rule that creates terrain above or below a tile is tied to a specific vanilla type:

- `road_nesw_manhole` gets `sewer_isolated` below it
- `sewer_connector` and `microlab_sub_connector` become sewer and subway ends
- `sub_station` gets fixed stations at fixed z-levels
- bridges get `bridge_road`+rotation above and `bridge`+rotation supports, and bridgehead ids are built from strings
- city manholes appear by `one_in(2)` on four-way `road` tiles, under the comment "TODO: Get rid of the hardcoded terrain ids"
- travel pathing treats only `bridgehead_ground`/`bridgehead_ramp` as ramps, so `railroad_bridgehead_*` tiles are not ramps

A mod's rope bridge, storm drain or metro station gets none of these rules.

```cpp
oter_above_actions = { { oter_type_road_nesw_manhole.id(), [&]( ... ) {             // overmap.cpp:1066
    ter_set( p, oter_sewer_isolated.id() ); sewer_points.emplace_back( p.xy() ); } } };
ter_set( p, oter_id( "bridge_road" + oter_get_rotation_string( oter_ground ) ) );   // :1171
generate_bridgeheads( bridge_points, oter_type_bridge, "bridgehead_ground", "bridgehead_ramp" ); // :1189
```

**Fix:**
```jsonc
// overmap_terrain
"vertical": { "above": "bridge_road", "supports_below_while_water": "bridge",
              "below": { "terrain": "sewer_isolated", "connect_with": "sewer_tunnel" } },
"bridgehead": { "ground": "bridgehead_ground", "ramp": "bridgehead_ramp" },
"flags": [ "RAMP" ]       // used by travel pathing
// region_settings_city
"manhole_chance": 2, "manhole_terrain": "road_nesw_manhole"
```

### OMAP-J14 · Docs drift: undocumented region and special fields, and a documented but ignored region_type
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctalk.cpp:8394`, `src/npctalk.cpp:8403`, `doc/JSON/EFFECT_ON_CONDITION.md:4865`, `doc/JSON/DIMENSIONS.md:71`, `doc/JSON/REGION_SETTINGS.md:387`, `src/regional_settings.h:353`, `src/regional_settings.cpp:531`, `src/regional_settings.cpp:546`, `src/overmap.cpp:3270`, `src/overmap_special.cpp:81`

1. `u_travel_to_dimension` parses the documented `region_type` and then drops it, because the lambda does not capture it.
2. `REGION_SETTINGS.md` does not document the river, ocean or ravine settings. Its highway table also omits `segment_ramp_special`, `fallback_bend_special`, `fallback_three/four_way_intersection_special` and `interchanges`.
3. `region_settings_highway` declares `symbolic_ramp_up_id`, `symbolic_ramp_down_id` and `symbolic_overpass_road_id`, but never loads them, and it reads `reserved_terrain_water_id` twice. The doc example still shows the `symbolic_*` members.
4. `JSON_FLAGS.md` lacks `OCEAN`, `OCEAN_SHORE`, `HIGHWAY_RESERVED`, `HIGHWAY_SPECIAL` and `SUBWAY`. `EXTRADIMENSIONAL` is documented, but only in `REGION_SETTINGS.md:622`.

```cpp
str_or_var region_type_var; optional( jo, false, "region_type", region_type_var, "default" );   // npctalk.cpp:8394
return [fail_message, success_message, dimension_prefix, npc_travel_filter, target_location, ...]  // :8403, no region_type_var
```

**Fix:** Either implement `region_type` (use a UNIFORM layout of that region when the dimension is first generated) or remove it from the docs. Document the river, ocean, ravine and missing highway fields. Delete the `symbolic_*` members and the duplicate read, and document the missing flags.

### OMAP-J15 · Special-placed camps always use a hardcoded camp type
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/overmap_special_fixed.cpp:283`, `src/overmap_special_mutable.cpp:820`, `src/overmap.cpp:1417`

Specials can pre-place NPC faction camps (`camp`, `camp_name`), but all three creation sites call `define_camp` with `faction_base_bare_bones_NPC_camp_0`, and the code carries a FIXME about it. That recipe is a deliberate NPC-only stub that provides a water well, which partly explains the choice. Still, a mod cannot place a farm or fortified camp blueprint. The z-level and BLOB problems in the same code are covered in OMAP-B11 and OMAP-B12.

```cpp
// FIXME? Camp types are raw strings! Not ideal.
temp_camp->define_camp( camp_loc, "faction_base_bare_bones_NPC_camp_0", false );   // overmap_special_fixed.cpp:282-283
```

**Fix:** Add `"camp_type": "<recipe id>"` next to `camp`/`camp_name`, validated in `check()`, and default it to the current recipe.

### OMAP-J16 · Overmap travel costs are hardcoded per-mode tables; nemesis movement matches OMT id substrings
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/overmapbuffer.cpp:983`, `src/overmapbuffer.cpp:1013`, `src/overmapbuffer.cpp:1036`, `src/overmap.cpp:1674`, `src/overmap.cpp:1765`, `src/overmap_terrain.cpp:611`, `src/omdata.h:338`

`travel_cost_type` is a closed enum, and its per-mode costs (walk, NPC, land vehicle, boat, aircraft) are C++ literals. Modders cannot add cost classes such as `mud`, `ice` or `rubble`. The nemesis horde ignores `travel_cost_type` and picks its move chance by substring of the OMT id (`road`, `field`, `forest`, `swamp`, ...). The horde signal constants (15 interest per signal power, minimum 30, 5-submap radius) are hardcoded too. Severity is low: mods can already assign any OMT to one of the 16 existing classes, and the nemesis exists only for the Hunted trait. Separately, `OVERMAP.md:322` leaves `highway` and `crop_field` out of its list of valid values.

```cpp
ret.set_cost( oter_travel_cost_type::swamp, 240 );                                       // overmapbuffer.cpp:987-1000
if( is_ot_match( "road", walked_into, ot_match_type::contains ) ) { movement_chance = 1; } // overmap.cpp:1675
const int inter_per_sig_power = 15; const int min_initial_inter = 30;                    // overmap.cpp:1765
```

**Fix:**
```jsonc
{ "type": "overmap_travel_cost", "id": "swamp", "walk": 240, "npc": 240, "land_vehicle": -1,
  "offroad_scaled": true, "watercraft": -1, "aircraft": -1, "horde_move_chance": 6 }
```
Make `travel_cost_type` a `string_id` of this type, use `horde_move_chance` in `move_nemesis`, and move the horde signal constants into a global JSON settings object. Add the missing values to the doc.

## 2. Bugs, oversights & quirks

### OMAP-B01 · place_special_attempt swaps must_be_unexplored and force; mission-spawned specials overwrite explored map
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/overmap.cpp:3190`, `src/overmap.h:957`, `src/overmapbuffer.cpp:2391`, `src/overmap.cpp:2516`, `src/overmap_special_fixed.cpp:318`, `src/overmap_special_mutable.cpp:780`, `src/overmap_special_mutable.cpp:912`

The signature is `place_special(special, p, dir, cit, must_be_unexplored, force)`, but `place_special_attempt` passes `(…, false, must_be_unexplored)`. The only caller that sets `must_be_unexplored=true` is `overmapbuffer::place_special`, which missions and EOCs reach through `create_if_necessary`. On that path the special is placed with `must_be_unexplored=false, force=true`. `lay_out_connection` refuses to route through already-generated OMTs only when `must_be_unexplored` is set, so the special's road can now cross explored terrain. The overmap then shows road while the real submaps still hold the old buildings or forest. `random_special_rotation` still checks the required locations, so only connections and non-root mutable pieces are affected. Mutable specials ignore the flag for every piece except the root anyway (TODO at `overmap_special_mutable.cpp:780`). A forced `OVERMAP_UNIQUE`/`GLOBALLY_UNIQUE` placement also uses up a deck slot.

```cpp
place_special( special, p, rotation, nearest_city, false, must_be_unexplored );   // overmap.cpp:3190
om->place_specials_pass( batch, sectors_in_range, true, true );                   // overmapbuffer.cpp:2391
```

**Fix:** Call `place_special( special, p, rotation, nearest_city, must_be_unexplored, false )`. Pass the flag through `satisfy()` so that `mutable_overmap_phase_remainder::can_place` rejects already-generated OMTs. Skip `consume_deck_placement` on the mission path.

### OMAP-B02 · find_closest can return a farther match, and its om_special branch is not "closest"
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/overmapbuffer.cpp:1407`, `src/overmapbuffer.cpp:1415`, `src/overmapbuffer.cpp:1420`, `src/overmapbuffer.cpp:1359`, `src/overmapbuffer.cpp:1370`, `src/overmapbuffer.cpp:1395`

(a) The terrain branch measures 3D Chebyshev distance over z −10..10. When it finds a strictly closer match, it lowers `found_dist` but does not clear `result`, so `random_entry` can still return the earlier, farther match. For example, with a match at z−3 in ring 1 (distance 3) and one at z0 in ring 2 (distance 2), each is returned half the time. (b) The `om_special` branch returns as soon as any overmap ring has a hit. A hit in ring 0 can be 170 OMTs away while an instance just across the border, in ring 1, is 2 OMTs away. About 163 mission definitions use `om_special`. The branch also ignores `min_z`/`max_z`, never generates overmaps, and uses `<` where the terrain branch uses `<=`. Today's only caller passes `existing_only=true` and no z-bounds, so those three gaps are latent.

```cpp
if( found_dist < dist ) { continue; }                 // overmapbuffer.cpp:1415-1422
if( is_findable_location( loc, params ) ) { found_dist = dist; result.push_back( loc ); }  // result never cleared
```

**Fix:** (a) When `dist < found_dist`, call `result.clear()` before `push_back`. (b) Keep scanning rings until the ring's minimum OMT distance exceeds the best match found, then pick the minimum. Apply the z filter, use `<= max_dist`, and use `get()` instead of `has()` when `!existing_only`. See OMAP-P02 for the cost of the same function.

### OMAP-B03 · find_camp is fuzzy and asymmetric, so placing a special's camps mutates the wrong camp
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/overmapbuffer.cpp:1575`, `src/overmap_special_mutable.cpp:809`, `src/overmap_special_fixed.cpp:271`, `src/overmap.cpp:1399`, `src/basecamp.cpp:207`, `src/savegame.cpp:739`

`overmapbuffer::find_camp(p)` scans x and y in `[p-3, p+2]` across every loaded overmap and returns the first camp it hits. It does not look up `p` exactly. Both special-placement paths call `add_camp(camp_loc)` and then `find_camp(camp_loc)` to set the owner and name and call `define_camp`. The vanilla mutable Isherwood farm places camps at (0,0) and (−1,1) in the same chunk. Under south or west rotation, the second camp's lookup returns the first camp, which is then renamed and has its `omt_pos` moved to the second tile. The real second camp is left with no owner, name or definition. After a reload, `savegame.cpp:739` re-keys camps by `omt_pos`, so the two collide, a debugmsg fires and one camp is lost.

```cpp
for( int x2 = p2.x() - 3; x2 < p2.x() + 3; x2++ ) {             // overmapbuffer.cpp:1579
    for( int y2 = p2.y() - 3; y2 < p2.y() + 3; y2++ ) { ... return camp;
get_map().add_camp( camp_loc, "faction_camp", false );            // overmap_special_mutable.cpp:811
std::optional<basecamp *> bcp = overmap_buffer.find_camp( camp_loc.xy() );
```

**Fix:** Make `find_camp(p)` an exact lookup on the overmap that owns `p` (`overmap::find_camp` is already an exact map lookup). Keep the radius search as `find_camp_near(p, r)` with a symmetric, inclusive range that returns the nearest camp. In special placement, have `map::add_camp` return the `basecamp*` it created. See also OMAP-P13.

### OMAP-B04 · Urbanity city-size adjustment is dead code (std::min instead of std::max); max_urbanity does nothing
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/overmap_city.cpp:75`, `src/overmap_city.cpp:78`, `src/overmap_city.cpp:84`, `src/regional_settings.cpp:640`, `doc/JSON/REGION_SETTINGS.md:24`

`city_size_adjust = min(urbanity - forestosity/2, 2 - op_city_size)`. For any `op_city_size >= 2`, the adjusted size is at most 2, and it is then clamped back up to `op_city_size`. Urbanity therefore never grows cities, forestosity never shrinks them, and the documented `max_urbanity` (a city-size multiplier) has no effect. The default region's `urbanity_increase [0,10,5,0]` was meant to make cities bigger to the east and south. The code comment ("never high enough to drop op_city_size below 2") shows that a floor, `std::max`, was intended. The same kind of clamp raises `city_spacing 1` to at least 2.

```cpp
int city_size_adjust = std::min( urbanity - static_cast<int>( forestosity / 2.0f ), -1 * op_city_size + 2 );
int max_city_size = std::min( op_city_size + city_size_adjust, op_city_size * max_urbanity );
if( max_city_size < op_city_size ) { max_city_size = op_city_size; }
```

**Fix:** Use `std::max( urbanity - forestosity/2, 2 - op_city_size )`, and drop the clamp-back (or apply it only to positive adjustments). Clamp `city_space_adjust` to `max(0, …)`. Generated city sizes will change, so treat this as a deliberate balance change.

### OMAP-B05 · pick_random_building_to_place can loop forever; empty building bins crash
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/overmap_city.cpp:268`, `src/overmap_city.cpp:277`, `src/regional_settings.cpp:924`, `src/weighted_list.h:133`, `src/weighted_list.h:150`, `src/regional_settings.cpp:799`

The `do/while` keeps redrawing from a bin until it gets a building that is not an already-placed unique and whose `city_sizes` contains `town_size`. The loop has no retry cap. If nothing in the bin qualifies (e.g. a modded region whose shops all have `city_sizes [12,-1]`, or a bin of only placed uniques), worldgen hangs with no message. `building_bin::pick()` dereferences `weighted_list::pick()`, which returns `nullptr` for an empty or zero-weight bin. Parks are rolled by default, so a region with `"parks": []` crashes. For `OVERMAP_UNIQUE` buildings, the loop also calls `overmap_buffer.contains_unique_special`, which only tracks `GLOBALLY_UNIQUE`. An already-placed overmap-unique building keeps being picked, is rejected later, and uses up all 10 retries. Vanilla bins are safe, so this needs modded region data.

```cpp
do { ret = pick_building( city_spec ); ...                                       // overmap_city.cpp:268-277
} while( existing_unique || !ret->get_constraints().city_size.contains( town_size ) );
return *buildings.pick();                                                        // regional_settings.cpp:924
```

**Fix:** Cap the attempts (e.g. 50), or pre-filter the bin by `town_size` and placed-unique state, and return an invalid id when nothing qualifies (`place_building` already handles failure). In `building_bin::pick`, return a null id when the bin is empty. Check `this->contains_unique_special(ret)` for `OVERMAP_UNIQUE`. Add the bin checks from OMAP-J05.

### OMAP-B06 · place_railroads indexes edges and neighbours in different direction orders, so rails dead-end at borders
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/overmap.cpp:2240`, `src/overmap.cpp:2254`, `src/overmap.cpp:2265`, `src/overmap.cpp:3740`, `src/point.h:438`

`neighbor_overmaps` is built in N,E,S,W order (from `four_adjacent_offsets`), but `place_railroads`' edge tables are laid out E,S,W,N. For `dir=0`, the code checks that the *north* neighbour is ungenerated and then places the exit on the *east* edge. Exits therefore face overmaps that already exist and will never connect back, so with the railroads mod enabled, lines end at overmap borders. The river-adjacency test is rotated in the same way and checks the wrong axis. `place_roads` gets this right with `get_border(dir, …)`. Severity is limited because railroads are enabled only by the railroads mod, which is labelled "Not intended for regular gameplay".

```cpp
// east == point( OMAPX - 1, n ); north == point( n, 0 )
static constexpr std::array<int, 4> edge_coords_x = {OMAPX - 1, -1, 0, -1};   // overmap.cpp:2240
if( neighbor_overmaps[dir] == nullptr ) {                                      // :2254 – N,E,S,W
```

**Fix:** Reuse the `place_roads` logic (`om_direction::all`, `get_border(dir, 0, 10)`, `turn_left/turn_right(dir)`). Alternatively, reorder the tables to N,E,S,W: `x={-1, OMAPX-1, -1, 0}`, `y={0, -1, OMAPY-1, -1}`.

### OMAP-B07 · Manual Voronoi region layout: Y negated for sites but not bounds; missing cells get an empty region
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/overmap_worldgen.cpp:71`, `src/overmap_worldgen.cpp:79`, `src/overmap_worldgen.cpp:83`, `src/overmap_worldgen.cpp:151`, `src/third-party/jc_voronoi/jc_voronoi.h:598`, `src/third-party/jc_voronoi/jc_voronoi.h:1535`

`generate_cata_voronoi_map` negates Y for every site to match jc_voronoi's axis, but passes the bounds to jcv unnegated. jcv culls sites against that rect. With Y-asymmetric bounds such as `[0,0]..[20,20]`, every site with y>0 is dropped and its cells are never filled. `get_overmap_region` then uses `placed_regions[om_point]`, which inserts an empty `region_settings_id`, and `generic_factory::obj` reports that as an invalid id. The documented example uses symmetric bounds, which hides the bug, and no vanilla data uses `MANUAL_VORONOI`. The diagram is also never freed: there is no `jcv_diagram_free` call.

```cpp
const jcv_point temp_point = { ...x(), -static_cast<float>( rv_point.region_point.y() ) };  // overmap_worldgen.cpp:71
const jcv_point bounds_min = { ...p_min.x(), ...p_min.y() };                              // :79 – not negated
return overmap_buffer.global_state.placed_regions[om_point];                              // :151
```

**Fix:** Build the rect as `{x_min, -y_max}`..`{x_max, -y_min}` and call `jcv_diagram_free(&diagram)`. In `get_overmap_region`, use `find()`, and on a miss fall back to the nearest site's region (or debugmsg).

### OMAP-B08 · City-distance rules and special road targets only see cities on the same overmap; open-ended minimums are never enforced
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/overmap.cpp:1255`, `src/overmap.cpp:3156`, `src/overmap_city.cpp:500`, `src/overmap_special.cpp:195`, `src/overmap_special.cpp:201`, `src/overmap_special_fixed.cpp:312`, `src/overmap_special_mutable.cpp:907`

`get_nearest_city` and `approx_distance_to_city` iterate only `this->cities`. `can_belong_to_city` therefore checks `city_distance`/`city_sizes` against the current overmap only. A wilderness special can spawn next to a large city just across the border, and specials that need a nearby city fail near borders. The reviewer found a worse bug: `requires_city()` is false for `city_distance [N,-1]` when `city_sizes.min` is 0. In that case `can_belong_to_city` returns true, and the minimum is never enforced, even against a city on the same overmap. This affects about 80 vanilla specials (e.g. Riverside Dwelling `[5,-1]`). Special road connections also target the nearest same-overmap city, but `lay_out_connection` charges 5× more for new tiles than for existing road, so in practice the route mostly follows existing roads.

```cpp
for( const city &elem : cities ) { const int dist = elem.get_distance_from( p ); ...   // overmap.cpp:1259-1264
if( cit ) { target = cit.pos; } else { target = om.get_fallback_road_connection_point(); } // overmap_special_fixed.cpp:313
```

**Fix:** Enforce `city_distance.min` whenever it is above 0, regardless of `city_sizes`. Add an overmapbuffer-level nearest-city query restricted to neighbour overmaps that already exist, and use it in `can_belong_to_city`. For connections, prefer the nearest existing road or `connections_out` point.

### OMAP-B09 · region_settings_city::finalize copy-paste clamps city_spacing into city_size
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/regional_settings.cpp:806`, `src/regional_settings.cpp:809`

When `city_spacing` is outside 0..8, `finalize` prints a debugmsg and then assigns the clamped *spacing* to `city_size`. A mod with `"city_spacing": 10, "city_size": 4` gets `city_size` 8, and its spacing stays at 10 and is used as-is.

```cpp
if( city_spacing > 8 || city_spacing < 0 ) { debugmsg( ... );
    city_size = std::clamp( city_spacing, 0, 8 ); }      // regional_settings.cpp:809
```

**Fix:** `city_spacing = std::clamp( city_spacing, 0, 8 );`

### OMAP-B10 · city::operator== treats all generated cities as equal; get_distance_from already subtracts size
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/city.h:46`, `src/overmap.cpp:1289`, `src/city.cpp:103`, `src/overmap_city.cpp:494`, `src/start_location.cpp:113`

`operator==` returns true if the ids match *or* the `database_id`s match. Every generated city has a null id and `database_id` 0, so any two of them compare equal. The `target_city` filter in `find_random_omt` therefore does nothing. That is latent today, because the only caller never passes it. Separately, `get_distance_from` already returns `max(dist - size, 0)`, but `start_location::can_belong_to_city` and the legacy `distance_to_city` path subtract `size` again. In `start_location` this always produces a negative value, so any start location with `city_distance.min >= 0` under `SELECT_STARTING_CITY` fails. No shipped start location sets that.

```cpp
return id == rhs.id || database_id == rhs.database_id || ( pos_om == rhs.pos_om && pos == rhs.pos );  // city.h:46
return std::max( static_cast<int>( trig_dist( p, tripoint_om_omt{ pos, 0 } ) ) - size, 0 );            // city.cpp:105
```

**Fix:** Compare `pos_om` and `pos`, and use `id`/`database_id` only when they are set. Rename `get_distance_from` to make clear that it measures to the city's edge, and remove the extra `- size` at the call sites.

### OMAP-B11 · Fixed-special camps are always created at z=0; worldgen NPC camps join the avatar's camp set
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/overmap_special_fixed.cpp:272`, `src/overmap_special_fixed.cpp:273`, `src/overmap_special_fixed.cpp:274`, `src/overmap_special_mutable.cpp:810`, `src/map.cpp:7749`, `src/basecamp.cpp:925`

The fixed path hardcodes z=0 for the camp ("pos() doesn't return z-level information"), even though `location.z()` is available. The mutable path uses `pos.z()`. The Exodii base defines its camp at `[1,2,1]` (`alien.json:61`), so the camp is created on the ground-level OMT. Both paths call `map::add_camp`, which also adds every worldgen NPC camp to the avatar's `camps` set. That set is deliberately used as the global camp registry (see the TODO at `basecamp.cpp:925`), and its consumers check ownership, so the second issue is minor.

```cpp
// This always results in z=0, but pos() doesn't return z-level information...
tripoint_abs_omt camp_loc = {project_combine( om.pos(), location.xy() ), 0};   // overmap_special_fixed.cpp:273
```

**Fix:** Use `location.z()`. Optionally create worldgen camps through `overmap_buffer.add_camp` (or give `add_camp` a `known_to_player` flag).

### OMAP-B12 · BLOB flag does nothing for mutable specials; fixed BLOB tiles are not recorded as part of the special
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/overmap_special_mutable.cpp:777`, `src/overmap_special_fixed.cpp:286`, `src/overmap_special_fixed.cpp:295`, `src/overmap.cpp:3062`, `data/json/overmap/overmap_mutable/crater.json:10`

The only vanilla BLOB special (Crater) is mutable, and mutable `place()` discards the `blob` argument. The flag therefore only works as a region whitelist/blacklist tag; Crater's shape comes from its phases. In fixed specials, blob spread calls `ter_set` on extra tiles but never adds them to `result.omts_used`. Those tiles get no `overmap_special_placements` entry, no special-scoped mapgen args and no `SAFE_AT_WORLDGEN`, and `om_special` searches do not find them. The spread radius (2) and falloff `1/(1+|x|+|y|)` are also hardcoded.

```cpp
overmap &om, const tripoint_om_omt &origin, om_direction::type dir, bool /*blob*/,   // overmap_special_mutable.cpp:777
elem.can_be_placed_on( om.ter( nearby_pos ) ) ) { om.ter_set( nearby_pos, tid ); }    // overmap_special_fixed.cpp:294 – no omts_used push
```

**Fix:** Add blob tiles to `result.omts_used`. Either implement blob for mutable specials or debugmsg in `check()` when BLOB is set on one. Optionally replace the flag with `"blob": { "radius": 2, "falloff": 1.0 }`, honored by both subtypes.

### OMAP-B13 · create_custom_overmap skips fix_mongroups/fix_npcs
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/overmapbuffer.cpp:124`, `src/overmapbuffer.cpp:100`, `src/overmapbuffer.cpp:137`, `src/overmap.cpp:3875`, `src/overmap.cpp:3381`

`add_mon_group(group, radius)` deliberately creates groups outside the overmap's bounds and relies on overmapbuffer to move them afterwards. `get()` calls `fix_mongroups`/`fix_npcs` after `populate()`, but `create_custom_overmap` (used to place mandatory specials in a neighbouring overmap) does not. Spilled groups stay in its `zg`. Because `zg` is keyed by position modulo the overmap, those monsters spawn at the wrapped position on the opposite edge until a reload fixes them. `get()` has a similar gap: `fix_mongroups` skips groups whose target overmap has not been generated yet.

```cpp
new_om.populate(); ... fix_mongroups( new_om ); fix_npcs( new_om );   // overmapbuffer.cpp:114-118 (get)
new_om.populate( specials );                                         // overmapbuffer.cpp:134 (create_custom_overmap)
```

**Fix:** Call `fix_mongroups(new_om)` and `fix_npcs(new_om)` after `populate` in `create_custom_overmap`. Re-run the fix for pending groups when their target overmap is generated.

### OMAP-B14 · overmap_location silently ignores unknown flags, so a typo yields a location that matches nothing
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/overmap_location.cpp:70`, `src/overmap_location.cpp:57`, `src/overmap_location.cpp:66`, `src/overmap_location.cpp:71`

`finalize()` maps each flag string to an `oter_flags` value and silently skips unknown names. `check()` validates only the explicit `terrains`. `{"flags": ["RIVR"]}` loads cleanly, matches nothing, and every special or connection that uses it quietly fails to place.

```cpp
auto it = oter_flags_map.find( elem ); if( it == oter_flags_map.end() ) { continue; }   // overmap_location.cpp:70-74
```

**Fix:** debugmsg on unknown flags, and warn when a location resolves to an empty terrain set.

### OMAP-B15 · Region terrain/furniture resolve loops until the result changes; self-reference or cycles hang mapgen
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/regional_settings.cpp:870`, `src/regional_settings.cpp:888`, `src/regional_settings.cpp:825`, `src/mapgen_functions.cpp:2255`

Both `resolve()` overloads keep replacing while any `ter_furn` entry matches the current result. A self-reference keeps rerolling until something else is picked, and a cycle (A→B, B→A) loops forever. An entry with an empty `replace_with_*` list dereferences the `nullptr` returned by `pick()`. `finalize()` is empty and there is no `check()`. Resolution only runs on `REGION_PSEUDO` tiles and no shipped data has a cycle, so in practice this is missing validation for modder mistakes.

```cpp
while( found_tfid != ter_furn.end() ) { ... result = *rtf_id->terrain.pick();          // regional_settings.cpp:879-884
    found_tfid = std::find_if( ter_furn.begin(), ter_furn.end(), predicate ); }
```

**Fix:** Bound the chain depth (e.g. 8) and stop when a replacement maps to itself. Add `region_terrain_furniture::check()` for empty lists and cycles. See OMAP-P14 for the same function's lookup cost.

### OMAP-B16 · Failed mutable special leaves a half-built structure and a player-visible DEBUG overmap note
**Severity:** Low · **Effort:** Medium · **Verified:** plausible
**Where:** `src/overmap_special_mutable.cpp:806`, `src/overmap_special_mutable.cpp:870`, `src/overmap_special_mutable.cpp:888`, `src/overmap_special_mutable.cpp:893`

Mutable placement writes each piece with `ter_set` as it goes. If joins are left unresolved, nothing is rolled back. The partial special stays on the map, a debugmsg pops up, and a permanent note "U:R;DEBUG: unresolved joins …" is saved to the player's overmap notes. Reachability in normal play is not proven. `can_place` already requires every mandatory join's neighbour to be in bounds and a valid location, and the code itself treats this failure as a JSON error.

```cpp
debugmsg( "Spawn of mutable special %s had unresolved joins. ..." );                         // overmap_special_mutable.cpp:888
om.add_note( p, string_format( "U:R;DEBUG: unresolved joins %s at %s placing %s", ... ) );
```

**Fix:** Stage pieces in a local map and commit them only on success, or retry another rotation or origin. Add the note only in debug mode.

## 3. Performance

### OMAP-P01 · required_locations()/get_terrains() return by value inside special-placement hot loops
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/overmap.cpp:3008`, `src/overmap.cpp:2967`, `src/overmap.cpp:2981`, `src/overmap.cpp:2959`, `src/overmap.cpp:2944`, `src/overmap.cpp:3158`, `src/overmap_special_fixed.cpp:231`, `src/overmap_special_fixed.cpp:250`, `src/overmap_special_mutable.cpp:761`, `src/omdata.h:713`, `src/overmap_highway.cpp:92`, `src/overmap_highway.cpp:957`

`can_place_special` is the innermost call of worldgen. It runs up to 4 rotations × every candidate special × 10 attempts × 144 sectors × 2 passes, plus every city building attempt. Each call copies the special's full location list. Every element contains a `cata::flat_set`, so each copy allocates. For mutable specials, `check_for_locations_area` expands to one entry per tile: 961 for `nether_monster_corpse`, 486 for Isherwood, 2178 for the mod ravine and canyon. A rejected rotation can therefore cost thousands of allocations before `all_of` fails on the first tile. The cheap terrain check also runs last. Before it come:

- string-keyed `has_flag` lookups
- building a dialogue and evaluating the EOC condition
- a (2r+1)² spawn-radius scan, with r up to 50
- a linear scan of `overmap_special_placements` for `OVERMAP_UNIQUE`

`score_rotation_at` also re-interns a `string_id` for each connection. The highway code makes the same by-value copy.

```cpp
const std::vector<overmap_special_locations> fixed_terrains = special.required_locations();  // overmap.cpp:3008
std::vector<overmap_special_locations> result; ... std::copy( terrains.begin(), ... );       // overmap_special_fixed.cpp:233
return check_for_locations;                                                                  // overmap_special_mutable.cpp:763
oter->get_type_id() == oter_type_str_id( con.terrain.str() )                                 // overmap_special_fixed.cpp:250
```

**Fix:** Change the virtual to `const std::vector<overmap_special_locations>& required_locations() const`. Fixed specials build the vector once in `finalize()`. Mutable specials return their member by reference. Do the same for `get_terrains()`. Reorder `can_place_special` to run bounds and terrain first, then the unique checks, the spawn radius, and the EOC last (or once per batch, see OMAP-J07). Cache flag booleans at load time. Compare `con.terrain` directly. Keep a per-overmap set of placed special ids. Hoist `std::set<int> priorities` out of `place_special_attempt`.

### OMAP-P02 · find_closest/find_all builds huge spiral vectors and string-compares each tile on all 21 z-levels
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/overmapbuffer.cpp:1404`, `src/overmapbuffer.cpp:1411`, `src/overmapbuffer.cpp:1267`, `src/overmapbuffer.cpp:1498`, `src/overmapbuffer.cpp:60`, `src/overmapbuffer.h:153`, `src/coordinates.h:1138`, `src/point.cpp:188`, `src/overmap.cpp:2740`, `src/overmap_terrain.cpp:292`, `src/mission_util.cpp:395`, `src/mission_util.cpp:237`, `src/npcmove.cpp:6046`, `src/mission_start.cpp:173`

Every mission, EOC location search (default `search_range` `OMAPX*3` = 540) and NPC goal search goes through these functions. `find_closest` first fills the whole spiral with `closest_points_first`: 1.17M points at radius 540, 3.24M at the radius-0 default of 900. It then copies the vector into a typed vector, so about 52 MB is allocated before the first test. Each xy point is then tested on every z from −10 to 10. Each test is `is_findable_location → check_ot → get_om_global → is_ot_match`, a `std::string` compare per wanted type, and the cheap `must_see` filter runs only after it. On a miss, one call costs tens of millions of string compares. With `existing_only=false` (the mission retry) it also generates every overmap in range, 49 at radius 540. The result is a multi-second hitch. NPC goal selection costs about 22.8k points × N types each time an NPC re-plans. `find_all` scans only the origin z but still builds the full spiral.

```cpp
for( const point_abs_omt &loc_xy : closest_points_first( origin.xy(), min_dist, max_dist ) ) {  // overmapbuffer.cpp:1404
    for( int z = params.min_z; z <= params.max_z; z++ ) {                                      // :1411
        if( is_findable_location( loc, params ) )                                              // :1419
return otype == oter.id().str();                                                               // overmap_terrain.cpp:296
std::vector<Point> raw_result = closest_points_first(...);  // coordinates.h:1138, then copied again
```

**Fix:**
1. Resolve the match once per query into a `std::vector<bool>` indexed by `oter_id::to_i()`, so each tile test becomes a bit lookup.
2. Replace the spiral vector with `find_point_closest_first(center, min, max, predicate)` (`point.h:490`, which allocates nothing), or iterate ring by ring grouped per overmap.
3. Test `seen`/`must_see` before the type match, and skip z-layers outside `[min_z, max_z]`.
4. Give `find_all` a plain per-overmap layer scan.

Expect a 20–100× speed-up on misses. Fix OMAP-B02 at the same time.

### OMAP-P03 · connect_closest_points pathfinds 10% of ALL O(n²) point pairs (not "rare" loops)
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/overmap.cpp:2658`, `src/overmap.cpp:2672`, `src/overmap.cpp:2719`, `src/overmap.cpp:2726`, `src/overmap.cpp:2499`, `src/overmap.cpp:1120`, `src/overmap.cpp:1988`, `src/overmap.cpp:2220`, `src/simple_pathfinding.cpp:72`

Sewers, subways, inter-city roads, railroads and forest-trail systems are joined by `connect_closest_points`. It builds a minimum spanning tree (MST) over all n(n−1)/2 edges. Every remaining edge then gets `one_in(10)` to become a loop, so about 0.1·n²/2 extra connections are built, including the longest pairs on the map. Each connection runs `greedy_path` over a 180×180 grid, which zero-fills about 200 KB of scratch buffers per call. A forest with 55 points gets about 54 tree paths plus about 140 loop paths. That contradicts the code comment's "rare extra connections" and produces spaghetti trail and sewer networks. The reviewer notes that `greedy_path` is pure best-first, so each search expands roughly path-length nodes. "Largest single worldgen cost" is therefore unproven; the issue is a layout quirk plus a moderate cost.

```cpp
for( size_t i = 0; i < points.size() - 1; ++i ) { for( size_t j = i + 1; ...; j++ ) { edges.push_back(...)  // overmap.cpp:2672
} else if( one_in( 10 ) ) { // ... making this connection creates a loop                                  // :2719
std::vector<bool> closed( map_size, false ); std::vector<int> open( map_size, 0 );                        // simple_pathfinding.cpp:72
```

**Fix:** Allow loop edges only among each point's k nearest neighbours (k≈3–4), or cap loops at about n/10. Reuse the `greedy_path` scratch buffers across calls. Layouts will change, so treat this as a tuning change.

### OMAP-P04 · Highway grid lake "guess" runs 8-octave noise on every tile of ~24 overmaps per new grid node
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/overmap_highway.cpp:1064`, `src/overmap_highway.cpp:1080`, `src/overmap_highway.cpp:1088`, `src/overmap.cpp:2073`, `src/overmap.cpp:2086`, `src/overmap_noise.cpp:29`, `src/overmapbuffer.cpp:1943`, `src/overmap_highway.cpp:984`

For each new highway intersection node, `generate_offset` calls `guess_has_lake` on 24 overmaps (radius 2). Each call scans all 32,400 OMTs with 8-octave simplex noise and stops early only after finding 20 lake tiles. Most overmaps have no lake, so a node costs about 6.2M `raw_noise_3d` calls. A new world creates about 12 nodes before the first overmap is generated, roughly 75M evaluations, which takes seconds. Another batch runs each time the player reaches a new grid cell. Grid spacing is 8–10 overmaps against a radius of 2, so the cache almost never hits across nodes.

```cpp
val_emplaced.first->second = !overmap::guess_has_lake( pt, lake_settings.noise_threshold_lake, lake_settings.lake_size_min );  // overmap_highway.cpp:1080
for( int i = 0; i < OMAPX && lake_tiles < max_tile_count; i++ ) { for( int j = 0; j < OMAPY; j++ ) {                      // overmap.cpp:2093
scaled_octave_noise_3d( 8, 0.5, 0.002, ...)                                                                                // overmap_noise.cpp:32
```

**Fix:** At frequency 0.002, lake features span hundreds of OMTs, so sample every 4th–6th OMT and scale the threshold to match. Optionally use fewer octaves for the guess. Build the noise layer once outside the loop. This is a 15–60× reduction.

### OMAP-P05 · must_be_unexplored checks hit disk (submap_exists → unserialize) per pathfinding node
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/overmap.cpp:2516`, `src/overmap.cpp:3018`, `src/overmap.cpp:3942`, `src/overmapbuffer.cpp:2391`, `src/mapbuffer.cpp:132`, `src/mapbuffer.cpp:365`, `src/mission_util.cpp:200`

Mission and EOC special placement runs with `must_be_unexplored=true`. In that mode, `can_place_special` calls `is_omt_generated` for every location on every rotation, before the cheap `ter()` check. The `lay_out_connection` scorer calls it for every node it expands. `is_omt_generated → MAPBUFFER.submap_exists` falls through to `unserialize_submaps` on a cache miss: string formatting plus `file_exist`, and with compression on, `zzip::load` and a footer parse, with no negative cache. One road connection can trigger thousands of filesystem operations. (OMAP-B01 currently stops the flag from reaching the connection, but the `can_place_special` path still pays the cost.)

```cpp
if( must_be_unexplored && !existing_connection ) {
    const bool existing_submap = is_omt_generated( tripoint_om_omt( cur.pos, z ) );   // overmap.cpp:2516
return MAPBUFFER.submap_exists( global_sm_loc );                                     // overmap.cpp:3952
```

**Fix:** Track generation on the overmap: a per-layer 180×180 "generated" bitset, set when mapgen finalizes an OMT and serialized with the overmap. A per-call memo also works. Do not use `submap_exists_approx` as the fix, because it also does file I/O.

### OMAP-P06 · Overmap notes/extras are unsorted vectors linearly scanned per tile per frame
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/overmap.cpp:511`, `src/overmap.cpp:525`, `src/overmap.cpp:556`, `src/overmap.cpp:604`, `src/overmap.cpp:635`, `src/overmap.cpp:649`, `src/overmapbuffer.cpp:2236`, `src/overmapbuffer.cpp:2262`, `src/sdltiles.cpp:3421`, `src/sdltiles.cpp:3541`, `src/overmap_ui.cpp:2599`, `src/overmap_ui.cpp:2617`, `src/panels.cpp:255`

`has_note`, `note`, `note_danger_radius`, `has_extra` and `extra` each scan the layer's vector linearly. `is_marked_dangerous` scans every note and computes `rl_dist` for each dangerous one. These run per drawn tile on every redraw in the tiles overmap, the curses overmap and the minimap, and once per expanded A* node in player travel. Every applied map extra and every auto-note adds an entry, so long games reach thousands. `get_notes`/`get_extras` call `om.note(p)` on all 32,400 tiles of every loaded overmap, even though `find_notes` already walks the vector directly.

```cpp
for( const om_note &i : layer[p.z() + OVERMAP_DEPTH].notes ) { if( i.p == p.xy() ) { return true; } }  // overmap.cpp:517
for( int i = 0; i < OMAPX; i++ ) { for( int j = 0; j < OMAPY; j++ ) { ... om.note( p );               // overmapbuffer.cpp:2241
```

**Fix:** Index each layer, with an `unordered_map<point_om_omt, size_t>` or per-layer `has_note`/`has_extra` bitsets. Precompute a per-layer danger bitset and rebuild it only when a dangerous note changes. Rewrite `get_notes`/`get_extras` to iterate the vectors.

### OMAP-P07 · Every save re-serializes and re-compresses ALL loaded overmaps; overmaps are never evicted
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/overmapbuffer.cpp:246`, `src/overmap.cpp:3747`, `src/overmapbuffer.cpp:254`, `src/overmapbuffer.cpp:261`, `src/game_io.cpp:592`, `src/game_io.cpp:891`

`overmapbuffer::save()` runs on every save, quicksave and autosave. It rewrites the seen file and fully serializes the terrain of every overmap in memory, with no dirty tracking: 21 RLE layers, all mongroups, map data and hordes. With compression on, each write appends a newly compressed copy to the zzip and periodically compacts it. Overmaps are removed only in `reset()`/`clear()`. Overmaps generated as side effects (searches, mandatory-special spillover) are therefore re-saved forever, and save time grows with distance travelled.

```cpp
for( auto &omp : overmaps ) { omp.second->save(); }               // overmapbuffer.cpp:248-251
std::stringstream s; serialize( s ); if( !z->add_file( terfilename_path, s.str() ) )   // overmap.cpp:3771
```

**Fix:** Add a terrain-dirty bit (`ter_set`, specials, mongroups, hordes, camps, npcs, vehicles) and a view-dirty bit (`set_seen`, explored, notes, extras) to each overmap, and skip clean overmaps in `save()`. Generated overmaps start dirty and loaded ones start clean. Optionally evict clean overmaps far from the player.

### OMAP-P08 · map_cache stores a 16-byte shared_ptr for every OMT on all 21 layers (~11 MB/overmap)
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/overmap.h:219`, `src/overmap.h:674`, `src/overmap.cpp:314`, `src/overmap.cpp:366`, `src/overmap.cpp:851`, `src/overmap_map_data_cache.cpp:49`, `src/savegame.cpp:1315`

Each overmap stores an `mdarray<std::shared_ptr<map_data_summary>>` per layer: 32,400 × 21 × 16 B = 10.9 MB, against 2.7 MB for the terrain itself. Nearly every entry is an aliasing pointer to a placeholder that can be derived from `terrain[p]->get_type_id()->default_map_data`. Overmaps are never evicted (OMAP-P07), so this reaches hundreds of MB after normal travel. The size hurts locality, load, save and generation, and the serializer walks all 680k entries. The finding JSON rates effort as large; the reviewer lowered it to medium because `map_cache` is touched in only about 8 places.

```cpp
cata::mdarray<std::shared_ptr<map_data_summary>, point_om_omt> map_cache;                       // overmap.h:219
l.map_cache.fill( std::shared_ptr<map_data_summary> { std::shared_ptr<void>(), &passable_omt } ); // overmap.cpp:323
```

**Fix:** Keep a 1–2 byte per-OMT placeholder index, plus a sparse `unordered_map<tripoint_om_omt, std::unique_ptr<map_data_summary>>` for real summaries. Cache the placeholder index on `oter_type_t` at finalize. This saves about 10 MB per overmap.

### OMAP-P09 · Overmap travel-path scorer does 4 overmap lookups, a std::map lookup and a note scan per node
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/overmapbuffer.cpp:1079`, `src/overmapbuffer.cpp:1093`, `src/overmapbuffer.cpp:1106`, `src/overmapbuffer.h:55`, `src/overmapbuffer.h:64`, `src/simple_pathfinding.cpp:433`, `src/overmapbuffer.cpp:1005`

For each node, the player travel scorer calls `seen_more_than`, `is_marked_dangerous`, `ter_existing` and `is_ramp → ter_existing`. That is four resolutions of the same tile's overmap, a linear note scan, and a `std::map` lookup on a small dense enum. Impact is modest. NPC travel disables the seen and danger checks. `get_existing` usually hits its one-entry cache. The search is capped at 100,000 nodes. The note scan is the main cost and is covered by OMAP-P06. Expect a gain of about 10–20%.

```cpp
if( params.avoid_danger && overmap_buffer.is_marked_dangerous( omt_pos ) ) ...   // overmapbuffer.cpp:1082-1090
std::map<oter_travel_cost_type, int> travel_cost_per_type;                       // overmapbuffer.h:55
```

**Fix:** Resolve the overmap once per node and read seen, terrain and danger from it. Derive the ramp flag from the same `oter_id`. Store the costs in a `std::array<int, N>` filled with −1.

### OMAP-P10 · oter_t::get_tileset_id builds strings with string_format per drawn tile
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/overmap_terrain.cpp:827`, `src/sdltiles.cpp:3287`, `src/sdltiles.cpp:3420`, `src/overmap_terrain.cpp:800`

The tiles overmap renderer calls `get_tileset_id(vision)` for every visible OMT. Below full vision this returns `string_format("vl#%s$%s", ...)`, otherwise `"om#" + id`, and the caller then makes two `substr` copies. The overmap redraws on input, not continuously, and each tile already does several other lookups, so the saving is a fraction of a millisecond per redraw.

```cpp
return string_format( "vl#%s$%s", type->vision_levels.str(), io::enum_to_string( vision ) );   // overmap_terrain.cpp:831
std::string id = ot_id->get_tileset_id( vision ); bool is_omt = id.substr( 0, 3 ) == "om#";   // sdltiles.cpp:3287
```

**Fix:** Precompute the id string for each vision level, without the prefix, in `oter_type_t::finalize`, and return a `const std::string&` together with an `is_omt` flag.

### OMAP-P11 · find_existing_globally_unique probes up to 40,401 overmap slots with filesystem stats
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/overmapbuffer.cpp:1430`, `src/overmapbuffer.cpp:1443`, `src/overmapbuffer.cpp:327`, `src/overmapbuffer.cpp:1229`, `src/mission_util.cpp:176`

Missions and EOCs that target a `GLOBALLY_UNIQUE` special spiral over `closest_points_first(center, 0, 100)`, which is 201² slots, calling `has()` on each. Each miss costs a `string_format`, `assure_dir_exist` and `file_exist`, and each saved overmap found is loaded in full. The TODO at :1435 notes that placement positions are not recorded. The search returns at the first match, so the full scan happens only on the failure path, which already ends in a debugmsg. The realistic cost is that saved overmaps closer than the special get loaded and stay resident.

```cpp
//TODO: Update the globally unique set to contain positions ...                       // overmapbuffer.cpp:1435
for( point_abs_om om : closest_points_first( center.xy(), 0, 100 ) ) { if( has( om ) ) {
```

**Fix:** Change `placed_unique_specials` to an `unordered_map<overmap_special_id, tripoint_abs_omt>` and serialize it, falling back to the scan for old saves. Move `assure_dir_exist` off the miss path.

### OMAP-P12 · Regional pseudo terrain/furniture resolve scans a std::set linearly for every mapgen tile
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/regional_settings.cpp:870`, `src/regional_settings.cpp:888`, `src/regional_settings.cpp:824`, `src/regional_settings.h:421`, `src/mapgen_functions.cpp:2248`

For every `REGION_PSEUDO` tile of each generated OMT (`t_region_groundcover` is used by 310 mapgen files), `resolve()` runs `find_if` over the 35-entry `ter_furn` set. After each replacement it scans again from the start. That is roughly 35–70 cached string_id checks per tile, on the order of 100+ µs per OMT.

```cpp
auto found_tfid = std::find_if( ter_furn.begin(), ter_furn.end(), predicate );   // regional_settings.cpp:876-884
```

**Fix:** In the empty `region_settings_terrain_furniture::finalize`, build lookup tables (`unordered_map<ter_id, const region_terrain_furniture*>` and the furniture equivalent, or vectors indexed by `to_i()`).

### OMAP-P13 · find_camp loops over every loaded overmap with 36 map lookups each
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/overmapbuffer.cpp:1575`, `src/overmap.cpp:3916`, `src/display.cpp:915`, `src/npcmove.cpp:4154`, `src/condition.cpp:1037`

Camps live only in the overmap that contains them, but `find_camp` does 36 `std::map::find` calls on every loaded overmap. It is called on sidebar redraws (`display::faction_text`), from NPC camp AI (inside a 30-radius chair search, where the call is loop-invariant) and from EOC conditions like `u_near_om_location`, once per point in radius. Most camp maps are empty, so each call is cheap. The more serious correctness problem with the same function is OMAP-B03.

```cpp
for( auto &it : overmaps ) { ... for( int x2 = p2.x() - 3; x2 < p2.x() + 3; x2++ ) { ...   // overmapbuffer.cpp:1577
```

**Fix:** Query only the at most 4 overmaps that intersect the window, using `get_existing`. Hoist the call out of the NPC chair loop.

### OMAP-P14 · Redundant full-overmap scans during generation (lake/ocean presence, all 10 sublevels)
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/overmap.cpp:3247`, `src/overmap.cpp:3254`, `src/overmap.cpp:1037`, `src/overmap.cpp:1058`, `src/overmap.cpp:1066`, `src/overmap.cpp:1111`

`place_specials` scans all 21 layers × 32,400 tiles twice, once for lakes and once for oceans. On a no-water overmap that is about 1.36M checks, even though `place_lakes`/`place_oceans` already know the answer. `generate_sub` is forced to run on all 10 sublevels, and on each one it rebuilds an `unordered_map<oter_type_id, std::function>` and walks every tile.

```cpp
for( int z = -OVERMAP_DEPTH; z <= OVERMAP_HEIGHT && !overmap_has_lake; z++ ) { ...is_lake();   // overmap.cpp:3247
//TODO: This forces every sub level to generate...                                            // :1057
```

**Fix:** Set `has_lake`/`has_ocean` flags in `place_lakes`/`place_oceans`, and build the action table once. Do not stop descending sublevels early: specials place `microlab_sub_connector` at z=−4, so deep levels still need a scan unless connector positions are recorded when the specials are placed.

### OMAP-P15 · place_swamps allocates a spiral vector per river tile and string-matches every tile
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/overmap.cpp:2116`, `src/overmap.cpp:2120`, `src/overmap.cpp:2122`, `src/overmap.cpp:2144`, `src/overmap.cpp:2150`, `src/overmap.cpp:2155`

For each tile, `place_swamps` runs `is_ot_match("river", …, contains)`, a substring search; a TODO already says to use `is_river`. For each river tile it builds `closest_points_first` with radius up to 15 (up to 961 points, built twice by the coordinate wrapper) just to increment counters. The second pass does a substring search for `forest` and can evaluate the floodplain noise twice at the same point.

```cpp
if( is_ot_match( "river", ter_unsafe( pos ), ot_match_type::contains ) ) {                // overmap.cpp:2120
    std::vector<point_om_omt> buffered_points = closest_points_first( pos.xy(), rng( ... ) );
```

**Fix:** Use `ter->is_river()` and a flag-based forest test. Replace the spiral with a clamped nested x/y loop, and compute the noise once per tile.

### OMAP-P16 · place_cities removes nearby candidates with std::find + vector erase
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/overmap_city.cpp:127`, `src/overmap_city.cpp:188`, `src/overmap_city.cpp:190`, `src/overmap_city.cpp:192`

`city_candidates` holds about 20–27k points. For each placed city, each of the 25 points within radius 2 is located with `std::find` over the whole vector (a miss is common, so this is a full scan) and then erased, which shifts the tail. With about 9 cities per overmap this costs a few ms, and more at low city spacing.

```cpp
for( const tripoint_om_omt &remove_radius : points_in_radius( selected_point, 2 ) ) {   // overmap_city.cpp:190
    auto removed_point = std::find( city_candidates.begin(), city_candidates.end(), remove_radius );
    if( removed_point != city_candidates.end() ) { city_candidates.erase( removed_point ); }
```

**Fix:** Keep a 180×180 "blocked" bitset and reject blocked candidates when drawing, or use swap-and-pop with an index map.

## Appendix: considered and rejected

- Horde entities migrating into a non-existent overmap are deleted (with a debugmsg) – Before an entity migrates, `passable()` (`overmap.cpp:1588`) checks its candidate squares, and `passable()` returns false when no overmap exists there. Overmaps are never evicted mid-turn, so the debugmsg branch cannot be reached and no monsters are lost.
- `get_settings` re-runs region-layout generation (RANDOM layout consumes RNG) on every call – Every caller is cold: once per OMT in mapgen, cached per turn for weather, hourly in `game.cpp`. No shipped data uses a RANDOM or ANGLES layout. `emplace` never overwrites, and `placed_regions` is saved, so the extra RNG draw has no visible effect.
