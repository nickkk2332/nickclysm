# 23 · Vehicle core & movement

> Engine audit – static code reading at commit `a1a646d6`; nothing was compiled or run. Each finding was checked against the code by a separate reviewer: **confirmed** = verified by reading the code; **plausible** = consistent with the code, but reachability or impact not proven. Line numbers refer to that commit.

**Scope:** `src/vehicle.cpp`, `src/vehicle_move.cpp`, `src/vehicle_part.cpp`, `src/vehicle_display.cpp`, `src/power_network.cpp`, `src/tileray.cpp`, `src/veh_shape.cpp`, `src/vehicle_part_location.cpp`

## Summary

The vehicle core works, but much of its behaviour is keyed to hardcoded ids and flags instead of data: pseudo-fuel ids, fault ids, rail terrain ids, proficiencies, and a single overloaded `bonus` field. EOCs can see very little of a vehicle and can change almost nothing. There are two high-severity latent bugs: a documented flag (`UNMOUNT_ON_MOVE`) that crashes the game, and fuel-tank explosions that never happen. Several medium bugs affect movement physics (reverse handling, integer acceleration, towing brakes, fuel debt). On performance, the main costs are allocations and full-part scans on hot paths: per-step collision, move-cost queries, per-turn `refresh()` for autopilot vehicles, and battery-grid settlement.

| Lens | Critical | High | Medium | Low | Total |
|---|---|---|---|---|---|
| Missing JSON / EOC | 0 | 1 | 10 | 7 | 18 |
| Bugs & quirks | 0 | 2 | 12 | 5 | 19 |
| Performance | 0 | 1 | 4 | 8 | 13 |
| **Total** | 0 | 4 | 26 | 20 | 50 |

**Fix first:**
1. **VEH-B01** · UNMOUNT_ON_MOVE parts crash the game: the fix is one argument, and the crash is a null dereference on a documented flag.
2. **VEH-B02** · Fuel tanks never explode: a one-line change brings back the existing explosion content for every vanilla tank.
3. **VEH-P01** · part_collision() looks up the driver on every call: a small change cuts per-step collision cost for every moving vehicle.
4. **VEH-B10** · Reversing makes handling difficulty negative: `std::abs` removes an easy-to-reach exploit.
5. **VEH-P03** · drive_to_local_target() refreshes every turn: deleting one call removes an O(P²) rebuild per turn and also fixes the smart-controller reset in VEH-B18.

## 1. Missing JSON bindings & EOC features

### VEH-J01 · Vehicle talker is nearly read-only and there are no vehicle math functions
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/talker_vehicle.h:44`, `src/talker_vehicle.cpp:104`, `src/talker_vehicle.cpp:175`, `src/vehicle.cpp:324`, `src/condition.cpp:2410`, `src/npctalk.cpp:6974`, `src/math_parser_diag.cpp:1744`, `src/math_parser_diag.cpp:1878`, `src/npctalk.cpp:8356`

EOCs can reach a vehicle through `u_run_vehicle_eocs`, the `vehicle_moves` event, vehicle spells and enchantment conditions. Once there, the vehicle talker exposes only booleans (flying, floating, skidding, on rails, has_part_flag) and five legacy `u_val` numbers (vehicle_facing, current_speed, unloaded_weight, friendly and hostile passenger counts). It can write only vars and effects, plus `u_teleport`. No math function takes a vehicle: `weight()` throws for a vehicle talker. EOCs cannot read fuel or battery level, engine state, cruise velocity, part HP, owner, lock state, power or max speed, and cannot change any of them. There are also correctness gaps. `is_driven` checks only the avatar, so it is false for autopilot, follow and patrol driving. `u_is_remote_controlled` calls `vehicle::remote_controlled()`, which has a side effect: it prints "Lost connection" and clears `g->remoteveh` when the avatar is more than 40 tiles away. `player_in_control()` calls the same function during normal play, so the side effect is mostly redundant. `u_run_vehicle_eocs` and the unit of `current_speed` (mph×100) are undocumented.

```cpp
// talker_vehicle.cpp:107
return me_veh_const->player_in_control( get_map(), get_avatar() );
// vehicle.cpp:337-338
add_msg( m_bad, _( "Lost connection with the vehicle due to distance!" ) );
g->setremoteveh( nullptr );
```

**Fix:** Add vehicle math functions that take the vehicle talker: `u_vehicle_fuel('gasoline')` (assignable), `u_vehicle_battery()` / `u_vehicle_battery_max()`, `u_vehicle_engine_on()` (assignable), `u_vehicle_velocity()` / `u_vehicle_cruise()` (assignable, mph), `u_vehicle_part_count('FLAG')`, `u_vehicle_part_hp('FLAG')`, `u_vehicle_part_broken_count('FLAG')`, `u_vehicle_power()`, `u_vehicle_max_speed()`. Add effects `u_damage_vehicle_parts: { "flag": ..., "amount": ..., "damage_type": ... }` and `u_set_vehicle_locked`. Split `remote_controlled()` into a side-effect-free query, make `is_driven` cover autodrive/follow/patrol, and document `u_run_vehicle_eocs` and the units.

### VEH-J02 · Rail turning uses a hardcoded list of rail terrain ids instead of terrain flags
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle_move.cpp:74`, `src/vehicle_move.cpp:1851`, `src/vehicle_move.cpp:1878`, `src/vehicle_move.cpp:1880`, `src/vehicle_move.cpp:1882`, `src/vehicle_move.cpp:1884`, `src/vehicle_move.cpp:1886`, `src/vehicle_move.cpp:1951`, `src/vehicle_move.cpp:2132`, `data/json/construction/railroads.json:26`, `data/json/construction/railroads.json:38`

`precalculate_vehicle_turning()` decides whether a wheel is on diagonal or straight track by comparing the terrain id against ten hardcoded `t_railroad_track*` ids. Several vanilla rails carry the RAIL flag but are in neither list: the player-constructible `t_railroad_track_small` and `t_railroad_track_small_d` (diagonal), and `t_railroad_track_recessed`. Turning on those rails therefore works differently. Related bug: both branches increment `incorrect_tiles_not_diagonal`, so `incorrect_tiles_diagonal` is never incremented and its check is dead code. Minor perf (merged from a plausible perf finding): both `unordered_set` literals are rebuilt for every wheel on every look-ahead tile. Auto-turn runs this three times per move step, so a 20-wheel train builds about 180 hash sets per tile, though only on rail tiles.

```cpp
// vehicle_move.cpp:1878
const std::unordered_set<ter_str_id> diagonal_track_ters = { ter_t_railroad_track_d, ... };
// :1880 and :1884 (both branches)
incorrect_tiles_not_diagonal++;
```

**Fix:** Add terrain flags `RAIL_DIAGONAL` / `RAIL_STRAIGHT` (or a ter field `"rail_direction": "diagonal" | "orthogonal"`), tag vanilla rails with them, and test with `has_flag_ter_or_furn`. Increment the correct counter. Until then, at least make the sets `static const`.

### VEH-J03 · No EOC trigger for collisions, part destruction or engine stall; fuel_tank_explodes has no talker
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/event.h:683`, `src/vehicle.cpp:8151`, `src/vehicle.cpp:8090`, `src/vehicle_move.cpp:879`, `src/vehicle.cpp:6274`, `src/vehicle.cpp:6382`, `src/veh_type.h:341`

The vehicle code raises only two events. `vehicle_moves` fires on every move and carries a talker (6382). `fuel_tank_explodes` is sent with plain `send()` and carries only the vehicle name, with no talker and no position. `part_collision()` (hits on creatures, terrain and other vehicles), `break_off()`/`damage_direct()` (part destruction), the `idle()` engine stall and reactor shutdown in `power_parts()` raise no event. `vpart_info`'s only EOC hook is `activatable_eoc`. Content cannot react to ramming, part loss or stalls: no ramming achievements, no "car bomb" parts, no cursed vehicles that scream when hit. Roadkill counting partly works already, because `part_collision` passes the driver as damage source (vehicle_move.cpp:1164).

```cpp
// vehicle.cpp:8151
get_event_bus().send<event_type::fuel_tank_explodes>( name );   // no talker
```

**Fix:** Add events sent with `send_with_talker( vehicle*, Creature* )`: `vehicle_collides { collision_type: body|terrain|vehicle|other, target_name, part_id, velocity_delta, damage }` with the struck creature as beta; `vehicle_part_destroyed { part_id, damage_type }`; `vehicle_engine_stalls { part_id, reason }`. Change `fuel_tank_explodes` to `send_with_talker` and add a fuel field. Add vpart hooks `"on_destroy_eocs": [ ... ]` and `"on_collision_eocs": [ ... ]`. Example:
```json
{ "type": "effect_on_condition", "eoc_type": "EVENT", "required_event": "vehicle_collides",
  "condition": { "compare_string": [ "body", { "context_val": "collision_type" } ] }, "effect": [ ] }
```

### VEH-J04 · Engine and steering fault effects use hardcoded flags and fault ids instead of numeric fault fields
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/vehicle.cpp:4065`, `src/vehicle.cpp:4095`, `src/vehicle.cpp:4443`, `src/vehicle.cpp:4451`, `src/vehicle.cpp:5293`, `src/vehicle.cpp:5335`, `src/vehicle.cpp:1088`, `src/vehicle_part.cpp:813`, `src/fault.cpp:288`, `src/vehicle_display.cpp:341`

