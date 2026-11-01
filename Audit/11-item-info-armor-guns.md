# 11 · Item names/info, armor & guns/tools/ammo

> Engine audit – static code reading at commit `a1a646d6`; nothing was compiled or run. Each finding was checked against the code by a separate reviewer: **confirmed** = verified by reading the code; **plausible** = consistent with the code, but reachability or impact not proven. Line numbers refer to that commit.

**Scope:** `src/item_tname.cpp`, `src/item_info.cpp`, `src/item_armor.cpp`, `src/item_gun_tool_ammo.cpp`, `src/iteminfo_query.cpp`, `src/ui_iteminfo.cpp`, `src/character_armor.cpp`, `src/armor_layers.cpp`, `src/character_guns.cpp`, `src/character_ammo.cpp`

## Summary

This system mostly works, but the armor and gun code has several real combat-math bugs. The most serious: ablative plates are counted twice per hit and also protect when the coverage roll misses them. Gunmod "must be unloaded" checks compare translated strings, so in translated games installing a bore conversion deletes the loaded magazine. Sided armor always goes on the left. Recoil math has a 2x jump at handling 1.0. Much of the gameplay logic is tied to hardcoded item ids, flag names and `_on` naming conventions where JSON fields would be cleaner. EOCs cannot see or change gun, ammo or gunmod state, and they cannot react to reloads, gunmod installs or plate breaks. On performance, armor mitigation, item info building and NPC weapon evaluation all do far more work than they need to, though none of it is catastrophic.

| Lens | Critical | High | Medium | Low | Total |
|---|---|---|---|---|---|
| Missing JSON / EOC | 0 | 0 | 8 | 10 | 18 |
| Bugs & quirks | 0 | 2 | 4 | 11 | 17 |
| Performance | 0 | 0 | 4 | 8 | 12 |
| **Total** | 0 | 2 | 16 | 29 | 47 |

**Fix first:**
1. **IINF-B01** · Ablative plates protect twice per hit, and still protect when the roll misses them. This is a common combat situation, and the fix is a single parameter.
2. **IINF-B02** · Gunmod "must be unloaded" checks compare translated slot names. In translated games this destroys magazines; the fix is switching `name()` to `str()`.
3. **IINF-B04** · Sided armor always goes on the left. The side-choice logic is dead code; it affects every player and every NPC wearing pouches, holsters or quivers.
4. **IINF-B05** · `is_reloadable()` reads the wrong pocket on multimag guns. Several Aftershock guns cannot be reloaded at all; the fix is a small local change.
5. **IINF-P01** · `mitigate_damage` computes resistance for every damage type to use one. This is about 10-17x wasted armor math per hit per layer, and the replacement math is drop-in.

## 1. Missing JSON bindings & EOC features

### IINF-J01 · `armor()` math function disagrees with real protection: worn-only for characters, always 0 for items
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character_armor.cpp:76`, `src/talker_character.cpp:867`, `src/talker.h:555`, `src/talker_item.h:18`, `src/math_parser_diag.cpp:215`

The protection used in combat comes from `Character::get_armor_type`, used at `character.cpp:3666`, `explosion.cpp:374` and `game.cpp:7590`. It adds worn armor, `armor_bonus`, bionic `protec`, mutation armor and the bodypart's innate `damage_resistance`. The EOC `u_armor('bash','torso')` calls only `worn.damage_resist`, so an EOC that checks "is the player armored enough" ignores Subdermal Plating, chitin mutations and enchantment armor bonuses. `talker_item` has no `armor_at` override, so `n_armor(...)` on an item always returns the `talker.h` default of 0, even though `coverage()` and `encumbrance()` do work on items. There is also no assignable form.

```cpp
int Character::get_armor_type(...) { int ret = worn.damage_resist( dt, bp ); ... armor_bonus ... bid->protec ...
    return ret + mutation_armor( bp, dt ) + bp->damage_resistance( dt ); }
double talker_character_const::armor_at( damage_type_id &dt, bodypart_id &bp ) const
{ return me_chr_const->worn.damage_resist( dt, bp ); }
// talker.h:555
virtual double armor_at(...) const { return 0; }  // no talker_item override
```

**Fix:** Have `talker_character::armor_at` call `get_armor_type`, with a kwarg `"source": "worn"|"all"` (default `all`). Implement `talker_item::armor_at` as `item::resist(dt, false, bp)`, with an optional kwarg `roll` (0 best / 50 median / 99 worst). Example: `{ "math": [ "n_armor('bullet', 'torso_upper', 'roll': 50)", "<", "10" ] }`.

### IINF-J02 · Clothing-mod stats are only computed by the sewing action; flags set by itemgroups or EOCs give no bonus
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_armor.cpp:1574`, `src/item_armor.cpp:1579`, `src/item.cpp:2370`, `src/item_group.cpp:771`, `src/iuse_actor.cpp:5789`, `src/npctalk.cpp:6258`

Clothing-mod bonuses are cached in `clothing_mod_*` item vars. `update_clothing_mod_val()` is called only from the `sew_advanced` iuse (`iuse_actor.cpp` 5789/5867/5910/5918). An item that gets the mod flag any other way keeps a zero bonus: itemgroup `custom-flags`, EOC `npc_set_flag: "kevlar_padded"`, `u_spawn_item` flags, or debug. `tname` still shows the "+1" marker, because `has_clothing_mod` checks the flag, so the item looks modded but isn't. Also, when a mod rebalances a clothing_mod `value`, existing items keep the stale cached numbers forever. No current data uses these paths, so this is a latent trap.

```cpp
float item::get_clothing_mod_val( clothing_mod_type type ) const
{ return get_var( get_clothing_mod_val_key( type ), 0.0f ); }
item &item::set_flag( const flag_id &flag ) { item_tags.insert( flag ); ... // no clothing-mod refresh
```

**Fix:** Compute the values lazily from current flags, with a cache invalidated in `set_flag`/`unset_flag` and on load. Alternatively, call `update_clothing_mod_val()` from `set_flag`/`unset_flag` when the flag belongs to a clothing_mod, and once on deserialize. That enables `{ "item": "jacket_leather", "custom-flags": [ "steel_padded" ] }` in itemgroups and EOC tailor rewards.

### IINF-J03 · Active armor preview and item names rely on the `<id>_on` convention plus hardcoded rm13_armor, GASMASK and PAPR checks
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_info.cpp:2390`, `src/item_info.cpp:2406`, `src/item_info.cpp:2007`, `src/item_tname.cpp:587`, `src/itype.cpp:237`, `src/item_factory.cpp:3084`

`armor_info` shows "When active" stats by converting a copy to `itype_id(typeId() + "_on")`. It does this only for power armor or the hardcoded `itype_rm13_armor`, and never looks at the item's real transform target. Activatable armor whose active form has a different name gets no preview. An inactive power-armor item without an `_on` twin goes through `find_template`'s "Missing item definition" debugmsg (once per session) and registers an `undefined-<id>` runtime type. No shipped content hits that path: Aftershock `exosuit_claw` is `SPAWN_ACTIVE` and cannot be deactivated. "Protection when active" is shown only when `type->can_use("GASMASK")` or `"PAPR_MASK_ACTIVATE"`. `can_use` only searches use_methods, while vanilla gas masks list GASMASK as a tick_action and `GASMASK_ACTIVATE` as the use action (`tool_armor.json:2074-2075`). So in practice the line only ever appears for PAPR masks. Separately, `tname` suppresses " (active)" only for ids ending in `_on`, so items named `foo_active` get a redundant suffix, and modders have no way to opt out.

```cpp
if( is_power_armor() || type->get_id() == itype_rm13_armor ) { ...
    tmp = tmp.convert( itype_id( tmp.typeId().str() + "_on" ) ); }
if( type->can_use( "GASMASK" ) || type->can_use( "PAPR_MASK_ACTIVATE" ) ) { ... "Protection when active" }
if( it.active && ... && !string_ends_with( it.typeId().str(), "_on" ) ) { return _( " (active)" ); }
```

**Fix:** Take the active form from the item's transform use_action (`iuse_transform::target`) or from a new armor field `"active_form": "my_suit_engaged"`. Show "When active" for any armor that has one, skip it otherwise, and drop the rm13 special case. Replace the GASMASK/PAPR check with `environmental_protection_with_filter > 0`. Replace the `_on` string test with a flag `NO_ACTIVE_SUFFIX` (or `"show_active_suffix": false`).

### IINF-J04 · Gun and gunmod info hide many loaded JSON fields, and the two blacklist labels are swapped
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_info.cpp:1570`, `src/item_info.cpp:1707`, `src/item_info.cpp:1721`, `src/item_info.cpp:1092`, `src/item_factory.cpp:3842`

