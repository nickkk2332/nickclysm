# 24 · Vehicle types, interaction, use & autodrive

> Engine audit – static code reading at commit `a1a646d6`; nothing was compiled or run. Each finding was checked against the code by a separate reviewer: **confirmed** = verified by reading the code; **plausible** = consistent with the code, but reachability or impact not proven. Line numbers refer to that commit.

**Scope:** `src/veh_type.cpp`, `src/vehicle_group.cpp`, `src/veh_interact.cpp`, `src/vehicle_use.cpp`, `src/veh_appliance.cpp`, `src/veh_utils.cpp`, `src/vehicle_autodrive.cpp`

## Summary

The vehicle subsystem works for vanilla content, but very little of it can be driven from JSON. EOCs can read only a handful of vehicle values and can write only vars and effects. Part toggles, washers, reapers, planters, mounted repair tools and many balance numbers are hardcoded C++ lists. The loaders for vparts, prototypes and vehicle groups have lossy or append-only copy-from rules that contradict the docs. There are a few real gameplay and crash bugs in the interaction menus: an engine-start bypass, an out-of-bounds read when filtering the door menu, autopilot modes that never start the engine, and a use-after-free in the Netherum boiler. On the performance side, the main costs are repeated tool and requirement preparation in hot UI and crafting-inventory paths, string-keyed part flags, and autodrive recomputing everything at each overmap tile.

| Lens | Critical | High | Medium | Low | Total |
|---|---|---|---|---|---|
| Missing JSON / EOC | 0 | 2 | 7 | 6 | 15 |
| Bugs & quirks | 0 | 2 | 7 | 7 | 16 |
| Performance | 0 | 0 | 5 | 3 | 8 |
| **Total** | 0 | 4 | 19 | 16 | 39 |

**Fix first:**

1. **VINT-B01** · Filtering the door-control menu to zero matches indexes `items[-1]`: a one-line bounds check fixes a crash anyone can trigger.
2. **VINT-B02** · "Control individual engines" sets `engine_on` directly: a small fix that closes a bypass of immobilizers, starter faults and fuel-pump faults reachable from a common menu.
3. **VINT-B03** · Autopilot Follow/Patrol never start a stopped engine: passing the driver is a one-line fix for a feature that otherwise silently does nothing.
4. **VINT-B08** · copy-from for vparts and prototypes is lossy: vanilla cars silently lose their inherited item spawns today, and two shipped parts cannot be repaired.
5. **VINT-J02** · Part toggles are a hardcoded list of 25 flags: this is the biggest blocker for modded powered parts and makes ACTIVE vpart enchantments unusable.

## 1. Missing JSON bindings & EOC features

### VINT-J01 · Vehicle talker is read-mostly and thin: EOCs can't read fuel, battery or part HP, and can't change parts or engines
**Severity:** High · **Effort:** Large · **Verified:** confirmed
**Where:** `src/talker_vehicle.h:48-67`, `src/talker_vehicle.h:88-92`, `src/talker.h:709-760`, `src/condition.cpp:2410-2414`, `src/npctalk.cpp:6974-7008`, `src/vehicle.cpp:1240-1243`, `src/talker_vehicle.cpp:84-87`, `src/npctalk.cpp:8355-8362`

`talker_vehicle` exposes only booleans (flying, floating, skidding, `has_part_flag`), facing, velocity, passenger counts, vars and effects. On a vehicle, an EOC can write only vars and effects. The math parser has no vehicle functions at all: nothing for fuel, battery charge or capacity, part HP, engine state, cruise velocity, owner, lock or alarm state, part counts or prototype id. Apart from `u_run_vehicle_eocs` (and the `take_vehicle` options of `u_teleport` / `travel_to_dimension`), no effect acts on a vehicle. Nothing can damage, repair, install, remove or toggle a part, start an engine, set speed or rename the vehicle. `vehicle::add_chat_topic` has no callers, so vehicles can never have dialogue. This blocks fuel-gauge spells, sentient-car NPCs, "rust plague" or EMP effects, and quest checks such as "bring me a working humvee".

```cpp
// condition.cpp:2410-2414 — the only vehicle vals
vehicle_facing, current_speed, unloaded_weight,
friendly_passenger_count, hostile_passenger_count
// talker_vehicle.h:88-92 — the only writes
set_value, remove_value, add_effect, remove_effect
```

**Fix:** Add math functions `vehicle_fuel('gasoline')` (assignable), `vehicle_battery()` and `vehicle_battery_max()`, `vehicle_part_count('FLAG')`, `vehicle_part_hp('FLAG', 'min'|'avg')` and `vehicle_type()`. Add effects `u_vehicle_damage_parts: { flag, amount, damage_type }`, `u_vehicle_repair_parts`, `u_vehicle_install_part: { part, mount }`, `u_vehicle_remove_part`, `u_vehicle_toggle_parts: { flag, state }`, `u_vehicle_engine: on|off`, `u_vehicle_set_speed` and `u_vehicle_set_owner`. Add `"chat_topics"` to vehicle prototypes plus a `u_add_chat_topic` effect.

### VINT-J02 · Part on/off toggles are a hardcoded list of 25 flags, so new powered parts (and ACTIVE enchantments) can't be switched on
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/vehicle_use.cpp:265-308`, `src/vehicle_use.cpp:337-390`, `src/vehicle.cpp:1558-1561`, `src/vehicle.cpp:1690-1730`, `src/magic_enchantment.cpp:403-423`

The electronics menu can toggle a part only if its flag is in a fixed C++ list (REACTOR, CONE_LIGHT, … SMART_ENGINE_CONTROLLER), each with a hardcoded label and hotkey. `vehicle::install_part` also disables every non-engine part unless its flag is in a second hardcoded list (with the comment `TODO: read toggle groups from JSON`). So a modded powered part (force-field emitter, heated seat, mana condenser) can never be enabled unless it borrows an unrelated flag such as STEREO and inherits that flag's side effects. Vpart enchantments are evaluated with `ench.is_active( *this, part.enabled )`, so an enchantment with an ACTIVE condition on such a part never activates.

```cpp
add_toggle( pgettext( "electronics menu option", "reactor" ), "TOGGLE_REACTOR", "REACTOR" );
// ... 24 more ...
// vehicle.cpp:1560
if( ench.is_active( *this, part.enabled ) && ench.is_vehicle_relevant() )
```

**Fix:** Add a vpart field `"toggle": { "group": "force_field", "name": "force field", "hotkey": "TOGGLE_FORCE_FIELD" }` (or a generic `TOGGLEABLE` flag that uses the part name). Build the menu and the `install_part` enable list from these toggle groups, and move the existing flags' groups into `vp_flags.json` as data.

### VINT-J03 · EOC_ACTIVATION / activatable_eoc is half-built: crashes if unset, no vehicle talker, whole-vehicle scan, undocumented
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle_use.cpp:2436-2443`, `src/veh_type.cpp:281`, `src/veh_type.h:341`, `src/veh_type.cpp:776-952`, `src/npctalk.cpp:8449`, `data/json/vehicleparts/vp_flags.json:57-61`, `data/json/debug/teleportation.json:43-44`

This is the only JSON-scriptable vehicle part interaction, and it has several problems:
1. A part with the flag but no `activatable_eoc` passes `check()`. Selecting it then calls `.value()` on an empty optional, which throws `bad_optional_access`. Nothing catches the exception, so the game terminates.
2. The dialogue is `dialogue( get_talker_for( player ), nullptr )`. There is no vehicle beta talker and no context vars for the part id or position, even though `get_talker_for(vehicle&)` exists. The only user (`EOC_PERAMBULATE_VEHICLE`) relies on `take_vehicle` at the player's position, which fails when the part is activated from an adjacent tile.
3. The loop uses `get_avail_parts( "EOC_ACTIVATION" )` over the whole vehicle, so every such part is listed from every tile, with no location hint.
4. Only an EOC id is accepted, not an inline EOC. There is no label, hotkey or condition, and the menu text is a hardcoded `"Activate  %s"` with a double space.
5. Neither the flag nor the field is documented. The only user is debug content.

```cpp
for( const vpart_reference &vp : this->get_avail_parts( "EOC_ACTIVATION" ) ) {
    ...
    dialogue newDialog( get_talker_for( get_player_character() ), nullptr );
    part.info().activatable_eoc.value()->activate( newDialog );
```