Wheel faults have numeric JSON knobs (`contact_area_mod`, `rolling_resistance_mod`, `vehicle_move_penalty_mod`). Engine faults only get on/off flags with fixed C++ multipliers: DOUBLE_FUEL_CONSUMPTION ×2, REDUCE_ENG_POWER ×0.6, EXTRA_EXHAUST squares smoke, ENG_BACKFIRE sets health to 0, NO_ALTERNATOR_CHARGE. `steering_effectiveness()` multiplies by 0.5, 0.1 or 0.9 for three specific fault ids, next to its own TODO. A modded fault (bent tie-rod, low oil) cannot set its own size of effect, and a modded wheel fault cannot affect steering. Side bug: in `basic_consumption()`, `fcon *= 2` doubles the running total for all engines so far, not just the faulty one. That function only feeds the fuel-display sort (vehicle_display.cpp:341), so the impact is small.

```cpp
// vehicle.cpp:4095-4096
if( vp.has_fault_flag( "REDUCE_ENG_POWER" ) ) { m2c *= 0.6; }
// vehicle.cpp:5293
// TODO: Wheel faults modify steering in a json way, and not this hardcoded check
```

**Fix:** Add numeric fault fields `"engine_power_mult"`, `"fuel_consumption_mult"`, `"exhaust_mult"`, `"backfire_chance_mult"`, `"alternator_mult"`, `"steering_mult"`, applied per part. Map the legacy flags to equivalent defaults in `fault::finalize`. Example: `{ "id": "fault_engine_low_oil", "engine_power_mult": 0.85, "fuel_consumption_mult": 1.1 }`.

### VEH-J05 · Wheel-fault escalation is hardcoded and vehicle traps cannot apply faults
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/vehicle_move.cpp:1333`, `src/vehicle_move.cpp:1355`, `src/vehicle_move.cpp:1379`, `src/vehicle_move.cpp:1436`, `src/trap.h:73`, `src/vehicle.cpp:4505`, `data/json/traps.json:285`

`apply_wheel_faults()` picks a C++ escalation chain by fault id: punctured_tires → flat_tire_riding_on_rims for tyres, cracked_rim (90%) → axle_broken for cart wheels. `check_flats_do_rim_damage_or_sounds()` hardcodes the next step and its sounds. A wheel with other potential faults (e.g. a modded track with `fault_thrown_track`) is never damaged by running things over. `vehicle_handle_trap_data` can deal damage, explode, spawn items or set a trap, but cannot apply a fault or run an EOC. As a result `tr_caltrops` only does `{ "damage": 300 }` (nothing at all to wheels with durability above 300), while loose caltrop items puncture tyres.

```cpp
// vehicle_move.cpp:1381
if( vp_wheel->faults_potential().count( fault_punctured_tires ) ) { apply_tire_punctures(...) }
else if( ...count( fault_cracked_rim ) ) { apply_generic_wheel_fault(...) }
```

**Fix:** Make the chain data-driven in fault JSON: `"escalates_to": [ { "fault": "fault_flat_tire_riding_on_rims", "chance": 100 } ]`, `"while_moving": { "sound": "hissssss", "volume": 5, "escalate_one_in": "durability" }`, and a `"runover_damage_faults"` list on the wheel or fault. Add `"wheel_faults": [ ... ]` and `"eoc"` to trap `vehicle_data`, run with the vehicle as alpha.

### VEH-J06 · Ramming damage to creatures is hardcoded and differs between characters and monsters
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/vehicle_move.cpp:1135`, `src/vehicle_move.cpp:1151`, `src/vehicle_move.cpp:1158`, `src/vehicle_move.cpp:1160`, `src/vehicle_move.cpp:1164`, `src/vehicle_move.cpp:1167`, `src/character_health.cpp:2988`, `src/character_health.cpp:2993`, `src/monster.cpp:2508`, `src/creature.cpp:1623`

vpart JSON only controls a `damage_modifier` percentage. Characters go through `Character::hitall()`, which always deals bash damage (marked FIXME) to all six main body parts and ignores SHARP. Monsters take damage once, to a hardcoded "torso", after a manual armor subtraction (cut if SHARP, else bash), via `monster::apply_damage`. That skips `deal_damage`, so immunities and `ondamage_eocs` (creature.cpp:1623) do not apply. Stun formula, bleed chance and duration, and the effect ids are fixed. There is no way to give a ram plate electric or heat damage, armor penetration or a custom on-hit effect.

```cpp
// vehicle_move.cpp:1157-1164
if( ph != nullptr ) { ph->hitall( dam, 40, driver ); } else { ...
    critter->apply_damage( driver, bodypart_id( "torso" ), dam );
```

**Fix:** Add `"collision_damage": [ { "damage_type": "cut", "amount": 1.0, "armor_penetration": 5 } ]` (amount = fraction of computed damage) applied through `Creature::deal_damage` at a random hit location for both characters and monsters. Add `"collision_effects": [ { "id": "bleed", "duration": ..., "chance": ... } ]` and `"on_collision_eocs"`. Default SHARP to a cut entry for back-compat.

### VEH-J07 · The overloaded vpart `bonus` field disagrees with its docs (funnel, muffler, seatbelt)
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/vehicle.cpp:8688`, `src/vehicle.cpp:9173`, `src/vehicle_move.cpp:2498`, `src/vehicle.cpp:7933`, `src/vehicle.cpp:4814`, `src/vehicle.cpp:5523`, `src/lightmap.cpp:1517`, `doc/JSON/JSON_INFO.md:2782`, `data/json/vehicleparts/vp_flags.json:430`

One untyped int `bonus` means a different thing for each part kind (light intensity, funnel size, muffler factor, seatbelt protection, shock absorption, rotor diameter, horn volume, recharger watts, mirror range). It has no units and no validation, and three uses contradict the docs. Funnel: the docs say "area in mm²" and vp_flags says "radius in mm²", but the code treats it as a radius in mm. A modder entering an area of 280000 gets a funnel of about 246,000 m². Muffler: documented as "% noise reduction", but the vanilla bonus of 40 leaves 40% of the noise, and a bonus of 100 gives no muffling at all. Seatbelt: documented as non-functional, but `shake_vehicle` subtracts it from impact damage. The shock-absorber and mirror meanings are undocumented.

```cpp
// vehicle.cpp:8688
const double area_in_mm2 = std::pow( pt.info().bonus, 2 ) * M_PI;
// vehicle.cpp:9173
m = 1.0 - ( 1.0 - vp.info().bonus / 100.0 ) * vp.health_percent();
```

**Fix:** Fix the doc lines now (small). Then add typed fields with `bonus` as legacy fallback: `"funnel_radius": "300 mm"`, `"muffle_factor": 0.4`, `"seatbelt_protection": 10`, `"shock_absorption": 15`, `"light_intensity"`, `"horn_volume"`, `"recharge_rate": "600 W"`, `"rotor_lift_bonus_diameter"`, validated in `check()`.

### VEH-J08 · Driving proficiencies and skill are hardcoded; control_requirements has no water key and does not affect handling
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/vehicle_move.cpp:1603`, `src/vehicle_move.cpp:1606`, `src/vehicle_move.cpp:1727`, `src/vehicle.cpp:5410`, `src/vehicle.cpp:5468`, `src/vehicle.cpp:4855`, `src/vehicle.cpp:4909`, `src/veh_type.cpp:316`, `data/json/vehicleparts/rotor.json:10`

JSON `control_requirements` (only `air` and `land`) is just a gate on taking the controls. Handling penalties, fumble chance and training are hardcoded to `skill_driving` plus `prof_driver` (land) or `prof_boat_pilot` (deep water). A helicopter whose rotor requires `prof_helicopter_pilot` is flown with `prof_driver` handling, and flying trains `prof_driver`, never the helicopter proficiency. Boats cannot declare requirements. The aircraft install/repair gate is hardcoded to `prof_aircraft_mechanic`.

```cpp
// vehicle_move.cpp:1606
if( !driver.has_proficiency( proficiency_prof_driver ) && !in_deep_water ) { ... }
else if( !driver.has_proficiency( proficiency_prof_boat_pilot ) && in_deep_water )
```

**Fix:** Extend to `"control_requirements": { "land"|"water"|"air": { "skills", "proficiencies", "handling_skill": "driving", "handling_proficiency": "prof_helicopter_pilot", "trains": [ "prof_helicopter_pilot" ] } }`. Have `pldrive()` and `practice_pilot_proficiencies()` read the active mode's entry, falling back to the current ids. Replace the aircraft-mechanic gate with a `"maintenance_proficiency"` field.

### VEH-J09 · Only three vehicle enchantment values exist
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/magic_enchantment.h:158`, `src/magic_enchantment.cpp:470`, `src/vehicle.cpp:3828`, `src/vehicle.cpp:3844`, `src/vehicle.cpp:3857`, `src/vehicle.cpp:5324`, `src/vehicle.cpp:5334`, `src/vehicle.cpp:4053`, `doc/JSON/MAGIC.md:1131`

Vehicle enchantments (from vparts and effects) can only change TOTAL_WEIGHT, FUEL_USAGE and TURNING_DIFFICULTY. Nothing else in the vehicle math reads an enchantment: engine power, max velocity, acceleration, drag, rolling resistance, traction, noise, damage taken, ramming damage, renewable output, battery drain. The existing hooks are inconsistent: TOTAL_WEIGHT is applied twice on wheels (see VEH-B15), and FUEL_USAGE is missing from `basic_consumption()` and reactor fuel draw (only the dashboard sort uses `basic_consumption`). No vanilla content uses vehicle enchantments yet.

```cpp
// magic_enchantment.h:158-160
TOTAL_WEIGHT, FUEL_USAGE, TURNING_DIFFICULTY,
```

**Fix:** Add `VEH_ENGINE_POWER`, `VEH_MAX_VELOCITY`, `VEH_ACCELERATION`, `VEH_AIR_DRAG`, `VEH_ROLLING_RESISTANCE`, `VEH_TRACTION`, `VEH_NOISE`, `VEH_PART_DAMAGE_TAKEN`, `VEH_COLLISION_DAMAGE`, `VEH_RENEWABLE_OUTPUT`, `VEH_EPOWER_DRAIN`. Wire them into the matching functions and extend `is_vehicle_relevant()`. Example: a Magiclysm "Haste" spell giving `{ "value": "VEH_MAX_VELOCITY", "multiply": 0.3 }`.

### VEH-J10 · Vehicle chat_topics is dead code; vehicle prototypes cannot set tags, vars or topics
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/vehicle.cpp:1240`, `src/vehicle.h:939`, `src/talker_vehicle.cpp:84`, `src/savegame_json.cpp:3855`, `src/veh_type.cpp:1339`, `src/mapgen.cpp:2711`

