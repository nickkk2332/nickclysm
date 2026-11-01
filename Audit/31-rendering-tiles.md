# 31 · Rendering, tilesets & audio backend

> Engine audit – static code reading at commit `a1a646d6`; nothing was compiled or run. Each finding was checked against the code by a separate reviewer: **confirmed** = verified by reading the code; **plausible** = consistent with the code, but reachability or impact not proven. Line numbers refer to that commit.

**Scope:** `src/sdltiles.cpp`, `src/cata_tiles.cpp`, `src/tileset_loader.cpp`, `src/cursesport.cpp`, `src/output.cpp`, `src/sdl_wrappers.cpp`, `src/sdl_font.cpp`, `src/sdl_utils.cpp`, `src/sdl_quad_batch.cpp`, `src/pixel_minimap.cpp`, `src/smooth_lighting.cpp`, `src/cata_shader.cpp`, `src/animation.cpp`, `src/overlay_ordering.cpp`, `src/color.cpp`, `src/font_loader.cpp`, `src/sdlsound.cpp`, `src/sound_backend_sdl3.cpp`

## Summary

The tiles renderer works for the common case, but the tileset data pipeline has several latent correctness holes. The worst are a use-after-free when the loader drops invalid tiles, and an ortho-mode map-memory region that silently discards most of what the player sees north of the screen. Many tileset JSON features (`field_variants`, `overlay_ordering`, weighted variants, `animated`, `transparency`, per-atlas `pixelscale`) are either broken or only partly honoured. On the performance side, the per-frame draw loop resolves every sprite by hashing and concatenating strings, and repeats per-tile creature lookups (up to ~10 per outdoor tile for shadows). Several load-time paths decode or scan every atlas pixel more than once. The SDL3 music path also mutates main-thread state from the audio thread.

| Lens | Critical | High | Medium | Low | Total |
|---|---|---|---|---|---|
| Missing JSON / EOC | – | – | – | – | – |
| Bugs & quirks | 0 | 2 | 5 | 9 | 16 |
| Performance | 0 | 2 | 11 | 6 | 19 |

**Fix first:**
1. **REND-B02** · Tileset load leaves dangling pointers in `tile_ids_by_season`: real use-after-free from a malformed or mod tileset, and the fix is a small rebuild pass.
2. **REND-B01** · Ortho map-memory region uses `max_mm_reg.y`: a one-token fix that restores memorization of 20-30 rows of seen terrain in default tiles mode.
3. **REND-P01** · Flying-creature shadow check does up to 10 `creature_at()` per outdoor tile per frame: the largest per-tile critter cost, with a local fix.
4. **REND-B03** · `field_variants` layering hides unlisted fields and can divide by zero: it can hide hazards such as fire or acid, and the fixes are small.
5. **REND-P02** · Every sprite draw resolves its tile by string hashing and concatenation: this is the core per-frame cost, and a resolution cache benefits every layer.

## 1. Missing JSON bindings & EOC features

_Not audited for this system._

## 2. Bugs, oversights & quirks

### REND-B01 · Ortho tiles: map-memory region top uses `max_mm_reg.y`, so seen terrain north of the screen is never memorized
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/cata_tiles.cpp:705`, `src/cata_tiles.cpp:706`, `src/cata_tiles.cpp:722`, `src/cata_tiles.cpp:1512`, `src/map_memory.cpp:280`, `src/map_memory.cpp:438`

In the non-isometric branch, the north edge of the prepared map-memory region is `std::min( max_mm_reg.y, northwest->y )` instead of `min_mm_reg.y`. When the screen shows fewer rows than the reality bubble (normal zoom on most monitors), the region therefore starts at the screen's top row, not the bubble's (`min_visible.y`). The post-draw sweep (1503-1547) is meant to memorize everything the character saw, including off-screen tiles, and walks `min_visible..max_visible`. Writes outside the prepared region go to the shared static `null_mz_submap` and are lost, and the decoration dirty bit is still cleared (1541). In tiles mode `map::draw` returns early (`map.cpp:7934`), so nothing else widens the region. In open daylight at default zoom, roughly 20-30 of the 60 sight rows north of the player are never memorized, while the same rows to the south are. The iso branch (692-693) and the curses path (`map.cpp:7952-7959`) compute the region correctly.

```cpp
// cata_tiles.cpp:705-706
min_mm_reg = point( std::min( min_mm_reg.x, northwest->x ),
                    std::min( max_mm_reg.y, northwest->y ) );   // should be min_mm_reg.y
// map_memory.cpp:442-446: out-of-region -> return null_mz_submap;
```

**Fix:** Change it to `std::min( min_mm_reg.y, northwest->y )` to mirror the iso branch. Optionally, always union the full bubble (`min_visible..max_visible`) into the region before calling `prepare_map_memory_region`.

### REND-B02 · Tileset load erases invalid `tile_ids` entries but leaves dangling pointers in `tile_ids_by_season` (use-after-free)
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/tileset_loader.cpp:467`, `src/tileset_loader.cpp:475`, `src/cata_tiles.cpp:361`, `src/cata_tiles.cpp:391`, `src/cata_tiles.cpp:398`, `src/cata_tiles.cpp:2458`, `src/cata_tiles.cpp:3110`

`create_tile_type` stores raw pointers into `tile_ids` nodes in `tile_ids_by_season` (`default_tile = &inserted_tile`, `season_tile = tile_lookup_res(...)`). After parsing, the loader erases every tile whose fg/bg ended up empty: sprite indices out of range or negative, or a multitile parent defined with only `additional_tiles`. The matching season entries are never removed or rebuilt. Draw lookups go only through `tile_ids_by_season` (`find_tile_with_season`), so they return a reference to freed memory. `draw_from_id_string_internal` then reads `multitile`, fg/bg, offsets and the id, and iterates the freed `available_subtiles` vector (3110-3113). Any tileset or mod tileset with a bad sprite index or a base-less multitile triggers undefined behaviour on every draw of that id (wrong sprites or a crash). `find_tile_type()` correctly returns null for the same id, so the two lookups disagree. Neither bundled tileset is known to trigger it.

```cpp
// tileset_loader.cpp:473-475
if( td.bg.empty() && td.fg.empty() ) {
    dbg( D_ERROR ) << "tile " << it->first << " has no (valid) foreground nor background";
    ts.tile_ids.erase( it++ );
// cata_tiles.cpp:398
by_season_map[id].default_tile = &inserted_tile;
// cata_tiles.cpp:361
return tile_lookup_res( iter->first, *res.default_tile );
```

**Fix:** After the erase pass, rebuild `tile_ids_by_season` from the surviving `tile_ids` (clear the four maps and re-run the suffix bucketing). Alternatively, erase the matching base and `_season_*` keys whenever a `tile_ids` entry is erased. See also REND-P19, which proposes a single-map layout built after cleanup.

