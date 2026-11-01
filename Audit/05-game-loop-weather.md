# 05 · Game loop, turn processing, actions, time & weather

> Engine audit – static code reading at commit `a1a646d6`; nothing was compiled or run. Each finding was checked against the code by a separate reviewer: **confirmed** = verified by reading the code; **plausible** = consistent with the code, but reachability or impact not proven. Line numbers refer to that commit.

**Scope:** `src/game.cpp`, `src/do_turn.cpp`, `src/handle_action.cpp`, `src/action.cpp`, `src/calendar.cpp`, `src/weather.cpp`, `src/weather_gen.cpp`, `src/weather_type.cpp`, `src/game_io.cpp`, `src/gamemode_tutorial.cpp`, `src/game_constants.h`

## Summary

The turn loop and action dispatch hold up, but the weather subsystem has the most problems. Some weather values are lost in type conversions, such as the drizzle funnel flood and scarlet-storm modifiers that do nothing. The generator is random and has side effects, and catch-up and forecast code re-runs it thousands of times. Several world-side weather effects depend on where the player is standing. EOCs can read a lot of world state but cannot change much of it: weather, temperature, wind, snow depth, calendar state, action blocking and NPC spawning are all hardcoded or reachable only from debug code. The worst correctness bugs are small fixes: `fling_creature` for non-avatars, the funnel flood, the tutorial temperature leak and a crash in the save path.

| Lens | Critical | High | Medium | Low | Total |
|---|---|---|---|---|---|
| Missing JSON / EOC | 0 | 1 | 9 | 8 | 18 |
| Bugs & quirks | 0 | 4 | 11 | 6 | 21 |
| Performance | 0 | 1 | 3 | 9 | 13 |
| **Total** | 0 | 6 | 23 | 23 | 52 |

**Fix first:**
1. **LOOP-B01** · fling_creature never advances `pos` for NPCs and monsters. Vehicle collisions and monster throws commonly hit this, and the fix is a single line.
2. **LOOP-B02** · Funnels collect 1 charge every turn in light drizzle. This floods water economies through a simple int truncation.
3. **LOOP-B03** · Tutorial's forced 20 C temperature leaks into the next new game. It permanently breaks a world's climate for new players, and the fix is a reset in `setup()`.
4. **LOOP-B04** · save_player_data dereferences an empty optional. It crashes during autosave, and the guard is a one-line check.
5. **LOOP-P01** · sum_conditions catch-up evaluates weather once per in-game minute. It stalls when a base with funnels or solar panels is reloaded, and fixing it only needs a coarser tick and hoisted invariants.

## 1. Missing JSON bindings & EOC features