`vehicle::chat_topics` is saved, loaded and returned by `talker_vehicle::get_topics()`, but nothing ever calls `vehicle::add_chat_topic()`. The only `add_chat_topic` callers are for computers (mapgen.cpp:3258), and no JSON field or effect sets vehicle topics. `vehicle_prototype::load()` reads only name, blueprint, parts, items and zones, and `jmapgen_vehicle` adds only placement options. A prototype cannot start with tags, vars, chat topics or a spawn EOC. Vars can only be set after spawn, through `run_vehicle_eocs` from some other trigger.

**Fix:** Add prototype fields `"chat_topics": [ ... ]`, `"tags": [ ... ]`, `"vars": { "key": value }`, `"spawn_eocs": [ ... ]` (run in `init_state` with the vehicle talker). Add an EOC effect `u_add_vehicle_chat_topic` and a "Talk to vehicle" controls entry when topics exist.

### VEH-J11 · Engine behaviour is hardcoded to the pseudo-fuel ids animal, muscle, wind, mana and battery
**Severity:** Medium · **Effort:** Large · **Verified:** confirmed
**Where:** `src/vehicle.cpp:1143`, `src/vehicle.cpp:1152`, `src/vehicle.cpp:1168`, `src/vehicle.cpp:301`, `src/vehicle.cpp:444`, `src/vehicle.cpp:636`, `src/vehicle.cpp:3925`, `src/vehicle.cpp:6262`, `src/vehicle.cpp:6272`, `src/vehicle_move.cpp:531`, `src/vehicle_move.cpp:557`, `src/vehicle_move.cpp:665`, `src/veh_type.cpp:913`, `src/veh_type.h:129`

What an engine does depends on which fuel itype id it burns, not on its JSON. `part_vpower_w()` replaces `power` with C++ formulas for animal (size, speed, weight), muscle (STR, athlete bonus, a fixed +8 for MUSCLE_VEH_BOOST) and wind (windspeed). `fuel_left()` gives muscle engines their fuel only when the driver stands on the engine tile; other PERPETUAL fuels get a flat 10. `idle()` skips the "engine dies" message only for four ids. Spawn logic, control checks and thrust messages test these ids. "Electric" behaviour is keyed on the battery itype: the 60% reverse-acceleration penalty is skipped only for battery engines. A new pseudo-fuel (steam, rowing, spirit power) gets only flat `power`. Modders must reuse vanilla ids, as Magiclysm and Xedra do with "mana". This is a design limitation with a working fallback, not wrong behaviour.

```cpp
// vehicle_move.cpp:557
if( thd == -1 && thrusting && !has_engine_type( fuel_type_battery, true ) ) { vel_inc = .6 * vel_inc; }
```

**Fix:** Add a power-source descriptor to `vpslot_engine`: `"engine": { "power_source": "fuel"|"rider"|"harnessed_animal"|"wind"|"perpetual", "power_math": "<math expr>", "electric": true, "reverse_efficiency": 0.6, "stall_message": false }`. Evaluate `power_math` with the vehicle as alpha and the rider or animal as beta. Default the descriptor from the legacy ids, then replace each `fuel_type == fuel_type_X` test with a descriptor check.

### VEH-J12 · Engine noise and smoke are hardcoded; running backfire ignores backfire_freq
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle.cpp:4409`, `src/vehicle.cpp:4446`, `src/vehicle.cpp:4457`, `src/vehicle.cpp:4466`, `src/vehicle.cpp:4469`, `src/veh_type.cpp:399`, `src/vehicle_use.cpp:712`, `doc/JSON/JSON_INFO.md:2909`

`noise_and_smoke()` returns early unless an engine has E_COMBUSTION (the silent-vehicle consequence is VEH-B14). The onomatopoeia ladder, the smoke field (always `fd_smoke`) and the running backfire chance `one_in( 50 + 150 * health )` are fixed. The JSON `backfire_freq` is used only at engine start. The docs present it as the general backfire chance with default 0, but the code default is 1. Custom exhaust is partly possible today through the vpart `exhaust`/`emissions` lists.

```cpp
// vehicle.cpp:4446
if( health < vp.info().engine_info->backfire_threshold && one_in( 50 + 150 * health ) )
```

**Fix:** Add engine fields `"noise_messages": [ { "threshold": 60, "text": "vroom!" } ]`, `"sfx_id"`, `"exhaust_field": "fd_smoke"`, `"running_backfire_chance"`. Apply `noise_factor` to every engine type. Fix the documented `backfire_freq` default and scope.

### VEH-J13 · Planter enable/disable checks buckwheat temperature at the player's position
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle.cpp:6279`, `src/vehicle.cpp:6284`, `src/vehicle_part.cpp:749`, `src/vehicle_part.cpp:751`, `src/vehicle_use.cpp:1110`

`can_enable()` and `idle()` decide whether a PLANTER may run by calling `warm_enough_to_plant()` for `seed_buckwheat` at the player's position. They ignore the loaded seed and the vehicle's position. `operate_planter()` does no per-seed check. Cold-hardy modded seeds are wrongly blocked, and a remote planter uses the player's temperature and sunlight (standing near an INDOORS tile can switch it off). `can_enable` also runs this expensive check for every part before testing the PLANTER flag.

```cpp
// vehicle.cpp:6279-6284
// FIXME/HACK: Always checks buckwheat seeds!
ret_val<void>can_plant = warm_enough_to_plant( player_character.pos_bub(), itype_seed_buckwheat );
```

**Fix:** Check the seeds actually in the planter's cargo at `bub_part_pos(planter)` (or skip when empty), and do the per-seed check in `operate_planter()` using each seed's JSON growth requirements. Test the PLANTER flag first in `can_enable`.

### VEH-J14 · Passenger ejection uses hardcoded traits DEFT and PROF_SKATER and fixed formulas
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle_move.cpp:2472`, `src/vehicle_move.cpp:2473`, `src/vehicle_move.cpp:2476`, `src/vehicle_move.cpp:2477`, `src/vehicle_move.cpp:2480`, `src/vehicle_move.cpp:2489`

Resistance to being thrown from a seat is STR×150+500. SEAT_REQUIRES_BALANCE seats add a fixed 500 penalty, reduced by 150 each only for the traits PROF_SKATER and DEFT. Pets resist at weight×200. No enchantment value or character flag is consulted, so mutations or CBMs cannot improve balance, and the seat penalty is not a JSON value.

```cpp
// vehicle_move.cpp:2476-2482
int resist_penalty = 500;
if( psg->has_trait( trait_PROF_SKATER ) ) { resist_penalty -= 150; }
if( psg->has_trait( trait_DEFT ) ) { resist_penalty -= 150; }
```

**Fix:** Replace the trait checks with a character enchantment value `VEHICLE_EJECT_RESIST` (or a `json_character_flag` such as `VEHICLE_BALANCE`). Move the penalty to the seat: `"seat_balance_penalty": 500`. Give PROF_SKATER and DEFT the enchantment in JSON.

### VEH-J15 · Power-grid wiring detection checks the wall_wiring item id instead of the WIRING vpart flag
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle.cpp:2639`, `src/vehicle.cpp:2682`, `src/veh_appliance.cpp:121`, `src/vehicle.cpp:2185`, `data/json/furniture_and_terrain/appliances.json:688`

The WIRING vehicle tag (needed for wire-only grids to pass `is_powergrid()`) is set only when the part's base item is exactly `wall_wiring`. This happens both on grid split (vehicle.cpp:2639) and on first placement (veh_appliance.cpp:121). The `wall_wiring` vpart already has a WIRING flag. A modded conduit with the WIRING flag but another item never gets the tag, its grid fails `is_powergrid()` and won't merge. Vanilla quirk: in a mixed split, the new piece keeps the tag only if its first part is wiring.

```cpp
// vehicle.cpp:2639
bool is_wiring = parts[split_part0].info().base_item == itype_wall_wiring;
```

**Fix:** Use `info().has_flag( flag_WIRING )` at both sites, and treat any WIRING-flagged part as wiring wherever the tag is decided.

