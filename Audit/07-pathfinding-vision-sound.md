# 07 · Pathfinding, vision, lighting, sound & scent

> Engine audit – static code reading at commit `a1a646d6`; nothing was compiled or run. Each finding was checked against the code by a separate reviewer: **confirmed** = verified by reading the code; **plausible** = consistent with the code, but reachability or impact not proven. Line numbers refer to that commit.

**Scope:** `src/pathfinding.cpp`, `src/simple_pathfinding.cpp`, `src/line.cpp`, `src/sounds.cpp`, `src/teleport.cpp`, `src/lightmap.cpp`, `src/shadowcasting.cpp`, `src/scent_map.cpp`, `src/scent_block.cpp`, `src/cellular_automata.cpp`

## Summary

These systems work, but they have sharp edges. The two worst bugs are a 3D Bresenham line that stops early on certain slopes (so creatures see through walls and some shots get a one-tile trajectory) and an A* pathfinder that calls the player's interactive stair routine, which can open modal prompts and use up items. Sound is the weakest area for modding: it ignores terrain entirely, its queues are not shifted with the map, and it has no EOC or event hooks. The lighting and pathfinding hot paths do avoidable work on almost every player step: relights triggered by changes that cannot affect light, `avoid()` called before the closed-set check, and a per-tile sqrt/exp in the 3D FOV.

| Lens | Critical | High | Medium | Low | Total |
|---|---|---|---|---|---|
| Missing JSON / EOC | 0 | 1 | 6 | 11 | 18 |
| Bugs & quirks | 0 | 2 | 9 | 9 | 20 |
| Performance | 0 | 2 | 5 | 7 | 14 |
| **Total** | 0 | 5 | 20 | 27 | 52 |

**Fix first:**
1. **SPAT-B02** · 3D bresenham stops early on tied axes – changing two conditions fixes see-through-wall LOS and broken projectile paths.
2. **SPAT-B01** · A* calls interactive `find_or_make_stairs` – it can raise popups every turn and use up the player's rope or grapnel; the fix is a side-effect-free stair lookup.
3. **SPAT-P01** · `avoid()` runs before the closed check in A* – swapping two `if` statements (plus an optional memo bit) cuts the main cost of every `route()` by 4-8x.
4. **SPAT-P02** · Whole-level relight on changes that cannot alter light – three small gating changes remove a full relight from most player steps.
5. **SPAT-B09** · Monster path-avoid bash test is inverted – removing one `!` lets door- and window-bashing monsters actually use A*.

## 1. Missing JSON bindings & EOC features

### SPAT-J01 · Sound propagation ignores terrain: no soundproofing field, hardcoded underground attenuation
**Severity:** High · **Effort:** Large · **Verified:** confirmed
**Where:** `src/sounds.cpp:258`, `src/sounds.cpp:498`, `src/sounds.cpp:507`, `src/sounds.cpp:585`, `src/sounds.cpp:1072`, `src/monster.cpp:4111`, `src/character_health.cpp:3215`

Every sound check goes through `sound_distance()`: monster hearing, sound-triggered traps, player and NPC hearing, and exterior engine sfx. That function is plain `rl_dist` plus a hardcoded vertical penalty (x5 per z-level, then +4 and +20 per level underground). Walls, closed doors, vehicle hulls and furniture never muffle anything, so a zombie hears a shout through a bunker wall as well as one across an open field. No ter/furn/vpart/field JSON key or flag affects sound, and the vertical constants cannot be tuned. `monster::hear_sound` and `Character::can_hear` only add weather `sound_attn`, and the only muffling anywhere is the vehicle `MUFFLER` on engine noise. This rules out soundproofed rooms, quiet basements, sci-fi hulls and sound-dampening spells or fields.

```cpp
static int sound_distance( const tripoint_bub_ms &source, const tripoint_bub_ms &sink ) {
    ... vertical_attenuation += 4; ... += ( underground_displacement - 1 ) * 20; ... *= 5;
    return rl_dist( source.xy(), sink.xy() ) + vertical_attenuation;
}
// sounds.cpp:498  const int dist = sound_distance( source, critter.pos_bub() );
// sounds.cpp:585  const int distance_to_sound = sound_distance( you->pos_bub(), pos );
```

**Fix:** Add per-tile attenuation:
```json
{ "type": "terrain", "id": "t_wall_padded", "sound_attenuation": 15, "flags": [ "SOUNDPROOF" ] }
```
Use about 15 for walls, about 8 for doors and 0 for open tiles. Add the same key to vparts (hulls) and per intensity to `field_type`. Implement it as a per-level attenuation grid cached next to `transparency_cache` and dirtied the same way. Then walk `line_to(source, listener)` once per centroid and sum the attenuation. A walk per monster per centroid without the cache would cost too much. Move the vertical constants to EXTERNAL_OPTIONs (`SOUND_Z_LEVEL_MULT`, `SOUND_UNDERGROUND_MULT`), following the `SPAWN_CITY_HORDE_*` precedent in `data/core/external_options.json`.

### SPAT-J02 · Monster `path_settings`: documented fields not loaded, `bash_strength` mis-documented, size never set
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monstergenerator.cpp:1198`, `src/monstergenerator.cpp:601`, `src/pathfinding.cpp:303`, `src/pathfinding.cpp:386`, `src/monmove.cpp:239`, `doc/JSON/MONSTERS.md:768`, `src/monster.cpp:4370`

MONSTERS.md documents `allow_unlock_doors` and `avoid_rough_terrain` under `path_settings`, but the loader reads neither, even though `pathfinding.cpp` implements unlock-door routing. `bash_strength` is documented as an int, but the field is a `std::map<damage_type_id,int>` and needs an object such as `{"bash": 10}`. `climb_cost` cannot be set directly. Monsters never set `path_settings.size`; only `character.cpp:544` and `mutation.cpp:576` do. As a result the size gates in `cost_to_pass` (`RestrictLarge`/`RestrictHuge`) never apply to monsters, while `monmove.cpp:239-241` still blocks large monsters from `SMALL_PASSAGE`. Large monsters that cannot bash can plan routes through crawlspaces and then stall there. Bashers are mostly protected by `get_path_avoid`, which has its own bug (SPAT-B09).

```cpp
// monstergenerator.cpp:1201-1208 loads: max_dist, max_length, bash_strength, allow_open_doors,
//   avoid_traps, allow_climb_stairs, avoid_sharp, avoid_dangerous_fields
// MONSTERS.md:768  `bash_strength` | (int, default -1)
// pathfinding.cpp:303  if( settings.size && ( ... RestrictLarge && settings.size > creature_size::medium ...
```

**Fix:** Load `allow_unlock_doors`, `avoid_rough_terrain` and an explicit `climb_cost` override:
```json
"path_settings": { "max_dist": 45, "allow_unlock_doors": true, "avoid_rough_terrain": true, "climb_cost": 3, "bash_strength": { "bash": 10 } }
```
In `finalize_pathfinding_settings`, set `mon.path_settings.size = mon.size`, with an optional `"size"` override. Change the `bash_strength` doc to "object, damage_type -> int, defaults to bash_skill". Add a `check()` warning when `max_length < max_dist`.

### SPAT-J03 · Pathfinding cache reads CLIMBABLE/SHARP/SMALL_PASSAGE from terrain only; `Restrict*` size flags are never set
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/map.cpp:12107`, `src/map.cpp:12149`, `src/map.cpp:12153`, `src/pathfinding.h:49`, `src/pathfinding.cpp:303`, `src/game.cpp:7593`, `src/npc.cpp:3832`

`update_pathfinding_cache` feeds `cost_to_pass` and `straight_route`, and it checks only the terrain for `CLIMBABLE`, `SHARP` and `SMALL_PASSAGE`. Content puts these flags on furniture: `f_barricade_road` and the sandbag, earthbag and gravelbag half-walls are `CLIMBABLE`, and `f_wreckage`/`f_plane_wreckage` are `SHARP`. Climbing creatures therefore path around sandbag walls, and `avoid_sharp` creatures route through sharp wreckage. `npc::path_avoids` has no sharp check of its own. Movement code uses `map::has_flag`, which checks terrain or furniture (`game.cpp:7593`). Separately, `RestrictTiny`, `RestrictSmall` and `RestrictMedium` exist but nothing ever sets them, so content cannot define a vent only tiny creatures fit through.

```cpp
// map.cpp:12107
if( terrain.has_flag( ter_furn_flag::TFLAG_CLIMBABLE ) ) { cur_value |= PathfindingFlag::Climbable; }
// map.cpp:12153
if( terrain.has_flag( TFLAG_SMALL_PASSAGE ) ) cur_value |= ( RestrictLarge | RestrictHuge );
```

**Fix:** Use `terrain.has_flag(X) || furniture.has_flag(X)` for all three flags. Add a ter/furn field that sets the matching `Restrict*` bits and that `monster::will_move_to` and Character movement also consult:
```json
{ "type": "furniture", "id": "f_cat_flap", "max_creature_size": "small" }
```
Keep `SMALL_PASSAGE` as shorthand for a maximum of `"medium"`.

### SPAT-J04 · Teleport anchors only work through Character worn items and effects; mutation, bionic and monster anchors are ignored
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/teleport.cpp:197`, `src/teleport.cpp:319`, `src/teleport.cpp:381`, `src/teleport.cpp:257`, `src/game.cpp:8558`, `src/character.cpp:7560`, `doc/JSON/JSON_FLAGS.md:394`

`teleport_to_point` blocks a teleport only when the teleportee is a Character (`p &&`) wearing a `DIMENSIONAL_ANCHOR` item or carrying an effect with `DIMENSIONAL_ANCHOR`/`TELEPORT_LOCK`. JSON_FLAGS.md lists `DIMENSIONAL_ANCHOR` as a Character flag, and `game.cpp:8558` honours `Character::has_flag`, which covers traits, bionics, effects, bodyparts and martial-arts buffs. `teleport.cpp` never calls it, so a mutation or enchantment that grants the flag does nothing. Monsters can never be anchored: a `TELEPORT_LOCK` effect on a monster (MindOverMatter's `effect_teleporting_lock`) is ignored. The victim check at :319 is also Character-only. Vanilla works only because the anchor bionic grants an integrated worn fake item. No event fires on a successful teleport, and `teleports_into_wall` (:257) is sent for the avatar only.

```cpp
// teleport.cpp:197-199
if( !force && !force_safe && p && ( p->worn_with_flag( json_flag_DIMENSIONAL_ANCHOR ) ||
    p->has_effect_with_flag( json_flag_DIMENSIONAL_ANCHOR ) ||
    p->has_effect_with_flag( json_flag_TELEPORT_LOCK ) ) )
