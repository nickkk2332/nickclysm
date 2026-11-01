# 25 · Crafting, recipes & requirements

> Engine audit – static code reading at commit `a1a646d6`; nothing was compiled or run. Each finding was checked against the code by a separate reviewer: **confirmed** = verified by reading the code; **plausible** = consistent with the code, but reachability or impact not proven. Line numbers refer to that commit.

**Scope:** `src/crafting.cpp`, `src/craft_command.cpp`, `src/craft_reservation.cpp`, `src/craft_reservation_search.cpp`, `src/recipe.cpp`, `src/recipe_dictionary.cpp`, `src/recipe_groups.cpp`, `src/requirements.cpp`, `src/temp_crafting_inventory.cpp`, `src/butchery_requirements.cpp`, `src/character_crafting.cpp`

## Summary

Crafting works for the common case, but the failure math has a real defect: `crafting_failure_roll()` never subtracts difficulty, so failures almost never destroy components or progress, while the UI shows a much higher chance. Disassembly is the weakest area. Reversible step recipes ignore `reversible.time`, quality substitutions are merged into OR groups, adjustments to recovered components are thrown away, and the substitution table and book-to-paper rule are hardcoded. On the data side, recipe `copy-from` silently drops `extend` blocks that vanilla data already uses, and EOCs get almost no crafting hooks or context. Performance problems come from work repeated far more often than needed: the "wait" mode on unattended steps runs the reservation pipeline every turn instead of every minute, the ImGui crafting menu rebuilds map-wide cost contexts every frame, and inventory and reservation building do deep copies and quadratic scans.

| Lens | Critical | High | Medium | Low | Total |
|---|---|---|---|---|---|
| Missing JSON / EOC | 0 | 2 | 6 | 8 | 16 |
| Bugs & quirks | 0 | 1 | 6 | 8 | 15 |
| Performance | 0 | 2 | 6 | 5 | 13 |
| **Total** | 0 | 5 | 18 | 21 | 44 |

**Fix first:**
1. **CRAFT-B01** · Craft failure roll ignores difficulty: a one-line fix that restores the intended failure penalties and makes the displayed chance match the real outcome.
2. **CRAFT-P01** · "Wait" mode on an unattended step runs the full env and reservation pipeline every turn: a due-time gate removes about 60x the work during multi-hour waits, and "wait" is the default choice.
3. **CRAFT-P02** · Crafting menu redraws recompute book bonuses and tool speeds about 100 times a second: memoizing the cost context removes map-wide scans from an idle menu.
4. **CRAFT-J01** · Recipe `copy-from` silently drops `extend` proficiencies: at least 8 shipped recipes already lose proficiencies they meant to add.
5. **CRAFT-J04** · `butchery_requirements::check_consistency()` is never called: a short JSON array loads cleanly and then crashes the game at butcher time; registering the check is trivial.

## 1. Missing JSON bindings & EOC features

### CRAFT-J01 · Recipe `copy-from` cannot extend/delete proficiencies or requirements; vanilla `extend` blocks are silently dropped
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/recipe.cpp:433`, `src/recipe.cpp:503`, `src/recipe.cpp:635`, `src/recipe.cpp:685`, `src/requirements.cpp:503`, `src/generic_factory.h:1592`, `data/json/recipes/other/parts.json:341`, `data/json/recipes/armor/storage.json:56`, `data/json/recipes/armor/storage.json:797`, `data/json/recipes/armor/pets_cow.json:142`

Recipe proficiencies are loaded with a plain `jo.read("proficiencies", ...)`, so `"extend": { "proficiencies": [...] }` is never read. The only code that opens the `extend` object is the generic reader for `using`, and it calls `allow_omitted_members()`, so the unknown key raises no error. At least 8 shipped recipes (5 vanilla, 3 DinoMod) rely on this and silently lose the proficiencies they meant to add: `qt_sheet_metal_small` never requires `prof_tempering`, `ammo_satchel_leather` and `chestpouch_leather` never get the leatherworking proficiencies, and the same applies to the pets_cow/horse leatherbone armour and DinoMod pets_bear/elephant/ostrich. Inline components, tools and qualities cannot be extended at all: `reqs_internal` is cleared on every load and `load_requirement` is called with `check_extend=false`. A mod can restate the whole `components` list after `copy-from`, but cannot append one alternative.

```cpp
// recipe.cpp:433
jo.read( "proficiencies", proficiencies );
// recipe.cpp:502-503
// inline requirements are always replaced (cannot be inherited)
reqs_internal.clear();
// recipe.cpp:635 (check_extend=false)
requirement_data::load_requirement( jo, req_id, false, abstract && !effectively_step_recipe );
// generic_factory.h:1592-1595
JsonObject tmp = jo.get_object( "extend" ); tmp.allow_omitted_members();
```

**Fix:** Load proficiencies through a `generic_typed_reader`, or give `recipe_proficiency` `handle_extend`/`handle_delete` keyed by proficiency id. For requirements, keep the parent's inline `requirement_data` on `copy-from` and support group-append semantics:

```json
"extend": { "proficiencies": [ { "proficiency": "prof_tempering" } ], "components": [ [ [ "mod_glue", 1 ] ] ] },
"delete": { "components": [ "duct_tape" ] }
```

(`delete` removes that itype from every group.) Until that exists, make `warn_disabled_feature( jo, "extend", "proficiencies" / "components" / "tools" / "qualities" )` throw so the mistake is no longer silent.

### CRAFT-J02 · Crafting/disassembly has almost no EOC hooks: `result_eocs` get no item or context; no start/fail/disassemble/learn events
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/crafting.cpp:3797`, `src/crafting.cpp:3798`, `src/crafting.cpp:3800`, `src/crafting.cpp:3742`, `src/crafting.cpp:3508`, `src/crafting.cpp:5513`, `src/crafting.cpp:2538`, `src/recipe.cpp:259`, `src/character_crafting.cpp:36`, `src/event.h:331`, `doc/JSON/EFFECT_ON_CONDITION.md:86`

`result_eocs` is the only crafting hook. It runs with only the crafter as talker: beta is null, no context variables are set (recipe id, batch size, result item, defect state), and the same dialogue is simply run `batch_size` times. It runs after `spawn_items`, so an EOC cannot tag, rename, enchant or inspect the item it just made, unlike `use_action` or COMESTIBLE hooks, which pass the item as beta. Disassembly runs no EOC at all, even though `result_eocs` is parsed before the type switch, so it loads on `uncraft` recipes and never fires. Craft start, the per-tick failure in `handle_craft_failure`, component destruction and recipe learning (`learn_recipe`, memorising from a reference, `decomp_learn`) emit nothing. The event bus has no craft events; `character_finished_activity` carries only an activity id, and crafts finalised by the wakeup system (`finalize_passive_craft`) bypass the activity, so that event does not fire for them. The documented behaviour (beta = NONE) matches the code, so this is a modding gap rather than a regression. A partial workaround is `u_run_inv_eocs` with an id search, which cannot single out the new item or see recipe/batch context.

```cpp
// crafting.cpp:3797-3802
for( const effect_on_condition_id &eoc : making.result_eocs ) {
    dialogue d( get_talker_for( *this ), nullptr );
    for( int i = 0; i < batch_size; i++ ) {
        eoc->activate_activation_only( d, "a recipe", "crafting", "recipe" );
    }
}
```

**Fix:**
1. In `complete_craft`, run `result_eocs` once per spawned result with an item talker as beta, before the items are placed, and set context vars `_recipe_id`, `_batch_size`, `_batch_index`, `_result_id`, `_faulty`. For result-less recipes keep a single run with `_batch_size` in context.
2. Add recipe fields `"start_eocs"`, `"fail_eocs"` (context `_destroyed_component`, `_progress_lost`) and `"disassembly_eocs"` (beta = the item being disassembled, context `_recovered` / `_destroyed`).
3. Add event_bus events `character_crafts_item {character, recipe, itype, batch}`, `character_disassembles_item {character, itype}`, `character_learns_recipe {character, recipe, source: book|craft|disassembly|eoc}` and `craft_failed {character, recipe}`.

Use cases: a cursed-forge mod that adds an enchantment var to every blade made on its anvil; an achievement for disassembling 100 electronics.

