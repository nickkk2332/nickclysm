# 12 · Containers, pockets, inventory & item locations

> Engine audit – static code reading at commit `a1a646d6`; nothing was compiled or run. Each finding was checked against the code by a separate reviewer: **confirmed** = verified by reading the code; **plausible** = consistent with the code, but reachability or impact not proven. Line numbers refer to that commit.

**Scope:** `src/item_pocket.cpp`, `src/item_contents.cpp`, `src/item_container.cpp`, `src/item_location.cpp`, `src/visitable.cpp`, `src/inventory.cpp`, `src/character_inventory.cpp`, `src/active_item_cache.cpp`, `src/item_wakeup.cpp`, `src/contents_change_handler.cpp`, `src/map_selector.cpp`, `src/vehicle_selector.cpp`, `src/pickup.cpp`, `src/auto_pickup.cpp`, `src/item_stack.cpp`, `src/item_search.cpp`, `src/handle_liquid.cpp`, `src/map_entity_stack.cpp`

## Summary

The pocket system does its main job, but several of its edges are loose. The `pocket_data` JSON loader accepts typos and dropped keys without complaint, and shipped data is already broken because of it. Pockets also cannot be patched with `extend`/`delete`. EOCs see almost nothing of inventory or container state. There are several correctness bugs in less-travelled branches: ammo-pocket overflow, an infinite loop in magazine restacking, fuel-burning UPS efficiency, and parent-capacity checks. On the performance side, the main costs are structural. Every visited item heap-allocates an `item_location`, load deep-copies nested contents once per nesting level (which also regenerates item uids), and every character's whole inventory tree is walked 2-3 times per turn.

| Lens | Critical | High | Medium | Low | Total |
|---|---|---|---|---|---|
| Missing JSON / EOC | 0 | 3 | 6 | 5 | 14 |
| Bugs & quirks | 0 | 1 | 8 | 6 | 15 |
| Performance | 0 | 2 | 4 | 9 | 15 |
| **Total** | 0 | 6 | 18 | 20 | 44 |

**Fix first:**
1. **INV-B01** · Ammo-pocket overflow moves the wrong quantity. A one-word fix (`dropped_ammo` instead of `*iter`) stops ammo being duplicated or deleted.
2. **INV-J01** · `pocket_data` loader ignores unknown and dropped keys. It is a small change, and it surfaces 30+ already-broken pocket definitions in core and mods.
3. **INV-P01** · Item deserialization deep-copies contents once per nesting level. Load gets faster, and the fix also stops uids being regenerated, which currently breaks saved in-container `item_location`s.
4. **INV-B02** · MAGAZINE restack loops forever on a non count-by-charges item. It is a one-line hang fix, and shipped Aftershock content can reach the hang.
5. **INV-B03** · `fuel_efficiency` is ignored for magazine wells. The core `gas_charger_on` burns about 7.5x too much propane, and the integral-magazine branch has UB.

## 1. Missing JSON bindings & EOC features

### INV-J01 · `pocket_data` loader ignores unknown and dropped keys, so typos silently break real items
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/savegame_json.cpp:364`, `src/savegame_json.cpp:370`, `src/savegame_json.cpp:376`, `src/item_pocket.cpp:153`, `src/item_pocket.cpp:168`, `src/item_pocket.h:506`, `src/item_pocket.h:507`, `src/item_factory.cpp:4633`, `doc/JSON/ITEM.md:723`, `doc/JSON/ITEM.md:753`, `data/json/items/armor/bandolier.json:85`, `data/json/items/tool/deployable.json:58`, `data/mods/Xedra_Evolved/items/inventor/misc.json:184`

`pocket_data::deserialize` (and the `sealable_data` and `pocket_noise` versions) calls `allow_omitted_members()` before `load()`. That marks every member as visited, so a misspelled, misplaced or ignored pocket key never causes a load error. These objects are only read from item definitions and `pocket_mods`, never from saves, so the leniency buys nothing. When `ammo_restriction` is set, the loader skips `min/max_item_volume`, `max_contains_*`, `max/min_item_length`, `extra_encumbrance`, `volume_encumber_modifier`, `ripoff` and `activity_noise` with no warning. The empty-flag-list guard checks `"flag_restrictions"`, but the real key is `"flag_restriction"`. ITEM.md calls `max_contains_volume`/`max_contains_weight` mandatory, but the code defaults them to 200,000 L / 2,000,000 kg. A data scan shows the damage in shipped content:
- 12 clothing pockets (`armor/coats.json`, `suits_clothes.json`) use `"max_length"`, which is ignored.
- `hobo_stove` uses `"min_contains_volume"`, which does not exist.
- Xedra `inventor_portal_closer` has a `link_up` use_action pasted into `pocket_data`. It becomes an unrestricted 200,000 L CONTAINER pocket.
- 19 ammo-restricted pockets lose their keys. For example, `bandolier_rifle`'s `"volume_encumber_modifier": 0.3` never reaches `max_encumber` (`item_factory.cpp:1120`).

```cpp
// savegame_json.cpp:364
void pocket_data::deserialize( const JsonObject &data ) { data.allow_omitted_members(); load( data ); }
// item_pocket.cpp:153 – loaded key is "flag_restriction" (line 143)
if( jo.has_member( "flag_restrictions" ) && flag_restrictions.empty() )
// item_pocket.cpp:168
if( ammo_restriction.empty() ) { optional( ..."min_item_volume"... ) ... }
```

**Fix:** Remove `allow_omitted_members()` from the `pocket_data`, `sealable_data` and `pocket_noise` deserializers. Correct the key to `"flag_restriction"`. For ammo pockets, either throw on the excluded keys or, better, load `max_contains_weight`, `ripoff`, `activity_noise`, `extra_encumbrance` and `volume_encumber_modifier` for them too, so bandoliers can use these values. In `check_definition`, error when a CONTAINER pocket without `ammo_restriction` lacks `max_contains_volume`/`max_contains_weight`. Update ITEM.md: list the MAGAZINE_WELL, E_FILE_STORAGE and CABLE pocket types, document `ememory_max`, and stop showing `0 ml`/`0 mm` as defaults for `max_item_volume`/`max_item_length`. Fix the broken data the scan found.

### INV-J02 · `pocket_data` has no extend/delete/relative/proportional, so copy-from items repeat whole pocket arrays
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/item_factory.cpp:4633`, `src/generic_factory.h:923`, `src/item_pocket.cpp:140`, `src/item_pocket.h:597`

itype pockets are read with a plain `optional()` into `std::vector<pocket_data>`. Any `pocket_data` key therefore replaces the whole array, and `extend`/`delete`/`relative`/`proportional` only produce a "does not use reader" warning. To tweak one pocket of a copy-from item, a modder must copy every pocket of the parent, and those copies drift when the parent changes. Of 17,440 copy-from objects in core and mods, 789 redefine the full `pocket_data` array and none can patch it. The optional pocket `"id"` (`pocket_id`, used by `firing_requirements`) already gives each pocket a handle.

```cpp
// item_factory.cpp:4633
optional( jo, was_loaded, "pocket_data", pockets );
// generic_factory.h:923
warn_disabled_feature( jo, "extend", name, "does not use reader" );
```

**Fix:** Add a `pocket_data` reader keyed by pocket `id`, falling back to array index:
```json
"extend":       { "pocket_data": [ { "id": "hidden", "max_contains_volume": "250 ml" } ] },
"delete":       { "pocket_data": [ "side" ] },
"relative":     { "pocket_data": { "main": { "max_contains_volume": "1 L", "moves": -20 } } },
"proportional": { "pocket_data": { "*": { "max_contains_weight": 1.25 } } }
```
Use cases: a "bigger backpacks" mod in one block, adding a concealed pocket to an existing jacket, making every canteen insulated.