// game.cpp:8558  u.worn_with_flag( flag_DIMENSIONAL_ANCHOR ) || u.has_flag( flag_DIMENSIONAL_ANCHOR )
```

**Fix:** Add one helper, `bool is_teleport_anchored(const Creature&)`. It should check `Creature::has_effect_with_flag` for both flags, `Character::has_flag` plus `worn_with_flag`, and a new `mon_flag` `DIMENSIONAL_ANCHOR`. Use it for the teleportee; for the victim, use it with `TELEPORT_LOCK` excluded, since that flag is documented as "you cannot teleport". Add an event sent at `teleport.cpp:381` for all creature types:
```json
"creature_teleported": { "creature": "...", "from": "...", "to": "...", "cause": "..." }
```

### SPAT-J05 · No EOC or event hooks for hearing: sounds and noise wake-ups cannot trigger JSON
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/sounds.cpp:473`, `src/sounds.cpp:496`, `src/sounds.cpp:571`, `src/sounds.cpp:614`, `src/sounds.cpp:659`, `src/sounds.h:62`, `src/event.h:524`, `src/monster.cpp:4173`

`process_sounds()` handles monsters and traps, and `process_sound_markers()` handles characters (deafening, waking, NPC `handle_sound`). Neither sends an event_bus event or runs an EOC, and `event.h` has no sound or hearing event. `character_wakes_up` carries only the character, so an EOC cannot tell a noise wake-up (:659) from a natural one. `sounds::sound()` records no source creature, and no math function exposes the noise meter (`Character::volume`); math `volume()` returns body volume. Existing partial hooks: monsters can react to noise through `mon_trigger` `SOUND` in anger, fear and placate triggers (`monster.cpp:4173`), and deafening already fires `character_gains_effect` with `deaf`. Still missing: clap-on lamps, sound-sensitive mutants, echo-sense abilities, and furniture, items or mutations that react to noise.

```cpp
// sounds.cpp:496-503
for( monster &critter : g->all_monsters() ) { ... critter.hear_sound( source, vol, dist, this_centroid.provocative ); }
// sounds.cpp:659
you->wake_up();
// event.h:524-528  character_wakes_up fields = { "character" } only
```

**Fix:** Add an event `character_hears_sound { character, volume, heard_volume, category, location, source? }`, sent from `process_sound_markers` only when `heard_volume > 0`. Add a `cause` field (`"noise"`, `"alarm"`, `"natural"`) to `character_wakes_up`. Add an optional `source` creature to `sounds::sound()`. Add an mtype hook with `_volume`/`_category` context values:
```json
"hear_eocs": [ "EOC_MONSTER_HEARD_NOISE" ]
```
Add math `noise_level()` returning `Character::volume`. Send events only when the event_bus has subscribers.

### SPAT-J06 · Colored light only for ter/furn/field/vpart; items, monsters, mutations and enchantments emit white light
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/lightmap.cpp:133`, `src/lightmap.cpp:387`, `src/lightmap.cpp:672`, `src/lightmap.cpp:765`, `src/lightmap.cpp:803`, `src/lightmap.cpp:1626`, `src/itype.h:1641`, `src/item_factory.cpp:4485`, `src/character.cpp:3850`

`light_color` is parsed only in `mapdata.cpp`, `field_type.cpp` and `veh_type.cpp`. Item `light` is a bare unsigned, and monster `luminance`, mutation `lumination` and the `LUMINATION` enchantment all reach the lightmap without a colour. Character light is applied right after `light_source_buffer.fill()`, so it never has a colour. Monster light is applied after the buffer is filled, so it picks up whatever coloured light is already buffered on its tile. Several values are hardcoded: on fire = 8, haslight = 4, and glowing/glowy LED = 5 (`character.cpp:3850`). Vehicle cone widths are fixed at 45/90/180 degrees per flag. `light_color` is also undocumented in `doc/JSON`.

```cpp
// lightmap.cpp:393-396
if( p.has_effect( effect_onfire ) ) { apply_light_source( pos, 8 ); }
else if( p.has_effect( effect_haslight ) ) { apply_light_source( pos, 4 ); }
// lightmap.cpp:803
apply_light_arc( ..., veh_luminance, 45_degrees, vp.light_color );
```

**Fix:** Add `"light_color": [r,g,b]` to itype (next to `light`), mtype (next to `luminance`), mutation (next to `lumination`) and enchantment (paired with `LUMINATION`). Route character and monster light through `add_light_source(p, lum, color)`. Replace the 8/4/5 constants with JSON on the effects, either `"light_emitted"`/`"light_color"` or LUMINATION enchantments. Add a vpart key such as `"light_arc": 60`, and document `light_color`.

### SPAT-J07 · Scent is one global type, only the avatar emits it, and EOCs cannot read or write it
**Severity:** Medium · **Effort:** Large · **Verified:** confirmed
**Where:** `src/scent_map.h:53`, `src/scent_map.cpp:115`, `src/scent_map.cpp:132`, `src/do_turn.cpp:723`, `src/scent_map.cpp:17`, `src/scent_map.cpp:200`, `src/scent_map.cpp:59`, `src/scent_map.cpp:176`, `src/scent_map.cpp:298`, `src/map_field.cpp:593`

`scent_map` has one int grid and a single `typescent` for the whole bubble. Every typed `set()` overwrites the type of every tile. The avatar is the only typed emitter (`do_turn.cpp:726`). NPCs have a `scent` value but never deposit it, so tracking monsters can only follow the player. Tuning is hardcoded: `SCENT_RADIUS` 40, diffusivity 100, `REDUCE_SCENT` 20%, -1 per turn decay, and updates stop after 1000 turns without moving. `scent_type` JSON loads only `receptive_species`. No math function, condition or effect reads or writes scent at a location. Existing hooks: field `apply_slime_factor` deposits untyped scent, `scent_neutralization` removes it (see SPAT-B10), and the `change_scent` iuse, mutation `scent_type` and `SCENT_MASK` exist. Bait items, NPC fugitives tracked by dogs and scent-reading mutations still need C++.

```cpp
// scent_map.cpp:117-120
grscent[p.x()][p.y()] = value; if( !type.is_empty() ) { typescent = type; }
// scent_map.cpp:135-136
if( inbounds( p ) && grscent[p.x()][p.y()] > 0 ) { id = typescent; }
// do_turn.cpp:726
scent.set( u.pos_bub(), u.scent, u.get_type_of_scent() );
```

**Fix:** (1) EOC: add an assignable math `scent(loc)` (`scent(loc) = 300`), a `scent_type(loc)` condition, and:
```json
{ "map_add_scent": { "global_val": "loc" }, "intensity": 400, "scent_type": "sc_bait", "radius": 1, "mode": "max" }
```
(2) Emitters: `"emits_scent": { "type": "sc_meat", "intensity": 200 }` on mtype, itype and field_type, and NPC deposits behind an EXTERNAL_OPTION. (3) Store a per-tile uint8 type index next to `grscent`. (4) Add `scent_type` fields `decay_per_turn`, `diffusivity` and `strength_mult`, plus an EXTERNAL_OPTION for `SCENT_RADIUS`.

### SPAT-J08 · `u_make_sound`: parser drops `sensory`, ignores variables, passes no sfx id; docs have drifted
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctalk.cpp:6392`, `src/npctalk.cpp:6406`, `src/npctalk.cpp:6464`, `doc/JSON/EFFECT_ON_CONDITION.md:4352`, `doc/JSON/EFFECT_ON_CONDITION.md:4381`

`f_make_sound` reads `type` with `jo.get_string` and an if-chain that leaves out `sound_t::sensory`. That is the only category drawn as a marker even when the source is visible, so EOC sonar or echolocation cannot use it. Spells and iuse actors use the enum reader and do accept it. The docs say `type` is mandatory and accepts a variable object, but the code defaults to `background` and accepts only a literal string. `ambient` is loaded but undocumented. The documented snippet example omits the required `volume` and would fail to load. No sfx id or variant is passed to `sounds::sound()`. Workaround: an EOC can cast a spell that sets `sound_type`/`sound_id`/`sound_variant`.

```cpp
// npctalk.cpp:6407-6433
std::string type_string = jo.get_string( "type", "background" );
if( type_string == "background" ) ... else if( type_string == "order" ) ...
else { jo.throw_error( "Invalid message type." ); }   // no "sensory"
```

**Fix:** Read `type` as `str_or_var` and convert it at runtime with `io::string_to_enum<sounds::sound_t>`. Add optional sfx keys:
```json
{ "u_make_sound": "a ping", "volume": 20, "type": "sensory", "sfx_id": "sonar", "sfx_variant": "ping", "provocative": false }
```
Document `ambient`, fix the "mandatory" wording, and add `volume` to the snippet example.

