# 29 · Inventory, AIM & crafting UIs

> Engine audit – static code reading at commit `a1a646d6`; nothing was compiled or run. Each finding was checked against the code by a separate reviewer: **confirmed** = verified by reading the code; **plausible** = consistent with the code, but reachability or impact not proven. Line numbers refer to that commit.

**Scope:** `src/inventory_ui.cpp`, `src/game_inventory.cpp`, `src/advanced_inv.cpp`, `src/advanced_inv_area.cpp`, `src/advanced_inv_pane.cpp`, `src/advanced_inv_listitem.cpp`, `src/advanced_inv_pagination.cpp`, `src/crafting_gui.cpp`, `src/crafting_gui_helpers.cpp`

## Summary

The item UIs work, but there are two serious correctness problems. AIM's Move All can dereference a null container and crash. Inventory drag-and-drop bypasses every take-off and self-containment check, which allows an exploit and a use-after-free. Several smaller action paths behave differently depending on the entry point: AIM take-off costs no moves, the unload menu asks "steal?" only when you press Enter, and AIM ignores category `sort_rank`. Performance problems come mostly from work that is recomputed on every frame or every keystroke: the ImGui crafting menu reruns heavy crafting logic about 100 times a second, AIM rebuilds whole panes on each filter keystroke, and several comparators and loops are quadratic. On the JSON side, the crafting menu and the EOC item pickers can't be opened or scoped from data, and a number of rules are hardcoded: farsight penalties, the 80% knowledge threshold, recipe category names, and the anesthesia waiver.

| Lens | Critical | High | Medium | Low | Total |
|---|---|---|---|---|---|
| Missing JSON / EOC | 0 | 0 | 4 | 5 | 9 |
| Bugs & quirks | 1 | 1 | 6 | 9 | 17 |
| Performance | 0 | 1 | 9 | 4 | 14 |
| **Total** | 1 | 2 | 19 | 18 | 40 |

**Fix first:**
1. **UIINV-B01** · AIM Move All null dereference on an invalid container pane: a reachable segfault, fixed with a one-line guard.
2. **UIINV-B02** · Inventory drag-and-drop skips take-off and containment checks: it allows stripping NO_TAKEOFF/INTEGRATED armor and can cause a use-after-free.
3. **UIINV-P01** · Crafting menu recomputes book and NPC scans about 100 times a second: the largest CPU cost in this system, and caching per selection removes it.
4. **UIINV-B04** · Gunmod "must be unloaded" check compares translated strings: the safety rule silently disappears in every localized build, and the fix is to compare `str()`.
5. **UIINV-P03** · `has_item()` full-inventory visit per menu entry: a small change to `held_by()` turns O(entries × items) into O(entries × depth).

## 1. Missing JSON bindings & EOC features