### CRAFT-J03 · Auto book disassembly is hardcoded to paper and ignores material (a 4 kg stone tablet yields ~1333 sheets)
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/recipe_dictionary.cpp:818`, `src/recipe_dictionary.cpp:819`, `src/recipe_dictionary.cpp:826`, `src/item.cpp:3313`, `data/mods/Magiclysm/items/spellbooks.json:719`, `data/json/items/book/spiritual.json:2005`

Every item with a book slot, weight > 0 and no explicit uncraft gets a generated disassembly of `weight / paper.weight` units of `uncraft_book` (paper), with a fixed time of `pages * 10` moves, no skill and no tools. Material is not checked, and since `is_disassemblable` only checks for an uncraft recipe there is no opt-out except a dummy uncraft recipe. Magiclysm's stone "Runic Tablet shard" (4000 g) gives about 1333 paper; the vanilla 12 kg leather/wood `holybook_torah_scroll` gives about 4000; Xedra's stone kin tablets also become paper. A scan finds 9 BOOK items with no paper/cardboard material. The code's own TODO says time cannot be specified.

```cpp
// recipe_dictionary.cpp:818-826
if( e->book && !recipe_dict.uncraft.count( rid ) && e->weight > 0_gram ) {
    const int pages = e->weight / itype_paper->weight;
    ...
    bk.requirements_ = *requirement_data_uncraft_book * pages;
    // TODO: allow specifying time in requirement_data
    bk.time = pages * 10;
```

**Fix:** Only auto-generate when the item is mostly paper or cardboard, or derive the yield from material `salvaged_into`. Add a book-slot field and an opt-out flag:

```json
"disassembly": { "requirement": "uncraft_book", "per": "3 g", "time_per_unit": "1 s" },
"flags": [ "NO_UNCRAFT" ]
```

### CRAFT-J04 · `butchery_requirements::check_consistency()` is never called, so a short JSON array crashes at butcher time
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/butchery_requirements.cpp:92`, `src/butchery_requirements.cpp:84`, `src/butchery_requirements.cpp:116`, `src/butchery_requirements.cpp:123`, `src/init.cpp:827`, `src/harvest.cpp:207`

`check_consistency()`, which verifies one entry per creature size and butcher type, has no callers in `src/` or `tests/`; `init.cpp` registers only `finalize_all`. Load only warns about too many sizes, not too few, and requirement ids are never checked with `is_valid()`. A mod that supplies fewer size arrays than `creature_size::num_sizes - 1` loads cleanly, and `get_fastest_requirements()` (from `butchery.cpp:1197` and `activity_actor.cpp:13826`) then throws `std::out_of_range` from `.at( size )` the first time a corpse of a missing size is butchered. Nothing catches it around `do_turn`, so the game crashes. A second crash path: `harvest.cpp:207` never validates the `butchery_requirements` id, and an invalid id returns the factory dummy with an empty map, whose `rbegin()` is then dereferenced (lines 122-123). The `butchery_requirement` type has no doc/JSON page.

```cpp
// init.cpp:827 – only finalize is registered
{ _( "Butchery Requirements" ), &butchery_requirements::finalize_all }
// butchery_requirements.cpp:116
riter.second.at( size ).at( butcher )->can_make_with_inventory(
```

**Fix:** Register `check_consistency` in the check list in `init.cpp`, add `is_valid()` checks for the requirement ids and for the id referenced from harvest, and replace `.at()` with `find()` plus a debugmsg fallback. Document the format (multiplier key → array of per-size objects keyed by butcher type) in doc/JSON.

### CRAFT-J05 · Missing recipe validation: unchecked flag strings, arbitrary choice between multiple reversible recipes, uncraft alternatives silently truncated
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/recipe.cpp:371`, `src/recipe.cpp:249`, `src/recipe.cpp:1104`, `src/recipe.cpp:443`, `src/recipe.cpp:468`, `src/recipe_dictionary.cpp:800`, `src/string_id.h:234`, `src/crafting.cpp:5550`, `doc/JSON/ITEM_CRAFT_AND_DISASSEMBLY.md:746`

(a) Recipe flags are a free `std::set<std::string>` compared by string in about 10 places; neither `get_consistency_error` nor `recipe_dictionary::check_consistency` validates them, so `BLIND_EAZY` or `NO_BENCHE` does nothing. `result_eocs`, autolearn/`decomp_learn` skill ids and `byproduct_group` are not checked either. (b) When several reversible recipes share a result (19 items in data/json, e.g. grapnel/grapnel_from_scratch, sling/sling_cord, reloaded_762_51/from_308) and there is no explicit uncraft, the first in `recipe_id` order wins. That order is the string-intern index, effectively load order, and `string_id.h` warns it is not stable. No warning is given. (c) Uncraft recipes use only `altercomps.front()` for items without stored components, and `proficiencies`/`skills_required` on uncraft recipes load but have no effect; the docs list these pitfalls but nothing checks them. (d) `reversible` with `result_mult > 1` duplicates resources (documented) but is not checked.

```cpp
// recipe_dictionary.cpp:800-802
const string_id<recipe> uncraft_id = recipe_id( r.result().str() );
if( r.is_reversible() && !recipe_dict.uncraft.count( uncraft_id ) ) {
    recipe_dict.uncraft[ uncraft_id ] = r;
// crafting.cpp:5550
const item_comp &comp = altercomps.front();
```

**Fix:** Add a recipe flag registry (or at least a known-flag list) and debugmsg on unknown flags; validate the other ids. In finalize, warn when more than one reversible recipe maps to the same result without an explicit uncraft, and allow `"reversible": { "primary": true }` to choose. For `uncraft` recipes, error on component groups with more than one alternative and on `proficiencies`/`skills_required`. Error on reversible + `result_mult > 1` unless `"allow_mult_reversible": true`.

### CRAFT-J06 · No recipe availability conditions; `u_know_recipe` is hardwired to the avatar with no `npc_` form
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/recipe.cpp:254`, `src/condition.cpp:2041`, `src/condition.cpp:2047`, `src/condition.cpp:2644`, `src/talker_character.cpp:300`, `src/character_crafting.cpp:82`

`recipe::load` reads no `condition`, so crafting cannot be gated on game state (a variable, time of day, weather, an effect). Autolearn is a skill-level map only, cached on skill levels, so a recipe cannot unlock on a proficiency, trait or EOC condition. On the EOC side, `u_know_recipe` ignores its talker and always checks `get_player_character()`, even though `talker_character_const::has_recipe` exists (and is used in `npctalk.cpp:7387`). There is no `npc_know_recipe`, no math for "can craft X now", success chance or known-recipe count, and no effect that starts a craft. Unlock-style gating can already be done with `u_learn_recipe`/`u_forget_recipe`; state-gated crafting (e.g. only at full moon) cannot.

```cpp
// condition.cpp:2045-2047
// should be a talker function but recipes aren't in Character:: yet
return get_player_character().knows_recipe( &rep );
// condition.cpp:2644 – no npc_ variant
{"u_know_recipe", jarg::member, &conditional_fun::f_u_know_recipe },
```

**Fix:** Add a recipe `"condition"` evaluated with the crafter as `u_` in `can_make`/`can_start_craft` and the GUI filter, plus optional `"condition_fail_message"`. Extend autolearn:

```json
"autolearn": { "skills": [ [ "tailor", 3 ] ], "proficiencies": [ "prof_leatherworking" ], "condition": { "u_has_var": "schematic_found" } }
```

Make `u_know_recipe` use `d.const_actor( is_npc )->has_recipe()` and register `npc_know_recipe`. Add math `u_can_craft('id')`, `u_recipe_success_chance('id')`, `u_known_recipes_count('category')` and an effect `"u_start_craft": { "recipe": "...", "batch": 1 }` for NPC dialogue.

### CRAFT-J07 · Disassembly tool/quality substitution table is hardcoded (TODO: "Allow jsonizing those tool replacements")
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/requirements.cpp:1334`, `src/requirements.cpp:1337`, `src/requirements.cpp:1350`, `src/requirements.cpp:1361`, `src/requirements.cpp:1367`, `src/requirements.cpp:1391`, `src/requirements.cpp:1416`, `src/requirements.cpp:1419`, `src/recipe.cpp:620`, `src/recipe.cpp:924`, `data/json/recipes/ammo/shot.json:17`, `data/json/requirements/toolsets.json:25`, `data/json/uncraft/recipe_deconstruction.json:3131`

`disassembly_requirements()` rewrites craft tools by item and quality id: welder, crude welder, oxy-torch, forge and charcoal forge become SAW_M_FINE 1; crucible is dropped; press becomes PULL 1 and also drops fire; SEW becomes CUT; GLARE and KNIT are dropped. A mod that adds its own forge, arc welder, loom or knitting quality gets nonsensical disassembly requirements unless it writes a full uncraft recipe. Explicit `uncraft` recipes are also forced reversible (`recipe.cpp:620`) and get rewritten, so `hd_steel_drum`'s welder requirement silently becomes SAW_M_FINE. The press/fire rule depends on group order: `remove_fire` is only set if the press group is visited first, but finalize adds `using` before the recipe's own tools, and `shot_forming` lists fire before `press`, so reversible reloaded shot still needs fire/hotplate and a ladle to disassemble. Deduplication uses `std::unique`, which only removes adjacent duplicates. See also CRAFT-B04 for how the substituted qualities are merged.

```cpp
// requirements.cpp:1336-1337
// TODO:
// Allow jsonizing those tool replacements
// requirements.cpp:1350-1352
if( type == itype_welder || type == itype_welder_crude || type == itype_oxy_torch ||
    type == itype_forge || type == itype_char_forge ) {
    new_qualities.emplace_back( qual_SAW_M_FINE, 1, 1 );
```

**Fix:** Move the table to data, then do the fire removal in a second pass and sort before `unique()`:

```json
// on a tool item
"disassembly_substitute": { "qualities": [ { "id": "SAW_M_FINE", "level": 1 } ] }
"disassembly_substitute": "none"
// on a tool_quality
"disassembly_substitute": { "id": "CUT", "level": "same" }
"drop_on_disassembly": true
```

### CRAFT-J08 · Terrain cannot provide crafting pseudo-tools; tree and brick-oven hacks are duplicated in two code paths
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/temp_crafting_inventory.cpp:365`, `src/temp_crafting_inventory.cpp:369`, `src/temp_crafting_inventory.cpp:427`, `src/temp_crafting_inventory.cpp:438`, `src/crafting.cpp:1518`, `src/crafting.cpp:1602`, `src/mapdata.h:773`, `src/mapdata.cpp:1627`

`crafting_pseudo_item` exists only on `furn_t`, and only as a single item. Terrain tools are special-cased: `TFLAG_TREE` gives `butchery_tree_pseudo` and `t_brick_oven` (by id) gives `brick_oven_pseudo`. Both hacks are copied into the reservation provider enumeration (`crafting.cpp:1518`, "Terrain has no pseudo item field"), so any fix must touch both. Modders cannot add a terrain anvil, a rock outcrop that works as a hammer, or a natural oven, and furniture cannot offer two tools (a counter with a cutting board and a sink). The reported faucet/keg limits are weaker than first stated: any vpart can list `water_faucet` in `pseudo_tools`, and modded furniture can use the `keg` examine action.

```cpp
// temp_crafting_inventory.cpp:365-372
// a temporary hack while trees are terrain
// Another terrible hack, as terrain can't provide pseudo items
ter_id brick_oven( "t_brick_oven" );
// mapdata.h:773 (furn_t only)
itype_id crafting_pseudo_item;
```

**Fix:** Move the field to `map_data_common_t` as a list, keep the singular form as a legacy alias, and have both enumerators read it; give trees and `t_brick_oven` the field in JSON:

```json
"crafting_pseudo_items": [ "anvil", "butchery_tree_pseudo" ]
```

Optionally replace the faucet/keg special cases with a `CRAFT_LIQUID_SOURCE` vpart/furniture flag.

### CRAFT-J09 · Hot results are decided by a hardcoded hotplate/atomic_coffeepot tool check
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/recipe.cpp:2149`, `src/recipe.cpp:2166`, `src/recipe.cpp:2171`, `src/recipe.cpp:1917`, `src/recipe.cpp:1221`, `src/recipe.cpp:1255`, `src/crafting.cpp:3726`, `src/crafting.cpp:4214`, `src/consumption.cpp:410`, `src/iuse.cpp:7565`

`recipe::hot_result()` returns true only if `hotplate` or `atomic_coffeepot` appears in the flattened tool list (marked "TODO: Make this less of a hack"). It controls spawn heating, the frozen-component filter, RAW/kcal handling, `is_cooked` component splitting and the COOKED flag in nutrient prediction. Vanilla works because `surface_heat` and `water_boiling_heat` include hotplate. A recipe using its own heat source (magic flame, solar cooker, a quality-based requirement) yields room-temperature food and refuses frozen ingredients; a mod that blacklists hotplate makes every cooked recipe produce cold food. Each call also constructs `item( result_ )` and rescans the tools.

```cpp
// recipe.cpp:2166-2172
// TODO: Make this less of a hack
if( ( t.type == itype_hotplate ) || ( t.type == itype_atomic_coffeepot ) ) {
    return true;
```

**Fix:** Add an explicit recipe (and optionally step-level) override, e.g. `"hot_result": true` or flags `HOT_RESULT` / `COLD_RESULT`, defaulting from a tool flag or quality such as `HEATS_CRAFT` instead of item ids. Cache the result in `finalize()`.

### CRAFT-J10 · BURROW/BURROWLARGE hardcoded to give pseudo pickaxe and shovel; mutations cannot grant crafting tools
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/crafting.cpp:736`, `src/character_inventory.cpp:1044`, `src/character_inventory.cpp:1091`, `src/mutation.h:233`

Three places check `trait_BURROW`/`trait_BURROWLARGE` and inject `pickaxe` and `shovel` into the crafting inventory or the intrinsic quality list (more BURROW hardcodes exist at `iexamine.cpp:1581` and `mutation.cpp:943`). Bionics have `passive_pseudo_items`/`toggled_pseudo_items`, and mutations have `provided_qualities`, but mutations cannot provide tool items. Only one vanilla requirement (`pickaxe_list`) names these tools by id, so most of this could already be `provided_qualities`; the gap is for content that needs a specific tool item.

```cpp
// crafting.cpp:736-738
if( has_trait( trait_BURROW ) || has_trait( trait_BURROWLARGE ) ) {
    crafting_cache.crafting_inventory->add_pseudo_item( itype_pickaxe );
    crafting_cache.crafting_inventory->add_pseudo_item( itype_shovel );
```

**Fix:** Add a mutation field mirroring bionics, consume it in `crafting_pseudo_items()` and `intrinsic_quality_sources()`, move BURROW to JSON and delete the special cases:

```json
"crafting_pseudo_items": [ "pickaxe", "shovel" ]
```

### CRAFT-J11 · CRAFTING_SPEED_MULTIPLIER `add` and the NO_ENCHANTMENT flag apply in the time estimate but not the real craft
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/crafting.cpp:267`, `src/crafting.cpp:354`, `src/crafting.cpp:381`, `src/crafting.cpp:512`, `src/magic_enchantment.cpp:1267`

The estimate path, `crafting_speed_multiplier(recipe)` (used by `expected_time_to_craft`), calls `modify_value`, which applies both add and multiply, and ignores the recipe flag NO_ENCHANTMENT. The real progress path uses `mut_crafting_speed_multiplier`, which honours NO_ENCHANTMENT but uses only `get_value_multiply`. An additive CRAFTING_SPEED_MULTIPLIER shows a shorter estimate and then does nothing during the craft. No current data uses `add` (MAGIC.md recommends multiply), so this is a latent trap.

```cpp
// crafting.cpp:269 (real progress)
return rec.has_flag( flag_NO_ENCHANTMENT ) ? 1.0f : 1.0 +
       enchantment_cache->get_value_multiply( enchant_vals::mod::CRAFTING_SPEED_MULTIPLIER );
// crafting.cpp:354 (estimate, no NO_ENCHANTMENT check)
enchantment_cache->modify_value( enchant_vals::mod::CRAFTING_SPEED_MULTIPLIER, crafting_speed );
```

**Fix:** Route both paths through one helper that applies `modify_value( CRAFTING_SPEED_MULTIPLIER, 1.0 )` unless the recipe has NO_ENCHANTMENT, and apply NO_BENCH and the workbench factor consistently. Document in MAGIC.md that `add` is valid.

### CRAFT-J12 · Docs drift: documented recipe fields that are not loaded, wrong doc paths, contradictory disassembly rules
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `doc/JSON/ITEM_CRAFT_AND_DISASSEMBLY.md:40`, `doc/JSON/ITEM_CRAFT_AND_DISASSEMBLY.md:99`, `doc/JSON/ITEM_CRAFT_AND_DISASSEMBLY.md:123`, `doc/JSON/ITEM_CRAFT_AND_DISASSEMBLY.md:808`, `src/requirements.cpp:1825`, `src/recipe.cpp:433`

The recipe field reference documents `"override"`, `"on_display"` and `"component_blacklist"`, but no recipe loader reads them (`on_display` exists only in `construction.cpp`), so using them as documented raises an "Invalid or misplaced field" error. The replacement NO_RECOVER marker is documented at line 469 but not in the field block. The dedup-overflow debugmsg points to "the Recipe section in doc/JSON/JSON_INFO.md", which does not exist; the section is in ITEM_CRAFT_AND_DISASSEMBLY.md. "Closing words" item 3 (line 808) says a reversible recipe and an uncraft for the same item cannot coexist, contradicting the section at line 768 and the code. The silent loss of `extend` on proficiencies (CRAFT-J01) is not documented either.

```cpp
// requirements.cpp:1827-1828
"See the Recipe section in doc/JSON/JSON_INFO.md"
```

**Fix:** Remove the three dead fields, or implement `component_blacklist` as sugar for `[ "item", n, "NO_RECOVER" ]`. Point the debugmsg at `ITEM_CRAFT_AND_DISASSEMBLY.md#overlapping-recipe-component-requirements`. Rewrite closing-words item 3 and add a "copy-from support" table for recipe fields.

### CRAFT-J13 · `recipe::npc_can_craft` is a stub with GUI plumbing and no JSON control; recipe_subset custom difficulty is dead
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/recipe.cpp:1974`, `src/crafting.cpp:442`, `src/crafting.cpp:5858`, `src/crafting_gui_helpers.cpp:130`, `src/crafting_gui_helpers.cpp:476`, `src/crafting_gui.cpp:1235`, `src/recipe_dictionary.cpp:939`, `src/recipe_dictionary.cpp:945`, `src/recipe_dictionary.cpp:977`

`npc_can_craft` ignores its argument and returns true, yet it is wired into `Character::craft`, the crafting GUI (`npc_cannot_craft`, the reasons column) and `npc::do_npc_craft`. All those gates are dead, and nothing in JSON can make it false, so modders cannot mark recipes NPCs should not be ordered to make (practice recipes, psionic or vampire-only recipes). `crafting.cpp:442` also tests `this->is_npc()` instead of the selected crafter. Separately, `recipe_subset::include` stores a per-recipe custom difficulty, and its `else if` branch can overwrite a lower stored value with a higher one; `get_custom_difficulty` is only used inside `recipe_subset` to copy the value. The book's `skill_level` is still enforced as an availability gate in `item.cpp:3568`, so this second part is dead-code cleanup.

```cpp
// recipe.cpp:1974-1977
bool recipe::npc_can_craft( std::string & ) const { return true; }
// recipe_dictionary.cpp:939-946
if( iter != difficulties.end() && custom_difficulty < iter->second ) { ... }
else if( custom_difficulty < r->difficulty ) { difficulties[r] = custom_difficulty; } // can raise 2 -> 3
```

**Fix:** Add a recipe flag `NPC_CANNOT_CRAFT` and/or `"npc_condition": { ... }` evaluated with the NPC as `u_`, fill `reason` from `"npc_cannot_craft_message"`, and check it on the chosen crafter. Make `include()` only ever lower the stored difficulty (`else if` should require `iter == end`), and either use the custom difficulty or remove it.

### CRAFT-J14 · Small special cases: candle id in component unload, BOIL quality rule in several places, no HP craft cost
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/craft_command.cpp:444`, `src/requirements.cpp:78`, `src/crafting.cpp:719`, `src/item.cpp:2471`, `src/item.cpp:2503`, `src/item.cpp:2520`, `src/crafting.cpp:4921`, `src/recipe.h:82`

(a) `safe_to_unload_comp()` refuses to unload a component only when its id is `candle`, although the candle already has NO_UNLOAD; modded wax or lamp items get unloaded. (b) "BOIL only counts when the container is empty" is hardcoded by quality id in `requirements.cpp:78`, `crafting.cpp:719` and three places in `item.cpp`, so no other quality can share the rule (modding value is speculative). (c) `character_resources` supports mana, stamina and vitamins but not HP. Bionic power is already reachable through a USES_BIONIC_POWER pseudo-tool, so only HP cost is truly missing.

```cpp
// craft_command.cpp:443-444
// Candle wax from candles should be consumed with the candle
if( it.is_container_empty() || it.typeId() == itype_candle ) {
```

**Fix:** (a) Check `it.has_flag( flag_NO_UNLOAD )` instead of the id. (b) Add a tool_quality field `"requires_empty_container": true` and set it on BOIL. (c) Add `"hp": 5` (and optionally `"bionic_power": "50 kJ"`) to `character_resources`, reusing the spell energy plumbing.

### CRAFT-J15 · Craft success math is hardcoded: INT-only stat, HYPEROPIC penalty by skill id (GUI copy already drifted), INT_ALPHA morale
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/crafting.cpp:3291`, `src/crafting.cpp:3305`, `src/crafting.cpp:3314`, `src/crafting.cpp:3535`, `src/crafting.cpp:3553`, `src/crafting_gui.cpp:1735`, `doc/JSON/MUTATIONS.md:178`, `src/character_knowledge.cpp:168`, `src/magic_enchantment.cpp:589`

The success roll always adds INT/4 (TODO: attribute should be data-driven). Hyperopia gives -2 to electronics and -1 to tailoring by hardcoded skill id (comment: should be a JSON `vision_penalty`). `crafting_gui.cpp:1735` repeats the check but omits `effect_transition_contacts`, so the displayed modifier differs from the real roll. MUTATIONS.md says a `craft_skill_bonus` of 4 equals one skill level, but the code adds it 1:1. INT_ALPHA failure morale is hardcoded at two sites. (Enchantment `skills` modifiers already feed the roll via `get_skill_level`, so flat skill bonuses from items/bionics are possible.)

```cpp
// crafting.cpp:3309, 3317
total_skill_modifiers += skill_bonuses.second * 1.0f;   // MUTATIONS.md: 4 = one level
total_skill_modifiers += get_int() / 4.0f;
```

**Fix:** Add to skills.json `"craft_stat": { "int": 0.25 }` and `"vision_penalty": 2`, with a per-recipe override `"success_stats": { "dex": 0.25 }`. Share one helper between the GUI and the roll. Fix the MUTATIONS.md wording or the scaling. Move the INT_ALPHA reaction to an EOC on the proposed `craft_failed` event (CRAFT-J02). Optional enchant values: CRAFT_FAILURE_CHANCE_MULTIPLIER, DISASSEMBLY_RECOVERY_MULTIPLIER.

### CRAFT-J16 · Disassembly recovery, recipe memorisation, assistant and skill-cap tuning are hardcoded constants
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/crafting.cpp:5582`, `src/crafting.cpp:5586`, `src/crafting.cpp:5706`, `src/crafting.cpp:3772`, `src/recipe.cpp:105`, `src/recipe.cpp:221`, `src/recipe.cpp:1959`, `src/recipe.cpp:1969`, `src/character_crafting.cpp:182`

Balance knobs that total-conversion mods want are in C++: disassembly recovery `dice(2 + skill*4, 16 + INT)` vs `dice(difficulty, 24)` times 0.8^damage; `decomp_learn` at `one_in(4)` ("TODO: make this depend on intelligence"); memorising from a reference at `8000*diff^4 / (skill*int)`; assistant factors 0.75 / 0.60; XP skill cap `difficulty * 1.25`; learning from helpers at 80% of difficulty; fixed morale types and maxima in `morale_modifier`. No external option or game_balance entry covers them. Per-recipe opt-outs partly exist already (`never_learn` blocks memorising; `result_eocs` can add custom morale).

```cpp
// crafting.cpp:3772
const double time_to_learn = 1000 * 8 * std::pow( difficulty, 4 ) / learning_speed;
// recipe.cpp:220-223
total_time * .75 ... total_time * .60
```

**Fix:** Add a singleton `crafting_settings` JSON object holding these constants, ideally as math expressions with `u_` = crafter. Optional per-recipe fields:

```json
"skill_cap": 6, "decomp_learn_chance": 0.25, "assistant_time_factors": [ 0.75, 0.6 ],
"morale_modifier": { "type": "morale_fun_craft", "bonus": 5, "max": 50, "duration": "2 h", "decay_start": "1 h" }
```

## 2. Bugs, oversights & quirks

### CRAFT-B01 · Craft failure roll ignores difficulty, so failures almost never destroy components or progress
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/crafting.cpp:3412`, `src/crafting.cpp:3434`, `src/crafting.cpp:3442`, `src/crafting.cpp:3455`, `src/crafting.cpp:3521`, `src/crafting.cpp:3531`, `src/crafting.cpp:3546`, `src/crafting.cpp:3313`, `src/crafting_gui_helpers.cpp:360`, `src/crafting_gui.cpp:1876`

`crafting_failure_roll()` computes `recipe_failure_roll_data()` but only uses `final_difficulty` in a debug message; it returns the raw skill roll. `crafting_success_roll()` and `item_destruction_chance()` (the UI's "Catastrophic Failure Chance") do subtract difficulty. The raw roll is centred on the weighted skill average plus INT/4, about skill+2 for INT 8, so in `handle_craft_failure()` `x_in_y( success_roll, 1.0 )` is almost always true (no component lost) and the progress loss `rng_exponential(...) * (1.0 - success_roll)` is negative (no progress lost). With skill 2 or more the real component-loss rate is near 0 while the UI shows 9-30%; a skill-0 crafter on a difficulty-4 recipe loses a component about 17% of the time against a displayed ~50%. In practice the only effect of reaching a failure point is FAULT_ON_COMPLETION.

```cpp
// crafting.cpp:3436-3442
craft_roll_data data = recipe_failure_roll_data( making );
float craft_roll = std::max( normal_roll( data.center, data.stddev ), 0.0 );
...
return std::max( craft_roll, 0.0f );   // data.final_difficulty never subtracted
// compare crafting_success_roll, 3431:
return std::max( craft_roll - data.final_difficulty, 0.0f );
```

**Fix:** Return `std::max( craft_roll - data.final_difficulty, 0.0f )`, matching `item_destruction_chance`. Clamp the value passed to `x_in_y` to [0,1]. Add a regression test that a skill == difficulty craft loses components at roughly the displayed rate.

### CRAFT-B02 · Unattended step past max_time + grace silently deletes the craft and its components; step JSON hides typos
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/crafting.cpp:2522`, `src/crafting.cpp:2535`, `src/crafting.cpp:2723`, `src/crafting.cpp:2725`, `src/crafting.cpp:3050`, `src/crafting.cpp:3051`, `src/recipe.cpp:670`, `src/recipe.cpp:731`, `doc/JSON/ITEM_CRAFT_AND_DISASSEMBLY.md:295`

`craft_actualize_fail` releases claims, ends any wait activity, cancels wakeups and removes the craft item with no message, distraction, salvage, byproduct or EOC. Neither caller logs anything first. Destruction is documented; the silence is the bug. In the reality bubble, `ready_at` always fires first, so the real trigger is the craft's submap being unloaded past `fail_at`: on reload `map::reconcile_loaded_items` rebuilds wakeups and `ready_check` goes straight to fail. Unattended steps are the ones players walk away from (the planning modal offers "Do something else"), and vanilla recipes use `max_time` (e.g. boiling water, 30 m). Content also cannot express ruin outcomes such as over-proofed dough becoming ruined dough or a burnt roast becoming charcoal. Separately, `recipe_step::load` starts with `jo.allow_omitted_members()`, so typos like `"max_tme"` or `"grace"` are silently ignored.

```cpp
// crafting.cpp:2531-2535
release_step_resources( craft );
end_live_wait_for( loc );
get_item_wakeups().cancel_all( craft.uid().get_value() );
item_location mut_loc = loc;
mut_loc.remove_item();      // no add_msg anywhere
// recipe.cpp:668-670
void recipe_step::load( ... ) { jo.allow_omitted_members();
```

**Fix:** Before removal, message the crafter ("Your %s was ruined while unattended", `add_msg_if_player_sees` for NPCs) and fire the step-completion distraction. Remove `allow_omitted_members()` from `recipe_step::load` and visit the requirement keys explicitly. Add per-step hooks (alpha = crafter if resolvable, beta = the craft item, context `_step`, `_recipe_id`):

```json
"on_ruin": { "result": "dough_ruined", "message": "The dough over-proofed.", "eocs": [ "EOC_DOUGH_RUINED" ] },
"start_eocs": [ ... ], "complete_eocs": [ ... ]
```

### CRAFT-B03 · Reversible step recipes ignore `reversible.time`; disassembly takes the full multi-step craft time
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/recipe.cpp:146`, `src/recipe.cpp:520`, `src/recipe_dictionary.cpp:801`, `src/recipe_dictionary.cpp:807`, `src/crafting.cpp:5390`, `src/crafting.cpp:5407`, `src/crafting.cpp:5504`, `src/item_info.cpp:3338`, `src/activity_item_handling.cpp:4191`, `src/game.cpp:6915`, `src/item.cpp:5589`, `data/json/recipes/armor/torso.json:1138`

For a reversible recipe, finalize copies the whole recipe, `steps_` included, into the uncraft map and overwrites `.time` with `uncraft_time`. Every disassembly time path (manual, zone, item info, game list, recursive) calls `time_to_craft_moves( ..., ignore_proficiencies )`, which returns the sum of step times whenever `has_steps()` and never reads `time`. Step recipes cannot set a root `time` (load error), so there is no workaround. Trigger: disassembling a duster (steps 4 h + 4 h, `"reversible": { "time": "50 m" }`) takes 8 h; duster_fur takes 10 h. In vanilla, 11 reversible step recipes exist and 6 (the duster and duster_fur families) set `reversible.time`; unattended steps would also inflate disassembly time.

```cpp
// recipe_dictionary.cpp:802-807
recipe_dict.uncraft[ uncraft_id ] = r;
if( r.uncraft_time > 0 ) { recipe_dict.uncraft[ uncraft_id ].time = r.uncraft_time; }
// recipe.cpp:146-153
if( flags == recipe_time_flag::ignore_proficiencies ) {
    if( has_steps() ) { ... total += s.time; return total; }
    return time;
```

**Fix:** When building the derived uncraft entry, flatten it: clear `steps_` (and `aggregate_proficiencies_`; requirements are already merged) and set `time = uncraft_time` if given, otherwise the sum of the non-unattended step times. Longer term, let `reversible` take `{ "time", "skill_used", "difficulty", "qualities" }` so most explicit uncraft duplicates become unnecessary.

### CRAFT-B04 · Disassembly quality substitutions are merged into `qualities[0]`, turning mandatory requirements into OR options
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/requirements.cpp:1380`, `src/requirements.cpp:1409`, `src/requirements.cpp:1415`, `src/requirements.cpp:1419`, `src/requirements.cpp:382`, `src/crafting.cpp:5229`, `src/activity_item_handling.cpp:2605`, `src/activity_item_handling.cpp:4191`, `src/item_info.cpp:3353`

`disassembly_requirements()` appends substituted qualities (SAW_M_FINE, PULL, CUT) to `qualities[0]`, but each inner vector is an OR group. Groups it clears (SEW/GLARE/KNIT) are left as empty vectors, and `std::unique` removes only adjacent duplicates and keeps the first, possibly lower, level. Consumers then disagree: `can_disassemble` treats every entry as mandatory, zone disassembly treats group 0 as `any_of`, and item info prints only `front()` of each group. About 31 reversible recipes are affected. For beartrap the groups become `[[ANVIL 3, SAW_M_FINE], [HAMMER 3], [WRENCH]]`: item info does not mention the saw, manual disassembly refuses without a fine saw, and zone disassembly (`multi_activity_do` assigns `disassemble_activity_actor` directly, which never calls `can_disassemble`) proceeds without one. For 40x53mm buckshot group 0 becomes CUT | PULL.

```cpp
// requirements.cpp:1415-1416
auto &qualities = ret.qualities[0];
qualities.insert( qualities.end(), new_qualities.begin(), new_qualities.end() );
// 1409-1411: if( replaced ) { it.clear(); }   // empty group left in place
// activity_item_handling.cpp:2606-2609
return cur.empty() || std::any_of( cur.begin(), cur.end(), ... );
```

**Fix:** Push each substituted quality as its own single-entry group (`ret.qualities.push_back( { q } )`), erase emptied groups as is done for tools, and dedupe by type across all groups keeping the maximum level. Make zone disassembly use the same check as `can_disassemble`. Related data-driven fix: CRAFT-J07.

### CRAFT-B05 · Disassembly discards FILTHY/FIT/fault/owner/temperature/rot adjustments when the item has stored components
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/crafting.cpp:5543`, `src/crafting.cpp:5571`, `src/crafting.cpp:5639`, `src/crafting.cpp:5651`, `src/crafting.cpp:5656`, `src/crafting.cpp:5658`

`complete_disassemble` builds `act_item = newit`, applies ambient temperature, FIT inheritance, FILTHY, the parent's relative rot and `set_owner( *this )` (and fault inheritance at 5571-5578 on the copy), then replaces `act_item` wholesale with `dis_item.components.remove( type )`. Removal succeeds whenever real components are stored: every player-crafted reversible item and item-group spawns with `"components"`. Trigger: disassembling filthy crafted clothing yields clean rags; components of crafted or stolen items keep stale temperature/rot and their original owner (`set_owner` does not recurse into components).

```cpp
// crafting.cpp:5639-5661
item act_item = newit;
if( act_item.has_temperature() ) { act_item.set_item_temperature( ... ); }
if( dis_item.is_filthy() ) { act_item.set_flag( flag_FILTHY ); }
act_item.set_relative_rot( dis_item.get_relative_rot() );
act_item.set_owner( *this );
ret_val<item> removed = dis_item.components.remove( newit.typeId() );
if( removed.success() ) { act_item = removed.value(); }
```

**Fix:** Resolve the component first (`item act_item = removed.success() ? removed.value() : newit;`), then apply the fault, temperature, FIT, FILTHY, rot and owner adjustments.

### CRAFT-B06 · `consume_tools(reachable_pts)` debits a `usage_from::both` selection twice
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/crafting.cpp:5152`, `src/crafting.cpp:5161`, `src/crafting.cpp:5164`, `src/character.cpp:5792`, `src/character.cpp:5855`, `src/crafting.cpp:4554`, `src/basecamp.cpp:1068`, `src/basecamp.cpp:1098`, `src/map.h:1525`

`Character::use_charges` takes `qty` by value, while `map::use_charges( reachable_pts, ..., int &quantity )` takes it by reference. For `usage_from::both`, the player draw does not reduce `quantity`, so the map then draws the full amount again, and any remainder is pulled from UPS. `select_tool_component` only returns `both` when neither pool alone suffices, so both pools are drained. The only caller is basecamp `consume_components` (`player_inv = !by_radio`). Trigger: an in-person camp craft where a tool's charges are split between your inventory and camp storage consumes more than the recipe asks for. The radius overload at 5128 is correct.

```cpp
// crafting.cpp:5159-5168
int quantity = tool.comp.count * batch * tmp->charge_factor();
if( tool.use_from == usage_from::player || tool.use_from == usage_from::both ) {
    use_charges( tool.comp.type, quantity, unreserved_filter );   // quantity unchanged
}
if( tool.use_from == usage_from::map || tool.use_from == usage_from::both ) {
    m.use_charges( reachable_pts, tool.comp.type, quantity, ... );
```

**Fix:** Subtract the charges actually returned by the player draw from `quantity` before the map draw, as `consume_items` does at `crafting.cpp:4424-4426`, or draw from the map first and take only the remainder from the player.

### CRAFT-B07 · Crafting search `l:` difficulty filter crashes on long digit strings (`std::stoi`)
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/recipe_dictionary.cpp:383`, `src/recipe_dictionary.cpp:387`, `src/recipe_dictionary.cpp:395`, `src/crafting_gui_helpers.cpp:711`

The difficulty filter accumulates every typed digit with no length cap and calls `std::stoi` with no exception handling anywhere up the chain (`filter_recipes` → `reduce` → `search`, no catch in `main.cpp`). Typing `l:99999999999` or `l:1~99999999999` in the crafting menu throws `std::out_of_range` and terminates the game. The parse is also repeated inside the per-recipe predicate.

```cpp
// recipe_dictionary.cpp:383, 387, 395
start = std::stoi( range_start );
end = std::stoi( range_end );
```

**Fix:** Parse once, outside the predicate, with `std::from_chars` and saturate to MAX_SKILL (or cap the string length / catch and clamp).

### CRAFT-B08 · Passive-step map debit falls back to the absent crafter's own inventory and position
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/crafting.cpp:5141`, `src/crafting.cpp:5145`, `src/crafting.cpp:4870`, `src/crafting.cpp:1414`, `src/crafting.cpp:1302`, `src/character.cpp:5792`, `src/character.cpp:5796`, `src/character.cpp:5826`, `src/crafting.cpp:5166`

When the crafter is in the bubble but more than PICKUP_RANGE from an unattended craft, debits are pinned to the map (`usage_from::map`). The map branch of `consume_tools( map, ..., origin, radius )` draws from around the craft, then sends any remainder (always the case for UPS-powered tools, since `map::use_charges` does not handle UPS) to `Character::use_charges( type, qty, radius )`, which centres on the crafter's `pos_bub()` and walks the crafter's items. The shortfall check was made against an inventory formed at the craft origin, which does count ground UPS. Trigger: a passive step with a USE_UPS tool and a UPS on the workbench drains the crafter's carried UPS or bionic power instead; if the crafter has none, nothing is drained but the bucket is recorded as paid.

```cpp
// crafting.cpp:5141-5146
} else if( tool.use_from == usage_from::map ) {
    m.use_charges( origin, radius, tool.comp.type, quantity, unreserved_filter, bcp );
    if( quantity > 0 ) { use_charges( tool.comp.type, quantity, radius, unreserved_filter ); }
```

**Fix:** In the map branch, consume the leftover via `map::consume_ups` on points around `origin`, as the `reachable_pts` overload does. Never fall back to the Character's inventory for a map-only selection.

### CRAFT-B09 · `byproduct_group` is rolled once per batch, making batch byproducts all-or-nothing
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/recipe.cpp:1307`, `src/recipe.cpp:1310`, `src/recipe.cpp:1313`, `src/recipe.cpp:1318`, `src/recipe.cpp:1333`

`create_byproducts_group` calls `item_group::items_from` once, then multiplies charges by `batch` or pushes `batch - 1` identical copies. Callers (`crafting.cpp:521`, `3788`, `faction_camp.cpp:3421`) pass the whole batch size. A probabilistic group (10% chance of a shell) gives either the full batch or nothing: same expected value, extreme variance, and batch crafting becomes a save-scum jackpot. No core group uses `prob` or `distribution` today, so the current effect is limited to scaling ranges such as salt [80,100].

```cpp
// recipe.cpp:1310-1320
std::vector<item> ret = item_group::items_from( bplist, calendar::turn );
for( item &it : ret ) {
    if( it.count_by_charges() ) { it.charges *= batch; }
    else { ... for( int i = 1; i < batch; ++i ) { bps_out.push_back( it ); } }
```

**Fix:** Roll the group once per batch unit (loop `batch` times over `items_from`) and merge count-by-charges results afterwards.

### CRAFT-B10 · Required proficiencies are learnable from stepless recipes but not from step recipes
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/crafting.cpp:3216`, `src/crafting.cpp:3217`, `src/recipe.cpp:974`, `src/recipe.cpp:1048`

`craft_proficiency_gain` skips proficiencies with `time_multiplier == 0`, assuming that identifies required ones. The step path in finalize zeroes required proficiencies, but the stepless path replaces every 0 multiplier, required ones included, with `default_time_multiplier()`. A crafter can lack a required proficiency when a helper has it (`recipe.cpp:1479-1486`), so in stepless recipes they train it and in step recipes they never do.

```cpp
// crafting.cpp:3216-3218
// Required profs (time_multiplier == 0) gate access, not learning
if( prof.time_multiplier == 0.0f ) { continue; }
// recipe.cpp:974-976 (stepless, applies to required too)
if( rpof.time_multiplier == 0.0f ) { rpof.time_multiplier = rpof.id->default_time_multiplier(); }
```

**Fix:** Test `prof.required` explicitly in `craft_proficiency_gain` (or deliberately allow learning for both kinds), and normalise required proficiencies the same way in both finalize paths.

### CRAFT-B11 · NPC component auto-selection ignores split stacks and aborts the craft with a debugmsg
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/crafting.cpp:4196`, `src/crafting.cpp:4204`, `src/crafting.cpp:3887`

In `select_item_component`'s NPC branch (`!npc_query && is_npc()`) with two or more alternatives, only `player_has` and `map_has` are considered. If every feasible alternative is in `mixed` (split between the NPC's inventory and the ground), the NPC hits "Attempted a recipe with no available components!" and cancels, although `usage_from::both` is valid and the single-choice branch uses it. Reachable from `can_continue_craft` (mid-craft top-up), NPC uses of `consume_items`, `veh_utils.cpp:131` and `magic.cpp:1202`.

```cpp
// crafting.cpp:4196-4206
} else if( !npc_query && is_npc() ) {
    if( !player_has.empty() ) {...} else if( !map_has.empty() ) {...}
    else { debugmsg( "Attempted a recipe with no available components!" ); ... return selected; }
```

**Fix:** Fall back to `mixed[0]` with `usage_from::both` before declaring failure.

### CRAFT-B12 · Disassembly tool charges ignore the number of charges disassembled and are taken after the item is gone
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/crafting.cpp:5534`, `src/crafting.cpp:5537`, `src/crafting.cpp:5178`, `src/crafting.cpp:5554`, `src/crafting.cpp:4565`, `src/requirements.cpp:1334`

`complete_disassemble` removes the target, then calls `consume_tools( vector )` per tool group with the default batch of 1. That re-runs `select_tool_component`, which can pop a "Use which tool?" menu at completion, and returns `usage_from::none` (consuming nothing) if no tool has charges, so it cannot fail. Components are scaled by `activity.position` (the number of charges disassembled), but tool charges are not. Example: reversible `reloaded_shot_00` keeps the fire/hotplate(2)/welder(2) group, so disassembling N shells with a hotplate costs 2 charges total.

```cpp
// crafting.cpp:5534-5539
target.remove_item();
for( const auto &it : dis_requirements.get_tools() ) { consume_tools( it ); }
// crafting.cpp:5553-5555
if( dis_item.count_by_charges() ) { compcount *= activity.position; }
```

**Fix:** Select tools when the disassembly starts (as `craft_command` does), debit `count * activity.position`, and verify and consume before removing the target, aborting with a message on shortfall.

### CRAFT-B13 · Crafting inventory cache key ignores `clear_path` (and the map)
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/crafting.cpp:690`, `src/crafting.cpp:694`, `src/character.h:4388`, `src/veh_interact.cpp:322`, `src/veh_interact.cpp:633`, `src/veh_interact.cpp:738`, `src/activity_item_handling.cpp:2136`, `src/activity_item_handling.cpp:2591`

`Character::crafting_inventory()` caches one inventory keyed on moves, radius, turn, position and reservation generation; `clear_path` and the `map*` argument are not in the key. `crafting_inventory( false )` (no line-of-reach) and the default flood-fill version can return each other's result within a turn, whichever ran first. The original trigger was wrong: `veh_interact` primes the cache with `clear_path = true` first, so the observed effect there is the reverse (a hose behind a wall is not counted). Other `clear_path = false` callers are zone activities, where moves or position usually change the key. A latent defect with unproven harmful reachability.

```cpp
// crafting.cpp:695-702
if( crafting_cache.valid && moves == crafting_cache.moves && radius == crafting_cache.radius &&
    calendar::turn == crafting_cache.time && inv_pos == crafting_cache.position &&
    reservation_generation == crafting_cache.reservation_generation ) {
    return *crafting_cache.crafting_inventory;
```

**Fix:** Add `clear_path` and the map pointer to `crafting_cache_type` and the validity check, or bypass the cache when `clear_path` is false.

### CRAFT-B14 · recipe_group OMT parameter matching dereferences a possibly empty optional
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/recipe_groups.cpp:156`, `src/recipe_groups.cpp:159`, `src/overmap.cpp:3057`, `src/overmap.cpp:3064`, `src/mapgendata.cpp:94`

`get_recipes_by_id` only null-checks the pointer from `overmap_buffer.mapgen_args()` before calling `->value()`. `place_special` stores an empty optional for every OMT of a parameterised special until it is first generated, so a query against an ungenerated "shelter" OMT throws `std::bad_optional_access`. All current callers query generated tiles (the actor's own OMT or one next to the camp); the crash needs e.g. an off-map unique NPC running EOCs on an ungenerated shelter. Side note from review: `condition.cpp:1032` looks up args at `omt_pos` instead of `curr_pos`, a separate bug.

```cpp
// recipe_groups.cpp:156-159
if( !!maybe_args ) {
    ...
    auto map_key_it = maybe_args->value().map.find( key_value_set_pair.first );
```

**Fix:** Test `maybe_args && *maybe_args` before reading, and treat empty args as "not matched" or resolve them with the special's defaults.

### CRAFT-B15 · Negative presence-tool counts (e.g. `["water", -80]`) are silently clamped to -1
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/requirements.cpp:390`, `src/requirements.cpp:396`, `src/requirements.cpp:403`, `src/requirements.cpp:319`, `src/requirements.cpp:1057`, `src/recipe.cpp:1016`, `src/crafting.cpp:1857`, `doc/JSON/ITEM_CRAFT_AND_DISASSEMBLY.md:105`

Every recipe requirement, including inline ones at scalar 1, goes through `operator*`, which clamps tool counts to `max( count * scalar, -1 )`, and `tool_comp::has` then checks for `abs(count)` = 1. About 106 entries in data/json use larger magnitudes (quenching `["water", -80]` / `["water", -240]`, `["makeshift_hand_drill", -2]`), so a recipe meant to need 240 water present is craftable with one unit. The clamp is documented as intended at `requirements.cpp:396-400` and the docs only define -1, so this is mostly a silent-load and data-cleanup issue. The load comment at line 319 wrongly says negative means charges are consumed, and the `crafting.cpp:1857` comment claims the magnitude survives.

```cpp
// requirements.cpp:401-404
for( tool_comp &e : group ) { e.count = std::max( e.count * static_cast<int>( scalar ), -1 ); }
```

**Fix:** Either support magnitudes (do not scale/clamp negative counts; check `charges_of() >= |count|` for count-by-charges types) or reject counts below -1 at load with a clear error and fix the data. Correct the comments at `requirements.cpp:319` and `crafting.cpp:1857`.

## 3. Performance

### CRAFT-P01 · "Wait" mode on an unattended step runs the full env and reservation pipeline every turn instead of every minute
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/activity_actor.cpp:7108`, `src/crafting.cpp:2647`, `src/crafting.cpp:2665`, `src/crafting.cpp:2066`, `src/crafting.cpp:1453`, `src/crafting.cpp:1336`, `src/crafting.cpp:4773`, `src/craft_reservation.cpp:521`, `src/crafting_enums.h:23`

With `do_wait` on an unattended step (78 such steps in core data, e.g. soup/broth steps needing COOK 3 and `water_boiling_heat`), `craft_activity_actor::unattended_craft_step` calls `craft_actualize_scheduled( env_check )` every turn, and `craft_actualize_env` has no due-time gate, although `env_check_at` is re-armed one minute ahead. For hours of game time, each turn runs: (a) `reserve_step_resources` → `enumerate_admitted_providers`, a reachable flood fill plus one item-sized `provider_candidate` per nested item, and six more passes over reachable tiles; (b) `env_qualities_satisfied_for_step`, which copy-assigns the crafter's cached crafting inventory (deep copy, and evicts the default cache entry) before checking whether all groups are already bound; (c) `consume_step_tool_targets`, which builds `scoped_own_claims` before the "no debit due" early return, bumping the reservation generation twice and invalidating every character's crafting-inventory cache. `do_wait` is the default `step_choice`. The generation bump and cache eviction only happen when the craft holds bindings or is off the crafter's tile.

```cpp
// activity_actor.cpp:7109-7113
if( plan.choice == step_choice::do_wait ) { // Per-turn env check fast-path
    craft_actualize_scheduled( *craft_item, item_wakeup_kind::env_check, calendar::turn, craft_item );
// crafting.cpp:2637-2639
const time_point next = now + 1_minutes;
craft.set_env_check_at( std::min( next, craft.get_ready_at() ) );
```

**Fix:**
1. In the fast path (or at the top of `craft_actualize_env`), return unless `now >= craft.get_env_check_at()` (or, while env-paused, `now >= craft.get_ready_at()`); the per-turn branch only keeps the actor alive. About 60x less work per waiting turn.
2. In `env_qualities_satisfied_for_step`, compute `bound_groups` first and return before building any inventory when all groups are bound.
3. Bind a `const temp_crafting_inventory &` to the crafter's inventory instead of copy-assigning; use a local only for the `form_from_map` branch.
4. Construct `scoped_own_claims` after the `debits.empty() && presence.empty()` early return.

### CRAFT-P02 · Crafting menu redraws (~100 Hz) recompute book bonuses, tool speeds and helpers via `expected_time_to_craft`
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/crafting.cpp:509`, `src/recipe.cpp:109`, `src/recipe.cpp:1595`, `src/crafting.cpp:756`, `src/crafting_gui.cpp:1140`, `src/crafting_gui.cpp:1859`, `src/crafting_gui.cpp:1408`, `src/ui_manager.cpp:482`, `src/crafting_gui.cpp:2985`

`expected_time_to_craft` builds a fresh `crafting_cost_context` on each call: `book_bonuses_nearby` walks every carried item plus every map item in a PICKUP_RANGE flood fill; step recipes add `compute_tool_speeds` (uncached `has_quality` and `best_quality_speed_modifier` walks); plus `available_assistant_count` and proficiency maluses. ImGui adaptors redraw on every `redraw_invalidated()` and the menu loop has a 10 ms timeout. `draw_recipe_info_panel` calls `expected_time_to_craft` at 1140 and again at 1859, and the expanded-steps view builds a third context at 1408. The GUI's `query_cache_scope` caches only type-index and provider-quality answers, so an idle menu repeats map-wide item scans dozens of times per second, a direct cause of crafting-menu lag in dense bases.

```cpp
// crafting.cpp:513-514
return rec.batch_time( *this, batch_size, modifier, assistants,
                       crafting_cost_context::for_recipe( *this, rec ) );
// recipe.cpp:112
return { guy.book_bonuses_nearby(), compute_tool_speeds( rec, guy ) };
// ui_manager.cpp:482
if( ( ui.invalidated || ui.is_imgui ) && ui.redraw_cb )
```

**Fix:** Memoize the cost context: store book bonuses, and tool speeds keyed by recipe, next to `crafting_cache` with the same key (turn, moves, position, radius, reservation generation), and have `for_recipe` / `book_bonuses_nearby` read through it. Alternatively, cache `{expected_turns, success, cost_ctx}` per (recipe, batch) in the GUI and clear it in `invalidate_info_panels`. Either makes per-frame cost O(1) after the first frame.

### CRAFT-P03 · `form_from_map` copies a whole `ter_t` and builds an item for every tile in range
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/map.cpp:6168`, `src/temp_crafting_inventory.cpp:433`, `src/temp_crafting_inventory.cpp:370`, `src/crafting.cpp:1582`

`form_from_map` and `enumerate_admitted_providers` call `m.liquid_from( p )` for every reachable tile (up to 13x13) on every crafting-inventory build, `map_inv` build and env poll. `liquid_from` copies the entire `ter_t` by value (translations, flag sets, a vector of deep-cloned `clone_ptr<iexamine_actor>`) and returns a default-constructed item for the ~99% of tiles with no liquid. The tile loop also builds `ter_id brick_oven( "t_brick_oven" )` from a string on every iteration.

```cpp
// map.cpp:6168
ter_t source_terrain = ter( p ).obj();   // copy, not reference
// temp_crafting_inventory.cpp:370
ter_id brick_oven( "t_brick_oven" );     // per tile
```

**Fix:** Check `t->liquid_source_item_id.is_null()` before calling `liquid_from` in both loops, change `map.cpp:6168` to `const ter_t &`, and hoist the brick-oven id to a file-level `static const ter_str_id`.

### CRAFT-P04 · Reservation candidate classing is quadratic: sort comparator re-scores, signatures rescan the whole pool
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/craft_reservation_search.cpp:274`, `src/craft_reservation_search.cpp:291`, `src/craft_reservation_search.cpp:305`, `src/craft_reservation_search.cpp:347`, `src/craft_reservation_search.cpp:448`, `src/craft_reservation_search.cpp:59`, `src/crafting.cpp:1637`, `src/crafting.cpp:2204`

When a step's groups are not all bound (step entry, rebinding, and every poll of a provider-paused craft), every admitted candidate goes through `collapse_merge_classes` and `build_candidate_classes`. The `stable_sort` comparator recomputes `already_held()` and `best_score()` (R calls to `score_reservation_candidate`, which may copy the synthesized item) for both sides on every comparison. Because `candidate_merges_with` only merges liquids, `kept` grows to about N, making the `any_of` merge loop and the `std::find` order restore O(N²). `exclusion_signature( cand, pool )` scans the whole pool per exclusive candidate. `filter_available_for_binding` also takes the vector by value. With 2-5k reachable items this is tens to hundreds of milliseconds per call.

```cpp
// craft_reservation_search.cpp:274-286 (comparator)
if( already_held( *a ) != already_held( *b ) ) ...
const std::optional<int> lhs = best_score( *a );
const std::optional<int> rhs = best_score( *b );
// 347: for( const provider_candidate &other : pool ) {
```

**Fix:** Precompute score and held state once per candidate and sort indices; drop candidates that serve no request before collapsing (keep the full list only as the debit pool); track `kept` with a flag per index; build exclusion signatures once per exclusion unit in one pass; take `admitted` by const ref or move; make `provider_pseudo_quality` take `const item &`. Together roughly O(N log N).

### CRAFT-P05 · `disassembly_requirements()` is rebuilt on every call and constructs an item per component alternative
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/requirements.cpp:1334`, `src/requirements.cpp:1436`, `src/recipe.h:289`, `src/item.cpp:5219`, `src/game_inventory.cpp:644`, `src/game_inventory.cpp:649`, `src/crafting.cpp:5227`, `src/activity_item_handling.cpp:2604`, `src/item_info.cpp:3336`, `src/item_search.cpp:105`

`recipe::disassembly_requirements()` returns a fresh copy of the whole `requirement_data`, rewrites tools and qualities, and constructs a full `item( comp.type )` per component alternative just to test a type flag (TODO in code: cache it). Callers: the Disassemble menu (about 3 rebuilds per entry when it opens; results are cached per entry afterwards), NPC zone disassembly per item per scanned tile, item info, `item::get_uncraft_components` (stacking comparisons, recursive disassembly time), `complete_disassemble`, and the `d:` item search filter per item.

```cpp
// requirements.cpp:1340-1341
// Maybe TODO: Cache it somewhere and return a reference instead
requirement_data ret = *this;
// requirements.cpp:1435
return !comp.recoverable || item( comp.type ).has_flag( json_flag_UNRECOVERABLE );
```

**Fix:** Compute it once in `recipe::finalize` into a `disassembly_requirements_` member (uncraft recipes are reversible too, so this covers every caller) and return `const requirement_data &`. Replace `item( comp.type ).has_flag(...)` with `comp.type->has_flag( json_flag_UNRECOVERABLE )`, which is equivalent for a fresh item.

### CRAFT-P06 · Startup: `requirement_data::finalize` is rerun ~120 times; inlining copies whole requirements and dedupes in O(n²)
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/requirements.cpp:775`, `src/requirements.cpp:730`, `src/requirements.cpp:801`, `src/fault.cpp:376`, `src/wound.cpp:229`, `src/init.cpp:858`

`requirement_data::finalize` inlines every LIST reference (3577 in core data), copying the entire referenced requirement with `operator*` but reading only `getter( multiplied ).front()`. `add_component` dedupes with a linear `find_if` over the growing accumulator. The bigger cost: the global finalize is also called from every `fault_fix::finalize` (72 in core) and `wound_fix::finalize` (46), so about 120 full passes over ~6k requirements, each redoing the quadratic dedupe and a `std::list` allocation per tool from `subtype_replacement`. Passes after item finalize also re-append tool subtypes, so `requirements_all` ends up with duplicates like `[A, B, B]`.

```cpp
// requirements.cpp:775-776
const requirement_data &req = r.obj();
const requirement_data multiplied = req * comp.count;   // only getter( multiplied ).front() is used
```

**Fix:** Stop calling the global `requirement_data::finalize()` from each fault/wound fix (finalize once, or only the fix's own requirement). Read `getter( req ).front()` directly and scale counts on the fly (same clamp as `operator*`), and keep an `unordered_map<itype_id, index>` beside `accum` for O(1) dedupe.

### CRAFT-P07 · Each `crafting_inventory` rebuild deep-copies every carried item; the single-slot cache thrashes
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/crafting.cpp:695`, `src/crafting.cpp:729`, `src/temp_crafting_inventory.cpp:149`, `src/temp_crafting_inventory.cpp:158`, `src/crafting.cpp:4549`, `src/crafting.cpp:4558`, `src/character.cpp:5796`, `src/crafting.cpp:1338`, `src/activity_item_handling.cpp:2548`

`Character::crafting_inventory` calls `add_item_copy( *it )` for every carried item whose CONTAINER pockets are empty: a full copy with contents, vars, flags and `item_components` (crafted items drag their whole component tree). The cache has one slot keyed on (moves, turn, position, radius, generation), and callers alternate keys: `select_tool_component` alternates radius -1 and PICKUP_RANGE per charged tool alternative, the env check uses the craft tile, `Character::use_charges` uses radius -1 on every player-sourced debit, and NPC zone activities rebuild per candidate tile. The `temp_crafting_inventory` copy constructor re-copies every owned item, and callers copy by value (`character.cpp:5796`, only to look for a USE_UPS tool; `crafting.cpp:1338`). The deep copying predates the fork, and per-turn cost is usually dominated by `form_from_map`.

```cpp
// crafting.cpp:729
crafting_cache.crafting_inventory->add_item_copy( *it ); return VisitResponse::SKIP;
// character.cpp:5796
temp_crafting_inventory inv = crafting_inventory( pos_bub(), radius, true );
```

**Fix:** Use `add_item_loc( it )` for leaf items (only the non-empty-container case needs a synthetic copy), relying on existing `invalidate_crafting_inventory()` calls. Split the cache into a carried part keyed on (turn, moves) and a map part keyed on (map, origin, radius, clear_path), or use a small 2-4 entry LRU. Bind `const temp_crafting_inventory &` at `character.cpp:5796` and `crafting.cpp:1338`.

### CRAFT-P08 · Reservation search pool check repeats a full inventory walk on each expansion (up to 32k per search)
**Severity:** Medium · **Effort:** Medium · **Verified:** plausible
**Where:** `src/crafting.cpp:1673`, `src/crafting.cpp:1694`, `src/crafting.cpp:1730`, `src/craft_reservation_search.cpp:792`, `src/craft_reservation.h:112`, `src/crafting.cpp:2256`

`assign_from`/`take_one` call `ctx.pool_feasible( state.selected )` once per expansion, with a budget of 4096 doubling to 32768. Each call redoes selection-independent work (`collect_step_tool_debits`, `aggregate_tool_debits`, the `types` set), then per charged type a `present_char->charges_of( type, INT_MAX, filter )` inventory walk, plus several linear passes over `debitable`. Separately, `pool_fingerprint` and the full classing run before the "capped and unchanged, defer" check, so a capped craft pays for discovery and classing on every poll. Most searches end after about as many expansions as there are classes; the thousands-of-expansions case was not shown to be common, but the deferral-ordering waste is certain.

```cpp
// craft_reservation_search.cpp:791-792
--ctx.expansions_left;
const bool pool_ok = ctx.pool_feasible( state.selected );
// crafting.cpp:1730
return src.present_char->charges_of( type, INT_MAX, [&hidden_carried_uids]( const item & it ) {...} );
```

**Fix:** Before the search, precompute the need maps and, per charged type, total carried charges and per-root/per-uid contributions; `pool_feasible` then subtracts hidden units in O(|selected|), and is constant true when need is empty. Move the deferral check ahead of classing using a cheap fingerprint over admitted candidates or a generation counter.

### CRAFT-P09 · `recipe::get_component_filter()` builds a full result item and several `std::function`s per call
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/recipe.cpp:1885`, `src/recipe.cpp:1888`, `src/crafting.cpp:3825`, `src/crafting_gui_helpers.cpp:158`, `src/crafting_gui_helpers.cpp:190`, `src/crafting_gui_helpers.cpp:213`, `src/crafting_gui.cpp:1369`

Each call constructs `item result( result_ )`, builds string temporaries for `has_flag( "ALLOW_ROTTEN" )` / `"NEED_FULL_MAGAZINE"`, and wraps filters in `std::function`, although all derived values depend only on the recipe. The crafting list calls it up to 3 times per recipe (cached per recipe), and `draw_recipe_info_panel` calls it every frame. `can_continue_craft`'s `std_filter` calls it inside the per-item predicate, but only when `continue_reqs` is non-empty, which is rare. The cost is small next to the inventory walks that follow.

```cpp
// recipe.cpp:1888
const item result( result_ );
// crafting.cpp:3825-3827
auto std_filter = [&rec]( const item & it ) { return rec.get_component_filter()( it ) && ...
```

**Fix:** Precompute `forbids_rotten`, `forbids_frozen` and `need_full_magazine` in `recipe::finalize` and return a single lambda over those bools; hoist `const auto comp_filter = rec.get_component_filter();` out of `std_filter`.

Reviewer side note (not verified as a separate finding): `craft_activity_actor` caches `cached_continuation_requirements` (`activity_actor.cpp:6766-6769`) and never clears it after `can_continue_craft` consumes the missing parts, while `do_turn` runs the check every turn, so the "consume missing components" prompt and consumption may repeat. Worth its own bug check.

### CRAFT-P10 · `deduped_requirement_data` re-checks identical tools/qualities for every alternative with no early exit
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/requirements.cpp:1768`, `src/requirements.cpp:1837`, `src/requirements.cpp:952`, `src/requirements.cpp:1188`

Every deduped alternative has the same `tools_unique` and qualities; only components differ. `can_make_with_inventory` runs the full check per alternative (up to 115, e.g. boots_fur), and that check never short-circuits: it evaluates qualities, tools, components and `check_enough_materials` even after a failure. Outside the GUI's query cache (can_make, can_start_craft, NPC/basecamp/zone checks) each repeat is a fresh inventory walk. The "available" marks are not display-only, though: `check_enough_materials` reads them, and each alternative owns its own tool copies, so checking tools only on `alternatives_[0]` would leave other alternatives' marks stale. Most recipes have a single alternative, so the gain is unproven.

```cpp
// requirements.cpp:1842-1845
return std::any_of( alternatives().begin(), alternatives().end(), [&]( const requirement_data & alt ) {
    return alt.can_make_with_inventory( actor, crafting_inv, filter, batch, flags );
} );
```

**Fix:** Add an early-exit mode (flag or separate function) to `requirement_data::can_make_with_inventory` for boolean callers, returning as soon as `retval` is false. If tool/quality results are shared across alternatives, copy the availability marks rather than skipping the check.

### CRAFT-P11 · Crafting search by book walks the whole crafting inventory once per recipe
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/recipe_dictionary.cpp:320`, `src/recipe_dictionary.cpp:329`, `src/recipe_dictionary.cpp:347`, `src/item.cpp:3558`, `src/item.cpp:3499`

For `search_type::book` (`b:`), the per-recipe predicate visits every crafting-inventory item, nested ones included, calls `get_available_recipes( crafter )` (which builds a vector, and for non-books does a `get_var( "EIPC_RECIPES" )` string build and split), lcmatches each book name, and then recomputes `get_crafting_group()`. That is O(recipes x items) per search submission.

```cpp
// recipe_dictionary.cpp:326-340 (inside the per-recipe predicate)
crafting_inventory.visit_items( [&]( const item_location & node ) {
    for( const auto &recipe : node->get_available_recipes( crafter_ref ) ) {
        if( recipe.first == r && ( lcmatch( node->display_name(), txt ) ...
```

**Fix:** In the "prepare search" step, walk the inventory once and collect the available recipes of name-matching books into an `unordered_set<const recipe *>`; compute name-matching helpers once. The predicate becomes a set lookup plus `knows_recipe` on those helpers.

### CRAFT-P12 · Startup: check for nested recipes with no parent category is O(nested x all recipes)
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/recipe_dictionary.cpp:830`

For each recipe in subcategory `CSC_*_NESTED` (2006 in core, 2415 with mods), `recipe_dictionary::finalize` scans the `std::map` of all recipes (~6.3k entries) and linear-searches every nested category list, about 6M node visits on each load (tens of ms) to emit a debugmsg that almost never fires.

```cpp
// recipe_dictionary.cpp:831-842
for( const auto &rec : recipe_dict.recipes ) {
    if( rec.second.subcategory == "CSC_*_NESTED" ) {
        for( const auto &nest : recipe_dict.recipes ) {
            if( nest.second.is_nested() ) { if( find( ... nested_category_data ..., rec.first ) != ...
```

**Fix:** Build an `std::unordered_set<recipe_id>` of all ids in any `nested_category_data` in one pass, then check each orphan with a set lookup.

### CRAFT-P13 · `get_available_recipes` copies the learned recipe_subset, then re-inserts it again
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character_crafting.cpp:172`, `src/character_crafting.cpp:208`, `src/recipe_dictionary.cpp:930`, `src/recipe_dictionary.cpp:970`

On a turn-cache miss, `get_group_available_recipes` calls `get_available_recipes` per crafting-group member, which copy-constructs the whole learned `recipe_subset` (recipe set, difficulties, category and per-component maps), and then `include()`s it into `cached_recipe_subset`, rebuilding the component and category indexes a second time. This runs at most once per turn, so the cost is small. The helper path at lines 176-185 is dead (no caller passes `helpers`). In the same path, the linear `recipe_subset::contains` (`recipe_dictionary.h:120`) called from `get_available_nested` (`character_crafting.cpp:106`) probably costs more than the double copy.

```cpp
// character_crafting.cpp:172, 208-210
recipe_subset res( get_learned_recipes() );
for( const Character *guy : get_crafting_group() ) {
    cached_recipe_subset->include( guy->get_available_recipes( crafting_inventory() ) );
}
```

**Fix:** Add `void collect_available_recipes( recipe_subset &out, ... ) const` that appends directly into the target subset; optionally build the `component` index lazily on the first `of_component()` call, and make `contains` a set lookup.

## Appendix: considered and rejected

- Proficiency maluses re-scan all characters with clear_path, twice per missing proficiency per call – the redundancy exists, but `get_characters_if` only covers the avatar and active NPCs and `clear_path` runs only for allies within 6 tiles; `proficiency_time_maluses` is cached lazily per list entry, and per-frame cost is dominated by `crafting_cost_context::for_recipe` (see CRAFT-P02), so the cost is negligible.