**Fix:** In `check()`, debugmsg when `has_flag("EOC_ACTIVATION") != activatable_eoc.has_value()` and validate the id. Replace the field with `"activation": { "eoc": <id or inline>, "name": "Engage warp drive", "hotkey": "w", "condition": { ... }, "requires_power": true }`, loaded through the inline-EOC loader. Build `dialogue( get_talker_for( player ), get_talker_for( *this ) )` with context vars `vpart_id`, `vpart_x`, `vpart_y` and `vpart_variant`. List only the parts on the examined tile. Document the field in JSON_INFO.md and the flag in JSON_FLAGS.md.

### VINT-J04 · Vehicle weight is unreadable or wrong in EOCs: weight() rejects vehicles and unloaded_weight overflows int above 2147 kg
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/talker_vehicle.cpp:94-102`, `src/math_parser_diag.cpp:1744-1755`, `src/math_parser_diag.cpp:1969`, `src/condition.cpp:2411-2412`, `doc/JSON/NPCs.md:1541-1544`

`talker_vehicle::get_weight` (total mass with cargo and passengers) has no reachable caller. The `weight()` math function accepts only characters, monsters and items, and throws for a vehicle. `unloaded_weight` narrows an int64 milligram mass to `int`, so it wraps for any vehicle over about 2147 kg (pickups, vans, buses, tanks). `current_speed` returns internal vmiph (mph × 100) with no units in the docs. NPCs.md also lists `weight` as a `val()` aspect that does not exist, so `val('weight')` throws "Invalid aspect".

```cpp
int talker_vehicle_const::get_weight() const {
    return units::to_milligram( me_veh_const->total_mass( get_map() ) ); // int64 -> int
}
throw math::runtime_error( "For weight(), talker is not character nor item" );
```

**Fix:** Return grams (or a double/int64 kg value) for vehicles. Let `weight()` accept vehicle talkers (`u_weight()` = total mass), add an `unloaded_weight()` math function, and document `current_speed` units or add `speed_kmh()`.

### VINT-J05 · Reaper and planter hardcode furniture, terrain and seed ids instead of using GROWTH_HARVEST, PLANTABLE and plant_data
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle_use.cpp:1062-1108`, `src/vehicle_use.cpp:1110-1148`, `src/vehicle_use.cpp:88-90`, `src/vehicle_use.cpp:100-102`, `src/vehicle_use.cpp:121-122`, `src/iexamine.cpp:2816`, `src/iexamine.cpp:2890`, `src/activity_actor.cpp:7751-7761`

The reaper harvests only `f_plant_harvest`, while manual harvesting uses the GROWTH_HARVEST flag. So the reaper ignores `f_planter_harvest` and modded crops, and afterwards it sets the furniture to null rather than `plant_data.base`. It skips `fungal_seeds` and `marloss_seed` by id (the same hardcode is in `iexamine.cpp:2841`). Yields are `rng(1, bonus)` plants and a hardcoded `rng(1,3)` seeds. The planter only plants into `t_dirtmound`, always produces `t_dirt` + `f_plant_seed`, and ignores the seed's `required_terrain_flag`. On other plowable terrain it drops the seed without planting it.

```cpp
} else if( here.furn( reaper_pos ) != furn_f_plant_harvest ) { continue; }
seed->typeId() == itype_fungal_seeds || seed->typeId() == itype_marloss_seed
} else if( t == ter_t_dirtmound ) { here.set( loc, ter_t_dirt, furn_f_plant_seed );
```

**Fix:** Reaper: test `TFLAG_GROWTH_HARVEST`, restore `plant_data.base`, and skip seeds with a new `NO_MACHINE_HARVEST` item flag (set in JSON on the fungal and marloss seeds). Planter: reuse `transform_terrain` (`"pre_flags": ["PLANTABLE"]`) or add `"planter": { "pre_flags": [...], "post_terrain": ..., "seed_furniture": ... }`, and honor `required_terrain_flag`. Add `"reaper": { "seed_yield": [1, 3] }`.

### VINT-J06 · No event or EOC hook for vehicle install, remove, repair, engine start/stop, part toggle or appliance placement
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/event.h:24-136`, `src/event.h:683-688`, `src/activity_actor.cpp:12043`, `src/veh_utils.cpp:107-174`, `src/activity_actor.cpp:12209-12212`, `src/vehicle_use.cpp:666-782`, `src/veh_appliance.cpp:86-157`, `src/veh_type.cpp:248-394`, `src/vehicle.cpp:8151`

Only two vehicle events exist. `vehicle_moves` fires on every move. `fuel_tank_explodes` carries only a `vehicle_name` string and is sent without a vehicle talker, so its EOC cannot act on the vehicle. Install, removal, repair, hotwiring, engine start or stop, electronics toggles and appliance placement emit no events. `character_finished_activity` carries only the activity id. `vpart_info::load` reads no per-part hook fields other than `activatable_eoc`. Modders cannot build cursed engines, vehicle-building achievements, faction reactions to car theft, or parts that react when switched on.

```cpp
// event.h:685 — the only field of fuel_tank_explodes
{ "vehicle_name", cata_variant_type::string }
```

**Fix:** Add events `vehicle_part_installed`, `vehicle_part_removed`, `vehicle_part_repaired`, `vehicle_part_destroyed`, `vehicle_engine_started`, `vehicle_engine_stopped`, `vehicle_part_toggled`, `vehicle_hotwired` and `appliance_placed`, with `{ character, vpart_id, vehicle_name }` and the vehicle as beta talker. Add a vpart field `"eocs": { "on_install": [...], "on_remove": [...], "on_enable": [...], "on_disable": [...], "on_destroyed": [...] }`, run with u = actor, npc = vehicle, and context vars `part_id` / `part_pos`.

### VINT-J07 · Overloaded vpart `bonus` int has 10+ meanings and the docs get several wrong (seatbelt, funnel, muffler)
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/veh_type.h:481-487`, `src/veh_type.cpp:265`, `doc/JSON/JSON_INFO.md:2782-2788`, `src/vehicle_move.cpp:2498`, `src/vehicle.cpp:8688`, `src/vehicle.cpp:9173`, `src/vehicle.cpp:4814`, `src/vehicle.cpp:7933`, `src/vehicle_use.cpp:895-906`, `src/vehicle_use.cpp:963`, `src/vehicle_use.cpp:1066`, `src/map.cpp:6505`, `src/lightmap.cpp:1517`

One untyped int means horn volume, beeper volume, light intensity, recharge watts, rotor extra diameter, shock-absorber reduction, seatbelt reduction, muffler factor, funnel size, reaper yield or vision range, depending on the part's flags. Three meanings are documented wrongly. The seatbelt is documented as "non-functional #30239", but the code subtracts it from crash damage. The funnel is documented as an area in mm², but the code treats it as a radius, so a modder who enters an area gets orders of magnitude more water. The muffler is documented as "% noise reduction", but it is actually the % of noise that remains (vanilla 40 = 60% reduction). The shock-absorber, reaper, beeper and vision uses are undocumented. The field has no units, and one part cannot hold two meanings at once (for example a lit horn).

```cpp
const double area_in_mm2 = std::pow( pt.info().bonus, 2 ) * M_PI;          // vehicle.cpp:8688
m = 1.0 - ( 1.0 - vp.info().bonus / 100.0 ) * vp.health_percent();         // vehicle.cpp:9173
dmg -= veh.part( belt_idx ).info().bonus;                                    // vehicle_move.cpp:2498
```

**Fix:** Fix the JSON_INFO comments now. Then split the field into typed fields, keeping `bonus` as a deprecated fallback: `"horn_volume"`, `"light_intensity"`, `"recharge_rate": "600 W"`, `"funnel_radius": "300 mm"`, `"muffler": { "noise_mult": 0.4 }`, `"seatbelt_strength"`, `"shock_absorption"`, `"reaper_yield"`, `"vision_range"`.