### INV-J03 · EOCs cannot read carried weight, volume or capacity, or any container state
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/character_inventory.cpp:1187`, `src/character_inventory.cpp:1815`, `src/character_inventory.cpp:1833`, `src/character_inventory.cpp:1862`, `src/character.cpp:2856`, `src/math_parser_diag.cpp:1744`, `src/math_parser_diag.cpp:1770`, `src/math_parser_diag.cpp:1876`, `src/condition.cpp:2366`, `src/condition.cpp:1962`, `src/condition.cpp:2221`

`weight_carried()`, `weight_capacity()`, `volume_carried()`, `volume_capacity()`, `free_space()` and `free_weight_capacity()` drive overload, pickup and encumbrance, but no math function or `val()` key exposes any of them. On a character, `weight()`/`volume()` return body mass (bodyweight + worn + wielded + bionics), not load or capacity. The legacy `val()` table is frozen. Item talkers cannot see contents count, remaining volume or weight, pocket capacity, sealed state or containment. The only item conditions, `has_ammo` and `is_rotten`, hardcode the beta talker.

```cpp
// math_parser_diag.cpp:1749 – Character::get_weight() = body + worn + weapon + bionics
return d.const_actor(...)->get_weight();
// condition.cpp:1965
item_location const *it = d.const_actor( true )->get_const_item(); ... debugmsg( "beta talker must be Item" )
```

**Fix:** Add math functions `u_/n_weight_carried()`, `weight_capacity()`, `volume_carried()`, `volume_capacity()` and `free_volume()`. For item talkers, add `n_contents_count()`, `n_remaining_volume()`, `n_remaining_weight()`, `n_volume_capacity()` and `n_contents_weight()`. Add conditions `u_/npc_is_container`, `is_sealed`, `is_container_empty`, `"contains_item": "id"` and `"is_contained_in": "id"`, and make `has_ammo`/`is_rotten` into `u_`/`npc_` pairs. Use cases: a "pack mule" mutation that triggers when carried weight exceeds 1.2x capacity, an NPC that checks you can carry a reward, refilling a container only when `n_remaining_volume() >= 250`.

### INV-J04 · `u_/npc_pickup_items` is half-built: `max_mass` misparsed, limits reset mid-pickup, wrong sign, NPC variant asserts
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctalk.cpp:8313`, `src/npctalk.cpp:8306`, `src/npctalk.cpp:8321`, `src/npctalk.cpp:8689`, `src/activity_actor.cpp:3100`, `src/pickup.cpp:212`, `src/pickup.cpp:268`, `src/pickup.cpp:383`, `src/pickup.cpp:438`, `src/pickup.cpp:440`, `src/game_inventory.cpp:2574`, `src/magic_spell_effect.cpp:2014`, `doc/JSON/EFFECT_ON_CONDITION.md:2646`

The EOC pickup feature and the pickup code beneath it have these defects:
1. `max_mass` is read with `has_float()`, a bool, so any value becomes 1 g.
2. Fields are only read as numeric literals, although the docs promise variable objects.
3. After 200 ml of bulk loading, `do_pickup` replaces the whole `pick_info` with a default. That drops `max_volume`, `max_mass`, `extra_moves_per_distance` and the running totals. This matters for the spell pickup path (`magic_spell_effect.cpp:2014`), which passes `info`.
4. In the bulk branch, `distance * extra_moves_per_distance` is added to moves instead of subtracted.
5. The limits are cumulative totals, but the docs describe them per item.
6. `npc_pickup_items` is registered, but `pickup_activity_actor::do_turn` asserts that the actor is the avatar. That assert is live, because NDEBUG is not set.
7. `do_pickup`/`pick_one_up` always act on `get_player_character()`.

The EOC itself calls `pick_up(what)` without `info` (8321), so its limits only cap the selection menu, and the selector's `overriden_volume/mass` (`game_inventory.cpp:2574`) are set but never read. No shipped data uses the effect yet.

```cpp
// npctalk.cpp:8313
max_mass = units::from_gram( jo.has_float( "max_mass" ) );
// pickup.cpp:438-440
if( info.total_bulk_volume > 200_ml ) { info = Pickup::pick_info(); }
// pickup.cpp:383
mod_moves( ( -std::max( item_handling_cost(...), 1 ) + ( distance * info.extra_moves_per_distance ) ) );
```

**Fix:** Read the fields with `get_dbl_or_var` (mass in grams, volume in ml). On bulk reset, clear only `total_bulk_volume`, `src_*` and `dst`. Negate the extra-moves term. Either make `do_pickup`/`pick_one_up` take a `Character &`, or drop the `npc_` registration. Pass `info` through from the EOC, or document the limits as menu-only. Add explicit per-item and total variants: `"max_item_mass"`, `"max_item_volume"`, `"max_total_mass"`, `"max_total_volume"`.

### INV-J05 · `u_consume_item_sum` never removes charges from partial stacks; docs wrong
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctalk.cpp:4283`, `src/npctalk.cpp:4284`, `src/npctalk.cpp:4266`, `src/npctalk.cpp:4271`, `doc/JSON/EFFECT_ON_CONDITION.md:4526`

When a count_by_charges stack is larger than the amount still wanted, the code zeroes `amount_desired` before using it. `mod_charges(-0)` then removes nothing, while the effect reports itself done. Taking 50 units from a 200-unit stack of currency, thread or ammo takes nothing. The search also covers `Access_Map_Around | Access_Vehicle`, limited to items owned by the player's faction, while the docs say "items in your inventory". `amount_desired` is reset for each pair (4271), so the weighted-sum behaviour the docs promise is not implemented. The docs table labels the effect `u_unset_flag`, and the spilling of consumed containers' contents is undocumented.

```cpp
// npctalk.cpp:4283-4284
amount_desired = 0;
it->mod_charges( -amount_desired );
```

**Fix:** Swap the two statements. Add an optional `"access": [ "inventory", "map", "vehicle" ]` that defaults to inventory only. Fix the docs table, describe the real per-pair semantics (or implement the weighted sum), and document the spill.

### INV-J06 · `OBTAIN_COST_MULTIPLIER` is bypassed for holsters; handling costs and `e_handcuffs` hardcoded
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_location.cpp:889`, `src/item_location.cpp:892`, `src/item_location.cpp:894`, `src/item_location.cpp:896`, `src/character_inventory.cpp:1440`, `src/character_inventory.cpp:1443`, `src/character_inventory.cpp:1488`, `src/character_inventory.cpp:3110`, `src/character_inventory.cpp:3111`, `src/character_inventory.cpp:3143`, `src/iuse_actor.cpp:2987`, `src/handle_liquid.cpp:488`, `src/handle_liquid.cpp:537`, `src/pickup.cpp:438`

`item_in_container::obtain_cost` applies `OBTAIN_COST_MULTIPLIER`, then the holster branch overwrites `primary_cost` with `item_retrieve_cost`. The holster use path (`wield_contents`) adds `item_retrieve_cost` with no enchantment either. Shipped content that is meant to speed up draws, such as BombasticPerks `perk_quickdraw` and the PACKMULE/DISORGANIZED mutations, therefore does nothing for holsters and sheaths. `item_store_cost` has no enchantment hook at all. Tuning is hardcoded: 50 moves per litre capped at 200, a `(lvl+10)/10` skill divisor, a 4x penalty tied to the `e_handcuffs` id, 100/1000 moves for pouring liquid, and a 200 ml bulk-pickup window.

```cpp
// item_location.cpp:889-896
primary_cost = ch.enchantment_cache->modify_value( enchant_vals::mod::OBTAIN_COST_MULTIPLIER, primary_cost );
if( container->get_use( "holster" ) ) { ... primary_cost = ch.item_retrieve_cost( *target(), *container ... ); }
// character_inventory.cpp:1443
if( weapon.typeId() == itype_e_handcuffs ) { mv *= 4; }
```

**Fix:** Apply `OBTAIN_COST_MULTIPLIER` after the holster branch and in `wield_contents`. Add enchantment values `STORE_COST_MULTIPLIER` and `HOLSTER_DRAW_COST`. Replace the `e_handcuffs` check with a flag such as `"RESTRAINS_HANDS"` plus a balance constant, or an item field `"wield_handling_multiplier": 4`. Allow a pocket `"insert_moves"` that is separate from `"moves"`.

### INV-J07 · Item, monster and vehicle talkers silently no-op every inventory query and effect
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/talker.h:383`, `src/talker.h:396`, `src/talker.h:579`, `src/talker.h:865`, `src/talker.h:938`, `src/talker_character.h:143`, `src/condition.cpp:700`, `src/math_parser_diag.cpp:722`, `src/npctalk.cpp:7068`, `src/npctalk.cpp:3998`

Only `talker_character` overrides `charges_of`, `get_amount`, `has_item_with_flag`, `items_with`, `i_add`, `i_add_or_drop` and `remove_items_with`. On an item talker (a bag), a monster (saddlebags, tack, armor) or a vehicle (cargo), `npc_has_item`, `n_item_count()`, `n_charge_count()`, `npc_has_item_with_flag`, `npc_remove_item_with` and `u_spawn_item` return 0/false or do nothing, with no error. `run_inv_eocs` bails unless the actor is a Character. No effect inserts into a specific container, spills, unloads, seals or unseals. The one container path, `receive_item` with `"container"`, sets `.charges` unconditionally and discards the `put_in` result, so an item that does not fit is lost.

```cpp
// talker.h:396, 865
virtual int get_amount( const itype_id & ) const { return 0; }
virtual void i_add( const item & ) {}
// npctalk.cpp:3998
new_item.charges = count; container.put_in( new_item, pocket_type::CONTAINER );  // ret_val discarded
```

**Fix:** Have `talker_item` answer amount, charge and flag queries and `items_with`/`remove_items_with` by walking its contents, with `i_add` as `put_in` plus overflow. Do the same for `talker_monster` (storage, armor and tack items, inventory) and `talker_vehicle` (cargo). Allow `run_inv_eocs` on these talkers. New effects:
```json
{ "u_put_in_container": { "item": "water_clean", "count": 250, "pocket_type": "CONTAINER", "on_fail": "drop" } },
{ "u_spill_contents": {} }, { "u_seal_container": {} }, { "u_unseal_container": {} }
```
In `receive_item`, check the `put_in` result and drop on failure.

### INV-J08 · No events or JSON hooks for pickup, drop, stash, take-out, spill or unseal
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/event.h:68`, `src/pickup.cpp:333`, `src/character_inventory.cpp:684`, `src/character_inventory.cpp:3137`, `src/character_inventory.cpp:3156`, `src/contents_change_handler.cpp:17`, `src/item_pocket.cpp:850`, `src/item_factory.cpp:4537`