### LOOP-J01 · EOCs cannot set the weather or durably change temperature or wind; `weather('windpower')` assignment does nothing to gameplay
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/math_parser_diag.cpp:1841`, `src/math_parser_diag.cpp:1849`, `src/weather.cpp:1037`, `src/weather.cpp:1052`, `src/weather.cpp:1054`, `src/weather.cpp:1188`, `src/npctalk.cpp:6033`, `src/debug_menu.cpp:3627`

`weather_manager` has `weather_override`, `forced_temperature`, `windspeed_override` and `wind_direction_override`, but only the debug menu, the debug console and the tutorial set them. The only weather effect available to EOCs is `next_weather`, which just re-runs the generator. `weather_ass` writes `weather_precise->temperature/windpower/humidity/pressure`. `update_weather` overwrites these at the next weather change, which by default is 5 minutes away. Wind is worse: every gameplay consumer reads `weather.windspeed` (`character_body.cpp:440`, `vehicle.cpp:5660`, `map_field.cpp:282`, `item.cpp:4668`, `bionics.cpp:1104`) and none reads `weather_precise->windpower`. So `weather('windpower') = 60` has no gameplay effect, even though NPCs.md documents it as writable. Mods work around this at great cost. Xedra_Evolved clones every rain type as `magic_weather_*` types gated on global vars plus `next_weather`. hunvre defines 12 fake weather types just to force a fixed temperature. Those workarounds still cannot make a temperature or wind change last.

```cpp
// math_parser_diag.cpp:1848
} else if( aspect == "windpower" ) { get_weather().weather_precise->windpower = val;
// weather.cpp:1052-1054
temperature = w.temperature; ... windspeed = windspeed_override ? *windspeed_override : w.windpower;
```

**Fix:** Add effects `{ "set_weather": "rain", "duration": "2 h" }` and `{ "clear_weather_override": true }`, implemented as a saved `weather_override` plus an expiry `time_point` checked in `update_weather`. Make `weather('windpower')` assign `windspeed_override`. Add `weather('wind_direction')` with read and write. Let `weather('temperature') = x` set `forced_temperature`, with an optional `'duration'` kwarg or a separate `weather('temperature_offset')`. Add `weather('duration_left')` to read the time until the next change. Example uses: a spell that summons a 3-hour storm, or a "nuclear winter" event that lowers temperature by 15 C for a week.

### LOOP-J02 · OMT `exit_eoc` runs the destination tile's EOC; entry/exit EOCs are skipped on vehicle travel, teleport and game start
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/game.cpp:11560`, `src/game.cpp:11562`, `src/game.cpp:11539`, `src/game.cpp:1073`, `src/game.cpp:1075`, `src/game.cpp:10261`, `src/map.cpp:1946`, `src/teleport.cpp:401`, `src/overmap_terrain.cpp:590`

`cata_event_dispatch::avatar_moves` checks whether the tile being left (`past_ter`) has an `exit_eoc`, then activates `cur_ter->get_exit_EOC()`, which belongs to the tile being entered. If the destination has no `exit_eoc`, a null id reaches `generic_factory::obj` and raises an "invalid id" debugmsg. If it does have one, the destination's exit EOC fires when you walk in. No shipped data uses `exit_eoc`, which is why nobody has noticed. Separately, the dispatcher runs only from `walk_move` (`game.cpp:7918`), `vertical_move` (`9792`), `avatar_action.cpp:712` and `iexamine.cpp:1409`. Vehicle displacement (`map.cpp:1946`), teleport (`teleport.cpp:401-402`) and `start_game` (which sends `avatar_enters_omt` but never runs `entry_eoc`) all skip the documented hook (doc/JSON/OVERMAP.md:335-336). Content that uses `entry_eoc` today (conquerors_road, Aftershock spaceport/ravine, Magiclysm forge) is mostly entered on foot, which limits the impact.

```cpp
if( !past_ter->get_exit_EOC().is_null() ) {
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    effect_on_condition_id eoc = cur_ter->get_exit_EOC();   // should be past_ter
    eoc->activate_activation_only( d, "OMT movement" );
```

**Fix:** Use `past_ter->get_exit_EOC()` on line 11562. Move OMT-change detection (`avatar_enters_omt` plus entry and exit EOCs) into a helper that `game::update_map` and teleport call whenever the avatar's OMT changes, and call it once from `start_game`. Optionally add `"npc_entry_eoc"`, or run the EOC with the NPC as talker when an NPC crosses an OMT boundary.

### LOOP-J03 · `u_add_wet` (and anything else using `wet_character`) silently does nothing on turns not divisible by 6
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/weather.cpp:400`, `src/weather.cpp:389`, `src/weather.cpp:394`, `src/npctalk.cpp:5710`, `src/effect_on_condition.cpp:288`, `src/effect_on_condition.cpp:331`, `data/json/effects_on_condition/weather_eocs.json:36`, `data/mods/MindOverMatter/powers/vitakinesis_eoc.json:109`

`wet_character` returns early unless `calendar::once_every(6_seconds)`. That gate is a rate correction meant for the per-turn rain path in `handle_weather_effects`, which already checks it at line 478. `wet_character` is also the backend of the `u_add_wet`/`npc_add_wet` EOC effect, so a one-shot call on a non-aligned turn does nothing. For example, MoM's vitakinesis sweat fails 5 times out of 6. Core snow and snowstorm wetting are global EOCs with `recurrence: 6` that call `u_add_wet`. In core, game start and the default weather durations are 6-aligned, so those EOCs work at first. Once `next_weather` (portal storms, scenario EOCs) or dimension travel shifts the phase, `process_reactivate` re-queues them at a non-aligned turn, and from then on no snowfall wets the player.

```cpp
// Coarse correction to get us back to previously intended soaking rate.
if( !calendar::once_every( 6_seconds ) ) { return; }
```

**Fix:** Move the `once_every(6_seconds)` gate out of `wet_character` and into `handle_weather_effects` only. Optionally add a kwarg for deterministic wetting, e.g. `{ "u_add_wet": 10, "ignore_clothing": true }`, for splash traps or water spells.

### LOOP-J04 · The fork's snow-depth system is fully hardcoded, unreachable from EOCs and undocumented
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/weather.cpp:1077`, `src/weather.cpp:1081`, `src/weather.cpp:1083`, `src/weather.cpp:1121`, `src/character.cpp:6326`, `src/monmove.cpp:1943`, `src/math_parser_diag.cpp:1819`

Accumulation and melt use file-static constants: a 12:1 snow-to-water ratio, 2.0 mm/°C/h melt, an 800 mm cap and 1 h catch-up. Snowfall is inferred as `precip != none && !rains`, at the `precip_mm_per_hour` rate. Movement penalties use literal thresholds: 100/250/500 mm gives +20/+50/+100 move cost for characters (`character.cpp:6326-6331`) and 1/2/4 for monsters (`monmove.cpp:1943-1947`). No weather_type or region field controls any of this. `weather_eval` and `weather_ass` only cover temperature, windpower, humidity, pressure and precipitation, so EOCs cannot read snow depth (e.g. for snowshoes or sleds) or set it (e.g. a blizzard spell or snow clearing). The only doc mention is the SNOWWALKING flag in JSON_FLAGS.md.

```cpp
static constexpr double snow_water_ratio = 12.0; ... melt_rate_per_deg_per_hour = 2.0; ... max_snow_depth = 800.0;
const int penalty = snow_mm >= 500 ? 100 : ( snow_mm >= 250 ? 50 : 20 );   // character.cpp:6329
```

**Fix:** Add a weather_type `"snow_rate"` (mm/h, default derived from precip) and a weather_generator `"snow": { "melt_rate": 2.0, "max_depth": 800, "water_ratio": 12 }`. Add `weather('snow_depth')` with read and write, defaulting to the player's OMT, with an optional `'loc'` kwarg. Move the move-cost thresholds into a JSON table or a `SNOW_MOVE_COST` enchantment. Document the system in WEATHER_TYPE.md.

### LOOP-J05 · Precipitation, wetting and glare behaviour is hardcoded per `precip_class` and applies only to the player
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/weather.cpp:455`, `src/weather.cpp:463`, `src/weather.cpp:448`, `src/weather.cpp:164`, `src/weather.cpp:107`, `src/weather.cpp:122`, `src/weather.cpp:484`, `src/do_turn.cpp:766`

A weather_type can only pick precip `none/very_light/light/heavy` plus `rains`. Everything else is a C++ switch on that enum: rainfall in mm/h (0.5/1.5/3), player wetness (5/30/60), scent and field decay time (5/15/45 turns), funnel charges (1/4/8 per turn) and the humidity override. Wetting applies only to `get_player_character()` ("Possible TODO, make npc/monsters affected"), and glare is player-only ("todo npcs"). Snow glare requires `season == WINTER`, not snow on the ground or snowing weather. Eternal-summer worlds, the fork's snow-depth system and exoplanet whiteouts therefore never cause snow glare. Glare durations (10/20 turns) are fixed. LOOP-B15 covers the related problem that all of this is gated on the player's z-level.

```cpp
if( w->precip == precip_class::very_light ) { wetness = 5; decay_time = 5_turns; }
else if( ...light ) { wetness = 30; decay_time = 15_turns; } else if( ...heavy ) { decay_time = 45_turns; wetness = 60; }
if( season == WINTER && incident_sun_irradiance( w, calendar::turn ) > irradiance::moderate ) {   // :122
```

**Fix:** Add optional weather_type fields that default from precip: `"precip_rate": 1.5` (mm/h, used by funnels, snow accumulation and `weather('precipitation')`), `"wetness": 30`, `"field_decay": "15 s"`, and `"glare": { "effect": "snow_glare", "min_irradiance": 200, "duration": "20 s" }`. Apply wetting and glare to every outdoor Character in the bubble, not just the avatar.

### LOOP-J06 · Climate shape, wind-direction tables and water temperature are hardcoded; regions cannot express non-New-England climates
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/weather_gen.cpp:31`, `src/weather_gen.cpp:196`, `src/weather_gen.cpp:272`, `src/weather_gen.cpp:301`, `src/weather_gen.cpp:389`, `src/weather.cpp:817`, `src/weather.cpp:1197`, `src/weather_gen.h:55`, `doc/JSON/REGION_SETTINGS.md:562`

`weather_generator` exposes only base values and per-season offsets. The shape of the climate is fixed in an anonymous namespace: seasonal swing ±12 K, daily swing 5 K (+2 K seasonal), coldest hour 05:00, ±6 K noise, and the humidity and pressure noise amplitudes. Wind-direction distributions per season are literal tables (`// TODO: Unhardcode?`). `get_water_temperature` uses a fixed New England formula (54.5 ± 20.7 F) that ignores the region, and swimming body temperature (`character_body.cpp:488`) and the thermometer (`iuse.cpp:7620`) use it. TropiCataclysm and Aftershock's frozen exoplanet therefore get New England water. Underground temperature follows `base_temperature` with no separate field. `get_local_windpower` halves wind only for the oter ids `forest` and `forest_water`. On the loading side, `base_temperature` and the `*_manual_mod` fields are plain numbers, not units. `weather_black_list`/`white_list` are unvalidated `std::string`, so typos are silently ignored. The `*_temp_manual_mod`/`*_humidity_manual_mod` fields used by 8 region files are missing from REGION_SETTINGS.md. Ten mods define their own weather_generator, so this matters.

```cpp
constexpr double coldest_hour = 5; constexpr double daily_magnitude_K = 5; ... constexpr double seasonality_magnitude_K = 12;
float annual_mean_water_temperature = 54.5 + 20.7 * std::sin( ... );     // weather_gen.cpp:321
if( ( omter->get_type_id() == oter_type_forest ) || ( omter->get_type_id() == oter_type_forest_water ) ) { windpower = windpower / 2; }
```

**Fix:** Add optional weather_generator fields that default to today's values: `"seasonal_amplitude": "12 K"`, `"daily_amplitude": "5 K"`, `"coldest_hour": 5`, `"temperature_noise": "6 K"`, `"underground_temperature": "6.5 C"`, `"water_temperature": { "mean": "12.5 C", "amplitude": "11.5 K" }`, and `"wind_direction_weights": { "spring": [ 16 ints ], ... }`. Accept unit strings for `base_temperature`. Store the black and white lists as `weather_type_id` and validate them in `check()`. Add an oter_type field `"wind_factor": 0.5` to replace the forest id check. Document the manual_mod fields.

### LOOP-J07 · Action blocking is hardcoded per trait/effect id, and actions have no EOC hooks
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/handle_action.cpp:2136`, `src/handle_action.cpp:2163`, `src/handle_action.cpp:2240`, `src/handle_action.cpp:2246`, `src/handle_action.cpp:2259`, `src/handle_action.cpp:1749`, `src/action.h:22`

`do_regular_action` has four hardcoded tables of forbidden actions, each with its own messages. They are keyed on active trait SHELL2/SHELL3, effect `incorporeal`, flag TEMPORARY_SHAPESHIFT_NO_HANDS and `is_mounted`. Firing is refused for traits BRAWLER and GUNSHY by id. Some generic json_flags exist (TEMPORARY_SHAPESHIFT_NO_HANDS, HANDS_CANNOT_USE_FIREARMS), and mods can apply the incorporeal effect. Still, a mod cannot define its own per-action block list with a custom message, e.g. a "vow of non-violence" trait that blocks only firing and throwing. `action_id` is a closed C++ enum. The only events `handle_action.cpp` sends are `character_smashes_tile` and `opens_spellbook`, so JSON cannot observe or veto an action, or bind a hotkey to an EOC.

```cpp
const bool in_shell = player_character.has_active_mutation( trait_SHELL2 ) || player_character.has_active_mutation( trait_SHELL3 );
if( u.has_effect( effect_incorporeal ) && actions_disabled_in_incorporeal.count( act ) > 0 ) {
if( you.has_trait( trait_BRAWLER ) ) { add_msg( m_bad, _( "You refuse to use ranged weapons." ) );
```

**Fix:** Add `"blocked_actions": [ { "actions": [ "craft", "pickup", "fire" ], "message": "..." } ]` to mutation, effect_type and bionic (or json_flag). Collect them into a per-character cache that `do_regular_action` checks. Convert SHELL2/3, incorporeal, NO_HANDS, BRAWLER and GUNSHY to that data. Add `event_type::avatar_performs_action { action: string }` and an optional pre-action condition EOC that can cancel the action.

### LOOP-J08 · Climbing down a "sheer drop" ignores the climbing_aid JSON system; slip chance special-cases BADKNEES and parkour
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/game.cpp:10073`, `src/game.cpp:10132`, `src/game.cpp:10143`, `src/game.cpp:10165`, `src/game.cpp:10174`, `src/game.cpp:10841`, `src/game.cpp:10853`

When the stairs have no counterpart on the other level, `find_or_make_stairs` falls back to its own C++ ladder of options: flag WEB_RAPPEL, traits VINES2/VINES3 (with literal costs: pain 5, 5 torso damage, -87 kcal, +10 thirst), item `grapnel`, item `rope_30`, and terrain `t_lava`. `data/json/climbing.json` already defines `item_grapnel`, `ability_WEB_RAPPEL` and `ability_VINES2/3` for the climb-down menu, so these mechanics exist twice. A mod's climbing_aid gear is never offered here, and `rope_30` is not a climbing aid at all. `slip_down_chance` hardcodes BADKNEES (×2) and prof_parkour (÷2), and there is no enchantment value for climbing or slip chance. The CLIMB_FLYING flag and the `slip_prevent_mod` character_modifier are the only levers. The branch is situational, which limits impact.

```cpp
} else if( u.has_trait( trait_VINES2 ) || u.has_trait( trait_VINES3 ) ) {   // :10143
} else if( u.has_amount( itype_grapnel, 1 ) ) {                             // :10165
} else if( u.has_amount( itype_rope_30, 1 ) ) {                             // :10174
```

**Fix:** Route the sheer-drop case through `climbing_aid::get_climbing_aids_by_*` with a new `"deploy_on_drop"`/`"rappel"` category. Move the VINES costs into `ability_VINES2_deploy`, as EOCs. Add an enchant value `CLIMB_SLIP_CHANCE` (add/mult) and express BADKNEES and parkour through it.

### LOOP-J09 · Random NPC spawning and the starting NPC are hardcoded; `npc_class` "common" weights are dead data
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/game.cpp:10457`, `src/game.cpp:10471`, `src/game.cpp:10473`, `src/game.cpp:10484`, `src/game.cpp:10511`, `src/game.cpp:1335`, `src/do_turn.cpp:430`, `src/game_constants.h:117`, `src/regional_settings.cpp:638`, `src/npc_class.cpp:355`

`perhaps_add_random_npc` reads only the region's `npc_spawntime`. Everything else is a literal: hourly check, 90-OMT radius, 0.8 decay per existing NPC, z=0 only, 10 placement retries, a 1% chance of fake flu or cold, and plain `randomize()` with no class or template. Each NPC also gets its own `solo_` faction. `npc_class::random_common()` has no callers, so the documented `common`/`common_spawn_weight` class fields never affect random spawns. `create_starting_npcs` hardcodes a 50% NC_DOCTOR with `TALK_SHELTER` and `NPC_MISSION_SHELTER` for every start location, and only LONE_START suppresses it. `u_spawn_npc` cannot work around this, because it spawns only inside the reality bubble. Overmap NPC travel uses a literal 600-OMT search radius.

```cpp
static constexpr double days_to_rate_factor = 100.0 / 24; ... static constexpr int radius_spawn_range = 90;
spawn_rate *= std::pow( 0.8f, npc_num );
tmp->randomize( one_in( 2 ) ? NC_DOCTOR : npc_class_id::NULL_ID() );   // :1335
```

**Fix:** Add a region_settings object `"random_npcs": { "spawn_time": 4.0, "radius": 90, "crowding_factor": 0.8, "classes": [ [ "NC_NONE", 80 ], [ "NC_BANDIT", 20 ] ], "templates": [ ... ], "spawn_eocs": [ ... ] }`, and use `random_common()` when no classes are given. Add a scenario/start_location field `"starting_npcs": [ { "class": "NC_DOCTOR", "chance": 50, "chat": "TALK_SHELTER" } ]`. Add `npc_spawned` to the event bus.

### LOOP-J10 · Skill cap (`MAX_SKILL = 10`) and other gameplay constants are compile-time globals
**Severity:** Medium · **Effort:** Large · **Verified:** confirmed
**Where:** `src/game_constants.h:94`, `src/character.cpp:2549`, `src/newcharacter.cpp:286`, `src/skill.h:163`, `src/skill.cpp:160`, `src/game_constants.h:111`, `src/item_degrade.cpp:1317`, `src/item_degrade.cpp:1320`, `src/item_degrade.cpp:1323`, `src/enums.h:124`

`MAX_SKILL` caps practice, and the comment at `character.cpp:2549` anticipates a per-skill cap. `SkillLevel::level()` clamps to it, and about 44 call sites use it, including a fixed-size cost array in `newcharacter.cpp:286`. Skill JSON has no `max_level`, so a mod cannot extend a skill to 15 or cap a minor one at 5. `BIO_CQB_LEVEL` is a global tied to the `bio_cqb` id, not a bionic field or enchantment. Item temperature control is a FRIDGE/FREEZER/HEATER enum clamped to fixed `temperatures::fridge`/`freezer`/`normal`, so vehicle parts and furniture cannot define a deep-freeze or warming set point.

```cpp
constexpr int MAX_SKILL = 10;                                        // game_constants.h:94
static std::array < int, 1 + MAX_SKILL > costs = { 0, 1, 1, 2, 4, 6, 9, 12, 16, 20, 25 };
constexpr int BIO_CQB_LEVEL = 5;                                     // game_constants.h:111
```

**Fix:** Add skill `"max_level"` (default 10) and replace `MAX_SKILL` at practice and clamp sites with `skill->max_level()`. Keep `MAX_SKILL` only as a scale constant in formulas. Replace `BIO_CQB_LEVEL` with an enchantment such as `MIN_COMBAT_SKILL`. Let FRIDGE/FREEZER sources specify `"temperature_setpoint": "-20 C"` on vpart and furniture.

### LOOP-J11 · Calendar state is barely scriptable; the documented `'unit':'years'` example throws
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/calendar.cpp:792`, `src/calendar.cpp:809`, `src/math_parser_diag.cpp:1310`, `src/math_parser_diag.cpp:1319`, `src/condition.cpp:1198`, `src/calendar.cpp:32`, `src/game.cpp:720`, `doc/JSON/NPCs.md:1470`

`time_duration::units` stops at days (the TODOs mention seasons and years), so `_time_in_unit` throws "Unknown time unit" for `years`. The documented example `time_since('cataclysm', 'unit':'years') > 1` (NPCs.md:1470) fails at runtime. Seasons can be computed with `game_option('SEASON_LENGTH')`, `ETERNAL_SEASON` is readable, and latitude is a per-world external option, so much of the remaining gap is convenience. What is truly missing: direct accessors (season, day of season, day of year, year), runtime toggling of eternal season or eternal time of day, and per-region latitude. Dimensions and regions all share one sun.

```cpp
{ "d", 1_days },
// TODO: maybe add seasons?
// TODO: maybe add years? Those two things depend on season length!
```

**Fix:** Accept `season(s)`, `year(s)` and `week(s)` in `_time_in_unit`, computed from `calendar::season_length()`. Add `calendar('season')`, `calendar('day_of_season')`, `calendar('day_of_year')`, `calendar('year')`, `calendar('season_length')` and `calendar('eternal_season')`, with assignment where safe. Add optional `"latitude"`/`"longitude"` to region or dimension data and call `calendar::set_location` on region change.

### LOOP-J12 · weather_type `temperature_modifier` applies underground despite the docs; area temperature ignores it
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/weather.cpp:1198`, `src/weather.cpp:1206`, `src/weather.cpp:1215`, `src/map.cpp:9546`

WEATHER_TYPE.md:17 says `temperature_modifier` is an "additive modifier to ambient temperature above ground". `get_temperature` uses `base_temperature` for z<0, but still adds the weather modifier to every tile. `get_area_temperature` ignores the modifier at every z. Scarlet storms and hunvre's forced temperatures therefore reach basements and labs. Butchery item temperature, the only caller of `get_area_temperature`, sees no change. See LOOP-B09 for why core scarlet-storm modifiers barely apply anyway.

```cpp
units::temperature temp = location.z() < 0 ? units::from_celsius( get_cur_weather_gen().base_temperature ) : temperature;
... temp_mod += weather_id->temperature_modifier;      // applied for every z
```

**Fix:** Apply `temperature_modifier` only when `z >= 0` in both functions, or add an explicit `"temperature_modifier_underground"` field (default 0) used by both. Update the doc to match.

### LOOP-J13 · Monster-spotted reactions are special-cased and gated on a UI setting; no "hostile spotted" event
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/game.cpp:4005`, `src/game.cpp:4011`, `src/do_turn.cpp:393`, `src/do_turn.cpp:396`, `src/uistate.h:222`

The Mycus defender reaction (trait M_DEFENDER + species PLANT gives `adrenaline_mycus` and +3 pain) sits inside `mon_info_update`'s safemode distraction branch. It runs only when `uistate.distraction_hostile_spotted` is on and exactly one new monster appeared, so a UI preference changes gameplay. `event.h` has no event for spotting a monster, so mods cannot add similar reactions. The MOTION_ALARM enchantment charges power only through `bio_alarm->power_trigger`, so other alarm sources are free and cannot set a cost.

```cpp
if( uistate.distraction_hostile_spotted && newseen > mostseen ) {
    ... if( u.has_trait( trait_M_DEFENDER ) && critter.type->in_species( species_PLANT ) ) {
```

**Fix:** Send `event_type::avatar_spots_monster { mtype, species, attitude }` from `mon_info_update`, independently of uistate, and move M_DEFENDER into a JSON EVENT EOC. Add a mutation or bionic `"on_motion_alarm_eocs"`, or make the alarm's power cost an enchantment value.

### LOOP-J14 · weather_type loading/docs drift: unvalidated debug EOCs, undocumented fields, unstable priority sort (mist unreachable)
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/weather_type.cpp:87`, `src/weather_type.cpp:105`, `src/weather_type.cpp:142`, `src/weather_type.h:127`, `src/weather_type.h:129`, `src/weather_gen.cpp:383`, `src/debug_menu.cpp:3621`

`debug_cause_eoc`/`debug_leave_eoc` are stored as `std::optional<std::string>` and converted only when the debug menu runs them. `check()` does not validate them, so a typo surfaces only when someone clicks the debug menu. WEATHER_TYPE.md omits `sun_sym`, lists only 7 of the 12 `sound_category` values (missing rainstorm, portal_storm, clear, sunny, cloudy), and says required weathers need lower load order. In fact `sort_weather` orders by `priority` with an unstable `std::sort`. This has real impact: mist and fog share priority 15. A libstdc++ simulation of the core load order sorts mist before fog, and fog's condition is a superset of mist's, so mist appears to be unreachable. The `passive_effects` doc comment is copy-pasted from dimensions.

```cpp
std::sort( sorted_weather.begin(), sorted_weather.end(), []( ... ) { return a->priority < b->priority; } );
```

**Fix:** Store the debug EOCs as `std::optional<effect_on_condition_id>` and validate them in `check()`. Use `std::stable_sort` or tie-break on load order, then check the core mist/fog priorities. Update WEATHER_TYPE.md: add `sun_sym`, complete the sound_category list and describe priority ordering.

### LOOP-J15 · Engine behaviour tied to specific content ids (towel, swim_fins, manhack, glass)
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/game.cpp:9065`, `src/game.cpp:9109`, `src/iuse.cpp:4779`, `src/iuse.cpp:4791`, `src/iuse.cpp:4794`, `src/game.cpp:9586`, `src/action.cpp:652`, `src/avatar_action.cpp:681`, `src/game.cpp:7432`, `src/handle_action.cpp:1048`, `src/melee.cpp:2326`, `src/ranged.cpp:1596`, `src/map.cpp:4922`, `src/gates.cpp:407`

Swimming turns an item wet only if its id is `towel`, which becomes `towel_wet`, and iuse TOWEL checks the same id. Surfacing and sinking check shoe id `swim_fins`, although `swim_speed` already uses the generic FIN armor flag. Only `mon_manhack` gets the reprogram menu in `disable_robot`, even though the activity actor's reprogram branch is generic. Only `material_glass` shatters on smash, and that check is copied into melee, ranged, map and gates code. The core `crystal` material never shatters. Modded towels, fins, drones and brittle materials cannot take part in these mechanics.

```cpp
} else if( loc->typeId() == itype_towel ) {                                       // game.cpp:9065
if( u.swim_speed() < 500 || u.shoe_type_count( itype_swim_fins ) ) {             // game.cpp:9586
if( mid == mon_manhack ) {                                                        // game.cpp:7432
const int glass_portion = weapon->made_of( material_glass );                     // handle_action.cpp:1048
```

**Fix:** Add an itype `"wet_variant": "towel_wet"` (or a WATER_CONVERT flag plus revert_to). Reuse the existing FIN armor flag for surfacing and sinking. Add a monster flag `REPROGRAMMABLE` and a material property `"shatters_on_smash": true`.

### LOOP-J16 · No weather lifecycle hooks: no `weather_changed` event, no on_start/on_end/while_active EOCs
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/weather.cpp:1049`, `src/weather.cpp:1066`, `src/weather_type.cpp:142`, `src/weather_type.h:127`, `src/event.h:25`, `src/debug_menu.cpp:3619`, `data/json/effects_on_condition/weather_eocs.json:2`

On a weather change, `update_weather` sets a bool and calls `process_reactivate`. The event bus has no weather or season event, so EVENT EOCs cannot react to "the weather became X" or "the storm ended". weather_type's `debug_cause_eoc`/`debug_leave_eoc` run only from the debug menu. Content therefore polls instead: core thunder and lightning are global EOCs with recurrence 1, and mods use `is_weather` 292 times. The performance cost is minor, since recurring EOCs with a `deactivate_condition` sit idle while the weather does not match. The gap is mainly expressiveness.

```cpp
if( weather_changed ) { effect_on_conditions::process_reactivate(); }   // weather.cpp:1066
```

**Fix:** Add `event_type::weather_changed { old_weather, new_weather }` and send it next to `process_reactivate`. Add weather_type fields `"on_start_eocs"`, `"on_end_eocs"` and `"active_eocs": { "eocs": [ ... ], "interval": "1 s" }`, run from `handle_weather_effects`. Make `debug_cause_eoc`/`debug_leave_eoc` aliases of the start and end lists.

### LOOP-J17 · Terrain step hazards (ROUGH/SHARP) use fixed C++ numbers, skip NPCs, and furniture has no on-step hook
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/game.cpp:8161`, `src/game.cpp:8176`, `src/game.cpp:8180`, `src/game.cpp:8204`, `src/monmove.cpp:2368`, `src/event.h:240`, `src/trap.cpp:165`

`game::place_player` hardcodes ROUGH damage (one_in(5), foot armor < rng(2,5), 1 cut) and SHARP damage (rng(1,10), a dex roll, parkour 1-in-4, THICKSKIN 1-in-8, tetanus 1/256, or 1/1024 with INFRESIST). The TODO at line 8161 admits NPCs cannot use it. Monsters have their own hardcoded copy in `monmove.cpp:2368-2381`. Terrain can already run EOCs on step through its built-in `trap` field and trap `eocs`, but furniture has no such hook. `avatar_moves` carries no position or furniture. The right-foot copy-paste bug in this code is filed as LOOP-B19.

```cpp
// TODO: Move the stuff below to a Character method so that NPCs can reuse it
( u.has_trait( trait_THICKSKIN ) ? !one_in( 8 ) : true ) ) { const int sharp_damage = rng( 1, 10 );
const int chance_in = u.has_trait( trait_INFRESIST ) ? 1024 : 256;
```

**Fix:** Add a ter/furn `"step_hazard": { "damage": { "damage_type": "cut", "amount": [ 1, 10 ] }, "chance": 0.66, "avoid_skill": "dex", "effect": { "id": "tetanus", "chance": 0.004 } }` and `"on_step_eocs": [ ... ]`, with the stepping Creature as `u`. Move the logic into Creature so monsters and NPCs share it. Express THICKSKIN and INFRESIST as flags or enchantments. Add position and furniture to `avatar_moves`.

### LOOP-J18 · Scenario start behaviours are hardcoded flags; HELI_CRASH finds its vehicle by substring "helicopter"
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/game.cpp:912`, `src/game.cpp:923`, `src/game.cpp:949`, `src/game.cpp:952`, `src/game.cpp:956`, `src/game.cpp:977`, `src/game.cpp:983`, `src/start_location.cpp:598`

LONE_START, FIRE_START (a fixed 3x3 burn), HELI_CRASH, BORDERED (a fixed ±50-OMT wall of overmap_special `world`) and the BLACK_ROAD option (density 70.0f) are all C++ special cases. HELI_CRASH places the player by scanning bubble vehicles for an id containing `"helicopter"` and renames the vehicle to the untranslated "Bird Wreckage". A modded `gyrocopter_crash` fails silently. Innawood and Aftershock use HELI_CRASH only for its wounds, which shows the flag mixes two behaviours. Scenario `eoc` can partly replace fire and placement.

```cpp
std::string name = v.v->type.str(); std::string search = std::string( "helicopter" ); if( name.find( search ) != std::string::npos ) {
v.v->name = "Bird Wreckage";
surround_groups.emplace_back( GROUP_BLACK_ROAD, 70.0f );
```

**Fix:** Add scenario fields `"start_vehicle_crash": { "vehicle": "helicopter_crashed", "name": { translation }, "place_player_at": "CONTROLS" }`, `"start_fire": { "radius": 3 }` and `"border": { "size": 100, "special": "world" }`. Keep the flags as shorthands, and move the BLACK_ROAD group and density into data.

## 2. Bugs, oversights & quirks

### LOOP-B01 · fling_creature never advances `pos` for NPCs and monsters: they move 1 tile, hit themselves and land at the start tile
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/game.cpp:9126`, `src/game.cpp:9194`, `src/game.cpp:9198`, `src/game.cpp:9208`, `src/game.cpp:9248`, `src/game.cpp:9250`, `src/game.cpp:9256`, `src/game.cpp:9273`, `src/tileray.cpp:75`, `src/tileray.cpp:117`

`pos` is read once. Each step computes `pt = pos + tdir.dx()`, but `tileray::dx()` is only the last single-step delta, and `pos` is refreshed only on the avatar branch. For NPCs and monsters every step lands next to the origin. On the next step with the same delta, which is every step for cardinal and 45° angles, `creature_at<monster>(pt)` finds the flung monster itself. The monster is then treated as an obstacle and takes `impact()` plus `apply_damage` from itself, and `thru=false` ends the flight. The landing damage, floor bash and DEEP_WATER check all use the stale `pos`. Callers include vehicle collisions (`vehicle_move.cpp:1190`), monster throw attacks (`mattack_actors.cpp:1098`, `monattack.cpp:1182/3936`) and teleport collisions. For example, a car hits a zombie, or a hulk throws your companion: the victim moves 1 tile and monsters take large self-collision damage.

```cpp
tripoint_bub_ms pos = c->pos_bub( here );                         // 9126, once
pt.x() = pos.x() + tdir.dx();                                     // 9194
if( monster *const mon_ptr = creatures.creature_at<monster>( pt ) ) // 9198 -> self
pos = c->pos_bub( here );                                         // 9248, avatar only
c->setpos( here, pt );                                            // 9256, pos not updated
```

**Fix:** After every successful `setpos`, set `pos = c->pos_bub( here )` for all creatures. Skip `creature_at` hits where the creature is `c` itself. Move the `c == nullptr` check above the first dereference at line 9126.

### LOOP-B02 · Funnels collect 1 water charge every turn in light drizzle (0.5 mm/h truncates to 0, and `one_in(0)` is true)
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/weather.cpp:461`, `src/weather.cpp:368`, `src/weather.cpp:334`, `src/weather.cpp:342`, `src/weather.cpp:317`, `src/weather.cpp:449`, `src/weather.cpp:246`, `src/weather.cpp:167`, `src/rng.cpp:116`

`precip_mm_per_hour()` returns a double (0.5/1.5/3), but `fill_water_collectors(int)` and `fill_funnels(int, ...)` take an int, so very_light becomes 0 and light becomes 1. `funnel_turns_per_charge(0)` returns 0.0, and `one_in(0)` is true. In `light_drizzle` every funnel in the bubble therefore gains 1 charge per turn, roughly 900 L/h, far more than heavy rain gives. Light rain is under-collected by a third. The off-screen catch-up (`retroactively_fill_from_funnel`) passes the summed 1/4/8-per-turn `rain_amount` into the mm/h parameter, which uses a third, inconsistent calibration (about 2-2.7× the intended amount).

```cpp
static void fill_water_collectors( int mmPerHour )          // 368
fill_water_collectors( precip_mm_per_hour( w->precip ) );   // 461, 0.5 -> 0
if( rain_depth_mm_per_hour == 0.0 ) { return 0.0; }         // 317
if( one_in( turns_per_charge ) ) {                          // 342
return chance <= 1 || rng( 0, chance - 1 ) == 0;            // rng.cpp:118
```

**Fix:** Make the parameters double. In `fill_funnels`, return early when `turns_per_charge <= 0` and use `x_in_y(1.0, turns_per_charge)`. Make `sum_conditions` accumulate mm of rain (`precip_mm_per_hour * hours`) so the live and retroactive paths share one formula.

### LOOP-B03 · Tutorial's forced 20 C temperature leaks into the next new game and is then saved permanently
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/gamemode_tutorial.cpp:140`, `src/gamemode_tutorial.cpp:363`, `src/game.cpp:686`, `src/game.cpp:753`, `src/game.cpp:815`, `src/do_turn.cpp:112`, `src/savegame.cpp:1715`, `src/savegame.cpp:1745`, `src/weather.cpp:1188`, `src/main.cpp:933`

`tutorial_game::init` sets `forced_temperature = 20 C`, and only `post_action(ACTION_SAVE)` clears it. `g` and its `weather_manager` live for the whole process. `game::setup()` resets only `weather_override`, and neither `start_game()` nor `cleanup_at_end()` resets anything. A new player who plays the tutorial, quits without saving and starts a real game in a fresh world gets a world where `get_temperature()` always returns 20 C. `serialize_all` writes `forced_temperature`, so the change becomes permanent in that world. The debug force-temperature and wind overrides leak the same way. Wind is session-only because it is not serialized.

```cpp
get_weather().forced_temperature = units::from_celsius( 20 );   // tutorial init
case ACTION_SAVE: get_weather().forced_temperature.reset();     // only reset
get_weather().weather_override = WEATHER_NULL;                  // game.cpp:753, only this is reset
```

**Fix:** In `game::setup()`, reset the whole `weather_manager`, or at least `forced_temperature`, `wind_direction_override` and `windspeed_override`. Clear the tutorial override in a gamemode teardown instead of on ACTION_SAVE.

### LOOP-B04 · save_player_data dereferences an empty `std::optional<zzip>` when the save file can't be mapped
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/game_io.cpp:613`, `src/game_io.cpp:614`, `src/game_io.cpp:616`, `src/zzip.cpp:367`, `src/mmap_file.cpp:322`

With compressed saves enabled, `zzip::load` returns `{}` after 3 failed mmap attempts. `zzip.cpp` names OneDrive and antivirus locks as causes. `save_player_data` then calls `z->add_file` and `z->compact_to` without a check. That is undefined behaviour and in practice a crash during autosave or quicksave. Every other caller checks `has_value()` (`game_io.cpp:357`, `mapbuffer.cpp:259/385`, `overmap.cpp:3710/3762`). The trigger is rare, but the failure loses the save.

```cpp
std::optional<zzip> z = zzip::load( save_path );
saved_data = z->add_file( ( playerfile + SAVE_EXTENSION )...
if( saved_data && z->compact_to( tmp_path, 2.0 ) ) {
```

**Fix:** Add `if( !z ) { debugmsg/popup; saved_data = false; }` before use and let `game::save()` report the failure. Optionally retry with back-off.

### LOOP-B05 · Deathcam re-sends `character_dies`/`avatar_dies`/kill events on every `is_game_over()` call
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/game.cpp:2863`, `src/game.cpp:2869`, `src/game.cpp:2872`, `src/game.cpp:2909`, `src/do_turn.cpp:535`, `src/do_turn.cpp:674`, `src/handle_action.cpp:330`, `src/handle_action.cpp:461`, `src/memorial_logger.cpp:496`

`is_game_over()` sends `character_kills_character`, `character_dies` and `avatar_dies` whenever `uquit` is QUIT_DIED or QUIT_WATCH, with no "already sent" guard. In deathcam, uquit stays QUIT_WATCH, turns advance on a 125 ms input timeout, and `do_turn` calls `is_game_over()` twice per turn. The events therefore repeat many times a second. The memorial log fills with "Died" lines, the killer is credited with repeated kills, and EVENT EOCs on `character_dies` re-fire every turn. DEATHCAM defaults to "ask", so only players who choose to watch are affected.

```cpp
if( uquit == QUIT_DIED || uquit == QUIT_WATCH ) {
    ... events().send<event_type::character_dies>( u.getID() ); events().send<event_type::avatar_dies>();
    if( uquit == QUIT_WATCH ) { ... return false; }
```

**Fix:** Send the death events once, at the transition into QUIT_WATCH or QUIT_DIED (around `game.cpp:2909-2919`), or guard them with a `death_events_sent` flag.

### LOOP-B06 · Water temperature and maple-sap season use a Jan-1 day where the code assumes spring = day 0 (about 81 days off)
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/weather_gen.cpp:311`, `src/weather_gen.cpp:320`, `src/calendar.h:582`, `src/calendar.cpp:856`, `src/map.cpp:9743`, `src/map.cpp:9748`, `src/map.cpp:9751`

`time_past_new_year()` adds `turn_zero_offset()` (81 days), so its day 0 is Jan 1. `get_water_temperature`'s sine assumes "Day = 0 corresponds to the start of spring". As a result water peaks at 75 F around mid-May and bottoms out around mid-November, instead of early August and early February. This drives swimming body temperature and the thermometer readout. The sap window at `map.cpp:9743-9753` makes the same mistake and runs Dec 7 to Feb 14 instead of late February to early May. `get_common_data` compensates correctly (`weather_gen.cpp:113-119`), which confirms the epoch.

```cpp
return ( p - calendar::turn_zero + calendar::turn_zero_offset() ) % calendar::year_length();   // calendar.h:584
int day = to_days<int>( time_past_new_year( t.t ) );                                            // weather_gen.cpp:311
// ... Day = 0 corresponds to the start of spring.
```

**Fix:** Add a `time_past_spring_start()` helper (`(p - turn_zero) % year_length()`) and use it in `get_water_temperature` and the sap code. Alternatively, subtract `turn_zero_offset` in those formulas.

### LOOP-B07 · "Set alarm for dawn" sets a 9-hour alarm (duplicate uilist retval 9)
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/handle_action.cpp:1175`, `src/handle_action.cpp:1177`, `src/handle_action.cpp:1212`, `src/handle_action.cpp:1169`, `src/handle_action.cpp:1238`

In alarm mode the loop adds retvals 1..9 for 1..9 hours, and the dawn entry then reuses retval 9. `durations` is a `std::map`, so the dawn entry's `emplace(9, ...)` does nothing, while the label still shows the real time to dawn. The player sees "Set alarm for dawn (6 hours and 10 minutes)" and gets a 9-hour alarm. Noon, dusk and midnight are unaffected.

```cpp
for( int i = 1; i <= 9; ++i ) { add_menu_item( i, '0' + i, "", i * 1_hours ); }
add_menu_item( 9,  'd', setting_alarm ? _( "Set alarm for dawn" ) : ...
durations.emplace( retval, duration );   // no-op for the second 9
```

**Fix:** Give the diurnal entries unique retvals (e.g. 20-23), or limit the alarm hour list to 1..8 as the non-alarm branch does.

### LOOP-B08 · Motion alarm fires once per monster per turn, including pets, your mount and motionless critters
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/do_turn.cpp:393`, `src/do_turn.cpp:397`, `src/do_turn.cpp:399`, `src/do_turn.cpp:407`, `src/game.cpp:1506`

The MOTION_ALARM check runs inside monmove's per-monster loop and filters only on dead, hallucination and distance. It ignores attitude, `effect_ridden` and whether the monster moved. Each match drains bio_alarm power and posts a message. It also calls `cancel_activity_or_ignore_query`, which asks "Cancel auto move?" each time during auto-travel, and calls `wake_up()`. With bio_alarm (described as "very useful during sleep") or the MoM, Aftershock or Xedra MOTION_ALARM sources, a pet dog within range wakes you every turn. A ridden mount, at distance 0, spams the alarm every turn.

```cpp
if( !critter.is_dead() && !critter.is_hallucination() && rl_dist( u.pos_abs(), critter.pos_abs() ) <
    u.enchantment_cache->modify_value( enchant_vals::mod::MOTION_ALARM, 0 ) ) {
    u.mod_power_level( -bio_alarm->power_trigger ); ... if( u.has_effect( effect_sleep ) ) { u.wake_up(); }
```

**Fix:** Skip friendly and pet monsters, the avatar's mount, and monsters that did not move this turn. Collect matches and fire at most once per turn after the loop, with one charge, one message and one wake-up.

### LOOP-B09 · Plain-number weather `temperature_modifier` is read as legacy body-temp units (×0.002 K), so core scarlet-storm modifiers do nothing
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/weather_type.cpp:111`, `src/units.cpp:99`, `src/units.cpp:102`, `src/units.h:552`, `src/weather.cpp:1206`, `data/json/weather_type.json:505`

`temperature_delta::deserialize` silently converts a bare int with `from_legacy_bodypart_temp_delta` (×0.002 K). Core data uses bare ints for `scarlet_dimension_storm` (-20) and `scarlet_storm_1..5` (-2 to -30) (`weather_type.json:505-726`). These load as -0.004 to -0.06 K, so the intended chill is effectively zero. String values such as `"-20 C"` (the doc example, hunvre) load correctly. See LOOP-J12 for the separate underground problem.

```cpp
optional( jo, was_loaded, "temperature_modifier", temperature_modifier, 0_K_delta );   // weather_type.cpp:111
if( jv.test_int() ) { *this = from_legacy_bodypart_temp_delta( jv.get_int() ); }     // units.cpp:101
"temperature_modifier": -30,                                                          // weather_type.json:726
```

**Fix:** Reject bare numbers for weather `temperature_modifier`, or read them as °C with a deprecation warning. Migrate the core entries to strings like `"-20 C"`.

### LOOP-B10 · `noon()` is wrong before 12:00, so `time_since('noon')` and `time_until('noon')` return wrong values
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/calendar.cpp:337`, `src/calendar.cpp:341`, `src/math_parser_diag.cpp:1377`, `src/math_parser_diag.cpp:1408`, `data/mods/Xedra_Evolved/monsters/cryptids.json:42`, `data/mods/Xedra_Evolved/monsters/cryptids.json:98`

_(Reported by both the JSON and bug auditors; merged.)_ In the morning `noon(p)` returns `p + till_noon`, so at 08:00 it gives 04:00 instead of 12:00. `time_since('noon')` therefore evaluates to |t − 12 h|. `time_until('noon')` also computes `turn - noon(turn)` instead of `noon - turn`, so at 15:00 it returns 3 h instead of 21 h. Both contradict NPCs.md:1470-1471. `EOC_DEBUG_TELL_ME_TIME` prints wrong values. Content is tuned around the bug: the Xedra "Man of the Nineteenth Hour" uses `time_since('noon')>=7` and `time_until('noon')<=7`, and it is active 19:00-05:00 only because of the bug. With correct semantics both conditions would be true from 05:00 to 12:00.

```cpp
const time_duration till_noon = time_of_day - 12_hours;
return ( till_noon > 0_seconds ) ? p - till_noon : p + till_noon;              // calendar.cpp:341
ret = to_turns<double>( calendar::turn - noon( calendar::turn ) );             // time_until, :1408
```

**Fix:** `return p - time_past_midnight( p ) + 12_hours;`. In `time_since` use `turn - noon` (+1 day if negative). In `time_until` use `noon - turn` (+1 day if negative). Re-tune the Xedra cryptid conditions in the same change.

### LOOP-B11 · knockback: drowning and aquatic-flop checks test the starting tile; the avatar water-landing branch is unreachable
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/game.cpp:4128`, `src/game.cpp:4135`, `src/game.cpp:4172`, `src/game.cpp:4173`, `src/game.cpp:4180`, `src/game.cpp:4199`, `src/game.cpp:4324`

`targ_pos` is captured before the trajectory loop, and the per-step liquid checks still use it after `setpos(traj[i])`. Shallow water has the LIQUID flag, so a drownable monster knocked back out of shallow water "drowns" on the first step even if it lands on dry ground. A monster knocked into deep water never drowns, and an AQUATIC monster knocked onto land never flops. In the avatar branch, `force_remaining == 0` is never true inside the loop, so `avatar_action::swim` is unreachable. The monster and NPC branches use 2-D `impassable(point)`, which checks the map's current z, while the avatar branch uses the 3-D overload.

```cpp
const tripoint_bub_ms targ_pos = targ->pos_bub( here );                                     // 4128
targ->setpos( here, traj[i] ); if( here.has_flag( ter_furn_flag::TFLAG_LIQUID, targ_pos ) && targ->can_drown() &&
if( here.has_flag( ter_furn_flag::TFLAG_LIQUID, pos ) && force_remaining == 0 ) {           // 4324
```

**Fix:** Use `traj[i]` in the liquid checks and break once the target dies. For the avatar, test `traj[i]` for liquid. Use 3-D `impassable()` consistently.

### LOOP-B12 · Rain field decay is a no-op: `decay_fields_and_scent` ages a copy of each field
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/map.cpp:4007`, `src/map.cpp:4012`, `src/weather.cpp:474`, `doc/JSON/JSON_INFO.md:4468`, `data/json/field_type.json:9`

_(Filed under performance by the auditor; the reviewer reclassified it as a bug.)_ During rain, `decay_fields_and_scent` walks every field on the z-level each turn, but `map.cpp:4007` takes `field_entry cur = fp.second;` by value and `set_field_age` ages the copy. Rain therefore never decays fields, even though `decay_amount_factor` is documented and set on dozens of core fields (blood, gibs and others). The original performance angle is small: `maybe_apply_field_effect` does a `veh_at` per creature even when `passive_effect` is empty, but `veh_at` returns early on bit caches.

```cpp
field_entry cur = fp.second;   // map.cpp:4007, should be a reference
```

**Fix:** Take a reference (`field_entry &cur`). This is a gameplay change, so check the decay rates afterwards. Also guard the creature loop in `handle_weather_effects` with `if( !w->passive_effect.empty() )`.

### LOOP-B13 · `weather_generator::get_weather` is random and has side effects: same time and place give different weather, and hypothetical queries change live wind
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/weather_gen.cpp:207`, `src/weather_gen.cpp:209`, `src/weather_gen.cpp:214`, `src/weather_gen.cpp:219`, `src/weather_gen.cpp:49`, `src/weather.cpp:192`, `src/weather.cpp:220`, `src/weather.cpp:583`, `src/weather.cpp:905`, `src/weather.cpp:1038`, `src/vehicle.cpp:5642`, `src/vehicle.cpp:6606`, `src/map.cpp:6336`

`get_weather()` is const, yet it computes wind with `rng(1,2)`, `rng(9, peaks)` and `rng_float`, and it mutates the static `current_winddir` through `one_in(W*2160)`, which is always true at W==0. Wind-dependent weather types (rainstorm and snowstorm need windpower ≥ 15, fog and mist need < 12) are re-rolled on every query. `current_weather(loc, t)` can therefore disagree with the global `weather_id` and with itself. Vehicle solar and wind power, portal-storm checks, funnel and vehicle catch-up, the radio forecast (72 calls) and planting (97 calls per growth stage) all query it. Each query consumes global RNG and can change the wind direction the next live `update_weather` adopts.

```cpp
W = std::max( 0, static_cast<int>( base_wind * rng( 1, 2 ) / std::pow( ( P + W ) / 1014.78, rng( 9, base_wind_distrib_peaks ) ) + variation ) );
bool changedir = one_in( W * 2160 ); if( changedir ) { current_winddir = get_wind_direction( season ); ...
```

**Fix:** Derive wind speed and direction from seeded noise of (location, time, seed), the way temperature, humidity and pressure already are, so `get_weather` becomes pure. Keep live wind-direction persistence only in `weather_manager::update_weather`.

### LOOP-B14 · Save failures leave the world half-saved; a failed dimension-travel save destroys items, vehicles and NPCs
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/game_io.cpp:746`, `src/game_io.cpp:747`, `src/game_io.cpp:749`, `src/game_io.cpp:644`, `src/game_io.cpp:586`, `src/game.cpp:9839`, `src/game.cpp:9852`, `src/game.cpp:9855`

`game::save()` chains its steps with `||` and writes player data first. If a non-essential step fails, such as achievements (written to a user-global directory) or the debug-only external options record, the master, dimension and map saves are skipped. The player file is already on disk at that point. On reload, picked-up items exist twice and dropped items are gone. `travel_to_dimension` removes the travelling items, detaches the vehicle into a local `unique_ptr` and erases the travelling NPCs from the old overmap before it checks `save_maps()`/`save_dimension_data()`. On failure it returns false and all of them are destroyed. This only happens when an I/O write fails.

```cpp
if( !save_player_data() || !save_achievements() || !save_factions_missions_npcs() ||
    !save_external_options_record() || !save_dimension_data() || !save_maps() || ...
il.remove_item(); ... vehicle_ref = here.detach_vehicle( veh ); ... if( !save_maps() || !save_dimension_data() ) { return false; }
```

**Fix:** Save world data (maps, overmap, master, dimension) before the player file. Treat achievements and the external-options record as non-fatal warnings. In `travel_to_dimension`, save before mutating state, or restore the items, vehicle and NPCs on failure.

### LOOP-B15 · Rain, funnels, field decay and weather passive effects depend on the avatar's z-level and underwater state
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/do_turn.cpp:766`, `src/weather.cpp:455`, `src/weather.cpp:457`, `src/weather.cpp:479`, `src/weather.cpp:484`, `src/weather.cpp:488`, `src/weather.cpp:82`, `src/condition.cpp:1683`, `src/map.cpp:9979`

`handle_weather_effects` runs only `if( levz >= 0 && !u.is_underwater() )`. While the player is in a basement or diving, surface funnels stop filling and get no catch-up, because their submaps stay loaded and retroactive filling only happens on actualize. Rain field decay also stops, and no surface creature receives weather `passive_effects`. When the player is on the surface, passive effects go to every creature in the bubble, including those at z<0, unless the effect sets `immune_inside`. All core weather passive effects set it, so only mods are affected. Rain wetting applies only to the avatar, so NPCs never get rained on. `is_creature_outside()`, which backs `u_/npc_is_outside` and sounds, tests the map's z instead of the creature's own z. LOOP-J05 covers the hardcoded per-precip values in the same function.

```cpp
if( levz >= 0 && !u.is_underwater() ) { handle_weather_effects( weather.weather_id ); }   // do_turn.cpp:766
//Possible TODO, make npc/monsters affected                                              // weather.cpp:457
return here.is_outside( tripoint_bub_ms( pos.xy(), here.get_abs_sub().z() ) ) && here.get_abs_sub().z() >= 0;
```

**Fix:** Always run the world-side parts (funnels, field decay, lightning reset) regardless of player position. Apply wetting and passive effects per creature, based on that creature's own z and `is_outside`. Use `pos.z()` in `is_creature_outside`.

### LOOP-B16 · Radio weather forecast: highs/lows accumulate across periods, Today/Tonight never shown, night labels shifted, "worst" weather picked by id order
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/weather.cpp:572`, `src/weather.cpp:573`, `src/weather.cpp:585`, `src/weather.cpp:590`, `src/weather.cpp:592`, `src/weather.cpp:599`, `src/weather.cpp:602`, `src/string_id.h:229`

`weather_forecast` has four defects:
1. `high` and `low` are declared outside the 6-period loop and never reset, so each period shows the running max and min.
2. For d==0, the second if/else always overwrites "Today"/"Tonight" with the weekday name.
3. `started_at_night` is a per-iteration local that is false for d>0, so night-start reports put the "Night" labels on the wrong periods.
4. `std::max` on `weather_type_id` compares interned string indices, whose order string_id.h says is arbitrary.

The forecast is flavor text only.

```cpp
units::temperature high = 0_K; units::temperature low = 1000_K;   // before for( int d = 0; d < 6; d++ )
forecast = std::max( forecast, wgen.get_weather_conditions( w ) );
bool started_at_night;                                            // inside the loop
```

**Fix:** Reset high and low per period and hoist `started_at_night` out of the loop. Only overwrite the label when d>0. Pick the reported weather by `priority` (or precip/danger), not by id order.

### LOOP-B17 · grab(): stale `optional_vpart_position` used after `separate_from_grid` compacts the part list
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/handle_action.cpp:680`, `src/handle_action.cpp:709`, `src/handle_action.cpp:717`, `src/handle_action.cpp:718`, `src/handle_action.cpp:727`, `src/vehicle.cpp:2174`, `src/vehicle.cpp:8572`

Grabbing part of a multi-part power grid splits it off (`separate_from_grid` → `part_removal_cleanup` erases parts). The code refreshes only the name through `split_vp`. It keeps using the original `vp` (old vehicle, old part index) for `has_loaded_furniture()`, `part_with_feature("FURNITURE_TIEDOWN")` and the boarding check. That index may now point to a different part, or past the end, which is undefined behaviour. Appliances rarely have these features, so the visible impact is small.

```cpp
const optional_vpart_position &vp = here.veh_at( grabp );
vp->vehicle().separate_from_grid( &here, vp.value().mount_pos() );
if( vp->has_loaded_furniture() ) {   // stale
```

**Fix:** After splitting, re-query `here.veh_at( grabp )` into a non-const local and use it for every later check.

### LOOP-B18 · Using stairs while riding a mech costs zero moves
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/game.cpp:9667`, `src/game.cpp:9670`, `src/game.cpp:9672`, `src/handle_action.cpp:2480`, `src/handle_action.cpp:2538`, `src/game.cpp:7816`

`vertical_move` deducts `move_cost` only when the player is neither mounted nor in a vehicle. For mounts it only charges mech power. Non-mech mounts are already refused at the stairs ("You can't go up stairs while you're riding"), but RIDEABLE_MECH riders change z-level for free. `walk_move` charges mounted players by mount speed.

```cpp
if( u.is_mounted() ) { ... if( crit->has_flag( mon_flag_RIDEABLE_MECH ) ) { crit->use_mech_power( ... ); } }
else if( !u.in_vehicle ) { u.mod_moves( -move_cost ); ...
```

**Fix:** Charge mounted characters a move cost scaled by the mount's speed, as `walk_move` does.

### LOOP-B19 · place_player: right-foot injury damages the left foot; mounted push loop moves the monster repeatedly
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/game.cpp:8171`, `src/game.cpp:8176`, `src/game.cpp:8256`, `src/game.cpp:8257`, `src/monmove.cpp:2220`

There are two bugs here:
1. The ROUGH-terrain "You hurt your right foot" branch checks `foot_r` armor but damages `foot_l`.
2. When a mounted player displaces a monster, the `while( !maybe_valid.empty() )` loop has no `break` after a successful `move_to`. `move_to` has no adjacency check and deducts moves on each call, so the monster is moved through every remaining candidate tile, paying moves and printing "You push the X out of the way." each time.

```cpp
add_msg( m_bad, _( "You hurt your right foot on the %s!" ), ...
u.deal_damage( nullptr, bodypart_id( "foot_l" ), damage_instance( damage_cut, 1 ) );   // 8176
while( !maybe_valid.empty() ) { if( critter.move_to( random_entry_removed( maybe_valid ) ) ) { add_msg( ... ); moved = true; } }
```

**Fix:** Use `foot_r` in the second block. Add `break;` after a successful `move_to`.

### LOOP-B20 · Diary time strings: "Early/Mid/Late" uses day-of-month, and the year is off by one
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/calendar.cpp:1014`, `src/calendar.cpp:981`, `src/calendar.cpp:836`, `src/calendar.cpp:875`, `src/diary.cpp:542`

With no time accuracy (no watch and no view of the sky), `day_norm = month_day.second * 100 / season_days`. In a 364-day year `month_day.second` is the day of the month (1-31), so the label is always "Early", or "Mid" on the 31st. In other year lengths it is the day of the year, so the label becomes "Late" too early. The diary year uses `years_since_cataclysm` without the `+1` that `to_string(time_point)` adds, so diary entries say "Year 0" while the rest of the UI says "Year 1".

```cpp
const int day_norm = ( month_day.second * 100 ) / to_days<int>( calendar::season_length() );   // 1014
const int year = calendar::years_since_cataclysm( turn );                                       // 981 (836 adds +1)
```

**Fix:** Use `day_of_season<int>( turn )` for `day_norm` and add +1 to the diary year.

### LOOP-B21 · weather_type `copy-from` silently resets `condition` to always-true and can only append `passive_effects`
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/weather_type.cpp:148`, `src/weather_type.cpp:135`, `src/condition.cpp:371`, `src/weather_gen.cpp:262`

`read_condition` replaces the condition with a constant `true` whenever the member is absent, ignoring `was_loaded`. A `copy-from` that only tweaks `light_modifier` therefore yields a weather type that wins whenever its required_weathers match. `passive_effects` are `emplace_back`ed onto the inherited vector, so a copy-from cannot replace or remove them. The one core copy-from (`moonlit_scenario_weather`) sets its own condition, so the bug is latent.

```cpp
// FIXME: generic factory reader for condition
read_condition( jo, "condition", condition, true );
if( !jo.has_member( member_name ) ) { condition = null_function; }   // condition.cpp:371
```

**Fix:** Call `read_condition` only when the member is present or `!was_loaded`. Clear `passive_effect` before reading, or support extend/delete.

## 3. Performance

### LOOP-P01 · sum_conditions catch-up evaluates full weather once per in-game minute (up to ~10,000 calls per funnel or vehicle)
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/weather.cpp:203`, `src/weather.cpp:210`, `src/weather.cpp:214`, `src/weather.cpp:220`, `src/weather.cpp:222`, `src/map.cpp:9979`, `src/map.cpp:9611`, `src/vehicle.cpp:8664`, `src/vehicle.cpp:8773`, `src/weather_type.cpp:129`

Every funnel tile on an actualized submap (`map.cpp:9979`) and every solar, wind, water or funnel vehicle that comes back into range calls `sum_conditions(last_touched, now)`. Gaps between 10 turns and 7 days are stepped one minute at a time, so a one-week absence costs 10,080 iterations. Each iteration copies the weather_generator and runs a full classification: noise, a dialogue and about 25 JMath conditions. It also redoes `overmap_buffer.ter()` and `get_local_windpower()`, whose inputs never change inside the loop; it even uses the *current* windspeed and direction. Live weather only changes every 5 minutes by default, so 1-minute sampling is at least 5× finer than the simulation itself. A base with several rain collectors or solar vehicles can stall for a second or more on return. Each iteration also consumes RNG and can change `current_winddir` (LOOP-B13).

```cpp
for( time_point t = start; t < end; t += tick_size ) {
    if( diff < 10_turns ) tick_size = 1_turns; else if( diff > 7_days ) tick_size = 1_hours; else tick_size = 1_minutes;
    weather_type_id wtype = current_weather( location, t );
    data.wind_amount += get_local_windpower( weather.windspeed, overmap_buffer.ter( ... ), location, weather.winddirection, false ) * ...
```

**Fix:** Use a 5-minute tick (1 hour for spans over a day). Hoist the oter lookup and `get_local_windpower` out of the loop and compute `wind_amount` once as windpower × duration. Pass a `const weather_generator &` instead of copying per tick, and share a memoized classifier (LOOP-P05). Expect 10-60× fewer evaluations.

### LOOP-P02 · An active weather radio rebuilds a full 3-day forecast (72 generations plus 72 classifications) every turn
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/weather.cpp:579`, `src/weather.cpp:581`, `src/weather.cpp:583`, `src/weather.cpp:585`, `src/iuse.cpp:2278`, `src/iuse.cpp:2303`, `src/weather_gen.cpp:247`, `src/weather_gen.cpp:209`, `src/weather_gen.cpp:214`, `src/item_gun_tool_ammo.cpp:4390`, `src/item.cpp:4213`

The radio_on RADIO_TICK handler runs every turn for each active radio and calls `weather_forecast()` with no caching. That function copies the weather_generator 6 times and makes 72 `get_weather` + `get_weather_conditions` calls. Each classification builds a dialogue and checks up to 25 conditions. `radio_tick` then broadcasts only one `RADIO_PER_TURN` segment of the text. The cost is about 1 ms per turn per tuned weather radio. The 72 calls also consume RNG and can flip `current_winddir` (LOOP-B13).

```cpp
for( int d = 0; d < 6; d++ ) {
    const weather_generator wgen = get_weather().get_cur_weather_gen();   // copy
    for( time_point i = ...; ...; i += 1_hours ) {
        w_point w = wgen.get_weather( abs_ms_pos, i, g->get_seed() );
        forecast = std::max( forecast, wgen.get_weather_conditions( w ) );
```

**Fix:** Cache the forecast text keyed on (tower `abs_sm_pos`, `last_hour`) and regenerate only when the in-game hour changes. Keep the per-turn obscure and fold step on the cached string. Bind `const weather_generator &` once outside the loop.

### LOOP-P03 · `get_snow_depth_mm` falls back to an O(n) scan over an unbounded map, on every monster move and Character step
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/weather.cpp:1160`, `src/weather.cpp:1170`, `src/weather.cpp:1118`, `src/weather.cpp:1098`, `src/weather.cpp:1156`, `src/monmove.cpp:1943`, `src/monmove.cpp:2270`, `src/character.cpp:6326`, `src/cata_tiles.cpp:926`, `src/savegame.cpp:1722`

`snow_depth_map` gains an entry for each OMT the player spends a minute in. The per-minute update sets `last_update = turn` on every entry, so the 6-hour prune never fires during a session. Zero-depth entries are dropped only when saving. A lookup for any OMT other than the player's own scans the whole `unordered_map` for the nearest neighbour. Callers are `monster::calc_movecost` (once per `move_to`), `Character::run_cost` (every NPC step) and the snow overlay (when enabled). For example, 100 outdoor monsters × 2,000 entries is about 200k node visits per turn all winter.

```cpp
auto it = snow_depth_map.find( ground_pos );
if( it != snow_depth_map.end() ) { return it->second.depth_mm; }
// Seed from nearest neighbor that has data
for( const auto &pair : snow_depth_map ) { const int dist = square_dist( ground_pos, pair.first );
```

**Fix:** Accumulation and melt apply uniformly to all entries, so on a miss return the player's OMT depth or a single regional value, in O(1), or memoize the seeded value. Alternatively, index entries in a coarse spatial grid and search only the 3x3 neighbourhood. Prune by distance from the player.

### LOOP-P04 · Long-absence item rot catch-up recomputes weather temperature per item per hour
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/item_degrade.cpp:1347`, `src/item_degrade.cpp:1369`, `src/item_degrade.cpp:1377`, `src/weather_gen.cpp:94`, `src/weather_gen.cpp:157`, `src/item.cpp:4209`

On actualize, every temperature-tracked item left outside the bubble for more than an hour loops hour by hour, calling `get_weather_temperature`. Each call runs `get_common_data` (cos and season math), a second cos and `raw_noise_4d`. The result depends only on (time, tile), so it is identical for every item in the same place and hour. A base with 2,000 food items revisited after 30 days does about 1.4M identical noise evaluations in the load frame. For non-perishables, hours older than 2 days compute `env_temperature` and then discard it.

```cpp
while( now - time > 1_hours ) {
    time += time_delta;
    if( pos.z() >= 0 && flag != temperature_flag::ROOT_CELLAR ) {
        env_temperature = wgen.get_weather_temperature( get_map().get_abs( pos ), time, seed );
```

**Fix:** Memoize `get_weather_temperature` on (turn, submap or OMT, seed), cleared per turn or as a capped LRU, or precompute an hourly series per submap in actualize. For items that don't rot, skip directly to `now - 2 days`.

### LOOP-P05 · `current_weather()` copies the generator and re-runs full dialogue-based classification on every call
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/weather.cpp:192`, `src/weather.cpp:195`, `src/weather_gen.cpp:237`, `src/weather_gen.cpp:247`, `src/game.cpp:4907`, `src/game.cpp:4921`, `src/monmove.cpp:235`, `src/bionics.cpp:1493`, `src/map.cpp:6336`, `src/vehicle.cpp:5642`, `src/weather.cpp:1021`, `src/game.cpp:9915`

`current_weather()` copies `weather_generator` by value before it checks `weather_override`. It then recomputes noise and builds a dialogue to evaluate every weather condition. Callers include `game::is_in_sunlight(map*, p)` for SUNDEATH monster moves, solar bionics in `burn_fuel`, mobile weather stations (4 calls per item per turn, compared by `.str()`), and vehicle solar power. The sibling `is_in_sunlight(const tripoint_bub_ms&)` simply reads `weather_id`. The reviewer found the hot-path claims overstated: `is_sheltered` filters most SUNDEATH calls, and the solar paths run rarely. `current_weather` does ignore ETERNAL_WEATHER, but only after dimension travel resets `weather_override`.

```cpp
const weather_generator wgen = weather.get_cur_weather_gen();     // copy first
if( weather.weather_override != WEATHER_NULL ) { return weather.weather_override; }
return wgen.get_weather_conditions( location, t, g->get_seed() );
```

**Fix:** Bind `const weather_generator &`. When `t == calendar::turn` and the location is in the bubble, return `get_weather().weather_id`. Otherwise memoize on (t rounded to 5 min, OMT, seed), cleared on weather update. In `map.cpp`, compute the weather once per item loop and compare `weather_type_id` constants.

### LOOP-P06 · Stepping onto an item pile builds tname() and display_name() for every item and copies items
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/game.cpp:8396`, `src/game.cpp:8406`, `src/game.cpp:8431`, `src/game.cpp:8433`, `src/game.cpp:8455`, `src/options.cpp:1953`

On every player step onto a tile with items, `place_player` computes both `tname()` and `display_name()` for each item and copies each new item, including its contents. It stops only after 11 *distinct* names, so 300 identical items cost 600 name builds. All of this happens before the `LOG_ITEMS_ON_THE_GROUND` check. That option defaults to true, so the real waste is the doubled name building and walking whole uniform piles.

```cpp
for( const item &tmpitem : ms ) {
    std::string next_tname = tmpitem.tname(); std::string next_dname = tmpitem.display_name();
    ... items.push_back( tmpitem ); if( names.size() > 10 ) { break; }
```

**Fix:** Check the option (and quick mode) before the loop. Store `const item *` instead of copies. Build only the name that is needed, group by `typeId()`/`stacks_with` before building strings, and cap total iterations (e.g. 50).

### LOOP-P07 · `mon_info_update` runs twice per turn
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/do_turn.cpp:699`, `src/do_turn.cpp:756`, `src/do_turn.cpp:649`, `src/do_turn.cpp:622`, `src/game.cpp:3864`, `src/character.cpp:6440`, `src/game.cpp:3713`, `src/player_activity.cpp:504`

During activities, `do_turn` calls `mon_info_update()` at line 699 and again after monmove at 756. Normal play also calls it twice (649 and 756). Each call does `get_visible_creatures(MAPSIZE_X)` with `sees()` on every creature. Monsters move only between the 756 call and the next turn's 699 call, so the first call usually recomputes the same result. The claimed third scan from `is_hostile_within` is small: it is range-gated before `sees()`. The real saving is one O(N) `sees()` pass per turn, which is small next to monmove.

```cpp
mon_info_update();   // do_turn.cpp:699
// required after monsters move and fields emit
mon_info_update();   // do_turn.cpp:756
```

**Fix:** Skip the first call unless the avatar's position or z, the creature count, or a dirty flag (spawn, death, vision rebuild) changed. Let `is_hostile_within` reuse `mon_visible`.

### LOOP-P08 · `overmap_npc_move` unloads and reloads every active NPC whenever any off-bubble traveller moves
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/do_turn.cpp:459`, `src/do_turn.cpp:469`, `src/do_turn.cpp:751`, `src/game.cpp:1307`, `src/game.cpp:1147`, `src/npc.cpp:3628`, `src/npc.cpp:3640`, `src/npc.cpp:3665`, `src/effect_on_condition.cpp:212`, `src/npcmove.cpp:2089`

Every 5 game minutes, if any travelling NPC took an overmap step, `do_turn` calls `reload_npcs()`, which unloads all active NPCs and reloads them. For each NPC, `npc::on_load` re-runs placement, gravity, vehicle and mount lookups and `effect_on_conditions::load_existing_character`, which iterates every EOC. It also sets `has_new_items = true`, which forces a full weapon re-evaluation. A TODO in the code acknowledges this. There is also a gameplay quirk: NPCs with an activity or destination get `mod_moves(dt)` bonus moves on each reload.

```cpp
elem->travel_overmap( elem->omt_path.back() ); npcs_need_reload = true; ... if( npcs_need_reload ) { g->reload_npcs(); }
// TODO: Make it not invoke the "on_unload" command for the NPCs that will be loaded anyway
```

**Fix:** Call `load_npcs()` (which already skips active NPCs) and unload only the moved NPCs that are now outside the bubble. Set `has_new_items` in `on_load` only after a real unload of non-zero duration.

### LOOP-P09 · draw_ter runs the curses creature loop every redraw in tiles mode, and keeps a curses blink layer alive
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/game.cpp:3567`, `src/game.cpp:3512`, `src/game.cpp:3518`, `src/game.cpp:3522`, `src/game.cpp:3526`, `src/creature.cpp:3442`, `src/sdltiles.cpp:4105`, `src/handle_action.cpp:454`

In tiles mode the tilecontext draws `w_terrain`, and `Creature::draw` returns early. `draw_ter` still loops over all creatures. For each one it calls `sees()`, and for each unseen creature in the viewport it builds a `const_dialogue`, which allocates two talkers. `draw_critter` also fills `blink_layer_curses` for visible creatures above or below. While that layer is non-empty, `handle_action.cpp:454` invalidates the whole UI every BLINK_SPEED ms (300 ms by default), forcing full tile redraws for an effect only curses shows.

```cpp
for( Creature &critter : all_creatures() ) { draw_critter( critter, center ); }
const_dialogue d( get_const_talker_for( u ), get_const_talker_for( critter ) );
if( is_draw_tiles_mode() ) { return; }   // creature.cpp:3442
```

**Fix:** Skip the creature loop and the curses-only blink and async layers when `is_draw_tiles_mode()`. In curses mode, check for special-vision entries before building the dialogue.

### LOOP-P10 · handle_progress_ui builds the progress string and a press_x hint every turn, but shows them at most once a minute
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/do_turn.cpp:488`, `src/do_turn.cpp:491`, `src/do_turn.cpp:503`, `src/do_turn.cpp:770`, `src/action.cpp:515`, `src/game.cpp:2571`, `src/game.cpp:2729`, `src/activity_actor.cpp:7157`

Every activity turn calls `get_progress_message` (for crafts, an uncached `tname()`). It also calls `press_x(ACTION_PAUSE)`, which copies the static ~150-action DEFAULTMODE `input_context` by value. The resulting string is only used inside the `once_every(min(1 min, refresh))` branch. The waste is tens of microseconds per turn.

```cpp
} else if( const std::optional<std::string> progress = u.activity.get_progress_message( u ) ) {
    ... wait_message += string_format( _( "\n%s to interrupt" ), press_x( ACTION_PAUSE ) );
if( first_redraw_since_waiting_started || calendar::once_every( std::min( 1_minutes, wait_refresh_rate ) ) ) {
```

**Fix:** Decide whether to redraw first, and build the message only inside that branch. Have `get_default_mode_input_context` return a const reference, or cache the `press_x(ACTION_PAUSE)` string until keybindings change.

### LOOP-P11 · find_nearby_spawn_point constructs a full monster on each of up to 75 attempts
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/game.cpp:4629`, `src/game.cpp:4632`, `src/game.cpp:4432`, `src/monster.cpp:265`, `src/npctalk.cpp:7893`, `src/npctalk.cpp:7923`, `src/monster.cpp:522`

The mtype overload builds a temporary `monster( mt->id )` as the first `&&` operand on every attempt, before the cheap distance and outdoor checks. The constructor sets up anatomy, builds a dialogue, rolls RNG for every special-attack cooldown, and creates items for mechs. An EOC that spawns 30 monsters into crowded terrain can construct up to 2,250 throwaway monsters and consume RNG on each one. The EOC spawn path at `npctalk.cpp:7923` already holds a constructed monster it could pass in.

```cpp
for( int attempts = 0; attempts < 75; attempts++ ) {
    target_point = target + tripoint( rng( -max_radius, max_radius ), rng( -max_radius, max_radius ), 0 );
    if( can_place_monster( monster( mt->id ), target_point ) &&
```

**Fix:** Hoist `const monster probe( mt->id );` above the loop, or accept a monster reference. Run the cheap tests before `can_place_monster`.

### LOOP-P12 · get_heat_radiation scans 169 tiles per call and is called uncached per item on submap load
**Severity:** Low · **Effort:** Medium · **Verified:** plausible
**Where:** `src/game.cpp:1647`, `src/game.cpp:1654`, `src/game.cpp:1690`, `src/field.cpp:146`, `src/weather.cpp:1192`, `src/weather.cpp:1203`, `src/item_degrade.cpp:1310`, `src/item_degrade.cpp:1356`, `src/character_body.cpp:483`, `src/character_body.cpp:497`

`get_heat_radiation` and its twin `get_best_fire` walk a 13×13 area with no early exit. Usually nothing is found, because only 5 core terrains radiate heat and fires are rare. `item_degrade.cpp:1356` calls it uncached for each item in the long-absence branch. Each tile is cheap, since `find_field` returns early on tiles with no fields, and the per-turn path is cached per location in `get_temperature`. The measured cost is a few percent of LOOP-P04's catch-up.

```cpp
for( const tripoint_bub_ms &dest : here.points_in_radius( location, 6 ) ) {
    maptile mt = here.maptile_at( dest ); int ffire = maptile_field_intensity( mt, fd_fire_int ); ...
temp_mod = get_heat_radiation( pos );   // item_degrade.cpp:1356, per item
```

**Fix:** Memoize `get_heat_radiation` per location in the item catch-up branch. A per-turn heat-source list (fires from `process_fields`, heat terrain registered in actualize) is possible but is not justified by the measured cost.

### LOOP-P13 · update_overmap_seen runs an O(r³) LOS sweep with per-line vector allocation on every submap shift
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/game.cpp:10351`, `src/game.cpp:10356`, `src/game.cpp:10363`, `src/game.cpp:10378`, `src/game.cpp:10382`, `src/map.cpp:1946`, `src/coordinates.h:1008`

`update_map` calls `update_overmap_seen` on every submap shift. Fast driving does several shifts per turn, because `displace_vehicle` calls `update_map`. The function visits (2r+1)² OMTs (r about 10-12, or about 20-24 with ZOOM optics) and calls `line_to` for each one. The coord wrapper allocates two vectors per line without reserving. The result is recomputed even when the player's OMT and range have not changed. Overmap lookups mostly hit the `last_requested_overmap` fast path. The total is well under 1 ms, which is small next to the shift itself.

```cpp
for( const tripoint_abs_omt &p : points_in_radius( ompos, dist ) ) {
    const std::vector<tripoint_abs_omt> line = line_to( ompos, p );
    for( auto it = line.begin(); it != line.end() && sight_points >= 0; ++it ) {
```

**Fix:** Memoize the last (ompos, dist, light bucket) and return early when it is unchanged. Use one ray-march or shadowcast from the centre, with a reused buffer.

## Appendix: considered and rejected

- do_regular_action rebuilds three std::maps of ~50 translated strings on every action – the `_()` macro has a per-call-site translation cache, so this is about 100 small allocations once per player action. That is negligible next to the cost of a turn and is code hygiene, not a performance issue.