### VINT-J08 · Washing machine, dishwasher and autoclave are fully hardcoded (water ids and amounts, detergent, 90 min cycle)
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/vehicle_use.cpp:1410-1456`, `src/vehicle_use.cpp:1459-1542`, `src/vehicle_use.cpp:1545-1600`, `src/vehicle_use.cpp:2706-2735`, `src/map.cpp:6266-6318`, `src/vehicle_part.cpp:219-221`

All three appliances implement the same pattern three times: process the items in cargo for N minutes while consuming supplies. Everything is hardcoded: the water ids `water`/`water_clean`, 8 or 24 charges, 5 detergent, a 90-minute cycle in map.cpp, a separate "(1.5 hours)" menu literal, and an output that clears FILTHY or NO_STERILE. A modder cannot add a dehydrator, kiln, decontamination shower or magic purifier without C++. The dishwasher's detergent inconsistency is covered under VINT-B16.

```cpp
fuel_left( here, itype_water ) < 8 && fuel_left( here, itype_water_clean ) < 8   // vehicle_use.cpp:1432
const time_duration washing_time = 90_minutes;                                   // map.cpp:6274
```

**Fix:** Add a vpart slot `"item_processor": { "name": "washing machine", "time": "90 m", "requirements": "wash_cycle", "liquid": [ ["water", 24], ["water_clean", 24] ], "input_condition": { "flag": "FILTHY" }, "reject_condition": { "is_bionic": true }, "remove_flags": ["FILTHY"], "on_finish_eoc": ... }`, and turn WASHING_MACHINE, DISHWASHER and AUTOCLAVE into data.

### VINT-J09 · Vehicle-mounted repair tools only work for 6 hardcoded tool ids (welder "evil hack")
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/vehicle_use.cpp:2287-2300`, `src/activity_handlers.cpp:361-433`, `src/activity_handlers.cpp:441-466`, `src/iuse_actor.cpp:3108-3120`, `src/iuse_actor.cpp:221-229`

When a VEH_TOOLS station starts `ACT_REPAIR_ITEM`, the activity is told how to find the tool on the vehicle only if the tool is welder, welder_crude, welding_kit, soldering_iron, small_repairkit or large_repairkit. Any other `repair_item` tool starts the activity pointing at a temporary item that is destroyed immediately, and the repair fails with a debugmsg ("Empty main tool for repair" / "Lost tool used for long repair"). Shipped data hits this: Aftershock's `exo_gantry` lists the repair_item pseudo tool `exo_repair_kit`. `weldrig_hack` also always charges the pseudo tool from `itype_battery`, whatever ammo the tool actually uses.

```cpp
if( act.id() == ACT_REPAIR_ITEM && ( tool_type == itype_welder || tool_type == itype_welder_crude ||
    tool_type == itype_welding_kit || tool_type == itype_soldering_iron ||
    tool_type == itype_small_repairkit || tool_type == itype_large_repairkit ) )
```

**Fix:** Drop the id list. Apply the coords/str_values hand-off to any tool invoked from a vehicle (or any tool whose use is `repair_item`), and fill the pseudo item through `vehicle::prepare_tool`, which already handles arbitrary ammo and multi-magazine tools.

### VINT-J10 · More hardcoded content ids: horn_bicycle, wall_wiring, prof_aircraft_mechanic, water faucet/purifier, plut_cell
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle_use.cpp:877-891`, `src/veh_appliance.cpp:121-123`, `src/vehicle.cpp:2639`, `src/vehicle.cpp:2683`, `src/vehicle.cpp:4856-4886`, `src/veh_interact.cpp:1196-1202`, `src/vehicle_use.cpp:2777-2860`, `src/veh_interact.cpp:2990-3019`

1. Only the part with id `horn_bicycle` works without battery power, so modded unpowered horns need power.
2. An appliance gets the vehicle WIRING tag only when its base item is `wall_wiring`. A lone or split grid built from modded wiring therefore isn't a powergrid (merged grids get the tag anyway, vehicle.cpp:2129). The WIRING flag is documented as "TBD".
3. Flight-worthiness after install, repair or removal checks a hardcoded `prof_aircraft_mechanic`.
4. The faucet, purifier and funnel flows hardcode water → water_clean and the `water_purifier` tool.
5. Unloading solid fuel special-cases `plut_cell` with `PLUTONIUM_CHARGES`. That code also has a menu-indexing bug (VINT-B14).

```cpp
if( ( horn_type.id != vpart_horn_bicycle ) && no_power ) { continue; }
if( vpinfo.base_item == itype_wall_wiring ) { veh->add_tag( flag_WIRING ); }
return !pc.has_proficiency( proficiency_prof_aircraft_mechanic );
```

**Fix:** (1) Require power only when the horn has epower < 0, or add a `NO_POWER` flag. (2) Use `vpinfo.has_flag( "WIRING" )`. (3) Add `"flight_maintenance": { "proficiencies": [...] }` or reuse `control_requirements.air.proficiencies`. (4) Add `"liquid_conversion": { "from": "water", "to": "water_clean", "tool": ... }`. (5) Derive charges per item from the item's own charges.

### VINT-J11 · Docs drift in JSON_INFO vehicle sections: dead or misplaced fields, wrong formats that fail to load, undocumented fields
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `doc/JSON/JSON_INFO.md:2811`, `doc/JSON/JSON_INFO.md:2940-2941`, `doc/JSON/JSON_INFO.md:2961-2967`, `doc/JSON/JSON_INFO.md:3057-3064`, `doc/JSON/JSON_FLAGS.md:1833`, `src/veh_type.cpp:248-394`, `src/veh_type.cpp:424-447`, `src/veh_type.cpp:1345-1358`

- `"difficulty"` is documented but not loaded; it was replaced by `requirements.install.skills`.
- Wheel `"diameter"` and `"width"` are documented on the vpart, but they are item wheel-slot fields.
- WORKBENCH fields are documented flat, but the loader requires a nested `"workbench": { "mass": "150 kg" }`.
- The "Vehicles" example uses a string `"blueprint"` and the legacy `{"x":0,"y":0,"part":"box"}` form, so it fails to load.
- These loaded fields are undocumented: `activatable_eoc`, `enchantments`, `emissions`, `exhaust`, `light_color`, `categories`, `default_ammo` and VEH_TOOLS `allowed_tools`.
- WIRING is marked "TBD".

The folding/unfolding tools are documented.

```cpp
jo.get_array( "blueprint" );                                  // veh_type.cpp:1347
mandatory( jo, was_loaded, "workbench", workbench_info );     // veh_type.cpp:378
```

**Fix:** Regenerate the vehicle_part reference from `vpart_info::load`: remove `difficulty`, `diameter` and `width`, nest `workbench`, replace the legacy Vehicles example with a pointer to VEHICLES_JSON.md, and document the missing fields.

### VINT-J12 · deploy_appliance can't name a vpart: resolved by scanning all parts, with a silent standing-lamp fallback
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/veh_appliance.cpp:75-84`, `src/iuse_actor.cpp:1325-1335`, `src/iuse_actor.h:425-439`, `src/iexamine_actors.cpp:75-95`, `src/construction.cpp:1926`

`deploy_appliance` and `appliance_convert` take only an item id. The vpart is found by a linear scan for the first APPLIANCE part with that base item. So two appliance variants cannot share a base item, and when two do, load order decides which one is used. If nothing matches, the code debugmsgs and places `ap_standing_lamp`. `deploy_appliance_actor` has no finalize or check, so the error appears only at use time, and `info()` repeats the scan every time the description is shown. No two vanilla appliances share a base item today.

```cpp
for( const vpart_info &vpi : vehicles::parts::get_all() ) {
    if( vpi.base_item == item_id && vpi.has_flag( flag_APPLIANCE ) ) { return vpi.id; }
}
debugmsg( ... ); return vpart_ap_standing_lamp;
```

**Fix:** Add an optional `"vpart": "ap_wall_lamp"` to `deploy_appliance` and `appliance_convert`. Build an item → appliance map once in `vehicles::parts::finalize`, and validate it in `deploy_appliance_actor::finalize`.