The event bus covers wield, wear and take-off, but picking up, dropping, storing into a container, unloading, spilling and breaking a seal emit nothing. itype hooks are limited to `use_action`, `tick_action`, `countdown_action` and `drop_action`. The coarse hooks that do exist (`character_starts_activity`/`character_finished_activity` and activity `completion_EOC` for ACT_PICKUP/ACT_DROP/insert/unload) do not carry the item.

```cpp
// character_inventory.cpp:3137 – no event
void Character::store(...) { mod_moves(...); container.put_in(...); craft_relocated(...); calc_encumbrance(); }
// contents_change_handler.cpp:26 – no event or hook
pocket->unseal();
```

**Fix:** Add event types sent with the item as beta talker: `character_picks_up_item {character, itype, count}`, `character_drops_item`, `character_stores_item {itype, container_itype}`, `character_takes_item_from_container`, `container_unsealed {container_itype}`, `item_spilled {itype, pos}`. Add itype fields `"on_pickup_eocs"`, `"on_insert_eocs"` and `"on_remove_eocs"`, and a `pocket_data`/`sealed_data` field `"on_unseal_eocs"`.

### INV-J09 · `sealed_data` supports only `spoil_multiplier` although docs promise a full sealed override
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/item_pocket.cpp:211`, `src/item_pocket.h:464`, `src/item_pocket.h:470`, `src/item_pocket.cpp:675`, `src/item_pocket.cpp:684`, `src/item_pocket.cpp:2131`, `src/item_pocket.cpp:2158`, `doc/JSON/ITEM.md:759`

ITEM.md says the sealed version of a pocket overrides the unsealed version of the same datatype. The header comment at `item_pocket.h:464-465` says a sealed pocket counts as watertight. In code, `sealable_data` loads only `spoil_multiplier`. `watertight()`, `airtight()` and `insulation()` ignore the seal; only `spoil_multiplier()`, `transparent()` and `will_spill()` look at it. All 48 `sealed_data` uses in data set only `spoil_multiplier`.

```cpp
// item_pocket.cpp:211
void sealable_data::load( const JsonObject &jo ) { optional( jo, was_loaded, "spoil_multiplier", spoil_multiplier, 1.0f ); }
// item_pocket.cpp:2131
bool item_pocket::watertight() const { return data->watertight; }
```

**Fix:** Let `sealed_data` optionally override `watertight`, `airtight`, `insulation`, `open_container`, `transparent` and `moves`, and apply the overrides in the accessors when `sealed()`. Add unseal costs: `"unseal_moves": 200`, `"unseal_quality": { "CAN_OPEN": 1 }`, `"unseal_message"`, `"resealable": true`. Combine this with `on_unseal_eocs` (INV-J08).

### INV-J10 · PALS/MOLLE attachment size hardcoded as three flags worth 1/2/3 slots
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_container.cpp:912`, `src/item_container.cpp:924`, `src/item_contents.cpp:2422`, `src/item_contents.cpp:2458`, `src/game_inventory.cpp:2233`, `src/item_group.cpp:740`

`get_pocket_size()` returns 1, 2 or 3 from the `PALS_SMALL`/`MEDIUM`/`LARGE` flags, with 3 as the default, and `can_attach_as_pocket()` checks only those flags. The vest's capacity comes from the attach actor's JSON `"size"`, but a pouch cannot declare its own cost, so 4+ slot carriers are impossible.

```cpp
// item_container.cpp:915-920
if( has_flag( flag_PALS_SMALL ) ) { return 1; } else if( has_flag( flag_PALS_MEDIUM ) ) { return 2; } else { return 3; }
```

**Fix:** Add an optional itype field `"pals_size": <int>` that falls back to the flags, and let `can_attach_as_pocket()` accept `pals_size > 0`. Half-slot pouches would also need the counter type changed.

### INV-J11 · Other hardcoded ids and tuning: INATTENTIVE, ground sonar, UPS battery recharge, rain water
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/map_entity_stack.cpp:256`, `src/cata_tiles.cpp:4458`, `src/monster.cpp:891`, `src/character_inventory.cpp:2921`, `src/character_inventory.cpp:2927`, `src/character_inventory.cpp:2882`, `src/character_inventory.cpp:2897`, `src/character_inventory.cpp:2900`, `src/item_pocket.cpp:477`, `src/weather.cpp:260`, `src/auto_pickup.cpp:215`

1. `trait_INATTENTIVE` is checked by id in `map_entity_stack`, `cata_tiles`, `monster` and the surroundings menu.
2. `search_surroundings` checks `bio_ground_sonar` by id, with a fixed 5-tile radius and fixed practice times.
3. USE_UPS recharge is hardcoded to `itype_battery` at 1 kJ per turn, so tools that use another ammotype are never recharged.
4. Funnels probe only with `itype_water`, and rain is created as water.
5. Auto-pickup treats only the `battery` ammotype as a powered tool.

```cpp
// character_inventory.cpp:2899-2900
ups_used += 1_kJ; it->ammo_set( itype_battery, it->ammo_remaining_linked( *here, nullptr ) + 1 );
// item_pocket.cpp:477
const std::vector<item> allowed_liquids{ item( itype_water, calendar::turn_zero, 1 ) };
```

**Fix:** Replace the ids with json_character_flags `"UNKNOWN_ATTITUDES"` and `"GROUND_SONAR"`. Recharge with `ammo_default()` and add a `"ups_recharge_rate"` field. Move the search radius and practice times to game_balance. Read the rain liquid from the weather type (e.g. `"precipitation_item"`).

### INV-J12 · Pocket favorite settings cannot be preset in JSON; auto-whitelist priority hardcoded
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/item_pocket.h:66`, `src/item_pocket.cpp:140`, `src/item_container.cpp:91`, `src/item_container.cpp:95`, `src/item_pocket.cpp:378`

Priority, item and category white/blacklists, collapsed, disabled and unload come only from the UI or the user's preset file, so modders cannot ship defaults such as a low-priority dump pouch or a medkit pocket that prefers drugs. `add_automatic_whitelist` hardcodes priority 100 plus collapse, and the `better_pocket` tie-break order is fixed in C++.

```cpp
// item_container.cpp:95-97
pkts.front()->settings.whitelist_item( ... ); set_priority( 100 ); set_collapse( true );
```

**Fix:** Add a `pocket_data` field that is applied when a pocket is created with null settings:
```json
"default_settings": { "priority": -5, "category_whitelist": [ "drugs" ], "item_whitelist": [], "collapsed": true, "disabled": false, "unload": false }
```
Make the auto-whitelist priority a game_balance constant.

### INV-J13 · Delayed EOCs can't target items; persistent `item_wakeup` scheduler closed to JSON
**Severity:** Low · **Effort:** Medium · **Verified:** plausible
**Where:** `src/npctalk.cpp:6470`, `src/npctalk.cpp:6481`, `src/item_wakeup.cpp:208`, `src/item_wakeup.cpp:223`, `src/item_location.cpp:1533`, `src/math_parser_diag_value.h:37`, `src/item.cpp:4811`, `src/npctalk.cpp:6357`

`run_eocs` with `time_in_future` queues only when the alpha is a Character. This is documented at EFFECT_ON_CONDITION.md:2219, so the drop is not truly silent. `diag_value` has no item-reference type. `item_wakeup_manager` is save-persistent and uid-addressed, but it only dispatches to crafts and test registries. A JSON workaround exists: `transform_item` re-arms `countdown_interval`, and `countdown_action` can run EOCs with the item as beta. That costs an active item. Storing EOC context in the wakeup queue would also go against its "item is authoritative" design.

```cpp
// item_wakeup.cpp:226
if( it.is_craft() ) { craft_actualize_scheduled(...); return; }  // else test_handler_registry only
```