### UIINV-J01 · EOC item pickers (`u_run_inv_eocs` manual/manual_mult) can't set a hint, denial reasons, columns, none_message or count
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/npctalk.cpp:7054`, `src/npctalk.cpp:7069`, `src/npctalk.cpp:3389`, `src/npctalk.cpp:3397`, `src/game_inventory.cpp:339`, `src/game_inventory.cpp:352`, `src/game_inventory.cpp:147`, `src/game_inventory.h:39`

The only way to open an inventory selector from JSON is `u_run_inv_eocs` with `manual`/`manual_mult` (17 uses in shipped data). It goes through `titled_filter_menu` / `titled_multi_filter_menu`, which forward only a title. No hint is ever passed, and `none_message` is always the generic "You don't have the necessary item at hand." `inventory_filter_preset` overrides only `is_shown`, so an EOC can't show an item greyed out with a reason (every C++ menu does this via `get_denial`) and can't add info columns (`append_cell`). In `manual_mult`, the chosen amount (`dloc.second`) is thrown away: if the player marks 3 of 20 arrows, the EOC runs on the stack and never learns "3". The radius limit is by design, because `u_map_run_item_eocs` already covers map items.

```cpp
// npctalk.cpp:7071
return game_menus::inv::titled_filter_menu( filter, *guy, title.evaluate( d ).translated() );
// npctalk.cpp:3402-3411 – count ignored
for( const drop_location &dloc : selected ) { if( target == dloc.first ) { true_eoc = true; ...
```

**Fix:** Add a data-driven preset (e.g. `eoc_inventory_preset`) and pass more fields through `run_inv_eocs`. For `manual_mult`, set a context var `_count` from `dloc.second` before each `true_eoc` runs:
```json
{ "u_run_inv_eocs": "manual_mult", "title": "Offer reagents",
  "hint": "Each reagent adds 10 mana", "none_message": "You have nothing to offer.",
  "denial": [ { "condition": { "not": { "npc_has_flag": "MAGIC_FOCUS" } }, "message": "inert" } ],
  "columns": [ { "title": "COST", "math": "n_val('cost')" } ],
  "true_eocs": [ "EOC_OFFER" ] }
```

### UIINV-J02 · The crafting menu can't be opened or scoped from JSON (no EOC effect, no workbench filter, no `u_can_craft`)
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/crafting_gui.cpp:2971`, `src/crafting.cpp:433`, `src/iexamine.cpp:7370`, `src/game.cpp:1957`, `src/faction_camp.cpp:3328`, `src/mapdata.cpp:564`, `src/veh_type.cpp:442`, `src/condition.cpp:2041`

`select_crafter_and_crafting_recipe` already accepts a `goto_recipe`, an initial filter string (any search prefix such as `c:`, `p:`, `Q:`), camp mode and an inventory override. Only the internal "craft this item" path passes a filter. Workbench examine always opens the menu unfiltered. The furniture/vpart `workbench` objects read only multiplier/mass/volume, and no EOC effect opens the crafting menu (npctalk's `do_craft` only resumes an NPC's in-progress craft). The only recipe condition is `u_know_recipe`, and it always checks the player, so an EOC can't ask "can I craft X right now". Furniture `crafting_pseudo_item` and recipe tool requirements already allow stations that gate recipes. What is missing is pre-filtering the menu and a "craftable now" query.

```cpp
// iexamine.cpp:7370
you.craft( examp );  // no filter
// game.cpp:1957-1958 – the only filtered caller
std::string filterstring = string_format( "r:%s", itname ); you.craft( std::nullopt, recipe_id(), filterstring );
```

**Fix:** Add an effect `{ "u_open_crafting_menu": { "filter": "p:fabrication", "category": "CC_ENCHANTED", "recipe": "forge_sword" } }` that calls `Character::craft` at the talker's location. Add optional `"recipe_filter": "..."` / `"recipe_categories": [ "CC_ENCHANTED" ]` to the furniture and vpart `workbench` objects, and pass them through `iexamine::workbench_internal`. Add the condition `{ "u_can_craft": "recipe_id", "batch": 1 }` (or math `u_can_craft('id')`), backed by `availability( crafter, recipe ).can_craft_recipe`.

### UIINV-J03 · JSON conditions can't gate recipe availability; the NPC gate is a stub; the 80% knowledge threshold is hardcoded
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/crafting_gui_helpers.cpp:113`, `src/crafting_gui_helpers.cpp:123`, `src/crafting_gui_helpers.cpp:130`, `src/crafting_gui_helpers.cpp:476`, `src/recipe.cpp:1762`, `src/recipe.cpp:1974`, `src/crafting_gui.cpp:1233`, `src/crafting_gui.cpp:3037`, `src/crafting.cpp:442`, `src/character_crafting.cpp:183`

`gate_recipe` checks only skills, proficiencies and stat minimums (`character_requirements`). A recipe can't require a trait, flag, var or any dialogue condition. Mods fall back on learn/forget EOCs, which don't cover recipes learned from books or helpers. `recipe::npc_can_craft` always returns true, so every "NPC cannot craft" branch in the UI and in `crafting.cpp` is dead, and no JSON can stop allies from crafting ritual or psionic recipes. The "theoretical knowledge" rule (knowledge ≥ 80% of difficulty) is a literal `0.8` that appears twice: `crafting_gui_helpers.cpp:123` and `character_crafting.cpp:183`.

```cpp
// recipe.cpp:1974-1977
bool recipe::npc_can_craft( std::string & ) const { return true; }
// crafting_gui_helpers.cpp:123-124
g.crafter_has_primary_skill = crafter.get_knowledge_level( recp.skill_used ) >= static_cast<int>( difficulty * 0.8f );
```

**Fix:** Add a recipe `"condition"` (dialogue condition, `u_` = crafter) with an optional `"condition_failure_message"` and `"hide_if_condition_fails": true`. AND it into `base_requirements` in `gate_recipe`. Replace the stub with a `NO_NPC_CRAFT` recipe flag or an `"npc_condition"`. Move the 0.8 factor into a single function backed by a balance option or a recipe field `"knowledge_ratio": 0.8`.

### UIINV-J04 · Farsight crafting penalty and the INT/4 bonus are hardcoded per skill, and the UI copy has drifted from the real roll
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/crafting.cpp:3291`, `src/crafting.cpp:3293`, `src/crafting.cpp:3314`, `src/crafting.cpp:3317`, `src/crafting_gui.cpp:73`, `src/crafting_gui.cpp:1729`, `src/crafting_gui.cpp:1733`, `src/character_knowledge.cpp:448`, `src/character.cpp:1343`

`get_recipe_weighted_skill_average` gives HYPEROPIC characters −2 for electronics and −1 for tailoring. Its own comment asks for a JSON `vision_penalty` on skills. INT/4 is also fixed. The crafting GUI modifier table re-implements the formula and has already drifted: it accepts glasses or `effect_contacts` but not `effect_transition_contacts`, which the real roll accepts. A character wearing transition contacts is therefore shown a penalty that the roll doesn't apply. Reading uses yet another predicate. Corrective effects are matched by effect id at about 10 call sites, so mods can't add lenses or give new skills a vision penalty.

```cpp
// crafting_gui.cpp:1735-1737
if( crafter->has_flag( json_flag_HYPEROPIC ) && !crafter->worn_with_flag( flag_FIX_FARSIGHT ) && !crafter->has_effect( effect_contacts ) ) {
// crafting.cpp:3292-3295
// This should be changed to a json-defined penalty by skill (vision_penalty) in skills.json
... !has_effect( effect_transition_contacts ) ) {
```

**Fix:** Add a skill field `"vision_penalty": 2` and a single `Character::has_uncorrected_farsight()` that checks a `FIX_FARSIGHT` character flag (granted by worn items, by effects through their `flags`, or by enchantments) instead of effect ids. Add `"crafting_stat": "INT", "crafting_stat_divisor": 4` on skills. Have `crafting.cpp` return a `std::vector<skill_modifier>` breakdown that both the roll and `draw_modifier_table` use.

### UIINV-J05 · The HIDDEN_ITEM flag only hides items in AIM
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/advanced_inv_pane.cpp:96`, `src/activity_actor.cpp:7749`, `src/inventory_ui.cpp:1993`, `src/inventory_ui.cpp:2028`, `doc/JSON/JSON_FLAGS.md:1522`

The only reader of `json_flag_HIDDEN_ITEM` is `advanced_inventory_pane::is_filtered`. The generic `inventory_selector`, which drives inventory, pickup, drop, eat, wield and every `game_menus::inv` menu, never checks it. The doc says "cannot be seen in AIM", so this matches the doc, and vanilla's one use (planted seeds) is protected by other means. But there is no flag that hides a marker item from all item UIs.

```cpp
// inventory_ui.cpp:1993 – no flag check
if( !preset.is_shown( locations.front() ) ) { return nullptr; }
```

**Fix:** Check `HIDDEN_ITEM` in `inventory_selector::add_entry_rec` and the `V` list, and update the doc. If the narrow meaning is wanted, split it into `HIDDEN_ITEM_AIM` and a general `HIDDEN_ITEM`.

### UIINV-J06 · Autodoc/surgeon menus re-derive surgery math and hardcode `bio_painkiller` as the anesthesia waiver
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/game_inventory.cpp:2852`, `src/game_inventory.cpp:2854`, `src/game_inventory.cpp:2932`, `src/game_inventory.cpp:2953`, `src/game_inventory.cpp:2982`, `src/game_inventory.cpp:3041`, `src/bionics.cpp:1960`, `src/bionics.cpp:1974`, `src/iexamine.cpp:5898`, `src/iexamine.cpp:5940`, `src/activity_actor.cpp:1634`

The install menus copy several surgery formulas instead of calling shared code: the anesthetic amount (`bodyweight_kg/10 * difficulty*2`), the duration (`difficulty*20` minutes) and the "anesthesia unneeded" rule. That rule tests the bionic id `bio_painkiller` next to `PAIN_IMMUNE`, and the same check is repeated in bionics.cpp, iexamine.cpp and activity_actor.cpp. The copies have already drifted: `has_enough_anesth` waives anesthesia for `MANUAL_CBM_INSTALLATION`, but the autodoc hint and `iexamine:5898` don't. A mod CBM or trait can't waive anesthesia without reusing that id. Audit/17 MUT-J09 covers the wider surgery model.

```cpp
// game_inventory.cpp:2932-2935
const int weight = units::to_kilogram( pa.bodyweight() ) / 10;
const int duration = loc.get_item()->type->bionic->difficulty * 2;
const requirement_data req_anesth = *requirement_data_anesthetic * duration * weight;
```

**Fix:** Add a character flag `NO_ANESTHESIA_NEEDED`, grant it from `bio_painkiller`'s JSON, and test only flags. Add shared `anesthetic_requirement(cbm, patient)` and `bionic_surgery_duration(cbm)` helpers in bionics.cpp and use them everywhere. Optionally add a bionic field `"surgery_time_per_difficulty"`.

### UIINV-J07 · Eat-menu ordering hardcodes `water_clean` and the SAPROPHAGE/SAPROVORE traits
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/game_inventory.cpp:870`, `src/game_inventory.cpp:874`, `src/game_inventory.cpp:88`, `src/game_inventory.cpp:100`, `src/consumption.cpp:516`, `src/consumption.cpp:1056`, `src/consumption.cpp:1219`

`comestible_inventory_preset::get_order` always puts the single itype `water_clean` first, so a mod's clean-water variants are never prioritised. It ranks rotten food "eat first" only for the trait ids SAPROPHAGE and SAPROVORE. The original finding argued for `IMMUNE_SPOIL` instead. The reviewer pointed out that the rotten-food morale penalty and warning are themselves keyed on those two traits (consumption.cpp:516, 1056), so the current ordering is consistent. What remains is id hardcoding that belongs to the wider hardcoded rotten-food preference.

```cpp
// game_inventory.cpp:870-878
if( loc->typeId() == itype_water_clean ) { return 0; }
else if( loc->rotten() ) {
    if( you.has_trait( trait_SAPROPHAGE ) || you.has_trait( trait_SAPROVORE ) ) { return 1; } else { return 5; }
```

**Fix:** Replace the trait test with a character flag shared with consumption.cpp (e.g. `PREFERS_ROTTEN`), not `IMMUNE_SPOIL`. Replace the `water_clean` id with a comestible field such as `"consume_menu_priority": -1` or an item flag `EAT_MENU_FIRST`.

### UIINV-J08 · `recipe_category` has no display name or sort order; `is_practice` is dead; special subcategories are hardcoded and undocumented
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/crafting_gui.cpp:195`, `src/crafting_gui.cpp:178`, `src/crafting_gui.cpp:226`, `src/crafting_gui.cpp:597`, `src/crafting_gui.cpp:792`, `src/crafting_gui.cpp:827`, `src/crafting_gui.h:44`, `src/crafting_gui_helpers.cpp:174`, `src/crafting_gui_helpers.cpp:222`, `src/crafting_gui.cpp:3300`, `src/crafting_gui.cpp:325`, `lang/string_extractor/parsers/recipe_category.py:9`

`crafting_category::load` reads only four bools and `recipe_subcategories`. Tab labels are cut out of the id (`CC_` / `CSC_<CAT>_` stripped), so mods can't give a category a readable name, and tab order is load order. The translation extractor derives labels differently from the C++ (`split('_')[1]` / `[-1]`), so ids with extra underscores would extract the wrong msgid. No shipped id has extra underscores, so this is latent. The category field `is_practice` has no reader (line 222 uses the recipe's own `is_practice()`). The FAVORITE/RECENT/HIDDEN subcategory meanings are matched by string. `subcategories_for_category` has no callers, and doc/JSON has no section for this type.

```cpp
// crafting_gui.cpp:792 / 827
std::string label = _( get_cat_unprefixed( crafting_cats[i] ) );
std::string label = _( get_subcat_unprefixed( tab.cur(), subcats[i] ) );
```

**Fix:** Add a `"name"` field (falling back to the id-derived label) and a `"sort_rank"`, and allow object entries in `recipe_subcategories` (`{ "id": "CSC_PSIONIC_TELEPATHY", "name": "Telepathy" }`). Sort `crafting_cats` by rank and update the extractor to read the name fields. Remove or implement `is_practice`, delete `subcategories_for_category`, and document the type in ITEM_CRAFT_AND_DISASSEMBLY.md.

### UIINV-J09 · Drinking from vehicle tanks is tied to the `water_faucet` itype and HOSE quality
**Severity:** Low · **Effort:** Medium · **Verified:** plausible
**Where:** `src/inventory_ui.cpp:2273`, `src/inventory_ui.cpp:2296`, `src/inventory_ui.cpp:2328`, `src/vehicle_use.cpp:1927`, `src/vehicle_use.cpp:2777`, `src/vehicle.cpp:8499`, `src/crafting.cpp:1602`, `src/activity_item_handling.cpp:4597`

`add_vehicle_tank_items` (consume menu) shows tank liquids only through `part_with_tool( itype_water_faucet )` or a HOSE tool. The code itself calls this a hack (vehicle_use.cpp:1927), and the same check appears at six sites. A mod vpart can already declare `"pseudo_tools": [ { "id": "water_faucet" } ]` without pulling in the item's recipes, so taps are possible. What is missing is a properly named hook and any way to restrict which liquids a tap dispenses.

```cpp
// inventory_ui.cpp:2328
if( vp.part_with_tool( here, itype_water_faucet ) ) {
```

**Fix:** Add a vpart flag `"TANK_ACCESS"` (or a pseudo-tool quality `"TANK_TAP"`) and replace the six itype checks with it. Give `water_faucet` the flag in data so vanilla behaviour is unchanged. Optionally add `"tank_access_liquids": [ "beer" ]`.

## 2. Bugs, oversights & quirks

### UIINV-B01 · AIM Move All crashes when the destination is a container pane whose container became invalid
**Severity:** Critical · **Effort:** Small · **Verified:** confirmed
**Where:** `src/advanced_inv.cpp:1053`, `src/advanced_inv.cpp:755`, `src/advanced_inv_pane.cpp:83`, `src/advanced_inv.cpp:1089`, `src/advanced_inv.cpp:1192`

`recalc_pane()` sets `pane.container = nowhere` when the viewed container is no longer adjacent, but it leaves the area as `AIM_CONTAINER`. `load_settings()` restores a saved container without checking it, and a dropped or destroyed container also yields a null location. `move_all_items()` dereferences `dpane.container.get_item()` before any validity check. The guards at 1089 (`canputitems`) and 1192 come too late. Trigger: point the right pane at a container on an adjacent tile, close AIM, walk two tiles away and reopen AIM (the last layout is restored by default). The pane shows "Invalid container!", and Move All in the left pane segfaults. Single-item moves are safe because they stop earlier with "Destination area is full".

```cpp
// advanced_inv.cpp:1053-1054
if( dpane.get_area() == AIM_CONTAINER &&
    dpane.container.get_item()->has_flag( json_flag_NO_RELOAD ) ) {
```

**Fix:** Guard with `!dpane.container ||` and print "Destination container isn't valid.", mirroring the source-pane check at 1043-1046. Better, make `recalc_pane` switch the pane back to `container_base_loc` (or `AIM_ALL`) when the container is invalidated, so no code path ever sees `AIM_CONTAINER` with a null container.

### UIINV-B02 · Inventory drag-and-drop skips take-off and self-containment checks (exploit, use-after-free)
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/inventory_ui.cpp:2054`, `src/inventory_ui.cpp:2079`, `src/inventory_ui.cpp:2098`, `src/inventory_ui.cpp:3964`, `src/character_inventory.cpp:3170`, `src/game_inventory.cpp:284`, `src/visitable.cpp:898`, `src/item_pocket.cpp:1407`

The `i` inventory enables drag-and-drop. `drag_drop_item()` checks only `sourceItem != destItem` and `pocket->can_contain()`, then calls `Character::store` → `pocket->insert_item( i_rem( &put ) )`. There is no `can_takeoff` check, so INTEGRATED, NO_TAKEOFF, power-armor or shapeshifted worn items can be bagged. There is no `eventually_contains` or `parents_can_contain_recursive` check either, while AIM and the holster preset have both. Dragging a soft bag onto a bag nested inside it destroys the source tree through `i_rem`, including the pocket that `pocket` points to, and `insert_item` then writes into freed memory. (`on_takeoff` does run via `outfit::remove_items_with`, and NO_UNWIELD items are already rejected by `item_pocket::is_compatible`.)

```cpp
// inventory_ui.cpp:2079 / 2098
ret_val<item_pocket::contain_code> contain = pocket->can_contain( *sourceItem );
u.store( pockets[action_menu.ret], *sourceItem, true, base_move_cost );
// character_inventory.cpp:3170
ret_val<item *> result = pocket->insert_item( i_rem( &put ) );
```

**Fix:** Deny the drop when the destination is, or is contained in, the source (`item_location::eventually_contains`). Run the same checks as `inventory_holster_preset::get_denial`: `can_takeoff`, `can_contain(..., parent)` and `parents_can_contain_recursive`. Route the move through `insert_item_activity_actor` as AIM does.

### UIINV-B03 · AIM Worn→Inventory takes armor off instantly for 0 moves
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/advanced_inv.cpp:1712`, `src/advanced_inv.cpp:1715`, `src/character_attire.cpp:573`, `src/character_attire.cpp:1533`, `src/item_location.cpp:497`, `src/handle_action.cpp:1658`

This branch calls `Character::takeoff(int)` → `outfit::takeoff`, which never calls `mod_moves`. The `T` command and the item menu go through `loc.obtain()`, which charges `item_handling_cost + 250`. Worn→ground and worn→container moves do cost time, so only this path is free. In combat, a player can swap gear at no time cost.

```cpp
// advanced_inv.cpp:1712-1715
if( srcarea == AIM_WORN && destarea == AIM_INVENTORY ) { ...
    player_character.takeoff( Character::worn_position_to_index( sitem->idx ) + 1 ); }
```

**Fix:** Use `player_character.takeoff( loc.obtain( player_character ) )` like the `T` menu, or charge `obtain_cost` before calling takeoff.

### UIINV-B04 · Gunmod "must be unloaded" rule compares translated location names to English literals
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/game_inventory.cpp:1342`, `src/item_gun_tool_ammo.cpp:3087`, `src/item_gun_tool_ammo.cpp:3095`, `src/itype.cpp:25`

`gunmod_location::name()` returns `_( _id )`, and locations are extracted for translation. The removal denial and the install check compare `name()` against `"magazine"`, `"mechanism"`, `"loading port"`, `"bore"` (and `"stock accessory"` on install). In any language that translates these words the check never matches, so mods can be removed from or installed on a loaded gun. Other call sites correctly compare `str()`.

```cpp
// game_inventory.cpp:1342
if( ( mod.type->gunmod->location.name() == "magazine" || ... == "bore" ) &&
    ( gun.ammo_remaining( ) > 0 || gun.magazine_current() ) )
```

**Fix:** Compare `location.str()` in both places. Better, add a JSON field on the gunmod location or slot (e.g. `"requires_unloaded": true`) instead of the hardcoded list.

### UIINV-B05 · AIM Unload always says "Nothing to unload" in a container pane
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/advanced_inv.cpp:1827`, `src/advanced_inv.cpp:1830`, `src/advanced_inv.cpp:1956`

`action_unload` starts with `src = spane.container` and then uses `if( !src && sitem ) {...} else { "Nothing to unload."; return false; }`. In a container view `src` is valid, so the else branch always runs, whatever item is highlighted. Trigger: open a backpack in AIM, highlight a loaded gun inside it and press Unload.

```cpp
item_location src = spane.container;
if( !src && sitem ) { src = sitem->items.front(); }
else { add_msg( m_info, _( "Nothing to unload." ) ); return false; }
```

**Fix:** Prefer the highlighted item: `if( sitem ) src = sitem->items.front(); else if( !src ) { msg; return false; }`.

### UIINV-B06 · Unload menu asks "steal?" only on Enter; hotkey, mouse and AIM unload skip the prompt
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/inventory_ui.cpp:5356`, `src/inventory_ui.cpp:5359`, `src/inventory_ui.cpp:5369`, `src/avatar_action.cpp:1278`, `src/advanced_inv.cpp:1840`

`unload_selector::execute` calls `check_stealing` only in the CONFIRM branch. Selecting by invlet or mouse click returns directly, and AIM's `action_unload`, `Character::unload` and `unload_activity_actor` never prompt. NPC-owned containers on adjacent tiles can be emptied without the "Really steal?" prompt. Theft-witness logic still runs later through `on_pickup`, so only the prompt is skipped. Whether you get asked depends on which key you press.

```cpp
// inventory_ui.cpp:5356 (SELECT) – no check
return {input.entry->any_item(), uistate.unload_auto_contain};
// 5372-5375 (CONFIRM only)
if( !avatar_action::check_stealing( get_player_character(), to_unload ) ) {...}
```

**Fix:** Move `check_stealing` into one place every entry point passes through: `avatar_action::unload`, `Character::unload` or `unload_activity_actor::start`.

### UIINV-B07 · AIM "sort by category" orders by pointer address, ignoring JSON `sort_rank`
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/advanced_inv.cpp:602`, `src/advanced_inv.cpp:604`, `src/advanced_inv_listitem.h:64`, `src/advanced_inv_listitem.cpp:20`, `src/advanced_inv_listitem.cpp:41`, `src/item.cpp:4097`, `src/item_category.cpp:69`, `src/inventory_ui.cpp:770`, `src/inventory_ui.cpp:773`, `data/json/item_category.json:59`

`advanced_inv_sorter` compares raw `const item_category *` pointers into `generic_factory` storage, so AIM orders categories by JSON load order. `item_category::operator<` (sort_rank, then name), which the `i` menu uses, is ignored, as is the documented `sort_rank`. In vanilla, CLOTHING (19) shows above FOOD (−18), and TOOL MAGAZINES (−15) lands after CORPSES (12). Mod categories always go after the core ones. Grouping still works; only the order of the headers is wrong. (The JSON and bug auditors both reported this, so the two findings are merged here.)

```cpp
// advanced_inv.cpp:602-605
case SORTBY_CATEGORY:
    if( d1.cat != d2.cat ) { return d1.cat < d2.cat; }
// inventory_ui.cpp:773
return *lhs.get_category_ptr() < *rhs.get_category_ptr();
```

**Fix:** `return *d1.cat < *d2.cat;`, keeping the pointer inequality test. The pagination pointer-equality test is fine as is.

### UIINV-B08 · Collation highlight and next-entry paths dereference an unchecked `find_by_location` result
**Severity:** Medium · **Effort:** Small · **Verified:** plausible
**Where:** `src/inventory_ui.cpp:2431`, `src/inventory_ui.cpp:2432`, `src/inventory_ui.cpp:2462`, `src/game_inventory.cpp:189`, `src/game_inventory.cpp:219`

In hidden-mode `highlight()`, the parent entry comes from `find_by_location`, which can return null. The code then tests `elem` (the column) instead of `pent` before dereferencing. The originally described trigger (reopening the Eat menu) is not reachable as written. The reviewer found a different, more likely crash in `get_collation_next` at 2462: expand a collated group in the Eat menu, then filter for text that matches a non-tip copy but not the tip (e.g. "hot" when only one copy is hot). The header moves to `entries_hidden` while the expanded entry stays visible, and pressing Enter dereferences null.

```cpp
// inventory_ui.cpp:2431-2433
inventory_entry *pent = elem->find_by_location( parent );
if( elem != nullptr ) { elem->set_collapsed( *pent, false );
// 2462
inventory_entry const &p_ent = *col.find_by_location( ent.collation_meta->tip );
```

**Fix:** Test `pent != nullptr`. In `get_collation_next`, check the pointer and return `{}` when the tip isn't visible.

### UIINV-B09 · Crafting search `r:` discards earlier comma-chained filters
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/crafting_gui_helpers.cpp:716`, `src/crafting_gui_helpers.cpp:726`

The `r:` case builds its result from the full `available_recipes` and assigns it to `filtered_recipes` instead of intersecting, so `c:plank,r:bow` ignores `c:plank`. An in-code TODO admits this, while the help text advertises comma chaining for every prefix.

```cpp
// TODO: this replaces filtered_recipes instead of intersecting, ...
result.include( recipe_subset( available_recipes, ... ) ); ... filtered_recipes = result;
```

**Fix:** Build from `filtered_recipes`, or `filtered_recipes = filtered_recipes.intersection( result );`.

### UIINV-B10 · "Missing required proficiencies" also lists proficiencies the crafter already has
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/crafting_gui.cpp:1206`, `src/crafting_gui.cpp:1209`, `src/recipe.cpp:1468`

The warning prints every id from `recp.required_proficiencies()` in red, unfiltered, so a crafter missing only B is told A and B are both missing.

```cpp
auto profs = recp.required_proficiencies();
for( const auto &prof : profs ) { cataimgui::TextColoredParagraph( c_red, prof->name() ); ...
```

**Fix:** Skip proficiencies the crafter or helpers have (as `character_has_required_proficiencies` does), or reuse `recipe::required_proficiencies_string(crafter)`.

### UIINV-B11 · Recipe result header shows "(1)" instead of the total yield for charge-based results
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/crafting_gui_helpers.cpp:802`, `src/crafting_gui_helpers.cpp:804`, `src/crafting_gui_helpers.cpp:892`

`result_item_header` builds `display_name` before it sets `dummy_item.charges = total_quantity`, so the "Result:" and "With byproduct:" lines show one charge (e.g. one round instead of 200). `result_item_details` does it in the right order. The main yield appears elsewhere in the panel, but byproduct totals don't. Restoring by dividing would also divide by zero for a zero amount.

```cpp
std::string display_name = ( description.empty() ? dummy_item.display_name() : description );
dummy_item.charges = total_quantity;
... dummy_item.charges /= total_quantity;
```

**Fix:** Set charges before building the name, and restore a saved value instead of dividing.

### UIINV-B12 · Crafting menu hangs if the goto recipe's category is hidden
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/crafting_gui.cpp:598`, `src/crafting_gui.cpp:614`, `src/output.h:1243`

`crafting_cats` leaves out `is_hidden` categories, but the goto path loops `while( tab.cur() != goto_recipe->category.str() ) tab.next();`, and `tab_list::next` wraps around. A recipe in a hidden category can still be started through search, so "view recipe" on such an in-progress craft (game.cpp:1929) loops forever. Vanilla only hides CC_BUILDING, so this mostly affects mods.

```cpp
while( tab.cur() != goto_recipe->category.str() ) { tab.next(); }
```

**Fix:** Look the category up in `crafting_cats` with `std::find` and switch tabs only if it is present (otherwise fall back to a filter string).

### UIINV-B13 · Component colors in the crafting panel use batch-1, avatar-based availability
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/crafting_gui.cpp:1922`, `src/requirements.cpp:1111`, `src/requirements.cpp:1188`, `src/crafting_gui_helpers.cpp:156`

`draw_components` refreshes the mutable availability flags with the default batch of 1, no craft flags and the avatar. This overwrites what `availability` computed for the real batch and crafter. The brown `a_insufficient` marking (a component also needed as a tool or quality) is then wrong for batches. For example, rock as a component plus hammering quality, at batch 3 with 3 rocks, shows green. Upstream `get_folded_list` has the same pattern.

```cpp
// Ensure availability cache is fresh
req.can_make_with_inventory( &get_player_character(), crafting_inv, filter );
```

**Fix:** Pass `crafter`, `batch_size` and the same craft flags that `availability` uses.

### UIINV-B14 · Tool-group name cache is a function static that is never reset (stale after a world or language change)
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/crafting_gui_helpers.cpp:1229`, `src/crafting_gui_helpers.cpp:1231`, `src/crafting_gui.cpp:2126`, `src/requirements.cpp:549`, `src/requirements.cpp:830`, `src/requirements.cpp:1303`

Named requirements lose their identity when recipes inline them. `find_tool_group_name` recovers the name by matching sorted tool lists against `requirement_data::all()` and caches the results in a function-static map with a `built` flag that is never cleared. `requirement_data::reset()` clears the source data on world load, but this cache survives. After loading a world with a different mod list, new groups show as "One of" and old labels can persist. The stored strings are already translated, so labels also stay in the old language after a language change. Ties between identical tool lists are broken arbitrarily by id order. (Reported by both the JSON and bug auditors, merged here.)

```cpp
static std::map<std::vector<itype_id>, std::string> cache;
static bool built = false;
... cache.try_emplace( std::move( ids ), rdata.display_name() ); built = true;
```

**Fix:** Keep the source requirement id on each tool group when it is inlined, and look the name up at draw time. At minimum, rebuild the cache in `requirement_data::finalize()`/`reset()` and store ids instead of translated strings.

### UIINV-B15 · Count prompts are saved to the shared "item_filter" history and become the V-menu filter after a reload
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/inventory_ui.cpp:3276`, `src/inventory_ui.cpp:3314`, `src/inventory_ui.cpp:559`, `src/string_input_popup.cpp:500`, `src/surroundings_menu.cpp:229`

`query_count()` reuses `query_string()`, which tags the popup `item_filter`. Confirming "5" pushes it into the history that AIM, zones and the `V` list share. On load, `list_item_filter` is rebuilt from `gethistory("item_filter").back()`. So if the V filter was active, it is now "5".

```cpp
spopup->identifier( "item_filter" ).hist_use_uilist( false );   // 3276
list_item_filter = gethistory( "item_filter" ).back();           // 559-560
```

**Fix:** Give `query_string` an identifier parameter and use a separate id (or none) for counts. Save `list_item_filter` directly.

### UIINV-B16 · "Select gun to modify" sort comparator is not a strict weak ordering
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/game_inventory.cpp:1286`, `src/game_inventory.cpp:1290`, `src/inventory_ui.cpp:1434`, `src/inventory_ui.cpp:1582`

When lhs has worse odds, `gunmod_inventory_preset::sort_compare` falls through to the name comparison instead of returning false. So `comp(a,b)` and `comp(b,a)` can both be true, which is undefined behaviour for `stable_sort`. In practice the result is inconsistent ordering, not a crash.

```cpp
if( a.first > b.first || ( a.first == b.first && a.second < b.second ) ) { return true; }
return inventory_selector_preset::sort_compare( lhs, rhs );
```

**Fix:** `if( a != b ) return a.first > b.first || ( a.first == b.first && a.second < b.second ); return base;`

### UIINV-B17 · AIM wear quantity discards the carry-weight cap
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/advanced_inv.cpp:2320`, `src/advanced_inv.cpp:2311`, `src/advanced_inv.cpp:1627`, `src/character_attire.cpp:262`

In `query_charges`, the `AIM_WORN` block overwrites the weight-limited amount with `std::min( slots_available, input_amount )`. The originally claimed "destination full" message is unreachable, because `can_wear()` runs first. The weight cap is lost, but that only matters near 4× carry capacity when wearing several of one type from the ground. Also, the weight check counts items already in inventory as extra weight, so a plain `min(slots, amount)` would wrongly limit inventory→worn moves.

```cpp
amount = std::min( weightmax, amount );            // 2311
amount = std::min( slots_available, input_amount ); // 2320
```

**Fix:** Apply the weight cap only for non-inventory sources, then take `min( slots_available, amount )`.

## 3. Performance

### UIINV-P01 · Crafting menu redraws about 100 times a second and reruns heavy crafting logic every frame
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/crafting_gui.cpp:2985`, `src/ui_manager.cpp:482`, `src/ui_manager.cpp:499`, `src/crafting_gui.cpp:1140`, `src/crafting_gui.cpp:1859`, `src/crafting_gui.cpp:1410`, `src/crafting_gui.cpp:1673`, `src/crafting_gui.cpp:1058`, `src/crafting_gui.cpp:1922`, `src/crafting_gui.cpp:1936`, `src/crafting_gui.cpp:1963`, `src/crafting_gui.cpp:2137`, `src/crafting_gui.cpp:308`, `src/crafting.cpp:509`, `src/recipe.cpp:112`, `src/crafting.cpp:756`, `src/crafting.cpp:5770`

The loop uses `set_timeout(10)`, and ImGui windows are always redrawn, so the info panel runs about 100 times a second even when the player is idle. Every frame calls `expected_time_to_craft` (line 1140). That reaches `book_bonuses_nearby()`, which visits carried items plus a flood fill over reachable map tiles, and `get_crafting_helpers()`, which runs a `clear_path` check per NPC. Each frame also computes success and destruction chances, the lighting multiplier, `get_books_for_recipe` and `count_item`, and runs `can_make_with_inventory` plus repeated `has()` calls per alternative (including inside a sort comparator) in `draw_components`/`draw_requirement_tools`. The cached item-info vector is also copied every frame. With "details" expanded, lines 1410, 1673 and 1859 add more, and 1673 ignores the crafting group already cached at 586. The open menu burns CPU constantly, and input lags in busy bases.

```cpp
ctxt.set_timeout( 10 );
while( !impl.is_done() ) { ui_manager::redraw_invalidated(); std::string action = ctxt.handle_input();
// recipe.cpp:112
return { guy.book_bonuses_nearby(), compute_tool_speeds( rec, guy ) };
```

**Fix:** Add a per-selection `recipe_panel_cache` to `crafting_ui_impl` (time, chances, lighting, book text, helper and proficiency flags, component and tool rows, result name). Rebuild it only on selection, batch, crafter or inventory changes, and have the `draw_*` functions only read it. Compute `book_bonuses_nearby`/`compute_tool_speeds` once per rebuild, and return item info by const reference.

### UIINV-P02 · `recipe::result_name()` builds a temporary item on every call (per visible row per frame, twice per sort comparison)
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/recipe.cpp:1832`, `src/recipe.cpp:1841`, `src/crafting_gui_helpers.cpp:333`, `src/crafting_gui_helpers.cpp:1030`, `src/crafting_gui_helpers.cpp:1129`, `src/crafting_gui.cpp:914`, `src/crafting_gui.cpp:992`, `src/crafting_gui.cpp:1526`, `src/crafting_gui.cpp:2418`

`result_name()` constructs a full `item` and calls `tname()`. It runs for each clipped list row every frame, in the info header and for nested children every frame, and twice per comparison in `recipe_sort_compare`, which drives both stable_sorts on every `recalculate_recipes` (tab switch, search, favorite toggle). Nothing memoizes it. It is not proven to be the dominant cost: building availability and filtering are comparable.

```cpp
item temp_item( result_ ); ... name = temp_item.tname( 1, segs );          // recipe.cpp:1841-1850
const std::string a_name = a->result_name(); const std::string b_name = b->result_name();
```

**Fix:** Compute a decorated sort key per entry once in `build_recipe_list`, and keep display names in a vector parallel to `current` that the list and header read.

### UIINV-P03 · `Character::has_item()` (full inventory visit) runs per menu entry during invlet assignment
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/inventory_ui.cpp:1686`, `src/inventory_ui.cpp:2566`, `src/inventory_ui.cpp:2617`, `src/inventory_ui.cpp:2626`, `src/game_inventory.cpp:569`, `src/inventory_ui.cpp:669`, `src/visitable.cpp:88`, `src/item_location.cpp:1471`

`reassign_custom_invlets` calls `p.has_item()` for every selectable entry. That is a full `visit_items` that builds an `item_location` per node, and for map entries it never aborts. `prepare_layout()` runs it twice. Cost is O(entries × carried items) per layout on open, filter, favorite and show/hide in every `inv_internal` menu. The pickup preset's `get_denial` does the same, but its result is cached per entry, so it costs only at build time.

```cpp
if( elem.is_selectable() && !p.has_item( *elem.any_item() ) ) {   // 1686
bool item_location::held_by( Character const &who ) const { return carrier() == &who; }
```

**Fix:** Use `elem.any_item().held_by( p )` / `loc.held_by( you )`, and run `reassign_custom_invlets` only on the final layout pass.

### UIINV-P04 · AIM list item construction: three tname calls, built before filtering, copied
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/advanced_inv_listitem.cpp:33`, `src/advanced_inv_listitem.cpp:34`, `src/advanced_inv_listitem.cpp:36`, `src/auto_pickup.cpp:698`, `src/advanced_inv_pane.cpp:106`, `src/advanced_inv_pane.cpp:143`, `src/advanced_inv_pane.cpp:286`, `src/advanced_inv_pane.cpp:480`, `src/advanced_inv_area.cpp:284`

Each `advanced_inv_listitem` calls `tname(1)` and `tname(sort_key)`, and `has_rule()` recomputes `tname(1)` again even when there are no rules. The listitem is fully built before `is_filtered()` is checked, so filtered-out items pay the full cost, and kept items are copied with `push_back(adv_it)`. All of this runs on every recalc, including each filter keystroke (see P07). The O(n²) claim for `item_list_to_stack` is overstated: type mismatches return immediately.

```cpp
name( list.front()->tname( 1 ) ), name_without_prefix( list.front()->tname( 1, tname::tname_sort_key ) ),
... autopickup( get_auto_pickup().has_rule( & * list.front() ) )
advanced_inv_listitem it( locs, x, square.id, is_in_vehicle ); if( is_filtered( *it.items.front() ) ) { continue; }
```

**Fix:** Run `is_filtered` before construction, pass the computed name to `has_rule` (or skip it when there are no rules), use `emplace_back`/`std::move`, and bucket `item_list_to_stack` by itype as `i_stacked` does.

### UIINV-P05 · Every redraw rescans for parent/child highlights; each mouse move forces a second full redraw
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/inventory_ui.cpp:3256`, `src/inventory_ui.cpp:4344`, `src/inventory_ui.cpp:4374`, `src/inventory_ui.cpp:3952`, `src/inventory_ui.cpp:3975`, `src/inventory_ui.cpp:4284`, `src/inventory_ui.cpp:2414`

`refresh_window` calls `highlight()` on every redraw (INVENTORY_HIGHLIGHT defaults to "symbol"). It scans every entry and copies `parent_item()` once per location of the selected stack. On every SDL motion event, `MOUSE_MOVE` calls `highlight(loc)`, a linear search that returns true whenever an entry is found, even if nothing changed. It then calls `ui_manager::redraw()` on top of the loop's own redraw, so each motion event costs two full redraws, stats included.

```cpp
} else if( input.action == "MOUSE_MOVE" ) {
    if( !dragActive && highlight( input.entry->any_item() ) ) { ui_manager::redraw(); }
```

**Fix:** Cache the highlighted entry and recompute the flags only when it changes, using an `unordered_set<const item*>` of selected locations. Have `highlight(loc)` report whether anything actually changed, and use `input.entry` directly instead of searching.

### UIINV-P06 · AIM sorter recomputes expensive keys (average_dps, price, spoilage, ammo name) inside the comparator
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/advanced_inv.cpp:566`, `src/advanced_inv.cpp:608`, `src/advanced_inv.cpp:616`, `src/advanced_inv.cpp:650`, `src/advanced_inv.cpp:655`, `src/advanced_inv.cpp:687`, `src/advanced_inv.cpp:804`, `src/item_gun_tool_ammo.cpp:539`

Each comparison recomputes its keys. Damage sort calls `average_dps` twice, which constructs monster objects. Price sort makes up to 4 recursive `price(true)` calls, and spoilage sort makes 4 `spoilage_sort_order()` calls. Ties copy both name strings into a tuple for a locale compare. This runs on every `recalc_pane`, including each filter keystroke.

```cpp
const double dam1 = d1.items.front()->average_dps( get_player_character() );
if( d1.items.front()->price( true ) != d2.items.front()->price( true ) ) { return d1.items.front()->price( true ) > ...
```

**Fix:** Compute the active sort key once per listitem in `recalc_pane` and sort on it. Use `std::tie` for the name tie-break.

### UIINV-P07 · AIM rebuilds the whole pane on every filter keystroke
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/advanced_inv.cpp:1911`, `src/advanced_inv_pane.cpp:480`, `src/advanced_inv.cpp:823`, `src/advanced_inv.cpp:741`, `src/advanced_inv.cpp:804`, `src/advanced_inv_pane.cpp:285`, `src/advanced_inv_listitem.cpp:27`

In the filter popup, each changed keystroke calls `set_filter`, which sets `recalc = true`. The next redraw then re-enumerates all tiles (9 in `AIM_ALL`), re-stacks, rebuilds listitems (P04) and re-sorts (P06), although no item has changed. This is the familiar "AIM lags while filtering" with thousands of stacks.

```cpp
filter = new_filter; filter_function = item_filter_from_string( filter ); recalc = true;
```

**Fix:** Keep an unfiltered, sorted `all_items` cache per pane, rebuilt only when items or the area change. Have `set_filter` set a `refilter` flag that runs a cheap pass over cached names.

### UIINV-P08 · `inventory_column::add_entry` stacks by linear scan, so menu construction is O(n²) per container or tile
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/inventory_ui.cpp:1364`, `src/inventory_ui.cpp:1367`, `src/inventory_ui.cpp:1376`, `src/inventory_ui.cpp:267`, `src/inventory_ui.cpp:2355`, `src/inventory_ui.cpp:2133`, `src/inventory_ui.cpp:1921`

Each new entry runs `std::find` over the column, then a `find_if` that copies `item_location`s and calls `where`/`pos_abs`/`parent_item` before `display_stacked_with` for every existing entry. All items of a tile, or all children of a container, share one column, so building a menu over a 1,000-item shelf costs about 500k pair checks. `selection_column::on_change` repeats the pattern on every toggle.

```cpp
if( auto it = std::find( dest.begin(), dest.end(), entry ); it != dest.end() )
auto entry_with_loc = std::find_if( dest.begin(), dest.end(), [...]( const inventory_entry & e ) { ...
```

**Fix:** Keep a transient `unordered_map<stack_key, vector<size_t>>` per column while it is filled (category, parent item*, where, typeId), and compare only within a bucket. Look up selection entries by `item*`.

### UIINV-P09 · Multiselect toggling (select-all, pickup/drop) is quadratic to cubic, with per-pair list allocations
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/inventory_ui.cpp:4429`, `src/inventory_ui.cpp:4445`, `src/inventory_ui.cpp:4551`, `src/inventory_ui.cpp:4564`, `src/inventory_ui.cpp:4711`, `src/inventory_ui.cpp:4728`, `src/inventory_ui.cpp:1921`

`set_chosen_count` scans `to_use` with `vector::erase` for every location, then scans it again. `toggle_entries` calls it for each entry. For drop, pickup and haul (`allow_select_contained` defaults to false), `deselect_contained_items` then calls `all_items_ptr()`, which allocates a `std::list`, once per (container, column, selected entry) triple. With 50 selected containers and 500 entries, that is about 25k list allocations per keypress.

```cpp
for( const item_location &loc : entry.locations ) { for( auto iter = to_use.begin(); iter != to_use.end(); ) {
    if( iter->first == loc ) { iter = to_use.erase( iter ); ...
```

**Fix:** Keep an `unordered_map<const item*, size_t>` index into `to_use`. In `deselect_contained_items`, build one `unordered_set` of all selected containers' contents, then make a single pass over the selected entries.

### UIINV-P10 · Inventory sort comparator allocates on every comparison and calls the locale comparison twice
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/inventory_ui.cpp:1582`, `src/inventory_ui.cpp:1584`, `src/inventory_ui.cpp:1441`, `src/inventory_ui.cpp:1457`, `src/inventory_ui.cpp:1480`, `src/inventory_ui.cpp:763`, `src/game_inventory.cpp:857`, `src/inventory_ui.cpp:1608`, `src/localized_comparator.h:28`, `src/localized_comparator.cpp:46`

`prepare_paging`'s `stable_sort` compares categories by value, which includes translated names. In hierarchy mode, every comparison builds two `path_to_top` vectors (calling `is_shown` per ancestor) and two temporary `inventory_entry` objects with cache lookups. Collation adds two more. `sort_compare`'s `make_tuple` copies strings, and the tuple `localized_compare` calls strcoll twice on the first element (on Windows it also converts the strings to wide strings). The comestible preset recomputes rot and trait checks. Headers and page breaks are added with repeated `vector::insert`. This runs on open, filter confirm and toggles, not on every redraw.

```cpp
parent_path_t const path_lhs = path_to_top( lhs, preset ); parent_path_t const path_rhs = path_to_top( rhs, preset );
inventory_entry ep_lhs( { p_lhs }, nullptr, true, 0, lhs.generation ); ... ep_lhs.update_cache();
```

**Fix:** Compute a key per entry once (category pointer or rank, ancestor keys, flags, a cached `collate::transform` key), then sort indices with plain comparisons. Compare category pointers first, use `std::tie`, and build paged entries in a single pass.

### UIINV-P11 · Inventory header stats are recomputed 4× per redraw, each walking the pocket tree 3×
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/inventory_ui.cpp:2675`, `src/inventory_ui.cpp:2716`, `src/inventory_ui.cpp:2721`, `src/inventory_ui.cpp:3389`, `src/inventory_ui.cpp:3363`, `src/inventory_ui.cpp:3115`, `src/inventory_ui.cpp:3252`, `src/inventory_ui.cpp:4933`, `src/inventory_ui.cpp:4957`, `src/inventory_ui.cpp:5180`

One `refresh_window()` reaches `get_stats()` four times (frame, header twice, columns), and `prepare_layout` adds 2-3 more. Each call does three recursive pocket walks. Drop, pickup and insert also copy an `item` for each selected count-by-charges stack. This curses UI redraws only on input, and `weight_carried` is cached, so the cost is about a millisecond per keypress.

```cpp
u.free_space(...); u.volume_capacity_recursive(...); u.get_all_pockets_with_constraints(...);  // 3115-3119
item copy( *loc.first.get_item() ); copy.charges = loc.second;                                 // 5180
```

**Fix:** Compute stats once per `refresh_window` into a cached member, cleared when `to_use` or items change. Compute charge-stack weight as `weight()*qty/charges` instead of copying the item.

### UIINV-P12 · Inventory name filter calls `tname()` per entry, in both paging passes
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/inventory_ui.cpp:781`, `src/inventory_ui.cpp:1566`, `src/inventory_ui.cpp:1577`, `src/inventory_ui.cpp:1579`, `src/item_search.cpp:210`, `src/inventory_ui.cpp:2626`

`get_filter` wraps `basic_item_filter`, whose name branch calls `remove_color_tags( a.tname() )`. `prepare_paging` applies it in two `move_if` passes, and a narrowing filter that changes the column height reruns paging in the second layout pass. An empty filter costs nothing. The filter only runs on confirm, so this is a one-off hitch.

```cpp
return [filter]( const item & a ) { return lcmatch( remove_color_tags( a.tname() ), filter ); };
```

**Fix:** For plain name terms, match against a cached name, but note that `cached_name_full` omits the favorite, durability and burn segments, so either add them or accept the slight change in what matches. Evaluate the filter once per entry.

### UIINV-P13 · `prepare_layout` runs the full layout pass twice
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/inventory_ui.cpp:2617`, `src/inventory_ui.cpp:2626`, `src/inventory_ui.cpp:2549`, `src/inventory_ui.cpp:1146`, `src/inventory_ui.cpp:2565`, `src/inventory_ui.cpp:1683`, `src/inventory_ui.cpp:1893`, `src/inventory_ui.cpp:1804`, `src/inventory_ui.cpp:1784`

Both passes run `reset_width` over every entry (the selection column also iterates every other column) and `reassign_custom_invlets` (see P03). When the list is shorter than the terminal, `set_height` invalidates paging, so filtering and sorting run twice. Per-entry option lookups and cell string copies are minor. This only runs on resize-type events.

```cpp
prepare_layout( TERMX - nc_width, TERMY - nc_height );          // 2617
prepare_layout( win_width - nc_width, win_height - nc_height ); // 2626
```

**Fix:** Make the first pass measure-only (no invlets, no height change). Hoist option lookups, and take cell text by const reference.

### UIINV-P14 · AIM `print_items` recomputes display names, ownership and colors for every visible row on each redraw
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/advanced_inv.cpp:440`, `src/advanced_inv.cpp:442`, `src/advanced_inv.cpp:469`, `src/advanced_inv.cpp:488`, `src/advanced_inv.cpp:490`, `src/advanced_inv.cpp:493`, `src/advanced_inv.cpp:549`, `src/advanced_inv.cpp:550`, `src/advanced_inv.cpp:332`, `src/advanced_inv.cpp:820`, `src/advanced_inv.cpp:2069`

Both panes redraw on every keypress. For each visible row, `print_items` calls `display_name(1, true)`, `is_owned_by` and an option lookup, plus a fresh `tname()` for compact auto-pickup rows. In the active pane it also calls `color_in_inventory`, which for guns and ammo walks the inventory and for food calls `will_eat`. The header calls `volume_capacity()` twice, and every redraw builds a new `input_context`.

```cpp
item_name = it.display_name( 1, true );             // 490
compact ? it.tname().substr( 0, 1 ) : ">"            // 550
```

**Fix:** Cache the display string and base color in `advanced_inv_listitem` when it is built. Hoist the option lookup, call `volume_capacity()` once, and reuse a member `input_context`.

## Appendix: considered and rejected

- AIM forces a full rebuild of both panes on every redraw while the item action menu is open – AIM is a curses UI that is repainted only when invalidated. While the item menu is open, it is repainted only after sub-UIs close, which is exactly when items may have moved. The forced rebuild is an intentional guard against dangling `item_location`s, and it runs rarely.
- Category-selection mode does an O(n) `std::find` per drawn entry (`page_of`) on every redraw – it only runs after cheap short-circuits, only in the rarely used category mode and only for on-screen rows. The comparisons are pointer compares, so the cost is negligible.
- Item compare window recomputes `tname()` 4× per frame at about 100 Hz – accurate, but each frame already reruns `display_item_info` and a full ImGui render, which cost far more. There is no visible latency. If anything, the shared 100 Hz ImGui polling loop is the systemic issue.