### VINT-J13 · Auto-generated gun turrets override any JSON turret_<gun> and hardcode skills, time, durability and looks_like
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/veh_type.cpp:581-667`, `src/veh_type.cpp:553-579`, `src/veh_type.cpp:483-501`, `src/generic_factory.h:392-405`, `data/json/vehicleparts/turret.json:13-23`

`vehicles::parts::finalize` builds `turret_<itemid>` for every mountable gun from `turret_generic` and calls `insert()`, which silently replaces any JSON part with the same id. Skills, install time, foldability (PISTOL/SMG only) and tile `looks_like` ids all come from hardcoded formulas. Workaround: putting `NO_TURRET` on the gun skips generation, and a hand-written TURRET part with a different id is fully supported. The real defects are the silent-overwrite trap and the undocumented formula.

```cpp
new_part.id = vpart_id( "turret_" + item_id.str() );
int mechanics_req = 3 + static_cast<int>( item->weight / 10_kilogram );
time_duration install_time = 10_minutes * difficulty;
vpart_info_factory.insert( new_part );
```

**Fix:** Skip generation when `turret_<id>` is already defined in JSON, or merge the JSON part over the generated one. Optionally add a gun-side `"vehicle_turret": { "durability": 600, "install_skills": [...], "time": "2 h", "looks_like": "mounted_m2", "foldable": false }`, and move the formula constants into `turret_generic`.

### VINT-J14 · Install, repair and hotwire tuning numbers hardcoded (engine count, axle skill, jack cap, skill/helper time scaling)
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/veh_interact.cpp:852-917`, `src/veh_utils.cpp:183-191`, `src/veh_interact.cpp:110`, `src/veh_type.cpp:1096-1113`, `src/veh_type.cpp:183-205`, `src/vehicle_use.cpp:2394-2415`, `src/vehicle_use.cpp:483-485`, `src/vehicle_use.cpp:1722`

These numbers are fixed in C++, and overhaul or realism mods would want to tune them:
- More than 2 engines needs `dif_eng/2 + 8` mechanics, and this logic is duplicated in veh_utils.
- Extra steering axles need `axles + 5`.
- The jack cap is 8500 kg.
- Install time scales by 10% per excess skill level (capped at 50%) and -10% per helper, with at most 3 helpers. Requirements cannot use proficiencies.
- Hotwiring takes `6000 s / mechanics`.
- The alarm-smash odds are 70/(1+skill) and (skill+3)×10.
- FURNITURE_LIFT_ASSIST adds +5 str.

```cpp
dif_eng = dif_eng / 2 + 8;
dif_steering = axles.size() + 5;
const time_duration required_time = 6000_seconds / skill;
```

**Fix:** Add vpart fields (`"max_per_vehicle": 2`, `"extra_install_skills": [["mechanics", 8]]`, `"lift_assist_str": 5`) and a global `vehicle_settings` JSON object for the jack cap, time scaling and hotwire rules. Allow `proficiencies` with time multipliers in `requirements.<action>`, as recipes do.

