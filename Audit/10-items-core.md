# 10 · Item core, item types, groups, materials & faults

> Engine audit – static code reading at commit `a1a646d6`; nothing was compiled or run. Each finding was checked against the code by a separate reviewer: **confirmed** = verified by reading the code; **plausible** = consistent with the code, but reachability or impact not proven. Line numbers refer to that commit.

**Scope:** `src/item.cpp`, `src/item_factory.cpp`, `src/itype.cpp`, `src/item_group.cpp`, `src/flag.cpp`, `src/flag_hot_bits.cpp`, `src/material.cpp`, `src/fault.cpp`, `src/ammo.cpp`, `src/ammo_effect.cpp`, `src/item_category.cpp`, `src/item_components.cpp`, `src/item_transformation.cpp`, `src/item_uid.cpp`, `src/clothing_mod.cpp`, `src/relic.cpp`, `src/item_degrade.cpp`

## Summary

The item core works, but it carries a lot of hand-written loading and hardcoded behaviour. Lists such as faults, relic effects and clothing-mod values only ever append under copy-from. Faults, spoilage, revival, smoking and artifact recharging are decided by C++ constants and type strings rather than data. EOCs can add faults and change degradation, but often to no effect, and they cannot read or write most per-item state. The most serious bugs are silent data loss: save RLE merges items that are not identical, out-of-bubble catch-up leaves frozen food unrotted, and `convert()` collapses stacks. Several finalize steps are skipped for guns because of an early `return`. On the performance side, the clear win is a one-line guard that stops `Item_factory::init()` from running once per ITEM object at load. Most of the rest is allocation churn on item construction and in per-tick temperature processing.

| Lens | Critical | High | Medium | Low | Total |
|---|---|---|---|---|---|
| Missing JSON / EOC | 0 | 0 | 8 | 6 | 14 |
| Bugs & quirks | 0 | 2 | 8 | 8 | 18 |
| Performance | 0 | 1 | 3 | 12 | 16 |
| **Total** | 0 | 3 | 19 | 26 | 48 |

**Fix first:**
1. **ITEM-B01** · Out-of-bubble catch-up uses stale FROZEN/COLD flags: a small change closes a one-way food-preservation exploit.
2. **ITEM-P01** · `items::load` re-runs `Item_factory::init()` per ITEM object: a one-line guard removes millions of redundant allocations from every world load.
3. **ITEM-B02** · Save RLE merges non-identical map items: real save corruption (ownership, uids, snippets, birthdays). It is medium effort, but it is the highest-impact bug here.
4. **ITEM-B05** · Whitelist finalization erases from a `std::set` while iterating it: undefined behaviour at load, and the fix is one line.
5. **ITEM-B03** · `finalize_post` returns early for guns, and NO_FAULTS runs before fault groups expand: a small fix repairs armor data, pocket flags, fault groups and the NO_FAULTS option.

## 1. Missing JSON bindings & EOC features

### ITEM-J01 · EOC fault effects: "message" never works, a wrong talker crashes, faults can't be read or removed
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctalk.cpp:6282`, `src/npctalk.cpp:6295`, `src/npctalk.cpp:8677`, `src/talker.h:785`, `src/talker_item.h:72`, `src/item_degrade.cpp:364`, `src/item_degrade.cpp:376`, `doc/JSON/EFFECT_ON_CONDITION.md:5403`

The EOC fault effects have three problems.
1. `f_set_fault` and `f_set_random_fault_of_type` dereference `*d.actor(is_npc)->get_item()` without a null check. The sibling `f_set_flag` checks and calls debugmsg. Writing `u_set_fault` when `u` is the avatar therefore dereferences nullptr.
2. The documented `"message"` option (default true) takes the holder from `get_character()` on the item talker. `talker_item` does not override that function, so the holder is always null and the message never prints.
3. Faults are write-only. EOCs have no has_fault condition, no fault-count math and no remove effect.

The crash needs misuse, because the doc lists Item as the only valid talker. Repair-kit style mending is already possible through `fault_fix`.

```cpp
item_location &it = *d.actor( is_npc )->get_item();
const Character *holder = msg ? d.actor( is_npc )->get_character() : nullptr;  // always nullptr
it.get_item()->set_fault( fault_id( fault_var.evaluate( d ) ), force, holder );
```

**Fix:** Null-check the talker and debugmsg, as `f_set_flag` does. Use `it.carrier()` as the holder when `"message"` is true. Add the condition `{ "npc_has_fault": "fault_x" }` / `{ "npc_has_fault_of_type": "shorted" }`, the math function `n_fault_count('shorted')`, and the effects `{ "npc_remove_fault": "fault_x" }` / `{ "npc_remove_fault_of_type": "wet", "all": true }`. Back them with the existing `item::remove_fault` and `remove_single_fault_of_type`.

### ITEM-J02 · Item degradation only works for category veh_parts; the documented field and EOC writes do nothing elsewhere
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_factory.cpp:4519`, `src/item_factory.cpp:4522`, `src/item_degrade.cpp:199`, `src/item_degrade.cpp:278`, `src/item_degrade.cpp:846`, `src/math_parser_diag.cpp:644`, `src/activity_actor.cpp:11246`, `doc/JSON/ITEM.md:133`

`itype::load` forces `degrade_mult = 0`, and so `degrade_increments_ = 0`, for every count-by-charges item and every item that is not `"category": "veh_parts"`. The gate is marked as a TODO. With increments at 0, `set_degradation()` stores 0, `rand_degradation()` returns early, and `get_degrade_amount()` returns 0. The documented `degradation_multiplier` ("Defaults to 1.0") is ignored, the assignable EOC math `degradation()` silently writes 0, and fault_fix `mod_degradation` does nothing. The field is also read with `was_loaded=false`. The derived increments are inherited through copy-from and then divided again by the child's multiplier. A veh_parts child of a non-veh_parts parent ends up with increments = 1, which means extremely fast degradation.

```cpp
float degrade_mult = 1.0f;
optional( jo, false, "degradation_multiplier", degrade_mult, 1.0f );
// TODO: remove condition once degradation is ready to be applied to all items
if( count_by_charges() || category_force != item_category_veh_parts ) { degrade_mult = 0.0f; }
```

**Fix:** Honour an explicit `degradation_multiplier` and only default to 0 for non-veh_parts items when the field is absent, or remove the gate. Read the field with `was_loaded`, and store the multiplier rather than the derived increments so copy-from does not compound. Document the default. Warn in `check()` when an item sets the multiplier but is forced to 0.