`gunmod_info` never shows these loaded fields (`item_factory.cpp:3842-3888`): `mode_modifier`, `hide_modes`, `ammo_effects`, `energy_drain_modifier`/`multiplier`, `ammo_to_fire_modifier`/`multiplier`, `overheat_threshold_*`, `cooling_value_*`, `heat_per_shot_*`, `consume_chance`/`divisor`, `shot_spread_multiplier_modifier`, `overwrite_min_cycle_recoil`, `barrel_length`, `install_time` and `is_bayonet`. `gun_info` never shows the gun's overheat threshold, cooling or heat per shot; only the HUD overheat glyph reflects heat. An installed mod's added modes and energy drain do appear in the host gun's "Fire modes" and energy lines (1456-1511), so those are hidden only on the gunmod itself. Separately, the blacklist labels are swapped: `blacklist_slot` (slot names) prints under "Incompatible with gunmods:" and `blacklist_mod` (mod ids) under "Incompatible with slots:".

```cpp
std::string mod_black_str = _( "<bold>Incompatible with gunmods: </bold> " );
for( const gunmod_location &black : mod.blacklist_slot ) ...
std::string mod_black_str = _( "<bold>Incompatible with slots: </bold> " );
for( const itype_id &black : mod.blacklist_mod ) ...
```

**Fix:** Swap the two labels. Add `iteminfo_parts` entries and lines for the listed fields, e.g. "Adds firing modes: auto (4)", "Heat per shot: +2", "Overheat threshold: x1.5", "Install time: 30 s". Add a gun "Heat: per shot / cooling / threshold" block when `overheat_threshold > 0`.