### VINT-J15 · Autodrive ignores JSON terrain and wheel data: no avoid/allow terrain flag, rails and wheel_terrain_modifiers unused, speed caps fixed
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/vehicle_autodrive.cpp:740-836`, `src/vehicle_autodrive.cpp:160-164`, `src/vehicle_autodrive.cpp:973-993`, `src/overmapbuffer.cpp:1013-1034`, `src/vehicle.cpp:4122-4138`

`check_drivable` relies on heuristics: `movecost == 2`, NOCOLLIDE, any item on the tile, furniture movecost, LIQUID and a hardcoded `t_open_air`. A modder cannot mark terrain as autodrive-safe or autodrive-unsafe except through movecost or NOCOLLIDE (which has collision side effects); visible traps are already avoided. Rails and `wheel_terrain_modifiers` are never consulted, and overmap routing has no railroad cost, so rail vehicles get routed over open land. The speed caps (3/10/16 tiles/s) and `max_steer = 1` (marked TODO) are constants.

```cpp
} else if( terrain_type.movecost == 2 || terrain_type.has_flag( ter_furn_flag::TFLAG_NOCOLLIDE ) ) {
if( here.has_items( pt ) ) { return false; }
```

**Fix:** Add ter/furn flags `AUTODRIVE_AVOID` and `AUTODRIVE_OK`, and consult the vehicle's wheel terrain modifiers (or `can_use_rails`) in `check_drivable`. Make the speed and steer caps data. The AUTOPILOT part uses a different code path (`drive_to_local_target`), so put these settings on the autodrive activity, not on that part.

## 2. Bugs, oversights & quirks

### VINT-B01 · Filtering the door-control menu to zero matches indexes items[-1] (UB/crash)
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/veh_utils.cpp:456`, `src/veh_utils.cpp:471`, `src/veh_utils.cpp:509`, `src/uilist.cpp:571`, `src/uilist.cpp:585`, `src/vehicle_use.cpp:226`, `data/raw/keybindings.json:52`

`veh_menu::query()` attaches `veh_menu_cb` when every entry has a location, which is the case for "Control doors and curtains". When the `/` filter matches nothing, `uilist::filterlist()` sets `selected = -1` and calls `callback->select()`. `veh_menu_cb::select` then runs `on_select()` unconditionally, and that reads `items[menu.selected]._on_select`. This is an out-of-bounds read of a `std::function`, which may then be called. To reproduce: take any vehicle with a door motor, examine it, choose Control doors, press `/` and type `zzz`.

```cpp
if( fentries.empty() ) { selected = -1; }                     // uilist.cpp:571
if( callback != nullptr ) { callback->select( this ); }       // uilist.cpp:585
cb.on_select = [this, &menu]() {
    if( items[menu.selected]._on_select ) { items[menu.selected]._on_select(); } };
```

**Fix:** In the `on_select` lambda (or in `veh_menu_cb::select`), return early unless `0 <= menu.selected < items.size()`.

### VINT-B02 · "Control individual engines" sets engine_on directly, skipping every engine start check
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle_use.cpp:439`, `src/vehicle_use.cpp:442`, `src/vehicle_use.cpp:452`, `src/vehicle_use.cpp:458`, `src/vehicle_use.cpp:825`, `src/vehicle_use.cpp:860`, `src/vehicle_use.cpp:2512`, `src/activity_actor.cpp:12640`, `src/vehicle.h:535`

After the menu closes, `control_engines()` does `engine_on |= vp.enabled` for every engine, and engines are enabled by default. On a stopped vehicle, just switching a multi-fuel engine's fuel sets `engine_on = true`. It then calls `start_engines( here )` with no driver, so no `start_engines_activity_actor` runs. The engine is now running without ever passing `vehicle::start_engine()`. That skips the immobilizer, the BAD_STARTER and BAD_FUEL_PUMP faults, the starter battery drain, cold starts and the damaged-engine failure chance. The reverse case is also broken: disabling every engine leaves `engine_on` true, and `start_engines()` then re-enables every unbroken engine, so you cannot turn all engines off from this menu.

```cpp
for( const int p : engines ) { ... engine_on |= vp.enabled; }          // :440-443
if( engine_on ) { start_engines( here ); }                            // :458
if( !autodrive && driver ) { driver->assign_activity( ... ) }         // :860, driver == nullptr
```

**Fix:** Save `engine_on` before the menu. Afterwards set it to `was_on && any engine enabled`, and never turn it from false to true. If a newly enabled engine needs starting, call `start_engines( here, &player_character )` so the start activity runs. If no engine is enabled, call `stop_engines()`.

### VINT-B03 · Autopilot Follow/Patrol never start a stopped engine, so the autopilot silently does nothing
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle_use.cpp:537`, `src/vehicle_use.cpp:872`, `src/vehicle_use.cpp:860`, `src/vehicle.cpp:6754`, `src/vehicle_move.cpp:595`

Both the "Follow…" handler and `enable_patrol()` call `start_engines( here )` without a driver. That queues no start activity and never sets `engine_on`. `vehicle::thrust()` then zeroes cruise velocity every turn without a message. The vehicle never moves and gives no feedback. "Stop…" turns the engines off, so Stop followed by Follow always ends in this state.

```cpp
autopilot_on = true; is_following = true; ... if( !engine_on ) { start_engines( here ); }
// vehicle_move.cpp:595
if( total_power( here ) <= 0_W || !engine_on || ... ) { ... cruise_velocity = 0; return; }
```

**Fix:** Pass `&get_player_character()` as the driver (with `take_control = false`). The menu entry is only offered at the controls, so the player is present. Alternatively, start each enabled engine via `start_engine()`, set `engine_on` from the result and show a message on failure.

### VINT-B04 · use_nl_boiler erases colony elements inside a range-for (use-after-free)
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle_use.cpp:1632`, `src/vehicle_use.cpp:1634`, `src/vehicle.cpp:6574`, `src/third-party/plf/colony.h:2633`

The Netherum boiler iterates `for( item &it : items )` and calls `remove_item( vp, &it )`, which erases from the underlying `cata::colony`. The range-for then increments the erased iterator. When an erase empties a colony group while others remain, plf::colony frees that group, so burning 9 or more items reads freed memory. Items may also be skipped or double-counted for `charge_battery`. This is niche, because the boiler works only in the Netherum safehouse dimension.

```cpp
for( item &it : items ) {
    total_weight += it.weight();
    remove_item( vp, &it );          // vp.items.erase( it )
}
```

**Fix:** Use the iterator form (`it = remove_item( vp, it )`), or sum the weights first and then clear the stack and call `invalidate_mass()`.

### VINT-B05 · Replacing a broken part skips install gates; NO_MODIFY_VEHICLE is documented but unregistered
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/veh_interact.cpp:1163`, `src/veh_interact.cpp:1186`, `src/veh_interact.cpp:826`, `src/veh_interact.cpp:2226`, `src/veh_utils.cpp:84`, `src/veh_utils.cpp:152`, `src/veh_type.cpp:842`

`do_repair` offers to replace a broken part after checking only its install requirements, jacking and whether it is carried. It skips the `NO_INSTALL_PLAYER` and `NO_MODIFY_VEHICLE` gates that apply to normal installs. `veh_utils::repair_part` then calls `install_part()` directly. As a result, helicopter rotors, military frames and wheels can be replaced bare-handed with just the base item, because rotors have no install requirements. Separately, `NO_MODIFY_VEHICLE` is documented in JSON_FLAGS.md but has no `json_flag` entry, so `vpart_info::check()` rejects it as unknown and the feature cannot be used.

```cpp
} else if( veh->has_part( "NO_MODIFY_VEHICLE" ) && !vp.has_flag( "SIMPLE_PART" ) ) {  // non-broken branch only
veh.remove_part( pt ); const int partnum = veh.install_part( here, mount, vpid, std::move( base ) );
```

**Fix:** Factor out a single `can_player_install( vpi )` predicate and apply it to the broken-replacement branch and to `most_repairable_part`. Register `NO_MODIFY_VEHICLE` in `vp_flags.json`.

### VINT-B06 · Repair/removal XP uses install skills and install time: no XP for repair-only skills, XP farming
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/veh_utils.cpp:46-66`, `src/veh_utils.cpp:145-147`, `src/activity_actor.cpp:12051`, `src/activity_actor.cpp:12209-12212`

`calc_xp_gain` looks the skill up only in `install_skills` and scales by `install_moves`. `repair_part` passes `repair_skills`, so skills used only for repair earn 0 XP. Examples are tailor for curtains and cloth roofs, electronics for weather stations, and mechanics for 46 appliance parts such as fridges and generators. Removal iterates `install_skills`, so `removal_skills` never give XP. For skills in both lists, XP ignores repair time: `foot_pedals` (60 m install, 4 m repair) gives about 60 XP per 4-minute repair, which can be farmed. The XP curve is also hardcoded.

```cpp
const auto iter = vp.install_skills.find( sk ); if( iter == vp.install_skills.end() ) { return 0; }
return std::ceil( to_moves<double>( vp.install_moves ) / ... );
for( const auto &sk : pt.is_broken() ? vp.install_skills : vp.repair_skills ) { who.practice( sk.first, calc_xp_gain( vp, sk.first, who ) ); }
```

**Fix:** Pass the skill map and duration of the action actually performed (install, repair or removal) into `calc_xp_gain`, and scale repair XP by repair time × levels repaired. Optionally add `"xp_multiplier"` to each `requirements.<action>` block.

### VINT-B07 · Inline vpart enchantment ids are built from the translated name and collide
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/veh_type.cpp:283-287`, `src/magic_enchantment.cpp:326-345`, `src/mod_tracker.h:75-81`, `src/translation.cpp:458-461`, `src/generic_factory.h:399-405`

Inline enchantment ids are `"INLINE_ENCH_" + name_ + "_" + n`, where `name_` is a translation, so the translated text is used. Most parts have no `"name"`, so they all get `INLINE_ENCH__0`. A copy-from child inherits `name_` and regenerates the parent's id. Within one mod this throws a "two definitions from the same source" load error; across mods the last one loaded silently overwrites the earlier one. Ids also differ by language. The `enchantments` vector is never cleared, so a child cannot drop inherited enchantments. The bug is latent: no vanilla vpart uses inline enchantments yet.

```cpp
std::string enchant_name = "INLINE_ENCH_" + name_ + "_" + std::to_string( enchant_num++ );
// mutation_data.cpp:445 uses id.str() instead
```

**Fix:** Use `"INLINE_ENCH_vpart_" + id.str() + "_" + n`. Clear the list when `enchantments` is present, and support `extend` / `delete`.

### VINT-B08 · copy-from for vparts and vehicle prototypes is lossy and contradicts the docs (requirements, parts, items, zones)
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/veh_type.cpp:180-211`, `src/veh_type.cpp:332-344`, `src/veh_type.cpp:1093`, `src/veh_type.cpp:1340-1362`, `src/veh_type.h:549-551`, `src/generic_factory.h:999-1008`, `src/generic_factory.h:1574`, `src/flexbuffer_json-inl.h:387`, `doc/JSON/VEHICLES_JSON.md:24`, `data/json/vehicleparts/manual.json:68`, `data/mods/Xedra_Evolved/vehicles/vehicle_parts.json:130`, `data/json/vehicleparts/rams.json:115`, `data/json/furniture_and_terrain/appliances.json:862`

VEHICLES_JSON.md says copy-from appends `parts`, `items` and `zones`. In the code:
- **Items (live vanilla bug).** Only `parts` is appended. `items` goes through plain `optional()`, which clears the vector, and has no `extend`. Vanilla cars such as `car_sedan`, `car_coupe*`, `car_super_*`, `car_shootingbrake_*` and `motorcycle_sidecart` define short `items` lists, so they silently lose their parents' `car_misc`, `car_kit` and `spare_tire_kit` spawns.
- **Zones.** `zones` is also replaced; `extend` works but `delete` is broken. `zone_def::operator==` compares `filter == other.name`.
- **Parts.** Prototype `parts` can only be appended and cannot be deleted.
- **Pseudo tools.** `"pseudo_tools": []` cannot clear inherited tools.
- **Requirement blocks.** `parse_vp_reqs` replaces each block as a whole and reads skills with `was_loaded = false`. A child that sets only `"install": { "time": ... }` drops the parent's tools, components and skills. A repair block with only time and skills (`hand_paddles`, Xedra's `floral_controls`) makes `is_repairable()` return false, so the part becomes unrepairable without warning.

Caveat (plausible part): vanilla relies on whole-block replacement in places. `rams.json` uses `"repair": {}`, and about 20 appliances use `"removal": { "time": ... }` precisely to drop the parent's weld-removal requirements. Making requirement sub-fields inherit independently would need those files updated.

```cpp
} else { reqs.clear(); ... requirement_data::load_requirement( src, req_id ); reqs.emplace_back( req_id, 1 ); }
parts.insert( parts.end(), tmp.begin(), tmp.end() );                       // parts: append
optional( jo, was_loaded, "items", item_spawns );                          // items: replace
return zone_type == other.zone_type && name == other.name && filter == other.name && pt == other.pt;
```

**Fix:** Append `items` (and `zones`) as documented, or fix the doc; either way support `"extend"` / `"delete"`, e.g. `"delete": { "parts": [ { "x": 0, "y": 1, "part": "stereo" } ] }`. Fix `operator==` to compare `filter == other.filter`. Treat `pseudo_tools: []` as a clear. For requirement blocks, document the whole-block semantics, or opt into per-key inheritance (`extend` / `delete` for skills and components, `relative` for time) and migrate the data that relies on replacement. Add a `check()` warning for parts that have repair skills or time but empty repair requirements.

### VINT-B09 · vehicle_group / placement / spawn are append-only, have no delete or nesting, and an empty group null-derefs on pick()
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/vehicle_group.cpp:28`, `src/vehicle_group.cpp:80-87`, `src/vehicle_group.cpp:105-113`, `src/vehicle_group.cpp:129-141`, `src/vehicle_group.cpp:186-204`, `src/vehicle_group.h:41`, `src/weighted_list.h:133`, `src/veh_type.cpp:1342`, `src/mapgen.cpp:2749`, `src/generic_factory.h:271-283`, `doc/JSON/JSON_INFO.md:3048-3049`, `src/enums.h:77-83`