### SPAT-J09 · Noise wake-up and deafening rules hardcoded with trait/bionic/effect ids
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/sounds.cpp:592`, `src/sounds.cpp:597`, `src/sounds.cpp:603`, `src/sounds.cpp:615`, `src/sounds.cpp:652`, `src/player_hardcoded_effects.cpp:1505`, `src/character_health.cpp:292`, `src/character_health.cpp:3218`

Waking from noise is a three-way branch on `HEAVYSLEEPER`/`HEAVYSLEEPER2` (2d15/3d15/6d15), with `effect_narcosis` and `bio_sleep_shutdown` as hard overrides. The pain-wake check at `character_health.cpp:292` repeats the trait ids. The deafening threshold (`rng(felt/2, felt) >= 150`), the durations (`(felt-130)/4`, or `/8` capped at 4 minutes) and the ache pain are fixed numbers. `HEARING_PROTECTION` halves felt volume as a boolean. Partial protection already exists through the `HEARING_MULT` enchantment (applied in `hearing_ability`, `character_health.cpp:3218`) and the `PARTIAL_DEAF`/`IMMUNE_HEARING_DAMAGE` flags. The remaining gap is that a modded deep-sleeper trait, light-sleeper drawback or sedative cannot plug into the wake logic. The alarm-clock chain in `player_hardcoded_effects.cpp:1505` is different (there `bio_sleep_shutdown` forces waking) and should not share the same helper.

```cpp
// sounds.cpp:652-657
( !( has_trait( trait_HEAVYSLEEPER ) || has_trait( trait_HEAVYSLEEPER2 ) ) && dice( 2, 15 ) < heard_volume ) ||
( has_trait( trait_HEAVYSLEEPER ) && dice( 3, 15 ) < ... ) ||
( has_trait( trait_HEAVYSLEEPER2 ) && dice( 6, 15 ) < ... ) ) &&
!has_effect( effect_narcosis ) && !has_bionic( bio_sleep_shutdown )
```

**Fix:** Add enchantment values `NOISE_WAKE_DICE` (base 2; set in JSON as +1 for HEAVYSLEEPER and +4 for HEAVYSLEEPER2) and `DEAFEN_THRESHOLD` (base 150). Replace the narcosis and bionic checks with a character flag `SLEEP_THROUGH_NOISE` granted in JSON. Factor the noise test into `Character::wakes_from_noise(volume)`.

### SPAT-J10 · Terrain/furniture transparency is binary; no partial translucency like fields have
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/lightmap.cpp:259`, `src/lightmap.cpp:268`, `src/lightmap.cpp:276`, `src/lightmap.cpp:158`, `src/map.cpp:2157`, `src/map.cpp:2652`

`build_transparency_cache` writes `LIGHT_TRANSPARENCY_SOLID` for any non-transparent ter/furn and open air otherwise. Only fields multiply in a float `translucency`. `TRANSLUCENT` passes light but blocks sight completely. Glass, hedges, bead curtains and tinted windows cannot dim light or reduce sight, as the TODO at :276 notes. The cache is already a float and `castLight` handles translucency > 1, so the change is small.

```cpp
// lightmap.cpp:259-261
if( vehicle_opaque[...] || !( ter.transparent && furn.transparent ) ) { write( p, LIGHT_TRANSPARENCY_SOLID, ... ); continue; }
// lightmap.cpp:276  // TODO: [lightmap] Have glass reduce light as well.
```

**Fix:** Add `"translucency": 1.5` (default 1.0) and an optional `"sight_translucency"` to ter/furn, multiplied into `value` the same way fields are. Add both to the ter/furn change-detection comparisons at `map.cpp:2157/2652` so the caches are dirtied.

### SPAT-J11 · `light_level()` math is coarse, cannot read ambient light, and its documented values are wrong
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/math_parser_diag.cpp:289`, `src/math_parser_diag.cpp:1895`, `src/lightmap.cpp:962`, `src/lightmap.h:94`, `doc/JSON/NPCs.md:1418`

`light_level(loc)` returns `map::light_at`, which only yields DARK(0), LOW(1), LIT(3) or BRIGHT(4). NPCs.md lists 3 as BRIGHT_ONLY (it is actually 2), leaves out LIT, and lists 5/6, which can never be returned. Its example `light_level(_pos) == 3` therefore tests LIT. Vision thresholds use the float `map::ambient_light_at`, which EOCs cannot read. `custom_light_level` is a global write only.

```cpp
// math_parser_diag.cpp:295
return static_cast<double>( here.light_at( here.get_bub( loc_val ) ) );
// lightmap.h:94-101  DARK = 0, LOW, BRIGHT_ONLY, LIT, BRIGHT, MEMORIZED, BLANK
```

**Fix:** Add math `ambient_light(loc)` returning `ambient_light_at()` as a float on the same scale as `custom_light_level`. Fix the NPCs.md table to 0 DARK, 1 LOW, 3 LIT, 4 BRIGHT. Optionally add `u_light_threshold()` exposing `get_vision_threshold`.

### SPAT-J12 · Horde hearing constants hardcoded in `get_signal_for_hordes`
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/sounds.cpp:447`, `src/sounds.cpp:451-458`, `src/sounds.cpp:487-494`, `src/overmap.cpp:1748-1789`

Hordes ignore sounds below volume 60, underground volume is divided by `2*|z|`, and the signal is `volume/SEEX` clamped to [8,180]. These are core difficulty levers for horde mods, but no option reads them. The EOC `signal_hordes` can only add signals. `overmap::signal_hordes` has further hardcoded interest constants (15/30/3) that belong with these.

```cpp
const int min_vol_cap = 60; const int underground_div = 2; const int hordes_sig_div = SEEX;
const int min_sig_cap = 8; const int max_sig_cap = 180;
int vol_hordes = ( centr.z < 0 ) ? vol / ( underground_div * std::abs( centr.z ) ) : vol;
```

**Fix:** Expose `HORDE_HEARING_MIN_VOLUME`, `HORDE_SIGNAL_DIVIDER`, `HORDE_SIGNAL_MIN` and `HORDE_SIGNAL_MAX` as EXTERNAL_OPTIONs, read once per `process_sounds()`. Optionally add a monstergroup `"hearing_mult"`.

### SPAT-J13 · `u_teleport` EOC hardwires safe mode and no messages; docs wrongly mark monsters unsupported
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/npctalk.cpp:8343-8344`, `src/teleport.cpp:181-182`, `src/teleport.cpp:326-329`, `src/teleport.cpp:342-382`, `src/teleport.cpp:397-398`, `src/magic_spell_effect.cpp:170-183`, `doc/JSON/EFFECT_ON_CONDITION.md:4836`

The EOC always calls `teleport_to_point(..., safe=true, add_teleglow=false, display_message=false, force, force_safe)`. The talker table marks Monster as unsupported, but `talker_monster::get_creature` makes monsters teleport correctly. Unsafe teleports and teleglow can already be had through a spell with `TARGET_TELEPORT`+`UNSAFE_TELEPORT` cast via `u_cast_spell`, and through `u_add_effect "teleglow"`. So the gap is convenience and doc accuracy. Exposing `safe: false` today would also run into SPAT-B07.

```cpp
// npctalk.cpp:8343-8344
teleport::teleport_to_point( *teleporter, get_map().get_bub( target_pos ), true, false, false, force, force_safe )
```

**Fix:** After fixing SPAT-B07, add optional keys (defaults keep current behaviour):
```json
{ "u_teleport": { "global_val": "dest" }, "safe": true, "teleglow": "30 minutes", "display_message": true }
```
Mark Monster as supported in the talker table, and make the 30-minute teleglow a parameter of `teleport.cpp`.

### SPAT-J14 · Clairvoyance-granting field hardcoded to `fd_clairvoyant`
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/lightmap.cpp:1117-1119`, `src/map.cpp:7787-7791`, `src/character.cpp:7299-7301`

`apparent_light_at` forces BRIGHT on tiles containing the single cached field id `fd_clairvoyant`, and `character.cpp:7299` hardcodes the same id for seeing creatures. Mods already get vision by spawning or redefining `fd_clairvoyant` (Magiclysm, MindOverMatter, Aftershock), so the only gap is that a field cannot keep its own look and also grant vision.

```cpp
// lightmap.cpp:1117
if( cache.clairvoyance_field && field_at( p ).find_field( *cache.clairvoyance_field ) ) { return lit_level::BRIGHT; }
```

**Fix:** Add a field_type flag `GRANTS_VISION` (or per-intensity `"reveals_tile": true`). Build the list of such fields at finalize, or cache a per-level bitset when fields change, and use it at both sites.

### SPAT-J15 · Dawn/dusk light tint hardcoded and limited to the `default` dimension
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/lightmap.cpp:53`, `src/lightmap.cpp:76-99`, `src/lightmap.cpp:120-126`, `src/lightmap.cpp:586-587`, `src/overmap_worldgen.cpp:237-240`

The twilight tint is fork-specific code. Its hue (25 + 20*progress), saturation 0.8 and strength 0.35 are constants, and every dimension except `default` gets no tint. `dimension_world::load` reads only `region_layout`, so modded dimensions cannot opt in or recolour it.

```cpp
static const dimension_id dimension_world_default( "default" );
if( dimension != dimension_world_default ) { return {}; }
const float hue = 25.0f + progress * 20.0f; ... const float tint_strength = ease * 0.35f;
```

**Fix:** Add dimension JSON:
```json
"twilight_tint": { "hue_start": 25, "hue_end": 45, "saturation": 0.8, "strength": 0.35 }
```
No key means no tint; the default dimension gets the current values in data. Look the values up through the `dimension_world` object instead of comparing ids.

### SPAT-J16 · Soundpack fallbacks hardcoded: footstep terrain lists, engine fuel ids, weather sound enum
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/sounds.cpp:1789-1823`, `src/sounds.cpp:1904-1925`, `src/sounds.cpp:946-957`, `src/sounds.cpp:1251-1311`, `src/sounds.cpp:1637-1650`, `doc/SOUNDPACKS.md:279-284`

Soundpacks can override by exact terrain or engine id, but the fallbacks are fixed. About 44 terrain ids sit in four hardcoded `std::set`s (grass, dirt, metal, chain fence); any modded terrain plays `walk_tarmac`. Engine variants come from three fuel itype ids. Weather ambience maps a closed `sound_category` enum to fixed sfx ids, so new weather such as acid rain cannot get its own ambience. Danger-music thresholds (5/10/15/20) are also fixed.

