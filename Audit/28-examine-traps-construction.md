# 28 · Examine actions, traps, computers & construction

> Engine audit – static code reading at commit `a1a646d6`; nothing was compiled or run. Each finding was checked against the code by a separate reviewer: **confirmed** = verified by reading the code; **plausible** = consistent with the code, but reachability or impact not proven. Line numbers refer to that commit.

**Scope:** `src/iexamine.cpp`, `src/iexamine_actors.cpp`, `src/trap.cpp`, `src/trapfunc.cpp`, `src/computer.cpp`, `src/computer_session.cpp`, `src/fungal_effects.cpp`, `src/emit.cpp`, `src/gates.cpp`, `src/construction.cpp`, `src/construction_category.cpp`, `src/construction_group.cpp`

## Summary

These systems are mostly hardcoded C++ tied to vanilla ids. Process furniture (kilns, smokers, mills, autoclaves), most trap actions, computer options, gates, fungal spread and construction post-specials all keep their content in code. The JSON escape hatches that do exist (the EOC examine actor, `eocs` traps, mapgen computer `eocs`) are under-powered: they have no conditions and no persistence, and some override the whole normal behaviour. The bug list is dominated by small, easy-to-fix logic errors in `iexamine.cpp` and `construction.cpp`: an inverted penalty, a discarded `clamp()`, a past-the-end dereference, a flag that is never cleared, and components that are lost. The one notable performance problem is `find_build_sequence` rebuilding the same index thousands of times during world load.

| Lens | Critical | High | Medium | Low | Total |
|---|---|---|---|---|---|
| Missing JSON / EOC | 0 | 2 | 7 | 9 | 18 |
| Bugs & quirks | 0 | 2 | 10 | 6 | 18 |
| Performance | 0 | 1 | 2 | 3 | 6 |
| **Total** | 0 | 5 | 19 | 18 | 42 |

**Fix first:**
1. **TERR-B01** · Quenching a smoker or braking a mill leaves PROCESSING on food: an easy exploit that makes food never rot, fixed with a small helper.
2. **TERR-B02** · Anaerobic digester dereferences `map_stack::end()`: undefined behaviour on a reachable path, fixed in a few lines.
3. **TERR-P01** · `find_build_sequence` rebuilds its index on every call: likely several hundred ms on every world load, fixed with one cached index plus memoisation.
4. **TERR-J06** · Trap `eocs` pile up on copy-from: already makes vanilla portal traps run an extra teleport, and the load-time validation is dead code.
5. **TERR-B03** · Digging and mining penalties are inverted: a one-character fix for a wrong gameplay cost on every dig.

## 1. Missing JSON bindings & EOC features