`VehicleGroup::load`, `VehiclePlacement::load` and `VehicleSpawn::load` all append to `map[id]`. A mod cannot remove or reweight vanilla entries; for example, a "no police cars" mod is impossible. Groups cannot reference other groups, so lists get copy-pasted. Every prototype, including abstract ones, also injects an implicit group with weight 100. `VehicleGroup::pick()` dereferences `weighted_list::pick()`, which returns nullptr for an empty or zero-weight group. An invalid group id resolves to a static empty dummy and then crashes the same way after the debugmsg. `VehicleSpawn` checks for null; `VehicleGroup` does not. `fuel` and `status` use mandatory `get_int`, while JSON_INFO documents a default of -1. `status` is cast to the enum unvalidated, and the PRISTINE=2 value is undocumented.

```cpp
VehicleGroup &group = vgroups[vgroup_id( jo.get_string( "id" ) )];
for( JsonArray pair : jo.get_array( "vehicles" ) ) { group.add_vehicle( ... ); }
const vproto_id &pick() const { return *vehicles.pick(); }      // nullptr when empty
fuel( jo.get_int( "fuel" ) ), status( jo.get_int( "status" ) )
```

**Fix:** Move these types to generic_factory with `copy-from`, `"extend": { "vehicles": [...] }` and `"delete": { "vehicles": ["car"] }`, and allow nested `{ "group": "city_parked", "weight": 500 }` entries. Aftershock Exoplanet relies on the current append behavior and would need updating. Make `pick()` return an optional so callers can skip spawning. Make `fuel` and `status` optional with the documented defaults, and accept status as an enum string. Skip the implicit group for abstract prototypes, and add a finalize check for ids and a total weight > 0.

### VINT-B10 · check() gaps let bad vpart JSON crash or misreport (battery part without a magazine base, fuel_options, categories, messages)
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/veh_interact.cpp:2886-2892`, `src/veh_interact.cpp:495-499`, `src/veh_utils.cpp:203`, `src/veh_type.cpp:752-753`, `src/veh_type.cpp:856-857`, `src/veh_type.cpp:405`, `src/veh_type.cpp:273`, `data/json/vehicleparts/utilities.json:123`

`veh_interact::display_details` treats any non-engine, non-alternator part with `fuel_type` battery as a battery and dereferences `find_type( base_item )->magazine` without a null check. `veh_mobile_weather_station` and its variants pair `fuel_type: battery` with a TOOL base item that has no magazine slot. They avoid the crash only because they are NO_INSTALL_HIDDEN and their appliance copies use a different UI. A modded electric gadget defined the same way crashes the install or remove screen. `check()` doesn't validate this pairing, engine `fuel_options`, `categories` (a typo hides the part from every tab), or the EOC_ACTIVATION pairing (VINT-J03). The undefined-fuel and undefined-default_ammo messages print `base_item` instead of the bad id.

```cpp
const cata::value_ptr<islot_magazine> &battery = item::find_type( part->base_item )->magazine;
... battery->capacity );
debugmsg( "vehicle part %s uses undefined fuel %s", id.c_str(), base_item.c_str() );
```

**Fix:** Test `VPFLAG_BATTERY` and null-check the pointer in the UI. In `check()`, require a magazine slot for battery-fuelled non-engine parts and validate `fuel_opts` and `categories`. Print `fuel_type` and `default_ammo` in the messages.

### VINT-B11 · most_repairable_part never returns broken parts: its broken-part branch is unreachable
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/veh_utils.cpp:79`, `src/veh_utils.cpp:85`, `src/vehicle_part.cpp:191`, `src/veh_interact.cpp:2533`, `src/veh_interact.cpp:1114`

The loop skips parts where `!vp.is_repairable()` before testing `is_broken()`. `vehicle_part::is_repairable()` begins with `!is_broken()`, so every broken part is skipped and the `vp_broken` branch is dead code. The stats panel therefore never lists a replaceable broken part under "Needs repair", and labels it "can't repair" even when the player has the tools.

```cpp
if( vp.removed || !vp.is_repairable() || ... ) { continue; }
if( vp.is_broken() ) { ... vp_broken = &vp; ... }        // unreachable
// vehicle_part.cpp:193
return !is_broken() && base.repairable_levels() > 0 && info().is_repairable();
```

**Fix:** Check `is_broken()` first (subject to the install gates in VINT-B05), and apply the `!is_repairable()` filter only to unbroken parts. `do_repair` (veh_interact.cpp:1114) re-checks `is_repairable()` and needs the same change.

### VINT-B12 · Rotorcraft airworthiness lost forever when the player confirms and then backs out
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/veh_interact.cpp:1048`, `src/veh_interact.cpp:1057`, `src/veh_interact.cpp:1217`, `src/veh_interact.cpp:1229`, `src/veh_interact.cpp:1937`, `src/veh_interact.cpp:208`, `src/vehicle.cpp:4846`, `src/vehicle.cpp:8311`

Install, repair and removal call `veh->set_flyable( false )` as soon as the player confirms the "no longer flightworthy" prompt, before any work happens. If the repair or removal activity is then interrupted or cancelled, the part is untouched but the aircraft stays grounded. Nothing ever sets `flyable` back to true. The install back-out path (a later "unfoldable" prompt) can't happen in vanilla, because rotorcraft are never foldable.

```cpp
if( query_yn( ...flightworthy... ) ) { veh->set_flyable( false ); }
// grep: no set_flyable( true ) anywhere
```

**Fix:** Don't change state in the UI. Pass a `breaks_airworthiness` flag to `vehicle_activity_actor`, and call `set_flyable( false )` in `complete_vehicle()` only when the action succeeds.

### VINT-B13 · Autodrive: uninitialized in_greedy_mode, speed pinned at 1 tile/s, wrong "skidding" abort text
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle_autodrive.cpp:414`, `src/vehicle_autodrive.cpp:1181`, `src/vehicle_autodrive.cpp:1316`, `src/vehicle_autodrive.cpp:1349`, `src/vehicle_autodrive.cpp:1268`, `src/vehicle_autodrive.cpp:1335`, `src/vehicle_autodrive.cpp:967`, `src/vehicle_autodrive.cpp:1452`

1. `bool in_greedy_mode;` is read before it is first assigned. This is formal UB, but has no visible effect, because the path is empty on the first call.
2. `reduce_speed` or an off-course reset sets `max_cautious_speed_tps = 1`, and only `precompute_data` raises it again, at the next OMT boundary. A single item or unseen tile therefore makes autodrive crawl at about 4 mph for the rest of the OMT.
3. `do_autodrive` aborts with "You lose control as the vehicle starts skidding." whenever `player_in_control()` fails, including when the controls break mid-drive.

```cpp
bool in_greedy_mode;                                     // :414, ctor only clears data
data.max_cautious_speed_tps = MIN_SPEED_TPS;             // :1268
if( !player_in_control( here, driver ) || skidding ) { ... _( "You lose control as the vehicle starts skidding." )
```

**Fix:** Initialize `in_greedy_mode = false`. Restore the cautious speed from `safe_velocity` after a few clean ticks instead of waiting for the OMT boundary. Use a separate abort message for each cause.

### VINT-B14 · Unload-fuel menu skips low plutonium entries but indexes the unfiltered list
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/veh_interact.cpp:2994`, `src/veh_interact.cpp:3005`, `src/veh_interact.cpp:3012`, `src/uilist.cpp:624`

`act_vehicle_unload_fuel` skips `plut_cell` entries with fewer than `PLUTONIUM_CHARGES` when building the menu, but resolves the choice as `fuels[smenu.ret]`. Every entry after the skipped one maps to the wrong fuel, and the last fuel can never be chosen. It is reachable only with Aftershock's minireactor plus a second solid fuel. A mismapped plutonium pick then hits the "no charged cells" early return and unloads nothing.

```cpp
for( auto &fuel : fuels ) {
    if( fuel == itype_plut_cell && veh->fuel_left( here, fuel ) < PLUTONIUM_CHARGES ) { continue; }
    smenu.addentry( item::nname( fuel ) );
}
fuel = fuels[smenu.ret];
```

**Fix:** Filter `fuels` before building the menu, or add entries with explicit retvals (`smenu.addentry( i, true, -1, name )`).

### VINT-B15 · Appliance menu calls keys_bound_to(...).front() on possibly empty vectors
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/veh_appliance.cpp:639`, `src/veh_appliance.cpp:663`, `src/veh_appliance.cpp:691`, `src/veh_appliance.cpp:173`, `src/input_context.cpp:246`, `src/veh_utils.cpp:284`