```cpp
static const std::set<ter_str_id> grass = { ter_t_grass, ter_t_shrub, ... };
if( grass.count( terrain ) > 0 ) ... play_plmove_sound_variant( "walk_tarmac", ... );
else if( veh->is_engine_type( vp, fuel_type_muscle ) ) { variant = "muscle"; }
```

**Fix:** Move the lists into data with ter/furn `"footstep_sound": "walk_grass"`. Add fuel/ammotype `"engine_sound_variant"`, and on weather_type:
```json
"ambient_sfx": { "id": "environment", "variant": "WEATHER_ACID" }
```

### SPAT-J17 · Cellular-automaton generator only reachable from hardcoded `mx_pond`
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/cellular_automata.h:40`, `src/map_extras.cpp:633`, `src/map_extras.cpp:641-642`, `src/map_extras.cpp:657-670`, `src/map_extras.cpp:676`

`generate_cellular_automaton` is a general organic-shape generator, but its only caller is the C++ map extra `mx_pond`. That caller hardcodes the parameters (55/5/4/3), the terrains `t_water_sh`/`t_water_dp`, and `GROUP_FISH`. JSON mapgen has no cellular or noise piece, so tar pits, fungal blooms, cave pockets and regional pond reskins need C++.

```cpp
CellularAutomata::generate_cellular_automaton( width, height, 55, 5, 4, 3 );
m.ter_set( location, ter_t_water_sh ); ... ter_t_water_dp ...
```

**Fix:** Add a mapgen piece, then re-express `mx_pond` as a JSON `update_mapgen` map extra:
```json
"place_cellular": [ { "x": [0,23], "y": [0,23], "alive": 55, "iterations": 5, "birth_limit": 4, "stasis_limit": 3,
                      "ter": "t_water_sh", "interior_ter": "t_water_dp", "interior_neighbors": 8 } ]
```

### SPAT-J18 · Overmap route costs are a C++ table over a fixed enum
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/simple_pathfinding.cpp:500`, `src/simple_pathfinding.cpp:501`, `src/simple_pathfinding.cpp:452`, `src/overmapbuffer.cpp:983`, `src/overmapbuffer.cpp:1013`, `src/overmapbuffer.h:54`, `src/omdata.h:338`, `src/overmap_terrain.cpp:611`, `src/overmap_ui.cpp:1960`

The per-terrain travel costs live in hardcoded `overmap_path_params` presets (`for_player`, `for_land_vehicle`, `for_watercraft`, `for_aircraft`), keyed by a 17-value enum. oter JSON can only pick one enum value. The only runtime changes are hardcoded swimming and `LEVITATION` overrides. Modders cannot add rail or ice classes, mount or bicycle profiles, or swamp-adapted mutants. The hardcoded 24 heuristic is covered as a bug in SPAT-B11.

```cpp
// overmapbuffer.cpp:987-1000
ret.set_cost( oter_travel_cost_type::highway, 24 ); ... swamp, 240 ...
// omdata.h:338
enum class oter_travel_cost_type { other, impassable, highway, ... tunnel, last };
```

**Fix:** Add a JSON type and use string travel classes instead of the enum:
```json
{ "type": "overmap_travel_profile", "id": "on_foot", "costs": { "road": 24, "forest": 72, "swamp": 240 }, "allow_diagonal": true }
```
Let characters and vehicles pick a profile, optionally with an enchantment multiplier.

## 2. Bugs, oversights & quirks