### TERR-J01 · Constructions have no EOC hooks; 22 C++ post_specials hold content JSON can't reach
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/construction.cpp:2348`, `src/construction.cpp:2405`, `src/construction.cpp:2427`, `src/construction.cpp:1772`, `src/construction.cpp:2027`, `src/construction.cpp:2065`, `src/construction.cpp:1484`, `src/construction.cpp:2550`, `src/event.h:331`

`construction::load()` (2348-2485) reads no condition, effect_on_condition or event fields. Anything beyond "set terrain or furniture and drop byproducts" has to be one of 18 hardcoded `pre_special` checks or 22 hardcoded `post_special` functions, and several of those functions hold content that should be data. `done_dig_grave` spawns one of 5 fixed monster ids at 1/10 and uses fixed item groups. `done_digormine_stair` checks two TROGLO traits and uses fixed kcal, thirst and sleepiness costs. `done_trunk_plank` always runs on `get_player_character()`. Helper speed-ups (0.4/0.75) are literals. No event fires when a construction finishes: `character_finished_activity` only carries `{character, activity, canceled}`. Modders can't gate a construction on character or world state (mana, faction, season), can't run effects while building or on completion, and can't react to a finished construction.

```cpp
// construction.cpp:2071-2076
if( one_in( 10 ) ) {
    static const std::array<mtype_id, 5> monids = {{ mon_zombie, mon_zombie_fat,
        mon_zombie_rot, mon_skeleton, mon_zombie_crawler }};
    g->place_critter_at( random_entry( monids ), p );
// construction.cpp:2561-2565
if( assistants >= 2 ) { final_time *= 0.4f; } else if( assistants == 1 ) { final_time *= 0.75f; }
```

**Fix:** Add construction fields `"condition"` (checked in `can_construct()`, alpha = builder, context `_pos`), `"do_turn_eocs"` and `"post_eocs"` (alpha = builder, context `_construction` = id, `_pos` = tile), loaded with `load_inline_eoc`. Add an event `character_finishes_construction {character, construction_str_id, tripoint pos}`. Then move `done_dig_grave`, `done_window_curtains`, `done_mark_firewood` and similar functions to JSON EOCs (`u_spawn_monster`, `map_spawn_item`, `set_trap`).

```json
{ "group": "ward_circle",
  "condition": { "math": [ "u_val('mana') >= 50" ] },
  "post_eocs": [ { "id": "EOC_WARD_DONE", "effect": [ { "u_cast_spell": { "id": "ward" }, "targeted": false } ] } ] }
```

### TERR-J02 · Process furniture (kiln, arc furnace, stook, autoclave, smoker, mill) is tied to vanilla furniture ids
**Severity:** High · **Effort:** Large · **Verified:** confirmed
**Where:** `src/iexamine.cpp:3062`, `src/iexamine.cpp:3127`, `src/iexamine.cpp:3182`, `src/iexamine.cpp:3274`, `src/iexamine.cpp:3315`, `src/iexamine.cpp:3415`, `src/iexamine.cpp:3442`, `src/iexamine.cpp:6174`, `src/iexamine.cpp:6402`

Every batch-processing furniture is a hardcoded empty/full state machine that compares the examined furniture against specific vanilla ids. If a mod gives its own furniture (say a brick kiln) `kiln_empty` or `arcfurnace_empty`, the function hits a debugmsg and does nothing. Inputs (wood/bone material, FRESH_GRAIN, cac2powder), outputs (charcoal, chem_carbide), loss formulas, durations (24 h, 2 h, 336 h), energy cost (1250 kJ) and smoker charcoal ratios are all C++ literals. `autoclave_empty` builds `furn_id("f_autoclave")` from a string at runtime, which bypasses static-id linting, and `smoker_options` checks fixed rack ids. A clunky EOC workaround exists (examine EOC + `u_map_run_item_eocs` + `queue_eocs` + `map_spawn_item`), but there is no reusable data-driven process actor. New kilns, smokers and drying racks are a frequently requested modding area.

```cpp
// iexamine.cpp:3066-3071
if( cur_kiln_type != furn_f_kiln_empty && cur_kiln_type != furn_f_kiln_metal_empty &&
    cur_kiln_type != furn_f_kiln_portable_empty ) {
    debugmsg( "Examined furniture has action kiln_empty, but is of type %s", ...
// iexamine.cpp:3447-3448
if( cur_autoclave_type == furn_id( "f_autoclave" ) ) { next_autoclave_type = furn_id( "f_autoclave_full" );
```

**Fix:** Add a pair of generic examine actors, with numeric values as `dbl_or_var` / `duration_or_var` so EOC math can scale them. The vanilla kiln, arc furnace, stook and autoclave then become pure data.

```json
{ "type": "process_start", "accepts": { "materials": [ "wood", "bone" ], "flags": [] },
  "reject_if_contains": [ "charcoal" ], "yield_percent": { "math": [ "40 + 2*u_skill('fabrication')" ] },
  "intermediate": "unfinished_charcoal", "output": "charcoal", "energy": "1250 kJ",
  "requires_outside": false, "firestarter": true, "furn_set": "f_kiln_full" }
{ "type": "process_finish", "duration": "24 h", "furn_set": "f_kiln_empty" }
```

### TERR-J03 · Generic `effect_on_condition` examine actor: no condition, wrong `this` on terrain, NPC quirk
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iexamine_actors.cpp:296`, `src/iexamine_actors.cpp:308`, `src/iexamine.h:30`, `src/mapdata.cpp:671`, `src/mapdata.cpp:729`, `src/mapdata.cpp:749`

This actor is the main way to replace the 80 hardcoded examine functions, but it is under-powered. (1) `iexamine_actor` has no `can_examine()`. `map_data_common_t::can_examine()` returns true whenever any actor exists, and actor entries in the selection menu are never greyed out. The EOC actor has no `condition` field, even though its sibling `mortar` actor has one. (2) `call()` always sets context `this` to the furniture id, so on terrain it reads `f_null`. Vanilla already puts this actor on terrain (`t_vitrified_pool_talker`, `t_pit_BEM_human_sample`). (3) An NPC facing several examine options always runs the first actor; a TODO admits this. (4) No beta talker is passed. Mitigation: an EOC can carry its own condition/`false_effect`, and `map_terrain_id` on the `pos` context var identifies the terrain. The real gaps are menu greying and gating `can_examine`.

```cpp
// iexamine_actors.cpp:300-302
dialogue d( get_talker_for( you ), nullptr );
d.set_value( "this", here.furn( examp ).id().str() );
// mapdata.cpp:673-675
if( !examine_actor.empty() ) { return true; }
```

**Fix:** Add `virtual bool can_examine( const Character &, const tripoint_bub_ms & ) const { return true; }` to `iexamine_actor`, and use it in `can_examine()` and when building menu entries (`e.enabled`). Give the EOC actor `"condition"`, `"condition_fail_msg"` (copy mortar's loader) and an optional `"npc_usable"`. Set `this` from whichever ter/furn owns the actor, and also set `this_ter` and `this_furn`.

```json
"examine_action": { "type": "effect_on_condition", "name": "Pray",
  "condition": { "u_has_trait": "SPIRITUAL" }, "condition_fail_msg": "You feel nothing.",
  "effect_on_conditions": [ "EOC_ALTAR" ] }
```

### TERR-J04 · cardreader actor: `query` is never honoured and hacking ignores the actor's own data
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iexamine_actors.cpp:213`, `src/iexamine_actors.cpp:257`, `src/iexamine_actors.cpp:280`, `src/iexamine_actors.cpp:284`, `src/activity_actor.cpp:1047`, `src/activity_actor.cpp:1107`, `doc/JSON/EXAMINE.md:178`

`query` is loaded and documented ("whether or not to query the player before activating"), but it is only read in `finalize()`. `call()` always runs `query_yn(query_msg)`, so `"query": false` still prompts, and with no `query_msg` the prompt is blank. Hacking goes through `hacking_activity_actor`, whose success branch ignores the actor's `terrain_changes`, `furn_changes`, `radius` and `mapgen_id`. It always turns `t_door_metal_locked` into `t_door_metal_c` within 3 tiles and sets the *terrain* under the reader to `t_card_reader_broken`, even when the reader is furniture. `finalize()` enforces this with a debugmsg, so every `mapgen_id` reader, and any reader that opens something other than vanilla metal doors, must set `allow_hacking: false`. That is why vanilla readers mapping to `t_thconc_floor` or `t_door_metal_o` have hacking disabled.

```cpp
// iexamine_actors.cpp:213
if( !cards.empty() && query_yn( _( query_msg ) ) ) {
// activity_actor.cpp:1110-1114
here.ter_set( examp, ter_t_card_reader_broken );
for( ... points_in_radius( examp, 3 ) ) { if( here.ter( tmp ) == ter_t_door_metal_locked ) here.ter_set( tmp, ter_t_door_metal_c );
```

**Fix:** `if( !cards.empty() && ( !query || query_yn( _( query_msg ) ) ) )`. When a hack succeeds, look up the cardreader actor on the examined ter/furn and call its `apply()`, so terrain_changes, furn_changes and mapgen_id are honoured. Add `"hack_difficulty"` (int or variable), `"hacked_ter_set"` / `"hacked_furn_set"` (replacing the fixed `t_card_reader_broken`) and `"hack_eocs"`. Then remove the `finalize()` constraint.

### TERR-J05 · `done_appliance` and `done_vehicle` work only for category APPLIANCE or id `constr_veh`
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/construction.cpp:1509`, `src/construction.cpp:1843`, `src/construction.cpp:1907`, `src/construction.cpp:1125`, `src/construction.cpp:1210`

`complete_construction()` removes the partial construction, which holds the consumed components, before post_specials run. The only exceptions are constructions in UI category `"APPLIANCE"` or with id `"constr_veh"`. `done_appliance` and `done_vehicle` need those components. A mod that puts a `done_appliance` construction in another category, or adds a second `done_vehicle` construction (the documented `vehicle_start` field implies more than one can exist), loses the components and gets "can't find partial construction". `check()` doesn't catch it. All shipped data happens to comply. Separately, the darkness check is bypassed for the hardcoded group `deconstruct_simple_furniture`, although a `dark_craftable` field exists for this and `player_can_see_to_build` already honours it.

```cpp
// construction.cpp:1510-1513
if( pc->id->category != construction_category_APPLIANCE && pc->id->id != construction_constr_veh ) {
    here.partial_con_remove( terp );
}
```

**Fix:** Keep the partial construction whenever `post_special` includes `done_appliance` or `done_vehicle`, or add an explicit `"post_flags": [ "keep_components" ]`. Have `check()` reject such constructions that would lose their components. Replace the group-id darkness bypass with `"dark_craftable": true` in that construction's JSON.

### TERR-J06 · Trap loader checks for `spell_data`/`eocs` can never fail; `eocs` pile up on copy-from
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/trap.cpp:156`, `src/trap.cpp:158`, `src/trap.cpp:165`, `src/trap.cpp:166`, `src/trap.cpp:170`, `src/trap.h:91`, `src/trapfunc.cpp:1756`, `src/trapfunc.cpp:503`, `data/json/traps.json:979`

`trap::load` tries to reject `"spell_data"` and `"eocs"` unless the action is `spell` or `eocs` by comparing `std::function::target_type()`. Every entry in `trap_function_from_string` is a plain function pointer of the same signature, so `target_type()` is identical for all of them and the check never throws. A trap with `"action": "blade"` and `"eocs"` loads silently and its EOCs never run. The `eocs` loader (FIXME at 164) also passes an empty source mod to `load_inline_eoc` and `push_back`s without clearing, so a copy-from child runs the parent's EOCs plus its own. This already breaks vanilla: `tr_portal_dungeon_entrance/_teleporter/_exit/_next_level` and `tr_nl_chasm` copy-from `tr_swirling_vortex`, so each runs `EOC_SWIRLING_VORTEX`'s teleport before its own EOC. For example, `EOC_PORTAL_STORM_DUNGEON` records `portal_dungeon_original_loc` after that extra teleport has already happened.

```cpp
// trap.cpp:158
if( act.target_type() != trap_function_from_string( "spell" ).target_type() ) {
// trap.cpp:169-171
for( JsonValue jv : jo.get_array( "eocs" ) ) {
    eocs.push_back( effect_on_conditions::load_inline_eoc( jv, "" ) );
}
```

**Fix:** Store the action name (`std::string action_name`) next to `act` and compare names, or compare `act.target<bool(*)(...)>()` pointer values. Call `eocs.clear()` when the member is present, and pass the real `src` through. Move the check into `trap::check_consistency()` so copy-from children are validated too.

### TERR-J07 · Trap EOC hooks: creature-only, no disarm hook, no `map_trap_id`, event has no position
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/trapfunc.cpp:494`, `src/trapfunc.cpp:508`, `src/trapfunc.cpp:1692`, `src/trap.cpp:317`, `src/trap.cpp:343`, `src/event.h:515`, `src/iexamine.cpp:4726`, `src/condition.cpp:2664`

`"action": "eocs"` is the only general-purpose trap hook, and it has gaps beyond the always-remove problem in TERR-B08. It returns early when no creature triggered it, so item-weight and sound triggers do nothing (a documented TODO, `EFFECT_ON_CONDITION.md:71`). Disarming (`trap::examine`) is avatar-only, uses a fixed roll formula, special-cases `tr_telepad` + `DIMENSIONAL_ANCHOR` by id, and has no on-disarm, disarm-fail or disarm-condition hook. `character_triggers_trap` fires only for Characters, has no location field, and is not sent on the sound-trigger path. EOCs can write traps with `set_trap` but no condition reads them: `map_terrain_id`, `map_furniture_id` and `map_field_id` exist, `map_trap_id` does not. Workarounds: traps can be found via `u_location_variable`'s `"trap"` search (`npctalk.cpp:4495`).

```cpp
// trap.cpp:322 (sound path, no event)
act( pos, nullptr, nullptr );
// trap.cpp:344-345
if( Character *ch = creature->as_character() ) {
    get_event_bus().send<event_type::character_triggers_trap>( ch->getID(), id );
// iexamine.cpp:4747
id == tr_telepad && player_character.worn_with_flag( flag_DIMENSIONAL_ANCHOR )
```

**Fix:** When only an item triggered the trap, run the EOCs with no alpha talker and set context `_trigger_item` (itype id) and `trap_location`. Add trap fields `"disarm_eocs"`, `"disarm_fail_eocs"`, `"disarm_condition"` (alpha = disarmer) and `"easy_take_down_condition"` to replace the telepad special case. Add the condition `{ "map_trap_id": "tr_beartrap", "loc": { "context_val": "loc" } }` and a math function `trap_visibility(loc)`. Add `pos` to `character_triggers_trap`, and add a `monster_triggers_trap` event.

### TERR-J08 · Most trap actions hardcode damage, effects, break chances and dropped items
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/trapfunc.cpp:300`, `src/trapfunc.cpp:338`, `src/trapfunc.cpp:372`, `src/trapfunc.cpp:381`, `src/trapfunc.cpp:423`, `src/trapfunc.cpp:1756`, `src/trap.cpp:173`

Of 37 trap actions, only `spell` and `eocs` are data-driven. `beartrap` does 12 bash + 18 cut with a 1/128 tetanus chance (1/512 with INFRESIST), and when an item triggers it, it spawns `itype_beartrap` rather than the trap's own `"drops"`, so a modded heavy bear trap drops a vanilla one. `board` and `caltrops` hardcode cut ranges (3-5, 9-15), tetanus odds, break-by-weight thresholds (100 kg, 200 kg) and self-disarm chances (40%, 20%). Vanilla already shows the limit: `tr_teeth` ("scattered monster teeth") reuses `caltrops` and deals the same 9-15 cut as steel caltrops. The trap loader's only damage tuning is in `vehicle_data`.

```cpp
// trapfunc.cpp:323-325, 331, 338
d.add_damage( damage_bash, 12 ); d.add_damage( damage_cut, 18 );
const int chance_in = you->has_trait( trait_INFRESIST ) ? 512 : 128;
here.spawn_item( p, itype_beartrap );
```

**Fix:** Add a generic trap tuning block that the hardcoded actions read, with today's values as defaults. When an item triggers the trap, spawn `trap::components` (`"drops"`).

```json
"trigger_damage": [ { "damage_type": "bash", "amount": 12 }, { "damage_type": "cut", "amount": 18 } ],
"trigger_effects": [ { "id": "beartrap", "duration": 1, "permanent": true } ],
"tetanus_chance": 128, "break_weight": "100 kg", "self_disarm_chance": 40
```

### TERR-J09 · Computer options are a fixed C++ enum; EOC computers drop security, hacking and options
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/computer.h:23`, `src/computer.h:95`, `src/computer.cpp:436`, `src/computer_session.cpp:390`, `src/computer_session.cpp:493`, `src/game.cpp:4377`, `src/mapgen.cpp:3227`, `src/talker_furniture.cpp:43`

A computer option is `{name, action enum, security}`, and the action must be one of 52 hardcoded `COMPACT_*` values. Many are bespoke content: terrain swaps around the *player's* position (unlock: `t_door_metal_locked`→`t_door_metal_c` within 8 tiles), mission ids, snippets, manhack/secubot spawns. Failures are also a fixed enum. The only JSON route is mapgen `"eocs"`, and in `game::use_computer` that replaces the whole `computer_session`, so the computer gets no login security, hacking, options menu or failures. Modders can't add one custom EOC option to an otherwise normal terminal. Mapgen eocs are string-only (no inline EOCs) and never validated. EOCs can't read or change security or alerts, and the computer talker's `pos_abs_omt()` returns the player's OMT. Mitigation: fully dialogue-driven terminals (eocs + chat_topics, documented in MAPGEN.md and used by retirement_community and string_dimension) are an established route.

```cpp
// game.cpp:4377-4383
if( used->eocs.empty() ) { computer_session( *used ).use(); }
else { dialogue d( ... ); for( ... eoc : used->eocs ) eoc->activate( d ); }
// talker_furniture.cpp:43-46
tripoint_abs_omt talker_furniture_const::pos_abs_omt() const { return get_player_character().pos_abs_omt(); }
```

**Fix:** Add a data-driven option type where an absent `"action"` means "eoc", run inside `computer_session` with alpha = user, beta = computer and context `_pos` = computer location. Allow failures as `{ "eocs": [...] }`. Make mapgen `"eocs"` use `load_inline_eoc`, validate them, and add `"eocs_mode": "replace" | "on_login"`. Expose readable and assignable `computer_security()` / `computer_alerts()` on the computer talker. Fix `pos_abs_omt` to project `comp.loc`. Reimplement lock/unlock as terrain transforms anchored at the computer, e.g. `"ter_furn_transform": "lab_doors_unlock"` with a `"radius"`.

```json
{ "name": "Vent reactor", "security": 3, "condition": { ... }, "eocs": [ "EOC_VENT" ] }
```

### TERR-J10 · Construction treats an id as furniture only if it starts with `f_`
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/construction.cpp:2377`, `src/construction.cpp:2388`, `src/construction.cpp:2369`, `src/construction.cpp:2397`, `src/construction.cpp:2511`, `src/construction.cpp:299`, `src/construction.cpp:1536`

`construction::load` decides whether `pre_terrain` and `post_terrain` are furniture by checking whether the string starts with `f_`. Furniture ids without that prefix (Xedra_Evolved's `xe_f_throne`, `xe_f_balefire`) can't be a construction's input or output. Only the first element of `pre_terrain` is checked, so mixed lists are impossible. The booleans are never reset, so a copy-from child that switches from furniture to terrain fails `check()` with a misleading message. Also, `"using"` as an array appends to the inherited list while the string form replaces it, and `pre_flags`/`post_flags` are never validated. A wrong guess fails loudly at load, and no vanilla construction is affected, hence low.

```cpp
// construction.cpp:2379-2384
const std::string &first_pre_terrain = *pre_terrain.begin();
if( first_pre_terrain.size() > 1 && first_pre_terrain[0] == 'f' && first_pre_terrain[1] == '_' ) {
    pre_is_furniture = true;
}
```

**Fix:** Add explicit `"pre_furniture"` / `"post_furniture"` fields and keep the `f_` heuristic only as a deprecated fallback with a warning, or resolve each id in `finalize()` by checking `ter_str_id`/`furn_str_id` validity. Reset the booleans in `load()` when the members are present. Make the array form of `"using"` replace, and validate `pre_flags` against `flag_id` in `check()`.

### TERR-J11 · snare_species stores the placing item on the shared trap type, forcing one trap id per item
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iuse_actor.cpp:4327`, `src/trap.h:174`, `src/trap.cpp:427`, `src/trapfunc.cpp:866`, `src/trapfunc.cpp:888`, `src/trapfunc.cpp:920`, `src/savegame_json.cpp:5225`, `src/savegame_json.cpp:5496`

Species snares read the target species from the `capture_species` property of the placing item. That item id is written into `trap::trap_item_type` on the *global* trap definition via `const_cast`, and loading a save writes it back the same way, so every placed instance of a trap id uses whichever item was placed or loaded last. Placing a ROBOT snare and then a NETHER snare of the same trap id retargets both. Xedra_Evolved avoids this by defining five trap ids that all use `snare_species`. No trap-level field sets species, immobilisation duration or sound. Separately, the debugmsg at `trapfunc.cpp:888` has a `%s` with no argument.

```cpp
// iuse_actor.cpp:4329
const_cast<trap &>( placed_trap ).set_trap_data( it.typeId() );
// trapfunc.cpp:920
critter->add_effect( effect_immobilization, 10_turns, hit );
```

**Fix:** Move the data onto the trap definition and read it in `trapfunc::snare_species`. Delete `set_trap_data` and the per-tile `trap_item_type` save field, keeping a migration that ignores it on load.

```json
{ "type": "trap", "action": "snare_species", "capture_species": [ "ROBOT", "CYBORG" ],
  "capture_effect": "immobilization", "capture_duration": "10 turns" }
```

### TERR-J12 · EXAMINE.md out of date: 4 listed actions don't exist, 51 registered actions undocumented
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `doc/JSON/EXAMINE.md:76`, `doc/JSON/EXAMINE.md:79`, `doc/JSON/EXAMINE.md:82`, `doc/JSON/EXAMINE.md:84`, `src/iexamine.cpp:7504`, `src/iexamine.cpp:7611`, `src/iexamine.h:79`, `doc/JSON/JSON_INFO.md:3290`

`iexamine_functions_from_string` registers 80 names (including the internal `invalid`), and EXAMINE.md lists 33. `recycle_compactor`, `shelter` and `slot_machine` are documented but not registered (using them hits the "Could not find an iexamine function" debugmsg at 7611). `mortar` is listed as a string action but exists only as an actor. 51 registered actions are undocumented, including kiln/arcfurnace/autoclave/stook/fvat/compost/smoker/quern, keg, workout, ledge, vending, atm, pay_gas, reload_furniture, sign, tree_maple, harvest_ter/furn and fireplace (gaspump is documented; workbench/curtains/translocator get passing mentions elsewhere). The required furniture-id pairings for the process actions are documented nowhere. `iexamine.h` declares `cardreader`, `flower_tulip`, `flower_spurge`, `flower_bluebell` and `attunement_altar` with no definitions. `JSON_INFO.md:3290` uses a nonexistent `toilet` action.

**Fix:** Generate the hardcoded-action list from `function_map`, or add a unit test that fails when the doc and the map differ. For each process action, document the furniture ids it is tied to until TERR-J02 replaces them. Delete the stale declarations in `iexamine.h`.

### TERR-J13 · Construction docs list special names that throw at load; `post_flags` and `explain_failure` undocumented
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `doc/JSON/JSON_INFO.md:2156`, `doc/JSON/JSON_INFO.md:2174`, `doc/JSON/JSON_INFO.md:2176`, `doc/JSON/JSON_INFO.md:2180`, `doc/JSON/JSON_INFO.md:2205`, `src/construction.cpp:2336`, `src/construction.cpp:2405`, `src/construction.cpp:1516`, `src/construction.cpp:2497`, `src/construction.cpp:1291`

`construction_special_reader` throws on unknown names, so copying from the docs breaks loading. The docs list `pre_special` `check_empty_stable`, `check_deconstruction` (real name: `check_deconstruct`) and `check_ramp_low`, and `post_special` `done_matching_down_above` (real name: `add_matching_down_above`). The example uses `done_mine_upstairs` (real name: `done_mine_upstair`). `check_floor_above` exists but is undocumented. `"post_flags"` (only `keep_items` is consumed; other strings are silently ignored) and `"explain_failure"` are undocumented. `pre_flags` are never checked against flags.json at load, despite the doc's "Must be defined in flags.json"; the only lookup is in the UI colourizer.

```cpp
// construction.cpp:2336
jv.throw_error( string_format( "Unknown function: %s, ...
```

**Fix:** Correct the names in JSON_INFO.md, document `post_flags`/`keep_items` and `explain_failure`, and generate the special-function tables from the maps. In `construction::check()`, validate `pre_flags` against `flag_id` and `post_flags` against a known set.

### TERR-J14 · emit variables always use the avatar as talker; emit has no validation or copy-from
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/map_field.cpp:2173`, `src/emit.cpp:39`, `src/emit.cpp:57`, `src/emit.cpp:61`, `src/emit.h:27`, `doc/JSON/JSON_INFO.md:1599`, `doc/JSON/JSON_INFO.md:4526`

emit `field`, `intensity`, `qty` and `chance` accept variable objects, and the docs advertise this, but `map::emit_field` (used by monsters, bionics, items, vehicles, ter/furn and the EOC `emit` effect) always builds the dialogue with the avatar as alpha and sets no context. A monster or furniture emitter using `u_val(...)` silently gets the player's value, and no `_pos` is available. `load_emit` writes straight into a `std::map` without generic_factory (no copy-from, extend or abstract). `finalize()` and `check_consistency()` are empty, so `"field": "fd_smok"` is only caught at runtime. The two doc sections disagree about `chance` (">100 increases quantity" vs "1 to 100"). `emit::is_valid()` is declared but never defined.

```cpp
// map_field.cpp:2173
dialogue d( get_talker_for( get_avatar() ), nullptr );
// emit.cpp:59-65
void emit::finalize() { }  void emit::check_consistency() { }
```

**Fix:** Pass the emitter's talker into `emit_field` (monster/character talker, item talker, or none plus `_pos` for ter/furn), or at least set `_pos`. Move emit to generic_factory. In `check_consistency`, validate constant field ids and non-negative constant `chance`/`qty`. Merge the two doc sections. (See TERR-P05 for the matching performance fix.)

### TERR-J15 · Gates: only terrain can be a winch, the type is undocumented, and EOCs can't operate them
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/gates.cpp:85`, `src/gates.cpp:113`, `src/gates.cpp:261`, `src/iexamine.cpp:1416`, `src/computer_session.cpp:453`, `doc/JSON/JSON_INFO.md:510`

A gate is looked up by the *terrain* id at the winch tile (the gate id must equal the winch terrain id), so furniture levers and vehicle/appliance controls can never operate a gate. Door, floor and walls are terrain-only too. Only `iexamine::controls_gate` and `game::open_gate` reach gates; no EOC effect or examine actor does (an EOC can only crudely emulate one with `transform_radius`). The computer actions `open_gate`/`close_gate` bypass the gate system entirely and swap every `t_wall_metal` within 8 tiles of the player (used by the Aftershock arcology). The `gate` JSON type appears only as a filename in JSON_INFO.md.

```cpp
// gates.cpp:86-89
gate_id get_gate_id( const tripoint_bub_ms &pos ) { return gate_id( get_map().ter( pos ).id().str() ); }
```

**Fix:** Add an examine actor `{ "type": "gate_control", "gate": "<gate id>" }` usable on terrain or furniture, and an EOC effect `{ "toggle_gate": "<gate id>", "loc": { "context_val": "pos" } }`. Make the computer `open_gate`/`close_gate` options take a gate id, or use the option EOC from TERR-J09. Document the gate type's fields.

### TERR-J16 · Pit, curtain and safe examine actions hardcode terrain/furniture swaps and item drops
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/iexamine.cpp:1809`, `src/iexamine.cpp:1838`, `src/iexamine.cpp:4993`, `src/iexamine.cpp:1904`, `src/activity_actor.cpp:1106`, `src/activity_actor.cpp:6588`, `src/iexamine.cpp:7280`, `src/mapdata.cpp:1577`, `src/iexamine_actors.cpp:296`

Several common examine actions are "consume or give item, then swap terrain" functions tied to vanilla ids. `pit()` consumes a 2x4 and prints success *before* checking the terrain, and knows only three pit ids, so a modded pit using it eats a plank and nothing changes. `pit_covered()` always hands back a 2x4 but swaps only the three vanilla ids, so a modded covered pit duplicates planks endlessly. `curtains()` always drops 4 nails, 2 sheets, a stick and string. The safe, gunsafe_el and safecracking paths always set `f_safe_o`/`f_safe_c`, so a modded safe turns into the vanilla one. These only misbehave when a mod reuses them on non-vanilla ids; the EOC examine actor can already build such actions (`u_query`, `u_consume_item`, `set_terrain`, `u_spawn_item`), and a curtain terrain without `curtain_transform` is already reported at load (`mapdata.cpp:1577`).

```cpp
// iexamine.cpp:1820-1830
you.consume_items( planks, 1, is_crafting_component );
... if( ter_pit == ter_t_pit ) {...} else if( ter_pit == ter_t_pit_spiked ) {...} else if( ter_pit == ter_t_pit_glass ) {...}
add_msg( _( "You place a plank of wood over the pit." ) );
```

**Fix:** Add a generic `"transform"` examine actor. Add a `"curtain_drops"` item group to `ter_t`, and `"safecrack_result"` / `"hack_result"` furniture fields mirroring the existing `"lockpick_result"`.

```json
{ "type": "transform", "name": "Cover the pit", "query": "Place a plank over the pit?",
  "consume": [ [ "2x4", 1 ] ], "ter_set": "t_pit_covered", "moves": 100,
  "condition": { ... }, "message": "You place a plank of wood over the pit." }
```

### TERR-J17 · Harvest has no EOC hook, so special plants stay bespoke C++ (poppy, cactus, dahlia, marloss)
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/iexamine.cpp:2384`, `src/iexamine.cpp:2443`, `src/iexamine.cpp:2471`, `src/iexamine.cpp:2591`, `src/iexamine.cpp:2335`, `src/iexamine.cpp:7588`, `src/iexamine.cpp:353`, `src/harvest.h:73`

Ordinary harvesting is data-driven, but harvest lists have no condition, on-harvest effect or hazard, and `harvest.cpp` has no EOC support. Plants with side effects are separate C++ functions: `flower_poppy` (2 h sleep, 4 damage per leg, 1-in-20 opiate addiction, rain modifier), `flower_cactus` (1-in-6 arm damage), `flower_dahlia` (root drop gated on qual_DIG or BURROW), `flower_marloss` (teleglow). Nectar drinking is gated on two hardcoded traits (PROBOSCIS, BEAK_HUM) instead of a character flag. Mitigation: the EOC examine actor already allows fully data-driven special plants. Note: the `harvestable_functions` set lists `"harvest_plant"`, which is not a registered action. That entry is dead code, but it should **not** be renamed to `harvest_plant_ex`: `harvestable_now` requires a non-empty harvest list, and `f_plant_harvest` has none, so the rename would make planted crops unexaminable.

```cpp
// iexamine.cpp:2337-2338
( you.has_active_mutation( trait_PROBOSCIS ) || you.has_active_mutation( trait_BEAK_HUM ) )
```

**Fix:** Extend `harvest_by_season` entries or `harvest_list` with `"condition"`, `"eocs"` (alpha = harvester, context `_pos`) and `"query"`. Replace the nectar trait checks with a `json_character_flag` such as `"NECTAR_DRINKER"`. The poppy, cactus and dahlia functions then become harvest JSON plus a small EOC. Remove the dead `"harvest_plant"` entry.

### TERR-J18 · Fungal spread is a hardcoded flag-to-terrain table with fixed odds; spores hit only the avatar
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/fungal_effects.cpp:61`, `src/fungal_effects.cpp:73`, `src/fungal_effects.cpp:106`, `src/fungal_effects.cpp:147`

`spread_fungus_one_tile` maps ter/furn flags (DIGGABLE, FLAT+INDOORS, SUPPORTS_ROOF, SHRUB, THIN_OBSTACLE, YOUNG, TREE, WALL+FLAMMABLE, FLOWER, ORGANIC) to 11 hardcoded fungal ids with fixed `x_in_y` odds, and spawns `mon_fungal_blossom` by id. A mod can't control what its terrain becomes, or add its own blight. `fungalize()` affects only monsters and the avatar (TODO: NPCs), with hardcoded per-limb chances, a 9-minute spore duration, a TAIL_CATTLE check and a 250-creature cap. EOCs can trigger spreading via `u_marlossify`, but nothing in JSON controls the table. The engine already has `ter_furn_transform`, which could express it.

```cpp
// fungal_effects.cpp:152-155
if( here.has_flag_ter( ter_furn_flag::TFLAG_DIGGABLE, p ) ) {
    if( x_in_y( growth * 10, 100 ) ) { here.ter_set( p, ter_t_fungus );
```

**Fix:** Add an optional ter/furn field `"fungal_transform": "<ter_furn_transform id>"`, with a global default transform (e.g. `fungal_spread`) that encodes today's table. Apply `fungalize` to any Character, and use a `json_character_flag` instead of TAIL_CATTLE.

## 2. Bugs, oversights & quirks

### TERR-B01 · Quenching a smoker or braking a mill leaves PROCESSING on food, so it never rots
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iexamine.cpp:6485`, `src/iexamine.cpp:6350`, `src/iexamine.cpp:7261`, `src/iexamine.cpp:7230`, `src/iexamine.cpp:7028`, `src/iexamine.cpp:7000`, `src/item_degrade.cpp:432`, `src/iexamine.cpp:6648`

`smoker_fire()` and `mill_activate()` set `flag_PROCESSING` on the food, and `item::goes_bad()` returns false for anything with that flag. It is only cleared in `smoker_finalize()` (for items with no smoking result; the rest are replaced) when the timer completes. Smoker "Quench burning charcoal" (case 7), smoker "Remove food" while active (case 4), mill "Apply brake" (case 4) and mill "Remove products" while milling (case 3) all just switch the furniture back. `on_smoke_out` skips finalize once the rack is no longer active, so the flag stays forever. Mill partial-batch leftovers in `mill_finalize` also keep it. Exploit: put raw meat in the rack, light it, quench it, remove the food, and it never rots. Removing food while smoking is also easy to do by accident.

```cpp
// iexamine.cpp:6482-6486
if( it.is_smokable() ) { ... it.set_flag( flag_PROCESSING ); }
// iexamine.cpp:7261-7268 (case 7)
here.furn_set( examp, furn_f_smoking_rack ); add_msg( ... "You stop the smoking process." );
// item_degrade.cpp:432-434
if( has_flag( flag_PROCESSING ) ) { return false; }
```

**Fix:** Add a helper such as `stop_processing( map &, const tripoint_bub_ms & )` that clears `flag_PROCESSING` from every item on the tile and removes the fake smoke plume or fake milling item. Call it from quench, smoker remove-while-active, mill brake, mill remove-while-active, `mill_finalize`'s leftovers and the debug-recovery branches.

### TERR-B02 · Anaerobic digester dereferences `map_stack::end()` (read and write past the last item)
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iexamine.cpp:4058`, `src/iexamine.cpp:4087`, `src/iexamine.cpp:4142`, `src/iexamine.cpp:4073`, `src/iexamine.cpp:4077`, `src/iexamine.cpp:3837`, `src/iexamine.cpp:3841`

`compost_empty()` sets a birthday through `*items_here.end()` when digestion starts, and `compost_full()` reads `ferm.age()` from the same past-the-end colony slot (and writes again at 4142) to compute the biogas yield. This is undefined behaviour on a reachable path (`f_compost_empty`/`f_compost_full`). The colony's minimum block size is 8 and the tank normally holds one item, so `end()` usually lands in an unconstructed slot rather than corrupting live data, which is why this is high rather than critical. The value survives within a session by accident, but it is never saved, so after reload the biogas amount is garbage. These functions and `fvat_full` also call `here.i_rem( examp, &it )` inside a range-for over the same stack, which invalidates the iterator when a non-liquid item is present (rare, since the contents are normally liquid).

```cpp
// iexamine.cpp:4057-4059
map_stack items_here = here.i_at( examp );
item &ferm = *items_here.end();
ferm.set_birthday( calendar::turn );
// iexamine.cpp:4087-4088
item &ferm = *items_here.end();
const time_duration last_open_time = ferm.age();
```

**Fix:** Store the timer properly: either as an item var on the biomass item (`set_var("last_gas_release", ...)`), or use `items_here.begin()`, the single biomass item. Have `compost_full` read that. Replace the erase-in-range-for loops with iterator loops (`it = items.erase( it )`).

### TERR-B03 · Digging and mining penalties are inverted: troglodyte traits make digging more tiring
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/construction.cpp:2032`, `src/construction.cpp:2035`, `src/construction.cpp:2037`, `src/construction.cpp:2133`, `src/construction.cpp:2136`

`no_mut_penalty` is set to 10 (or 15 in `done_mine_upstair`) when the character *has* PAINRESIST_TROGLO or STOCKY_TROGLO ("Subterranean Fortitude" / "Subterranean Build"), and to 0 otherwise. Underground-adapted characters pay an extra 90-135 kcal, 10-15 thirst and 10-15 sleepiness per dig for stairs and mining up/down, and everyone else pays the base cost. The variable name confirms the intent is the opposite.

```cpp
bool dig_muts = player_character.has_trait( trait_PAINRESIST_TROGLO ) ||
                player_character.has_trait( trait_STOCKY_TROGLO );
int no_mut_penalty = dig_muts ? 10 : 0;
player_character.mod_stored_kcal( -43 - 9 * mine_penalty - 9 * no_mut_penalty );
```

**Fix:** `int no_mut_penalty = dig_muts ? 0 : 10;` (and `dig_muts ? 0 : 15` at 2136).

### TERR-B04 · Keg/bathtub says "It is empty" and destroys stored liquid if a non-empty container sits in it
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iexamine.cpp:4248`, `src/iexamine.cpp:4257`, `src/iexamine.cpp:4304`, `src/iexamine.cpp:4327`, `data/json/furniture_and_terrain/furniture-plumbing.json:12`

`iexamine::keg` treats the keg as empty whenever any non-empty, unloadable container is on the tile. `f_bathtub` has CONTAINER/PLACE_ITEM, so this is easy to hit. It takes the fill path: `displace_items_except_one_liquid` keeps the existing liquid, then `here.i_clear( examp )` deletes it and adds only the newly poured drink. Unlike `pour_into_keg`, charges are not merged. Trigger: a bathtub with 150 L of water, drop a filled canteen or a packed bag in, examine, Fill. The 150 L is silently deleted.

```cpp
const bool has_container_with_liquid = map_cursor( ... ).has_item_with( []( const item & it ) {
    return !it.is_container_empty() && it.can_unload(); } );
if( !liquid_present || has_container_with_liquid ) {
    add_msg( m_info, _( "It is empty." ) );
    ... displace_items_except_one_liquid( examp );
    ... here.i_clear( examp ); here.add_item( examp, drink );
```

**Fix:** Run `displace_items_except_one_liquid` first, then decide emptiness from the remaining liquid. When adding to an existing liquid of the same type, merge charges as `pour_into_keg` does instead of using `i_clear`. Refuse a different liquid type.

### TERR-B05 · Autodoc uninstall needs no anesthetic (consumes an unchecked, possibly empty tool list)
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iexamine.cpp:5895`, `src/iexamine.cpp:5906`, `src/iexamine.cpp:5993`, `src/iexamine.cpp:6001`, `src/iexamine.cpp:6005`, `src/crafting.cpp:4545`, `src/crafting.cpp:4571`, `src/crafting.cpp:5178`

Install is gated by `has_enough_anesth` using `requirement_data_anesthetic` scaled by body weight. Uninstall builds `anesth_kit` from any ANESTHESIA-quality item with ammo, never checks it, and calls `consume_tools( anesth_kit, volume )`. With an empty list, or kits holding fewer than `difficulty*40` charges, `select_tool_component` returns `usage_from::none` and nothing is consumed, while `introduce_into_anesthesia` and `uninstall_bionic` still run. `can_uninstall_bionic` has no anesthetic check either. Result: free uninstalls with full narcosis and no anesthetic.

```cpp
for( const item *anesthesia_item : a_filter ) {
    if( anesthesia_item->ammo_remaining( ) >= 1 ) { anesth_kit.emplace_back( anesthesia_item->typeId(), 1 ); }
}
...
patient.introduce_into_anesthesia( duration, installer, needs_anesthesia );
if( needs_anesthesia ) { you.consume_tools( anesth_kit, volume_anesth ); }
```

**Fix:** Use the same requirement as install (`*requirement_data_anesthetic * difficulty*2 * weight`). Check `can_make_with_inventory` before the operation, show `list_missing()` if it fails, then consume.

### TERR-B06 · Construction placement destroys already-consumed components when a later selection is cancelled
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/construction.cpp:1448`, `src/construction.cpp:1451`, `src/construction.cpp:1452`, `src/crafting.cpp:4489`, `src/crafting.cpp:4316`

`place_construction()` consumes component groups one at a time with `can_cancel=true`. If the player cancels the choice prompt for group N, `consume_items` returns an empty list and the function returns. Groups 1..N-1 have already been removed from inventory or the map, and they were held only in the local `used` list, which is dropped. The prompt only appears when a later group has at least two available alternatives (e.g. nails or screws). Cancelling it destroys the 2x4s from the first group.

```cpp
for( const std::vector<item_comp> &it : con.requirements->get_components() ) {
    std::list<item> tmp = player_character.consume_items( it, 1, is_crafting_component,
                          return_false<itype_id>, true );
    if( tmp.empty() ) {
        return;   // earlier groups already consumed, `used` discarded
    }
    used.splice( used.end(), tmp );
}
```

**Fix:** Do it in two phases like crafting: call `select_item_component` for every group first, abort if any is cancelled, then consume. Or, on early return, give back everything in `used` with `add_item_or_charges` at the player's position.

### TERR-B07 · done_trunk_plank: the 2-3 log loop yields one log's planks, and always runs on the avatar
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/construction.cpp:1772`, `src/iuse.cpp:4230`, `src/iuse.cpp:4238`, `src/character.cpp:5363`, `src/character.cpp:5371`, `src/character.cpp:5449`, `src/construction.cpp:1561`, `data/json/construction/misc.json:38`

`done_trunk_plank` ignores `who` and calls `cut_log_into_planks( get_player_character() )` 2-3 times. Each call assigns a 20-minute `chop_planks` activity. ACT_CHOP_PLANKS has `can_resume: false`, so the earlier ones are pushed to the backlog, and `resume_backlog_activity` only resumes auto-resume entries, so only one log is ever cut. The ACT_MULTIPLE_CONSTRUCTION check at 1562 then sees a chop activity at the backlog front and doesn't resume multi-construction. If an NPC finishes the construction from a blueprint zone, the avatar's current activity is replaced wherever they are, and the planks spawn at the avatar's position.

```cpp
void construct::done_trunk_plank( const tripoint_bub_ms &/*p*/, Character &/*who*/ )
{
    int num_logs = rng( 2, 3 );
    Character &player_character = get_player_character();
    for( int i = 0; i < num_logs; ++i ) {
        iuse::cut_log_into_planks( player_character );
    }
}
```

**Fix:** Use `who`. Either pass the log count into a single `chop_planks_activity_actor` (add a `logs` field), or spawn planks directly at `p` using the chop_planks yield formula `num_logs` times.

### TERR-B08 · `eocs` traps always delete the trap afterwards, wiping any trap the EOC set and ignoring UNCONSUMED
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/trapfunc.cpp:494`, `src/trapfunc.cpp:497`, `src/trapfunc.cpp:501`, `src/trapfunc.cpp:508`, `src/trapfunc.cpp:1680`, `src/trapfunc.cpp:1692`

`trapfunc::eocs` runs the EOCs and then always calls `remove_trap(p)`. If the EOC re-arms or changes the trap with the documented `set_trap` effect at `trap_location`, the new trap is removed immediately. Unlike `cast_spell`, the UNCONSUMED flag is ignored, so persistent EOC traps (pressure plates, healing or teleport pads) can't be built. About 31 `eocs` traps use this, including teleport/dimension-travel ones (`EOC_LS_stairs_*`, `EOC_TELEPAD`), where `remove_trap(p)` runs on stale bubble coordinates after the map has shifted. Item and sound triggers return early (see TERR-J07). Workaround: a `spell` trap with UNCONSUMED and an `effect_on_condition` spell. The per-trigger copy (`trap tr = here.tr_at( p )`) is harmless but unnecessary.

```cpp
if( !critter ) { //TODO: Pass the item as a talker ... return false; }
trap tr = here.tr_at( p );
for( const effect_on_condition_id &eoc : tr.eocs ) { ... eoc->activate_activation_only( d, ... ); }
here.remove_trap( p );
// compare cast_spell:
if( !tr.has_flag( json_flag_UNCONSUMED ) ) { here.remove_trap( p ); }
```

**Fix:** Remove the trap *before* running the EOCs, and only if it lacks UNCONSUMED, matching `cast_spell`. Take a `const trap &` instead of a copy.

### TERR-B09 · Mount-related trap effects always target the avatar, even when an NPC is riding
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/trapfunc.cpp:152`, `src/trapfunc.cpp:530`, `src/trapfunc.cpp:539`, `src/trapfunc.cpp:542`, `src/trapfunc.cpp:990`, `src/trapfunc.cpp:1406`, `src/trapfunc.cpp:1554`, `src/npcmove.cpp:3897`, `src/character.cpp:1670`

NPCs can mount creatures (`find_mount_activity_actor`), and a mounted NPC's move calls `creature_on_trap( *mounted_creature )` (`npcmove.cpp:3897`). Tripwire, goo, sinkhole and glow then act on `get_player_character()` instead of `z->mounted_player`. Tripwire teleports the avatar, possibly across the map, to the NPC's mount. Goo and sinkhole call `forced_dismount()` on the avatar, which works even when the avatar isn't mounted: it moves them to an adjacent tile, can deal bash damage, applies `downed` and writes a "Fell off a mount" memorial entry. Glow irradiates the avatar. The pit case (`pit_try_dismount`, FIXME at 152) is harmless because it compares positions. `dissector` and `mount_step_on_trap_make_slow_give_msg` already use `mounted_player` correctly.

```cpp
// goo
if( z->has_effect( effect_ridden ) ) { get_player_character().forced_dismount(); }
// dissector (correct)
if( z->has_effect( effect_ridden ) && z->mounted_player ) { Character *ch = z->mounted_player;
```

**Fix:** Add one helper `Character *rider_of( monster & )` returning `z->mounted_player`, use it in every trapfunc, and call `update_map` only when the rider is the avatar.

### TERR-B10 · Gas station: `clamp()` result discarded, so you can buy unlimited fuel with any balance
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iexamine.cpp:5425`, `src/iexamine.cpp:5432`, `src/iexamine.cpp:5437`, `src/iexamine.cpp:5444`

`pay_gas()` computes `maximum_liters` from the card balance, then discards the result of `clamp( liters, 0, maximum_liters )` (`clamp` returns by value). `query_int` has no upper bound, so whatever the player types is pumped, limited only by tank stock. `use_charges` then takes whatever cash the cards hold, and the displayed balance goes negative. Example: $14 on the card, type 200, get 200 L. The default is the correct max, so the player has to type a larger number deliberately.

```cpp
int maximum_liters = std::min( money / pricePerUnit, tankUnits / 1000 );
clamp( liters, 0, maximum_liters );   // return value ignored
if( !pGasPump || !toPumpFuel( pTank, *pGasPump, liters * 1000 ) ) {
```

**Fix:** `liters = clamp( liters, 0, maximum_liters );`, and refuse the purchase if `cost > you.charges_of( itype_cash_card )`.

### TERR-B11 · Vending machine: an item costing exactly your card balance is free, and mixed card+bank payments are mis-billed
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iexamine.cpp:1214`, `src/iexamine.cpp:1057`, `src/iexamine.cpp:1202`, `src/iexamine.cpp:1218`

The affordability check is `iprice > money`, but the payment check is a strict `iprice < card`. When the price equals the card balance on a normal machine, neither branch runs and the item is free. On BANK_NETWORKED machines (`f_vending_reinforced_networked`, `f_vending_c_networked`), any price the cards can't fully cover is charged entirely to `you.cash`, which can go negative, and the cards are untouched. Example: card $9, bank $2, item $10 leaves bank at -$8 and the card at $9.

```cpp
if( iprice < you.charges_of( itype_cash_card ) ) {
    you.use_charges( itype_cash_card, iprice );
} else if( use_bank ) {
    you.cash -= iprice;
}
```

**Fix:** `card = charges_of( itype_cash_card )`. If `iprice <= card`, `use_charges( iprice )`. Otherwise, if `use_bank`, `use_charges( card )` and `cash -= iprice - card`. Otherwise refuse the sale.

### TERR-B12 · Construction copy-from silently drops the parent's components, tools and qualities
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/construction.cpp:2364`, `src/construction.cpp:2366`, `src/construction.cpp:2678`, `src/requirements.cpp:497`, `src/requirements.cpp:563`

`construction` has no inheritance handling. `load()` always rebuilds the inline requirement `inline_construction_<id>` from the child's own JSON with `check_extend=false`. A copy-from construction that doesn't repeat components, tools and qualities becomes free to build (only `using` survives, through the copied `reqs_using`). There is no `extend` support and no warning, unlike `recipe.cpp`. No vanilla construction uses copy-from, so this only affects modders.

```cpp
const requirement_id req_id( "inline_construction_" + id.str() );
requirement_data::load_requirement( jo, req_id );
requirements = req_id;
```

**Fix:** When `was_loaded` and the JSON has no components, tools or qualities, copy the parent's `requirement_data` into the new `req_id`. Support `extend` by passing `check_extend=true`, or at least debugmsg as `recipe.cpp` does.

### TERR-B13 · Dissector trap hits the right leg twice and never the left leg
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/trapfunc.cpp:1037`, `src/trapfunc.cpp:1038`, `src/trapfunc.cpp:1059`, `src/trapfunc.cpp:1060`

Both the rider block and the main block deal the leg damage to `leg_r` twice (12 cut each) and never to `leg_l`. The arm, hand and foot pairs alternate l/r, so this is a copy-paste error.

```cpp
c->deal_damage( nullptr, bodypart_id( "leg_r" ), damage_instance( damage_cut, 12 ) );
c->deal_damage( nullptr, bodypart_id( "leg_r" ), damage_instance( damage_cut, 12 ) );
```

**Fix:** Change the first call in each pair to `leg_l`. Better, iterate `get_all_body_parts( only_main )` so non-human anatomies work.

### TERR-B14 · `trap::on_disarmed` drops the z coordinate: components spawn on the player's z-level
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/trap.cpp:394`, `src/map.h:1444`, `src/map.cpp:9311`, `src/trapfunc.cpp:418`, `src/trapfunc.cpp:459`

`on_disarmed` spawns the trap's `drops` at `p.xy()`. The `point_bub_ms` overload of `spawn_item` fills in `abs_sub.z()`, which for the main map is the player's z, while `remove_trap` uses the correct `p`. `board` and `caltrops` call this when any creature triggers them (40% and 20%). A zombie stepping on a nailboard in your basement while you're upstairs drops the nailboard on your floor, at the same x,y, possibly inside a wall.

```cpp
for( const trap::comp &i : components ) { m.spawn_item( p.xy(), i.item_type, i.quantity, i.charges ); }
```

**Fix:** `m.spawn_item( p, i.item_type, i.quantity, i.charges );`

### TERR-B15 · QUERY_DECONSTRUCT confirmation never appears
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/construction.cpp:2248`, `src/construction.cpp:2253`, `src/mapdata.cpp:1551`, `src/mapdata.cpp:1706`, `src/options.cpp:2045`, `src/construction.cpp:1720`

`do_turn_deconstruct` only asks for confirmation when the ter/furn has *both* a `deconstruct` block and a base `item`. `finalize()` debugmsgs on that combination, and a scan of data/json and data/mods found no ter/furn with both. The default-on QUERY_DECONSTRUCT option ("will query before deconstructing terrain/furniture") therefore never prompts. Deconstruction is already an explicit menu choice, so only the confirmation is lost. Related latent bug: the terrain branch of `check_deconstruct` (1720) tests `here.furn( p ).obj().base_item` instead of the terrain's.

```cpp
if( f.deconstruct && !f.base_item.is_null() ) {
    deconstruct_query( f.deconstruct->potential_deconstruct_items( f ) );
}
```

**Fix:** Query when `f.deconstruct || !f.base_item.is_null()`, using the extended-description text (`mapdata.cpp:841-845`) for the base_item-only case. Fix the copy-paste in `check_deconstruct` to use the terrain's base_item.

### TERR-B16 · Computer hacking lockout (`next_attempt`) is bypassed once main security is 0
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/computer_session.cpp:171`, `src/computer_session.cpp:172`, `src/computer_session.cpp:192`, `src/computer_session.cpp:239`, `src/computer_session.cpp:1495`, `src/computer_session.cpp:290`

`activate_random_failure()` sets a 45-minute lockout, but `use()` only checks `next_attempt` inside `if( comp.security > 0 )`. A successful login hack sets security to 0, and many consoles start at 0 with secured options, so a failed option hack can be retried immediately by re-examining. When a login hack fails on a console with no `failures`, no lockout is set at all. Every failed retry still triggers the console's failure (alarm, manhacks, shock, shutdown), and only 2 of 91 secured consoles have no failures, hence low.

```cpp
if( comp.security > 0 ) {
    if( calendar::turn < comp.next_attempt ) { ... return; }
```

**Fix:** Check `calendar::turn < comp.next_attempt` at the start of `use()` (or before any hack attempt), whatever the security level. Also set `next_attempt` in the "Maximum login attempts exceeded" branch.

### TERR-B17 · Autoclave "filthy CBM" and "unpacked CBM" checks use `all_of` instead of `any_of`
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iexamine.cpp:3460`, `src/iexamine.cpp:3473`, `src/iexamine.cpp:3520`, `src/iexamine.cpp:3553`, `src/vehicle_use.cpp:1414`

The message says "Some of those CBMs are filthy", but the cycle is only refused when *all* items are filthy CBMs. One clean plus one filthy CBM runs the cycle. The unpacked-CBM contamination warning in `autoclave_full` likewise only shows when every CBM is unpacked. The vehicle autoclave uses `any_of` for the same checks. Impact is mostly wrong or missing messages: a filthy CBM is only sterilised if it was packed.

```cpp
bool filthy_cbms = std::all_of( items.begin(), items.end(), []( const item & i ) {
    return i.is_bionic() && i.has_flag( flag_FILTHY ); } );
```

**Fix:** Use `std::any_of` with `i.has_flag( flag_FILTHY )`, and likewise with `flag_NO_PACKED` for the warning.

### TERR-B18 · Geiger console adds the platform reading inside the zone loop
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/computer_session.cpp:1347`, `src/computer_session.cpp:1353`, `src/computer_session.cpp:1359`

`action_geiger` adds the platform's radiation and increments `tiles_counted` once per zone tile, because the platform accumulation sits inside the radius-3 loop. The reported AVG is therefore about `(zone + 49*platform)/98`. MAX is unaffected, since the platform is itself one of the radius-3 tiles. The comment calls the platform the "primary measurement", so the weighting may be intended. The output is flavour text with no gameplay effect.

```cpp
for( const tripoint_bub_ms &dest : here.points_in_radius( platform, 3 ) ) {
    sum_rads += here.get_radiation( dest );
    tiles_counted ++;
    ...
    sum_rads += here.get_radiation( platform );
    tiles_counted ++;
```

**Fix:** Move the platform accumulation outside the loop, or drop it, since the platform is already counted.

## 3. Performance

### TERR-P01 · `find_build_sequence` rebuilds a string-keyed multimap of every construction on every call during blueprint finalization
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/construction.cpp:2691`, `src/construction.cpp:2697`, `src/construction.cpp:2777`, `src/construction.cpp:2805`, `src/recipe.cpp:831`, `src/recipe.cpp:871`, `src/mapgen.cpp:7798`

`get_build_reqs_for_furn_ter_ids()` calls `find_build_sequence()` once per distinct ter/furn id a blueprint places. Each call walks all constructions (783 in core, about 589 passing the filter), does an `is_blacklisted()` std::map lookup per construction, and inserts into a fresh `std::unordered_multimap<std::string, ...>`, copying every `post_terrain` string (many longer than SSO). It also allocates a new queue, map and set keyed by `std::string`. Call chain: init → `recipe_dictionary::finalize` → `recipe::finalize` (bp_autocalc) → `calculate_all_blueprint_reqs` → one call per mapgen-parameter combination (526 autocalc blueprints, 1803 combinations) → `add_builds` per id. That is thousands of calls on every world load, each building an identical index, and the result for a given target (e.g. `t_wall_log`) is always the same. The per-combination `small_fake_map` + `update_map` in `get_changed_ids_from_update` (`mapgen.cpp:7798`) is probably a cost of similar size that this fix does not touch.

```cpp
// construction.cpp:2697-2703
std::unordered_multimap<std::string, const construction *> construction_map;
for( construction const &cons : get_constructions() ) {
    if( !filter( cons ) || cons.is_blacklisted() ) continue;
    construction_map.insert( { cons.post_terrain, &cons } );
}
```

**Fix:** Build the `post_terrain` index once at the end of `finalize_constructions()`, after blacklisting is final; ideally key it by resolved `ter_id`/`furn_id` (see TERR-P03) and clear it in `reset_constructions()`. Memoize `find_build_sequence` results per target id inside `get_build_reqs_for_furn_ter_ids`, since the filter and `can_build` lambdas never change. Expected saving: likely a few hundred ms to about 1 s per world load with core data; confirm with a profiler.

### TERR-P02 · Construction menu re-runs full buildability checks for every visible row on every redraw, including 300 ms blink timeouts
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/construction.cpp:913`, `src/construction.cpp:415`, `src/construction.cpp:1163`, `src/construction.cpp:1193`, `src/construction.cpp:587`, `src/construction.cpp:1066`, `src/construction.cpp:1354`

`on_redraw` calls `construction_color()` for each visible group row (up to about 40). Each call runs `player_can_build_valid_constructions()`: a scan of all constructions to find the group, then `player_can_build()` with `can_construct` on every member. That means a full `can_make_with_inventory()`, which deliberately doesn't short-circuit, plus `can_construct` on all 8 adjacent tiles. Only `.front()`/`!empty()` of the result is used. The loop redraws on every iteration, and `set_timeout( BLINK_SPEED )` (default 300 ms) wakes it about three times a second while idle. The player's inventory and position can't change while the menu is open. The cost mostly matters with large crafting inventories. (In `valid_constructions_near_player`, `can_construct && player_can_build` short-circuits, so the inventory check only repeats on tiles where `can_construct` passed.)

```cpp
// construction.cpp:422-423
std::vector<const construction *> cons = player_can_build_valid_constructions( player_character,
        player_character.crafting_inventory(), group );
// construction.cpp:587
ctxt.set_timeout( get_option<int>( "BLINK_SPEED" ) );
```

**Fix:** Cache each group's colour per menu session in `std::unordered_map<construction_group_str_id, nc_color>`, filled lazily and cleared only when `filter_mode` changes. Let `construction_color` stop at the first buildable construction instead of building a vector. In `valid_constructions_near_player`, evaluate `player_can_build( ..., true )` once per construction before the tile loop. Optionally skip list recomputation on TIMEOUT redraws, which only need the blink preview.

### TERR-P03 · Construction prerequisites are stored as strings and re-resolved to ids on every check
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/construction.h:46`, `src/construction.h:48`, `src/construction.h:55`, `src/construction.cpp:292`, `src/construction.cpp:1224`, `src/construction.cpp:1302`, `src/activity_item_handling.cpp:1807`, `src/activity_item_handling.cpp:1838`

`pre_terrain` is a `std::set<std::string>`, `post_terrain` a `std::string`, and `pre_flags` a `std::map<std::string,bool>`. `has_pre_terrain`, `can_construct` and `already_done` build `ter_id(string)`/`furn_id(string)` each time: a string intern plus a factory lookup, with no cached cid. `has_pre_flags` uses the `std::set<std::string>` overload of `has_flag`, even though most flags have a `ter_furn_flag` enum bit. This runs per construction × tile in the menu (TERR-P02) and in the blueprint zone activity, where `_find_prereq`'s filter lambda re-resolves the loop-invariant `top_idx` id and does string set lookups for every construction at each recursion step. Pre_specials also run before the cheap terrain filter, and some do their own string work (`check_support`'s `has_flag( "EMPTY_SPACE" )`, three string builds per call in `check_matching_down_above`). Route pathfinding in `multi_activity_can_do` probably still dominates, hence medium.

```cpp
// construction.cpp:307-310
ter_id t = here.ter( p );
for( const auto &pre_terrain : con.pre_terrain ) { if( t == ter_id( pre_terrain ) )
// construction.cpp:1326-1328
return f != furn_id( con.post_terrain ); ... return t != ter_id( con.post_terrain );
```

**Fix:** In `construction::finalize()`, resolve and store `std::vector<ter_id>`/`std::vector<furn_id>` for pre-terrain, a `ter_id`/`furn_id` for post-terrain, and an `enum_bitset<ter_furn_flag>` for pre_flags (with a string fallback for non-enum flags). Keep the strings for display and save compatibility. Rewrite `has_pre_terrain`, `has_pre_flags`, `can_construct`, `already_done` and `_find_prereq` to compare ints, hoist the `top_idx` id out of the lambda, and test pre_terrain/pre_flags before pre_specials.

### TERR-P04 · `constructions_by_group`/`constructions_by_filter` linear-scan every construction with std::function and a new vector on each call
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/construction.cpp:332`, `src/construction.cpp:341`, `src/construction.cpp:1171`, `src/construction.cpp:1215`, `src/construction.cpp:1354`, `src/activity_item_handling.cpp:1786`, `src/activity_item_handling.cpp:1871`

There is no group-to-constructions index. Each `constructions_by_group()` walks all constructions through a `std::function`, does an `is_blacklisted()` map lookup per match, and returns a freshly allocated vector. It is called from `construction_color` (per row per redraw), `recalc_buffer`, `player_can_see_to_build`, `valid_constructions_near_player` and the blueprint activity's `find_base_construction`. Each call is only a few microseconds, and most of the cost is already counted in TERR-P01/P02.

```cpp
// construction.cpp:343-348
std::vector<const construction *> result;
for( const construction &constructions_a : get_constructions() ) {
    if( filter( constructions_a ) && !constructions_a.is_blacklisted() ) result.push_back(...)
```

**Fix:** Build `std::unordered_map<construction_group_str_id, std::vector<const construction *>>` at the end of `finalize_constructions()` (blacklisting is final by then: `init.cpp:857` runs before 897), return a `const &` from `constructions_by_group`, switch `_find_alt_construction`'s group filter to it, and clear it in `reset_constructions()`.

### TERR-P05 · Every field emission builds a dialogue and re-resolves its emit through std::map, though all shipped emits are constants
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/emit.h:29`, `src/emit.cpp:12`, `src/emit.cpp:57`, `src/map_field.cpp:2167`, `src/item.cpp:4831`, `src/vehicle.cpp:6315`, `src/monster.cpp:2996`

`map::emit_field()` runs for every active emitting item and every enabled emitting/exhaust vehicle part each turn, for monster emit_fields, and for ter/furn emitters every 10 s. Each call builds `dialogue d( get_talker_for( get_avatar() ), nullptr )` (a talker plus a const clone), does 2-5 `std::map` finds into `emits_all`, and `emit::field()` converts the evaluated string to `field_type_id`. All 129 emit definitions in data/json and data/mods are constant, yet `emit::finalize()` is empty. The saving is a few hundred ns per call; `propagate_field` dominates whenever an emission actually happens.

```cpp
// emit.h:33-35
field_type_id field( const_dialogue const &d ) const { return field_type_id( field_.evaluate( d ) ); }
// map_field.cpp:2169-2178
dialogue d( get_talker_for( get_avatar() ), nullptr );
const float chance = src->chance( d ) * mul; ... propagate_field( pos, src->field( d ), qty, src->intensity( d ) );
```

**Fix:** In `emit::finalize()`, when all four `value_or_var` members are constant, cache `field_type_id`, intensity, qty and chance with an `is_constant` flag. In `map::emit_field`, fetch `const emit &e = src.obj()` once and skip the dialogue on the constant path. Optionally move emits to generic_factory for int ids (pairs with TERR-J14).

### TERR-P06 · `construction::adjusted_time()` scans nearby characters and reads an option twice every turn of construction; uses the avatar's helpers for NPC builders
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/construction.cpp:2550`, `src/construction.cpp:2540`, `src/activity_actor.cpp:9522`, `src/crafting.cpp:5770`

`build_construction_activity_actor::do_turn` calls `built.adjusted_time()` every turn. That calls `get_avatar().get_crafting_helpers()` (a character scan with `clear_path` per candidate) and `time_scale()` reads `get_option<int>( "CONSTRUCTION_SCALING" )` twice. The performance gain is negligible (`clear_path` only runs for awake, obeying NPCs in range). The real value is the attached bug: for an NPC builder, helpers are computed around the avatar, and the building NPC itself passes the helper filter, so it counts as its own assistant and builds 25-60% faster near the player, while its own nearby helpers give no bonus.

```cpp
// activity_actor.cpp:9522
const double cur_total_moves = std::max( 100, built.adjusted_time() );
// construction.cpp:2555
for( Character *elem : get_avatar().get_crafting_helpers() ) {
```

**Fix:** Pass the builder into `adjusted_time()`, compute helpers from that builder (excluding the builder itself), and cache the helper count in the activity actor, refreshed about once per minute. Read the option once into a local.

## Appendix: considered and rejected

- Spell, EOC and snare trap triggers copy the whole trap, build a throwaway npc, or construct an item per trigger – accurate, but `trap::trigger` is rare, event-driven work whose follow-up (spells, spawns) costs far more. The dormant-corpse trap does not refire (it lacks UNCONSUMED and is removed on first trigger). Using `const trap &` is a cleanup with negligible speed gain.
- `harvest_plant` rebuilds the full crafting inventory for every plant even when no grass-cutting check is needed – real, but each call is followed by a 60-second harvest activity, so it is at most one rebuild per 60+ simulated turns (well under 1%), and the multi-farm loop already pre-filters CUT_HARVEST plants.
- `check_support_below` scans every creature in the bubble to test one tile – real, but only 2 constructions (bridge/dock) use it, so the cost is microseconds on a cold path; `creature_tracker::creature_at` would be cleaner but changes hallucination handling slightly.