**Fix:** Optionally, add an `"eoc"` wakeup kind that keeps `{eoc_id, context}` on the item (in item vars) and only the uid in the queue. Expose it as `"u_queue_item_eoc": "EOC_FERMENT_DONE", "time_in_future": "3 days"`, or route `run_eocs` + `time_in_future` through it when the alpha is an item. Consider an item-reference variable type (uid plus locator hint).

### INV-J14 · Magic pseudo-item ids (`apparatus`, `fire`) and `bio_ups` hardcoded in inventory queries
**Severity:** Low · **Effort:** Medium · **Verified:** plausible
**Where:** `src/visitable.cpp:1487`, `src/character_inventory.cpp:2308`, `src/visitable.cpp:1194`, `src/visitable.cpp:1230`, `src/visitable.cpp:1357`, `src/character.cpp:5737`, `src/visitable.cpp:686`, `src/bionics.cpp:3145`, `src/iuse_actor.cpp:868`

`amount_of` maps `apparatus` to "any SMOKE_PIPE-quality item", and `has_charges` maps `apparatus`/`fire` to `has_fire()`. These stand in for a quality-based requirement that `consume_drug` does not support, because its `tools_needed` takes only itype ids. The `bio_ups` id is checked directly in `visitable.cpp` and `character.cpp`. The reviewer notes that bionic and mutation pseudo/integrated items with `IS_UPS` + `USES_BIONIC_POWER` can already power UPS tools from JSON, and the apparatus/fire route is documented. The real gap is that `consume_drug` has no `qualities_needed`. The player-power leak into NPC queries is covered by INV-B12.

```cpp
// character_inventory.cpp:2308
if( it == itype_fire || it == itype_apparatus ) { return has_fire( quantity ); }
```

**Fix:** Add `"qualities_needed": { "SMOKE_PIPE": 1 }` and `"requires_fire": true` to `consume_drug`, then retire the `apparatus` pseudo-item. Optionally replace `bio_ups` id checks with a character flag such as `"UPS_FROM_BIONIC_POWER"`.

## 2. Bugs, oversights & quirks