### VEH-J16 · Important vpart fields are undocumented, and activatable_eoc gets no vehicle talker
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/veh_type.cpp:259`, `src/veh_type.cpp:260`, `src/veh_type.cpp:266`, `src/veh_type.cpp:281`, `src/veh_type.cpp:283`, `doc/JSON/JSON_INFO.md:2773`, `src/vehicle_use.cpp:2440`, `src/vehicle.cpp:6315`

`vpart_info::load` reads `activatable_eoc` (with the EOC_ACTIVATION flag), `enchantments`, `emissions`, `exhaust` and `light_color`. None appear in the vehicle-part section of JSON_INFO.md or in VEHICLES_JSON.md, EOC_ACTIVATION is missing from JSON_FLAGS.md, and MAGIC.md never names the field. `activatable_eoc` also runs as `(player, nullptr)`, even though `get_talker_for(vehicle*)` exists (vehicle.cpp:9260), so the EOC cannot act on the vehicle it was activated on.

```cpp
// vehicle_use.cpp:2440
dialogue newDialog( get_talker_for( get_player_character() ), nullptr );
```

**Fix:** Document these fields in JSON_INFO.md and cross-link MAGIC.md. Pass `get_talker_for( this )` as beta for `activatable_eoc` and document it.

### VEH-J17 · Spawn condition of vehicles is fixed in C++ and cannot be set per prototype or per mapgen
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/vehicle.cpp:380`, `src/vehicle.cpp:418`, `src/vehicle.cpp:423`, `src/vehicle.cpp:428`, `src/vehicle.cpp:443`, `src/vehicle.cpp:448`, `src/vehicle.cpp:474`, `src/vehicle.cpp:550`, `src/mapgen.cpp:2711`

`init_state()` hardcodes the spawn rolls: 1/3 locked, 2/3 alarm destroyed, at least 1/3 engine faults, running-engine chance 1/(day+4), per-light on-chances, blood chances, the 4d8 damage curve, solar panel loss, and the `engine_maintenance` fault category. Mapgen already allows fuel and status (`"status": 2` is PRISTINE), so this is only about tuning the locked, alarm, light and blood chances. Doc side note: MAPGEN.md:977 says default fuel is 1-7%, but the code uses `normal_roll(0.3, 0.15)`.

```cpp
// vehicle.cpp:418
if( one_in( 3 ) ) { //33% chance for a locked vehicle
```

**Fix:** Add a `vehicle_spawn_profile` type, e.g. `{ "id": "civilian_wreck", "locked_chance": 0.33, "alarm_broken_chance": 0.66, "engine_fault_chance": 0.33, "light_on_chance": { "CONE_LIGHT": 0.05 }, "blood_chance": 0.1, "fuel": { "mean": 0.3, "stddev": 0.15 } }`, usable on prototypes and as a mapgen `"spawn_profile"` override. The current numbers become the default. Fix the MAPGEN.md fuel line.

### VEH-J18 · Air drag and braking come from hardcoded flags and constants, with no per-part JSON contribution
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/vehicle.cpp:4607`, `src/vehicle.cpp:4673`, `src/vehicle.cpp:7017`, `src/vehicle_move.cpp:552`, `src/vehicle_move.cpp:580`

`coeff_air_drag()` builds drag from a fixed flag set and `constexpr` heights and penalties, so a new part (spoiler, drag chute) cannot have its own drag. EXTRA_DRAG reuses the part's `power` field as a drag value. Braking is a fixed 20 mph/s (25% on thick ice), with a TODO about wheels, traction and skill. Side bug: an enabled EXTRA_DRAG part that is also WIND_TURBINE or WATER_WHEEL adds `vpi.power` twice (lines 7017 and 7020).

```cpp
// vehicle_move.cpp:552-553
const float ICE_BRAKE_FACTOR = 0.25f;
const int max_brake = static_cast<int>( 20 * 100 * ( on_thick_ice ? ICE_BRAKE_FACTOR : 1.0f ) );
```

**Fix:** Add vpart fields `"air_drag_mod"`, `"drag_height"` (metres) with the current flags as defaults, `"extra_drag": "-70000 W"` to replace the EXTRA_DRAG+power hack, and a wheel field `"braking_force"` scaled by contact area and traction. Fix the double-count.

## 2. Bugs, oversights & quirks

### VEH-B01 · UNMOUNT_ON_MOVE parts crash the game: shed_loose_parts() dereferences a null map
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle.cpp:6733`, `src/vehicle.cpp:7661`, `src/vehicle.cpp:7703`, `src/vehicle.cpp:7705`, `src/vehicle.cpp:6980`, `src/vehicle.h:2104`, `src/map.cpp:990`, `doc/JSON/JSON_FLAGS.md:1821`

`gain_moves()` calls `shed_loose_parts()` with no arguments every turn a vehicle with loose parts moves or falls. The defaults are `here = nullptr, dst = nullptr`, and the both-null guard lets that through. Cables are skipped, but any non-cable loose part (UNMOUNT_ON_MOVE) reaches `part_to_item( *here, ... )` and `here->add_item_or_charges(...)`, a null dereference. A modder who uses the documented flag crashes the game on the first moving turn. No shipped JSON uses the flag, so the crash is latent. The docs also say the part is not dropped, but the code drops it.

```cpp
// vehicle.cpp:6733
shed_loose_parts();
// vehicle.cpp:7703-7705
const item drop = part_to_item( *here, vp_loose );
if( !magic ) { here->add_item_or_charges( bub_part_pos( *here, vp_loose ), drop ); }
```

**Fix:** Pass the map from `gain_moves`: `shed_loose_parts( trinary::NONE, &here, nullptr )`. Better, make the map a reference parameter. Align the drop behaviour with JSON_FLAGS.md (or add a JSON option).

### VEH-B02 · Fuel tanks never explode: explode_fuel() keys on the vpart's fuel_type, which is null for all tanks
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle.cpp:8135`, `src/vehicle.cpp:8150`, `src/vehicle.cpp:8184`, `src/vehicle.cpp:8227`, `src/vehicle_part.cpp:235`, `src/veh_type.cpp:261`, `src/material.cpp:413`, `src/rng.cpp:118`

`explode_fuel()` uses `vp.fuel_current()`, which returns `ammo_pref` (set only for engines and turrets) or `info().fuel_type`. No tank in tanks.json sets `fuel_type`, and the default is NULL_ID, so the function returns false for every vanilla tank. The gasoline and diesel `explosion_data` in materials.json is never used for vehicles: shooting, burning or ramming a full gas tank never explodes it. Second trap: `chance_hot`/`chance_cold` default to 0 and `one_in(0)` returns true, so a mod material that omits one explodes on every hit (all vanilla materials define both).

```cpp
// vehicle.cpp:8135-8137
const itype_id &ft = vp.fuel_current();
if( ft.is_null() ) { return false; }
```

**Fix:** Use the tank contents: `const itype_id ft = vp.ammo_current();`. Treat explosion chances of 0 as "never" (`if( explosion_chance <= 0 ) return false;`).

### VEH-B03 · wheel_terrain_modifiers stops at the first matching entry, contradicting the docs
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle_move.cpp:2343`, `src/vehicle_move.cpp:2349`, `src/vehicle_move.cpp:2408`, `src/vehicle_move.cpp:2414`, `src/veh_type.cpp:410`, `src/veh_type.cpp:429`, `doc/JSON/JSON_INFO.md:2944`, `doc/JSON/JSON_INFO.md:2951`, `data/json/vehicleparts/wheel.json:201`, `src/pathfinding.cpp:745`

(Reported by both the JSON and bug auditors; merged.) The docs say penalties stack: a tile with neither FLAT nor ROAD gives "1/6 of base" for the standard wheel. Both `map::vehicle_wheel_traction` overloads `break` after the first applicable override or penalty, so a non-FLAT tile gets only the FLAT penalty and ROAD is never checked. Evaluation order is not authoring order: flexbuffer object keys are sorted, so entries are checked alphabetically by flag. The road roller (`{ "FLAT": [0,6], "ROAD": [0,10] }`) gets +6 on rough ground but +10 on flat unpaved ground, so it handles better off-road. Existing content may have been tuned around this.

```cpp
// vehicle_move.cpp:2343-2351 (duplicated at 2408-2417)
if( ter_has_flag && mod.move_override ) { move_mod = mod.move_override; break; }
else if( !ter_has_flag && mod.move_penalty ) { move_mod += mod.move_penalty; break; }
```

**Fix:** Accumulate all penalties and let an override replace the total. Factor the loop into one helper for both overloads. Update `veh_drag_cost` in pathfinding.cpp:745, which hardcodes the current semantics. If short-circuiting is wanted instead, document it and load the field as an ordered array.

### VEH-B04 · Reactor parts reuse `power` as fuel efficiency, can divide by zero, and always spawn with plut_cell
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle.cpp:5842`, `src/vehicle.cpp:5848`, `src/vehicle.cpp:5850`, `src/vehicle.cpp:505`, `src/vehicle.cpp:6401`, `src/veh_type.cpp:257`, `src/veh_type.cpp:913`, `data/json/furniture_and_terrain/appliances_generators.json:14`

(Reported by both the JSON and bug auditors; merged.) In `power_parts()`, a fueled reactor's kJ-per-fuel-unit is `units::to_watt( vpi.power )`, an energy ratio stored in a power field (the vanilla JSON comments "BIZARRE FIELD NAMES HERE!"). It is used as an unguarded integer divisor and modulus. `power` defaults to 0, and the only non-zero check covers ENGINE parts. A REACTOR without ENGINE or PERPETUAL and without `power` therefore loads cleanly, then crashes with SIGFPE the first turn it runs with fuel and a battery deficit. Aftershock's `minireactor` (flags `["REACTOR"]`) has exactly this shape, so copying it and dropping `power` crashes. Also, `init_state()` fills every reactor with `plut_cell` whatever its fuel. A REACTOR+FLUIDTANK generator in a prototype spawns empty with no message. `slow_leak()` hardcodes `plut_slurry_dense`. Shipped content is safe.

```cpp
// vehicle.cpp:5842-5850
const int efficiency = units::to_watt( vp.info().power );
int fuel_consumed = reactors_output_bat / efficiency;
... x_in_y( reactors_output_bat % efficiency, efficiency )
```

**Fix:** Now: in `vpart_info::check()`, reject REACTOR parts without PERPETUAL whose efficiency is ≤ 0. Use `rng_fuel_amount( pt, pt.info().fuel_type )` in `init_state()`. Then add `"reactor": { "energy_per_fuel": "10000 kJ" }` (default: the fuel item's energy; read `power` as a deprecated fallback) and `"leaks_as": "plut_slurry_dense"` on the fuel.

### VEH-B05 · Towing vehicle cannot brake when accel < 200 (for example engine off or poor traction)
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle_move.cpp:521`, `src/vehicle_move.cpp:522`, `src/vehicle_move.cpp:555`, `src/vehicle.cpp:4142`, `src/vehicle.cpp:6755`