### IINF-J05 · Gunmod compatibility hardcodes six item ids and five location names
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/item_gun_tool_ammo.cpp:3070`, `:3078`, `:3081`, `:3084`, `:3087`, `:3094`, `:3098`, `src/item_gun_tool_ammo.cpp:103`, `src/itype.cpp:25`, `src/game_inventory.cpp:1342`

`item::is_gunmod_compatible` hardcodes rules for specific content:
- `hand_crossbow` only accepts mods that also target `pistol`.
- `waterproof_gunmod`, `tuned_mechanism` and `brass_catcher` are refused when the gun already has the matching flag.
- `stock_none` blocks `stock accessory` mods.
- `arredondo_chute` is always refused.
- `magazine`, `mechanism`, `loading port` and `bore` mods require the gun to be unloaded.

None of these rules can be expressed in JSON. A modder cannot write their own "already waterproof" mod, an "install only when unloaded" location, or a small gun that only takes small mods. The existing `blacklist_slot`/`blacklist_mod` fields cannot test gun flags or the loaded state. Two of the ids are dead: `tuned_mechanism` has no JSON definition, and `waterproof_gunmod` appears only in `obsoletion_and_migration_0.I/gun.json`. No data item uses the `magazine` location. The location checks compare against translated text; IINF-B02 covers that bug, which also affects the removal check at `game_inventory.cpp:1342`.

```cpp
} else if( typeId() == itype_hand_crossbow && !mod.type->gunmod->usable.count( pistol_gun_type ) ) {
} else if( mod.typeId() == itype_waterproof_gunmod && has_flag( flag_WATERPROOF_GUN ) ) {
} else if( mod.typeId() == itype_arredondo_chute ) { return ...( "chute needs modification before attaching" );
```

**Fix:** Make the rules data-driven on the gunmod side:
- `"blacklist_gun_flags": [ "WATERPROOF_GUN" ]`, with an optional `"blacklist_message"`.
- A new `gunmod_location` JSON type with fields `requires_unloaded` and `requires_mod_location`, so mods can also set `"requires_unloaded": true`.
- `"installable": false` for templates such as `arredondo_chute`.
- A gun field `"mod_targets_override": [ "pistol" ]` for the hand-crossbow case.

Compare locations with `location.str()`, never with `name()`.

### IINF-J06 · Clothing mods support only five hardcoded damage types, so stab and modded damage types never get a bonus
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/item_armor.cpp:1114`, `src/item_armor.cpp:1559`, `src/clothing_mod.h:20`, `src/item_degrade.cpp:713`, `src/item_degrade.cpp:776`

Damage types are defined in JSON, but clothing_mod `mod_value.type` is a fixed C++ enum. `get_clothing_mod_val_for_damage_type` (marked FIXME) maps only bash, cut, bullet, heat and acid, and returns 0 for anything else. `item::_resist` adds that value directly, without the `derived_from` fallback it uses for materials. As a result, Kevlar or steel padding with a `cut` bonus gives nothing against `stab`, even though stab is derived from cut at x0.8. A custom damage type from Magiclysm or Xedra can never be padded against. The values are stored under fixed `clothing_mod_<enum>` var keys, so the enum cannot be extended from data.

```cpp
float item::get_clothing_mod_val_for_damage_type( const damage_type_id &dmg_type ) const {
    // FIXME: Hardcoded damage types
    if( dmg_type == damage_bash ) ... else if( dmg_type == damage_acid ) {...}
    return 0.0f; }
enum clothing_mod_type { acid, fire, bash, cut, bullet, encumbrance, warmth, invalid };
```

**Fix:** Let `mod_value` accept any damage type, e.g. `{ "damage_type": "stab", "value": 1.2, "proportion": [ "thickness" ] }`. Keep `encumbrance` and `warmth` as special keys, and store the results as a `map<damage_type_id, float>`. Fall back through `derived_from`, so a cut bonus also applies to stab at 0.8 unless overridden.

### IINF-J07 · EOCs cannot read or set gun, magazine or ammo state; writing `n_val('power')` unloads the gun
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/talker_item.cpp:100`, `src/talker_item.cpp:105`, `src/talker_item.cpp:161`, `src/item_gun_tool_ammo.cpp:287`, `src/item_gun_tool_ammo.cpp:379`, `src/condition.cpp:2525`, `src/math_parser_diag.cpp:1880`

`talker_item` exposes nothing gun-specific. There is no accessor for ammo remaining or capacity, the loaded ammo id, the magazine, installed gunmods, gun mode, or heat. The math table has `gun_damage`/`melee_damage`, but no ammo or gunmod functions. There are no effects to reload, unload, set ammo, or install or remove a gunmod. The only access is the battery proxy. `power_cur()` returns `ammo_remaining()` of any ammo (documented as "battery charges"). On a non-battery gun, `set_power_cur` clamps the value to `ammo_capacity(ammo_battery) = 0`. `ammo_set` then reaches `qty <= 0` and calls `ammo_unset()`, which silently empties the gun.

```cpp
void talker_item::set_power_cur( units::energy value ) {
    me_it->get_item()->ammo_set( itype_battery, clamp( static_cast<int>( value.value() ), 0,
                                  ...ammo_capacity( ammo_battery ) ) ); }
// item::ammo_set
if( qty <= 0 ) { ammo_unset(); return *this; }
```

**Fix:**
- Make `set_power_cur` a no-op unless the item really holds battery ammo.
- Add item-talker math functions: `n_ammo_remaining('pocket_id'|'')` (assignable), `n_ammo_capacity('9mm')`, `n_has_gunmod('suppressor')`, and `n_gun_mode()` / `n_gun_heat()` (assignable).
- Add effects: `{ "npc_set_ammo": "9mm_fmj", "count": 15 }`, `{ "npc_install_gunmod": "suppressor", "force": true }` and `{ "npc_set_gun_mode": "AUTO" }`.

### IINF-J08 · No event hooks for reload, gunmod install/remove, armor damage or ablative plate break; `non_functional` only works for plates
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/character_armor.cpp:324`, `src/character_armor.cpp:336`, `src/character_armor.cpp:372`, `src/character_attire.cpp:1904`, `src/character_guns.cpp:182`, `src/activity_actor.cpp:11379`, `src/item_gun_tool_ammo.cpp:3388`, `src/event.h:24`

These moments cannot trigger an EOC:
1. **Reload and unload.** `item::reload` sends nothing. `character_finished_activity` carries only the activity id and a canceled flag, not the item.
2. **Gunmod install (success or failure) and removal.** `Character::gunmod_add` and `gunmod_add_activity_actor::finish` send no event.
3. **Armor damaged but not destroyed.** No event exists for this.
4. **Ablative plates.** The DESTROYED branch (372-383) and the TRANSFORMED branch (336-363) send nothing. Only worn armor sends `character_armor_destroyed` (`character_attire.cpp:1904`).

Separately, `non_functional` is read only for ablative plates (`ITEM.md:364` says so), so a destroyed helmet cannot turn into a "cracked helmet".

```cpp
// character_attire.cpp:1904 (worn armor)
cata::event e = cata::event::make<event_type::character_armor_destroyed>( guy.getID(), armor.typeId() );
get_event_bus().send_with_talker( &guy, &loc, e );
// character_armor.cpp:372-383 (ablative): describe_damage, destroyed_armor_msg, remove_item; no event
```

**Fix:**
- Add item-talker events: `character_reloads_item {character, itype, ammo_itype, qty}`, `character_unloads_item`, `character_installs_gunmod {character, gun_itype, mod_itype, success}`, `character_removes_gunmod` and `character_armor_damaged {character, itype, damage_type, bodypart}`.
- Send `character_armor_destroyed` and a new `character_armor_transformed` from the ablative paths.
- Honour `non_functional` in `outfit::absorb_damage` for normal armor.

### IINF-J09 · Ammo effects have no description field; item info hardcodes text for five effects
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_info.cpp:996`, `src/item_info.cpp:97`, `src/ammo_effect.cpp:103`

`ammo_effect::load` has no `description` or `info` field. The ammo panel prints text only for RECYCLED, MATCHHEAD, BLACKPOWDER and INCENDIARY/IGNITE, using C++ literals. Every other effect is invisible: modded ones (`eoc`, `spell_data`) and most vanilla ones (BEANBAG, FLASHBANG, EXPLOSIVE...). Gun and gunmod `ammo_effects` (merged at `item_gun_tool_ammo.cpp:2221`) are never shown at all. The severity is low because the item's own description can explain special ammo, and vanilla mostly does that.

```cpp
if( ammo.ammo_effects.count( ammo_effect_RECYCLED ) && ... ) { fx.emplace_back( _( "This ammo has been <bad>hand-loaded</bad>..." ) ); }
```

**Fix:** Add `"info": { "str": "Releases a sedative on hit." }` (a translation, with an optional `"info_color": "bad"`) to `ammo_effect`. Print it for every effect in `ammo_info` and in `gunmod_info`, and move the five hardcoded strings into `data/json` ammo effects.

### IINF-J10 · Multi-charge ammo (1 unit = 500 charges) is hardcoded to the `plutonium` ammotype
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_gun_tool_ammo.cpp:3346`, `src/item_gun_tool_ammo.cpp:3506`, `src/item_gun_tool_ammo.cpp:3528`, `src/item_gun_tool_ammo.cpp:3433`, `src/game_constants.h:58`, `src/activity_actor.cpp:6672`, `src/crafting.cpp:5763`

The reload capacity math and the loaded quantity both special-case `ammo_plutonium`, using the global `PLUTONIUM_CHARGES = 500`. No other ammo can express "one item loads N charges", and the ratio cannot be tuned. The pocket-targeted integral-magazine path in `item::reload` (3433-3457) inserts charges 1:1, so the code paths are already out of sync. That desync is latent, because no data puts plutonium in such a pocket. The only plutonium user is the Aftershock minireactor. See also IINF-B16.

```cpp
if( ammo_obj.ammo_type() == ammo_plutonium ) { remaining_capacity = remaining_capacity / PLUTONIUM_CHARGES + ...; }
if( ammo->ammo_type() == ammo_plutonium ) { item_copy.charges = std::min( qty * PLUTONIUM_CHARGES, ammo_capacity( ammo_plutonium ) ); }
```

**Fix:** Add an ammo (or ammunition_type) field `"charges_per_unit": 500`. Use it in `reload_option::qty` and in both `item::reload` branches, and remove the `ammo_plutonium` checks.

### IINF-J11 · Gunmod install odds and the repair-kit helper options are hardcoded to two item ids
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character_guns.cpp:148`, `src/character_guns.cpp:248`, `src/character_guns.cpp:258`, `src/character_guns.cpp:35`, `src/activity_actor.cpp:11397`

For INSTALL_DIFFICULT mods, `gunmod_installation_odds` is a fixed formula. The only helpers offered are `small_repairkit` (100 charges, x2 success, half risk) and `large_repairkit` (25 charges, x3 success, no risk), wired in as literals. A modded gunsmith bench, toolkit or proficiency cannot improve the odds, and total conversions that remove these kits lose the options entirely. The impact is narrow: only 6 vanilla mounts use INSTALL_DIFFICULT, and `min_skills` already shifts the odds.

```cpp
actions.emplace_back( [&] { tool = "small_repairkit"; qty = 100; roll *= 2; risk /= 2; } );
... tool = "large_repairkit"; qty = 25; roll *= 3; risk = 0;
```

**Fix:** Add a JSON type such as `{ "type": "gunmod_install_aid", "tool": "small_repairkit", "charges": 100, "success_mult": 2.0, "risk_mult": 0.5 }`, or key the bonus on a tool quality (`"quality": "GUNSMITHING", "level": 2`). Add a gunmod field `"install_difficulty": 3` and allow proficiency bonuses.

### IINF-J12 · Melee DPS estimator hardcodes RAPID technique numbers and reference monsters
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_gun_tool_ammo.cpp:482`, `src/item_gun_tool_ammo.cpp:532`, `src/item_gun_tool_ammo.cpp:114`

`effective_dps` models only the RAPID technique, using literal multipliers: damage x0.66 on every damage unit, and half the strikes at 0.33 move cost. This ignores the technique's JSON (`techniques.json:214-218`: movecost 0.5, damage 0.66 for bash/cut/stab only) and every other technique. The "Vs. Agile/Armored/Mixed" comparisons use fixed monster ids, which make no sense in total conversions. The result feeds item info, NPC weapon choice (`melee.cpp:2802`) and AIM sorting.

```cpp
{ to_translation( "Vs. Agile" ), { mon_zombie_smoker, true, true } },
{ to_translation( "Vs. Armored" ), { mon_zombie_soldier_no_weakpoints, true, true } },
```

**Fix:** Derive technique effects from `ma_technique` mult/flat bonuses for each technique the weapon has. Move the reference targets to JSON, e.g. `{ "type": "dps_reference", "id": "agile", "name": "Vs. Agile", "monster": "mon_zombie_smoker", "display": true, "evaluate": true }`.

### IINF-J13 · Clothing sizing uses hardcoded encumbrance multipliers
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/item_armor.cpp:363`, `src/item_armor.cpp:591`, `src/item_armor.cpp:601`, `src/item_armor.cpp:606`, `src/item_tname.cpp:431`

`get_sizing` maps `creature_size::tiny` to small and `huge` to big. The mismatch penalties are fixed (undersized x3, oversized on small characters x2), and FIT is fixed at `max(enc/2, enc-10)`. Mapping only tiny and huge is intentional: JSON_FLAGS.md UNDERSIZE and the HUGE mutation text match it. What remains is a niche gap: mods cannot retune the multipliers, and a mutation cannot say "counts as huge for clothing". Side note: `JSON_FLAGS.md:1520` says FIT "reduces encumbrance by one", which contradicts the code.

```cpp
if( has_flag( flag_FIT ) && has_flag( flag_VARSIZE ) ) { encumber = std::max( encumber / 2, encumber - 10 ); }
case sizing::small_sized_human_char: case sizing::small_sized_big_char: encumber *= 3;
```

**Fix:** Add a mutation field `"clothing_size": "huge"` (falling back to body size), and game-balance or armor fields `"fit_encumbrance": { "mult": 0.5, "max_reduction": 10 }`, `"undersize_mult": 3` and `"oversize_mult": 2`. Correct the FIT doc.

### IINF-J14 · Sort-armor layer text ignores per-portion `layers` JSON, and flag blurbs are a hardcoded list
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/armor_layers.cpp:214`, `src/armor_layers.cpp:406`, `src/item_factory.cpp:1452`, `src/body_status_ui.cpp:797`

`clothing_info::clothing_layer` reads only item-level flags (PERSONAL, SKINTIGHT, WAIST, OUTER, BELTED, AURA). Per-portion `layers` is the modern way to set layers, and finalize folds it into `all_layers` without setting flags. About 59 vanilla items, e.g. `basic_papr_helmet`, therefore show no layer line in the sort-armor and body-status screens. `clothing_flags_description` is a fixed 9-entry C++ table, even though `json_flag` already loads an `info` field (`flag.cpp:432`). This is a UI-only problem.

```cpp
if( worn_item.has_flag( flag_PERSONAL ) ) { layer = _( "This is in your personal aura." ); } else if( worn_item.has_flag( flag_SKINTIGHT ) ) ...
```

**Fix:** Build the layer text from `item::get_layer(bp)` and `layer_to_string`. Replace the flag table with each flag's JSON `info`, or a new `json_flag` field `"worn_info"`.

### IINF-J15 · ITEM.md drift: wrong key names, wrong `is_bayonet` semantics, and undocumented or unloaded gunmod fields
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `doc/JSON/ITEM.md:363`, `doc/JSON/ITEM.md:907`, `doc/JSON/ITEM.md:925`, `doc/JSON/ITEM.md:865`, `doc/JSON/ITEM.md:898`, `src/item_factory.cpp:3632`, `src/item_factory.cpp:3841`, `src/item_gun_tool_ammo.cpp:772`, `src/activity_actor.cpp:8671`

Five mismatches between ITEM.md and the code:
1. ITEM.md documents `"energy_shield_max_hp": false`; the loader reads the int field `max_energy_shield_hp`.
2. The typo `energy_drains_multiplier` is documented; the real key is `energy_drain_multiplier`.
3. `is_bayonet` is documented as "added to the base damage of the gun", but `damage_melee` takes the maximum of the gun's and the bayonets' damage.
4. Gunmod `reload_noise`/`reload_noise_volume` are documented but never loaded. The gun-level `reload_noise`, described as "seems to not work", does play when `reload_noise_volume > 0`.
5. `ammo_to_fire_modifier`, `ammo_to_fire_multiplier` and `min_str_required_mod_if_prone` are loaded but not documented.

```cpp
std::vector<int> opts = { res };
for( ... mod->type->gunmod->is_bayonet ) opts.push_back( mod->damage_melee( dt ) );
return *std::max_element( ... );
```

**Fix:** Correct the key names and the example value, and delete the typo line. Describe `is_bayonet` as "uses the higher of gun or bayonet damage per type". Either load the gunmod reload noise fields or remove them from the docs. Document the three missing fields.

### IINF-J16 · gun_type (gunmod compatibility class) is derived from skill plus a hardcoded bolt check
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/item_gun_tool_ammo.cpp:1136`, `src/item_gun_tool_ammo.cpp:1144`, `src/item_gun_tool_ammo.cpp:3066`, `src/item_gun_tool_ammo.cpp:4534`

`item::gun_type()` decides which gunmod `mod_targets` fit a gun. It is the skill id, except for archery guns, which become `crossbow` if they use ammo `bolt` or are `bullet_crossbow`, and `bow` otherwise. The code carries a TODO to move this to JSON. The reviewer found no current breakage: vanilla crossbows use skill `rifle`, and every crossbow-targeted gunmod also lists `rifle`. This is a modding wish, e.g. a crossbow firing a new `quarrel` ammotype would be classed as a bow. The related use of `get_player_character()` in `get_min_str` is covered by IINF-B06.

```cpp
// TODO: move to JSON and remove extraction of this from "GUN" (via skill id)
if( gun_skill() == skill_archery ) {
    if( ammo_types().count( ammo_bolt ) || typeId() == itype_bullet_crossbow ) { return ..."crossbow"; }
    else { return ..."bow"; } }
return gun_type_type( gun_skill().str() );
```

**Fix:** Add an optional `islot_gun` field `"gun_type": "crossbow"` (extracted for translation with context `gun_type_type`) that defaults to today's derivation. Add `"strength_proficiency": "archery"` for the min-str bonus.

### IINF-J17 · Wield, unfold and reload timing and melee flag multipliers are hardcoded formulas tied to flags
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/item_gun_tool_ammo.cpp:608`, `src/item_gun_tool_ammo.cpp:626`, `src/item_gun_tool_ammo.cpp:765`, `src/character_ammo.cpp:195`, `src/character_ammo.cpp:222`

These handling costs and damage multipliers are fixed formulas behind boolean flags:

| Flag / case | Hardcoded effect |
|---|---|
| SLOW_WIELD | 32/skill × volume/250 ml, capped at 700 |
| NEEDS_UNFOLD | 300 − 10×skill for guns, 150 − 10×skill for melee |
| REDUCED_BASHING | bash ×0.5 |
| DIAMOND | edged ×1.3 |
| MAG_BULKY | reload ×1.5 |
| STR_RELOAD | −20 moves per STR point |
| Non-gun reload | flat +100 moves |

Some of this can already be tuned from data: enchantments give per-type melee multipliers and RELOADING_SPEED, and SLOW_WIELD reads an item var `volume` (settable via itype `variables`). The genuinely missing piece is a fixed per-item wield or unfold time.

```cpp
int penalty = get_var( "volume", volume() / 250_ml ) * d; mv += std::min( penalty, 700 );
if( dt == damage_bash && has_flag( flag_REDUCED_BASHING ) ) { res *= 0.5; } if( dt->edged && has_flag( flag_DIAMOND ) ) { res *= 1.3; }
```

**Fix:** Add optional itype fields `"wield_time": "2 s"` and `"unfold_time": "3 s"`, a magazine field `"reload_time_mult": 1.5`, a gun field `"str_reload_bonus": 20`, and a `json_flag` field `"melee_damage_mult": { "bash": 0.5 }`.

### IINF-J18 · absorb_hit hardcodes the bio_ads forcefield, the power cost per hit and the SEESLEEP eye bonus
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/character_armor.cpp:167`, `src/character_armor.cpp:212`, `src/character_armor.cpp:115`

Only bionic `bio_ads` can absorb damage before armor, with fixed ranges: bash 1-4, cut 2-8, stab/bullet 4-16. USE_POWER_WHEN_HIT drains exactly 1 kJ per damage point. Flag SEESLEEP adds a fixed +8 eye environmental resist. Conditional `incoming_damage_mod` enchantments plus EOCs can approximate the bio_ads behaviour, but not per-hit random reduction with a power gate checked before the hit.

```cpp
if( has_active_bionic( bio_ads ) ) { ... if( elem.type == damage_bash ) { elem.amount -= rng( 1, 4 ); } ... }
const auto amount = units::from_kilojoule( du.amount );
if( bp == body_part_eyes && has_flag( json_flag_SEESLEEP ) ) { ret += 8; }
```

**Fix:** Add an enchantment block `"incoming_damage_absorb": [ { "damage_type": "bullet", "min": 4, "max": 16, "energy_cost": "10 kJ" } ]` and port bio_ads to it. Add an armor field `"energy_per_damage": "1 kJ"`. Turn the SEESLEEP bonus into a flag or enchantment value.

## 2. Bugs, oversights & quirks

### IINF-B01 · Ablative plates protect twice per hit, and still protect when the coverage roll misses them
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character_attire.cpp:1883`, `src/character_attire.cpp:1887`, `src/character_armor.cpp:320`, `src/character_armor.cpp:246`, `src/item_armor.cpp:1312`, `src/item_armor.cpp:1317`, `src/item_degrade.cpp:690`, `src/item_degrade.cpp:723`, `src/item_factory.cpp:1097`

`outfit::absorb_damage` first calls `ablative_armor_absorb`, which mitigates with the plate if `roll <= plate coverage`. It then calls `armor_absorb` on the carrier regardless of that roll. That path runs `mitigate_damage(du, sbp, -1)` → `item::resist` → `armor_made_of(sbp)`. The sub-bodypart overload of `armor_made_of` appends the first plate's materials to the carrier's own. Inferred plate materials have `cover = 100`, so `_resist` always counts them. The result:
- Hits that land on the plate subtract its resistance twice.
- Hits that miss the plate (e.g. rolls 46-100 against an ESAPI plate with coverage 45) still get full plate protection through the carrier.

So plates behave like 100%-coverage armor that sometimes counts double. This contradicts `doc/DEVELOPER_FAQ.md:81` (at most one plate per attack). The info panel shows the plate once and understates the real effect. Trigger: wear any ballistic vest with an `esapi_plate` and take rifle fire to `torso_upper`.

```cpp
// character_armor.cpp:320
ablative_armor.mitigate_damage( du );
// character_armor.cpp:246
armor.mitigate_damage( du, sbp, -1 );
// item_armor.cpp:1317-1320
auto abl_mat = ablative_armor.armor_made_of( bp );
if( !abl_mat.empty() ) { matlist.insert( ... abl_mat ... ); return matlist; }
// item_degrade.cpp:723
if( internal_roll < m->cover ) { ... resist += tmp_add; }
```

**Fix:** Add a parameter `armor_made_of(bp, include_ablative)` and pass `false` from the combat path (`item::resist` called from `Character::armor_absorb`). Then the carrier mitigates only with its own materials, and plates only mitigate in `ablative_armor_absorb`. Keep `true` for UI totals, labelled "with plate".

### IINF-B02 · Gunmod "must be unloaded" checks compare translated slot names; non-English players lose magazines
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_gun_tool_ammo.cpp:3087`, `src/item_gun_tool_ammo.cpp:3094`, `src/itype.cpp:25`, `src/item_container.cpp:121`, `src/item_contents.cpp:2222`, `lang/string_extractor/parsers/gunmod.py:10`, `src/game_inventory.cpp:1342`

`is_gunmod_compatible` compares `location.name()` with the English strings `"magazine"`, `"mechanism"`, `"loading port"`, `"bore"` and `"stock accessory"`. But `gunmod_location::name()` returns `_( _id )`, and `gunmod.py` extracts locations for translation. In any translated language these checks never match. The underbarrel check two lines earlier correctly uses `.str()`. Nothing else guards this. In a translated game, a player can install a bore conversion such as `retool_ar15_300blk`, which has `pocket_mods` for MAGAZINE_WELL, on an AR-15 with a loaded STANAG. `put_in(MOD)` → `update_modified_pockets` then drops the old well together with the magazine and its ammo, with an "Oops! deleted some items" debugmsg. The `stock_none` guard breaks the same way, and so does the removal check at `game_inventory.cpp:1342`.

```cpp
( mod.type->gunmod->location.name() == "magazine" || ... == "mechanism" ||
  ... == "loading port" || ... == "bore" ) && ( ammo_remaining( ) > 0 || magazine_current() )
// itype.cpp
std::string gunmod_location::name() const { return _( _id ); }
```

**Fix:** Compare `location.str()` everywhere in `is_gunmod_compatible` and in `game_inventory.cpp:1342`. For the longer-term data-driven version, see IINF-J05.

### IINF-B03 · gun_recoil jumps 2x at handling 1.0, gets worse as handling improves below 1, and can go NaN
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_gun_tool_ammo.cpp:1325`, `src/item_gun_tool_ammo.cpp:1339`, `src/item_gun_tool_ammo.cpp:1347`, `src/ranged.cpp:1264`, `src/item_factory.cpp:761`, `data/json/items/gunmod/stock.json:67`, `data/json/items/gun/exodii.json:202`

Effective handling is `p = wt^0.8 * (h/10)^1.2`. For `p > 1` recoil is `qty/p`; otherwise it is `qty*(1+|p|)`. That gives a 2x jump at p = 1, and below 1 the penalty shrinks as handling gets worse (p = 0.99 gives 1.99x, p = 0 gives 1.0x). Example: the rugerlcp (267 g) gets about 1.84x base recoil, while the swbodyguard (337 g) gets 0.99x. Arm strength 1 can give up to 2x, but arm strength 0 gives 1x. If the summed handling goes negative, `pow(negative, 1.2)` is NaN and is then cast to int (undefined behaviour). This happens in vanilla: launchers default to handling 10, `folding_stock_folded` is −15, and the Exodii sapra launcher has a stock slot.

```cpp
handling = std::pow( wt, 0.8 ) * std::pow( handling, 1.2 );
if( handling > 1.0 ) { return qty / handling; }
else { return qty * ( 1.0 + std::abs( handling ) ); }
```

**Fix:** Clamp summed handling to >= 0 before `pow`. Use a continuous, monotonic curve, e.g. `p >= 1.0 ? qty / p : qty * ( 2.0 - p )`. Add a unit test that sweeps weight and arm strength and checks that recoil is monotonic.

### IINF-B04 · Sided armor always goes on the left: on_wear's "least encumbrance side" logic is dead
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_armor.cpp:442`, `src/item_armor.cpp:456`, `src/character_attire.cpp:325`, `src/character_attire.cpp:411`, `src/character_attire.cpp:418`, `src/creature.cpp:2692`, `src/bodypart.cpp:1006`

For sided items that have no rigid conflict, `on_wear` sets LEFT, sums `get_part_encumbrance()` over all parts, sets RIGHT, sums again, and keeps the smaller side. `get_part_encumbrance()` reads the cached `encumb_data`, which `set_side()` does not refresh. `on_wear` also runs (line 411) before `calc_encumbrance` (line 418). Both sums are therefore always equal, and the item always goes LEFT. Even with a fresh cache, comparing whole-body totals would not separate the sides. Trigger: wear two leg pouches, holsters or quivers (117 sided items in core). Both stack on the left leg with layering penalties. Players can swap sides by hand; NPCs (`npc.cpp:993`, `1187`) cannot.

```cpp
set_side( side::LEFT );  for(...) lhs += p.get_part_encumbrance( bp );
set_side( side::RIGHT ); for(...) rhs += p.get_part_encumbrance( bp );
set_side( lhs <= rhs ? side::LEFT : side::RIGHT );
```

**Fix:** Compute each side directly: sum `get_encumber()` of worn items covering only the parts this item would cover on that side. Alternatively, prefer the side with fewer items on the same layer, as `one_per_layer_sidedness` already does.

### IINF-B05 · is_reloadable() reads the wrong pocket on multimag guns with an integral magazine
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_gun_tool_ammo.cpp:4467`, `src/item_gun_tool_ammo.cpp:1642`, `src/item_gun_tool_ammo.cpp:1449`, `src/item_contents.cpp:1419`, `src/character_ammo.cpp:248`, `src/game.cpp:7184`

For a MAGAZINE pocket, `is_reloadable()` calls the aggregate `remaining_ammo_capacity()`. Whenever any well is loaded, that returns the capacity of the first loaded MAGAZINE_WELL's magazine. On items with an integral projectile pocket plus a battery well (Aftershock `afs_rebar_rifle`, `afs_marsec_t72`, `afs_antitank_railgun`, `exo_rod_launcher`), inserting a full power cartridge while the integral pocket is empty makes `is_reloadable()` return false. `rate_action_reload`, `reload_wielded` and `find_reloadables` then all refuse ("You can't reload a ..."). `afs_marsec_t72` can still get through via its built-in shotgun mod. Relatedly, the aggregate `ammo_remaining()` adds battery kJ to projectile counts (1449-1451).

```cpp
} else if( pocket->is_type( pocket_type::MAGAZINE ) ) { ... if( remaining_ammo_capacity() > 0 ) { return true; }
// First-loaded-mag fallback
if( const item *mag = magazine_current() ) { return mag->remaining_ammo_capacity(); }
```

**Fix:** In `is_reloadable`, test the pocket itself: `for ammotype at : pocket->ammo_types()`, check `pocket->remaining_ammo_capacity( at ) > 0`. Filter `magazines_current()` by ammotype in `ammo_remaining`.

### IINF-B06 · get_min_str() and bionic-tool ammo_capacity read the avatar's stats for every character
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/item_gun_tool_ammo.cpp:4536`, `src/item_gun_tool_ammo.cpp:4541`, `src/item_gun_tool_ammo.cpp:4546`, `src/item_gun_tool_ammo.cpp:1758`, `src/character.cpp:3018`, `src/npc.cpp:1419`, `src/ranged.cpp:1838`, `src/item_gun_tool_ammo.cpp:1398`

`item::get_min_str()` takes no character. It applies the archery proficiency strength bonus and the prone-only modifiers from `get_player_character()`. Its callers evaluate NPCs too: `Character::meets_stat_requirements`, the NPC starting-gear stat bump, `ranged.cpp`'s strength ratio and arm-stamina burn, and `gun_range(p)`. So NPCs inherit the avatar's proficiency and posture: once the avatar learns archery, unskilled NPCs can draw heavy bows. The `USES_BIONIC_POWER` branch of `ammo_capacity()` likewise returns the avatar's max bionic power regardless of who holds the item.

```cpp
const Character &p = get_player_character();
min_str -= p.get_proficiency_bonus( "archery", proficiency_bonus_type::strength );
if( p.is_prone() ) {
if( has_flag( flag_USES_BIONIC_POWER ) ) { return units::to_kilojoule( get_player_character().get_max_power_level() ); }
```

**Fix:** Change the signature to `get_min_str( const Character &who )` and pass the wielder at every call site. Keep a no-argument overload only for UI. Add a carrier parameter to the bionic branch of `ammo_capacity`.

### IINF-B07 · Plate cover_vitals / cover_ranged / cover_melee are ignored once the plate is in a carrier
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_armor.cpp:835`, `src/item_armor.cpp:870`, `src/creature.cpp:1041`, `src/character_armor.cpp:309`, `data/json/items/armor/ballistic_armor.json:1480`

Both `get_coverage` overloads merge plate coverage with `ablative_armor.get_coverage( bodypart )` and drop the requested `cover_type`, so the plate's default coverage is used for VITALS, RANGED and MELEE queries. As a result, `Creature::get_crit_factor` sees 80 instead of 100 for `iotv_shoulder_plate`, and the carrier's ranged gate uses 80 instead of 95. The UI shows the same reduced values. The plate's own hit roll (`character_armor.cpp:309`) passes the type correctly.

```cpp
coverage = std::max( coverage, ablative_armor.get_coverage( bodypart ) );
```

**Fix:** Pass the type through, `ablative_armor.get_coverage( bodypart, type )`, at both sites.

### IINF-B08 · ammo_required() truncates to int, so a sub-1.0 multiplier gives infinite free shots
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/item_gun_tool_ammo.cpp:1783`, `src/item_gun_tool_ammo.cpp:1425`, `src/ranged.cpp:1116`, `src/ranged.cpp:1308`

`ammo_required()` returns `(ammo_to_fire * multiplier) + modifier` through an implicit float-to-int conversion. A result of 0 makes `shots_remaining` return 1000, skips the out-of-ammo check, and consumes nothing, while the loaded round still applies its damage. A negative result gives negative shot counts and "Cannot consume negative quantity" debugmsgs. The multimag path (`effective_qty`) rounds any positive cost up to 1, so the two paths disagree. No shipped content reaches 0 (Xedra's lowest case gives 2), so this is a latent trap for modders, plus a floor-instead-of-round bias (5 × 0.75 gives 3).

```cpp
return ( type->gun->ammo_to_fire * multiplier ) + modifier;
```

**Fix:** When `ammo_to_fire > 0`, return `std::max( 1, (int)std::lround( base * mult ) + modifier )`, matching `effective_qty`. Warn at load time when mods could bring the cost to 0.

### IINF-B09 · Building item info runs snippet effect_on_examine EOCs as a side effect
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/item_info.cpp:307`, `src/item_info.cpp:313`, `src/recipe_dictionary.cpp:210`, `src/wish.cpp:1109`, `src/item.cpp:272`

The const `item::basic_info` applies the snippet's `effect_on_examine` EOC and calls `add_snippet` whenever DESCRIPTION info is built. Some callers never show the item to the player: the crafting description search builds info for freshly constructed result items, and so does the debug wish menu. These can fire lore EOCs and burn "first seen" state. The EOC dialogue also has a `nullptr` beta, so the EOC cannot refer to the item. In current data the only real trigger is the debug wish menu, because the `artisan_docs` items are not craftable.

```cpp
if( !get_avatar().has_seen_snippet( snip_id ) ) { ... dialogue d( get_talker_for( get_avatar() ), nullptr );
    examine_effect.value().apply( d ); ... get_avatar().add_snippet( snip_id ); }
```

**Fix:** Move the EOC trigger to the explicit examine/read UI paths, or gate it behind an `iteminfo_parts` flag that only those paths set. Pass an item talker as beta.

### IINF-B10 · Item name "+N" mod counter counts factory built-in and default mods
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_tname.cpp:203`, `src/item.cpp:242`

The filter `!built_in.count || !default.count` is true unless a mod is in both lists, and no mod ever is. So stock guns show "+N": an M249 with its built-in bipod is named "M249+1", and guns with default mods show "+2".

```cpp
if( !it.type->gun || !it.type->gun->built_in_mods.count( mod->typeId() ) ||
    !it.type->gun->default_mods.count( mod->typeId() ) ) { amt++; }
```

**Fix:** `!gun || ( !built_in_mods.count( id ) && !default_mods.count( id ) )`.

### IINF-B11 · Armor info merges body parts with different full/empty encumbrance into one line
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_info.cpp:2121`, `src/item_info.cpp:2292`, `src/item_info.cpp:2322`

`armor_encumb_data` is a `std::map` key, but its `operator<` compares only the current encumbrance. Parts with the same base encumbrance but a different min/max fall into one group, which shows the first-inserted part's "When full" value. About 69 core items are affected, e.g. `suit` (torso 7-13, legs 7-10, arms 7). This is display-only.

```cpp
bool operator<( const armor_encumb_data &lhs, const armor_encumb_data &rhs ) { return lhs.encumb < rhs.encumb; }
```

**Fix:** Compare `std::tie( encumb, encumb_min, encumb_max )`.

### IINF-B12 · Info for an active underbarrel gunmod mixes in the host gun's data
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_info.cpp:4430`, `src/item_info.cpp:1316`, `src/item_info.cpp:1505`, `src/item_info.cpp:1515`, `src/item_info.cpp:1521`, `src/item_info.cpp:1376`, `src/item_info.cpp:1160`, `src/item_info.cpp:1108`

When an aux gunmod such as an M203 is the current mode, `get_info` calls `gun_info(aux)` on the host and says "Stats of the active gunmod are shown". Several blocks still read `this` (the host):
- "Compatible magazines" lists the rifle's STANAGs.
- The Variants gate (1376) and the default-ammo fallback (1160) read the host.
- The Mods block (1521) uses the aux's empty `valid_mod_locations`, so it disappears.
- Fire modes come from `mod->gun_all_modes()` but are filtered by `target == this`, so none are listed.

```cpp
if( e.second.target == this && !e.second.melee() ) {
if( !magazine_integral() && ... ) { if( uses_magazine() ) { info.emplace_back( "DESCRIPTION", print_compatible_mags_or_flags() );
```

**Fix:** Use `mod->` consistently in `gun_info` (modes filtered by `target == mod`, `mod->magazine_integral()`, `mod->print_compatible_mags_or_flags()`). Show the host's mod slots separately.

### IINF-B13 · Sort-armor screen: "equip here" leaves encumbrance stale, and the layer comparisons are lexicographic
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/armor_layers.cpp:1108`, `src/armor_layers.cpp:955`, `src/armor_layers.cpp:472`, `src/armor_layers.cpp:1045`, `src/armor_layers.cpp:1229`, `src/item_armor.cpp:674`, `src/character_attire.cpp:917`

There are three problems:
1. EQUIP_ARMOR_HERE wears the item at its default position, then splices it to the cursor without recalculating encumbrance. The MOVE_ARMOR path does recalculate (955), so the displayed layering penalty stays stale here.
2. `get_item_penalties`' no-sub-part branch compares whole `layer_level` vectors lexicographically. No shipped body part reaches this branch.
3. SORT_ARMOR and `outfit::sort` order items by lexicographic `all_layers`, ignoring per-bodypart layers such as the BELTED layer added for MOLLE pouches. The real penalty (`character_attire.cpp:917`) uses `get_layer(bp)`, so "sort into natural layer order" can create penalties.

```cpp
worn.splice( cursor_it, worn, *new_equip_it );  // no recalc (cf. 955: guy.calc_encumbrance();)
return l.get_layer() < r.get_layer();
```

**Fix:** Call `guy.calc_encumbrance()` after the splice. Compare the highest layer (as the sub-part branch does). Sort by the highest per-bodypart layer.

### IINF-B14 · gunmod_add installs the first mod of the same type, not the one the player selected
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character_guns.cpp:189`, `src/character_guns.cpp:209`, `src/character_guns.cpp:218`, `src/iuse.cpp:5046`

After wielding the gun, the code searches the whole inventory for items of the mod's type and installs `mods.front()`. With two copies in different states (damaged, faulted, or a loaded vs. an empty launcher), the wrong copy can be installed. The possession guard also uses `&&` where `||` was meant. That guard never fires in practice, because only inventory items reach this path.

```cpp
if( !has_item( gun ) && !has_item( mod ) ) {
std::vector<item *> mods = items_with( [&mod_type]( const item & it ) { return it.typeId() == mod_type; } );
item &moved_mod = *mods.front();
```

**Fix:** Hold the selected mod in an `item_location` (or match by uid) across the wield, and use `||` in the guard.

### IINF-B15 · Small copy-paste bugs in item info (barrel table check, e-book dedup, colour tag)
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_info.cpp:875`, `src/item_info.cpp:3054`, `src/item_info.cpp:2525`

There are three small bugs:
- **(a)** `ammo_by_barrel_info` compares `medium_damage` with `large_dispersion`, so the table can appear when nothing varies. This only applies to ammo that has barrel-length data.
- **(b)** The e-book recipe list checks for duplicates by searching for `name` but stores `"<bold>"+name+"</bold>"`. Known recipes therefore repeat and inflate the "N unique recipes" count.
- **(c)** `<bad>` is closed with `</info>`. This has no visible effect, because `replace_colors` maps every closing tag to `</color>`.

```cpp
small_dispersion != medium_dispersion || medium_damage != large_dispersion ) {
```

**Fix:** Compare `medium_dispersion`. Search for the formatted string, or use a `std::set<recipe_id>`. Close the tag with `</bad>`.

### IINF-B16 · Plutonium reload into a partly filled magazine loses the cell: the excess cap uses total capacity
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_gun_tool_ammo.cpp:3507`, `src/item_gun_tool_ammo.cpp:3530`, `src/item_pocket.cpp:1609`, `data/mods/aftershock_exoplanet/vehicles/part_items.json:101`

The inserted charges are capped at `ammo_capacity(ammo_plutonium)`, the total capacity, not the remaining space. Example: a 1000-capacity minireactor holds 600. One cell is consumed and 500 charges are put in. `can_contain` rejects the insert with a "too many charges" debugmsg, and the cell is lost. The code comment says the excess should be wasted. This only affects the Aftershock minireactor reloaded as a loose item.

```cpp
// any excess is wasted rather than overfilling the item
item_copy.charges = std::min( qty * PLUTONIUM_CHARGES, ammo_capacity( ammo_plutonium ) );
```

**Fix:** Cap at `ammo_capacity( ammo_plutonium ) - ammo_remaining()`. See IINF-J10 for removing the special case.

### IINF-B17 · Ablative plate break message: untranslated verb, inconsistent visibility for NPCs
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character_armor.cpp:342`, `src/character_armor.cpp:346`, `src/character_armor.cpp:348`, `src/character_armor.cpp:375`, `src/character_armor.cpp:379`, `src/character_armor.cpp:125`

The fallback `damage_verb = "is shattered"` is not wrapped in `_( )`. The Aftershock exosuit plates have no JSON `damage_verb`, so they show English text in every language. In the TRANSFORMED branch, floating text appears only when `is_avatar()`, and the break message uses `add_msg_if_player`, so an NPC's plate transforming is never reported. In the DESTROYED branch, floating text appears whenever the wearer is visible, and `destroyed_armor_msg` uses `add_msg_player_or_npc`.

```cpp
std::string damage_verb = "is shattered";
if( is_avatar() ) { SCT.add(...) }                                  // TRANSFORMED
if( get_player_view().sees( here, *this ) ) { SCT.add(...) }        // DESTROYED
```

**Fix:** Wrap the literal in `_( )`. Use the `sees()` check and `add_msg_player_or_npc` in both branches.

## 3. Performance

### IINF-P01 · mitigate_damage computes resistance for every damage type (with RNG rolls) to use only one
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_armor.cpp:1218`, `src/item_armor.cpp:1230`, `src/damage.cpp:735`, `src/damage.cpp:745`, `src/item_degrade.cpp:689`, `src/item_degrade.cpp:725`, `src/character_armor.cpp:246`, `src/character_armor.cpp:284`, `src/character_armor.cpp:320`, `src/character_attire.cpp:1858`, `src/character_attire.cpp:1887`

Both `mitigate_damage` overloads build a full `resistances` object. That loops over every damage type (10 in core, 17 with Magiclysm/Aftershock) and calls `item::resist()` for each. Each call runs `armor_made_of` (vector allocation, `covers()`, ablative recursion) and `get_thickness`. With `roll = -1`, it also draws `rng(0,99)` once per material. Only `du.type` is read afterwards. This runs on every hit, for every covering layer, once or twice per layer. 90-95% of the work is discarded, and the extra RNG draws shift the random stream.

```cpp
const resistances res = resistances( *this, false, roll, bp );
const float mitigation = res.get_effective_resist( du );
// damage.cpp
for( const damage_type &dam : damage_type::get_all() ) { set_resist( dam.id, armor.resist( dam.id, to_self, bp, roll ) ); }
```

**Fix:** Compute only the incoming type, keeping the `is_armor()`/`is_pet_armor()` guard: `r = du.type->no_resist ? 0 : resist( du.type, false, bp, roll ); mitigation = max( r - du.res_pen, 0 ) * du.res_mult * du.unconditional_res_mult;`, then update `res_pen` and `amount` as `type_resist`/`get_effective_resist` do. Expect roughly 10-17x less armor math per hit per layer.

### IINF-P02 · final_info scans every armor template with can_contain on every item-info build
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_info.cpp:4151`, `src/item_info.cpp:4161`, `src/item_info.cpp:4286`, `src/item_info.cpp:4037`, `src/item_factory.cpp:5353`, `src/recipe_dictionary.cpp:200`, `src/recipe_dictionary.cpp:313`, `src/surroundings_menu.cpp:186`, `src/surroundings_menu.cpp:939`, `src/surroundings_menu.cpp:1201`

Every info build calls `can_contain_directly()` against every armor template whose biggest pocket is at least the item's volume. For small items that is about 415 templates, and each failed check formats a message. This happens before the `parts->test(DESCRIPTION_ARMOR_CONTAINERS)` check, so queries that exclude that section pay too. Hot callers:
- The crafting `d:` description search builds info for every recipe result (about 5,600 recipes), which costs millions of `can_contain` calls on the first search.
- The surroundings menu rebuilds the selected item's info every 10 ms frame.
- The crafting GUI rebuilds info on every highlight.

Two minor extras: the vehicle-part section copies matching `vpart_info` structs by value, and the milling section copies a whole `recipe`.

```cpp
auto [begin, end] = item_controller->get_armor_containers( volume() );
for( ... ) { if( iter->can_contain_directly( *this ).success() ) {   // before the parts->test at :4161
```

**Fix:**
- Move the scan inside `if( parts->test( DESCRIPTION_ARMOR_CONTAINERS ) )`.
- Stop counting past the display threshold, or cache the result per itype.
- Use `std::vector<const vpart_info *>` (ideally from a static itype → vpart multimap).
- Take the milling recipe by `const &`.

### IINF-P03 · Every iteminfo line runs replace_colors twice plus a std::stringstream
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_info.cpp:4525`, `src/item_info.cpp:4529`, `src/item_info.cpp:4537`, `src/output.cpp:1035`, `src/output.cpp:1015`

The `iteminfo` constructor calls `replace_colors()` on both `Name` and `Fmt`. Each call runs 16 `string_replace` passes, each taking the text by value, and builds temporary tag strings. The constructor then formats the number through a fresh `std::stringstream`. All of this runs even for empty strings. With about 100 lines per item, one info build costs thousands of allocations. That cost is multiplied by the per-turn-cleared recipe description search and the 10 ms surroundings-menu redraw.

```cpp
sName = replace_colors( Name ); sFmt = replace_colors( Fmt );
std::stringstream convert; ... convert << std::fixed << Value;
```

**Fix:** Skip `replace_colors` when the string is empty or has no `'<'`. This helps less than it might, because many Fmt strings contain `<num>`. Make `replace_colors` use static tag strings and a single scan. Format numbers with `string_format("%.*f")` or `std::to_chars`.

### IINF-P04 · item::dps constructs 3 full monster objects per call; NPC AI calls it per weapon per combat turn
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/item_gun_tool_ammo.cpp:417`, `src/item_gun_tool_ammo.cpp:463`, `src/item_gun_tool_ammo.cpp:472`, `src/item_gun_tool_ammo.cpp:548`, `src/item_gun_tool_ammo.cpp:559`, `src/melee.cpp:2802`, `src/npcmove.cpp:5033`, `src/npcmove.cpp:2598`, `src/npc.cpp:2530`, `src/advanced_inv.cpp:608`, `src/item_info.cpp:3606`

`dps()` builds a `monster` for each of 3 comparison types. The constructor creates a dialogue and evaluates special-attack cooldowns. `effective_dps` then rolls damage 2-4 times, builds `resistances`, and calls the mutating `deal_damage_handle_type`. This runs from `npc::evaluate_best_weapon` for each unwielded melee or gun item every NPC combat turn, from `npc::value`, from item info, and twice per comparison in the AIM sort-by-damage comparator (6 monster constructions per comparison, O(n log n)).

```cpp
monster test_mon = monster( comp_mon.second.mon_id );
results[ comp_mon.first.translated() ] = effective_dps( guy, test_mon );
```

**Fix:** Precompute the per-mtype inputs once into a static struct: dodge, size penalty, armor, immunities and the PLASTIC flag. Compute post-armor damage numerically instead of mutating a temporary monster. Memoize `average_dps` per item and turn. In AIM, compute the sort key once per row.

### IINF-P05 · Character::mutation_armor(bp, dt) builds a full resistances map over all mutations to read one value
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character_armor.cpp:56`, `src/character_armor.cpp:66`, `src/character_armor.cpp:71`, `src/character_armor.cpp:92`, `src/character.cpp:5124`, `src/npcmove.cpp:839`, `src/npcmove.cpp:1505`

The typed overloads copy `get_functioning_mutations()` into a vector and merge every trait's resistances into an `unordered_map`, then read one entry. They run per damage unit per hit, and from `npc::estimate_armour` (24 calls per evaluated character per NPC move). Most traits return an empty static, so the main cost is the vector allocation plus lookups. The gain is small next to `worn.damage_resist`.

```cpp
for( const trait_id &iter : get_functioning_mutations() ) { res += iter->damage_resistance( bp ); }
return mutation_armor( bp ).type_resist( dt );
```

**Fix:** Sum `iter->damage_resistance( bp ).type_resist( dt )` directly, iterating `cached_mutations` without copying, then apply `res_pen`/`res_mult`. Optionally cache per bodypart.

### IINF-P06 · tname CONTENTS segment re-aggregates container contents on every name build
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_tname.cpp:259`, `src/item_tname.cpp:276`, `src/item_tname.cpp:283`, `src/item_container.cpp:1016`, `src/advanced_inv_listitem.cpp:13`, `src/inventory_ui.cpp:123`

The if-init runs `aggregated_contents()` before the `CONTENTS_FULL` test, and that includes nested calls. An AIM row aggregates 3 times: the default name, the sort key, and the explicit call. The impact is small: nested items are usually leaves, AIM rebuilds rows only on recalc, and `inventory_ui` caches names. Line 283 builds a `std::set` via `ammo_types()` just to test whether it is empty.

```cpp
if( item::aggregate_t aggi = it.aggregated_contents(); segments[tname::segments::CONTENTS_FULL] && aggi ) {
```

**Fix:** Test `CONTENTS_FULL` first. Optionally cache the aggregate per turn, as `cached_category` is. Replace `ammo_types().empty()` with a cheap pocket check.

### IINF-P07 · Sort-armor UI recomputes items_cover_bp about 6x per body part per redraw
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/armor_layers.cpp:845`, `src/armor_layers.cpp:858`, `src/armor_layers.cpp:859`, `src/armor_layers.cpp:878`, `src/armor_layers.cpp:882`, `src/armor_layers.cpp:540`, `src/armor_layers.cpp:431`

The `combine_bp` lambda (2 `items_cover_bp` calls) runs in both the sizing loop and the print loop, plus direct calls at 859 and 882. Each `items_cover_bp` call runs `get_item_penalties` (a scan over all worn items per sub-part), `get_encumber` and `tname()` for each covering item. This costs up to tens of milliseconds per keypress with a large outfit.

```cpp
const auto &combine_bp = [&guy]( const bodypart_id & cover ) -> bool { ...
    guy.worn.items_cover_bp( guy, cover ) == guy.worn.items_cover_bp( guy, opposite ); };
```

**Fix:** Build a `std::map<bodypart_id, std::vector<layering_item_info>>` once per redraw (or when the worn list changes), and derive everything from it.

### IINF-P08 · item::info() always formats a display string that most callers discard
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_info.cpp:4519`, `src/item_info.cpp:4509`, `src/surroundings_menu.cpp:939`, `src/surroundings_menu.cpp:1177`, `src/inventory_ui.cpp:4330`, `src/inventory_ui.cpp:5215`, `src/advanced_inv.cpp:1806`, `src/game.cpp:2209`, `src/game_inventory.cpp:2659`, `src/iexamine.cpp:1175`

The vector-out overload of `info()` always returns `format_item_info(...)`, and every listed caller ignores that string. The wasted pass is small next to `get_info` itself.

```cpp
std::string item::info( std::vector<iteminfo> &info, const iteminfo_query *parts, int batch ) const
{ info = get_info( parts, batch ); return format_item_info( info, {} ); }
```

**Fix:** Switch these callers to the existing `get_info( true )` (`item.h:477`). Optionally make the vector-out overloads return `void`.

### IINF-P09 · is_melee()/damage_melee() loop over all damage types and allocate per type for guns
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/item_gun_tool_ammo.cpp:1020`, `src/item_gun_tool_ammo.cpp:773`, `src/item_gun_tool_ammo.cpp:787`, `src/item_gun_tool_ammo.cpp:1155`, `src/npcmove.cpp:5025`, `src/npcmove.cpp:2598`

`is_melee()` calls `damage_melee()` for each damage type. For guns, each call allocates an `opts` vector and a `gunmods()` vector. NPC weapon evaluation calls it as a pre-filter on every inventory node. The per-call work is cheap, and guns with bash above 5 exit on the first type, so the real gain is unproven.

```cpp
for( const damage_type &dt : damage_type::get_all() ) { if( is_melee( dt.id ) ) {
std::vector<int> opts = { res }; for( const item *mod : gunmods() ) {
```

**Fix:** Precompute a per-itype `has_melee_damage` at finalize. Use a running max in `damage_melee` instead of a vector.

### IINF-P10 · LOCATION_PRECISE_CLOSEST_CITY tname segment collects and sorts all nearby cities per name build
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/item_tname.cpp:351`, `src/item_tname.cpp:360`, `src/overmapbuffer.cpp:1775`, `src/overmapbuffer.cpp:1793`, `src/overmapbuffer.cpp:1801`

For maps and ID cards with this flag, every `tname()` call runs `closest_city`. That collects all cities in up to 9 overmaps and fully sorts them just to read `front()`, even though `spawn_location` never changes. AIM calls `display_name` on each redraw. The cost is tens of microseconds on rare items.

```cpp
closest_city = overmap_buffer.closest_city( map_pos );
// overmapbuffer.cpp:1793
std::sort( result.begin(), result.end(), ... )
```

**Fix:** Resolve the city once and store it in an item var, or use `std::min_element` in `closest_city`. The latter also helps the stacking check at `item.cpp:705-709`.

### IINF-P11 · ammo_types() and pocket ammo accessors build std::set / flat_set copies on every call
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/item_gun_tool_ammo.cpp:2121`, `src/item_gun_tool_ammo.cpp:1525`, `src/item_gun_tool_ammo.cpp:1530`, `src/item_gun_tool_ammo.cpp:2161`, `src/item_gun_tool_ammo.cpp:2486`, `src/item_pocket.cpp:720`, `src/item_pocket.cpp:802`, `src/item_contents.cpp:1471`, `src/item.cpp:2696`, `src/item.cpp:1346`, `src/character_guns.cpp:318`

`item::ammo_types()` returns a `std::set` by value. For non-guns it rebuilds the set from per-pocket temporary sets. `item_type_restrictions()` copies a `flat_set`. `ammo_default()` calls `ammo_types()` twice, and `pocket_for_ammo` builds vectors plus per-pocket copies. Callers include `is_money()`, `color_in_inventory` and `display_name`, which run per UI row. Each copy is a 1-2 element set, so the total is microseconds per frame. This is a cleanup, not a hotspot.

```cpp
std::set<ammotype> item::ammo_types( bool conversion ) const ... return contents.ammo_types();
if( !ammo_types( conversion ).empty() ) { ... *ammo_types( conversion ).begin()
```

**Fix:** Store pocket ammo types in `pocket_data` at load time and return them by `const &`. Return `item_type_restrictions()` by `const &`. Add helpers `has_ammo_types()`, `accepts_ammotype()` and `first_ammo_type()`, and call `ammo_types()` once in `ammo_default()`.

### IINF-P12 · item::covers() and layer accessors do full scans, var lookups and allocations
**Severity:** Low · **Effort:** Medium · **Verified:** plausible
**Where:** `src/item_armor.cpp:55`, `src/item_armor.cpp:87`, `src/item_armor.cpp:285`, `src/item_armor.cpp:627`, `src/item_armor.cpp:655`, `src/item_armor.cpp:732`, `src/character_attire.cpp:1055`, `src/character_attire.cpp:2636`

`covers(bp)` has several inefficiencies:
- It calls `get_side()` even for non-sided items.
- It walks every portion through a type-erased `std::function` with no early exit.
- It allocates an ablative-pocket vector.

`covers(sbp)` re-derives `has_sub_data` on every call. `get_layer()` returns a copy, and `get_layer(bp)` and `has_layer` allocate. The reviewer found the "very hot" callers overstated: the per-turn warmth and wind paths avoid `covers()`, `calc_encumbrance` is event-driven, and the per-hit cost largely overlaps IINF-P01.

```cpp
iterate_covered_body_parts_internal( get_side(), [&]( const bodypart_str_id & covered ) {
    does_cover = does_cover || bp == covered; }, check_ablative_armor );
return armor->all_layers;  // by value
```

**Fix:** At finalize, precompute in `islot_armor` a `body_part_set` for all parts and for each side, `has_sub_data`, and per-bp layer lookups. Return `get_layer()` by `const &`, make `has_layer` allocation-free, and iterate ablative pockets without building a vector.

## Appendix: considered and rejected

- **Item name prefixes/suffixes hardcoded per flag even though json_flag item_prefix/item_suffix exist** – `prefix_tags_cache` already picks up own, type and inherited flags, so modders can already add prefixes through a json_flag with `inherit` and `item_prefix`. What remains is cosmetic.
- **item_reload_cost has swapped clamp() bounds for ammo containers** – The bounds are swapped, but the `is_ammo_container` branch is unreachable from every caller. It is dead legacy code to delete, not a live bug.
- **get_encumber redoes item-level work per body part; clothing-mod var lookups allocate** – `get_check_encumbrance()` is only set by the drop activity, and other paths use the cached relative encumbrance. The remaining costs are on cold or dirty-flag paths.
- **gun_current_mode()/gun_get_mode() build the full std::map of modes to fetch one** – Guns have 1-3 modes and the callers are cold, so the cost is about a microsecond per call.
- **process_tool calls energy_remaining twice per active tool per turn** – Only a handful of active tools exist, the extra call costs about 100-200 ns, and UPS lookups are flag-cached.
- **null_item_reference() default-constructs and move-assigns an item on every call** – Almost every call site is a cold or error path, and the reset is a deliberate guard against callers mutating the shared item.