### INV-B01 · Ammo-pocket overflow moves the wrong quantity (duplicates or deletes ammo) and ignores `capacity_mods`
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_pocket.cpp:1902`, `src/item_pocket.cpp:1906`, `src/item_pocket.cpp:1908`, `src/item_pocket.cpp:1910`, `src/item_pocket.cpp:1549`, `src/item_pocket.cpp:1555`, `src/item_pocket.cpp:2585`, `src/item_pocket.cpp:778`, `src/item_pocket.cpp:600`, `src/item_container.cpp:183`, `src/character_inventory.cpp:771`

In the ammo-restricted branch of `item_pocket::overflow`, the code removes `overflow_count` from the stack and builds `dropped_ammo`, but then passes `*iter`, the already-reduced stack, to `move_to_parent_pocket_recursive`. The pocket and the floor end up with the same reduced count, so ammo is duplicated or destroyed. For example, overfilling with a second stack can leave a 0-charge ghost on the ground. The limit at 1906 and `pocket_data::volume_capacity()` use the raw `ammo_restriction` count, while `ammo_capacity()` applies the fork's per-pocket `capacity_mult`. Magazines boosted by `capacity_mods` can therefore be reloaded past a size that overflow then rejects. Usually a single boosted stack fails the volume check at 1555 and the whole stack falls out. `overflow()` runs every turn on the wielded item via `drop_invalid_inventory`, and also after crafting and on map and vehicle items.

```cpp
// item_pocket.cpp:1905-1911
const int overflow_count = total_qty - ammo_iter->second;
if( overflow_count > 0 ) {
    ammo.charges -= overflow_count;
    item dropped_ammo( ammo.typeId(), ammo.birthday(), overflow_count );
    move_to_parent_pocket_recursive( here, pos, *iter, loc, carrier );   // dropped_ammo unused
// item_pocket.cpp:778
return std::lround( found_ammo->second * capacity_mult );
```

**Fix:** Pass `dropped_ammo` instead of `*iter`. Compare against `ammo_capacity( ammotype )` instead of the raw map value. Derive ammo-pocket `volume_capacity()` in `item_pocket` so that it scales by `capacity_mult`.

### INV-B02 · Order-preserving MAGAZINE restack loops forever on a non count-by-charges item
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_pocket.cpp:277`, `src/item_pocket.cpp:278`, `src/item_pocket.cpp:322`, `src/item_pocket.cpp:323`, `src/item_gun_tool_ammo.cpp:3535`, `src/item_pocket.cpp:1718`

Both `restack()` overloads have a MAGAZINE branch that executes `continue` without advancing the iterator when `!iter->count_by_charges()`. A MAGAZINE pocket holding 2+ items, one of them non-stackable, hangs the game in `restack()`, which runs on every add, insert, `use_amount` and `on_contents_changed`. Nothing requires MAGAZINE pockets to declare `ammo_restriction`. Shipped content reaches this: the Aftershock `tac_fullhelmet_uicas` has a MAGAZINE pocket with only `flag_restriction` GASFILTER_MED, and reloading a second filter puts a second non-stackable item in it.

```cpp
// item_pocket.cpp:277-280
for( auto iter = contents.begin(); iter != contents.end(); ) {
    if( !iter->count_by_charges() ) {
        continue;     // iter never incremented
    }
```

**Fix:** Change both branches to `{ ++iter; continue; }`. Optionally, have `check_definition` require `ammo_restriction` on MAGAZINE pockets.

### INV-B03 · `fuel_efficiency` for fuel-burning UPS is half-baked: ignored for mag wells, mis-credited, UB on empty pocket
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_contents.cpp:1328`, `src/item_contents.cpp:1336`, `src/item_contents.cpp:1348`, `src/item_contents.cpp:1351`, `src/item_contents.cpp:1355`, `src/item_contents.cpp:1357`, `src/item_gun_tool_ammo.cpp:1938`, `src/character.cpp:5775`

1. The MAGAZINE_WELL branch of `item_contents::ammo_consume` calls `mag->ammo_consume( qty, ... )`, which drops the efficiency. The core `gas_charger_on` (`"fuel_efficiency": 0.3`, propane at 25,000 kJ/L) therefore burns 1 ml per kJ instead of 1 ml per 7.5 kJ, about 7.5x too much.
2. The integral-MAGAZINE branch burns `energy/(fuel_energy*eff)` charges but credits `res * to_kilojoule(fuel_energy)` without the efficiency, and truncates sub-kJ fuels to 0.
3. It reads `pocket.front()` after `ammo_consume` may have erased the last stack, which is UB.

Items 2 and 3 are only reachable by modded fuel tools with integral magazines.

```cpp
// item_contents.cpp:1336
const int res = mag->ammo_consume( qty, *here, pos, nullptr );   // no efficiency
// item_contents.cpp:1355-1357
const int res = pocket.ammo_consume( charges_used );
int energy_generated = res * units::to_kilojoule( pocket.front().fuel_energy() );
```

**Fix:** Cache `fuel_energy()` before consuming. Compute `res * fuel_energy * efficiency` in `units::energy` and convert once. Recurse into magazine-well magazines through `contents.ammo_consume( qty, here, pos, fuel_efficiency )`. Add a load check that `fuel_efficiency` tools actually hold fuel.

### INV-B04 · `max_charges_by_parent_recursive` only ever checks the innermost parent pocket
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_location.cpp:1290`, `src/item_location.cpp:1302`, `src/item_location.cpp:1309`, `src/item_location.cpp:907`, `src/item_location.cpp:911`, `src/character.cpp:3869`, `src/activity_actor.cpp:8347`, `src/character_inventory.cpp:391`

The loop walks `current_location` up the container chain but calls `weight_capacity()`/`volume_capacity()` on `this`, the starting location, so grandparent and higher pockets never limit the result. `insert_item_activity_actor::try_insert`, `Character::try_add` and `pour_into` (for non-rigid chains) rely on this guard. Inserting into a soft container nested in a nearly full backpack pushes the backpack over capacity, and the next `overflow()` drops items.

```cpp
// item_location.cpp:1300-1312
if( weight_multiplier > 0 ) {
    units::mass temp_weight = weight_capacity() / weight_multiplier;   // this->, not current_location
...
    units::volume temp_volume = volume_capacity() / volume_multiplier;
...
current_location = current_location.parent_item();
```

**Fix:** Use `current_location.weight_capacity()`/`volume_capacity()` inside the loop. Guard `get_standard_pockets().front()` (lines 1233/1286) against items with no standard pockets.

### INV-B05 · Active-item `pocket_chain` leaks sibling pockets, corrupting spoilage/insulation of items on map & vehicles
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/active_item_cache.cpp:47`, `src/active_item_cache.cpp:50`, `src/active_item_cache.cpp:12`, `src/active_item_cache.cpp:30`, `src/item_pocket.cpp:2029`, `src/map.cpp:6660`, `src/map.cpp:6768`

`active_item_cache::add` copies `pocket_chain` once and appends every sibling pocket without popping it. Items in pocket N therefore carry pockets 1..N-1 in their chain. `item_reference::spoil_multiplier()`/`insulation()` take a product over the chain, and `has_watertight_container()` is true if any pocket in it is watertight. The carried path in `item_pocket::process`, by contrast, takes the minimum. The map and vehicle processing paths use these values, so food rots at different rates on the floor and in your pack. In core this affects `robofac_armor_rig`/`robofac_armor_anchor`, where food in the pouches rots at 0.64x instead of 1x. The chain holds raw pocket pointers, so if pockets are erased on a map item, a dangling pointer could be dereferenced. That part is only plausible.

```cpp
// active_item_cache.cpp:47-55
std::vector<item_pocket const *> pockets = pocket_chain;
for( item_pocket *pk : it.get_standard_pockets() ) {
    pockets.emplace_back( pk );          // never popped
    for( item *pkit : pk->all_items_top() ) {
        ret |= add( *pkit, location, &it, pockets );
```

**Fix:** Build a fresh `chain = pocket_chain; chain.push_back( pk );` per pocket, or push before recursing and pop after. Make `item_reference::spoil_multiplier()` use `std::min` to match `item_pocket::process`, and settle on one insulation rule.

### INV-B06 · Auto-pickup selection empties whitelisted containers: spills liquids, opens sealed packs, even if pickup fails
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/auto_pickup.cpp:96`, `src/auto_pickup.cpp:103`, `src/auto_pickup.cpp:107`, `src/auto_pickup.cpp:178`, `src/auto_pickup.cpp:284`, `src/auto_pickup.cpp:286`, `src/auto_pickup.cpp:289`, `src/pickup.cpp:510`

`select_items` calls `empty_autopickup_target` on every whitelisted container before deciding to pick it up. For non-rigid containers, it removes everything that is not whitelisted, liquids included, and adds it to the map tile. There is no seal check, even though `get_autopickup_items` deliberately forces whole-container pickup for sealed containers and liquids. The emptying also happens before `within_autopickup_limits` and before the weight/stash filter in `Pickup::autopickup`, so the container can be emptied and then left behind. The nested path at 178 has the same problem. Example: whitelisting "small waterskin" (watertight, not rigid) pours the water out as a puddle.

```cpp
// auto_pickup.cpp:284-291
if( pickup_state == rule_state::WHITELISTED ) {
    if( item_entry->is_container() ) {
        empty_autopickup_target( item_location( map_location, item_entry ), location );
    }
    if( !within_autopickup_limits( item_entry ) ) { continue; }
```

**Fix:** Skip containers with `any_pockets_sealed()`, and never eject liquids or gases. Treat liquid contents as "pick whole or skip". Defer the emptying to the pickup activity, after the weight and volume checks pass.

### INV-B07 · Smoking `apparatus` check never looks inside containers (SKIP), so pipes must be wielded
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/visitable.cpp:1481`, `src/visitable.cpp:1487`, `src/visitable.cpp:1493`, `src/visitable.cpp:466`, `src/iuse_actor.cpp:915`, `src/consumption.cpp:940`, `src/game_inventory.cpp:1158`

The apparatus branch of `Character::amount_of` returns `VisitResponse::SKIP` for every node, and SKIP does not descend into contents. Only wielded and top-level worn items are checked, so a pipe in a backpack is never found. The drugs in `med.json` that need `"apparatus"` fail with "You need a smoking device..." unless the pipe is wielded or worn. All three gates (iuse_actor, consumption, game_inventory) are affected. The pseudo-item shortcut also returns 1 regardless of `filter`/`limit`.

```cpp
// visitable.cpp:1489-1494
visit_items( [&qty, &limit, &filter]( const item_location & e ) {
    if( e->get_quality( qual_SMOKE_PIPE ) >= 1 && filter( *e ) ) { qty = sum_no_wrap( qty, 1 ); }
    return qty < limit ? VisitResponse::SKIP : VisitResponse::ABORT;
```

**Fix:** Return `VisitResponse::NEXT` when below the limit. Count pseudo items that pass the filter into `qty` instead of returning 1.

### INV-B08 · `Character::has_quality` ignores the level of mutation-provided qualities
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/visitable.cpp:240`, `src/visitable.cpp:243`, `src/visitable.cpp:279`, `src/visitable.cpp:282`, `src/character_inventory.cpp:1116`, `src/visitable.cpp:380`, `src/iuse_actor.cpp:4308`, `doc/JSON/MUTATIONS.md:123`

`has_quality` and `has_unreserved_quality` return true for any functioning mutation that has the quality key, whatever the requested level and qty. `intrinsic_quality_sources` and `max_quality` do compare levels, so the systems disagree. A mutation with `"provided_qualities": { "DIG": 1 }` passes `has_quality( qual_DIG, 3 )`.

```cpp
// visitable.cpp:240-245
for( const trait_id &mut : get_functioning_mutations() ) {
    const auto &q = mut->provided_qualities.find( qual );
    if( q != mut->provided_qualities.end() ) {
        return true;
```

**Fix:** Require `q->second >= level` in both functions, and count matching mutations toward `qty` as the bionic loop does.

### INV-B09 · `better_pocket` returns early for perishable food, skipping all later tie-breakers
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_pocket.cpp:415`, `src/item_pocket.cpp:417`, `src/item_pocket.cpp:420`, `src/item_pocket.cpp:437`, `src/item_contents.cpp:983`

For any spoiling comestible, `better_pocket` returns the spoil-multiplier comparison even when the multipliers are equal, which is the usual 1.0 vs 1.0 case. The later checks never run: avoiding watertight pockets for solids, `extra_encumbrance`, ripoff, rigid, remaining volume and obtain cost. Perishables end up in whichever eligible pocket iteration reaches first.

```cpp
// item_pocket.cpp:415-418
if( it.is_comestible() && it.get_comestible()->spoils != 0_seconds ) {
    // a lower spoil multiplier is better
    return rhs.spoil_multiplier() < spoil_multiplier();
}
```

**Fix:** Add `&& rhs.spoil_multiplier() != spoil_multiplier()` to the condition so that equal multipliers fall through.

### INV-B10 · Holster/ablative pockets accept oversized stackable items once one is inside (skips volume, weight, ammo cap)
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_pocket.cpp:1597`, `src/item_pocket.cpp:1598`, `src/item_pocket.cpp:1573`, `src/item_pocket.cpp:1575`, `src/item_pocket.cpp:400`, `src/item_contents.cpp:979`, `src/character_inventory.cpp:339`, `src/item_pocket.cpp:1862`, `src/character.cpp:2282`

In `_can_contain`, a non-empty holster or ablative pocket returns success as soon as `contents.front().can_combine(it)`, before the ammo, weight and volume checks. `better_pocket` prefers pockets that already hold a matching stack, and `try_add` inserts the whole stack with no further check. For example, nails tied to a rope holster accept any later stack of nails. It is not unlimited storage: the next turn's `drop_invalid_inventory`/`overflow` drops the whole oversized stack, original contents included, to the ground.

```cpp
// item_pocket.cpp:1597-1601
if( data->holster && !contents.empty() ) {
    if( contents.front().can_combine( it ) ) {
        // Only items with charges can succeed here.
        return ret_val<item_pocket::contain_code>::make_success();
```

**Fix:** Don't return early. Fall through to the ammo and weight/volume checks (`charges_per_remaining_volume/weight`). Alternatively, clamp in `try_add` to `remaining_capacity_for_item`.

### INV-B11 · `pocket_data` JSON: explicit `default_magazine` is overwritten; empty `flag_restriction` check never fires
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_pocket.cpp:148`, `src/item_pocket.cpp:161`, `src/item_pocket.cpp:164`, `src/item_pocket.cpp:144`, `src/item_pocket.cpp:155`, `src/item_pocket.cpp:131`, `src/item_factory.cpp:213`, `src/item_group.cpp:574`, `src/item_group.cpp:700`, `src/item_gun_tool_ammo.cpp:2320`, `data/json/items/tool/cooking.json:1027`

This merges two duplicate reports, one from the JSON lens and one from the bug lens.
1. After reading `"default_magazine"`, the loader unconditionally sets it to `item_restriction.front()`. `acetylene_cooker` declares `smallweldtank` but spawns with `tinyweldtank`, and it is the only mismatch among the 197 uses in data. Nothing checks that the default is compatible.
2. The empty-array guard checks `"flag_restrictions"` instead of `"flag_restriction"`, so `"flag_restriction": []` silently produces an unrestricted pocket (see also INV-J01).
3. The `check_definition` message at line 131 states the opposite of its condition.

```cpp
// item_pocket.cpp:148, 161-164
optional( jo, was_loaded, "default_magazine", default_magazine );
if( !item_id_restriction.empty() ) { ... default_magazine = item_restriction.front(); }
```

**Fix:** Only set `default_magazine` from `item_restriction.front()` when it is null, and error in `check_definition` if it is not in the restriction list or is not a magazine. Fix the key and reword the message.

### INV-B12 · `charges_of` external pools use the avatar's bionic power for any inventory (NPCs) and can double-count it
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/visitable.cpp:1194`, `src/visitable.cpp:1196`, `src/visitable.cpp:1200`, `src/visitable.cpp:1227`, `src/visitable.cpp:1230`, `src/visitable.cpp:1271`

`apply_external_pools_legacy`/`multimag` add `get_player_character().get_power_level()` whatever the owner of the inventory being queried. An NPC with an empty USE_UPS tool passes `has_charges` checks when the avatar has an active `bio_ups`, and the later consumption, which uses the NPC's own power, fails. If one query sees both a USE_UPS entry and a USES_BIONIC_POWER entry, the legacy path adds the power twice.

```cpp
// visitable.cpp:1194-1201
if( any_ups && qty < limit && get_player_character().has_active_bionic( bio_ups ) ) {
    qty = sum_no_wrap( qty, ... get_player_character().get_power_level() ... );
}
if( any_bionic ) {
    qty = sum_no_wrap( qty, ... get_player_character().get_power_level() ... );
```

**Fix:** Pass the owning `Character *` (or `nullptr` for map and vehicle selectors) into `apply_external_pools`, and add the bionic pool at most once.

### INV-B13 · `handle_liquid(item_location)` leaves 0-charge liquid in its container after pouring to ground/vehicle
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/handle_liquid.cpp:693`, `src/handle_liquid.cpp:699`, `src/handle_liquid.cpp:485`, `src/handle_liquid.cpp:486`, `src/handle_liquid.cpp:530`, `src/handle_liquid.cpp:251`, `src/iexamine.cpp:4649`

Cleanup (removing the zero-charge source and notifying the parent) only runs for watertight `LD_ITEM` and `LD_KEG` targets. When the destination is `LD_GROUND` (which sets `charges = 0`) or `LD_VEH`, a 0-charge ghost stays in the container. The only container-source caller is the tree tap's "harvest sap" option. The effect is mostly cosmetic.

```cpp
// handle_liquid.cpp:693-701
if( success && ( ( liquid_target.dest_opt == LD_ITEM && liquid_target.item_loc->is_watertight_container() ) || liquid_target.dest_opt == LD_KEG ) ) {
    ...
    if( liquid->charges == 0 ) { liquid.remove_item(); }
```

**Fix:** Run the `charges == 0 → remove_item()` step and the parent `on_contents_changed()` for every successful transfer. Keep only the FROZEN flag reset conditional.

### INV-B14 · NPC liquid handling fills containers via raw `item::fill_with`: no `make_active`, no parent capacity
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/handle_liquid.cpp:168`, `src/handle_liquid.cpp:172`, `src/handle_liquid.cpp:225`, `src/handle_liquid.cpp:502`, `src/character.cpp:3870`

`handle_npc_liquid` is reached from NPC crafting and butchery. It sizes containers with the bool `get_remaining_capacity_for_liquid` overload and calls bare `item::fill_with`. Unlike the avatar path (`pour_into` + `make_active`), liquid on the ground or in vehicles misses active-item processing, and soft containers nested in full bags can be overfilled.

```cpp
// handle_liquid.cpp:224-226
const int amount = std::min( liquid.charges, capacities[best_idx] );
liquid.charges -= container_locs[best_idx]->fill_with( liquid, amount );
```

**Fix:** Use `who.pour_into( container_loc, liquid, ... )`, then call `container_loc.make_active()`.

### INV-B15 · `has_fire` HACK: NPCs always count as having fire/`apparatus` charges
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character_inventory.cpp:2349`, `src/character_inventory.cpp:2308`, `src/iuse.cpp:478`, `src/iuse_actor.cpp:361`, `src/character.cpp:5900`

`has_fire` returns true for every NPC so that NPCs can throw Molotovs. Because `has_charges(fire|apparatus)` routes to it, NPCs pass smoking, drug-apparatus and transform `need_fire` checks without a lighter, and `use_fire` then consumes nothing.

```cpp
// character_inventory.cpp:2349-2352
if( is_npc() ) {
    // HACK: A hack to make NPCs use their Molotovs
    return true;
}
```

**Fix:** Remove the blanket return. Give the NPC throwing AI its own `find_firestarter_with_charges()` check.

## 3. Performance

### INV-P01 · Item deserialization deep-copies each container's contents once per nesting level (and regenerates uids)
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/savegame_json.cpp:3563`, `src/item_contents.cpp:725`, `src/item_contents.cpp:779`, `src/item_pocket.cpp:2270`, `src/item_pocket.cpp:2284`, `src/item_uid.h:22`, `src/item_location.cpp:1124`, `src/item_location.cpp:1132`

`item::deserialize` reads `contents` into a temporary `item_contents`, then calls `combine()`, which copies each item into the real pocket through `insert_item(const item &)` (including `can_contain` with recursive volume/weight). An item at depth d is deep-copied d times. `combine` also uses `std::advance` on a list for every pocket. The copy is also a correctness problem: `item_uid`'s copy constructor issues a fresh uid, so every contained item gets a new uid on each load. Restoring a saved in-container `item_location` by uid then fails with a debugmsg and returns nowhere.

```cpp
// savegame_json.cpp:3564-3568
item_contents read_contents;
data.read( "contents", read_contents );
contents.combine( read_contents, false, true, false, true );
// item_pocket.cpp:2283-2287
contents.push_front( it ); ... contents.push_back( it );
```

**Fix:** Add `item_contents::combine( item_contents &&, ... )`. When the saved pocket type matches the target, splice the `std::list<item>` nodes directly, after an `is_compatible` check. Otherwise fall back to `insert_item( std::move( item ) )`. Walk both pocket lists in lockstep.

### INV-P02 · Every visited item heap-allocates an `item_location` (2 allocs + atomics); every deref locks a `weak_ptr`
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/visitable.cpp:595`, `src/item_location.cpp:1004`, `src/item_location.cpp:1016`, `src/visitable.cpp:457`, `src/visitable.cpp:488`, `src/item_location.h:217`, `src/character_inventory.cpp:922`, `src/character_inventory.cpp:1019`, `src/visitable.cpp:686`, `src/safe_reference.h:29`, `src/item_location.cpp:153`

`item_pocket::visit_contents` builds `item_location( parent, &e )` for every nested item. The constructor does `new impl::item_in_container`, which a shared_ptr then wraps: 2 allocations, a parent shared_ptr copy and a `get_safe_reference`. Every `*node` calls `safe_reference::get()`, i.e. `impl.lock().get()`. In addition:
- The default `std::set<pocket_type>` argument is built on every top-level visit.
- `get_pseudo_items()` returns its cached vector by value.
- The default `item_location` constructor allocates `impl::nowhere`, so `get_wielded_item()` allocates even when unarmed (192 call sites).

`has_item_with`, `items_with`, `amount_of`, `charges_of`, `has_quality`, `max_quality`, the NPC AI scans and `find_index` all pay about 2N mallocs and 6N+ atomics per query.

```cpp
// item_location.cpp:1016-1017
item_location::item_location( const item_location &container, item *which )
    : ptr( new impl::item_in_container( container, which ) ) {}
// safe_reference.h:29-31
T *get() const { return impl.lock().get(); }
```

**Fix:**
1. Use `std::make_shared` in every constructor.
2. Share one static `impl::nowhere` between the default constructor and `get_wielded_item()`.
3. Replace `std::set<pocket_type>` with a bitmask in the visit and `is_type` APIs.
4. Return `const std::vector<const item *> &` from `get_pseudo_items()`.
5. Medium step: add a non-allocating `visit_items_raw( function<VisitResponse(const item &)> )` and move the location-agnostic helpers onto it.

Expected gain is 2-5x on inventory queries.

### INV-P03 · Zone, AIM and inventory filters re-parse `-` and `b:` sub-filters (and build `flag_id`s) per item
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_search.h:83`, `src/item_search.cpp:97`, `src/item_search.cpp:121`, `src/clzones.cpp:1312`

The exclusion lambda calls `filter_from_string( filter.substr( 1 ) )` on every evaluation. `b:` splits and parses both halves per item, and `f:` uppercases a string and constructs a `flag_id` per item. An excluded `V:`/`L:`/`M:` term also builds a fake item per item. Zone sorting evaluates the cached custom-zone filter per item per zone, so `cached_item_filter` does not help.

```cpp
// item_search.h:82-85
return [filter, basic_filter]( const T & i ) {
    return !filter_from_string( filter.substr( 1 ), basic_filter )( i );
};
```

**Fix:** Build the inner function once outside the lambda (`auto inner = filter_from_string( ... ); return [inner]( const T &i ){ return !inner( i ); };`). Do the same for `b:` and `f:`. The semantics stay identical.

### INV-P04 · `best_pocket` walks the whole inventory, allocating per leaf item and re-summing pocket volume/weight
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/item_pocket.cpp:2298`, `src/item_pocket.cpp:2315`, `src/item_contents.cpp:938`, `src/item_contents.cpp:944`, `src/item_pocket.cpp:452`, `src/item_pocket.cpp:2387`, `src/item_pocket.cpp:2399`, `src/item_contents.cpp:1165`, `src/item_container.cpp:552`, `src/character_inventory.cpp:297`, `src/character_inventory.cpp:441`

With `allow_nested` true by default, `best_pocket_in_contents` builds an `item_location` for every contained item and recurses into it, even when the item has no pockets. Per pocket, `_can_contain` re-sums the uncached recursive `volume()`/`weight()` of the contents. `better_pocket` calls `remaining_volume()` up to 4 times, `item_contents::can_contain` builds a translated failure string on every call, and `item::can_contain` computes the full recursive `volume()` first for the bags-of-holding check. Callers: pickup (twice per item), zone-sort `can_add`, NPC `can_take_that`, and the `try_add(copies)` and `fill_with` loops.

```cpp
// item_pocket.cpp:2312-2317 – no is_container() test
for( item &contained_item : contents ) { ...
    item_location new_loc( this_loc, &contained_item );
    ... contained_item.best_pocket( it, new_loc, ...
// item_contents.cpp:1165
ret_val<void> ret = ret_val<void>::make_failure( _( "is not a container" ) );
```

**Fix:** Low-risk steps:
- Skip non-containers before building `new_loc`.
- Implement the TODO at `item_contents.cpp:944`: skip nested soft pockets when the item exceeds the parent's remaining volume.
- Compute `remaining_volume()` once per side.
- Translate the failure string lazily.
- Run the cheap `bigger_on_the_inside` check on the inserted item first.

Medium step: cache `contents_volume`/`contains_weight` per pocket behind a dirty flag.

### INV-P05 · Each character's whole inventory tree is walked 2-3 times every turn (process + leak + overflow)
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/character.cpp:2281`, `src/character_inventory.cpp:2820`, `src/character_inventory.cpp:2837`, `src/character_inventory.cpp:2851`, `src/character_inventory.cpp:2859`, `src/item.cpp:4746`, `src/item.cpp:4905`, `src/item_pocket.cpp:2023`, `src/item_contents.cpp:2752`, `src/character_inventory.cpp:761`, `src/character_inventory.cpp:441`, `src/item_pocket.cpp:1835`, `src/do_turn.cpp:288`

`Character::process_turn` runs `process_items` (recursing into every nested item, active or not), `leak_items` (a second walk) and, when the cache is invalid, `drop_invalid_inventory`/`overflow` (a third walk with an `item_location` per item). This happens for the avatar and every NPC. Inactive items still pay `process_relic`, a fault lookup and `get_var("gun_heat")`. `item_pocket::process` re-evaluates the loop-invariant `spoil_multiplier()`/`can_contain_liquid(false)` for every child. `can_add`, a query, invalidates the validity cache. With 20-40 NPCs, that is thousands of recursive visits per turn.

```cpp
// item_pocket.cpp:2027-2031
for( auto iter = contents.begin(); ... ) {
    if( iter->process( here, carrier, pos, insulation, flag,
        std::min( spoil_multiplier_parent, spoil_multiplier() ), watertight_container || can_contain_liquid( false ) ) )
// character_inventory.cpp:441-442 (in can_add)
invalidate_inventory_validity_cache(); invalidate_leak_level_cache();
```

**Fix:** Short term: hoist the invariants out of the loop, fold `leak_items` into the `process_items` walk, and stop invalidating caches in `can_add`. Medium term: keep a per-Character active-item list maintained on acquire, wear and transform, plus a slow sweep (for example every 10 minutes) for temperature and rot of inactive items.

### INV-P06 · `item_stack::stored_volume` re-sums every item's recursive volume on each map add, so dumping onto a tile is O(n²)
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/item_stack.cpp:99`, `src/item_stack.cpp:142`, `src/map.cpp:5973`, `src/map.cpp:6009`, `src/item_location.cpp:370`, `src/vehicle.cpp:234`

`_add_item_or_charges → how_many_copies_fit → free_volume → stored_volume()` sums the recursive `volume()` of every item on the tile and evaluates `obj.volume()` twice. Piling N items on one tile (drop-all, butchery, disassembly, mapgen item groups, zone sorting) costs O(N²) volume evaluations, up to `MAX_ITEM_IN_SQUARE` = 4096.

```cpp
// item_stack.cpp:104-111
units::volume item_stack::stored_volume() const {
    units::volume ret = 0_ml;
    for( const item &it : *items ) { ret += it.volume(); }
    return ret;
}
// map.cpp:5975
obj.volume() == 0_ml ? INT_MAX : free_volume( e ) / obj.volume(),
```

**Fix:** First step: compute `obj.volume()` and `free_volume` once per call. Then keep a cached stored volume per tile in `submap`, updated in add/remove and invalidated on content changes. Vehicle cargo can share the approach.

### INV-P07 · `item_pocket::restack` is O(n²)–O(n³) and runs twice on every single insert
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_pocket.cpp:270`, `src/item_pocket.cpp:295`, `src/item_pocket.cpp:278`, `src/item_pocket.cpp:323`, `src/item_pocket.cpp:2182`, `src/item_pocket.cpp:1958`, `src/item_pocket.cpp:891`, `src/character_inventory.cpp:339`, `src/character_inventory.cpp:344`

`restack()` compares all pairs and resets `outer_iter` to `begin()` after each merge. `add()` restacks after `push_back`, `on_contents_changed()` restacks again, and `try_add` calls both. In practice the pocket is already stacked and most pairs fail at the `type` pointer compare, so this is O(n²) per call and cheap per pair. The magazine-branch hang in the same function is INV-B02.

```cpp
// item_pocket.cpp:2184-2189
contents.push_back( it );
if( ret == nullptr ) { restack(); } else { *ret = restack( &contents.back() ); }
// item_pocket.cpp:1958-1962
void item_pocket::on_contents_changed() { unseal(); restack(); }
```

**Fix:** Add `restack_one( item *inserted )` (O(n)) for add, insert and fill. Keep the full pass for load and migration without the `begin()` reset. Add a dirty flag so `on_contents_changed()` skips a redundant restack.

### INV-P08 · Autopickup runs `tname()`, option lookups and allocations per item on every player step
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/game.cpp:8366`, `src/pickup.cpp:466`, `src/pickup.cpp:497`, `src/auto_pickup.cpp:69`, `src/auto_pickup.cpp:141`, `src/auto_pickup.cpp:267`, `src/auto_pickup.cpp:792`, `src/auto_pickup.cpp:813`, `src/auto_pickup.cpp:735`

With AUTO_PICKUP on (off by default), every step evaluates every item on the tile, or on 9 tiles with ADJACENT. Each evaluation renders `tname()` (twice more per rule list on a cache miss), does `get_option` string lookups and builds a `flat_set<flag_id>`. There is no early return when the rule list is empty, even though `player_settings::empty()` exists. `select_items` already skips consecutive same-itype items.

```cpp
// auto_pickup.cpp:792
const std::string item_name = it.tname( 1, false );
// auto_pickup.cpp:69
!pickup_item->has_any_flag( cata::flat_set<flag_id> { flag_ZERO_WEIGHT, flag_NO_DROP } )
```

**Fix:** Return early when `get_auto_pickup().empty()`. Read the options once per call and make the flag set `static const`. Compute `tname` once and pass it to `create_rule`. Memoize per `(itype, variant)` within a call.

### INV-P09 · Pickup/add/remove paths deep-copy whole items (and their contents) 3–6 times each
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/pickup.cpp:226`, `src/character_inventory.cpp:979`, `src/character_inventory.cpp:989`, `src/character_inventory.cpp:1263`, `src/character_inventory.cpp:1288`, `src/character_inventory.cpp:456`, `src/character_inventory.cpp:462`, `src/character_inventory.cpp:309`, `src/character_inventory.cpp:374`, `src/item_container.cpp:603`, `src/item_contents.cpp:2779`, `src/item_pocket.cpp:613`, `src/item_pocket.cpp:1766`, `src/visitable.cpp:930`, `src/visitable.cpp:976`

A manual pickup copies the item, including its whole pocket tree and a fresh safe_reference anchor, about 5 times: `newit`, `can_pickWeight_partial`, `can_stash_partial`, by-value `i_add`, by-value `try_add`, and finally `push_back`. Related copies:
- `remaining_capacity_for_liquid` builds a `liquid_copy` that is never used.
- `item_pocket::remove_item` copies before checking membership.
- `remove_items_with` uses insert/push_back where it could splice or move.

```cpp
// character_inventory.cpp:979
item copy = it; if( it.count_by_charges() ) { copy.charges = 1; }
// item_contents.cpp:2779-2780 – never read
item liquid_copy = liquid; liquid_copy.charges = 1;
```

**Fix:** Copy in the `*_partial` helpers only when the charges change. Make `try_add` take `item &&`/`const item &` and add a `pocket->add( item && )` overload. Delete `liquid_copy`. Find the iterator first and move out of it in `remove_item`. Use `splice` and `std::move` in the `remove_items_with` variants.

### INV-P10 · Saving any `item_location` re-walks the owner's whole inventory to compute an unused legacy idx
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_location.cpp:279`, `src/item_location.cpp:421`, `src/item_location.cpp:597`, `src/item_location.cpp:768`, `src/item_location.cpp:60`, `src/item_location.cpp:164`, `src/item_location.cpp:1124`

The `serialize` methods for map, person and vehicle locations call `find_index`, a full owner walk with an `item_location` copy per node. The in-container version calls `calc_index` and then serializes its parent. On load, `idx` is only used when `uid_hint == 0`, and every item gets a uid in its constructor. Saving long activity target lists therefore costs O(locations × inventory). `find_index` also acts as an "item still at this location" check (it writes nowhere on -1), so dropping it changes that behaviour slightly.

```cpp
// item_location.cpp:171-175
if( uid_hint > 0 ) { i = unpack_by_uid( uid_hint ); }
if( !i && uid_hint == 0 ) { // Legacy save ... i = unpack( idx ); }
```

**Fix:** When `uid().is_valid()`, write only the uid. Keep `idx` for uid-less items and the vehicle-base sentinel. Take `const item_location &` in the walker lambdas.

### INV-P11 · `active_item_cache`: per-turn `item_reference` copies with vectors, and one expired item wipes the whole index
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/active_item_cache.cpp:124`, `src/active_item_cache.cpp:133`, `src/active_item_cache.cpp:62`, `src/active_item_cache.cpp:48`, `src/active_item_cache.cpp:50`, `src/map.cpp:6627`, `src/map.cpp:6673`

`get_for_processing()` copies `item_reference`s, each with a `pocket_chain` vector. A single expired reference clears the whole hash index for that speed bucket, and the next `add()` rebuilds it in O(n). `add()` also copies vectors and lists per recursion level and does a string `get_use("explosion")` lookup per item. The reviewer noted the per-turn volume is small (about size/speed entries per bucket), so the impact is unproven. The sibling-pocket accumulation in `add()` is a real correctness bug, reported as INV-B05.

```cpp
// active_item_cache.cpp:128, 131-134
items_to_process.push_back( *it );
// The item has been destroyed ...
active_items_index[kv.first].clear(); it = kv.second.erase( it );
```

**Fix:** Don't clear the index on expiry, because `add()` already validates entries. Prune only when the index exceeds twice the list size. Make `pocket_chain` cheap to copy (shared or small vector). Cache the explosion use as a bool on itype. Give `item_network_connections` a non-rotating accessor.

### INV-P12 · `vehicle_cursor` visits use the uncached string `"CARGO"` lookup and copy quality maps
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/visitable.cpp:758`, `src/visitable.cpp:1019`, `src/visitable.cpp:167`, `src/visitable.cpp:352`, `src/vehicle.cpp:3089`, `src/item_location.cpp:1659`

`part_with_feature( mount, "CARGO", ... )` binds to the `std::string` overload, which scans all parts with `use_cache = false`. `has_quality_from_vpart`/`max_quality_from_vpart` copy a `std::map` per part (`auto tq = ...`). The `find_item_by_uid` fallback builds a cursor per part, which is O(parts²), but it is a cold path.

```cpp
// visitable.cpp:758
const int idx = veh.part_with_feature( vp.mount, "CARGO", true );
// visitable.cpp:167
auto tq = vp.info().qualities;
```

**Fix:** Use `VPFLAG_CARGO`. Change `auto tq` to `const auto &tq`. Iterate `get_any_parts( VPFLAG_CARGO )` in `find_item_by_uid`. All three are one-line changes.

### INV-P13 · Liquid "pour into container" menu is O(N²): full inventory visit plus ~5 item copies per row
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_container.cpp:775`, `src/item_container.cpp:778`, `src/item_container.cpp:783`, `src/item_container.cpp:747`, `src/item_container.cpp:601`, `src/item_contents.cpp:2776`, `src/item_contents.cpp:2779`, `src/item_pocket.cpp:610`, `src/game_inventory.cpp:528`

`is_shown` calls the Character overload of `get_remaining_capacity_for_liquid` for every row. That overload calls `get_wielded_item()` twice and runs `has_item` (a full visit), then makes several item copies. Line 783 recomputes exactly the value already computed, so `min(x, x)` never changes `res` and `NO_SPACE_IN_PARENT` is never set. The parent-capacity check this was meant to perform does not exist.

```cpp
// item_container.cpp:778-783
const bool allow_bucket = ( p.get_wielded_item() && this == &*p.get_wielded_item() ) || !p.has_item( *this );
...
res = std::min( contents.remaining_capacity_for_liquid( liquid ), res );
```

**Fix:** Add an overload that takes the known location, use a cheap wielded check, implement the parent check that line 783 was apparently meant to be (`max_charges_by_parent_recursive`), drop the dead copy, and copy only when charges change.

### INV-P14 · Map visits construct a pseudo item per furniture tile per visit; `map_selector` does LOS per tile
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/visitable.cpp:699`, `src/visitable.cpp:702`, `src/visitable.cpp:727`, `src/map_selector.cpp:18`, `src/character_inventory.cpp:1224`, `src/character_inventory.cpp:1237`, `src/character.cpp:2801`, `src/character.cpp:2850`

Each map-cursor visit calls `furn(p)` twice and constructs a full `item` (plus an `item_location` to a stack item) for crafting pseudo-item furniture. The `map_selector` constructor runs `clear_path` for each of 169 tiles at PICKUP_RANGE. The weapon branch of `weight_carried_with_tweaks` lacks outfit's `without.empty()` fast path. The `best_nearby_lifting_assist` hot chain only fires when over capacity, so the measured cost is unproven. Separately, `best_nearby_lifting_assist( world_pos )` builds its selector from `pos_bub()` instead of `world_pos` (`character.cpp:2850`).

```cpp
// visitable.cpp:699-704
if( here->furn( p ) != furn_str_id::NULL_ID() ) {
    itype_id it_id = here->furn( p )->crafting_pseudo_item;
    if( it_id.is_valid() ) { item it( it_id ); ...
```

**Fix:** Cache one static pseudo item per furniture type and fetch `furn()` once. Skip `clear_path` for empty tiles. Add the `without.empty()` fast path. Use `world_pos` in `best_nearby_lifting_assist`.

### INV-P15 · Surroundings-menu sort renders `tname()` and lowercases it inside every comparison
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/map_entity_stack.cpp:287`, `src/map_entity_stack.cpp:295`, `src/map_entity_stack.cpp:388`, `src/map_entity_stack.cpp:394`, `src/surroundings_menu.cpp:326`

With NAME sort, the `std::sort` comparator in `apply_filter` renders `tname()` twice, lowercases both and calls `localized_compare` on every comparison, which adds up to thousands of renders for hundreds of stacks. `apply_filter` runs on menu open, filter commit and sort cycling. The priority-filter `erase` loop is O(n²), but it only shifts pointers.

```cpp
// map_entity_stack.cpp:293-295
compare_dist = localized_compare( to_lower_case( lhs_it->tname( ..., tname::unprefixed_tname ) ),
                                  to_lower_case( rhs_it->tname( ..., tname::unprefixed_tname ) ) );
```

**Fix:** Precompute a sort key per stack (lowercased name and category) and sort on it. Use `std::stable_partition` for the priority filters and return early when the priorities are empty.

## Appendix: considered and rejected

- **BURROW traits hardcoded to give pickaxe and shovel; mutations cannot grant pseudo-items** – The hardcode exists, but mutations already grant real tool items through `integrated_armor` (`mutation_data.cpp:549`), and data uses this widely. Moving BURROW to data is minor cleanup, not a missing capability.
- **`vehicle_cursor::remove_items_with` skips `invalidate_mass` on the early-return path** – The only real caller (`item_on_vehicle::remove_item`) already calls `on_contents_changed()`, which invalidates mass. Cargo consumption during crafting goes through `vehicle::remove_item`, which also invalidates it, so nothing goes stale.
- **Inventory search caches rebuild string keys and re-hash several times per call** – The code is accurate, but the cost is one small allocation and a few hashes per call, negligible next to item_location validation. The populate path is cold.