In `thrust()`, the "struggles to pull" early return checks `accel < 200 && velocity > 0 && is_towing()` but not `thrusting`. Braking (manual or cruise-control slowdown) while towing on poor traction or with the engine off (acceleration is 0 when `!engine_on`) returns before the brake is applied. Only drag slows the vehicle, and the player sees a misleading "struggles to pull" message.

```cpp
// vehicle_move.cpp:521-522
int accel = current_acceleration( here ) * traction;
if( accel < 200 && velocity > 0 && is_towing() ) { ... return; }
```

**Fix:** Add `thrusting &&` to the condition so braking falls through to the `max_brake` path.

### VEH-B06 · Armor plating is damaged twice when it is the randomly chosen target part
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle.cpp:7845`, `src/vehicle.cpp:7865`, `src/vehicle.cpp:7886`, `src/vehicle.cpp:7902`, `src/vehicle.cpp:7906`, `src/vehicle.cpp:7907`

`vehicle::damage()` picks `target_part = random_entry( parts_here )`, and that list includes the ARMOR part. When the armor is picked, the else branch calls `damage_direct` on it twice: once with `dmg`, then with `dmg - protection` (and `damage_direct` subtracts its reduction again). This happens with probability 1/N per hit from collisions, projectiles and explosions, so armor wears faster than intended.

```cpp
// vehicle.cpp:7902-7907
} else {
    const int damage_dealt = damage_direct( here, vp_armor, dmg, type );
    damage_direct( here, vp_target, overhead ? dmg : dmg - protection, type );
```

**Fix:** Exclude the armor part from the random pick, or damage it only once when `target_part == armor_part`.

### VEH-B07 · Power-grid merge size check uses the min corner twice, so the 24-tile cap is not enforced
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle.cpp:2103`, `src/vehicle.cpp:2106`, `src/vehicle.cpp:2114`, `src/vehicle.cpp:164`, `src/vehicle.cpp:8970`, `src/veh_appliance.cpp:139`

In `merge_appliance_into_grid`, `max` is built from both bounding boxes' `p1` (min corner) instead of `p2`. The size check therefore compares the two grids' top-left corners. Placing an appliance or wall wiring up or left of a grid always merges, so the grid grows without limit in that direction, while growing right or down is refused. Players building wired bases see an inconsistent cap, and oversized appliance vehicles make every later grid operation slower.

```cpp
// vehicle.cpp:2106
const point_bub_ms max = { std::max( veh_pos.x() + vehicle_box.p1.x(), target_pos.x() + target_vehicle_box.p1.x() ), ... };
```

**Fix:** Use `p2` for both terms of `max`.

### VEH-B08 · Spawned locked vehicles never get an engine immobilizer: the fault is set on the alarm part
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle.cpp:601`, `src/vehicle.cpp:607`, `src/vehicle_part.cpp:534`, `src/vehicle.cpp:1115`, `src/vehicle_use.cpp:721`, `data/json/faults/faults_vehicles.json:25`, `data/json/vehicleparts/vehicle_parts.json:1516`

`init_state()` means to give half of the keyless vehicles an immobilizer, but calls `fault_set( fault_engine_immobiliser )` on the SECURITY part (base item `processor`, which has no faults). `fault_set` fails when the fault is not in `faults_potential()`, so the call never does anything. `unlock()` and `start_engine()` both look for the fault on engines, which confirms the intent. No other path sets this fault.

```cpp
// vehicle.cpp:602-608
if( vp.has_feature( "SECURITY" ) && has_no_key && pt.is_available() ) {
    is_locked = true;
    if( one_in( 2 ) ) { pt.fault_set( fault_engine_immobiliser ); }
```

**Fix:** When the roll succeeds, call `fault_set( fault_engine_immobiliser )` on each part in `engines`.

### VEH-B09 · Summoned vehicles: timer burns per tile moved, and split-off pieces become permanent
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle_move.cpp:2013`, `src/vehicle.cpp:6786`, `src/vehicle.cpp:6804`, `src/map.cpp:1001`, `src/map.cpp:1078`, `src/vehicle.cpp:6735`, `src/vehicle.cpp:2665`, `src/magic_spell_effect.cpp:1414`

(1) `decrement_summon_timer()` runs at the top of `act_on_map()`, which `map::vehproceed` calls once per tile moved plus once more per turn. A summoned vehicle at about 15 tiles/turn loses about 16 turns of duration per real turn. (2) `split_vehicles` copies velocity, owner and engine state to new pieces, but not `magic`, `summon_time_limit`, `is_locked`, effects or values. If a crash splits a summoned vehicle, the split-off piece is permanent and drops real items when its parts break (every drop is gated on `!magic`). Only Magiclysm summons are affected.

```cpp
// vehicle_move.cpp:2013 (inside act_on_map)
if( decrement_summon_timer( here ) ) {
// vehicle.cpp:6804
*summon_time_limit -= 1_turns;
```

**Fix:** Decrement the timer once per turn (in `gain_moves` or `idle`). In `split_vehicles`, copy `magic`, `summon_time_limit`, `is_locked`, effects and values.

### VEH-B10 · Reversing makes handling_difficulty negative: free fast steering and instant skid recovery
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle.cpp:5324`, `src/vehicle_move.cpp:1516`, `src/vehicle_move.cpp:1637`, `src/vehicle_move.cpp:1668`, `src/vehicle_move.cpp:1724`, `src/rng.cpp:32`

`handling_difficulty()` multiplies by signed `velocity`, so the value is negative in reverse. `rng_float` swaps reversed bounds, so in `pldrive` the penalty is at most `-skill`: no fumbles and minimum turn cost. Both skid-recovery checks (`< 15` in selfdrive, `< dex + 2*skill` in pldrive) always pass. Electric vehicles can reverse at full `max_velocity`, so fast reversing on ice or with broken steering is easier than driving forward.

```cpp
// vehicle.cpp:5324-5325
return enchantment_cache.modify_value( enchant_vals::mod::TURNING_DIFFICULTY,
                                       velocity * diff_mod / vehicles::vmiph_per_tile );
```

**Fix:** Use `std::abs( velocity )`.

### VEH-B11 · Driverless (follow/patrol) vehicles train the player's driving proficiency from anywhere
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle.cpp:5402`, `src/vehicle.cpp:5409`, `src/vehicle.cpp:6754`, `src/vehicle_move.cpp:630`, `src/vehicle.cpp:5468`, `src/vehicle.cpp:3925`, `src/vehicle.cpp:637`, `src/vehicle.cpp:654`

`gain_moves` calls `thrust()` for following or patrolling vehicles with no driver: autopilot, animal carts following the player, Magiclysm follow vehicles. `consume_fuel` then falls back to the player (existing FIXME) and calls `practice_pilot_proficiencies`, so the player gains 1 s of `prof_driver` or `prof_boat_pilot` per thrusting turn while walking around. The same fallback exists in `fuel_left` for muscle fuel, but there it still requires the player to stand on this vehicle's engine tile.

```cpp
// vehicle.cpp:5402-5411
if( !driver ) { // FIXME: Should return early, not arbitrarily assume player
    driver = &get_player_character(); }
if( load > 0 ) { practice_pilot_proficiencies( *driver, in_deep_water ); }
```

**Fix:** Skip the training section when `get_driver()` is null; return `fl` from `fuel_left` when there is no driver.

### VEH-B12 · Enabled engine with no fuel builds an unbounded fuel debt, paid off instantly on refuel
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle.cpp:5341`, `src/vehicle.cpp:5349`, `src/vehicle.cpp:5385`, `src/vehicle.cpp:5389`, `src/vehicle_part.cpp:401`, `src/savegame_json.cpp:3996`

`fuel_usage()` counts every enabled engine regardless of fuel, while `total_power(fueled)` skips unfuelled ones, so a vehicle keeps running on its fuelled engines. For the empty fuel, `drain_energy` returns 0 and `fuel_remainder[ft]` becomes `-to_consume`. That debt is added to next turn's request, so it grows without limit and is saved. On refuel, `consume_energy` takes up to the whole debt in one turn, so liters can vanish at once. A common case: a gas+electric hybrid with a flat battery builds battery debt while driving. The fuel UI also shows consumption for an engine producing nothing.

```cpp
// vehicle.cpp:5385-5392
to_consume -= remainder;
if( to_consume > 0_J ) { fuel_remainder[ ft ] = drain_energy( ft, to_consume ) - to_consume; }
```

**Fix:** Skip engines with `engine_fuel_left() == 0` in `fuel_usage()`, or clamp the remainder to at most one fuel unit of debt.

### VEH-B13 · Acceleration uses integer W/kg; heavy or low-power vehicles lose up to 100% of acceleration
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle.cpp:4154`, `src/vehicle.cpp:4155`, `src/vehicle.cpp:4186`, `src/vehicle.cpp:4187`, `src/vehicle_move.cpp:529`

`ground_acceleration` and `water_acceleration` store `watts / kg` in an int. 1.9 W/kg becomes 1, and anything below 1 becomes 0, which triggers "too heavy for its engine(s)" although the engine has power. Foot pedals on a bicycle (about 3.8 W/kg) lose about 21%. Animal power pulling a loaded wagon can drop below 1 W/kg and stop entirely. Normal cars lose under 2%.

```cpp
// vehicle.cpp:4154-4155
int engine_power_ratio = units::to_watt( total_power( here, fueled ) ) / weight;
int accel_at_vel = 100 * 100 * engine_power_ratio / cmps;
```

**Fix:** Use `double` for both values and round only the final result.

### VEH-B14 · Non-combustion vehicles are silent at any speed (velocity noise is unreachable)
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle.cpp:4426`, `src/vehicle.cpp:4466`, `src/vehicle.cpp:4481`, `src/vehicle.cpp:4489`, `src/vehicle_move.cpp:628`, `data/json/vehicleparts/motor.json:8`, `doc/JSON/JSON_INFO.md:2911`

`noise_and_smoke()` resets `vehicle_noise` to 0 and returns when no combustion engine runs. That skips the "even a vehicle with engines off will make noise at high speed" line and the `sounds::sound` call. Electric, muscle, animal, wind and mana vehicles never make sound, so monsters never hear them even at 100 mph. The documented `noise_factor` on motors and yokes is ignored. A gas vehicle coasting with the engine off never reaches this function at all (thrust bails at vehicle_move.cpp:595, idle requires `engine_on`), so removing the early return alone does not cover coasting. See VEH-J12 for the data-driven noise side.

```cpp
// vehicle.cpp:4466-4481
if( !combustion ) { return; }
...
// Even a vehicle with engines off will make noise traveling at high speeds
noise = std::max( noise, static_cast<double>( std::fabs( velocity / 500.0 ) ) );
```

**Fix:** Skip only the smoke and backfire parts for non-combustion vehicles. Always compute velocity noise plus each engine's `noise_factor`, and emit the sound when noise > 0. Call the velocity-noise path for coasting vehicles too.

### VEH-B15 · TOTAL_WEIGHT vehicle enchantment is applied twice in weight_on_wheels()
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle.cpp:3828`, `src/vehicle.cpp:3846`, `src/vehicle.cpp:3857`, `src/vehicle.cpp:5004`, `src/vehicle.cpp:5023`, `doc/JSON/MAGIC.md:1135`

`total_mass()` already applies TOTAL_WEIGHT. `weight_on_wheels()` takes that value, subtracts animal mass and applies it again. A multiply of -0.1 becomes 0.81× for traction. A negative `add` is subtracted twice and can push mass negative; `k_traction` then clamps to 0.1, so a "lightening" spell cripples the vehicle. Only TEST_DATA uses this on vehicles, so only third-party mods hit it.

```cpp
// vehicle.cpp:3846-3857
const units::mass vehicle_mass = total_mass( here );
...
return enchantment_cache.modify_value( enchant_vals::mod::TOTAL_WEIGHT, vehicle_mass - animal_mass );
```

**Fix:** Return `vehicle_mass - animal_mass` directly.

### VEH-B16 · split_vehicles copies stale has_fake/fake_part_at indices into the new vehicle
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/vehicle.cpp:2719`, `src/vehicle.cpp:2743`, `src/vehicle.cpp:7080`, `src/vehicle.cpp:8218`, `src/vehicle.cpp:2313`, `src/vehicle.cpp:1984`, `src/mapgen.cpp:7023`

`vehicle_part` is copied by default, so split pieces keep `has_fake = true` and `fake_part_at` (an index into the old vehicle). The new vehicle's `fake_parts` is empty, so `remove_fake_parts` does not clear them, and `add_fake_part` trusts any in-range index. Once enough fresh fakes are appended, a stale index can point at another part's fake. That part gets no fake (a diagonal collision gap), `damage_direct` copies its base item onto the wrong fake, `remove_part` marks the wrong fake removed, and doors toggle the wrong fake. This needs the new piece's fake count to exceed the size of the piece that stays behind. Effects stay limited to fakes, and the problem clears on reload. The copies in `merge_rackable_vehicle` (1984) and mapgen's install copy (mapgen.cpp:7023) could alias real parts and deserve a separate check.

```cpp
// vehicle.cpp:7080-7084
if( part_real.has_fake && static_cast<size_t>( part_real.fake_part_at ) < parts.size() ) {
    relative_parts[ parts[ part_real.fake_part_at ].mount].push_back( part_real.fake_part_at ); return; }
```

**Fix:** In every part-copying path, reset `has_fake = false; fake_part_at = -1; is_fake = false; is_active_fake = false;`, or call `remove_fake_parts( here, true )` on the source before splitting.

### VEH-B17 · Rotor collisions are pushed into colls twice, so the rotor takes double damage
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle_move.cpp:804`, `src/vehicle_move.cpp:814`, `src/vehicle_move.cpp:823`, `src/map.cpp:1189`

For a ROTOR part, each hit inside the rotor radius is pushed into `colls` and stored in `coll`. After the loop the code falls through to `colls.push_back( coll )`, which appends the last rotor hit again. `map::move_vehicle` applies damage per entry, so that hit damages the rotor twice. A vehicle-vehicle hit is also added twice to `veh_collisions`.

```cpp
// vehicle_move.cpp:809-823
if( rotor_coll.type != veh_coll_nothing ) { coll = rotor_coll; if( just_detect ) { break; } else { colls.push_back( rotor_coll ); } }
...
colls.push_back( coll );
```

**Fix:** After the rotor scan, `continue` when rotor hits were already pushed (non-detect mode), or stop pushing inside the loop.

### VEH-B18 · Smart controller: alternator load capped by min() instead of max(); state reset every turn
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle_move.cpp:278`, `src/vehicle_move.cpp:360`, `src/vehicle.cpp:789`, `src/vehicle.cpp:6908`, `src/vehicle_move.cpp:259`, `src/map.cpp:986`, `src/vehicle.cpp:6260`

(1) `cur_load_alternator = std::min( 0.01f, ... )` caps the term at 1%, at both sites. The idle code's "minimum idle is 1%" suggests `max` was intended, though that is inferred. The controller underestimates alternator fuel cost; the effect on engine choice is small. (2) `drive_to_local_target()` calls `refresh()` every turn for following or patrolling vehicles, and `refresh()` sets `smart_controller_state = std::nullopt`. The 15-second "don't shut a gas engine right after starting it" guard and the "state unchanged" cache therefore never apply to autopilot vehicles. Gas engines can flap on and off, and the full mask search reruns every turn (see VEH-P03).

```cpp
// vehicle_move.cpp:278
float cur_load_alternator = std::min( 0.01f, static_cast<float>( alternator_load ) / 1000 );
// vehicle.cpp:6908-6909
has_enabled_smart_controller = false; smart_controller_state = std::nullopt;
```

**Fix:** Use `std::max` at both sites. Remove the per-turn `refresh()` (VEH-P03), or keep `smart_controller_state` across `refresh()` unless the engine set changed.

### VEH-B19 · Any battery charge/discharge equalises the whole cable grid instantly, with no loss
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/vehicle.cpp:5998`, `src/vehicle.cpp:6087`, `src/vehicle.cpp:6158`, `src/vehicle.cpp:6167`, `src/vehicle.cpp:6179`, `src/vehicle.cpp:6222`

`charge_battery` and `discharge_battery` pool every battery in the cable-connected grid, apply cable loss only to the small delta, then rewrite every battery to its capacity share. A full bank linked to an empty car battery equalises at about 50% as soon as either side draws 1 kJ, so bulk energy crosses the cable with no loss. Cable loss is only 1–5% per hop, so the saving is small, and pooling is a deliberate simplification.

```cpp
// vehicle.cpp:6167, 6179
lost_amount = roll_remainder( amount * loss );
distribute_charge_evenly( batteries, total_charge, total_capacity );
```

**Fix:** Spread only the delta (after loss) over the batteries, or pool within one vehicle and do an explicit lossy transfer step between vehicles. This combines well with the grid-settlement rework in VEH-P05.

## 3. Performance

### VEH-P01 · part_collision() looks up the driver once per structural part per tile moved
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle_move.cpp:889`, `src/vehicle_move.cpp:789`, `src/vehicle_move.cpp:803`, `src/vehicle_move.cpp:806`, `src/vehicle.cpp:3745`, `src/vehicle.cpp:3690`, `src/vehicle.cpp:3693`, `src/vehicle.cpp:289`, `src/map.cpp:1155`

`collision()` calls `part_collision()` for every structural, active-fake and rotor part on every tile step (about 25 steps per turn at 100 mph, up to 10 attempts per step). `part_collision()` calls `get_driver(here)` at the top, but the driver is only used when a creature is hit (lines 1158–1171). `get_driver()` walks all parts through `get_avail_parts(BOARDABLE)` into a new vector, and `player_in_control` adds an uncached string-flag scan for CONTROL_ANIMAL. That is O(P_struct × P) per step: about 30k part checks plus about 100 allocations per step for a 300-part vehicle. Rotors are worse: each runs `part_collision` for every point in its radius (289 points for diameter 16), and every call repeats the driver scan.

```cpp
// vehicle_move.cpp:886-889
Creature *critter = get_creature_tracker().creature_at( p, true );
Character *ph = dynamic_cast<Character *>( critter );
Character *driver = get_driver( here );
```

**Fix:** Look up the driver lazily inside the `veh_coll_body && !just_detect` branch, or compute it once in `collision()` and pass it in. Make `get_driver` use a `boardable` index list built in `refresh()`. For rotors, skip radius points that belong to this vehicle.

### VEH-P02 · part_with_feature/parts_at_relative heap-allocate a vector on every call, including move_cost/pathfinding
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle.cpp:2806`, `src/vehicle.cpp:2828`, `src/vehicle.cpp:3069`, `src/vehicle.cpp:3081`, `src/vehicle.cpp:2843`, `src/vehicle.cpp:2858`, `src/map.cpp:2771`, `src/map.cpp:12101`, `src/pathfinding.cpp:323`, `src/pathfinding.cpp:335`

`parts_at_relative()` returns `std::vector<int>` by value: it copies the cached vector, or builds a filtered one. Both bitflag `part_with_feature` overloads loop over that temporary, so nearly every call on a vehicle tile allocates (163 call sites). Hot chain: `map::move_cost_internal` → `get_movecost` → `obstacle_at_part` → `part_with_feature(OBSTACLE)`, then `part_with_feature(AISLE)`: one or two allocations per vehicle-tile move-cost query. This sits under A* edge costs (`cost_to_pass`, pathfinding.cpp:335), the pathfinding cache rebuild (map.cpp:12101), coverage checks and `do_vehicle_caching`.

```cpp
// vehicle.cpp:2828, 3069
if( include_fake ) { return iter->second; }   // copy
for( const int p : parts_at_relative( vp.mount, /* use_cache = */ true, include_fake ) ) {
```

**Fix:** Add a non-allocating accessor (e.g. `const std::vector<int> *parts_at_relative_cached( const point_rel_ms & ) const`, or a span) and use it in both bitflag overloads, `get_monster`, `get_passenger`, `roof_at_part` and `part_displayed_at`, filtering `is_fake` inline. Optionally replace the `std::map` with an `unordered_map` or a dense grid over the tracked mount bounds.

### VEH-P03 · drive_to_local_target() runs a full vehicle::refresh() every turn for following/patrolling vehicles
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle.cpp:789`, `src/vehicle.cpp:688`, `src/vehicle.cpp:726`, `src/map.cpp:984`, `src/vehicle.cpp:6853`, `src/vehicle.cpp:6909`, `src/vehicle.cpp:7111`, `src/vehicle.cpp:7146`

`map::vehmove` calls `drive_to_local_target()` every turn for every following vehicle (animal carts, magic and autopilot follow), and `autopilot_patrol()` reaches it on every path. Its first action is an unconditional `refresh()`. That rebuilds all index lists, copies `relative_parts`, re-runs `add_fake_part` with uncached string scans (O(P²)), marks mass, air, insides and zones dirty (so those are recomputed downstream), and rebuilds the active item cache by walking all cargo. The function only reads values that `refresh()` already keeps current. It also resets the smart controller every turn (VEH-B18).

```cpp
// vehicle.cpp:789
refresh( );
// vehicle.cpp:7146-7152
check_environmental_effects = true; insides_dirty = true; zones_dirty = true;
coeff_air_dirty = true; invalidate_mass(); ... refresh_active_item_cache();
```

**Fix:** Remove the call. If a stale cache motivated it, do only that specific update, gated on a dirty flag. Verify with the autopilot/follow tests.

### VEH-P04 · Fuel drain invalidates mass, pivot and drag caches every turn, so all cargo is re-weighed per turn
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle.cpp:5368`, `src/vehicle.cpp:5385`, `src/vehicle_part.cpp:401`, `src/vehicle.cpp:4024`, `src/vehicle.cpp:8840`, `src/vehicle.cpp:8851`, `src/vehicle.cpp:7232`, `src/vehicle.cpp:7305`

`consume_fuel()` runs every turn an engine runs. It calls `drain_energy()`, which always ends with `invalidate_mass()`, even when 0 J was drained (common, because `consume_energy` floors). That marks mass, both centre-of-mass flags, pivot, rolling and water dirty. The next `total_mass()` walks every part, cargo item and tool calling `item::weight()` (recursive, with a string-keyed var lookup per item). The next `pivot_point()` re-runs `refresh_pivot()` with a string STEERABLE scan per wheel. While driving, mass and pivot are queried every turn, so a loaded truck pays a full inventory walk and an O(wheels × P) scan per turn for a few grams of fuel.

```cpp
// vehicle.cpp:5368
invalidate_mass(); return drained;
// vehicle.cpp:7305
} else if( part_with_feature( wheel.mount, "STEERABLE", true ) != -1 ) {
```

**Fix:** Touch caches only when `drained > 0`, and don't mark `pivot_dirty` (pivot depends on wheels). Better, subtract the drained fuel mass from `mass_cache` directly. Use the `steering` index list or a VPFLAG bitflag in `refresh_pivot`.

### VEH-P05 · Battery grids: every charge/discharge re-runs the grid search and rewrites every battery via item::ammo_set
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/vehicle.cpp:5869`, `src/vehicle.cpp:6188`, `src/vehicle.cpp:6230`, `src/vehicle.cpp:6052`, `src/vehicle.cpp:6087`, `src/vehicle.cpp:5969`, `src/vehicle.cpp:5938`, `src/vehicle.cpp:1207`, `src/vehicle_part.cpp:355`, `src/item_gun_tool_ammo.cpp:306`

`power_parts()` calls `charge_battery`/`discharge_battery` for any vehicle with non-zero net energy this turn. Each call (1) re-runs `search_connected_vehicles`, a Dijkstra-style search over the cable graph that builds `std::map`s, with off-map lookups scanning every part of every vehicle in the submap; (2) sums all grid batteries; and (3) calls `ammo_set()` on every battery. For battery magazines, `ammo_set` copies compatibility sets, unsets the ammo, constructs a new ammo item (new uid) and `put_in`s it. `fuel_left(battery)`, `battery_left`, `is_battery_available` and `connected_battery_power_level` redo the same search. Low-wattage appliances trigger a call on only 1–20% of turns (energy is rolled per kJ), so the cost per turn is lower than the worst case, but it scales with consumers × batteries in appliance bases.

```cpp
// vehicle.cpp:6091-6096
for( ... : batteries ) { ... vp.ammo_set( fuel_type_battery, portion );
```

**Fix:** (a) Cache the connected-vehicle map per turn, keyed by a global cable-topology generation counter. (b) In `distribute_charge_evenly`, skip unchanged batteries and adjust charges in place instead of `ammo_set`. (c) Longer term, collect net energy per vehicle and settle each grid once per turn in `map::vehmove` (which already walks grids). See VEH-B19 for the loss model.

### VEH-P06 · fuel_left()/is_perpetual_type() build a full temporary item just to test PERPETUAL, many times per turn
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle.cpp:3950`, `src/vehicle.cpp:1069`, `src/vehicle.cpp:4075`, `src/vehicle.cpp:4093`, `src/vehicle.cpp:1117`, `src/vehicle.cpp:1140`, `src/vehicle_move.cpp:635`, `src/item.cpp:177`

`fuel_left()`, `is_perpetual_type()` and `consumption_per_hour()` construct a full `item` (new uid, contents, vars, prefix/suffix flags) just to test PERPETUAL, although `itype::has_flag` exists. `total_power(fueled)` does this per engine and again per alternator, and is reached about 12 times per `thrust()`. `thrust()` calls `strain()` twice on one line. The smart controller repeats this for up to 31 engine masks. `part_vpower_w` also does a linear `index_of_part()` scan on every call, though only the animal and muscle branches need it. The cost is real but small.

```cpp
// vehicle.cpp:3950
} else if( item( ftype ).has_flag( flag_PERPETUAL ) ) {
// vehicle_move.cpp:635
const int strn = static_cast<int>( strain( here ) * strain( here ) * 100 );
```

**Fix:** Use `ftype->has_flag( flag_PERPETUAL )` at all three sites. Hoist `strain(here)` into a local. Compute the part index only in the animal/muscle branches (or use `&vp - parts.data()`).

### VEH-P07 · String-flag part_with_feature(point, string) ignores the relative_parts cache, making several loops O(P²)
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle.cpp:3090`, `src/vehicle.cpp:3093`, `src/vehicle.cpp:2806`, `src/vehicle.cpp:7007`, `src/vehicle.cpp:7029`, `src/vehicle.cpp:7061`, `src/vehicle.cpp:7786`, `src/vehicle.cpp:7930`, `src/vehicle_move.cpp:995`, `src/vehicle.cpp:2248`, `src/vehicle.cpp:9144`, `src/map.cpp:1194`, `src/vehicle.cpp:789`

The `std::string` overload of `part_with_feature` calls `parts_at_relative(pt, use_cache=false)`, a linear scan of all parts comparing mounts. It runs inside per-part or per-mount loops: STEERABLE per steerable part in `refresh()` (7007, redundant because `vp` itself qualifies), TURRET_CONTROLS per turret, `need_fake_part` four times per mount, ROOF per part in `refresh_insides`, SHOCK_ABSORBER per part in `damage_all` (once per collision), PROTRUSION in `part_collision`, `remove_part` and `refresh_zones`. The cost is O(M×P) cheap mount compares (flag lookups only for co-located parts). These paths mostly run on part changes, not every turn, except through VEH-P03.

```cpp
// vehicle.cpp:3093
for( const int p : parts_at_relative( pt, /* use_cache = */ false, include_fake ) ) {
// vehicle.cpp:7007
if( ( vpi.has_flag( "STEERABLE" ) && part_with_feature( pt, "STEERABLE", true ) != -1 ) ||
```

**Fix:** Use the cache when it is valid. Add a validity flag, because `remove_part` and `suspend_refresh()` leave it stale. Convert hot callers to bitflags (ROOF, OBSTACLE, CURTAIN, TURRET_CONTROLS, STEERABLE, PROTRUSION, SHOCK_ABSORBER). Drop the redundant STEERABLE check.

### VEH-P08 · Tile drawing: each vehicle tile rescans the whole vehicle when the player is in a vehicle
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/cata_tiles.cpp:4201`, `src/cata_tiles.cpp:1044`, `src/vehicle_display.cpp:66`, `src/vehicle_display.cpp:114`, `src/vehicle.cpp:3568`, `src/vehicle.cpp:3573`, `src/vehicle.cpp:3717`, `src/vehicle.cpp:3690`, `src/vehicle.cpp:293`, `src/map.cpp:8248`

`draw_vpart` (and `map::drawsq`) calls `get_display_of_tile()` and `player_in_control()` for every visible vehicle tile. With default `roof=true`, `part_displayed_at()` copies the mount vector and calls `is_passenger(player)`. While the player is in any vehicle, that runs `boarded_parts()`, a full part scan with allocation, for every visible vehicle. It also calls `get_option<bool>("VEHICLE_ARMOR_COLOR")` per tile. `player_in_control()` starts with `tags.count( "IN_CONTROL_OVERRIDE" )`, a 19-char temporary string (heap allocated) per call. Both roof layers repeat this. Redraws happen per input or turn, so the cost is about 1 ms at most.

```cpp
// vehicle.cpp:3570-3574
bool in_vehicle = !roof;
if( roof ) { in_vehicle = is_passenger( get_player_character() ); }
// vehicle.cpp:293
if( tags.count( "IN_CONTROL_OVERRIDE" ) ) {
```

**Fix:** Decide "player is aboard this vehicle" in O(1) (`pc.in_vehicle && veh_pointer_or_null( get_map().veh_at( pc.pos_bub() ) ) == this`), or pass a per-frame flag. Iterate the cached vector by reference. Cache the option. Replace the tag lookup with a bool member, or use `std::set<std::string, std::less<>>` with a `string_view`.

### VEH-P09 · power_network_manager is rebuilt every turn for every vehicle in the bubble but is write-only
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/power_network.h:19`, `src/power_network.cpp:93`, `src/power_network.cpp:107`, `src/power_network.cpp:150`, `src/map.cpp:844`, `src/map.cpp:885`, `src/map.cpp:1044`, `src/savegame.cpp:1884`

`map::resolve_off_map_grid_generation()` runs every turn and calls `pnm.add_grid()` for every grid in the bubble, singletons included. Each call allocates a network, a nodes vector (with rated-output and battery scans), an overlap map and a map entry, and `begin_rebuild`/`finish_rebuild` rebuild and move the whole map. The header says this is "Write-only infrastructure for phase 3"; `find_network_at` has no callers outside tests. The only consumer is savegame serialization, which writes one network per vehicle into every save. The BFS itself is still needed for off-map renewables, so the savings are minor.

```cpp
// power_network.h:19-21
// within a power network. Write-only infrastructure for phase 3 --
// not used for runtime generation decisions yet.
```

**Fix:** Build the snapshot lazily at save time (or when the cable-topology counter changes). Skip single-vehicle grids with no batteries, renewables or cables.

### VEH-P10 · init_state() deep-copies vpart_info per part on every vehicle spawn
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle.cpp:456`, `src/mapgen.cpp:6890`

In the non-pristine spawn path (the mapgen default), the light-randomisation loop copies `vpart_info` by value. It is a large type with many sets, maps, vectors and optional slots, so each copy costs dozens of allocations. A parking lot with 30 cars × 100 parts makes about 3,000 copies, a one-time mapgen cost of a few milliseconds.

```cpp
// vehicle.cpp:455-456
for( vehicle_part &vp : parts ) {
    const vpart_info vpi = vp.info();
```

**Fix:** `const vpart_info &vpi = vp.info();`. Consider enabling clang-tidy `performance-unnecessary-copy-initialization`.

### VEH-P11 · idle(), on_move(), thrust() and lights() do full part scans with string flags every turn or tile
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle.cpp:6327`, `src/vehicle.cpp:3144`, `src/vehicle.cpp:6346`, `src/vehicle.cpp:6362`, `src/vehicle.cpp:6376`, `src/vehicle_move.cpp:663`, `src/vehicle_move.cpp:1482`, `src/vehicle.cpp:5505`, `src/lightmap.cpp:789`, `src/map.cpp:1041`

For every vehicle in the bubble every turn, `idle()` calls `has_parts( { "STEREO", "CHIMES", "CRASH_TERRAIN_AROUND" }, true )`. That builds a vector of three strings and scans every enabled, unbroken part (nearly all parts) with set lookups. It also always runs `get_parts_at(player_pos, "CONTROLS")`, even without turrets. `on_move()` runs a four-string `has_parts` per tile and builds a `vehicle_moves` event that calls `is_passenger()`. `thrust()` and `get_harnessed_animal` loop over all parts instead of `engines`. `lights()` rescans and allocates per lightmap build. In practice only `idle()` matters, at a few microseconds per vehicle per turn.

```cpp
// vehicle.cpp:6327
tiny_bitset flags = has_parts( { "STEREO", "CHIMES", "CRASH_TERRAIN_AROUND" }, true );
```

**Fix:** In `refresh()`, build index lists (stereo, chimes, crash_terrain, transform_terrain, scoop, reaper, lights, boardable) next to `planters`/`emitters`, and check only those. Compute `player_at_controls` only when there are turrets. Iterate `engines` in `thrust()`/`get_harnessed_animal`.

### VEH-P12 · Part removal/damage triggers repeated full refreshes (double refresh in cleanup, one per broken part)
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle.cpp:2339`, `src/vehicle.cpp:2376`, `src/vehicle.cpp:2389`, `src/vehicle.cpp:7969`, `src/vehicle.cpp:7992`, `src/vehicle.cpp:8000`, `src/vehicle.cpp:8223`, `src/vehicle.cpp:3090`, `src/vehicle.cpp:6836`, `src/map.cpp:1014`

Each part destroyed in a collision calls `refresh(false)` from `damage_direct()`. Each removal calls it from `remove_part()`, after four `remove_dependent_part` string scans. At end of turn, `part_removal_cleanup()` runs `refresh()` and then `refresh(false)` again with nothing in between that changes part state. `shift_if_needed()` refreshes after `shift_parts()`, which already refreshed. A crash that breaks 10 parts costs 13 or more full refreshes. This only happens on collisions and removals.

```cpp
// vehicle.cpp:2375-2389
if( changed || parts.empty() ) { refresh( ); ... }
shift_if_needed( here ); ...
refresh( false ); // Rebuild cached indices
```

**Fix:** Skip the second refresh when the first ran and no shift happened (have `shift_if_needed` return whether it refreshed). Remove the duplicate refresh after `shift_parts()`. Longer term, split `refresh` into a cheap reindex and a full path, and defer to the existing `part_removal_cleanup` pass.

### VEH-P13 · part_with_tool()/prepare_tools() construct items and a std::map<item,int> just to compare typeIds
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/vehicle.cpp:2884`, `src/vehicle.cpp:2905`, `src/vehicle.cpp:6597`, `src/veh_type.cpp:1135`, `src/vehicle_use.cpp:2205`, `src/inventory_ui.cpp:2296`, `src/inventory_ui.cpp:2328`, `src/crafting.cpp:1602`, `src/vehicle.cpp:8694`

`part_with_tool` and `part_with_unreserved_tool` build the full tool map from `prepare_tools()` only to compare `typeId`. `prepare_tools` copies the pseudo-tool `std::set` by value, constructs and copies items, and calls `prepare_tool` on each. That calls `tool_ammo_available`, which runs `battery_left`/`fuel_left` and so the battery-grid search. Callers include the inventory UI (`water_faucet` for every tile in PICKUP_RANGE), crafting provider enumeration and the funnel code while raining. The cost is situational.

```cpp
// vehicle.cpp:2884-2888
const std::map<item, int> tools = vehicle().prepare_tools( here, vp.part() );
if( std::find_if( tools.begin(), tools.end(), [&tool_type]( ... ) { return pair.first.typeId() == tool_type;
```

**Fix:** Implement both as type-only checks: iterate `vp.info().pseudo_tools` by const reference (keeping the NEEDS_SUNLIGHT gate) and `vp.tools` by `typeId()`, constructing no items. Make `get_pseudo_tools()` return a const reference.

## Appendix: considered and rejected

- **is_external_part(mount) is always true, so tow lines attach to internal parts** – The mount overload is broken (it tests an int for truth and skips `coord_translate`), but tow lines cannot reach internal parts. `link_tow_cable` and `choose_adjacent_highlight` use the correct map-based overload, and the only other caller re-links an already-valid tow point. Worth a one-line cleanup, not a user-visible bug.
- **precollision_check() is O(P²) per creature on the autopilot path** – The loop is as described, but it runs only for autopilot/follow vehicles and only when a creature is on a path tile. Any non-pet creature stops the search at once, so it costs about 10k pointer compares at most on a rare path, which is negligible.