### REND-B03 · `layering.json` `field_variants` broken: shadowed `fld`, unconditional `return true` hides unlisted fields, flag contexts ignored, divide-by-zero
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/cata_tiles.cpp:3926`, `src/cata_tiles.cpp:3931`, `src/cata_tiles.cpp:3942`, `src/cata_tiles.cpp:3946`, `src/cata_tiles.cpp:3962`, `src/cata_tiles.cpp:3983`, `src/cata_tiles.cpp:4022`, `src/cata_tiles.cpp:4057`, `src/cata_tiles.cpp:2946`, `src/tileset_loader.cpp:690`, `src/tileset_loader.cpp:698`

The `draw_layer_field` lambda captures the outer `fld` (the tile's displayed field, 3926), not the per-field loop variable at 4022 that shadows it. It also returns `true` whenever the furniture or terrain has any `field_variants`, matched or not. Consequences:
- (a) On layered furniture or terrain, any field not listed is never drawn, because the caller skips the default draw. For example, layering `f_desk` with only `fd_fire` makes acid, smoke or electricity on desks invisible, which hides hazards.
- (b) With two fields on a tile, the displayed field's sprite is drawn once per field, and the other field's variant is never used.
- (c) Only furniture and terrain ids are tried, so the documented flag `context` (TILESET.md:585) works for items but not for fields.
- (d) The sprite roll hashes bubble coordinates, so the chosen sprite changes on every map shift. `"sprite": []` or zero/negative weights give `total_weight <= 0` and raise SIGFPE at `% total_weight` (3946, and the item path at 3983).
- (e) If a layering sprite is missing, the FIELD fallback builds `field_type_id( found_id )` from a non-field id with no validity check, so `generic_factory` raises a debugmsg every frame.

```cpp
3926: const field_type_id &fld = fld_overridden ? ... : f.displayed_field_type();
3942: if( fld.id().str() == layer_var.id ) {   // outer fld, not the loop's
3946: int roll = simple_point_hash( p.xy().raw() ) % layer_var.total_weight;
3962: return true;                              // matched or not
4022: const field_type_id &fld = fd_pr.first;   // shadows; lambda never sees it
2946: const field_type_id fid = field_type_id( found_id );
```

**Fix:** Pass the `field_type_id` into `draw_layer_field` as a parameter, and return `true` only when a variant matched and drew. Try furniture/terrain flags as the item path does (4116-4126). Seed the roll with `here.get_abs( p )`. In `load_layers`, reject empty sprite arrays and weights `<= 0`. Guard the FIELD fallback with `field_type_str_id( found_id ).is_valid()`, as the other categories do.

### REND-B04 · Idle tile animation: signed/unsigned modulo skips a frame on about half of tiles; a bg-only animated tile divides by zero
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/cata_tiles.cpp:2754`, `src/cata_tiles.cpp:2756`, `src/cata_tiles.cpp:2757`, `src/cata_tiles.cpp:2763`, `src/cata_tiles.cpp:2780`, `src/cata_tiles.cpp:3130`, `src/weighted_list.h:287`

`animation_frame` is an `int`, and adding the 32-bit position hash wraps it negative for roughly half of map positions. The negative remainder of `% frames_in_loop` is stored into the unsigned `loc_rand` as `2^32 - k`. `pick_ent` then computes `randi % total_weight`, so for any non-power-of-two total weight one slot (`2^32 mod W`) is never shown and slot 0 shows twice. A 3-frame animation never shows its middle frame on those tiles. Separately, `frames_in_loop` falls back to the bg weight only when the fg weight is exactly 1. A bg-only animated tile (fg weight 0) passes the `bg.size() > 1` guard (3130) and divides by zero. The portrait path (`get_texture_draw_data`, 2780) has no guard at all, so even a single-fg, no-bg animated tile hits `% 0` there. The `animated` field is not documented in `doc/TILESET.md`.

```cpp
2754: int animation_frame = value.count() / 17;
2756: animation_frame += loc_rand;
2757: int frames_in_loop = display_tile.fg.get_weight(); if( frames_in_loop == 1 ) { ...bg... }
2763: loc_rand = animation_frame % frames_in_loop;
```

**Fix:** Do the math unsigned: `const uint32_t frame = static_cast<uint32_t>( value.count() / 17 ) + loc_rand;`. Use `frames_in_loop = std::max( fg.get_weight(), bg.get_weight() )`, return early if it is 0, and set `loc_rand = frame % frames_in_loop`. Document `animated` in TILESET.md.

### REND-B05 · Weighted sprite variants never vary for items, traps, players/NPCs and overlays (seed 0); NPC seeding branch is dead code
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/cata_tiles.cpp:2629`, `src/cata_tiles.cpp:2681`, `src/cata_tiles.cpp:2689`, `src/cata_tiles.cpp:2699`, `src/cata_tiles.cpp:2705`, `src/cata_tiles.cpp:4700`, `src/cata_tiles.cpp:4733`, `src/weighted_list.h:287`

TILESET.md ("Random variations") documents weighted fg/bg variants for any id. `get_variant_seed` leaves `seed = 0` for ITEM, TRAP, BULLET, HIT_ENTITY and for NONE ids other than graffiti. The mix of 0 is 0, so `pick()` always returns the first entry. Players, NPCs and every worn, wielded or mutation overlay are drawn as `TILE_CATEGORY::NONE` (4700, 4733). The explicit `case NONE:` handles only graffiti, so the `default:` branch that seeds `player_*` and `npc_*` is unreachable. A tileset with three weighted `npc_male`, `rock` or trap variants shows only variant 1. Monsters are seeded from the raw `monster*` address (FIXME at 2700), so their variant re-rolls after save/load or unload/reload. The same code exists upstream.

```cpp
2681: case TILE_CATEGORY::NONE: if( found_id == "graffiti" ) {...} ... break;
2689: case TILE_CATEGORY::ITEM: case TILE_CATEGORY::TRAP: ... // TODO ... break;
2705: default: if( string_starts_with( found_id, "player_" ) ) ... "npc_" ...
2702: seed = reinterpret_cast<uintptr_t>( creatures.creature_at<monster>( pos ) );
```

**Fix:** Move the `player_`/`npc_` checks into the NONE case before the graffiti test. Seed ITEM and TRAP from the absolute position (or the top item's `seed`). For monsters, derive a stable seed, e.g. a persisted random `sprite_seed`.

### REND-B06 · Music "finished" callback runs on the SDL3_mixer audio thread and mutates main-thread music state (race, double free, `cata_fatal`)
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/sound_backend_sdl3.cpp:117`, `src/sound_backend_sdl3.cpp:586`, `src/sdlsound.cpp:400`, `src/sdlsound.cpp:440`, `src/sdlsound.cpp:474`, `src/sdlsound.cpp:478`, `src/music.cpp:75`, `src/do_turn.cpp:555`

`music_stopped_cb` fires on the mixer's audio thread (the file says so at 53-58) and calls `musicFinished` synchronously. On that thread, `musicFinished` stops and frees `current_music` and reads `music::music_id_list` (a `std::map` whose `operator[]` can insert). It also reads and writes `current_playlist`, `playlist_indexes` and `absolute_playlist_at`, calls `play_music` (which does an unlocked `g_music_generation_tokens.emplace_back`), and loads files with `MIX_LoadAudio` while the mixer is locked. Meanwhile the main thread calls `play_music()` every turn (`do_turn.cpp:555`), `stop_music()`, and `update_music_volume()`, which runs `deactivate_music_id_all`. The concrete hazards:
- Both threads free `current_music` before either nulls it, which is a double free.
- The `current_playlist` string is assigned on one thread and compared on the other with no synchronization.
- `deactivate_music_id_all` clears every flag before re-activating title. If the audio thread calls `get_music_id` inside that window, it hits `cata_fatal("At least one music_id must remain activated")`.

The fork already defers one-shot cleanup to `poll()` for exactly this reason. Separately, `musicFinished` never updates `current_music_track_volume`, so a volume change mid-playlist uses the first track's volume. The harmful interleavings need a track ending at the same moment as a playlist switch, which is why this is medium.

```cpp
// sound_backend_sdl3.cpp:117-125
void music_stopped_cb(...) { ... if( g_music_finished_cb != nullptr ) { g_music_finished_cb(); } }
// sdlsound.cpp:406-414 (audio thread)
sound_backend::stop_music( 0 ); sound_backend::free_music( current_music ); current_music = nullptr;
// sdlsound.cpp:484-486 (main thread)
sound_backend::free_music( current_music ); sound_backend::stop_music( 0 ); current_music = nullptr;
```

**Fix:** Have `music_stopped_cb` only set a `std::atomic<bool> music_finished_pending` (after the generation check). In `sound_backend::poll()`, which already runs on the main thread, exchange the flag and call `g_music_finished_cb` there. Also set `current_music_track_volume = next.volume` in `musicFinished`.

### REND-B07 · Tileset `"overlay_ordering"` is discarded: each context load clears the shared global, and cache hits never refill it
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/cata_tiles.cpp:472`, `src/cata_tiles.cpp:475`, `src/cata_tiles.cpp:4834`, `src/tileset_loader.cpp:591`, `src/sdltiles.cpp:7026`, `src/sdltiles.cpp:7043`, `src/game.cpp:650`, `src/overlay_ordering.cpp:28`

`tileset_mutation_overlay_ordering` is a single process-wide map. Every `cata_tiles::load_tileset`, whichever context it is for (close, far, portrait, overmap), clears it, and only an actual parse refills it. Contexts load in the order close, far, portrait, overmap, both at startup (sdltiles 7026-7043) and on reload (`game.cpp:~650`). With the defaults, PORTRAIT_TILES (`MshockXottoplus_Portraits`) and OVERMAP_TILES (`Larwick Overmap`) are separate tilesets with no `overlay_ordering`, so the global ends up empty. When contexts share a tileset, the cached bundle is returned without a parse (4834-4837), which also leaves it empty. Either way the main tileset's documented `overlay_ordering` (TILESET.md:796) never takes effect. Overlays fall back to the base JSON order, for example wings drawn under armour against the tileset author's intent.

```cpp
cata_tiles.cpp:475:      tileset_mutation_overlay_ordering.clear();
tileset_loader.cpp:592:  if( config.has_array( "overlay_ordering" ) ) { load_overlay_ordering_into_array( config, tileset_mutation_overlay_ordering );
sdltiles.cpp:7043:       overmap_tilecontext->load_tileset( get_option<std::string>( "OVERMAP_TILES" ), ... // loads last
```

**Fix:** Store the parsed overlay ordering on the `tileset` object, and have `get_overlay_order_of_mutation` consult the active close context's tileset. Alternatively, let only the main/close context publish to the global, re-publishing from the bundle on a cache hit, and never clear the global from the far, portrait or overmap loads.

### REND-B08 · Synthetic `highlight_item` is never drawn: inserted into `tile_ids` only, so draws fall back to the "unknown" sprite
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/tileset_loader.cpp:489`, `src/tileset_loader.cpp:492`, `src/cata_tiles.cpp:367`, `src/cata_tiles.cpp:2391`, `src/cata_tiles.cpp:2459`, `src/cata_tiles.cpp:3098`, `src/cata_tiles.cpp:4772`

When a tileset lacks `highlight_item`, the loader reserves a slot and fills it with a translucent quad (upload_atlases, 1112-1124). It registers the id by writing `ts.tile_ids[ITEM_HIGHLIGHT]` directly, bypassing `create_tile_type`, so `tile_ids_by_season` never learns the id. All sprite draws resolve through `tile_ids_by_season`. With category NONE there is no looks_like or ASCII fallback, so `draw_item_highlight` ends at the "unknown" sprite. Any tileset without `highlight_item` therefore shows the large unknown glyph on every multi-item tile, every vehicle cargo part and every editor or construction highlight. The shipped ASCIITileset defines `highlight_item`, so only third-party tilesets are affected, but for them it is very visible.

```cpp
tileset_loader.cpp:492: ts.tile_ids[ITEM_HIGHLIGHT].fg.add( std::vector<int>( {reserved_index} ), 1 );
cata_tiles.cpp:3097:    if( !tt ) { tt = tileset_ptr->find_tile_type( "unknown" ); }
```

**Fix:** Register the synthetic tile with `ts.create_tile_type( ITEM_HIGHLIGHT, std::move( t ) )` so the season maps are populated.

### REND-B09 · Item layering: shared `variant` string is never reset, so the `append_variants` suffix doubles and itype variants leak between items
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/cata_tiles.cpp:3966`, `src/cata_tiles.cpp:3993`, `src/cata_tiles.cpp:3997`, `src/cata_tiles.cpp:4107`, `src/cata_tiles.cpp:4118`, `src/cata_tiles.cpp:4119`

`draw_layer_item` takes `std::string &variant`, a single local (4107) reused across every matched layer entry, every flag context and both furniture and terrain. For each match it assigns `variant` only when the item has an itype variant, and always appends the suffix. A wall with two posted items (context `["WALL"]`, `append_variants: "_postup"`) gives the second item `_postup_postup`, or the first item's variant (e.g. `indian_flag`). The lookup misses and falls back to the plain item sprite or an unrelated `_var_` sprite. The flag loop combines results with `|=`, so an item can also be drawn once per matching flag. This needs a `layering.json` and two or more layered items on one tile, and the effect is cosmetic.

```cpp
3993: if( i.has_itype_variant() ) { variant = i.itype_variant().id; }
3997: if( !layer_var.append_suffix.empty() ) { variant += layer_var.append_suffix; }
4107: std::string variant;   // shared by all draw_layer_item calls
```

**Fix:** Build a fresh string per matched item: `std::string v = i.has_itype_variant() ? i.itype_variant().id : ""; v += layer_var.append_suffix;`. Stop after the first context that drew an item.

### REND-B10 · Furniture override draw uses the real furniture's connect groups, then overwrites the result with no rotate group (construction preview mis-connects)
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/cata_tiles.cpp:3743`, `src/cata_tiles.cpp:3744`, `src/cata_tiles.cpp:3747`, `src/cata_tiles.cpp:3751`, `src/cata_tiles.cpp:5789`, `src/construction.cpp:481`, `src/editmap.cpp:555`

In the furniture override branch, the connect and rotate groups come from `f`, the furniture actually on the map and often null, instead of `f2`, the override. `get_furn_connect_values` also receives `{}` instead of `furniture_override`. Line 3751 then discards that result and recomputes with `get_tile_values_with_ter( ..., 0 )`, which checks only same-id neighbours, uses no rotate group, and reads NO_SELF_CONNECT/ALIGN_WORKBENCH from the real map tile (5797-5798). The terrain override branch (3637-3646) does this correctly. In the construction preview (`construction.cpp:481`) and the editmap brush (`editmap.cpp:555/583`), furniture that uses `connect_groups` or `rotates_to` is previewed with the wrong sprites.

```cpp
3743: const std::bitset<NUM_TERCONN> &connect_group = f.obj().connect_to_groups;
3747: get_furn_connect_values( p, subtile, rotation, connect_group, rotate_group, {} );
3751: get_tile_values_with_ter( p, f2.to_i(), neighborhood, subtile, rotation, 0 );
```

**Fix:** Use `f2`'s `connect_to_groups` and `rotate_to_groups`, and pass `furniture_override` to `get_furn_connect_values`. Call `get_tile_values_with_ter` only in the no-connect-group else branch, with `f2`'s rotate group.

### REND-B11 · Overmap horde sprite check tests the terrain id instead of `overmap_horde_N`
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/sdltiles.cpp:3400`, `src/sdltiles.cpp:3481`, `src/sdltiles.cpp:3483`, `gfx/Larwick_Overmap/tile_config.json:5237`

To decide between the tileset's `overmap_horde_N` sprites and the hard-coded `mon_zombie*` fallback, the code checks `find_tile_with_season( id )`. Here `id` is the OMT terrain id under the horde (set at 3400-3420), checked exactly with no looks_like. Horde display therefore depends on the terrain underneath. A tileset without horde sprites can draw `overmap_horde_N` as the unknown tile, and a tileset with them takes the fallback over terrain that resolves only via looks_like. With the default Larwick_Overmap, the fallback draws nothing, because that tileset has no `mon_zombie*` or `unknown` sprite. Larwick also defines only `overmap_horde_3..10`, so hordes of size 3-12 (sprite_size 1-2) draw nothing even when the check passes; the suggested fix below does not address that case.

```cpp
3481: if( find_tile_with_season( id ) ) {
          draw_from_id_string( string_format( "overmap_horde_%d", sprite_size ), omp, 0, 0, lit_level::LIT, false );
      } else { // hardcoded mon_zombie* fallbacks
```

**Fix:** Build the horde id first and test it: `const std::string horde_id = string_format( "overmap_horde_%d", sprite_size ); if( find_tile_looks_like( horde_id, TILE_CATEGORY::NONE, "" ) ) ...`. Separately, decide on a fallback for sizes the tileset does not cover, such as clamping to the nearest defined size.

### REND-B12 · Explosion animation: tiles draws one ring too few (r=1 is invisible but still delays); r=0 is never shown and the curses `*` has x/y swapped
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/cata_tiles.cpp:5181`, `src/cata_tiles.cpp:5283`, `src/animation.cpp:171`, `src/animation.cpp:172`, `src/animation.cpp:199`, `src/animation.cpp:317`

`draw_explosion` runs frames 1..r, and the tiles `draw_explosion_frame` loops `i < exp_rad`. The first frame is therefore blank, the last shows only rings 1..r-1, and an r=1 blast shows nothing while still sleeping for `EXPLOSION_MULTIPLIER`. Curses draws `i <= frame`, so the two front ends disagree, and tiles under-reports the blast radius for `draw_explosion_radius`, flashbang, shockwave and the debug iuse. For r=0, neither front end calls `progress()`, so the curses r==0 branch is dead; it also passes `point( p.y(), p.x() )`. In `draw_custom_explosion_frame`, a `c_black` tile does `return` instead of `continue`. This has no practical effect today, because `c_black` comes only from spells, which colour every tile the same.

```cpp
cata_tiles.cpp:5181: for( int i = 1; i < exp_rad; ++i ) {
animation.cpp:172:   mvwaddch( g.w_terrain, point( p.y(), p.x() ), '*' );
animation.cpp:199:   for( frame = 1; frame <= r; ++frame ) { anim.progress(); }
```

**Fix:** Use `i <= exp_rad` in `draw_explosion_frame` and draw a centre tile for `i == 0`. Make the frame loops run at least once when r == 0. Change the curses point to `p.xy().raw()`. Use `continue` for `c_black`.

### REND-B13 · DISPLAY_VEHICLE_AI debug overlay strings are written, then immediately overwritten
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/cata_tiles.cpp:773`, `src/cata_tiles.cpp:781`, `src/cata_tiles.cpp:1037`

The vehicle autopilot overlay (773-785) emplaces its text into the out-parameter `overlay_strings`. At 1037, `overlay_strings = here.overlay_strings_cache;` replaces the whole multimap. Every other overlay (scent, radiation, temperature, 839-983) writes into `overlay_strings_cache`, so only the vehicle AI overlay is lost. The toggle is real (`debug_menu.cpp:4840`, `debug_console.cpp:1765`), but the overlay never shows.

```cpp
781:  overlay_strings.emplace( player_to_screen( pt ), formatted_text( text, color, text_alignment::left ) );
1037: overlay_strings = here.overlay_strings_cache;
```

**Fix:** Emplace into `here.overlay_strings_cache`, as the other overlays do.

### REND-B14 · Tileset `"transparency"` {R,G,B} is parsed, but the colour key is hard-coded to black
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/tileset_loader.cpp:322`, `src/tileset_loader.cpp:535`, `src/tileset_loader.cpp:563`, `src/tileset_loader.cpp:1176`, `src/tileset_loader.cpp:1179`

`parse_atlases` reads `tiles-new[].transparency.R/G/B` into the atlas descriptor. `upload_one_atlas` uses the values only as an on/off switch and always keys out (0,0,0). A tileset that declares magenta (255,0,255), the classic sprite-sheet convention, renders solid magenta backgrounds and makes pure-black pixels transparent instead. The only shipped user (ASCIITileset) declares 0,0,0, so the bug is latent. The field is also undocumented in TILESET.md.

```cpp
1176: if( desc.color_key_r >= 0 && desc.color_key_r <= 255 && ... ) {
1179:     const Uint32 key = MapRGB( tile_atlas, 0, 0, 0 );
```

**Fix:** Use `MapRGB( tile_atlas, desc.color_key_r, desc.color_key_g, desc.color_key_b )`, and document the field in TILESET.md.

### REND-B15 · Per-atlas `"pixelscale"` is not included in `max_tile_extent`, so enlarged sprites are culled at screen edges
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/tileset_loader.cpp:548`, `src/tileset_loader.cpp:550`, `src/cata_tiles.cpp:547`, `src/cata_tiles.cpp:638`, `src/cata_tiles.cpp:2128`, `src/cata_tiles.cpp:3261`

Sprites from an atlas with `"pixelscale": 2` (TILESET.md:509) are drawn at `width * pixelscale` (3261-3262). The tileset's `max_tile_extent`, however, is computed from the unscaled sprite size plus offsets (550-559). The culling range at 638 and 2110-2131 is therefore too tight. Off-screen tiles above or left of the view whose scaled sprites reach into it are skipped, so large sprites pop at the top and left edges, and along upper rows in iso. The documented `tree.png` example (64 px, offset -16, pixelscale 2) hits this. Low-res sprites upscaled to tile size do not.

```cpp
tileset_loader.cpp:552: std::max( ts.max_tile_extent.p_max.x, sprite_width + std::max( sprite_offset.x, ... ) ), ...
cata_tiles.cpp:3261:    destination.w = width * tile_width * tile.pixelscale / tileset_ptr->get_tile_width();
```

**Fix:** Multiply `sprite_width`/`sprite_height` by `sprite_pixelscale` (rounded up) when updating `max_tile_extent`.

### REND-B16 · Ambient sound volume squares the soundpack's per-file volume and is also scaled by the SFX slider
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/sdlsound.cpp:715`, `src/sdlsound.cpp:777`, `src/sdlsound.cpp:781`, `src/sdlsound.cpp:787`

`play_ambient_variant_sound` folds `effect.volume` and `AMBIENT_SOUND_VOLUME` into `volume`, then multiplies by `effect.volume` and `SOUND_EFFECT_VOLUME` again (the comment at 777-780 notes `effect.volume^2`). An ambient loop declared at volume 40 plays at 16%, and setting the SFX slider to 0 silences all ambient sound regardless of `AMBIENT_SOUND_VOLUME`. One-shot effects (715, 745) apply `effect.volume` once, so the same file sounds different in the two paths. Nothing in the code or history marks this as a deliberate choice.

```cpp
781: volume = selected_sound_effect.volume * get_option<int>( "AMBIENT_SOUND_VOLUME" ) * volume / ( 100 * 100 );
787: opts.volume = selected_sound_effect.volume * get_option<int>( "SOUND_EFFECT_VOLUME" ) * volume / ( 100 * 100 );
```

**Fix:** Compute `opts.volume = effect.volume * AMBIENT_SOUND_VOLUME * volume / (100*100)`, applying `effect.volume` once and dropping `SOUND_EFFECT_VOLUME`. Note in the changelog that soundpack ambient levels will rise.

## 3. Performance

### REND-P01 · Flying-creature shadow check runs up to 10 `creature_at()` lookups per visible outdoor tile per frame
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/cata_tiles.cpp:1052`, `src/cata_tiles.cpp:1187`, `src/cata_tiles.cpp:4473`, `src/cata_tiles.cpp:4486`, `src/creature_tracker.cpp:310`, `src/creature_tracker.h:147`, `src/map.cpp:8093`

In the per-frame layer loop, every visible tile that has a floor and no drawn creature calls `draw_critter_above()` whenever the tileset has a `shadow` sprite (the bundled ASCIITileset has one). It walks upward through open air, calling `creature_tracker::creature_at()` at each level, up to `fov_3d_z_range` (10). Each call does a bub→abs conversion, an `unordered_map` find that copies a `shared_ptr`, an avatar compare, and a linear scan of the `std::list` of active NPCs. Outdoors every column is open air, so a daytime outdoor view pays about 10 lookups per ground tile per frame. Roofed tiles pay nothing. Frames happen every turn, every 125 ms while animated tiles or rain are shown, and on every animation step. The reviewer estimates roughly 0.5-5 ms per frame depending on zoom.

```cpp
4486: while( pcritter == nullptr && scan_p.z() <= OVERMAP_HEIGHT && !here.dont_draw_lower_floor( scan_p )
             && scan_p.z() - you.posz() <= fov_3d_z_range ) {
          pcritter = get_creature_tracker().creature_at( scan_p, true ); scan_p.z()++; }
```

**Fix:** Once per frame (or per draw-points rebuild), iterate monsters and NPCs once and build a small set or bitmap of the xy columns that have a creature above the z-level being drawn. Call `draw_critter_above` only for those columns. This combines naturally with REND-P11.

### REND-P02 · Every sprite draw resolves its tile by hashing the id string, and multitile, variant and intensity ids are rebuilt by concatenation each frame
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/cata_tiles.cpp:350`, `src/cata_tiles.cpp:2392`, `src/cata_tiles.cpp:2412`, `src/cata_tiles.cpp:2434`, `src/cata_tiles.cpp:2443`, `src/cata_tiles.cpp:2458`, `src/cata_tiles.cpp:2534`, `src/cata_tiles.cpp:2873`, `src/cata_tiles.cpp:3105`, `src/cata_tiles.cpp:3115`, `src/cata_tiles.cpp:4216`, `src/cata_tiles.h:453`

`draw_from_id_string_internal` runs for every layer of every tile in every frame and resolves sprites only by string, through `tile_ids_by_season[season].find(id)`. The per-category costs:
- Multitile terrain (walls, fences, water, roads) does a `std::find` over `available_subtiles` strings. It then re-enters the whole function with `found_id + "_" + multitile_keys[subtile]`, which means a second hash and a second set of restore guards.
- Fields build `id + "_int" + to_string(intensity)`.
- Variants build `id + "_var_" + variant`, or loop over `id + "_" + chunk`.
- Vehicle parts build `"vp_" + id`.
- On a miss, the looks_like fallback constructs `string_id`s and calls `is_valid()`/`obj()`, `itype_id` + `type_is_defined`/`find_type`, or `vpart_id( id.substr(3) )`, every frame.

At 4k-20k visible tiles with 3-6 lookups each, that is roughly 20k-100k hashes and thousands of allocations per frame, even though every caller already holds an int id. With the bundled ASCIITileset every tile goes through the full fallback at 2898-3060 (see also REND-P05).

```cpp
2458: if( auto ret = find_tile_with_season( id ) ) {
3115: return draw_from_id_string_internal( found_id + "_" + multitile_keys[subtile], category, ...
2873: res = find_tile_looks_like( id + "_int" + std::to_string( intensity_level ), category, variant );
2534: if( !item::type_is_defined( itype_id( id ) ) ) {
```

**Fix:** (a) At load time, store each multitile's subtiles as `std::array<const tile_type*, num_subtiles>` in the `tile_type`, so the draw picks the subtile directly with no concatenation or recursion. (b) Add a per-tileset memo keyed by (category, int_id, variant, intensity, season) that stores the resolved `tile_lookup_res`, including negative and looks_like results. Fill it lazily and clear it on tileset reload. Terrain, furniture, traps and fields can use flat vectors indexed by `to_i()`. Expect a several-ms-per-frame reduction at normal zoom, and more when zoomed out.

### REND-P03 · Item-layering lookups (furniture id, every furniture flag, terrain id, every terrain flag) run for every tile, even tiles with no items
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/cata_tiles.cpp:3967`, `src/cata_tiles.cpp:3971`, `src/cata_tiles.cpp:3974`, `src/cata_tiles.cpp:4112`, `src/cata_tiles.h:465`

For every visible tile in every frame, `draw_field_or_item` calls `draw_layer_item` for the furniture id, each furniture flag (iterating a `std::set<std::string>`), the terrain id and each terrain flag. Each call does an `item_layer_data.find(string)` before looking at items, so an empty floor tile costs 6-10 string lookups per frame for any tileset with a `layering.json`. Tiles with items also do an O(variants × items) string-compare scan. The bundled ASCIITileset has no `layering.json` and is unaffected. Related quirk: `draw_layer_item` returns true whenever the context key exists, even if no item matched, so a furniture context entry suppresses the terrain contexts.

```cpp
4113: if( !invisible[0] ) { bool has_drawn_item = draw_layer_item( tile.get_furn_t().id.str(), ...);
          ... for( const std::string &f : tile.get_furn_t().get_flags() ) { has_drawn_item |= draw_layer_item( f, ...
3971: auto itt = tileset_ptr->item_layer_data.find( terfurn_key );
```

**Fix:** Guard the whole block with `tile.get_item_count() > 0`, which is already used at 4147 and is safe because an empty tile cannot draw a layered item. Optionally, pre-index each context's variants by `itype_id` so tiles with items do one lookup per item.

### REND-P04 · Each vehicle tile is resolved and drawn twice per frame because the roof and no-roof layers query the same part
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/cata_tiles.cpp:1044`, `src/cata_tiles.cpp:1183`, `src/cata_tiles.cpp:4179`, `src/cata_tiles.cpp:4185`, `src/cata_tiles.cpp:4201`, `src/vehicle.h:1493`, `src/vehicle_display.cpp:63`, `src/vehicle.cpp:2806`, `src/vehicle.cpp:3565`

`draw_vpart_no_roof` and `draw_vpart_roof` are separate layers, but `draw_vpart` ignores its `roof` argument and calls `get_display_of_tile( mount )` with the defaults (`below_roof = true`, `roof = true`). Both layers therefore pick and draw the same top part. `get_display_of_tile` is expensive:
- It copies `parts_at_relative()` by value four times (via `part_displayed_at` and three `part_with_feature` calls).
- It does a `std::map<string>` variant find and an item `get_var("tied_down_furniture")` string copy.
- It calls `get_option<bool>("VEHICLE_ARMOR_COLOR")` and `get_items`.

The `"vp_"` and variant strings, the sprite submit, the cargo highlight and `memorize_decoration` are all doubled too. City streets show hundreds of vehicle tiles.

```cpp
4201: const vpart_display vd = veh.get_display_of_tile( ovp->mount_pos() );   // roof not passed
vehicle.h:1493: get_display_of_tile( const point_rel_ms &dp, bool rotate = true, bool include_fake = true,
                                     bool below_roof = true, bool roof = true )
```

**Fix:** Call `veh.get_display_of_tile( ovp->mount_pos(), true, true, !roof, roof )` so each layer draws only its own part. Return early in the roof layer when no roof part exists, to avoid the debugmsg in `get_display_of_tile`. Separately, have `part_displayed_at` and `part_with_feature` iterate the cached relative parts by const reference, and cache the `VEHICLE_ARMOR_COLOR` option.

### REND-P05 · An item without a sprite builds a full `item` object on every frame to find its ASCII fallback glyph
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/cata_tiles.cpp:2533`, `src/cata_tiles.cpp:2880`, `src/cata_tiles.cpp:2956`, `src/cata_tiles.cpp:3085`, `src/item.cpp:169`, `src/item.cpp:177`

When an ITEM id resolves to no tile even after the looks_like chain (common for modded items), `draw_from_id_string_internal` default-constructs `item tmp` and then assigns a full `item( itype_id, turn_zero )`, for every such visible tile in every frame. That constructor builds pocket contents, rolls the global RNG for variant and snippet selection, bumps the item uid counter, and for guns and magazines builds nested default mods and ammo. Only `symbol()` and `color()` are needed, and both come straight from the itype or variant (`item.cpp:1191`, `1909`). The failed looks_like walk (2533-2544) and the `"unknown_" + category + "_" + subcategory` concatenations (3085, 3092) are also redone every frame. Mod-heavy games hit this constantly.

```cpp
2956: } else if( category == TILE_CATEGORY::ITEM ) { item tmp; ...
          tmp = item( itype_id( found_id ), calendar::turn_zero ); ... sym = ...tmp.symbol()...; col = tmp.color();
3085: tt = tileset_ptr->find_tile_type( "unknown_" + category_id + "_" + subcategory );
```

**Fix:** Read the symbol and colour from `item::find_type( itype_id( found_id ) )` (plus the variant's `alt_sym`/`alt_color`) without constructing an item. Memoize the final fallback result per (category, id, variant) in the resolution cache from REND-P02.

### REND-P06 · Custom memory-map colour filter calls `get_option` 7 times and `std::pow` for every opaque atlas pixel
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/sdl_utils.h:69`, `src/sdl_utils.cpp:22`, `src/sdl_utils.cpp:132`, `src/tileset_loader.cpp:113`, `src/tileset_loader.cpp:288`, `src/atlas_bake_plan.cpp:19`, `src/options.cpp:2670`, `src/options.cpp:4367`

With `MEMORY_MAP_MODE=color_pixel_custom`, the memory variant is always baked on the CPU (`atlas_bake_plan.cpp:19-20`). For each opaque pixel of each atlas, `color_pixel_custom` reads seven options by name. Six of the names exceed the small-string limit, so each read heap-allocates and then does one or two `unordered_map` lookups. It then calls `color_pixel_mixer`, which runs `std::pow` on a value with only 256 possible inputs. On a large tileset that is hundreds of millions of lookups at startup, on reload and on device-reset replay, likely tens of seconds. This affects only the non-default Custom mode (the default is `color_pixel_sepia_light`) and only at load time.

```cpp
sdl_utils.h:71:   const SDL_Color dark = { static_cast<Uint8>( get_option<int>( "MEMORY_RGB_DARK_RED" ) ), ... };
sdl_utils.cpp:146: const Uint8 finalv = std::min( static_cast<int>( std::round( std::pow( pv, gammav ) * 150 ) ), 100 );
```

**Fix:** Read the seven options once before the pixel loop and precompute a 256-entry LUT from average grey to output colour. Pass the LUT or a stateful functor to `apply_color_filter` instead of a bare function pointer.

### REND-P07 · Atlas opaque-bounds scan calls `SDL_GetRGBA`, with a format-details lookup and palette fetch, for every pixel
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/tileset_loader.cpp:143`, `src/tileset_loader.cpp:276`, `src/sdl_wrappers.cpp:589`

`compute_opaque_rect` runs over every pixel of every sprite rect of every atlas chunk at load, reload and replay. Each pixel goes through the `GetRGBA` wrapper, which makes three library calls: `SDL_GetPixelFormatDetails` (a hash-table lookup in SDL3), `SDL_GetSurfacePalette` and `SDL_GetRGBA`. The surface is always the fixed 32-bit RGBA layout from `create_surface_32`, and `apply_color_filter` already reads `pix->a` directly.

```cpp
sdl_wrappers.cpp:597: SDL_GetRGBA( pixel, SDL_GetPixelFormatDetails( surface->format ),
                                   SDL_GetSurfacePalette( surface.get() ), &r, &g, &b, &a );
```

**Fix:** Read alpha directly (`reinterpret_cast<const SDL_Color*>( row )[x].a`), or at least hoist the format details and palette out of the loop. Scanning inward from the edges also reduces work for padded sprites. Expect hundreds of milliseconds saved on large tilesets.

### REND-P08 · Every tileset PNG is fully decoded twice: once just to read its dimensions, then again to upload
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/tileset_loader.cpp:308`, `src/tileset_loader.cpp:561`, `src/tileset_loader.cpp:586`, `src/tileset_loader.cpp:1173`, `src/sdl_wrappers.cpp:298`

`read_image_dimensions` is described as reading the image header, but it calls `load_image` (`IMG_Load`), which decodes the whole PNG, just to get `w` and `h`. `upload_one_atlas` then decodes the same file again (1173). Large tilesets have dozens of multi-megabyte atlases, so the parse-pass decode is a large and entirely wasted share of load time.

```cpp
313:  const SDL_Surface_Ptr tile_atlas = load_image( img_path...c_str() ); ... tile_atlas_width = tile_atlas->w;
1173: SDL_Surface_Ptr tile_atlas = load_image( desc.image_path_u8.c_str() );
```

**Fix:** Read only the PNG IHDR (fixed offset), or keep the decoded surface from the parse pass in the descriptor, hand it to the first upload, and re-decode only for device-reset replays. This roughly halves PNG decode time during tileset load.

### REND-P09 · Pixel minimap rebuilds all 14.6k tiles about 8 times a second because the avatar always counts as a blinking beacon
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/pixel_minimap.cpp:153`, `src/pixel_minimap.cpp:234`, `src/handle_action.cpp:413`, `src/creature.cpp:605`, `src/options.cpp:2782`

`build_batches` has no cache and sets `has_blinking_beacons_` for any creature the player view sees. At the centre tile `creature_at` returns the avatar, and `Creature::sees` is always true for oneself. With the default `PIXEL_MINIMAP_BLINK=10`, the flag is therefore always true. In the 125 ms idle input loop, `handle_action` then `werase`s the minimap every tick, which triggers a full rebuild of 121×121 tiles. Each tile does `veh_at`/`get_display_of_tile`, terrain and furniture lookups, `creature_at` with its NPC list scan, and `sees()`. That is about 8 full rebuilds per second while idle, only to animate beacon colours. The option is labelled "Hostile creature beacon blink speed", so counting the avatar is also a logic error.

```cpp
pixel_minimap.cpp:234: Creature *critter = creatures.creature_at( p, true );
                       if( critter != nullptr && get_player_view().sees( m, *critter ) ) { if( indicator_length > 0 ) { has_blinking_beacons_ = true; }
handle_action.cpp:415: if( tilecontext->has_blinking_minimap() ) { werase( g->w_pixel_minimap ); }
```

**Fix:** Set `has_blinking_beacons_` only for beacons that actually flicker (MATT_ATTACK/MATT_FOLLOW), never for the avatar. Split the batches: cache the terrain batch keyed on (centre, turn, visibility generation, settings) and rebuild only the beacon batch on blink ticks. Idle minimap CPU should drop by more than 90%.

### REND-P10 · Map-memory sweep re-runs `draw_terrain` over the whole 121×121 range every turn, repeating on-screen tiles in ortho
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/cata_tiles.cpp:1488`, `src/cata_tiles.cpp:1508`, `src/cata_tiles.cpp:1515`, `src/cata_tiles.cpp:1535`, `src/cata_tiles.cpp:3610`, `src/cata_tiles.cpp:5481`, `src/handle_action.cpp:281`

`get_player_input` calls `set_draw_cache_dirty()` every turn, which makes `sweep_due` true. The end-of-frame sweep then calls `draw_terrain( memorize_only )` on all 14,641 tiles. That means override-map finds, `here.ter`, and either `get_connect_values` (a `std::function` plus neighbour checks) or `get_terrain_orientation`. Dec-dirty tiles also run furniture, trap, partial-construction and vpart memorization. Only isometric mode skips tiles already drawn this frame. In ortho, every on-screen tile is processed twice per turn, and holding a movement key pays this every step. Only the sweep clears the dec bit; the main loop clears only the ter bit (1488), which is likely why ortho cannot skip yet. Note that `ter_str_id` is interned, so its copies are cheap.

```cpp
1510: const bool sweep_due = draw_points_rebuilt || memory_writes_pending || ...;
      for( int mem_y = min_visible.y; sweep_due && mem_y <= max_visible.y; mem_y++ ) { ...
          if( is_isometric() && top_any_tile_range.contains( colrow ) ) { continue; }
```

**Fix:** Have the main draw loop also clear the dec-dirty bit after memorizing a visible tile, then skip tiles inside `top_any_tile_range` in both projections, as upstream does. In the sweep, skip non-connecting terrain whose ter and dec bits are already clean, since `draw_terrain` does nothing for them. Connecting terrain is re-evaluated on purpose (see REND-P14).

### REND-P11 · `creature_at()` is called for every drawn tile and minimap pixel, and each call scans the `std::list` of NPCs
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/cata_tiles.cpp:1009`, `src/cata_tiles.cpp:4298`, `src/cata_tiles.cpp:4489`, `src/pixel_minimap.cpp:234`, `src/creature_tracker.cpp:310`, `src/creature_tracker.cpp:336`, `src/creature_tracker.h:147`

`draw_critter_at` runs for every sprite tile in every frame, memorized tiles included, and starts with `creature_at( p, true )`. The draw-points rebuild calls it for every vision-effect tile (1009), and `pixel_minimap::build_batches` calls it for all 121×121 tiles. Each call does `get_abs`, a hash find that copies a `shared_ptr`, an avatar compare, and a walk of the `active_npc` `std::list`. With few NPCs the cost is small next to REND-P02. In faction camps it grows with tiles × NPCs.

```cpp
cata_tiles.cpp:4298:  const Creature *pcritter = get_creature_tracker().creature_at( p, true );
creature_tracker.h:147: std::list<shared_ptr_fast<npc>> active_npc;
```

**Fix:** At the start of `cata_tiles::draw` and `pixel_minimap::build_batches`, build a per-frame flat array (or map) from bub position to `Creature*` in one O(creatures) pass, honouring the "ridden" rule. Look tiles up in it, and use it in `draw_critter_above` (REND-P01).

### REND-P12 · Overmap view builds and re-parses a formatted tile-id string for every OMT, fetches each terrain about 5 times, and computes LOS unconditionally
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/sdltiles.cpp:3243`, `src/sdltiles.cpp:3287`, `src/sdltiles.cpp:3397`, `src/sdltiles.cpp:3452`, `src/overmap_terrain.cpp:831`, `src/cata_tiles.cpp:2474`, `src/cata_tiles.cpp:2976`

`draw_om` runs on every overmap redraw (each keypress and each blink) for every OMT on screen. For each OMT it does the following:
- `get_omt_id_rotation_and_subtile` calls `oter_at` for the centre and four neighbours (3243-3253).
- `get_tileset_id` builds a `string_format( "vl#%s$%s" )` or `"om#" + id`, which is then `substr`'d twice.
- Vision-level ids are re-parsed (`find('$')`, substrs, `string_to_enum`) in `find_tile_looks_like` (2474) and again in the fallback (2976).
- `los` (`overmap_los`: a `line_to` vector plus one overmap lookup per step) is computed for every OMT above "details" vision, but is only read at 3452 under `showhordes`.
- `has_trait( DEBUG_CLAIRVOYANCE )` is checked per OMT.

```cpp
3397: const bool los = overmap_buffer.seen_more_than( omp, ... ) && ( you.overmap_los( omp, sight_points ) || ... );
3287: std::string id = ot_id->get_tileset_id( vision ); bool is_omt = id.substr( 0, 3 ) == "om#"; return std::make_pair( id.substr( 3 ), is_omt );
```

**Fix:** Compute `los` lazily, only once `get_horde_size() >= HORDE_VISIBILITY_SIZE`, and hoist `has_trait` out of the loop. Prefetch the (oter_id, vision) grid for the visible area plus a one-tile border once per frame, so neighbour checks read the grid. Memoize the resolved tile per (oter_id, vision) in an int-indexed cache instead of formatting and re-parsing strings.

### REND-P13 · Character and monster overlays are rebuilt and resolved by string from scratch for every creature every frame
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/cata_tiles.cpp:2552`, `src/cata_tiles.cpp:4675`, `src/cata_tiles.cpp:4726`, `src/cata_tiles.cpp:4749`, `src/character.cpp:2916`, `src/character_attire.cpp:1788`, `src/overlay_ordering.cpp:28`

For the avatar and every visible NPC in every frame, `draw_entity_with_overlays` calls `get_functioning_mutations` and `Character::get_overlay_ids`. The latter builds a multimap plus a `vector<pair<string,string>>`, with `"effect_"`/`"active_"`/`"worn_"` concatenations, two `std::map` lookups per mutation or bionic, and an `all_ablative_armor()` copy per worn item. Each overlay then goes through `find_overlay_looks_like`, which builds and hashes up to four ids (`overlay_male_..._var_`, `overlay_..._var_`, and so on). The `_var_` probes run even when the variant is empty. Overlays with no sprite walk the item looks_like chain up to 10 times. A geared NPC with about 40 overlays costs 200-300 string builds and hashes per frame, roughly 0.1-0.5 ms for 20 NPCs. Monsters repeat the effect and armour part. Side bug: in `get_overlay_ids`, `variant` is declared outside the mutation loop and never cleared, so a mutation without a variant inherits the previous one's.

```cpp
4726: std::vector<std::pair<std::string, std::string>> overlays = override_look_muts.empty() ? ch.get_overlay_ids() : ...
2574: str_append( draw_id, ( male ? "overlay_male_" : "overlay_female_" ), over_type, looks_like, "_var_", variant );
```

**Fix:** Memoize `find_overlay_looks_like` in an `unordered_map` keyed by (male, overlay id, variant), storing the resolved draw id or `tile_type*` (or "none"), and clear it on tileset load. Skip the `_var_` probes when the variant is empty. Better still, cache each creature's resolved overlay list keyed by a cheap appearance stamp (worn, mutation and effect change counters). Reset `variant` per mutation in `get_overlay_ids`.

### REND-P14 · Connecting terrain re-dirties itself, so walls, water and pavement are re-memorized every frame
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/cata_tiles.cpp:662`, `src/cata_tiles.cpp:1488`, `src/cata_tiles.cpp:3610`, `src/cata_tiles.cpp:3618`, `src/map_memory.cpp:221`, `src/map.cpp:529`

For any terrain with `connect_to_groups` (611 definitions: walls, water, pavement, rail, fences), `draw_terrain` unconditionally marks the tile dirty and then memorizes it. The end of the frame cleans it (1488), so the next frame repeats the write. `map_memory::set_tile_terrain` copies the `memorized_tile` out and back. The developers know about this, and `map_memory_sweep_pending` is read before the layer loop (662-664), so it does not trigger the expensive sweep. What remains is about one memory write (~50 ns) per visible connecting tile per frame.

```cpp
3610: if( connect_group.any() ) { get_connect_values(...); ... here.memory_cache_ter_set_dirty( p, true ); }
3618: if( here.memory_cache_ter_is_dirty( p ) ) { get_avatar().memorize_terrain( here.get_abs( p ), tname, subtile, rotation ); }
```

**Fix:** Only write when something changed: compare the memorized ter id, subtile and rotation with the new values first. Inside `set_tile_terrain`, mutate the stored tile in place instead of copying it out and back.

### REND-P15 · Special-vision check constructs a dialogue (two heap talkers) per hidden creature per frame, before checking whether any special vision exists
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/cata_tiles.cpp:1012`, `src/cata_tiles.cpp:4338`, `src/cata_tiles.cpp:4427`, `src/magic_enchantment.cpp:1222`, `src/character.cpp:3855`

In `draw_critter_at`, for every creature on a tile the player cannot see or perceive, the code builds `const_dialogue( get_const_talker_for( you ), get_const_talker_for( critter ) )`, which is two `make_unique` allocations. It then calls `enchantment_cache->get_vision( d )`, whose empty check runs only after the dialogue is built. The draw-points rebuild does the same via `sees_with_specials`. In the common case (no special vision), `get_vision` returns `{}` with no struct copy, so the cost is about two small allocations per hidden creature per frame. With a special-vision enchantment active, the result is returned by value (copying a vector and a `std::function`), and the EOC condition callbacks run per creature per frame.

```cpp
4427: const_dialogue d( get_const_talker_for( you ), get_const_talker_for( *pcritter ) );
      const enchant_cache::special_vision sees_with_special = you.enchantment_cache->get_vision( d );
```

**Fix:** Add a `has_special_vision()` accessor and check it before building the dialogue. Have `get_vision` return `const special_vision *`. Optionally, cache per-creature results per turn.

### REND-P16 · Zombie-revival indicator layer runs string-flag set lookups on every visible tile per frame before checking for items
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/cata_tiles.cpp:1046`, `src/cata_tiles.cpp:4600`, `src/map.cpp:6812`

`draw_zombie_revival_indicators` is the 11th draw layer and runs for every sprite tile. It hashes the indicator id through `find_tile_type` per tile, then calls `could_see_items`. That function does `has_flag_ter_or_furn( "CONTAINER" )`, two `std::set<std::string>::count` calls, plus a SEALED lookup, all before touching `i_at( pos )`, which is almost always empty. `TFLAG_CONTAINER` already exists, and `sees_some_items` checks `has_items` first. The cost is around 100 ns per tile per frame.

```cpp
4600: if( tileset_ptr->find_tile_type( ZOMBIE_REVIVAL_INDICATOR ) && !invisible[0] && ... && here.could_see_items( pos, get_player_character() ) ) {
map.cpp:6814: static const std::string container_string( "CONTAINER" ); const bool container = has_flag_ter_or_furn( container_string, p );
```

**Fix:** Resolve the indicator tile once per frame or at tileset load. Use `here.sees_some_items()` instead of `could_see_items`. In `map.cpp`, use `TFLAG_CONTAINER` instead of the string flag.

### REND-P17 · Coloured-text printing allocates heavily per call: segment strings copied twice, a deque-backed stack, colour-name rebuilding
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/output.cpp:180`, `src/output.cpp:233`, `src/output.cpp:272`, `src/output.cpp:1265`, `src/output.cpp:1683`, `src/color.cpp:634`, `src/color.cpp:742`

`print_colored_text` and `trim_and_print` run for every sidebar line each turn and every menu row on each keypress. Each call allocates heavily:
- `split_by_color` returns a `vector<string>`, and `for( auto seg : ... )` copies each segment again.
- `std::stack<nc_color>` defaults to a deque, which allocates on construction.
- `rm_prefix` takes and returns a `std::string`.
- `color_from_string` builds several strings plus a `std::array` of string pairs per call.
- `get_name`/`id_to_name` return by value.

`word_rewrap` rescans `tag_positions` on each `<` (O(tags²)). `display_item_info`'s `get_comparison_color` is O(n×m) string compares per ImGui frame. All of this runs at UI-redraw rate and is bounded by visible rows, so it costs tens to hundreds of microseconds per redraw.

```cpp
output.cpp:240: const auto color_segments = split_by_color( text ); std::stack<nc_color> color_stack; ...
                for( auto seg : color_segments ) { ... seg = rm_prefix( seg );
color.cpp:640:  std::string new_color( color ); if( new_color.substr( 1, 1 ) != "_" ) { new_color = "c_" + new_color; }
```

**Fix:** Walk tag positions with `string_view` segments, use `std::stack<nc_color, small_vector<...>>`, and skip the tag prefix by offset. Fast-path `c_`/`h_`/`i_` names in `color_from_string` with a heterogeneous lookup. Cache the `nc_color` → tag table. Use `std::binary_search` on `tag_positions` in `word_rewrap`. Index `vItemCompare` once per comparison.

### REND-P18 · Terminal glyph rendering does a `std::map<string>` lookup plus an `unordered_map<string>` lookup per drawn cell
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/sdl_font.cpp:382`, `src/sdl_font.cpp:700`, `src/sdl_font.h:149`, `src/sdl_font.h:217`, `src/sdltiles.cpp:3902`

`draw_window` decodes each non-space cell twice (`UTF8_getch`, `utf8_width`) and calls `FontFallbackList::queue_char` with the string. That does a `std::map<std::string, ...>` find (700), then `CachedTTFFont::glyph_for` does an `unordered_map<std::string>` find (382). Software mode also builds `ch + color` per cell, but that fits in the small-string buffer. Full-screen redraws cover 10k+ cells at about 50-100 ns each. Changing the key type touches the virtual `Font::queue_char` interface.

```cpp
sdl_font.cpp:700: auto cached = glyph_font.find( ch );
sdl_font.h:217:   std::map<std::string, std::vector<std::unique_ptr<Font>>::iterator> glyph_font;
```

**Fix:** Key both caches by codepoint (`char32_t`, which `draw_window` already decodes) using `unordered_map<char32_t, ...>` or a flat BMP array, and pass the codepoint to `queue_char`. Optionally store the decoded codepoint and width in `cursecell` at print time.

### REND-P19 · Each tile id is stored in five string-keyed hash maps (`tile_ids` plus four per-season copies)
**Severity:** Low · **Effort:** Medium · **Verified:** plausible
**Where:** `src/cata_tiles.cpp:361`, `src/cata_tiles.cpp:371`, `src/cata_tiles.cpp:398`, `src/cata_tiles.h:452`, `src/tileset_loader.cpp:475`

`create_tile_type` inserts each tile (every multitile subtile and every ASCII glyph tile included) into `tile_ids` and, for non-seasonal ids, into all four `tile_ids_by_season` maps. Each insert copies the key and allocates a node. That means about five string-keyed inserts per id at load, plus four extra key copies per id in memory per loaded tileset context, which the reviewer puts at up to 10-25 MB for very large tilesets. Load is cold and cached by `tileset_cache`, and draw-time lookups are already a single find, so the perf impact is minor. This layout is also what makes REND-B02 possible.

```cpp
cata_tiles.cpp:396: if( !has_season_suffix ) { for( auto &by_season_map : tile_ids_by_season ) { by_season_map[id].default_tile = &inserted_tile; } }
```

**Fix:** Use a single `unordered_map<std::string, std::array<season_tile_value, NUM_SEASONS>>`, or store seasonal overrides inside `tile_type`, so each id is hashed and stored once. Build it after the invalid-tile cleanup, which also removes the dangling pointers from REND-B02.

## Appendix: considered and rejected

- `get_terrain_orientation` copies `ter_str_id` strings five times per non-connecting terrain tile per frame – `ter_str_id` is a static, interned id (`string_id.h:116-119`), so copies are a few integers with no string copy or allocation. Conversion back to `ter_id` uses the cached `_cid`/`_version` (`generic_factory.h:167-170`, set at 424). The extra cost is negligible next to the string-keyed tile lookups.