### SPAT-B01 · A* calls the interactive `find_or_make_stairs`: modal prompts, rope/grapnel use, avatar-relative stairs
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/pathfinding.cpp:169`, `src/pathfinding.cpp:183`, `src/pathfinding.cpp:680`, `src/game.cpp:10029`, `src/game.cpp:10057`, `src/game.cpp:10065`, `src/game.cpp:10166`, `src/game.cpp:10008`, `src/game.cpp:10011`, `src/game.cpp:3720`, `src/player_activity.cpp:504`, `src/game.cpp:6856`, `src/map.cpp:292`

`for_each_vertical_step()` runs for every expanded A* node. For each stair it calls `g->find_or_make_stairs(..., peeking=false, cur)`, the player's interactive climb routine. When `cur` is the avatar's tile, `is_avatar` is true, and the routine can open `query_yn` dialogs from inside the pathfinder: "Attempt to push past?", "Climb your grappling hook down?" or "Detach a vine?". Answering yes uses up the rope or grapnel, and the vine branch applies pain, damage and kcal changes. In `route()`, the start node is expanded first. `game::is_hostile_within(5, true)` routes from the avatar with `allow_climb_stairs=true` every turn through `player_activity::get_distractions`, and also from the butcher pre-check (`game.cpp:6856`). So if you craft on a staircase while a visible zombie stands on the stairs below (`GOES_DOWN` tiles are see-through, `map.cpp:292`), you get a popup every turn. A second problem: `find_stairs` ranks candidates by distance to `u.pos` and skips the scan when the avatar is in open air, so monster and NPC routes pick the stair nearest the player. `monster::move_to` uses `find_closest_stair` relative to the mover, so the two disagree.

```cpp
// pathfinding.cpp:169
g->find_or_make_stairs( get_map(), cur.z() - 1, rope_ladder, false, cur );
// game.cpp:10029
const bool is_avatar = u.pos_bub( mp ) == pos;
// game.cpp:10057
query_yn( _( "%s  Attempt to push past? ..." ) )
// game.cpp:10011
if( rl_dist( u.pos_bub( mp ), dest ) <= best && ...
```

**Fix:** Make pathfinding free of side effects. Add a const stair lookup (or adapt `find_stairs`) that ranks candidates by distance to `pos` and never prompts, and call it from `for_each_vertical_step`. Keep `find_or_make_stairs` for the avatar's real movement only.

### SPAT-B02 · 3D bresenham stops early when |dx|==|dz|<|dy| or |dy|==|dz|<|dx| (LOS through walls, short shots)
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/line.cpp:158`, `src/line.cpp:171`, `src/map.cpp:8465`, `src/map.cpp:8837`, `src/map.cpp:8667`, `src/ballistics.cpp:361`, `tests/line_test.cpp:89`

The tie branches do not check that the tied pair is actually the major axis. The `a.x == a.z` branch loops on x even when y is larger, and the `a.y == a.z` branch loops on y even when x is larger. The line therefore ends after `min(|dx|,|dy|)` steps. (0,0,0)->(1,5,1) gives only `[(1,0,1)]`, and ->(2,6,2) gives `[(1,0,1),(2,1,2)]`. `map::sees` and `map::clear_path` never test the skipped tiles, so they report "visible/clear". The sees result is also stored in the skew cache. NPC sight, monster-to-NPC sight and all projectiles are affected; monster-to-avatar sight uses `seen_cache` and is not. A shot at a target one level up and offset (1,5) gets a one-tile trajectory, which `continue_line` then extends along the wrong slope. The tests cover only unit offsets.

```cpp
} else if( a.x == a.z ) {
    while( cur.x() != loc2.x() ) {   // y may be the dominant axis here
...
} else if( a.y == a.z ) {
    while( cur.y() != loc2.y() ) {   // x may be the dominant axis here
```

**Fix:** Change the conditions to `a.x == a.z && a.x >= a.y` and `a.y == a.z && a.y >= a.x`, so other cases fall through to the generic x-major and y-major branches. Add `line_test` cases (1,5,1), (5,1,1) and (2,6,2) that assert the last point equals the target.

### SPAT-B03 · Sound clustering merges distant sounds into cluster 0 (wrong distance cap), creating phantom loud sounds
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/sounds.cpp:389`, `src/sounds.cpp:406`, `src/sounds.cpp:407`, `src/sounds.cpp:414`, `src/sounds.cpp:458`

`cluster_sounds()` starts with `found_centroid = begin()` and `dist_factor = max_map_distance`, then compares `dist*dist` against that unsquared value. `max_map_distance` is also measured from z=+10 to z=+10, so it has no vertical term (132, or 186 with trigdist). With more than 10 sounds in a turn, any sound more than about 11-13 units from every seed falls into the first cluster, which then takes the maximum volume and a volume-weighted average position. One z-level underground already counts as 25 or more units. Monsters hear a loud sound where nothing happened, and hordes are signalled there. The averaged z can be fractional, and `get_signal_for_hordes` divides by `2*|z|`, so z=-0.3 amplifies the horde signal 1.67x.

```cpp
const size_t max_map_distance = sound_distance( { point_bub_ms::zero, OVERMAP_DEPTH }, { MAPSIZE_X, MAPSIZE_Y, OVERMAP_HEIGHT } );
auto found_centroid = sound_clusters.begin(); float dist_factor = max_map_distance;
if( dist * dist < dist_factor ) {
```

**Fix:** Initialise `dist_factor` to `std::numeric_limits<float>::max()` (or the cap squared) and use `-OVERMAP_DEPTH` for the endpoint. Optionally refuse to merge across z-levels. In `get_signal_for_hordes`, round z and divide only when z <= -1.

### SPAT-B04 · `process_sounds` iterates `trap_locations` while trap triggers erase from it
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/sounds.cpp:505`, `src/sounds.cpp:506`, `src/sounds.cpp:512`, `src/sounds.cpp:518`, `src/trapfunc.cpp:1680`, `src/trapfunc.cpp:1681`, `src/map.cpp:7320`, `src/map.cpp:10398`, `data/json/monsters/zed_dormant.json:61`

The range-for walks the live `traplocs` vector returned by reference. The only vanilla sound trap, `tr_dormant_corpse` (action `spell`, not `UNCONSUMED`), calls `remove_trap`, which erases from that vector during the loop. Because `trapfunc::cast_spell`'s `p` is a reference to the vector element, after the erase it points at the next corpse's position. The wake spell and its sound fire at the wrong corpse, and the original trap disappears without waking anyone. The stale end iterator then re-reads the shifted slot. A gunshot near two or more dormant corpses is enough. Sounds made by triggered traps are also lost, because `recent_sounds.clear()` runs right after the loop.

```cpp
for( const tripoint_bub_ms &tp : here.trap_locations( trapType->id ) ) {
    ...
    tr.trigger( tp );
// trapfunc.cpp:1680
if( !tr.has_flag( json_flag_UNCONSUMED ) ) { here.remove_trap( p ); }
```

**Fix:** Copy the location vector before the loop, and re-check `here.tr_at(tp).loadid == trapType->loadid` before triggering each trap. Clear `recent_sounds` before the trigger loop, or swap it into a local first.

### SPAT-B05 · Out-of-bounds write in `add_light_source` for wall-mounted lights at the bubble edge
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/lightmap.cpp:796`, `src/lightmap.cpp:815`, `src/lightmap.cpp:821`, `src/lightmap.cpp:918`, `src/lightmap.cpp:1627`, `src/lightmap.cpp:1633`, `src/lightmap.cpp:1768`, `src/veh_appliance.cpp:91`, `src/mdarray.h:64`

For `HALF_CIRCLE_LIGHT` + `WALL_MOUNTED` parts, only `src` is bounds-checked. The light is then offset one tile and passed to `add_light_source` (no bounds check), `apply_light_arc` (unguarded colour write) and `apply_light_source`. `mdarray::operator[]` does not check bounds either. In vanilla, appliances always face 0 degrees, so `ap_wall_light` offsets +1 x. A powered wall light at bubble x=131 (the east edge) writes past the end of the buffers on every relight. West and north cases need modded or vehicle parts.

```cpp
if( !inbounds( src ) || src.z() != zlev ) { continue; }
tripoint_bub_ms offset = src; offset.x() = src.x() + tdir.dx(); ... add_light_source( offset, M_SQRT2, vp.light_color );
auto &buf = get_cache( p.z() ).light_source_buffer[p.x()][p.y()];       // :918, no inbounds()
light_color_cache[p2.x()][p2.y()] += color * LIGHT_SOURCE_LOCAL;        // :1768, unguarded
```

**Fix:** Add `if( !inbounds( p ) ) return;` at the top of `add_light_source`. Guard the colour write in `apply_light_arc` with `lightmap_boundaries.contains(p2)`. Skip the wall-mounted offset when it falls outside the bubble.

### SPAT-B06 · Pending sound queues are stored in bubble coordinates and not shifted with the map
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/sounds.cpp:246`, `src/sounds.cpp:248`, `src/sounds.cpp:250`, `src/game.cpp:7895`, `src/game.cpp:7913`, `src/game.cpp:10307`, `src/game.cpp:10327`, `src/map.cpp:1946`, `src/do_turn.cpp:742`

`recent_sounds`, `sounds_since_last_turn` and `sound_markers` are static containers keyed by `tripoint_bub_ms`. `game::update_map` shifts monsters, NPCs, the destination and scent, but not sounds. `walk_move` makes footstep and clatter noise before `place_player` -> `update_map`. So each time the player crosses a submap boundary, that noise is moved 12 tiles ahead, and monsters investigate an empty spot. Vehicle noise during `vehmove` (`map.cpp:1946`) and sounds queued before a teleport or fling recentring have the same problem.

```cpp
static std::vector<std::pair<tripoint_bub_ms, monster_sound_event>> recent_sounds;
// game.cpp:7895  u.make_footstep_noise();  ...  game.cpp:7913  place_player( dest_loc );
// game.cpp:10306-10327  shift_monsters(...); ... scent.shift( shift_ms );   // no sounds:: shift
```

**Fix:** Add `sounds::shift(point_rel_ms)` that offsets every queued position and rekeys `sound_markers`, and call it next to `scent.shift()`. Alternatively, store sounds as `tripoint_abs_ms` and convert them when processed.

### SPAT-B07 · Teleport telefrag never happens for players or NPCs: collision message 5 times, then "You flicker"
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/teleport.cpp:307`, `src/teleport.cpp:309`, `src/teleport.cpp:342`, `src/teleport.cpp:349`, `src/teleport.cpp:358`

In the telefrag loop, all the collision handling (`collision = true`, fling, explosion, damage) is in the `else` of `if( p )`, where `p` is the teleporting Character. For the avatar or an NPC, only the message prints. The occupant never moves, so the `while( creature_at(...) )` loop repeats until the attempts run out: the message appears 5 times, then "You flicker." and the teleport fails. Every non-safe caller can reach this (`iuse.cpp:2961`, `bionics.cpp:943`, `map_field.cpp:1721`, `suffer.cpp:1665`, `player_hardcoded_effects.cpp:516`, `UNSAFE_TELEPORT` spells).

```cpp
if( p ) {
    if( display_message ) { p->add_msg_player_or_npc( ..., "You collide with %s mid teleport..." ); }
} else {
    ... collision = true; ... g->fling_creature( poor_soul, ... ); explosion_handler::explosion(...); ...
}
```

**Fix:** Close the `if( p ) {...} else {...}` right after the messages and run the collision, fling, explosion and damage for every teleporter type. Keep only the visibility message conditional.

### SPAT-B08 · `teleport_creature` passes integer degrees as radians and truncates, so 1-tile shifts do nothing
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/teleport.cpp:173`, `src/teleport.cpp:175`, `src/teleport.cpp:184`, `src/suffer.cpp:1678`, `src/magic_spell_effect.cpp:192`

`rangle = rng(0,360)` is passed straight to `std::cos/sin`, which take radians, and the offset is truncated with `int()`. For (1,1), the result is the origin 99.7% of the time, so the teleport returns false at :184. The resonance "You suddenly shift slightly." (`suffer.cpp:1678`) prints but almost never moves you. For (2,2), 99.7% of results fall below the minimum distance; for (2,12), 9.1% do. The retry loop rejects only impassable tiles, so short-range teleport spells, which pass range and range+aoe as min/max, have an unreliable minimum range.

```cpp
int rangle = rng( 0, 360 );
new_pos = { origin.x() + int( rdistance * std::cos( rangle ) ), origin.y() + int( rdistance * std::sin( rangle ) ), new_pos.z() };
```

**Fix:** Use `units::from_degrees(rng(0,359))` with `units::cos/sin`, round instead of truncating, and retry (within the tries budget) when `square_dist < min_distance` or the result equals the origin.

### SPAT-B09 · Monster path-avoid bash test is inverted: bashers refuse to route through bashable obstacles
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monster.cpp:4370`, `src/pathfinding.cpp:545`, `src/pathfinding.cpp:613`, `src/pathfinding.cpp:398`, `src/pathfinding.cpp:416`, `src/monmove.cpp:2055`

This defect is in `monster.cpp`, outside the file list, but it breaks `map::route`. The comment says "if we can't move there and can't bash it, don't path through it", but the code tests `!bash_skill().empty()`, which means "can bash". This looks like a leftover from when `bash_skill` was an int. `route()` closes avoided tiles before `cost_to_pass` runs, so for bashing monsters both the bash-cost branch (:416) and the door-opening branch (:398) are dead. Ordinary zombies have no `path_settings` and never use A*. The roughly 100 monster types that have both `path_settings` and a bash skill (for example feral humans with `allow_open_doors` and `max_dist` 45) cannot path through closed doors and fall back to straight-line movement.

```cpp
// If we can't move there and can't bash it, don't path through it.
if( !can_move_to( p ) && ( !bash_skill().empty() || !here.is_bashable( p ) ) ) { return true; }
```

**Fix:** Change the test to `( bash_skill().empty() || !here.is_bashable( p ) )`. Better still, let `cost_to_pass` decide about bashing (it already uses `settings.bash_strength`) and limit the avoid callback to creature avoidance.

### SPAT-B10 · `scent_neutralization` magnitude is ignored: any value wipes scent in a 3x3 area; `apply_slime_factor` misdocumented
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/scent_block.cpp:13`, `src/scent_block.cpp:30`, `src/scent_block.cpp:50`, `src/scent_block.cpp:53`, `src/scent_block.cpp:54`, `src/map_field.cpp:289`, `src/map_field.cpp:294`, `src/map_field.cpp:295`, `src/map_field.cpp:593`, `src/map_field.cpp:2276`, `src/scent_map.cpp:141`, `doc/JSON/JSON_INFO.md:4464`, `doc/JSON/JSON_INFO.md:4477`, `data/json/field_type.json:588`

The docs say `scent_neutralization` reduces scent "by this value". `apply_gas` starts from a fresh datum (`{NONE, 0}`), computes `max(0, 0 - n) = 0` in SET mode, and the commit writes 0. Smoke (1) and tear gas (5) therefore both zero scent completely, on all 9 tiles of `points_in_radius(p,1)`. The JSON magnitudes in `field_type.json` mean nothing. This also lets players cheat: standing next to campfire smoke makes them unsmellable. The neutralization runs only inside `spread_gas`, so it only works on spreading gas fields, with no `check()` warning for others. The commit can also overwrite the 2D scent map from an adjacent z-level. `apply_slime_factor` is documented as "slime to apply to creatures", but it actually MAX-raises the scent map at the tile, and because of the single global type (SPAT-J07) the trail smells like the player.

```cpp
// scent_block.cpp:53-55
assignment[...].mode = data_mode::SET;
assignment[...].intensity = std::max( 0, assignment[...].intensity - nintensity );   // intensity starts at 0
// map_field.cpp:294-296
for( const tripoint_bub_ms &tmp : points_in_radius( p, 1 ) ) { sblk.apply_gas( tmp, scent_neutralize ); }
```

**Fix:** Add a `data_mode::SUBTRACT` that accumulates n and commits `set_unsafe(p, max(0, get_unsafe(p) - n))`. Restrict commits to the player's z. Optionally add `"scent_neutralization_radius"`, and warn in `field_type::check` when neutralization is set on a non-spreading or non-gas field. Rewrite the `apply_slime_factor` doc ("adds scent of intensity*factor at this tile") and add `"scent_type"` to field_type.

### SPAT-B11 · Overmap A* uses a fixed 24-per-OMT heuristic and never re-queues improved nodes
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/simple_pathfinding.cpp:484`, `src/simple_pathfinding.cpp:486`, `src/simple_pathfinding.cpp:500`, `src/simple_pathfinding.cpp:501`, `src/overmapbuffer.cpp:1018`, `src/overmap_ui.cpp:1964`, `src/overmap_ui.cpp:1989`

The remaining-cost estimate is `octile_dist * 24`. That is admissible for on-foot profiles (all costs >= 24), but autodrive costs highway 3, road 8 and field 12, boats and aircraft cost 8, levitation air costs 8, and direct travel is log2-flattened. For those profiles the heuristic overestimates 3-8x, so the search becomes greedy and autodrive may take a cross-country route over a cheaper road. When a node's cost improves, only `cumulative_cost`/`prev_dir` change; the node is not pushed again, so the improvement never reaches its children. The unused `max_cost` pruning would also wrongly reject reachable goals.

```cpp
if( next_node.cumulative_cost > cumulative_cost ) { next_node.cumulative_cost = cumulative_cost; next_node.prev_dir = ...; }  // no push
// TODO: pass in the 24 (default terrain cost)
const int xy_score = octile_dist( next_point.xy(), dest.xy(), 24 );
```

**Fix:** Pass the minimum positive cost from `overmap_path_params` into `find_overmap_path` and use it as the heuristic multiplier (scale the z cost to match). On improvement, push a new `scored_address` and skip stale queue entries.

### SPAT-B12 · Detached melee sfx thread reads the map, the calendar and the global RNG concurrently
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/sounds.cpp:1441`, `src/sounds.cpp:1490`, `src/sounds.cpp:1493`, `src/weather.cpp:82`, `src/rng.cpp:20`, `src/rng.cpp:207`

In SDL_SOUND builds, every melee swing starts a detached thread whose own comment says it must not touch game data. It calls `rng()` and `rng_float` (the global engine and a static distribution) and reads `calendar::turn`. It also calls `is_creature_outside(get_player_character())`, which reads the map's outside cache. This is a formal data race and undefined behaviour. In practice it means a wrong sound variant and RNG state contention; the use-after-free on quit is speculative.

```cpp
std::thread the_thread( sound_thread(...) ); ... the_thread.detach();
// must not access game data ...
std::this_thread::sleep_for( std::chrono::milliseconds( rng( 1, 2 ) ) );
const bool indoors = !is_creature_outside( get_player_character() );
```

**Fix:** Compute season, indoors, night and both sleep durations in the `sound_thread` constructor on the main thread. Use a `thread_local` RNG for any jitter inside the thread.

### SPAT-B13 · A* ledge descent writes to a z-layer the pathfinder never reset
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/pathfinding.cpp:79`, `src/pathfinding.cpp:104`, `src/pathfinding.cpp:568`, `src/pathfinding.cpp:645`, `src/pathfinding.cpp:652`, `src/pathfinding.cpp:665`, `src/pathfinding.cpp:669`

`pf.reset(min.z, max.z)` clears only the layers between the start and target levels. The `avoid_traps` ledge/open-air descent adds `p + below`, which can be at `min.z - 1`, a layer that still holds closed and open bits from earlier, unrelated searches. `add_point` rejects nodes on that stale data, so whether a creature finds a "drop down" route depends on what was pathed earlier. Only the gscore read was patched (see the comments at :649 and :682).

```cpp
pf.reset( min.z(), max.z() );                          // :568
tripoint_bub_ms below( p + tripoint::below );           // :645
pf.add_point( new_g, new_g + 2 * rl_dist( below, t ), cur, below );
if( layer.closed[index] ) { return; }                   // :104
```

**Fix:** When `avoid_traps` is set, use `pf.reset(std::max(min.z()-1, -OVERMAP_DEPTH), max.z())`. Alternatively, give each layer a generation stamp and clear it lazily on first touch.

### SPAT-B14 · `map::route` excludes the last row and column: east/south edge targets are unreachable
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/pathfinding.cpp:563`, `src/pathfinding.cpp:566`, `src/pathfinding.cpp:609`, `src/pathfinding.cpp:524`, `src/map.cpp:12189`, `src/pathfinding.cpp:995`

`clip_to_bounds` clamps `max` to `MAPSIZE-1` inclusive, but the neighbour test treats `max` as exclusive. Out-of-bubble targets are clipped onto exactly that edge (:524), so they can only be reached by the straight-line shortcut. Any obstacle makes A* fail. The west and north edges work. `route_with_grab` does this correctly.

```cpp
if( p.x() < min.x() || p.x() >= max.x() || p.y() < min.y() || p.y() >= max.y() ) { continue; }
// grab variant: max_bound.x() = std::min( max_bound.x(), MAPSIZE_X );   // exclusive, correct
```

**Fix:** Use `p.x() > max.x() || p.y() > max.y()`, or compute an exclusive `max` the way `route_with_grab` does.

### SPAT-B15 · `vertical_move_destination` reads the pathfinding cache out of bounds near the bubble edge
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/pathfinding.cpp:140`, `src/pathfinding.cpp:141`, `src/game.cpp:10013`, `src/game.cpp:10014`, `src/coordinates.h:1149`

`closest_points_first(t.xy(), SEEX)` is not clipped, and `pf_cache.special[x][y]` is read before any bounds check. Going up a sewer ladder (`t_manhole_cover` has no `GOES_DOWN`) scans up to 12 tiles and can read out of bounds. The bounds-checked `has_flag` keeps a bogus destination from being returned; the only effect is the undefined read.

```cpp
for( const point_bub_ms &p : closest_points_first( t.xy(), SEEX ) ) { if( pf_cache.special[p.x()][p.y()] & ...
```

**Fix:** Skip points that fail `m.inbounds(tripoint_bub_ms(p, t.z()))` inside the loop.

### SPAT-B16 · `teleport_to_point` unboards the passenger before validating, so failed teleports leave them unseated
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/teleport.cpp:205`, `src/teleport.cpp:206`, `src/teleport.cpp:254`, `src/teleport.cpp:303`, `src/teleport.cpp:316`, `src/teleport.cpp:324`, `src/teleport.cpp:335`, `src/teleport.cpp:381`, `src/map.cpp:1761`, `src/npctalk.cpp:8343`

`unboard_vehicle` runs before the impassable, safe-mode, anchor and telefrag checks. For a driver it also stops autodrive and sets `skidding=true`. Any later `return false` leaves the character on the vehicle tile but no longer seated, so a moving vehicle can drive off without them or run them over. Safe spell teleports and `u_teleport` hit these paths whenever the destination is blocked. A Character teleporter landing on an occupied tile always fails (SPAT-B07).

```cpp
if( p && p->in_vehicle ) { here.unboard_vehicle( p->pos_bub() ); }   // :205-207
restore_map_on_failure(); return false;                               // :253-254, likewise 302, 315, 323, 334
```

**Fix:** Move the unboard to just before `critter.move_to(abs_ms)` (:381), after all the failure checks.

### SPAT-B17 · `for_each_vertical_step` returns early on a missing down-stair, also skipping up-stairs and ramps
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/pathfinding.cpp:168`, `src/pathfinding.cpp:171`, `src/pathfinding.cpp:176`, `src/pathfinding.cpp:182`, `src/pathfinding.cpp:207`

In the `GOES_DOWN` branch, `if( !opt_dest ) return;` exits the whole function, so the `GOES_UP` and ramp steps never run for that node. Five vanilla terrains carry both flags (`t_ladder_up_down`, `t_scaffolding_pipe_updown`, `t_string_z`, `t_string_xyz`, `t_scrap_scaffolding_scarlet`). The trigger is narrow: a route spanning three or more levels through such a ladder whose lower floor has no matching stair. The `!inbounds(dest)` return is effectively dead code.

```cpp
if( !opt_dest ) { return; }          // :171-173
if( !m.inbounds( dest ) ) { return; } // :176-178
```

**Fix:** Turn the two stair branches into independent `if( opt_dest && ... ) { step(...); }` blocks.

### SPAT-B18 · `calc_ray_end` square-distance mode uses 215 instead of 225 degrees
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/line.cpp:1013`, `src/magic_spell_effect.cpp:277`, `src/magic_spell_effect.cpp:1156`, `src/vehicle_move.cpp:948`, `src/options.cpp:1808`

The octant boundaries are 45/135/225/315, but the code uses 215. For angles in (215,225], the y-dominant formula is used in an x-dominant octant, so rays overshoot by up to about 1.43x. This affects spell cones and lines, push/pull distances and vehicle animal-push. It only happens with `CIRCLEDIST` off; the default is on.

```cpp
if( angle <= 45_degrees || ( 135_degrees <= angle && angle <= 215_degrees ) || 315_degrees < angle ) {
```

**Fix:** Change `215_degrees` to `225_degrees`.

### SPAT-B19 · Modded vehicle cameras with `bonus` > 60 read past the trig-distance lookup table
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/lightmap.cpp:1517`, `src/lightmap.cpp:1242`, `src/lightmap.cpp:1275`, `src/lightmap.cpp:1193`, `src/veh_type.cpp:265`

`offsetDistance = MAX_VIEW_DISTANCE - bonus*hp/durability` goes negative for `bonus` > 60, so the cast radius goes past 60. With `CIRCLEDIST` on (the default), `trig_dist_2d_lut()` (61x61) is then indexed with offsets up to about 131. `bonus` is loaded with no range check. No camera in the repo exceeds the limit (omnicam is 24), so only third-party mods can trigger this.

```cpp
offsetDistance = MAX_VIEW_DISTANCE - vpi_mirror.bonus * vp_mirror.hp() / vpi_mirror.durability;
constexpr int N = MAX_VIEW_DISTANCE + 1; std::array<std::array<int, N>, N> t{};
```

**Fix:** Clamp `offsetDistance` to >= 0 (or the radius to `MAX_VIEW_DISTANCE`). Add a vpart check that warns when a `CAMERA` part's `bonus` exceeds `MAX_VIEW_DISTANCE`.

### SPAT-B20 · Latent math bugs: `rl_vec3d::dot_product` uses `y*v.z`, `rotated()` drops z; lightmap top-floor constant
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/line.cpp:866`, `src/line.cpp:868`, `src/line.cpp:853`, `src/lightmap.cpp:610`, `src/lightmap.cpp:611`

`dot_product` returns `x*v.x + y*v.y + y*v.z`. `rotated()` uses the two-argument constructor, so z becomes 0. Neither function has callers today. `generate_lightmap` uses `OVERMAP_DEPTH` where `OVERMAP_HEIGHT` is meant. Both constants are 10, so this only breaks if a fork changes one of them.

```cpp
return x * v.x + y * v.y + y * v.z;                         // line.cpp:868
return rl_vec3d( x * cos - y * sin, x * sin + y * cos );    // line.cpp:855-858, z dropped
bool top_floor = zlev == OVERMAP_DEPTH;                     // lightmap.cpp:611
```

**Fix:** Use `z * v.z`, pass z through in `rotated()`, and use `OVERMAP_HEIGHT` at `lightmap.cpp:610-611`.

## 3. Performance

### SPAT-P01 · A* calls the expensive `avoid()` callback before the closed check, up to 8 times per tile
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/pathfinding.cpp:613`, `src/pathfinding.cpp:618`, `src/pathfinding.cpp:50`, `src/pathfinding.cpp:87`, `src/pathfinding.cpp:114`, `src/monster.cpp:4365`, `src/npc.cpp:3832`, `src/monmove.cpp:158`, `src/monmove.cpp:1208`, `src/npcmove.cpp:3592`

For each of the 8 neighbours of every expanded node, `avoid(p)` runs before `layer.closed[index]` is checked, so closed and open tiles pay the full cost each time they are neighbours. For monsters that cost is `can_move_to` -> `will_move_to`: a vector allocation for impassable field ids, a walk over the tile's fields, about 10 `has_flag` lookups and a `creature_at` call. For NPCs it is `path_avoids` (creature lookup, door probes, `is_open_air`, `sees_dangerous_field`, reservation checks). `route()` runs per pathing NPC per turn while the target moves, per monster whenever its destination changes, and several times per step in zone activities. Both callbacks have no side effects and depend only on `p`.

```cpp
if( !target.contains( p ) && avoid( p ) ) {
    layer.closed[index] = true;
    continue;
}
if( layer.closed[index] ) {
    continue;
}
```

**Fix:** (1) Move the closed test above the avoid test; behaviour does not change. (2) Memoize per search with a `std::bitset<MAPSIZE_X*MAPSIZE_Y> avoid_checked` cleared in `reset()`. Together these cut avoid calls about 4-8x. (3) Drop the write-only `score` array (:114, 70 KB per layer) and keep the open-list capacity between calls.

### SPAT-P02 · Whole-level relight triggered by changes that cannot alter light
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/map.cpp:11047`, `src/map.cpp:11060`, `src/map.cpp:11114`, `src/lightmap.cpp:387`, `src/lightmap.cpp:400`, `src/map.cpp:7637`, `src/monster.cpp:367`

`generate_lightmap` relights a whole level: about 1 MB of cache fills, a 17,424-tile scan, and casting every light. Three triggers fire far more often than light actually changes. (a) The light-source diff in `build_map_cache` includes every Character at its position, lit or not, so every avatar or NPC step dirties the level, even though `apply_character_light` does nothing at or below `LIGHT_AMBIENT_LOW`. (b) `map::on_field_modified` dirties on every field change (blood, smoke, acid); fields that block light already relight through the transparency path. (c) `monster::on_effect_int_change` dirties on any effect change, although monster luminance and on-fire state are already in the diff. Dirty flags coalesce until the next draw, so the cost is about one extra relight per interactive step or combat turn.

```cpp
auto character_light = []( const Character & ch ) -> light_source_state {
    float light = ch.active_light(); ...
    return { light, ch.pos_bub(), 0 };
};
current_lights.push_back( character_light( get_player_character() ) );   // :11060, and every NPC
// map.cpp:7634-7636 on_field_modified: set_lightmap_cache_dirty( p.z() );  unconditionally
// monster.cpp:367-373 on_effect_int_change: here.set_lightmap_cache_dirty( posz() );
```

**Fix:** (a) Push a character only when its light exceeds `LIGHT_AMBIENT_LOW` or it is on fire or has `haslight`. (b) Dirty on field changes only when the field type emits light or overrides local light; precompute a bool at finalize. (c) Remove the dirty call in `on_effect_int_change`, or gate it on `effect_onfire`. Expected effect: removes a 0.5-2 ms relight from most steps.

### SPAT-P03 · 3D FOV cast computes a sqrt and `std::exp` for every tile it visits
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/shadowcasting.cpp:305`, `src/shadowcasting.cpp:477`, `src/shadowcasting.h:97`, `src/lightmap.cpp:1434`, `src/lightmap.cpp:1190`, `src/line.h:200`, `src/options.cpp:1806`

`cast_zlight` runs on every avatar move. Under open sky it covers about 11 levels and 8 octants, roughly 150k tiles. For each tile it calls `rl_dist` (a double sqrt with `CIRCLEDIST`, which is the default) and `sight_calc` (`std::exp`). Within a span, the distance and the cumulative transparency usually match the previous tile. The 2D `castLight` already replaced this sqrt with a lookup table, citing "the hottest code path".

```cpp
const int dist = rl_dist( tripoint_rel_ms::zero, delta ) + offset_distance;
last_intensity = calc( numerator, this_span->cumulative_value, dist );
// shadowcasting.h:97-100  return numerator / std::exp( transparency * distance );
```

**Fix:** Replace `rl_dist` with a lookup table: an isqrt table over `dx²+dy²+dz²` (about 11k ints), or `max(|dx|,|dy|,|dz|)` when trigdist is off. Reuse `last_intensity` when `dist` and `cumulative_value` match the previous tile. Results are identical; this saves about 1 ms per outdoor step.

### SPAT-P04 · `generate_lightmap` walks all 2,541 submaps to find vehicles, once per lit level
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/lightmap.cpp:785`, `src/lightmap.cpp:789`, `src/map.cpp:597`, `src/map.cpp:1646`, `src/map.cpp:11073`, `src/level_cache.h:178`, `src/vehicle.cpp:5505`

`get_vehicles()` iterates 11x11 submaps on all 21 levels. `v->lights()` then allocates a vector and scans every part of every vehicle before the parts are filtered by z. This repeats for each level that is relit. The fork already replaced this pattern in `build_map_cache`, with a comment that walking every submap costs more than the rest of a stationary build.

```cpp
VehicleList vehs = get_vehicles();
for( wrapped_vehicle &vv : vehs ) {
    vehicle *v = vv.v;
    auto lights = v->lights();
```

**Fix:** Iterate the level caches' `vehicle_list` (`level_cache.h:178`) as `build_map_cache` does. Optionally compute `lights()` once per `build_map_cache` and reuse it for every level.

### SPAT-P05 · `find_overmap_path` re-scores rejected nodes every time a neighbour is expanded
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/simple_pathfinding.cpp:427`, `src/simple_pathfinding.cpp:495`, `src/overmapbuffer.cpp:1081`, `src/overmapbuffer.cpp:1106`, `src/do_turn.cpp:453`, `src/npcmove.cpp:6043`, `src/npcmove.cpp:6065`

Rejected nodes (water, unknown, dangerous) are never stored, so the `std::function` scorer runs again up to 8 times per rejected node. Each run does up to 4 `overmap_buffer` hash lookups, with `ter_existing` called twice. `known_nodes` is a node-based `unordered_map` with no `reserve()` and up to 100k entries. Unreachable goals, such as NPC random goals in water, search all the way to the cap. This runs for travelling NPCs every turn, for NPC goal selection (whose own comment says it "waste[s] a lot of time"), and for player auto-travel.

```cpp
const omt_score next_score = scorer( next_point );
if( next_score.node_cost < 0 ) {
    // TODO: add to closed set to avoid re-visiting
    continue;
}
```

**Fix:** Keep rejected nodes in a separate closed set; putting them in `known_nodes` would count them against `max_search_count`. Reserve the map, or use an open-addressing flat map. Have one `ter_existing` lookup feed both the cost and `is_ramp`.

### SPAT-P06 · Every relight walks all vehicle cargo recursively, with no per-part lit-item gate
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/lightmap.cpp:847`, `src/lightmap.cpp:852`, `src/lightmap.cpp:128`, `src/lightmap.cpp:729`, `src/item.cpp:3321`, `src/item.cpp:3713`, `src/vehicle.cpp:6547`, `src/vehicle.cpp:6569`, `src/submap.h:190`

Ground items are gated by the submap lum count, but every cargo part on the level runs `add_light_from_items` over all its items. That recursion goes into transparent pockets and allocates a vector and a list per container. A second full `is_emissive` pass follows. Each cargo part also does a string `has_flag("COVERED")` lookup. A storage truck holding hundreds of items pays that cost on every relight.

```cpp
for( const vpart_reference &vpr : v->get_any_parts( VPFLAG_CARGO ) ) { ...
    if( !inbounds( pos ) || pos.z() != zlev || vpr.info().has_flag( "COVERED" ) ) { continue; }
    add_light_from_items( pos, vpr.items() );
    for( const item &it : vpr.items() ) { if( it.is_emissive() ) { ...
// ground is gated: lightmap.cpp:729  if( cur_submap->get_lum( { sx, sy } ) ) {
```

**Fix:** Keep a lit-item count per vehicle part, mirroring `submap::update_lum_add/rem`, and maintain it in `vehicle::add_item`/`remove_item` (which already call `is_emissive`) and in in-place transforms. Skip parts with a zero count. Have `add_item_light_recursive` report whether it emitted anything, so the second pass can go. Iterate pockets without building temporary containers, and use a cached bitflag for `COVERED`.

### SPAT-P07 · Any transparency change on any level rebuilds sunlight for every level and relights the levels above
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/map.cpp:11189`, `src/map.cpp:11206`, `src/lightmap.cpp:292`, `src/lightmap.cpp:444`, `src/lightmap.cpp:487`, `src/lightmap.cpp:597`, `src/map.cpp:401`

`update_sunlight` rebuilds all 21 levels on any sight or geometry revision change, and it gives every level a new `sun_revision`, which forces `generate_lightmap` on the avatar's level and the vision levels. A level's sunlight depends only on the levels above it. For example, a basement fire or a door opening below the avatar relights the avatar's floor every turn.

```cpp
for( int zlev = zlev_max; zlev >= zlev_min; zlev-- ) { ... map_cache.sun_revision = next_cache_generation();
const level_cache &prev_map_cache = get_cache_ref( zlev + 1 );
// generate_lightmap skips only if lightmap_sun_revision == sun_revision
```

**Fix:** Find the highest changed level `zc`. Rebuild only `zc` and the levels below it; `zc`'s own transparency and floor still seed the fully-outside/fully-inside state below it. Bump `sun_revision` only where `sun_lm` actually changed. Natural-light changes still rebuild everything.

### SPAT-P08 · `bresenham` takes `std::function`, heap-allocating on every uncached `map::sees` and `clear_path`
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/line.h:151`, `src/line.cpp:26`, `src/line.cpp:73`, `src/map.cpp:8446`, `src/map.cpp:8465`, `src/map.cpp:8814`, `src/coordinates.h:1026`, `src/pathfinding.cpp:265`

The lambdas from `sees` (40 B of captures) and `clear_path` (32 B) exceed the `std::function` small-buffer size in libstdc++ and libc++, so each call allocates and every tile goes through an indirect call. The typed `line_to` wrappers also copy into a second vector through `back_inserter` without `reserve`. The gain is modest: the allocations happen only on skew-cache misses, which already allocate twice.

```cpp
void bresenham( const point_bub_ms &p1, const point_bub_ms &p2, int t,
                const std::function<bool( const point_bub_ms & )> &interact );
bresenham( F.xy(), T.xy(), bresenham_slope, [this, f_transparent, &visible, &T]( const point_bub_ms & new_point ) {
```

**Fix:** Make `bresenham` a template on the callback (move the body to the header or an `.inl` file). Reserve in the `line_to` wrappers, or build their result directly on `bresenham`.

### SPAT-P09 · `scent_map::update` scans every vehicle part on all z-levels each turn (and ignores z)
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/map.cpp:11835`, `src/map.cpp:597`, `src/map.cpp:1636`, `src/scent_map.cpp:203`, `src/do_turn.cpp:729`

`scent_blockers` calls the all-level `get_vehicles()` (2,541 submaps) every turn and checks every part's xy only. As a result, obstacle parts on other z-levels also mark tiles `REDUCE_SCENT`. The cost is tens of µs per turn.

```cpp
VehicleList vehs = get_vehicles();
for( wrapped_vehicle &wrapped_veh : vehs ) {
    for( const vpart_reference &vp : veh.get_all_parts_with_fakes() ) { ...
        if( local_bounds.contains( part_pos.xy() ) ) { reduces_scent[part_pos.x()][part_pos.y()] = true;
```

**Fix:** Iterate `get_cache_ref( abs_sub.z() ).vehicle_list`, skip vehicles whose bounding box misses the window, and require `part_pos.z() == abs_sub.z()`.

### SPAT-P10 · Each FOV cast clears all 21 seen caches; later scans walk whole levels
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/lightmap.cpp:1413`, `src/lightmap.cpp:1421`, `src/lightmap.cpp:1437`, `src/lightmap.cpp:1449`, `src/map.cpp:11148`

`build_seen_cache` fills about 1.4 MB (21 levels) before every cast. `merge_camera` max-merges all 21 levels for each camera cast, and `level_reached_by_cast` scans whole levels below the avatar. The cast already knows which levels it wrote. This is real but minor: the fill is cheap next to the cast, and the scans stop early on hits.

```cpp
for( int z = -OVERMAP_DEPTH; z <= OVERMAP_HEIGHT; z++ ) { ...
    std::uninitialized_fill_n( &( *seen_caches[z + OVERMAP_DEPTH] )[0][0], map_dimensions, light_transparency_solid );
```

**Fix:** Have `cast_zlight` record the z range (and optionally the xy box) it wrote. Clear only levels written by the previous or the current cast, merge cameras only over those levels, and set `level_reached` from the recorded range.

### SPAT-P11 · Multi-level A* rescans a 24x24 overmap tile and builds 625 points for each expanded stair
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/pathfinding.cpp:169`, `src/pathfinding.cpp:183`, `src/pathfinding.cpp:140`, `src/pathfinding.cpp:453`, `src/game.cpp:10007`, `src/coordinates.h:1135`

When the stairs are not directly aligned, `find_stairs` scans 576 tiles and re-queries `u.pos_bub` on each. `vertical_move_destination` then builds a 625-point `closest_points_first` vector (allocated twice), even though the first point already matches. This only applies to routes that span z-levels, and the cost is a few to tens of µs per route. The avatar-relative stair choice is a correctness issue, covered in SPAT-B01.

```cpp
std::optional<tripoint_bub_ms> opt_dest = g->find_or_make_stairs( get_map(), cur.z() - 1, rope_ladder, false, cur );
for( const point_bub_ms &p : closest_points_first( t.xy(), SEEX ) ) {
```

**Fix:** Memoize the stair destination per (tile, dz) for each route call, or per level in `pathfinding_cache` using the existing dirty mechanism. Use the lazy `find_point_closest_first` (`point.h:376`). Together with SPAT-B01, use a stair lookup made for pathfinding.

### SPAT-P12 · Sound bookkeeping copies the full sound event per sound per listener before any distance check
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/sounds.cpp:583`, `src/sounds.cpp:584`, `src/sounds.cpp:592`, `src/sounds.cpp:356`, `src/do_turn.cpp:625`, `src/monmove.cpp:1655`, `src/mtype.cpp:551`

Every moving monster adds a footstep `sound_event` (4 strings) each turn. `process_sound_markers` runs for each NPC within 60 tiles plus the avatar, and copies every event before computing distance; the copy was added on purpose to avoid a use-after-free. Most strings fit in the small-string buffer, so the bigger per-sound cost is `you->has_flag(HEARING_PROTECTION)` (:592), which walks traits, bionics, effects and more, plus an `rng` call. Nearly all footsteps are inaudible beyond about 6 tiles.

```cpp
const tripoint_bub_ms pos = tripoint_bub_ms( sounds_since_last_turn[i].first );
const sound_event sound = sounds_since_last_turn[i].second;
```

**Fix:** Read the position and volume by index, compute felt and heard volume, and `continue` before copying anything or calling `has_flag`. Hoist `HEARING_PROTECTION` out of the loop. Store the season as `season_type` instead of a string.

### SPAT-P13 · Per-turn sound effects walk every submap for vehicles and recheck visibility of all creatures
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/sounds.cpp:1063`, `src/sounds.cpp:1068`, `src/sounds.cpp:1625`, `src/do_turn.cpp:798`, `src/map.cpp:597`, `src/game.cpp:3864`

In SDL_SOUND builds, `do_vehicle_exterior_engine_sfx` calls the all-level `get_vehicles()` every turn, and `do_danger_music` calls `get_visible_creatures(40)`, repeating the `sees` pass that `mon_info_update` already did that turn. Neither function checks `sounds::sound_enabled`/`sound_init_success`, so this work runs even with sound turned off.

```cpp
VehicleList vehs = here.get_vehicles();
for( wrapped_vehicle vehicle : vehs ) { ... sound_distance( ... ) ... sound_distance( ... ) ...
for( Creature *&critter : player_character.get_visible_creatures( 40 ) ) {
```

**Fix:** Return early when sound is disabled. Iterate the level `vehicle_list`, skip `vehicle_noise == 0`, and compute the distance once. Reuse `mon_info_update`'s visible list for danger music.

### SPAT-P14 · `greedy_path` decrease-key drains the entire priority queue
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/simple_pathfinding.cpp:113`, `src/simple_pathfinding.cpp:116`, `src/overmap.cpp:2442`, `src/overmap.cpp:2460`

Each decrease-key moves heap entries one by one into a second heap, which costs O(n log n). In practice only ravines trigger it: their `rng(1,2)` scorer makes the search a random flood fill. Ravines are off by default and on only in `highland_dimension`, `portal_storm_dungeon`, `aftershock_exoplanet` and MindOverMatter's `shoggothed_world`, where they slow overmap generation.

```cpp
if( open[n] != 0 ) {
    while( nodes[i].top().pos != p ) { nodes[1 - i].push( nodes[i].top() ); nodes[i].pop(); }
    nodes[i].pop();
    ...
    while( !nodes[i].empty() ) { nodes[1 - i].push( nodes[i].top() ); nodes[i].pop(); }
```

**Fix:** Use a single heap with lazy deletion: push the improved entry, and on pop skip entries whose priority no longer matches `open[map_index(pos)]` or whose node is already closed.

## Appendix: considered and rejected

- One emissive item or blinking vehicle lamp forces a full relight every turn – `generate_lightmap` runs only from a draw-time `build_map_cache`, because the per-turn build passes `skip_lightmap=true` (`do_turn.cpp:746`), and redraws during waiting, reading, crafting and sleeping are sparse. The dirty flag therefore collapses to one relight per human-paced interactive turn.
- `scent_map::get` runs `map::valid_move` for z±1 tiles before the cheap zero-scent test – `scent_move` returns early when the monster's own tile has no scent, and the 27-tile loop runs only on scented tiles whose neighbours are almost always >0 after diffusion, so reordering saves almost nothing. Side quirk noted by the reviewer: a monster one level off with zero scent gets `scent_here = -1` and slips past the `==0` check.