### ITEM-J03 · Item "faults" and other hand-parsed lists only append under copy-from (`"faults": []` does nothing)
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/item_factory.cpp:4573`, `src/item_factory.cpp:4579`, `src/item_factory.cpp:3778`, `src/item_factory.cpp:3104`, `src/item_factory.cpp:3108`, `src/relic.cpp:414`, `src/relic.cpp:420`, `src/relic.cpp:427`, `src/relic.cpp:445`, `src/fault.cpp:292`, `src/fault.cpp:435`, `src/clothing_mod.cpp:71`, `data/json/items/gun/12mm.json:31`, `data/json/items/gun/nail.json:55`, `data/json/items/relics/extradimensional_artifacts.json:24`, `data/json/items/vehicle/engine.json:112`, `data/mods/aftershock_exoplanet/items/armor/exosuit/exosuit_frame.json:611`

`itype::load` parses `"faults"` by hand and appends each entry to `faults` or `fault_groups` without clearing either. Copy-from has already copied the parent's lists, so a child can add faults but cannot replace or remove one, and `extend` and `delete` are ignored for this key. Vanilla relies on `"faults": []` to drop inherited faults, and that silently fails:
- `coilgun` and `hk_g80` keep `gun_base_rifle_semi`'s six powder-gun faults. hk_g80 is mostly protected by NEVER_JAMS and similar flags, but coilgun can jam and can spawn dirty.
- `mp3_reverberation` keeps mp3's `electronic_general` group.
- `1cyl_combustion_small` keeps its parent's engine faults.

The same append-only behaviour affects comestible `consumption_effect_on_conditions`, fault `melee_damage_mod`/`armor_mod`, fault_group `group` and clothing_mod `mod_value`. It also affects relics: `load_slot` keeps the copied relic slot, and `relic::load` appends `passive_effects`/`active_effects` and resets `moves` to 100 when the key is absent. In aftershock, `modular_exosuit_light_on` stacks the base STR+10, `ench_exo_strength` (STR+10) and its own STR+5, for STR+25 total.

```cpp
if( jo.has_array( "faults" ) ) {
    for( JsonObject jo_f : jo.get_array( "faults" ) ) {
        ... faults.add( fault_id( jo_f.get_string( "fault" ) ), fault_weight );
        ... fault_groups.emplace_back( ... );   // never cleared
```

**Fix:** When the key is present, an explicit key replaces the inherited value (clear `faults`, `fault_groups` and `consumption_eocs`). Support `"extend": { "faults": [ { "fault_group": "handles" } ] }` and `"delete": { "faults": [ "fault_gun_blackpowder" ] }` through a `generic_typed_reader`, as `item_quality_reader` does. Apply the same rule to relic effects (and read `moves`, `charge_info` and `activation_requirement` with `optional(..., was_loaded, ...)`), fault mods, fault_group entries and clothing_mod `mod_value`. Optionally warn when an empty array is used on a child that has inherited entries.

### ITEM-J04 · EOCs cannot read or write an item's charges/ammo, temperature, rot, wetness or relic charges
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/math_parser_diag.cpp:223`, `src/condition.cpp:1962`, `src/condition.cpp:2221`, `src/condition.cpp:2409`, `src/condition.cpp:2453`, `src/condition.cpp:2523`, `src/talker_item.cpp:100`, `src/talker_item.cpp:161`, `src/npctalk.cpp:6357`

`talker_item` exposes vars, flags, damage, degradation, power, weight, volume, price and quality. It does not expose the state that item processing updates every turn:
- No math function for the item's own non-battery charges or ammo. `charge_count()` counts a character's inventory.
- No item temperature or frozen state. The only `temperature` in math is the weather.
- Rot is visible only as the boolean `is_rotten`.
- No wetness, relic charges or countdown, and no setters for any of these.
- `has_ammo`, `is_rotten` and `transform_item` work only with the beta talker.

Battery charge already works through the legacy `val('power')`, which reads `ammo_remaining()` and is assignable. A preserve-food spell, a heating rune or a ritual that spends charges from a non-battery focus cannot be written.

```cpp
// math_parser_diag.cpp:226 – counts the character's inventory, not the item
return d.const_actor( is_beta( scope ) )->charges_of( itype_id( params[0].str( d ) ) );
// condition.cpp:2227 – beta only
return ( *it )->get_relative_rot() > 1;
```

**Fix:** Add item-scoped math functions, assignable where it makes sense: `n_charges()`, `n_ammo_remaining('pocket_id'?)`, `n_temperature()` (Kelvin, assigned via `set_item_temperature`), `n_freshness()` (`get_relative_rot`, assigned via `set_relative_rot`), `n_wetness()`, `n_relic_charges()` and `n_countdown()`. Make `has_ammo`, `is_rotten` and `transform_item` accept the `u_`/`npc_` prefixes.

### ITEM-J05 · Artifact activation fires no event; relic recharge is a closed hardcoded enum
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/relic.cpp:498`, `src/relic.cpp:560`, `src/relic.cpp:630`, `src/item.cpp:4459`, `src/avatar.cpp:1474`, `src/event.h:207`, `doc/JSON/EFFECT_ON_CONDITION.md:1849`, `doc/JSON/ARTIFACTS.md:30`

EFFECT_ON_CONDITION.md documents the `activates_artifact` event, and event.h defines it, but no code sends it. `relic::activate` casts its spells and spends charges without notifying the event bus, so EOCs keyed on this event never run. Recharging is a switch over `relic_recharge_type` (periodic, solar, lunar, moon phases, forest, underground, portal_storm), and portal_storm compares against a hardcoded weather id. Mods cannot recharge on blood, in a lab, during a mod weather or on a global var. ARTIFACTS.md also notes that mods cannot control artifact secrecy.

```cpp
caster.mod_moves( -moves );
for( const fake_spell &sp : active_effects ) { ... }
charge.charges -= charge.charges_per_use;   // no event sent
...
if( get_weather().weather_id == weather_portal_storm ) {
```

**Fix:** Send `activates_artifact` from `relic::activate`, adding an `itype_id` field to the spec. Add `"recharge_type": "condition"` with `"recharge_condition_eoc": <dialogue condition>`, evaluated with the item as alpha and the carrier as beta. Replace the portal_storm case with `"recharge_weather": [ "portal_storm" ]`. Add `"on_activate_eocs"` to relic data.

### ITEM-J06 · Rot, storage temperature and corpse-revival timings are hardcoded constants
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/item_degrade.cpp:452`, `src/item_degrade.cpp:600`, `src/item_degrade.cpp:1277`, `src/item_degrade.cpp:1312`, `src/item_degrade.cpp:1334`, `src/map.cpp:6646`, `src/vehicle_part.cpp:476`, `src/item.cpp:2661`, `src/item.cpp:2669`, `src/item.cpp:2676`, `src/item.cpp:2681`, `src/item.cpp:4485`, `src/item.cpp:4506`

These spoilage and revival values are fixed in C++:
- Every corpse has a 24 h shelf life and rots away after 10 days.
- FIELD_DRESS ×0.75, MUSHY ×3 and IRRADIATED ×0.25 are flag-specific rot multipliers.
- Carried items get +5 °F and ×1.5 insulation.
- Storage temperature is a closed enum (HEATER, FRIDGE, FREEZER, ROOT_CELLAR), and the root cellar is keyed to terrain `t_rootcellar`.
- Revival needs an age over 6 h with a 48 h ramp, is checked once per second, and REVIVE_SPECIAL uses a distance of 3.
- DORMANT corpses need the trap `tr_dormant_corpse`, and revival messages branch only on species ROBOT.

`mtype` loads `revive_forms` but no timing. Pocket `spoil_multiplier` and furniture `NO_SPOIL` are partial workarounds.

```cpp
} else if( is_corpse() ) { return 24_hours; }
return get_rot() > 10_days;
int rez_factor = 48 - age_in_hours; ... if( age_in_hours > 6 && ...
here.trap_set( pos, trap_id( "tr_dormant_corpse" ) );
```

**Fix:** Add mtype fields `"corpse_shelf_life": "24 h"`, `"corpse_rot_away": "10 d"`, `"revive_delay": { "min": "6 h", "guaranteed": "48 h" }` and `"revive_message"`. Add a json_flag field `"rot_multiplier": 0.25` so IRRADIATED, MUSHY, FIELD_DRESS and mod flags are all data. Add a `temperature_profile` JSON type (min/max clamp) that furniture and vehicle-part flags reference instead of `temperature_flag`.

### ITEM-J07 · Item groups: only real-world holidays gate entries, with no condition and no way to delete entries
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/item_factory.cpp:4982`, `src/item_factory.cpp:5023`, `src/item_factory.cpp:5134`, `src/item_factory.cpp:5160`, `src/item_group.cpp:244`, `src/item_group.cpp:500`, `src/item_group.cpp:975`, `src/item_group.cpp:989`, `doc/JSON/ITEM_SPAWN.md:100`, `doc/JSON/ITEM_SPAWN.md:138`

The only per-entry gate is `"event"`, a `holiday` enum checked against the system clock. There is no dialogue condition, so a mod cannot spawn loot only after day 30, only in winter, only when a global var is set, or only while a faction is hostile. Group copy-from works only for the same id, and only `extend` is supported. There is no `delete` and no way to change the prob of an existing entry. A mod can redefine the whole group, but that is brittle. Some behaviour is hardcoded: VARSIZE items get FIT with a 1/3 chance, and default gun dirt is 0–500. The loaded `dirt` modifier is missing from ITEM_SPAWN.md.

```cpp
holiday event = obj.get_enum_value<holiday>( "event", holiday::none );
if( event == get_holiday_from_time() ) {
// We can only copy-from a group with the same id (for now)
if( one_in( 3 ) && tmp.has_flag( flag_VARSIZE ) ) { tmp.set_flag( flag_FIT ); }
```

**Fix:** Add a per-entry `"condition": { "math": [ "time_since('cataclysm', 'unit':'days') > 30" ] }`, evaluated at spawn with the avatar as `u` and cached per call. Add `"delete": { "items": [ "foo" ], "groups": [ "bar" ] }` and an `"override"` for the prob of existing entries. Add `"fit_chance"` (default 33). Document `dirt`.

### ITEM-J08 · Fault triggers and recovery are hardcoded fault-type strings; faults have no EOC hooks
**Severity:** Medium · **Effort:** Large · **Verified:** confirmed
**Where:** `src/item.cpp:4799`, `src/item.cpp:4908`, `src/item_degrade.cpp:873`, `src/item_group.cpp:520`, `src/fault.h:92`, `src/fault.cpp:91`, `src/fault.cpp:269`, `src/fault.cpp:316`, `src/activity_actor.cpp:11223`

Faults are JSON objects, but C++ decides when they apply and recover:
- WATER_BREAK uses the type strings "wet" and "shorted", which also appear in game.cpp:9097 and avatar_action.cpp:1205.
- `mod_damage` makes one "mechanical_damage" roll per 1000 damage, with a TODO saying so.
- `fault_emp_reboot` heals with a 1/60 chance per turn, and in 1/20 of those cases it falls through to "shorted" instead.
- Spawned guns get `fault_gun_dirt`, or `fault_gun_unlubricated` with a 1/10 chance.

`fault` and `fault_fix` have no trigger, self-repair or EOC fields, and applying a fix runs no EOC. `fault_type` and fault flags are free strings that are never validated. `faults::reset`, `finalize` and `check_consistency` never touch `fault_group_factory`, so groups persist across world loads, and groups that no item uses are never validated.

```cpp
set_random_fault_of_type( "wet", true ); if( has_flag( flag_ELECTRONIC ) ) { set_random_fault_of_type( "shorted", true );
if( one_in( 60 ) ) { if( !one_in( 20 ) ) { remove_fault( fault_emp_reboot ); ...
// TODO: think about better way to telling the game what faults should be applied when ...
```

**Fix:** Add to fault: `"triggers": [ { "on": "damage", "damage_types": ["bash"], "per_damage": 1000, "chance": 100 }, { "on": "submerged" }, { "on": "emp" } ]`, `"recovery": { "interval": "1 m", "chance": 0.95, "on_fail_fault_type": "shorted" }`, and `"eoc_on_add"` / `"eoc_on_remove"` (item as alpha, holder as beta). Add `"condition"` and `"effect_on_conditions"` to fault_fix. Register `fault_type` as a validated id. Reset, finalize and check `fault_group_factory`. Make spawn-time gun dirt an item_group modifier default.

### ITEM-J09 · Material-implied item flags (allergens, CARNIVORE_OK, honey, hemovore) are a C++ table
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_factory.cpp:3918`, `src/item_factory.cpp:3968`, `src/item_factory.cpp:796`, `src/item_factory.cpp:526`, `src/item.cpp:1251`, `src/character_attire.cpp:138`, `src/material.cpp:83`, `src/consumption.cpp:827`

`Item_factory::set_allergy_flags` maps 22 material ids to ALLERGEN_*, CARNIVORE_OK, URSINE_HONEY and HEMOVORE_FUN in a fixed 29-entry array. `material_type` has no flags field, so a mod material cannot imply these flags. `hflesh` is silently rewritten to `flesh` on every item, before default vitamins are derived. The impact is limited: diet checks mostly use allergen vitamins that items declare themselves, and many ALLERGEN_* flags are read nowhere. Only the vegan check, ALLERGEN_JUNK, CARNIVORE_OK, URSINE_HONEY and HEMOVORE_FUN still depend on the table, and items can declare those flags directly.

```cpp
static const std::array<std::pair<material_id, flag_id>, 29> all_pairs = { { { material_hflesh, flag_ALLERGEN_MEAT }, ... } };
mats.emplace( "flesh", ports );
```

**Fix:** Add `"implied_item_flags": [ "ALLERGEN_MEAT", "CARNIVORE_OK" ]` (and optionally `"monotonous": false`) to material JSON, move the table into `data/json/materials.json`, and replace `set_allergy_flags` with a loop over the item's materials. Drop the hflesh migration or make it a JSON material migration.

### ITEM-J10 · The item "properties" bag is untyped, unvalidated and mostly undocumented (DECAYS_IN_AIR)
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_factory.cpp:4593`, `src/item.cpp:2447`, `src/item_degrade.cpp:644`, `src/item_degrade.cpp:1344`, `doc/JSON/ITEM.md:1607`, `data/json/items/comestibles/drink.json:1686`, `src/trapfunc.cpp:887`, `src/iuse.cpp:7882`, `src/ranged.cpp:1667`

`properties` is a free-form `map<string,string>`. C++ reads four keys from it: `burst_when_filled`, `max_air_exposure_hours`, `creature_size_capacity` and `capture_species`. Only `burst_when_filled` is documented, and DECAYS_IN_AIR has no JSON_FLAGS.md entry. Values are integer hour strings, not time units. A typo in a key is silently ignored. A DECAYS_IN_AIR item without the property gets 0 h and transforms at its first temperature update. EOCs cannot read properties at all.

```cpp
int64_t max_air_exposure_hours = decays_in_air ? get_property_int64_t( "max_air_exposure_hours" ) : 0;
```

**Fix:** Promote the keys to typed itype fields: `"air_exposure_limit": "30 d"` (required with DECAYS_IN_AIR), `"burst_when_filled": 75`, and `"capture": { "species": [...], "max_size": "MEDIUM" }`. Keep `properties` for mod data only, add a math accessor `n_property('key')`, and document the flag.

### ITEM-J11 · Docs drift: MAGIC.md relic_data, ITEM.md legacy artifact fields, transform_into target_ammo/target_group
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `doc/JSON/MAGIC.md:871`, `doc/JSON/MAGIC.md:878`, `doc/JSON/MAGIC.md:893`, `doc/JSON/MAGIC.md:906`, `doc/JSON/ITEM.md:985`, `doc/JSON/ITEM.md:991`, `doc/JSON/ITEM.md:1095`, `doc/JSON/ITEM.md:1161`, `doc/JSON/ARTIFACTS.md:86`, `src/item_transformation.cpp:42`, `src/item_factory.cpp:3012`, `src/item_factory.cpp:3170`, `src/relic.cpp:414`, `src/init.cpp:367`

Several item docs describe things the loader does not do:
- MAGIC.md nests enchantments in `"relic_data": {...}` on a `TOOL_ARMOR` type. The loader registers only `ITEM` and reads relic fields at top level under the ARTIFACT subtype, and nothing in data uses `relic_data`.
- ITEM.md still documents `effects_carried`, `effects_worn` and the other AEP_/AEA_ fields, and points to the removed `src/artifact.h`.
- ITEM.md says `target_ammo` is a number of charges, but the code reads an `itype_id`.
- A `transform_into` with only `target_group` fails `check_definitions`, because the empty `target` fails `has_template`.
- ARTIFACTS.md omits the `underground` and `portal_storm` recharge types.

```cpp
optional( jo, false, "target_ammo", ammo_type );   // itype_id ammo_type
if( type->transform_into && ( !has_template( type->transform_into.value().target ) ...
```

**Fix:** Rewrite MAGIC.md's relic section to the top-level ARTIFACT format, documenting `active_effects`, `activation_requirement`, `moves`, `artifact_name` and `charges_per_activation`. Delete the legacy effects_* section from ITEM.md and fix the `target_ammo` description. Make `check_definitions` accept `target_group` and validate the group id. Add the missing recharge types to ARTIFACTS.md.

### ITEM-J12 · clothing_mod value types are a hardcoded enum, not damage_type ids, with no consistency check
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/clothing_mod.h:20`, `src/clothing_mod.cpp:62`, `src/clothing_mod.cpp:85`, `src/item_armor.cpp:1114`, `src/init.cpp:500`, `src/init.cpp:829`

Damage types are defined in JSON, but `mod_value.type` only accepts acid, fire, bash, cut, bullet, encumbrance and warmth. `get_clothing_mod_val_for_damage_type` maps only five damage types, under a FIXME. A mod's cold, electric or stab resistance patch cannot be expressed. Clothing mods have no `check_consistency`, so a bad `flag` or `item` id goes unreported. `mod_value` also appends under copy-from (see ITEM-J03).

```cpp
// FIXME: Hardcoded damage types
if( dmg_type == damage_bash ) {...} else if( dmg_type == damage_acid ) {...}
return 0.0f; // No mods for other damage types
```

**Fix:** Change mod_value to `{ "damage_type": "cold", "value": 1, "proportion": [...] }` (a `damage_type_id`), keeping encumbrance and warmth as special keys. Add `clothing_mod::check()` for the flag, item and damage_type ids.

### ITEM-J13 · Lit smokables (LITCIG) hardwire effects, traits, joint_lit and fd_weedsmoke
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/item.cpp:4560`, `src/item.cpp:4572`, `src/item.cpp:4607`, `src/item.cpp:4614`, `src/item.cpp:4621`, `src/item.cpp:4669`, `src/item.cpp:4690`, `src/item.cpp:4859`

`process_litcig` gives `cig` if the item has TOBACCO, and `weed_high` otherwise. Puff duration is 15 s (7 s with TOLERANCE, 30 s with LIGHTWEIGHT) and puff chance is 1 in 10. Drop chances are fixed for shakes and JITTERY. Only `joint_lit` gets a last puff and a `fd_weedsmoke` field. `process_extinguish` hardcodes the rain odds and `windpower > 5`. A mod incense stick or herbal cigarette cannot use LITCIG without being forced into one of the two effects. A mod can avoid LITCIG by using a TOOL with a tick_action EOC plus WIND_/WATER_EXTINGUISH, and no mod currently uses LITCIG.

```cpp
if( typeId() == itype_joint_lit && carrier != nullptr ) { carrier->add_effect( effect_weed_high, 1_minutes );
    here.add_field( ..., field_type_id( "fd_weedsmoke" ), 2 );
```

**Fix:** Add an itype `"smoke_data": { "effect": "cig", "duration": "15 s", "puff_chance": 0.1, "field": "fd_weedsmoke", "final_puff_eoc": "...", "on_puff_eocs": [ ... ] }`. Express the trait modifiers as enchantments or EOCs, and move the extinguish thresholds into flag or weather JSON.

### ITEM-J14 · Sawn-off barrel and stock behaviour is tied to the itype ids barrel_small and stock_none
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/item.cpp:2086`, `src/item.cpp:2096`, `src/item.cpp:2120`, `src/item.cpp:2321`, `src/item_gun_tool_ammo.cpp:713`, `src/item_gun_tool_ammo.cpp:732`, `src/iuse_actor.cpp:4406`, `src/iuse_actor.cpp:4469`

Weight, length and volume reductions for sawn guns check for two specific gunmod ids. They use magic numbers: stock dimensions of 0.26×0.11×0.04 m and a wood density of 850. A TODO says the hardcoding is no longer needed. Weight is already data-driven through gunmod `weight_multiplier`, but `integral_volume` and `integral_longest_side` reject negative values, so mod gunmods (folding stock, bullpup conversion) cannot shorten a gun.

```cpp
if( gunmod_find( itype_stock_none ) ) { const float stock_dimensions = 0.26f * 0.11f * 0.04f; ... const int density = 850;
//TODO: Unhardcode this now that integral_longest_side can be used
```

**Fix:** Allow negative or relative `integral_volume`, `integral_weight` and `integral_longest_side`, or add `"removes": { "barrel_fraction": 1.0, "stock": true }`. Convert `barrel_small` and `stock_none` to that data.

## 2. Bugs, oversights & quirks

### ITEM-B01 · Out-of-bubble catch-up uses stale FROZEN/COLD flags, so food left for weeks doesn't rot
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_degrade.cpp:1405`, `src/item_degrade.cpp:1416`, `src/item_degrade.cpp:595`, `src/item_degrade.cpp:617`

`process_temperature_rot` replays time spent outside the bubble in 1-hour chunks. It skips `calc_temp()` for chunks older than 2 days but still calls `calc_rot()` on them. `calc_rot()` returns early on the item's own FROZEN flag and clamps to fridge temperature on its own COLD flag. Those flags still describe the item's state when the player left. Meat frozen in winter and left until summer rots only during the final 2 days, and a COLD item rots at fridge rate all summer. This is a one-way preservation exploit, because the warm-to-winter direction is handled correctly.

```cpp
if( now - time < 2_days ) { calc_temp( env_temperature, insulation, time_delta ); }
...
if( process_rot ) { calc_rot( env_temperature, spoil_modifier, time_delta ); }
// calc_rot:
if( has_own_flag( flag_FROZEN ) ) { return; }
if( has_own_flag( flag_COLD ) ) { temp = std::min( temperatures::fridge, temp ); }
```

**Fix:** For chunks where `calc_temp` is skipped, have `calc_rot` derive frozen and cold from `env_temperature` (pass a flag that tells it to ignore the item's temperature flags). Alternatively, call `set_temp_flags(env_temperature, ...)` for those chunks, which is cheap.

### ITEM-B02 · Save RLE merges map items that aren't identical: uid, owner, birthday and snippet are lost
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/item.cpp:609`, `src/item.cpp:625`, `src/item.cpp:855`, `src/item.cpp:928`, `src/item.cpp:387`, `src/json.h:925`, `src/flexbuffer_json-inl.h:515`, `src/item_uid.h:22`, `src/item_location.cpp:167`, `src/savegame_json.cpp:3267`, `src/savegame_json.cpp:3307`, `src/savegame_json.cpp:5207`

Map item piles are saved with run-length encoding. `same_for_rle()` decides what counts as a run by calling `stacks_with(precise=true)`. That function never compares `uid_`, owner/old_owner, `bday`, `snip_id`, `countdown_point`, `item_counter`, poison, irradiation, specific energy or `mission_id`, and it ignores some vars. On load each run is rebuilt by copying the first item, and `item_uid`'s copy constructor makes a fresh uid. Consequences:
1. A player's hammer dropped next to a shop-owned hammer comes back with the same owner as the shop's: either free theft or lost property.
2. Reservations that target the second identical item fail with "item_location lost its target", because `item_location` has no index fallback when `uid_hint > 0`.
3. Two flyers with different text come back as copies of one.
4. Rotten corpses merge birthdays, which changes when they revive.

```cpp
return stacks_with( rhs, true, false, false, 0, 999, true );       // item.cpp:625
if( nxt == container.end() || !it->same_for_rle( *nxt ) )        // json.h:925
item_uid( const item_uid & ) : value( generate_next_item_uid() ) {} // item_uid.h:22
```

**Fix:** Give `same_for_rle` its own strict comparison. Require equal owner, old_owner, bday, snip_id, countdown_point, item_counter, temperature, poison, irradiation and mission_id, and compare raw item_vars with nothing ignored. Also store per-item uids in the RLE tuple (e.g. `[item, count, [uids]]`), or refuse to RLE items whose uid is referenced.

### ITEM-B03 · finalize_post returns early for guns, and NO_FAULTS clears faults before fault groups expand
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_factory.cpp:453`, `src/item_factory.cpp:903`, `src/item_factory.cpp:909`, `src/item_factory.cpp:941`, `src/item_factory.cpp:968`, `src/item_factory.cpp:1296`, `doc/JSON/ITEM.md:843`, `data/core/external_options.json:116`, `data/json/items/gun/9mm.json:297`, `data/mods/Xedra_Evolved/items/inventor/gun.json:753`

`Item_factory::finalize_post()` returns right after assigning gun repair tools to any non-PRIMITIVE_RANGED_WEAPON gun. Everything after that point is skipped for guns:
- `finalize_post_armor`, the only place `armor->data` is built. `briefcase_smg` (ARMOR+GUN) is therefore worn with no coverage, protection or encumbrance.
- NO_UNLOAD/NO_RELOAD propagation to pockets. Spill and pocket-move paths read the pocket flags.
- The pocket name fallback and the comestible contamination check.
- `fault_group` expansion, which ITEM.md documents for guns. Xedra's inventor gun never gets its `electronic_general` faults.

Separately, the NO_FAULTS external option clears `obj.faults` in `finalize_pre`, and `finalize_post` then re-adds every fault from fault_groups. Most engine faults come from groups, so the option mostly fails at its stated purpose.

```cpp
// handle complex firearms as a special case
if( obj.gun && !obj.has_flag( flag_PRIMITIVE_RANGED_WEAPON ) ) { std::copy( gun_tools...); return; }
...
if( get_option<bool>( "NO_FAULTS" ) ) { obj.faults.clear(); }   // finalize_pre, before group expansion
```

**Fix:** Restrict the gun special case to skipping the material-based repair-tool loop (913-939) instead of returning from the function. Expand fault_groups before the NO_FAULTS check in finalize_pre, or clear `fault_groups` as well when NO_FAULTS is set.

### ITEM-B04 · mod_damage rolls 2 mechanical faults per damage level; instant_damage faults can recurse forever
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_degrade.cpp:345`, `src/item_degrade.cpp:383`, `src/item_degrade.cpp:384`, `src/item_degrade.cpp:404`, `src/item_degrade.cpp:874`, `src/item_degrade.cpp:875`, `src/melee.cpp:293`

The fault loop runs `i = 0; i <= qty; i += damage_scale`, so a single `inc_damage()` (qty 1000) makes two rolls. Each roll always picks a fault if any is eligible. Every melee wear hit can therefore add two blade or handle faults. In addition, `can_have_fault()` does not exclude faults the item already has, and `set_fault()` calls `mod_damage(instant_damage)`. A mechanical_damage fault with non-zero instant_damage can therefore re-pick itself without end until the stack overflows. Only modded content can trigger the recursion today.

```cpp
if( qty > 0 ) { for( int i = 0; i <= qty; i += itype::damage_scale ) { set_random_fault_of_type( "mechanical_damage", false, holder ); } }
faults.insert( f_id ); mod_damage( f_id->instant_damage(), holder );
```

**Fix:** Use `i < qty`, or roll `qty / damage_scale` times with a minimum of 1. Make `can_have_fault()` return false for faults already present. Apply instant_damage via `force_set_damage`, or guard against re-entry.

### ITEM-B05 · Item whitelist finalization erases from a std::set while range-iterating it
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_factory.cpp:1670`, `src/item_factory.cpp:1672`, `src/item_factory.cpp:1855`

`finalize_item_blacklist()` erases the current element of `whitelist` inside a range-for and then advances the invalidated iterator. This is undefined behaviour at data load, and with glibc a crash is likely. It triggers when an item is in both an ITEM_WHITELIST and an ITEM_BLACKLIST, which is exactly the case the loop exists for. Only mod combinations reach it, because no vanilla data uses `whitelist: true`.

```cpp
for( const itype_id &it : whitelist ) { if( blacklist.count( it ) ) { whitelist.erase( it ); } }
```

**Fix:** `std::erase_if( whitelist, [&]( const itype_id &i ) { return blacklist.count( i ) > 0; } );`

### ITEM-B06 · every_item_min_max dereferences an empty std::optional modifier (bash drops / bash UI)
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_group.cpp:451`, `src/item_group.cpp:453`, `src/item_group.cpp:457`, `src/item_factory.cpp:5089`, `src/map.cpp:5047`, `src/mapdata.cpp:474`, `src/mapdata.cpp:850`

For count-by-charges items, `Single_item_creator::every_item_min_max()` reads `modifier->charges` and `modifier->count` before the `if( modifier )` check at line 463. Entries written as a plain id, as `[id, prob]` or as `{"item": x}` leave the optional empty, so the function reads uninitialized storage. The result can be a garbage spawn count. Vanilla reaches this in the UI path: an earthen wall's bash drops use `digging_soil_loam_50L`, which contains `{"item":"flint","prob":1}`, and flint is AMMO. The bad read happens when the terrain's extended description is built. No vanilla content was found on the `map::bash` drop path.

```cpp
if( i->count_by_charges() ) {
    const int min_charges = modifier->charges.first == -1 ? ...   // modifier may be empty
    return { std::make_pair( i, std::make_pair( modifier->count.first * min_charges, ... ) ) };
}
```

**Fix:** Handle the no-modifier case first: count (1,1) and charges = `charges_default()`, with 0..1 when probability < 100. This mirrors the non-charges branch.

### ITEM-B07 · Relic procgen: negative-power cap checked too early, hit_me/hit_you never loaded, generation can hang
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/relic.cpp:205`, `src/relic.cpp:262`, `src/relic.cpp:767`, `src/relic.cpp:768`, `src/relic.cpp:776`, `src/relic.cpp:777`, `src/relic.cpp:797`, `src/relic.cpp:841`, `src/relic.h:90`, `src/relic.h:113`, `doc/JSON/ARTIFACTS.md:131`

There are three problems in relic generation.
1. The cap check `max_negative_power > negative_attribute_power` runs before the new power is added, so the total can overshoot the cap by one attribute. The default of 0 still allows one negative attribute.
2. `relic_procgen_data::load` never fills `passive_hit_you` or `passive_hit_me`, yet `type_weights` accepts "hit_you" and "hit_me", and `check()` and the generator handle them. ARTIFACTS.md only hints that those types are non-functional.
3. Every rejection path leaves `num_attributes` and the power level unchanged: a cap break, a value of 0, or a `pick()` returning nullptr from an empty list. The `while` loop never ends if no candidate can be accepted, for example when type_weights lists only hit_me or every entry has negative power. Mapgen then hangs. An empty `type_weights` is dereferenced through `*type_weights.pick()`. Vanilla rulesets avoid all of these cases.

```cpp
while( rules.max_attributes > num_attributes && rules.power_level > ret.power_level( id ) ) {
    switch( *type_weights.pick() ) {
    ...
    if( power < 0 ) { if( rules.max_negative_power > negative_attribute_power ) { break; } negative_attribute_power += power; }
```

**Fix:** Test `negative_attribute_power + power < rules.max_negative_power` before accepting. Load `"passive_hit_you_procgen_values"` and `"passive_hit_me_procgen_values"` with the same shape as `active_procgen_values`, or reject those types in type_weights. Add a consecutive-failure counter (e.g. 100) that ends generation, and null-check `pick()`.

### ITEM-B08 · countdown_interval does nothing without SPAWN_ACTIVE, and transform() clears active
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item.cpp:181`, `src/item.cpp:205`, `src/item.cpp:445`, `src/item.cpp:4213`, `src/item.cpp:4797`, `src/item.cpp:4811`, `src/item_transformation.cpp:87`

ITEM.md:1587 says the countdown "is started at the birth of the item". The constructor sets `countdown_point` but does not activate the item, and the countdown is only checked inside `if( active )`. `convert()` sets `active = true` for countdown types, but `item_transformation::transform()` then overwrites it. As a result, B with `countdown_interval` + `revert_to` and no SPAWN_ACTIVE never reverts, whether it was reached by transform or spawned directly. Every vanilla countdown item carries SPAWN_ACTIVE (or uses target_timer, or is food) as a workaround. Item-group entries can also set `"active": true`. The constructor also starts the timer from `calendar::turn` rather than its `turn` argument.

```cpp
if( type->countdown_interval > 0_seconds ) { countdown_point = calendar::turn + type->countdown_interval; }  // no activate
it.active = it.has_temperature() || it.has_flag( json_flag_SPAWN_ACTIVE ) || target_timer > 0_seconds;
```

**Fix:** Call `activate()` in the constructor when `countdown_interval > 0`, and use `turn` as the start. In `transform()`, OR the computed value with `it.countdown_point != calendar::turn_max` instead of overwriting it.

### ITEM-B09 · EOC item-category spawn rates aren't saved; overriding a category resets its rate to 1.0
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_category.cpp:64`, `src/item_category.cpp:134`, `src/item_category.cpp:144`, `src/item_category.h:100`, `src/npctalk.cpp:6128`

Spawn rates live in a static singleton map that is never serialized. Rates set by the `set_item_category_spawn_rates` EOC revert after save/load, and No_Hope re-runs its difficulty EOCs on `game_load` as a workaround. `item_category::load` starts from a local `spawn_rate = 1.0f` and always calls `set_spawn_rate`. A copy-from override that only renames a category therefore resets an earlier mod's rate to 1.0. The EOC is also undocumented.

```cpp
float spawn_rate = 1.0f;
optional( jo, was_loaded, "spawn_rate", spawn_rate, 1.0f );
set_spawn_rate( spawn_rate );
```

**Fix:** Store the rate as an `item_category` member initialized from the existing value when `was_loaded` is true. Persist EOC overrides in the save (e.g. in the global var store) and reset them on unload. Document the EOC.

### ITEM-B10 · convert() never fixes charges when count_by_charges changes; stacks collapse to 1 item
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/item.cpp:422`, `src/item.cpp:426`, `src/item.cpp:441`, `src/item.cpp:4817`, `src/item_transformation.cpp:59`, `src/itype.h:1730`, `data/json/items/comestibles/nuts.json:722`, `data/json/items/comestibles/egg.json:1300`

`item::convert()` assigns the new `type` first and only then compares `count_by_charges()` with `new_type->count_by_charges()`. Both sides now read the same itype, so the fix-up is dead code. A count-by-charges stack converted to a single-item type becomes ONE item with stale charges. The reverse direction leaves 0 charges. Example: a batch of 10 crafts produces a 20-charge liquid stack of `acorns_blanching_active`. After 6 days it becomes a single solid `acorns_blanched`, and 19 of the 20 handfuls are lost. `curing_roe_active` → `cured_roe` behaves the same way.

```cpp
type = find_type( new_type );
...
if( count_by_charges() != new_type->count_by_charges() ) { charges = new_type->charges_default(); }  // always false
```

**Fix:** Capture `const bool was_cbc = count_by_charges();` before reassigning. When going from count-by-charges to single items, have the caller (transform or countdown) split the stack into `charges` items and put them in the parent pocket. In the other direction, set charges to `max(1, charges_default())`. Warn at load when revert_to or transform changes count_by_charges.

### ITEM-B11 · faults::random_of_type_item_has returns a dangling reference
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/fault.cpp:47`, `src/fault.cpp:57`, `src/fault.cpp:62`, `src/activity_actor.cpp:507`

`faults::all_of_type()` returns a vector by value. `random_of_type_item_has` binds it to a local reference and returns `const fault_id &` into it, so the reference dangles once the function returns. `random_of_type` returns a reference into a temporary; it has no callers. The one caller, the aim check, only compares the result with NULL_ID. ASan will flag it, but in practice it is close to harmless, and the function also copies a vector on every aim check.

```cpp
const std::vector<fault_id> &typed = all_of_type( type );
for( const fault_id &fid : typed ) { if( it.has_fault( fid ) ) { return fid; } }
```

**Fix:** Return `fault_id` by value from both functions, or have `all_of_type` return a const reference into `faults_by_type` (with a static empty vector for unknown types).

### ITEM-B12 · Variant kept from old type after convert; container transforms set the variant on the wrong type
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item.cpp:423`, `src/item.cpp:3104`, `src/item.cpp:5012`, `src/item_transformation.cpp:59`, `src/item_transformation.cpp:95`, `src/savegame_json.cpp:3349`, `src/item_factory.cpp:4753`

`convert()` changes `type` but leaves `_itype_variant` pointing at the old type's variant, and `set_itype_variant` returns early when the new type has no variants. A `box_small` with the `223_ammo_box_500` variant that is folded into `box_small_folded` keeps the ".223/5.56 ammo box" name. On reload the invalid variant is dropped and the name changes. In `transform()`'s container branch, `set_itype_variant` runs before `convert`, so the variant is chosen from the old type. `migrate_item` already calls `clear_itype_variant()` by hand to work around this. The effects are cosmetic or limited to stacking.

**Fix:** In `convert()`, clear `_itype_variant` when it is not in the new type's list, then call `select_itype_variant()`. In `transform()`, move the content `set_itype_variant` call after `convert`.

### ITEM-B13 · Charged qualities are checked against the avatar's power, even for NPC-held tools
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item.cpp:2486`, `src/item_gun_tool_ammo.cpp:1406`, `src/item_gun_tool_ammo.cpp:1434`, `src/item_gun_tool_ammo.cpp:1800`, `src/activity_item_handling.cpp:1944`

`get_quality_nonrecursive()` grants `charged_qualities` only if `ammo_sufficient( &get_player_character() )`. For UPS- or bionic-powered tools, the remaining charge includes that character's power. NPC crafting and zone activities call `has_quality`, so an NPC's tool gains or loses its quality depending on the player's UPS. `get_quality_speed` already uses the real crafter. Few tools combine charged qualities with UPS or bionic power.

```cpp
if( !type->charged_qualities.empty() && ammo_sufficient( &get_player_character() ) ) {
```

**Fix:** Add an optional `const Character *carrier` parameter to `get_quality` and `get_quality_nonrecursive`, threaded through the inventory and crafting calls. The default, nullptr, checks only the item's own charge.

### ITEM-B14 · ENERGY_SHIELD stores MAX_ENERGY_SHIELD_HP, but the docs say ENERGY_SHIELD_MAX_HP
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item.cpp:209`, `src/item_factory.cpp:2459`, `src/item_factory.cpp:3632`, `doc/JSON/JSON_FLAGS.md:199`, `doc/JSON/ITEM.md:363`

The constructor writes the var `MAX_ENERGY_SHIELD_HP`, while JSON_FLAGS.md and ITEM.md document `ENERGY_SHIELD_MAX_HP`. An EOC that reads the documented name on a fresh shield gets 0. ITEM.md also documents the JSON key as `energy_shield_max_hp`, but the loader reads `max_energy_shield_hp`. Aftershock writes its own copy before dividing, and ENERGY_SHIELD on non-armor is already rejected at load, so this is a naming bug only.

**Fix:** Write `ENERGY_SHIELD_MAX_HP` (keeping or migrating the old key), and fix the JSON key name in ITEM.md.

### ITEM-B15 · json_flag "conflicts" is loaded, validated and documented but never enforced
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/flag.cpp:433`, `src/flag.cpp:459`, `src/flag.h:529`, `doc/JSON/JSON_INFO.md:4031`, `data/json/flags.json:249`, `src/melee.cpp:248`

`json_flag::load` reads `conflicts` into the private `conflicts_`, and `check()` only verifies that the ids exist. There is no getter and no runtime reader. JSON_INFO.md promises that "only one will be applied". flags.json declares about 10 pairs, such as DURABLE_MELEE/FRAGILE_MELEE, FRAGILE/STURDY and DEAF/PARTIAL_DEAF. An item that gets both flags through inheritance or an EOC `set_flag` has both effects stacked, e.g. in `handle_melee_wear`. No vanilla item currently declares a conflicting pair directly.

```cpp
optional( jo, was_loaded, "conflicts", conflicts_ );   // only read by check()
```

**Fix:** Enforce it: in `item::set_flag`, drop or refuse flags listed in the new flag's conflicts (and the reverse), and debugmsg on itypes that declare both at finalize. Otherwise remove the field from the docs and data. Expose `json_flag::conflicts()`.

### ITEM-B16 · Corpses under 250 ml divide by zero in ready_to_revive and can never revive
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item.cpp:2665`, `src/item.cpp:4503`, `src/units.h:255`, `data/mods/Magiclysm/monsters/holiday_magiclysm.json:45`

`volume() / 250_ml` is integer division and gives 0 for small corpses. `burnt / 0` gives NaN or inf, and casting that to int is UB. The revival gate `rng( 0, 0 ) > burnt` is never true. The only data that hits this is Magiclysm's `mon_yulecat_cub` (120 ml, likely a data typo) with ZOMBIFY_INTO_ENABLED.

```cpp
age_in_hours -= static_cast<int>( static_cast<float>( burnt ) / ( volume() / 250_ml ) );
if( rng( 0, volume() / 250_ml ) > burnt &&
```

**Fix:** Use `std::max( 1, volume() / 250_ml )` in both places, or compute in float millilitres.

### ITEM-B17 · Inactive-item branch returns after blackpowder fouling, skipping gun cooling and EMP reboot
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/item.cpp:4902`, `src/item.cpp:4905`, `src/item.cpp:4908`, `src/item_gun_tool_ammo.cpp:4398`, `src/item_gun_tool_ammo.cpp:4413`

In `process_internal`'s inactive branch, `return process_blackpowder_fouling( carrier );` always returns, and the callee always returns false. A fouled gun therefore never cools or reboots after EMP until it is cleaned. In the same way, `gun_heat > 0` skips the reboot until the gun has cooled. No shipped gun combines heat, black powder and ELECTRONIC, so only modded content hits this.

```cpp
if( has_fault_flag( flag_BLACKPOWDER_FOULING_DAMAGE ) ) { return process_blackpowder_fouling( carrier ); }
if( get_var( "gun_heat", 0 ) > 0 ) { return process_gun_cooling( carrier ); }
```

**Fix:** Call each handler in sequence and return false at the end.

### ITEM-B18 · Active TOOL items skip temperature/rot, and tick_action runs only for TOOLs
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item.cpp:258`, `src/item.cpp:423`, `src/item.cpp:4885`, `src/item.cpp:4889`, `src/item_gun_tool_ammo.cpp:4321`, `src/item_gun_tool_ammo.cpp:4346`, `src/item_gun_tool_ammo.cpp:4390`, `src/item_factory.cpp:4538`, `src/item_degrade.cpp:1347`, `doc/JSON/ITEM.md:1570`, `data/json/items/comestibles/brewing.json:843`

`process_internal` returns `process_tool(...)` for any item with a tool slot before it reaches `process_temperature_rot`. So:
- COMESTIBLE+TOOL items do not freeze, heat or rot while active. data/json has 6: two sourdough starters, malting_grain, soaking_dandelion, fermenting_yogurt and fermentable_fish_mixture.
- `tick_action` is loaded for every itype but invoked only inside `process_tool`. Active armour, comestibles and generic items cannot run tick EOCs, and nothing warns when they declare one. ITEM.md does document the tool-only restriction.

For the vanilla countdown items, rot is postponed rather than lost: `last_temp_check` is not advanced, so the converted item catches up afterwards. A modded perishable TOOL without a countdown becomes effectively imperishable, though.

```cpp
if( is_tool() ) { return process_tool( carrier, pos ); }
// All foods that go bad have temperature
if( has_temperature() && process_temperature_rot( ... ) )
```

**Fix:** Run `process_temperature_rot` first when `has_temperature()` (destroying the item on rot), then call `type->tick()` for any active item with a tick_action, then fall through to the tool energy drain. Optionally add `"tick_interval": "10 s"` so EOC ticks don't run every turn.

## 3. Performance

### ITEM-P01 · items::load re-runs Item_factory::init() (~200 iuse/actor registrations) for every ITEM JSON object
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_factory.cpp:5410`, `src/item_factory.cpp:2038`, `src/item_factory.cpp:2249`, `src/item_factory.cpp:1914`, `src/item_factory.cpp:1926`, `src/item_factory.cpp:4825`, `src/item_factory.cpp:4830`, `src/game_io.cpp:222`, `src/init.cpp:367`, `src/init.cpp:689`

`items::load` calls `item_controller->init()` before loading each ITEM object, and `init()` has no guard. Each call runs 201 `add_iuse`/`add_actor` registrations. Each registration allocates a wrapper with `make_unique`, does a `std::map<std::string>` lookup and frees the previous entry. Each call also recreates EMPTY_GROUP. Core has 10,442 ITEM objects and the bundled mods add about 4.4k more, so a world load does roughly 2–3M redundant allocate/free pairs. That is likely several hundred milliseconds. The call is redundant, because `unload_data` → `items::reset` → `Item_factory::reset()` already runs `clear(); init();`.

```cpp
void items::load( const JsonObject &jo, const std::string &src ) {
    item_controller->init();
    item_controller->get_generic_factory().load( jo, src );
}
```

**Fix:** Add an `initialized` flag that is set at the end of `init()` and cleared in `clear()`, and call `init()` from `items::load` only when the flag is unset. An alternative is to call `init()` once before the load pass.

### ITEM-P02 · Temperature/rot catch-up evaluates weather noise every hour even when the result is discarded
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_degrade.cpp:1341`, `src/item_degrade.cpp:1370`, `src/item_degrade.cpp:1377`, `src/item_degrade.cpp:1405`, `src/item_degrade.cpp:1416`, `src/item.cpp:4208`, `src/active_item_cache.cpp:12`, `src/weather_gen.cpp:101`

When a submap reloads after an absence, every comestible and corpse loops over each elapsed hour. Each iteration calls `get_weather_temperature` (two cos plus `raw_noise_4d`) and `get_map().get_abs(pos)`. For items with `process_rot == false` (non-spoiling food, sealed pockets) and no `decays_in_air`, the result matters only for the last 48 hours. For example, 1,000 such items after 30 days away cost about 720k noise evaluations, spread over the first 100 turns.

```cpp
const bool process_rot = goes_bad() && spoil_modifier != 0;
while( now - time > 1_hours ) {
    time += time_delta;
    env_temperature = wgen.get_weather_temperature( get_map().get_abs( pos ), time, seed );
    if( now - time < 2_days ) { calc_temp( ... ); }
```

**Fix:** When `!process_rot && !decays_in_air`, advance `time` in whole hours to just before `now - 2_days` before entering the loop. The results are bit-identical. Hoist `get_abs(pos)` and the root-cellar/underground lookup out of the loop. For perishables, an hourly temperature cache keyed per OMT would cut the noise calls further, but it changes results slightly, because position enters the noise.

### ITEM-P03 · Every item construction or copy pays 8+ unconditional heap allocations from cata::heap members
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/item.h:3558`, `src/item.h:3577`, `src/item.h:3582`, `src/item.h:3585`, `src/item_components.h:22`, `src/value_ptr.h:80`, `src/value_ptr.h:90`, `src/item.cpp:184`, `src/item.cpp:387`

`item` has 7 `cata::heap<>` members (faults, item_tags, three tag caches, item_vars, techniques), and `item_components` adds an eighth. `cata::heap` allocates in its constructor even when empty, and its copy-assign always does `new T`. Most items have all of these empty, but every construct or copy (mapgen, save load, `i_add`, stacking, transform, convert) still pays at least 8 allocations. The item ctor adds a ninth with `item_vars = type->item_variables`.

```cpp
heap( Args &&...args ) : heaped_{ new T{ std::forward<Args>( args )... } } {}
if( rhs.heaped_ ) { heaped_.reset( new T{ *rhs.heaped_ } ); } else { heaped_.reset( new T{} ); }
```

**Fix:** Make `cata::heap` allocate lazily. Null means empty; const accessors return a static empty `T`; mutating accessors allocate on first use; copy allocates only for a non-empty source. Keep the noexcept move and the `T&` conversion semantics. In the ctor, assign `item_vars` only when `type->item_variables` is non-empty.

### ITEM-P04 · Item blacklist finalization rescans all item groups, requirements and recipes once per blacklisted item
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/item_factory.cpp:1689`, `src/item_factory.cpp:1697`, `src/item_factory.cpp:1701`, `src/item_factory.cpp:1706`, `src/item_factory.cpp:1753`, `src/item_group.cpp:380`, `src/item_group.cpp:997`, `src/requirements.cpp:1265`, `src/recipe_dictionary.cpp:924`

For each blacklisted itype, finalization walks every item group (about 35k entries, with string compares), every requirement, and all recipes and uncrafts through a `std::function` predicate. classic_zombies blacklists 382 items and Generic_Guns 129, which costs tens of millions of operations. A whitelist would make the blacklist roughly every other itype. The replacement path a few lines below already does one batched pass.

```cpp
for( const itype_id &blackout : item_blacklist.blacklist ) {
    for( ... &g : m_template_groups ) { g.second->remove_item( blackout ); }
    for( ... &r : requirement_data::all() ) { ...blacklist_item( blackout ); }
    recipe_dictionary::delete_if( [&blackout]( const recipe & r ) { return r.result() == blackout; } );
}
```

**Fix:** Add set-based overloads (`remove_items(const std::unordered_set<itype_id>&)`, `blacklist_items(set)`) and a single `delete_if` that tests set membership, and call each once. Store typed ids in `Single_item_creator` (see ITEM-P12).

### ITEM-P05 · Relic enchantment accessors return heavy vectors by value; called every turn per character
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/relic.cpp:681`, `src/relic.cpp:686`, `src/relic.cpp:920`, `src/item.cpp:3386`, `src/item.cpp:3394`, `src/character.cpp:4932`, `src/character_body.cpp:252`, `src/melee.cpp:632`

`recalculate_enchantment_cache` runs every turn from `update_body` and on each melee attack. For every carried relic it calls `get_proc_enchantments()` and `get_defined_enchantments()`, and both deep-copy vectors of enchantments, each holding about 14 maps, translations and `std::function`s. `relic::operator==`, which `stacks_with` uses, copies the vector about 3N+5 times because the loop condition re-copies it. The per-turn cost is microseconds per relic, but the fix is trivial.

```cpp
std::vector<enchant_cache> relic::get_proc_enchantments() const { return proc_passive_effects; }
std::vector<enchantment> relic::get_defined_enchantments() const { return defined_passive_effects; }
```

**Fix:** Return `const std::vector<...> &` from both the relic and item accessors, using a static empty vector for non-relics. Make the non-const range-for refs at iuse.cpp:8763/8799 and talker_item.cpp:113 const. In `operator==`, bind each vector once.

### ITEM-P06 · finalize_pre scans all itypes per gun/magazine ammo type and copies the whole itype into each lambda
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_factory.cpp:634`, `src/item_factory.cpp:774`, `src/item_factory.cpp:5338`, `src/item_pocket.cpp:118`, `src/item_pocket.cpp:2592`

For each magazine and gun ammotype, `Item_factory::find` scans all ~10–12k itypes. The lambdas capture `[obj, am]` by value although `obj` is unused, which costs two full itype copies per call (itype has no implicit move). This is a one-time startup cost of tens of ms. The same missing ammotype index makes `pocket_data::volume_capacity()` scan all itypes at runtime for ammo-restricted container pockets such as bandoliers and quivers.

```cpp
auto temp_vec = Item_factory::find( [obj, am]( const itype & e ) { ... return e.ammo->type == am; } );
```

**Fix:** At minimum, change the capture to `[&am]`. Better, build `std::map<ammotype, std::set<itype_id>>` once in a single pass (skipping blacklisted items), keep it on `Item_factory` (reset in `clear()`), and use it from `volume_capacity` too, or cache `volume_capacity` per pocket after finalize.

### ITEM-P07 · item::weight() constructs a full temporary item for ammo-belt linkage and scans gunmod pockets on every item
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item.cpp:177`, `src/item.cpp:2080`, `src/item.cpp:2087`, `src/item.cpp:2096`, `src/item.h:3706`

For linked belts, `weight()` builds `item links(...)`, a full construction that also uses up an item uid and advances the global RNG through the `seed` member initializer. That side effect makes a const getter perturb RNG determinism. Every item, not only guns, also runs `gunmod_find` twice. Only 4 core magazines have a linkage, so the cost is small.

```cpp
if( type->magazine && type->magazine->linkage ) { item links( *type->magazine->linkage ); links.charges = ammo_remaining( ); ret += links.weight(); }
if( gunmod_find( itype_barrel_small ) ) {   // not gated by type->gun
```

**Fix:** Compute the linkage weight directly as `linkage->obj().weight * ammo_remaining()` (linkages count by charges). Wrap both `gunmod_find` calls in `if( type->gun )`, as `_stacks_weapon_mods` does at item.cpp:655.

### ITEM-P08 · set_temp_flags churns flags on every temperature update, triggering full prefix/suffix cache rebuilds
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item.cpp:4375`, `src/item.cpp:4383`, `src/item.cpp:2401`, `src/item.cpp:1533`, `src/item.cpp:4835`

`set_temp_flags` always unsets the current FROZEN/COLD/HOT flag and then sets the target flag, even when they are the same. Each `unset_flag` rebuilds both prefix and suffix caches over own, type and inherited flags. It also sets `requires_tags_processing`, which sends the next `process_internal` through the full chain of flag checks. The cost is sub-microsecond per call, so this is minor.

**Fix:** Compute the target flag first and return early when it is already set. Change flags only on a real transition, and keep the FROZEN→thawed side effects (freezerburn, SHREDDED, FROM_FROZEN_LIQUID). In `unset_flag`, touch the caches only when `erase` actually removed something.

### ITEM-P09 · Corpse thermal properties allocate a material vector per accessor (5–8 allocations per temperature update)
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item.cpp:2955`, `src/item.cpp:3208`, `src/item.cpp:3216`, `src/item.cpp:3224`, `src/item.cpp:3232`, `src/item.cpp:3947`, `src/item.cpp:4247`, `src/item.cpp:4377`

Corpses are not comestible, so the four thermal accessors use `made_of_types()[0]`. That builds a heap vector just to read its first element. `calc_temp` makes 5 such calls and `set_item_temperature` makes 8. `calc_temp` exits early for small temperature changes, so this is a cheap micro-fix rather than a hotspot.

```cpp
float item::get_specific_heat_liquid() const { if( is_comestible() ) {...} return made_of_types()[0]->specific_heat_liquid(); }
```

**Fix:** Add a `first_material()` helper that returns `made_of().begin()->first.obj()` (guarding the empty case), or cache the thermal constants as finalize_pre already does for comestibles.

### ITEM-P10 · Per-turn full-inventory has_item() walks in process_extinguish and relic HELD recharge
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/item.cpp:4662`, `src/item.cpp:4667`, `src/item.cpp:4859`, `src/relic.cpp:568`, `src/relic.cpp:651`, `src/visitable.cpp:88`, `src/visitable.cpp:663`

`process_extinguish` runs every turn for lit torches, candles and fuses. It calls `carrier->has_item(*this)` unconditionally, although the result only matters for the deep-water and wind branches. It also copies `w_point`, which holds a std::string, and never uses the copy. Relics with a HELD requirement call `has_item` every turn while below max charges. Wielded items are found at the first node, so the full scan only happens for items stowed in containers.

```cpp
bool in_inv = carrier != nullptr && carrier->has_item( *this );
w_point weatherPoint = *get_weather().weather_precise;   // unused
```

**Fix:** Evaluate the cheap tile and weather predicates first, and call `has_item` only when one of them needs `in_inv`. Drop the unused copy. For relics, cache the answer per turn. A "carrier != nullptr" hint is not reliable, because vehicle.cpp:6353 passes the player as carrier for turret bases.

### ITEM-P11 · get_option<T>("...") string lookups in per-frame and per-item paths
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item.cpp:190`, `src/item.cpp:1193`, `src/item.cpp:1827`, `src/item.cpp:3088`, `src/item.cpp:3090`, `src/item.cpp:4495`, `src/item_tname.cpp:120`, `src/item_tname.cpp:129`, `src/item_degrade.cpp:999`, `src/options.cpp:4367`

`get_option` builds a key string (the keys over 15 chars heap-allocate) and does one or two map lookups plus a `getPage()` copy. Paths that call it per item:
- `has_itype_variant` (used by `symbol()`, cata_tiles and `type_name`) for gun and drug variant items
- tname for ITEM_HEALTH and ASTERISK_POSITION
- AMMO_IN_NAMES
- ZOMBIFY_INTO_ENABLED

`cached_options.h` exists for exactly this case.

```cpp
case itype_variant_kind::gun: return get_option<bool>( "SHOW_GUN_VARIANTS" );
const std::string item_health_option = get_option<std::string>( "ITEM_HEALTH" );
```

**Fix:** Add `show_gun_variants`, `show_drug_variants`, `ammo_in_names`, `item_health_mode` (enum), `zombify_into_enabled` and `asterisk_position` to `cached_options.h`, and update them in options.cpp.

### ITEM-P12 · Item group spawning re-interns string ids, builds context strings, and constructs items before the spawn-rate roll
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_group.cpp:212`, `src/item_group.cpp:236`, `src/item_group.cpp:264`, `src/item_group.cpp:295`, `src/item_group.cpp:298`, `src/item_group.cpp:323`, `src/item_group.cpp:665`

On every spawn, `Single_item_creator` re-interns its id from a `std::string`, compares it with `"corpse"`, and builds the string `"modifier for " + context()`, which is only used in debugmsgs. `create()` reads ITEM_SPAWNRATE on every call. Items are fully built, modified and containerized before the spawn-rate roll discards them. The shipped difficulty presets set ITEM_SPAWNRATE to 0.8 and 0.5, so 20–50% of that work is wasted on hard worlds. Ammo modifiers also create an ammo item just to read its typeId.

```cpp
item itm = create_single_without_container( birthday, rec );
if( flags & spawn_flags::use_spawn_rate && !itm.has_flag( json_flag_MISSION_ITEM ) && rng_float( 0, 1 ) > spawn_rate ) { continue; }
```

**Fix:** Store typed ids plus an `is_corpse` bool at load. Format the context lazily, only inside the debug paths. Hoist the ITEM_SPAWNRATE read. Roll spawn rate before construction, using the itype's MISSION_ITEM flag for the exemption. Add a `pick_type()` for ammo groups.

### ITEM-P13 · stacks_with computes all ~40 segments, with allocations, even when the caller only needs a bool
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/item.cpp:198`, `src/item.cpp:631`, `src/item.cpp:788`, `src/item.cpp:818`, `src/item.cpp:855`, `src/item.cpp:867`, `src/item.cpp:918`, `src/item.cpp:923`, `src/item.cpp:961`, `src/advanced_inv_area.cpp:286`, `src/json.h:925`

Once the types match, `stacks_with` evaluates every segment with no early exit. Several segments allocate: the whitelist/blacklist pocket vectors, `mods()` vectors, `get_uncraft_components()`, `overheat_symbol()` strings and `get_var` string copies. `is_broken()` is evaluated twice. Most callers only use `operator bool`. The gain is situational: AIM's O(N²) stack check over same-type items that don't stack benefits, but RLE save comparisons mostly compare items that are identical and need the full evaluation anyway.

**Fix:** Add a boolean fast path that checks the cheap integer and bitset fields first and returns on the first mismatch before evaluating the allocating segments. Keep the full bitset version for `item_container`'s aggregation, and drop the duplicate BROKEN evaluation.

### ITEM-P14 · type_name() re-evaluates every conditional_name predicate on each tname
**Severity:** Low · **Effort:** Medium · **Verified:** plausible
**Where:** `src/item.cpp:2383`, `src/item.cpp:5021`, `src/item.cpp:5035`, `src/item.cpp:5049`, `src/item.cpp:5054`, `src/consumption.cpp:338`, `src/itype.h:1310`

Core defines 1,040 conditional names on 118 base items. For each one, `type_name` constructs `std::function` objects, and COMPONENT_ID_SUBSTRING walks the component tree recursively with `string::find`. FLAG names re-intern `flag_id` from a string on every call. VITAMIN names run a full effective-nutrient computation. inventory_ui and AIM already cache names, so the cost is microseconds per render, not a demonstrated hotspot.

**Fix:** Parse conditions into typed `flag_id` and `vitamin_id` at load. Collect component ids once per call. Optionally cache the resolved prefix on the item, invalidated when components, flags or vars change.

### ITEM-P15 · Every relic item instance deep-copies the type's enchantments, including unused defined_passive_effects
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/item.cpp:288`, `src/item.cpp:3394`, `src/relic.h:208`, `src/relic.h:210`, `src/relic.h:222`, `src/relic.cpp:920`

The item ctor does `relic_data = type->relic_data`, a deep copy of every effect vector, and each later item copy repeats it. The instance's `defined_passive_effects` and `passive_enchant_ids` are never read for behaviour, which uses `type->relic_data`, and they are not serialized. Fresh items therefore carry them while loaded items do not, and `is_same_relic` (which compares their sizes) can then refuse to stack otherwise identical items. About 400 ARTIFACT-subtype types exist across core and mods.

**Fix:** Split relic into immutable type data referenced from the itype and per-instance mutable state (charges and procgen effects). As a first step, clear `defined_passive_effects` and `passive_enchant_ids` on the instance copy in the ctor.

### ITEM-P16 · all_items_top() and related accessors build std::list<item*> via std::function filters
**Severity:** Low · **Effort:** Large · **Verified:** plausible
**Where:** `src/item.h:3373`, `src/item.h:3375`, `src/item_pocket.cpp:500`, `src/item_contents.cpp:1883`, `src/item.cpp:1510`, `src/item.cpp:2552`, `src/item.cpp:3330`, `src/item_degrade.cpp:503`, `src/lightmap.cpp:854`

`all_items_top()` (103 call sites) allocates one list node per contained item, after building a `std::function` filter and splicing per-pocket lists, only for the caller to iterate once. None of the named callers is shown to be hot. The most frequent caller is probably the lightmap scan of vehicle cargo.

**Fix:** Return a reserved `std::vector`, or add a non-allocating `for_each_top_item(F&&)` and migrate the frequent callers first. Check the callers that rely on list splice or erase semantics.

## Appendix: considered and rejected

- Unused-ammotype check uses a hardcoded C++ whitelist; ammunition_type has no copy-from – magazines, tools, gunmods and turrets count as users through pocket `ammo_restriction`, and ammotypes with no solid AMMO items are exempt. The whitelist is a FIXME list of legacy content, not an extension point; `stackable` covers the stacking use case, and ids can still be overridden.
- Ethereal items count down once per process call, so ground items in slow buckets last too long – `spawn_ethereal_item` never makes comestibles ethereal, so ethereal items always have processing speed 1. The trigger cannot happen.
- Item group 'damage' on count-by-charges items deletes charges – `set_damage` clamps to `max_damage()`, which is 0 for count-by-charges types, so no charges are removed.
- Magazine migration loops iterate by value, so migrated magazines are never applied – `itype::magazines` is never loaded and is always empty, so the loop is dead code. Runtime reads use pocket data, which this code does not migrate either way.
- `spoilage_sort_order` is O(n²) per call, and AIM's comparator calls it 4× per comparison – it only applies to perishables nested directly in one top-level item. Furniture and vehicle storage hold top-level items, so the real cost is negligible.