`populate_app_actions` passes `ctxt.keys_bound_to( "REFILL" ).front()` (and does the same for SIPHON, RENAME, REMOVE, PLUG, TOGGLE_CONNECTION_VISIBILITY and DISCONNECT_GRID) to `addentry`. If the player unbinds one of these actions, or binds it only to a non-printable key, the vector is empty and examining any appliance is UB. PLUG is also never registered on the context, so it cannot be rebound from the help screen. `veh_utils`' `veh_keybind()` already handles the empty case.

```cpp
imenu.addentry( -1, can_refill( ), ctxt.keys_bound_to( "REFILL" ).front(), ...
```

**Fix:** Use a helper that returns the first bound event or a null `input_event` (as `veh_keybind` does), and register PLUG.

### VINT-B16 · Inconsistent rules: dishwasher hardcodes "detergent"; vehicle refill instant vs appliance 5 min; inverted W/kW test
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle_use.cpp:1465`, `src/vehicle_use.cpp:1549`, `src/vehicle_use.cpp:1594`, `src/veh_interact.cpp:184`, `src/veh_appliance.cpp:491`, `src/veh_interact.cpp:1366`, `src/activity_actor.cpp:12055`

- The washing machine accepts any item with the DETERGENT flag. The dishwasher checks and consumes only `itype_detergent`, so vanilla `soap_flakes` and `liquid_soap` work in one appliance and not the other. Its message also refers to "5 charges" of an item that has no charges.
- A tank refill started from veh_interact takes 0 seconds, because the time switch has no VEHICLE_REFILL case. The same refill from the appliance UI takes a flat 5 minutes.
- The battery header test `epower < 10_kW || epower > 10_kW` prints watts for every value except exactly 10 kW.

```cpp
it.has_flag( json_flag_DETERGENT ) && ...                                   // washer, :1465
player_character.crafting_inventory().has_amount( itype_detergent, 5 );     // dishwasher, :1549
vehicle_activity_actor( VEHICLE_REFILL, 5_minutes, ...                       // veh_appliance.cpp:491
if( epower < 10_kW || epower > 10_kW ) {                                    // veh_interact.cpp:1366
```

**Fix:** Share the detergent selection between the washer and the dishwasher. Compute refill time from the transferred volume in both UIs. Use `epower > -10_kW && epower < 10_kW` for the watts branch.

## 3. Performance

### VINT-P01 · form_inventory prepares every pseudo-tool twice per vehicle tile, scanning all parts each time
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle_use.cpp:1928`, `src/vehicle_use.cpp:1943`, `src/vehicle.cpp:2894`, `src/vehicle.cpp:2916`, `src/vehicle.cpp:6597`, `src/vehicle_use.cpp:2205`, `src/vehicle_use.cpp:2186`, `src/vehicle.cpp:6131`, `src/veh_type.cpp:1135`, `src/temp_crafting_inventory.cpp:449`, `src/crafting.cpp:695`

`temp_crafting_inventory::form_from_map` calls `form_inventory` for every vehicle tile in range. Its cache is keyed on moves, turn and position, so it rebuilds after almost every action, and NPC zone activities rebuild it for each candidate tile. Each tile probes `part_with_unreserved_tool( water_faucet )` before the "vehicle already seen" check, and then calls `get_tools_with_sources`. Both use the uncached `parts_at_relative( ..., false )` full-vehicle scan, and both call `prepare_tools`. That function copies the `pseudo_tools` set by value. For each tool it then builds items and magazines and runs `battery_left`, which does a `search_connected_vehicles` BFS. So every tool is prepared twice per tile. The reviewer estimates sub-ms to about 1 ms per rebuild on merged appliance grids: real waste, but probably not dominant.

```cpp
const std::optional<vpart_reference> vp_faucet = part_with_unreserved_tool( here, itype_water_faucet );
if( vp_faucet ) { if( veh.find( &vehicle() ) == veh.end() ) { ...
for( const vpart_tool_source &src_tool : get_tools_with_sources( here ) ) {
std::set<std::pair<itype_id, int>> vpart_info::get_pseudo_tools() const { return pseudo_tools; }
```

**Fix:** Call `get_tools_with_sources` once, and find the faucet in its result. Check `veh.find` first. Use the cached `parts_at_relative( mount_pos(), true )`. Return the tool sets by const reference. Compute `battery_left` / `fuel_left` once per vehicle per rebuild and pass the result into `prepare_tool`.

### VINT-P02 · veh_interact recomputes most-repairable part (can_make per damaged part) on every redraw
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/veh_interact.cpp:532`, `src/veh_interact.cpp:2532`, `src/veh_utils.cpp:68`, `src/veh_interact.cpp:2416`, `src/veh_interact.cpp:2651`, `src/veh_interact.cpp:667`

Every keypress in the vehicle editor force-redraws, which runs `display_stats()` → `most_repairable_part`. For each damaged part, that builds the repair requirement (twice) and runs `can_make_with_inventory`. On a wreck with 100+ damaged parts, that is 100+ requirement builds and inventory checks per cursor move. The same redraw sums the volume of every cargo item, and `display_mode()` calls `cant_do()` 10 times, several of which are O(parts) scans. None of these values can change while the UI waits for input.

```cpp
while( !finish ) { calc_overview( here ); ui_manager::redraw();
vehicle_part *most_repairable = get_most_repairable_part();
const requirement_data reqs = info.repair_requirements() * vp.get_base().repairable_levels();
if( reqs.can_make_with_inventory( &who, inv, is_crafting_component ) )
```

**Fix:** Compute the most damaged part, the most repairable part, the cargo totals and the `cant_do` results once (next to `count_durability()`). Recompute only when the inventory is refreshed or `cpart` changes, and have the draw functions read the stored values.

### VINT-P03 · Every veh_interact construction ranks all ~1,100 vparts, even for a simple tank picker
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/veh_interact.cpp:322`, `src/veh_interact.cpp:628`, `src/veh_interact.cpp:634`, `src/veh_utils.cpp:195`, `src/veh_utils.cpp:213`, `src/veh_interact.cpp:279`, `src/handle_liquid.cpp:526`, `src/veh_interact.cpp:2961`, `src/vehicle_use.cpp:2837`, `src/veh_interact.cpp:983`, `src/veh_interact.cpp:1004`, `src/localized_comparator.cpp:48`

The constructor always runs `cache_tool_availability()` → `list_install_candidates()` over every vpart: 507 JSON parts plus about 600 generated turrets. For each part it builds the install requirement and runs `can_make_with_inventory`. It then sorts both lists with a comparator that calls `name()` (translation, a string flag lookup and `string_format`) and constructs a `std::locale` for every comparison, about 20k `name()` calls in total. `veh_interact::select_part` pays the same cost when it only needs the part overview. It is used when pouring liquid into a vehicle with 2+ tanks (a common refuelling action), when siphoning and when purifying water. In the install filter tab, every keypress re-runs `lcmatch( p->name(), filter )` over all candidates.

```cpp
count_durability(); cache_tool_availability();                                  // ctor
install_options = veh_utils::list_install_candidates( player_character, *crafting_inv, *veh );
return localized_compare( a->name(), b->name() );                               // sort comparator
veh_interact vehint( here, const_cast<vehicle &>( veh ) );                      // select_part
```

**Fix:** Compute `install_options` lazily on the first entry to `do_install()`, and skip it entirely for `select_part`. Sort on precomputed (name, ptr) pairs, and reuse the lower-cased names for the filter.

### VINT-P04 · vpart flags are std::set<std::string>; per-turn and per-move loops use string lookups
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/veh_type.h:292`, `src/veh_type.h:358`, `src/veh_type.cpp:970`, `src/vehicle.cpp:6327`, `src/vehicle.cpp:3144`, `src/vehicle.cpp:6362`, `src/vehicle.cpp:3090`, `src/map.cpp:3116`, `src/monmove.cpp:190`, `src/vehicle_use.cpp:2706`, `src/map.cpp:1041`

Only the 64 `vpart_bitflags` are O(1); every other `has_flag` call is a string-set lookup. Hot callers:
- `vehicle::idle` runs every turn for every vehicle, including lamps and wires. It calls `has_parts({"STEREO","CHIMES","CRASH_TERRAIN_AROUND"})`, which allocates a vector of strings and scans every part. The early exit never fires, because no data uses CRASH_TERRAIN_AROUND.
- `on_move` does the same with 4 flags, and the scoop, planter, reaper and transform passes then rescan.
- `vpart_info::name()` checks "TURRET" by string.
- The string overload of `part_with_feature( point, ... )` ignores the `relative_parts` cache.
- `map::has_vehicle_floor` uses that string path for BOARDABLE and OBSTACLE even though VPFLAGs exist, so every monster step onto a vehicle tile does 2 full-vehicle scans.
- `build_interact_menu` makes about 16 string scans, several for flags that already have VPFLAGs.

This is sub-ms per turn in typical bubbles, but it is spread everywhere.

```cpp
bool has_flag( const std::string &flag ) const { return flags.count( flag ) != 0; }
tiny_bitset flags = has_parts( { "STEREO", "CHIMES", "CRASH_TERRAIN_AROUND" }, true );
for( const int p : parts_at_relative( pt, /* use_cache = */ false, include_fake ) )
return veh_at( p ).part_with_feature( "BOARDABLE", false ) || veh_at( p ).part_with_feature( "OBSTACLE", false );
```

**Fix:** Intern vpart flags at finalize into a dynamic bitset indexed by `json_flag` id, and add a `has_flag( const flag_id & )` overload. Quick wins:
- Switch the calls that already have VPFLAGs (vehicle_use.cpp:2706-2747, `has_vehicle_floor`) to the bitflag overloads.
- Add VPFLAGs for TURRET, SCOOP, PLANTER, REAPER, TRANSFORM_TERRAIN, STEREO, CHIMES, BEEPER, CRASH_TERRAIN_AROUND and NO_REPAIR.
- Make the string `part_with_feature` use the cache.
- Pass `has_parts` a static array instead of a vector of strings.

### VINT-P05 · Autodrive re-derives 24 profiles and an O(24×1152×hull) validity map at each OMT; rotor squares inflate it
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/vehicle_autodrive.cpp:899`, `src/vehicle_autodrive.cpp:706`, `src/vehicle_autodrive.cpp:994`, `src/vehicle_autodrive.cpp:838`, `src/vehicle_autodrive.cpp:862`, `src/vehicle_autodrive.cpp:1128`, `src/map_iterator.h:171`

`precompute_data` runs on every OMT change, which is every 1.5-2.4 turns at autodrive speeds. Each run:
- (a) recomputes all 24 vehicle profiles, each a `std::map` plus an `unordered_set`, even though the vehicle's shape hasn't changed.
- (b) runs `compute_valid_positions` over 24 facings × 48×24 nav points × the hull. The inner loop redoes rotations that do not depend on the loop variables, and it exits early only on obstacles, so it runs in full over open terrain.
- (c) for rotorcraft, adds a full (2r+1)² square of points per rotor with no dedupe (289 points for the Apache rotor), reaching about 8-16M inner iterations per OMT while flying, i.e. tens of ms.
- (d) for land vehicles, inserts all 6,912 view tiles into an `unordered_set` for the ramp flood fill.

```cpp
for( orientation dir : all_orientations() ) { data.profile( dir ) = compute_profile( here, dir ); }
for( tripoint_rel_ms pt : points_in_radius( pos, radius ) ) { ret.occupied_zone.emplace_back( pt.xy().raw() );
const point view_pt = data.nav_to_view.transform( nav_pt ) + veh_rot.transform( veh_pt.raw() ) - veh_rot.transform( point::zero );
ramp_points.visited.emplace( p );
```

**Fix:** Dedupe `occupied_zone` and use `points_in_radius_circ` for rotors. Precompute the rotated offsets once per facing, and consider per-row prefix sums of obstacles. Cache the profiles until the vehicle's part set changes. Replace the visited set with a bitmap, and `reserve()` the node map.

### VINT-P06 · vpart_info install/repair/removal requirements and is_repairable rebuilt per call
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/veh_type.cpp:1071`, `src/veh_type.cpp:1081`, `src/veh_type.cpp:1086`, `src/veh_type.cpp:1091`, `src/requirements.h:254`, `src/requirements.cpp:436`, `src/requirements.cpp:492`, `src/veh_utils.cpp:80`, `src/veh_interact.cpp:2227`, `src/activity_item_handling.cpp:2175`

`install_requirements()`, `removal_requirements()` and `repair_requirements()` each build a new `requirement_data` with `std::accumulate`, copying through `operator*` and `operator+` (the latter with an O(n²) quality merge). `is_repairable()` builds the whole repair requirement just to call `is_empty()`. These are called per part in the loops behind VINT-P02 and VINT-P03, in `move_cursor` and in NPC repair scans, yet the result never changes after finalization. Each build costs only microseconds, so this mainly adds to P02 and P03.

```cpp
requirement_data vpart_info::install_requirements() const { return requirement_data( install_reqs ); }
bool vpart_info::is_repairable() const { return !has_flag( "NO_REPAIR" ) && !repair_requirements().is_empty(); }
```

**Fix:** Cache the summed requirements per `vpart_info` (fill them lazily or in a post-requirements finalize pass, and clear them in reset), return them by `const &`, and precompute a `repairable_` bool.

### VINT-P07 · build_interact_menu re-prepares the tile's tool set 4-5x and copies a whole vpart_info
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle_use.cpp:2635`, `src/vehicle_use.cpp:2636`, `src/vehicle_use.cpp:2682`, `src/vehicle_use.cpp:2693`, `src/vehicle_use.cpp:2777`, `src/vehicle_use.cpp:2825`, `src/vehicle_use.cpp:3023`, `src/veh_appliance.cpp:697`, `src/vehicle.cpp:2876`, `src/vehicle.cpp:2931`, `src/vehicle.cpp:6597`

On every vehicle examine, keep-open menu rebuild and appliance menu build, `build_interact_menu` calls `vp.get_tools()` once only to test `.empty()`, calls it again to list the tools, and calls `part_with_tool` twice (faucet, purifier). Each of these calls prepares every tool on the tile, including a grid BFS. Tools with firing requirements are prepared a fifth time. It also copies a full `vpart_info` by value just to read two fields. This is a cold path, costing a few ms at most on large grids.

```cpp
if( vp_toolstation && !vp.get_tools( *here ).empty() ) { const vpart_info vp_info = vp_toolstation->info();
for( const auto&[tool_item, hk] : vp.get_tools( *here ) ) {
if( vp.part_with_tool( *here, itype_water_faucet ) ) {
```

**Fix:** Compute `vp.get_tools( *here )` once and reuse it for the emptiness check, the tool entries and the faucet/purifier detection. Reuse the prepared items, and use `const vpart_info &`.

### VINT-P08 · finalize_prototypes builds two items per prototype part entry for a fuel check that rarely applies
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/veh_type.cpp:1618`

At load, `vehicles::finalize_prototypes` constructs `item( &*base )` and `item( pt.fuel )` and calls `can_contain` for every part entry of every prototype: 17,078 entries in core data, of which only 301 specify a fuel. When `pt.fuel` is null, neither debugmsg can fire. That makes about 34k pointless item constructions per world load, tens of ms.

```cpp
if( item( &*base ).can_contain( item( pt.fuel ) ).success() || base->magazine ) {
    if( !item::type_is_defined( pt.fuel ) ) { debugmsg( ... ) }
} else { if( !pt.fuel.is_null() ) { debugmsg( ... ) } }
```

**Fix:** Wrap the block in `if( !pt.fuel.is_null() )`. This does not change behavior.

## Appendix: considered and rejected

- Appliance interaction UI has no theft/ownership or lock checks – Only `place_appliance()` (or splitting an existing appliance, which copies the owner) creates APPLIANCE-tagged vehicles. It always assigns the player's or a follower's faction, mapgen never creates appliances, and appliances have no security systems, so no non-player-owned appliance can exist.
- crash_terrain_around is O(8 × parts) per crash part per turn – No data in core, mods or docs uses CRASH_TERRAIN_AROUND, so this code never runs. Even if a mod used it, the cost would be microseconds. The real per-turn cost is the string-flag scan covered in VINT-P04.
