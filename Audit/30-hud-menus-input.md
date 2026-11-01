# 30 · HUD, widgets, menus & input

> Engine audit – static code reading at commit `a1a646d6`; nothing was compiled or run. Each finding was checked against the code by a separate reviewer: **confirmed** = verified by reading the code; **plausible** = consistent with the code, but reachability or impact not proven. Line numbers refer to that commit.

**Scope:** `src/panels.cpp`, `src/widget.cpp`, `src/display.cpp`, `src/uilist.cpp`, `src/ui_manager.cpp`, `src/overmap_ui.cpp`, `src/input.cpp`, `src/input_context.cpp`, `src/cata_imgui.cpp`, `src/surroundings_menu.cpp`, `src/messages.cpp`, `src/player_display.cpp`, `src/safemode_ui.cpp`, `src/string_input_popup.cpp`, `src/popup.cpp`, `src/main_menu.cpp`, `src/auto_note.cpp`, `src/help.cpp`

## Summary

The JSON widget system is flexible on the surface, but it is evaluated only for the avatar, has no beta talker, and is never validated at load time. Several malformed or merely non-exhaustive widget definitions crash the game (integer division by zero) instead of failing at load. Large parts of the HUD, @ screen, safe mode, overmap and input layers are still hardcoded C++ that mods cannot extend: no mod keybindings, no EOC access to overmap notes or safe mode, and no way to inject a sidebar section into every sidebar. There are also a few small but real bugs that lose state: per-character safe-mode rules are never saved, the keybinding editor can skip its save prompt, and keybinding saves freeze the defaults. On the performance side, the main problem is the surroundings (V) menu, which redraws the whole map and sidebar every 10 ms while idle. Most other performance items are small per-frame waste.

| Lens | Critical | High | Medium | Low | Total |
|---|---|---|---|---|---|
| Missing JSON / EOC | 0 | 0 | 10 | 8 | 18 |
| Bugs & quirks | 0 | 3 | 7 | 6 | 16 |
| Performance | 0 | 1 | 2 | 8 | 11 |
| **Total** | 0 | 4 | 19 | 22 | 45 |

**Fix first:**
1. **UIHUD-B01** · Per-character safemode rules are never saved – a one-character fix (`||` to `&&`) for silent data loss.
2. **UIHUD-B02** · Clause-driven column layout divides by zero – a mod's conditional row crashes the game mid-play; the guard is a few lines.
3. **UIHUD-B03** · ASCII overmap generates whole overmaps for unseen tiles – deleting dead code removes stalls and save bloat.
4. **UIHUD-B04** · `carry_weight` widget divides by zero – reachable in vanilla through mutation combinations with a shipped sidebar; one shared guard fixes it.
5. **UIHUD-P01** · Surroundings menu redraws the whole map every 10 ms while idle – the largest performance waste in this system, and the fix is a small change check.

## 1. Missing JSON bindings & EOC features

### UIHUD-J01 · Missing load-time validation for widgets lets typos fail silently or read out of bounds
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/init.cpp:930`, `src/widget.cpp:543`, `src/widget.cpp:1445`, `src/widget.cpp:1420`, `src/widget.cpp:1719`, `src/display.cpp:1584`, `src/panels.cpp:553`, `src/panels.cpp:430`

Widgets are only finalized at load (`init.cpp:930`). The consistency-check list has no widget entry, and `widget` has no `check()`. As a result:
- an unknown `style` renders the literal `???`. `"style": "clause"` is used as a de facto abstract template in 19 vanilla objects.
- an unknown `fill` calls debugmsg on every redraw instead of failing at load.
- an unknown `arrange` silently becomes columns.
- a misspelled `body_graph` id silently renders an empty widget.
- bad child `widgets` ids fail only when the widget is drawn.
- a `breaks` array of the wrong length only triggers a load debugmsg, and `value_color` then reads `_breaks[i]` out of bounds.
- a graph with one symbol, or `pool` fill with width 0, divides by zero (see B05).
- a missing sidebar falls back to the non-existent id `"labels"` (see B09).

```cpp
{ _( "Widgets" ), &widget::finalize_all }   // init.cpp:930, no check entry
if( _breaks.size() != _colors.size() - 1 ) { debugmsg( ... ); }   // widget.cpp:543
for( int i = 0; i < color_max; i++ ) { if( value_offset < _breaks[i] )   // :1445
ret += "???";                                                    // :1420
current_layout_id = "labels";                                    // panels.cpp:553
```

**Fix:** Add `widget::check_consistency()` to the check list. It should validate:
- `style`, `fill` and `arrange` against their enums;
- that `var` fits the style (text vars need style text; custom needs number or graph);
- child and clause widget ids, body_graph ids and bodypart ids;
- that `breaks` has the right length and is ascending;
- that colors parse;
- that graphs have at least 2 symbols and a width greater than 0.

Add an `"abstract"` flag for clause-only templates so they no longer need style `clause`. Change the panel fallback to the platform default (`legacy_labels_sidebar` / `sidebar-mobile`).

### UIHUD-J02 · Widget text interpolation only half works (default_clause, sym, string, context vars)
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/widget.cpp:1538`, `src/widget.cpp:1543`, `src/widget.cpp:1551`, `src/widget.cpp:1567`, `src/widget.cpp:1590`, `src/widget.cpp:1595`, `src/widget.cpp:1605`, `src/widget.cpp:559`, `src/widget.cpp:337`, `src/widget.cpp:344`, `src/npctalk.cpp:2308`, `src/npctalk.cpp:2492`, `src/math_parser_diag_value.cpp:109`

`parse_tags` (which expands `<u_val:x>`, `<global_val:x>`, etc.) is applied only to the `text` of non-default clauses. Five gaps follow from that:
1. `default_clause` loads `parse_tags`, but the fallback branches of `text_cond` and `sym_text_cond` ignore it.
2. `sym` is never parsed.
3. The widget-level `string` is never parsed.
4. Where tags are parsed, the 3-arg `parse_tags` builds a fresh `const_dialogue`, so the `widget` context var set in `meets_condition` is lost and `<context_val:widget>` (the current body part) cannot be printed.
5. Numbers are printed with default ostream formatting: 6 significant digits, so `1234567` becomes `1.23457e+06`.

As a result, defaults like `Mana: <u_val:mana>/<u_val:mana_max>`, or per-limb text that shows the limb's own value, do not work. Mods already rely on this path (Isolation-Protocol's `mana_numbers`).

```cpp
// widget.cpp:1541-1545 default branch: no should_parse_tags check
std::string txt = _default_clause.text.translated(); return ... colorize( txt, _default_clause.color );
// widget.cpp:1551-1553
if( wp->should_parse_tags ) { parse_tags( txt, get_player_character(), get_player_character() ); }
// npctalk.cpp:2311 — fresh dialogue, no context
const_dialogue d( get_const_talker_for( u ), get_const_talker_for( me ) );
```

**Fix:** Pass the clause's dialogue, with the `widget` context set, into `parse_tags`. Honor `parse_tags` on `default_clause`, `sym` and `string`. Add a formatted-number tag, e.g. `<math:EXPR[:precision]>` or `<u_val:x:%.1f>`.

### UIHUD-J03 · No way to add a sidebar section to every sidebar, and no EOC to show or hide panels
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/panels.cpp:374`, `src/panels.cpp:399`, `src/panels.cpp:555`, `src/widget.cpp:1139`, `data/mods/Magiclysm/ui/sidebar.json:73`, `data/mods/Xedra_Evolved/ui/sidebar.json:49`, `doc/WIDGETS.md:1257`

Panels are built only from each sidebar widget's own `_widgets` list. Panel visibility comes only from `W_DISABLED_BY_DEFAULT` and the user's saved `panel_options`. A mod that wants a HUD section must `copy-from` + `extend` each of the 13 vanilla sidebars, and it still cannot reach sidebars added by other mods. In practice:
- Magiclysm extends 8 sidebars. It skips `custom_sidebar`, `sidebar-mobile`, both zenfs sidebars and `my_labels_sidebar_cleaner`, even though WIDGETS.md says it extends `custom_sidebar`.
- Xedra extends 10 and skips mobile and both zenfs sidebars.

No EOC can toggle a panel or switch the layout. The `W_DISABLED_WHEN_EMPTY` + clauses pattern covers "show when the player gains magic", but not injecting a section everywhere.

```cpp
for( const widget_id &row_wid : wgt._widgets ) { ... ret.emplace_back( row_widget.get_window_panel( width ) ); }  // panels.cpp:382-385
```

**Fix:** Add a section-injection field on widgets that `initialize_default_panel_layouts()` merges into every sidebar after load:
```json
"sidebar_section": { "sidebars": [ "*" ], "after": "stats_layout", "default_enabled": true }
```
Also add the EOC effects `{ "set_sidebar_section": "xe_mana_graph_layout", "enabled": true }` and `{ "set_sidebar_layout": "custom_sidebar" }`, plus a `has_sidebar_section_enabled` condition.

### UIHUD-J04 · EOCs cannot create, read or delete overmap notes or danger marks
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/overmap_ui.cpp:2879`, `src/overmap_ui.cpp:2922`, `src/overmap_ui.cpp:2941`, `src/overmap_ui.cpp:505`, `src/auto_note.cpp:167`, `src/npctalk.cpp:8692`, `src/map_extras.cpp:1360`

The engine already has what it needs for notes:
- the note helpers `ui::omap::range_mark`, `line_mark` and `path_mark` (only `faction_camp.cpp` calls them);
- the `sym:color;text` note format;
- `mark_note_dangerous`;
- C++ auto-notes.

None of it is reachable from EOCs. There is no note effect, condition or math function. Auto-notes cover only map_extras (`autonote_visibility`), not overmap specials. Mods with radio broadcasts, quest-givers or scouting spells can only use `reveal_map`, which cannot label anything or mark a lair dangerous for auto-travel. Mission target markers are a partial alternative.

```cpp
if( !overmap_buffer.has_note( pt ) ) { overmap_buffer.add_note( pt, message ); }  // overmap_ui.cpp:2909-2913
for( const map_extra &extra : MapExtras::mapExtraFactory().get_all() )        // auto_note.cpp:167
```

**Fix:** Add these effects:
```json
{ "u_add_map_note": { "context_val": "loc" }, "text": "Bandit camp", "symbol": "B", "color": "red", "danger_radius": 2, "overwrite": false }
{ "u_delete_map_note": { "context_val": "loc" }, "only_if_text": "..." }
```
Add a `"shape": "circle" | "line"` option that reuses `range_mark`/`line_mark`. Also add:
- condition `{ "map_has_note": loc }`
- math `map_note_danger(loc)`
- string tag `<map_note:loc>`
- an `"autonote": { "symbol", "color", "text" }` field on `overmap_special`

### UIHUD-J05 · Clause value-threshold lookup and clause ids are unused; clauses cannot be extended
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/widget.cpp:365`, `src/widget.cpp:394`, `src/widget.cpp:1633`, `src/widget.cpp:549`, `src/widget.cpp:533`, `src/widget.cpp:539`, `src/widget.cpp:1649`, `data/mods/Xedra_Evolved/ui/vamp_blood_levels.json:8`

Text, symbol, number and legend widgets show every clause whose condition is true. To get mutually exclusive bands, authors must write both bounds in every clause, as `thirst.json` and Xedra's vampire blood widget do. A threshold lookup already exists (`get_clause_for_id` with `thresh_val`, plus `get_val/text/sym/color_for_id` and `widget::get_clause(id)`), but nothing calls any of it. The clause `id` has no effect.

`clauses`, `colors`, `breaks` and `bodyparts` are read with a manual `has_array`, so `extend`/`delete` do not work on them. For example, adding one status to the vanilla `bodypart_status` widget means copying the whole clauses array. `breaks` is also read only inside the `colors` block, so a copy-from child cannot override `breaks` alone.

```cpp
if( jo.has_array( "clauses" ) ) { _clauses.clear(); ... }  // widget.cpp:549-556
if( jo.has_array( "breaks" ) )                            // :539, nested inside colors
```

**Fix:** Add `"clause_mode": "all" | "first" | "threshold"`:
- `first`: the first true clause wins.
- `threshold`: wire up `get_clause_for_id`, so the clause with the largest `value` that is `<=` the var wins.

Key clauses by `id`, so that `"extend": { "clauses": [...] }`, `"delete": { "clauses": [ "bitten" ] }` and same-id replacement all work. Load colors, breaks and bodyparts with `optional()`.

### UIHUD-J06 · Widgets have no beta talker, so the HUD cannot show the driven vehicle, wielded item or target
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/widget.cpp:346`, `src/widget.cpp:463`, `src/widget.cpp:472`, `src/display.cpp:149`, `src/display.cpp:1025`, `src/widget.cpp:1322`, `src/vehicle.cpp:9252`, `src/item_location.cpp:1491`, `src/npctalk.cpp:2670`

Every widget clause and custom var runs with alpha = avatar and beta = nullptr, even though vehicle and item talkers exist. As a result:
- The vehicle HUD and the weapon HUD stay fixed C++ text functions.
- Mods cannot show vehicle vars (mech heat, reactor charge), the wielded item's vars, or stats of the aimed-at creature.
- An `npc_` condition in a widget clause does not just fail: `const_actor` calls debugmsg.

The only workaround is a recurring EOC that copies values into `u` vars through `u_run_vehicle_eocs` / `u_run_inv_eocs`.

```cpp
dialogue d( get_talker_for( get_avatar() ), nullptr );          // widget.cpp:346
const_dialogue d( get_const_talker_for( ava ), nullptr );       // :463/:472
std::unique_ptr<talker> get_talker_for( vehicle &me )           // vehicle.cpp:9252
```

**Fix:** Add a widget field `"beta": "driven_vehicle" | "wielded_item" | "aim_target" | "mount"`. Child widgets inherit it. It fills the beta talker before conditions and custom vars run. When the target is missing, the widget counts as empty, so it combines with `W_DISABLED_WHEN_EMPTY`. A mech HUD could then use, for example, `"custom_var": { "value": { "math": [ "n_val('heat')" ] } }`.

### UIHUD-J07 · Safe mode is opaque to JSON: name-matched rules, no state access, no 'spotted' event
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/safemode_ui.cpp:652`, `src/safemode_ui.cpp:661`, `src/safemode_ui.cpp:717`, `src/game.cpp:3936`, `src/game.cpp:4005`, `src/condition.cpp:608`, `src/event.h:25`

Safe mode has three gaps for mods:
- **Rule matching.** Rules match wildcard patterns against the *translated* display name (`mtype.nname()` / `critter.name()`), not against id, species or flag. Mods cannot ship default rules, and user rules break when the language changes.
- **State access.** EOCs cannot read or set `g->safe_mode`. The only related conditions are `u_safe_mode_trigger` and `u_monsters_in_direction`, which give one bool per direction, with no count and no NPC coverage.
- **No event.** The "spotted a hostile" moment (`game.cpp:4005-4028`) fires no event_bus event, so mods cannot hook first-sighting lore, sanity hits or an automatic ward cast.

```cpp
for( const mtype &mtype : MonsterGenerator::generator().get_all_mtypes() ) { set_rule( rule, mtype.nname(), rule_state::BLACKLISTED ); }  // safemode_ui.cpp:661-663
get_safemode().check_monster( critter.name(), ... )   // game.cpp:3936-3940
```

**Fix:**
1. Add an mtype/species field `"safemode": "ignore" | "always"` that is checked before user rules, and let rules match `id:` and `species:` prefixes.
2. Add an assignable math function `safe_mode()` (0 off / 1 on / 2 suppressed) and `u_monsters_in_direction(dir, 'count')`.
3. Add `event_type::avatar_spots_hostile { monster_id | npc character_id, distance, direction, first_time }`, emitted where `newseen > mostseen`.

### UIHUD-J08 · @ screen has fixed rows and per-id special cases (bio_cqb, TROGLO); mods cannot add entries
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/player_display.cpp:448`, `src/player_display.cpp:272`, `src/player_display.cpp:826`, `src/player_display.cpp:831`, `src/player_display.cpp:1587`, `src/player_display.cpp:1618`, `src/suffer.cpp:774`, `src/math_parser_diag.cpp:1819`

The @ screen is hardcoded in three places:
- **Stats tab.** It has 9 fixed rows with hardcoded help text. Mods cannot add stats such as mana, sanity or blood. The only JSON hook is an enchantment's name/description in the Effects tab.
- **Skills tab.** It special-cases `bio_cqb`: it forces level 5 and "locked" for a hardcoded list of 5 skills.
- **Effects tab.** It hardcodes TROGLO/TROGLO2/TROGLO3 sunlight text that repeats suffer.cpp's numbers. DAYFEAR (suffer.cpp:774) has no entry at all. These cannot become enchantments because no condition or math exposes sun irradiance.

Enchantments can already modify skill levels (`:831`), so only the lock/no-training part is missing there.

```cpp
if( you.has_active_bionic( bio_cqb ) && is_cqb_skill( aSkill->ident() ) ) { level_num = 5; exercise = 0; locked = true; }  // :826-830
if( has_trait( trait_TROGLO3 ) && g->is_in_sunlight( pos_bub() ) ) { ... "Strength - 4; ..." }                             // :1587-1590
```

**Fix:**
- Add a character-sheet slot: `{ "type": "ui_slot", "id": "character_sheet_stats", "widgets": [ "mana_num" ] }`, with an optional per-widget `"description"` for the info pane.
- Replace the bio_cqb branch with `"skill_overrides": [ { "skills": [...], "level": 5, "prevents_training": true } ]` on bionics or enchantments.
- Add math `sun_irradiance()` and a `u_in_sunlight` condition, so TROGLO and DAYFEAR can become conditional enchantments.

### UIHUD-J09 · Widgets only render for the avatar, so other status UIs fall back to hardcoded copies
**Severity:** Medium · **Effort:** Large · **Verified:** confirmed
**Where:** `src/widget.cpp:344`, `src/widget.cpp:461`, `src/widget.cpp:467`, `src/widget.h:349`, `src/display.cpp:646`, `src/display.cpp:677`, `src/display.cpp:842`, `src/game_inventory.cpp:952`, `src/faction_ui.cpp:600`, `src/player_display.cpp:455`, `src/player_display.cpp:1541`, `src/debug_menu.cpp:1996`

Hunger, thirst and rest text in the sidebar is defined in JSON clauses. Widget evaluation is tied to the avatar (`show(const avatar&)`, `get_talker_for( get_avatar() )`). Every other screen that shows the same needs calls hardcoded `display::` functions with duplicated thresholds, strings and colors: the consume-menu hint, the follower panel (which shows an NPC), the @ screen lines and the debug menu. A mod that renames thirst states, moves their thresholds or adds a new need (e.g. Xedra's blood) changes only the sidebar. The copies currently match (the reviewer found no drift), but they are maintained separately.

```cpp
dialogue d( get_talker_for( get_avatar() ), nullptr );   // widget.cpp:346
display::hunger_text_color( *picked_follower ); display::thirst_text_color( ... );  // faction_ui.cpp:600-602
```

**Fix:** Make widget evaluation work for any Character or talker: `widget::show( const Character &, ... )`. Then let other UIs render named widgets through a JSON slot such as:
```json
{ "type": "ui_slot", "id": "consume_menu_needs", "widgets": [ "hunger_desc_label", "thirst_desc_label", "sleepiness_desc_label", "pain_desc_label" ] }
```
Do the same for a `follower_needs` slot. Mods could then `extend` these slots with their own need widgets.

### UIHUD-J10 · Mods cannot add keybindings or input actions, and no key can trigger an EOC
**Severity:** Medium · **Effort:** Large · **Verified:** confirmed
**Where:** `src/input.cpp:161`, `src/input.cpp:171`, `src/input.cpp:263`, `src/path_info.cpp:304`

`input_manager::init()` reads only `data/raw/keybindings.json`, `data/raw/keybindings/vehicle.json` and the user file. The mod loader has no keybinding loader, and `handle_action` has no EOC dispatch. Mods cannot add hotkeys such as "cast blink" or "open my menu". Workarounds exist but cost two keypresses: spell-menu invlets, `ACTION_RECAST_SPELL`, and mutation-menu invlets.

```cpp
load( PATH_INFO::keybindings(), false );   // input.cpp:171; then :176 vehicle, :183 user file
```

**Fix:** Add a mod-loadable keybinding type with an EOC payload:
```json
{ "type": "keybinding", "id": "MOM_TELEPORT", "category": "DEFAULTMODE", "name": "Psionic teleport",
  "bindings": [ { "input_method": "keyboard_any", "key": "T", "mod": [ "ctrl" ] } ],
  "effect_on_condition": "EOC_MOM_TELEPORT_MENU", "condition": { "u_has_trait": "TELEPORTER" } }
```
Load these after the vanilla defaults and before the user file, so player rebinds still win. Dispatch them from the default branch of `handle_action`.

### UIHUD-J11 · Numeric widgets are integer-only and built-in var ranges cannot be overridden
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/widget.cpp:461`, `src/widget.cpp:470`, `src/widget.cpp:671`, `src/widget.cpp:728`, `src/widget.cpp:976`, `src/widget.cpp:1488`, `doc/WIDGETS.md:976`

Problems:
- custom_var `value`, `range` and `norm` are cast to int, and numbers print with `%d`, so 0.45, 12.7 kg or 36.6 C show as 0, 12 or 36.
- Built-in var ranges are hardcoded in `set_default_var_range` (pain max 80, sleepiness 1000, etc.). The docs say they cannot be customized.
- `mood` loads but always returns 0, and `cardio_acc` has no range.

`breaks` and rebuilding the var with custom math partly work around this.

```cpp
wgt._var_min = static_cast<int>( min.evaluate( d ) );   // widget.cpp:464
string_format( "%d", value )                             // :1490
case widget_var::pain: _var_min = 0; _var_max = 80;     // :756-761
```

**Fix:** Accept `"range": [min, (norm), max]` (dbl_or_var) on any widget, overriding the defaults. Keep values as doubles internally and add `"precision": 1`, `"scale": 0.001` and `"format": "%s kg"`, or `"units": "mass" | "energy" | "temperature"`. Remove `mood` or make loading it an error.

### UIHUD-J12 · Message 'high' filter is hardcoded to effect weed_high, with untranslated strings
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/messages.cpp:43`, `src/messages.cpp:192`, `src/messages.cpp:255`, `src/messages.cpp:342`

Every player message goes through `modify_msg_with_exclamations`. That function checks only `weed_high`, uses a hardcoded frequency curve, and inserts English strings that are not wrapped in `_()`. Mods cannot reuse it for drunkenness, hallucinogens or psychic static. Because it runs before coalescing, it also stops identical messages from merging.

```cpp
if( !p_char->has_effect( effect_weed_high ) ) { return; }                 // :258
std::vector<std::string> exclamations = { "Woah.", "Dude." };             // :342, no _()
```

**Fix:** Move the filter to an effect_type field backed by snippets, which also makes the text translatable:
```json
"message_filter": { "insert_snippets": "weed_smileys", "insert_chance": { "math": [ "..." ] },
  "suffix_snippets": { "good": "weed_good", "bad": "weed_bad", "neutral": "weed_neutral" },
  "skip_words": [ "that", "with" ], "replace": [ [ "s", "sh" ] ] }
```

### UIHUD-J13 · run_eoc_selector exposes only part of uilist and cannot report a cancel
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctalk.cpp:6644`, `src/npctalk.cpp:6711`, `src/npctalk.cpp:6787`, `src/npctalk.cpp:6800`, `src/uilist.h:72`, `src/uilist.h:471`

The selector exposes names, descriptions, keys, title, `hide_failing`, `allow_cancel` and `hilight_disabled`. It does not expose uilist's second column (`ctxt`), the footer or categories. A cancel just returns, so no EOC runs, and entries are fixed at load. Workarounds: set a variable before the menu and clear it in each option to detect a cancel, and use `<color_>` tags in names for per-entry colors.

```cpp
eoc_list.query(); if( eoc_list.ret < 0 ) { return; }   // npctalk.cpp:6800-6803
```

**Fix:** Add optional parallel arrays `"columns"` (str_or_var, mapped to `ctxt`) and `"colors"`, a `"footer"`, `"cancel_eocs": [...]`, and `"result_var": { "context_val": "choice" }` (-1 on cancel). Add a dynamic form `"entries_from": { "global_val": "waypoints" }` that runs one EOC per array element, passing the element as a context var.

### UIHUD-J14 · Overmap renderer hardcodes horde glyph tiers, mongroup ids and forest-trail masking
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/overmap_ui.cpp:2650`, `src/overmap_ui.cpp:1135`, `src/overmap_ui.cpp:1139`, `src/overmap_ui.cpp:2693`, `src/overmap_ui.cpp:1981`, `src/overmap_ui.cpp:1984`, `src/overmapbuffer.cpp:983`, `src/panels.cpp:292`

These overmap visuals and costs are fixed in code:
- **Horde glyphs.** They use a fixed 10-tier size table (z/Z, green to red) for every horde type, so bandit or robot hordes draw as green `z`. The minimap has its own hardcoded `z`/`Z`.
- **Mongroup ids.** The debug mongroup view special-cases `GROUP_FOREST`/`GROUP_NEMESIS`.
- **Forest trails.** `forest_trail` is masked as `forest` by hardcoded type.
- **Auto-travel costs.** Water (100) and air (8) are set in code.

```cpp
if( horde_size < 5 ) { c_light_green, "z" } else if( horde_size < 13 ) ...   // :2652-2681
cur_ter->get_type_id() == oter_type_forest_trail                              // :2694
```

**Fix:**
- Add `"overmap_display": { "symbol": "Z", "tiers": [ [5, "z", "light_green"], ... ] }` to monstergroups, falling back to the vanilla table.
- Add `"hide_as": "forest"` on oter types, governed by the existing show-forest-trails option.
- Move the auto-travel terrain costs into `overmap_path_params` JSON.

### UIHUD-J15 · INATTENTIVE trait id is hardcoded in 7 UI spots instead of being a flag
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/surroundings_menu.cpp:1021`, `src/monster.cpp:891`, `src/monster.cpp:1013`, `src/monster.cpp:1103`, `src/map_entity_stack.cpp:256`, `src/cata_tiles.cpp:4458`, `src/cata_tiles.cpp:4539`

Whether the "monster sees you" marker is hidden depends on exactly one trait id. A mod's own trait, effect or item (blindfold, rage) cannot hide it without granting the vanilla trait. MindOverMatter and Xedra already add and remove INATTENTIVE through EOCs to get this behavior.

```cpp
const bool inattentive = you.has_trait( trait_INATTENTIVE ); bool sees = mon->sees( get_map(), you ) && !inattentive;
```

**Fix:** Replace the trait checks with a character flag `UNAWARE_OF_DETECTION`, checked with `has_flag`. Add the flag to the INATTENTIVE mutation.

### UIHUD-J16 · WIDGETS.md is out of date: wrong var names, about 20 vars undocumented, false Magiclysm example
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `doc/WIDGETS.md:1065`, `doc/WIDGETS.md:1071`, `doc/WIDGETS.md:125`, `doc/WIDGETS.md:681`, `doc/WIDGETS.md:1257`, `src/widget.h:31`, `src/widget.cpp:537`

Errors in WIDGETS.md:
1. It documents `bp_outer_armor_text`; the enum is `bp_armor_outer_text`.
2. It lists `move_mode_letter` and `move_mode_text` as vars, but using them as `var` throws (`move_mode_letter` is only a widget id).
3. 21 vars are missing from the tables (oxygen, carry_weight*, faction_territory, snow_depth_text, wielding_*_text, power_percentage, sundial_time_text, veh_battery_text, max_mana, body_graph*, and others).
4. The Optional fields table omits `body_graph`, `description`, `custom_var`, `default_clause` and `bodyparts`. The last three have their own sections, but `body_graph` is never mentioned.
5. The Magiclysm example claims the mod extends `custom_sidebar`; it does not.
6. `colors` needs full names (`c_green`, through `name_to_color`), while clause `color` accepts short names. The doc does not explain the difference.

**Fix:** Generate the Variables table from the `widget.h` enum, or add a test that diffs `enum_to_string<widget_var>` against the doc. Fix the names, document the missing fields, and update the Magiclysm example. Make `colors` use `color_from_string` so both fields accept the same names.

### UIHUD-J17 · Body graph colors and the 'status' view are hardcoded to three effects and fixed cut-offs
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/display.cpp:1513`, `src/display.cpp:977`, `src/display.cpp:960`, `src/display.cpp:936`, `src/display.cpp:1546`, `src/display.h:30`, `src/widget.h:139`, `src/widget.cpp:280`

`get_bodygraph_bp_color()` hardcodes all of its rules:
- HP bands at 12.5/37.5/62.5/87.5%;
- encumbrance at 10/40/70;
- the BODYWET constants;
- a special case for the eyes in temperature view;
- a "status" view that checks only `bleed`, `bite` and `infected`.

A mod's own limb effect shows in the clause-driven text status widget but never colors the graph. The views are a fixed enum, so no new view can be added. The `bodypart_status` enum in `widget.h` is dead code. The impact is cosmetic.

```cpp
get_effect_int( effect_bleed, bp ) ... has_effect( effect_bite, bp.id() ) ... has_effect( effect_infected, bp.id() )  // display.cpp:984-987
```

**Fix:** Let body_graph widgets take per-part clauses evaluated with the `_widget` context. The first match wins, and `default_clause` is the fallback:
```json
"var": "body_graph", "part_clauses": [
  { "color": "light_blue", "condition": { "u_has_effect": "frostbite", "bodypart": { "context_val": "widget" } } },
  { "color": "red", "condition": { "math": [ "u_hp(_widget) < u_hp_max(_widget) * 0.125" ] } } ]
```
Rewrite the built-in views as vanilla JSON templates.

### UIHUD-J18 · Text-function widget vars hardcode their strings, cut-offs and colors
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/widget.cpp:1383`, `src/display.cpp:465`, `src/display.cpp:538`, `src/display.cpp:577`, `src/display.cpp:860`, `src/display.cpp:1135`, `src/display.cpp:1458`, `src/display.cpp:1627`

About 40 `*_text` vars use fixed English labels, cut-offs and colors. Examples: pain bands, weariness, power, carry weight 25/50/75/100%, the safe-mode ramp, snow depth, compass danger at difficulty 30/16/8, and NPC `@` colors by attitude. The widget `colors` field can only replace all of them with `_colors.front()`. Most of these vars can be rebuilt from math values. The compass/legend thresholds and NPC colors cannot.

```cpp
if( !_colors.empty() ) { ret += colorize( desc.first, _colors.front() ); }   // widget.cpp:1383-1384
get_total_difficulty() >= 30 ... >= 16 ... >= 8                             // display.cpp:1488-1495
```

**Fix:** Let `clauses` with `clause_mode: threshold` (J05) override the text and color, using the underlying numeric value. For the compass legend, add:
```json
"danger_thresholds": [ [30, "red"], [16, "light_red"], [8, "white"] ],
"npc_colors": { "hostile": "red", "follower": "light_green", "other": "pink" }
```

## 2. Bugs, oversights & quirks

### UIHUD-B01 · Per-character safemode rules are never saved: the save-exists check uses || where && is needed
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/safemode_ui.cpp:780-785`, `src/safemode_ui.cpp:480-486`, `src/game_io.cpp:601-624`, `src/game_io.cpp:754`, `src/auto_pickup.cpp:908-912`, `src/auto_note.cpp:46-47`, `src/surroundings_menu.cpp:1343-1345`

`safemode::save(true)` returns early, reporting success, unless *both* `<char>.sav` and `<char>.sav.zzip` exist. A world writes only one of the two, so the per-character `.sfm.json` is never written. To reproduce:
1. Add a rule on the Character tab and answer "Save changes?" Yes, or use `SAFEMODE_BLACKLIST_ADD` in the surroundings menu.
2. Save and reload.

The rules are gone and no error is shown. auto_pickup and auto_note use the correct `&&`.

```cpp
if( !file_exist( PATH_INFO::player_base_save_path() + ".sav" ) ||
    !file_exist( PATH_INFO::player_base_save_path() + ".sav" + zzip_suffix ) ) { return true; }  // safemode_ui.cpp:782
if( !file_exist( player_save ) && !file_exist( player_save_zzip ) ) { return true; }           // auto_pickup.cpp:911
```

**Fix:** Change `||` to `&&`. Consider a one-time warning for players whose rules were dropped.

### UIHUD-B02 · Clause-driven column layout crashes (divide by zero) when no clause is true and there is no default_clause
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/widget.cpp:1849`, `src/widget.cpp:1866-1875`, `src/widget.cpp:1620-1625`, `src/widget.cpp:1514-1520`

When every clause condition of a layout is false, `widgets_cond()` returns `_default_clause.widgets`, which is empty if there is no `default_clause`. The columns branch then logs a debugmsg and goes on to compute `avail_width / num_widgets` with `num_widgets == 0`. Core layouts all use exhaustive A / not-A clauses, which hides the bug. A modder's conditional row (e.g. shown only while `u_has_effect`) crashes the game when its condition turns false. Xedra's blank-line `default_clause` looks like a workaround for this. Rows layouts survive because their loop simply does nothing.

```cpp
const int num_widgets = wgts.size();
if( num_widgets == 0 ) { debugmsg( "widget layout has no widgets" ); }
...
const int child_width = avail_width / num_widgets;   // widget.cpp:1866-1875
```

**Fix:** When `wgts` is empty, return `""` (height 0 for `W_DYNAMIC_HEIGHT`, and honor `W_DISABLED_WHEN_EMPTY`). Keep the debugmsg only for layouts that have no clauses. Document that conditional layouts may be empty.

### UIHUD-B03 · ASCII overmap loads terrain for UNSEEN tiles (inverted check, unused result), which generates whole overmaps
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/overmap_ui.cpp:1111-1120`, `src/overmap_ui.cpp:963`, `src/overmap_ui.cpp:1182-1183`, `src/overmap_ui.cpp:1324-1337`, `src/overmapbuffer.cpp:889-893`, `src/overmapbuffer.cpp:100-122`, `src/overmapbuffer.cpp:867-873`

_Merges a bug finding and a performance finding about the same lines._ The comment in `draw_ascii` says "Only load terrain if we can actually see it", but the code calls `overmap_buffer.ter(omp)` only when `vision == unseen`. `seen()` returns unseen for overmaps that do not exist yet. `ter()` goes through `get_om_global`/`get()`, which construct and `populate()` them. So scrolling the ASCII overmap into unexplored space generates full overmaps (cities, specials, NPCs), causing stalls, save growth and a different generation order. The result is never needed: it goes into `ccur_ter`, which nothing reads. Every other `ter()` call in overmap_ui is guarded by `!= unseen`.

This affects only the ASCII path: curses builds, or tiles with `USE_OVERMAP_TILES` off. Each overmap is generated once and then cached. Once generated, the call is still a wasted lookup for every unseen tile on every redraw.

```cpp
if( vision == om_vision_level::unseen ) {
    // Only load terrain if we can actually see it
    cur_ter = overmap_buffer.ter( omp );
}
...
ccur_ter = cur_ter;   // :1183, never read
```

**Fix:** Delete `cur_ter`/`ccur_ter` and the lookup (lines 963, 1111, 1117-1120, 1183). If terrain is ever needed there, use `ter_existing()` and only for seen tiles.

### UIHUD-B04 · carry_weight widget var divides by weight_capacity() with no zero guard (the text version guards it)
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/widget.cpp:968-970`, `src/display.cpp:1139-1143`, `src/character.cpp:2856-2870`, `src/character.cpp:7745-7748`, `src/units.h:255-258`, `data/json/ui/structured/structured_speed_vehicle.json:18`, `data/json/ui/structured/structured_speed_vehicle.json:116-120`, `src/medical_ui.cpp:351-352`, `src/character.cpp:8057`

`weight_capacity()` clamps negative results to 0 g. Stacked non-exclusive CARRY_WEIGHT multipliers can produce that: SMALL2 -0.5, HOLLOW_BONES -0.4 and BADBACK -0.35 give a factor of -0.25. `widget::get_var_value` then does an integer `units::mass` division by zero (SIGFPE) on the next redraw of any sidebar with a `carry_weight` graph, including the shipped structured sidebar. The text path guards this case. The same unguarded division also appears in `medical_ui.cpp:352` and `character.cpp:8057`.

```cpp
value = ( 100 * ava.weight_carried() ) / ava.weight_capacity();                         // widget.cpp:969
if( u.weight_capacity() > 0_gram ) { carry_wt = ( 100 * u.weight_carried() ) / u.weight_capacity(); } else { carry_wt = 100; }  // display.cpp:1139
```

**Fix:** Add one shared helper that returns 100 (or a "max" sentinel) when capacity <= 0, and use it at all four sites.

### UIHUD-B05 · Graph widget divides by zero with one symbol (the default), or with 'pool' fill and width 0
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/widget.cpp:480`, `src/widget.cpp:1665`, `src/widget.cpp:1690-1692`, `src/widget.cpp:1704-1706`, `src/widget.cpp:1408-1410`, `doc/WIDGETS.md:380-388`

`depth = utf8_width(_symbols) - 1`. `symbols` defaults to `"-"`, so depth is 0, and bucket fill crashes with `value / depth` on the first redraw. The docs even present a one-symbol graph as valid. `pool` fill divides by `_width`, which defaults to 0. No shipped content triggers either case, and the crash shows up as soon as a modder first displays the widget.

```cpp
optional( jo, was_loaded, "symbols", _symbols, "-" );   // :480
int depth = utf8_width( _symbols ) - 1;                 // :1665
quot = value / depth; rem = value % depth;              // :1691-1692
quot = value / w;                                       // :1705
```

**Fix:** At load, require at least 2 symbols for graphs and a width/height above 0 (see J01). In `graph()`, return the empty-symbol string when depth or w is 0 or less.

### UIHUD-B06 · Sidebar overmap widget cache is never invalidated: hordes, notes, danger marks and newly seen tiles go stale
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/display.cpp:1284-1295`, `src/display.cpp:1303-1305`, `src/display.cpp:1336`, `src/display.h:63-66`, `src/display.cpp:81-84`, `src/display.h:243`

`colorized_overmap_text` (var `overmap_text`) caches its output keyed only on center OMT, mission target and width. The output also depends on:
- hordes (`showhordes = true`);
- notes and danger marks;
- vehicles;
- seen state and sight range.

`display::invalidate_overmap_cache()` has no callers. The minimap keeps showing a horde that walked past, a deleted note, or the pre-nightfall view until the player crosses an OMT boundary. The cache is file-static, so it can also survive loading another save at the same coordinates.

```cpp
bool is_valid_for( const tripoint_abs_omt &center, const tripoint_abs_omt &mission, int width ) const {
    return _center == center && _mission == mission && _width == width; }   // display.h:64
```

**Fix:** Add `calendar::turn` and height to the cache key, or call `invalidate_overmap_cache()` from note, seen and horde changes and on game load.

### UIHUD-B07 · Keybinding editor overwrites `changed`, so a cancelled edit loses earlier edits and skips the save prompt
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/input_context.cpp:1037-1039`, `src/input_context.cpp:1170-1179`, `src/input_context.cpp:1188-1196`, `src/input_context.cpp:967-997`, `src/input_context.cpp:937-965`, `src/input_context.cpp:884-903`

`display_menu` assigns `changed = action_*(...)` instead of OR-ing the result. Those functions return false when the user declines a prompt or picks a key that is already used. To reproduce:
1. Add a key to action A.
2. Try a conflicting key on action B and answer No.
3. Press Esc.

There is no "Save changes?" prompt and no revert. A's binding stays live this session but is never written. `display_menu` is the only caller of `inp_mngr.save()`, so the edit is lost on restart.

```cpp
changed = action_remove( name, action_id, is_local, is_empty );   // :1171
changed = action_reset( action_id );                              // :1173
changed = action_add( name, action_id, is_local, kb_menu.status ); // :1179
if( changed && query_yn( _( "Save changes?" ) ) ) {               // :1188
```

**Fix:** Use `changed |= ...`, or compare `inp_mngr.action_contexts` with `old_action_contexts` on exit.

### UIHUD-B08 · Overmap 'auto-travel only' toggle is unreachable: hint uses TOGGLE_OVERMAP_ONLY_TRAVEL, handler uses unbound TOGGLE_FAST_TRAVEL
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/overmap_ui.cpp:276`, `src/overmap_ui.cpp:2174`, `src/overmap_ui.cpp:2177`, `src/overmap_ui.cpp:2391-2392`, `data/raw/keybindings.json:1554-1563`

`keybindings.json` binds `TOGGLE_OVERMAP_ONLY_TRAVEL` to `x` / shift+x, and the sidebar hint advertises it. The OVERMAP context never registers that action. It registers (twice) and handles `TOGGLE_FAST_TRAVEL`, which has no binding anywhere in `data/`. Pressing `x` does nothing, so `uistate.overmap_only_auto_travel` can only be changed by manually binding the raw action.

```cpp
print_hint( "TOGGLE_OVERMAP_ONLY_TRAVEL", ... );                       // :276
} else if( action == "TOGGLE_FAST_TRAVEL" ) { uistate.overmap_only_auto_travel = !uistate.overmap_only_auto_travel;  // :2391
```

**Fix:** Register and handle `TOGGLE_OVERMAP_ONLY_TRAVEL`, and drop the duplicate `TOGGLE_FAST_TRAVEL` registration.

### UIHUD-B09 · Panel options fallback sets an invalid layout id "labels" and drops every other layout's saved panel config
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/panels.cpp:544-555`, `src/panels.cpp:424-433`, `src/panels.cpp:476-485`, `src/panels.cpp:398-410`, `src/panels.cpp:500-537`, `src/panels.cpp:879-881`, `src/game.cpp:705-707`

`panel_options` is global, but the available layouts depend on the world's mods. If the saved layout is missing, `deserialize` sets `current_layout_id = "labels"` and returns early. `"labels"` is a label, not an id (the id is `legacy_labels_sidebar`). As a result:
- `get_current_layout()` fires "Invalid current panel layout, defaulting to classic".
- The early return skips restoring the order and toggles of every other layout.
- The next QUIT from the panel options menu saves the defaults over the player's settings.

To reproduce: select Xedra's `vampire_sidebar`, then load a world without Xedra.

```cpp
if( layouts.find( current_layout_id ) == layouts.end() ) { ... current_layout_id = "labels"; return; }  // panels.cpp:549
debugmsg( "Invalid current panel layout, defaulting to classic" ); current_layout_id = "legacy_classic_sidebar";  // :430
```

**Fix:** Fall back silently to the platform default id. Keep parsing the remaining layouts, and preserve the raw JSON for layouts that are not loaded when re-saving.

### UIHUD-B10 · Keybinding save writes every default binding into the user file, freezing defaults forever
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/input.cpp:379-457`, `src/input.cpp:357-376`, `src/input.cpp:181-183`, `src/input_context.cpp:1188-1191`

`input_manager::save()` writes every action in every context, including untouched defaults. On load, each action in the user file replaces its defaults. Once a player rebinds a single key, all defaults are snapshotted. New default keys added later to existing actions (by fork updates or fixes such as B08) never reach that player. Only new action ids get defaults. The `version` field is used only for old-format migration.

```cpp
for( const auto &a : action_contexts ) { ... for( const auto &action : actions ) { ... jsout.member( "id", action.first );  // save(), no diff vs basic_action_contexts
attributes.input_events = events;   // load()
```

**Fix:** In `save()`, skip actions whose `input_events` and `is_user_created` match `basic_action_contexts`. Write an explicit empty bindings array only for actions the user deliberately unbound.

### UIHUD-B11 · display::activity_level_str indexes a 6-element array with floor(level/2); exertion >= 12 reads out of bounds
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/display.cpp:606-621`, `src/display.cpp:631-634`, `src/widget.cpp:1224-1225`, `src/avatar_action.cpp:272-273`, `data/json/enchantments.json:366`, `data/json/ui/sidebar-mobile.json:1884-1928`

`idx = floor(level / 2)` indexes a `std::array<translation, 6>` with no clamp. Activity level is not capped at 10: running (8) in an inactive combat exoskeleton (x1.5) gives 12, which is index 6. A negative multiplier gives a negative index. No shipped widget uses the `activity_text` var (sidebar-mobile uses clauses on `u_val('activity_level')`), and the other callers pass values of 10 or less. So this hits only mods or custom widgets that use the documented var. `Character::activity_level_str` handles the same input safely.

```cpp
int idx = std::floor( level / 2 );
return activity_descriptions[idx].translated();   // display.cpp:618-620, array size 6
```

**Fix:** Clamp idx to [0, 5] or reuse `Character::activity_level_str`. Also clamp `Character::activity_level_index()`, which EOCs reach through `get_activity_level`.

### UIHUD-B12 · Graph widgets ignore var_min, so custom-var ranges and negative ranges render wrong despite the docs
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/widget.cpp:1662-1683`, `src/widget.cpp:461-468`, `src/widget.cpp:1437-1454`, `doc/WIDGETS.md:983`, `doc/WIDGETS.md:1108-1110`

The docs say `var_min` maps to the graph's zero point. `widget::graph` scales only by `value / _var_max` and clamps negatives to 0. Examples:
- A custom range [50, 150] at value 100 draws 67% instead of 50%.
- A morale graph (range -100..100) is empty for any negative value.

`value_color` does use `var_min`, so a graph's fill and its color disagree. No vanilla graph is affected.

```cpp
if( _var_max > 0 && _var_max != max_graph_val ) { value = max_graph_val * value / _var_max; }  // :1671
// Negative values are not (yet) supported
if( value < 0 ) { value = 0; }                                                               // :1677
```

**Fix:** When both bounds are defined, scale as `max_graph_val * (value - _var_min) / (_var_max - _var_min)` using 64-bit intermediates.

### UIHUD-B13 · uilist filter matches against raw text including color tags (e.g. 'light' matches every colored entry)
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/uilist.cpp:547-554`, `src/uilist.cpp:333-339`, `src/cata_utility.cpp:89-116`, `src/color.cpp:742-753`, `src/overmap_ui.cpp:898-900`

`filterlist` runs `lcmatch`/`find` on `entries[i].txt`, which still contains markup such as `<color_c_light_gray>`. Typing `light`, `gray` or `color` therefore matches every colorized entry, for example all notes in the overmap notes list. This affects every uilist with colored entries.

```cpp
visible = lcmatch( entries[i].txt, filter );                     // :550
visible = entries[i].txt.find( filter ) != std::string::npos;    // :553
```

**Fix:** Filter on `entries[i]._txt_nocolor()` (or `remove_color_tags(txt)`) in both branches.

### UIHUD-B14 · Message cooldown (MESSAGE_COOLDOWN) is ignored when the sidebar log flows from the top
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/messages.cpp:941-968`, `src/messages.cpp:970-983`, `src/panels.cpp:349`

`Messages::display_messages` skips cooldown-hidden messages only in the bottom-up branch. With `LOG_FLOW=new_top`, spammy repeats are all printed in the sidebar messages panel and in advanced_inv. The cooldown option silently does nothing there.

```cpp
if( m.is_in_cooldown() ) { continue; }          // bottom-up branch only
if( message_exceeds_ttl( m ) ) { break; }       // the only check in the log_from_top loop
```

**Fix:** Add the same `if( m.is_in_cooldown() ) continue;` to the log_from_top loop.

### UIHUD-B15 · Widget 'breaks' length mismatch only warns, then reads out of bounds; 'breaks' cannot be overridden alone
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/widget.cpp:533-547`, `src/widget.cpp:1443-1450`, `src/flexbuffer_json-inl.h:810-816`, `src/flexbuffer_json.cpp:249-289`

If `breaks.size() != colors.size() - 1`, load only calls debugmsg. `value_color` then reads `_breaks[i]` past the end when breaks is shorter (UB, random thresholds). `breaks` is parsed only inside the `colors` block. A `breaks` without `colors` is reported as an unvisited member, so a copy-from widget cannot retune thresholds alone. Overriding only `colors` clears the inherited breaks (line 535). The impact is low, because it requires malformed JSON that already raises an error. See also J01 and J05.

```cpp
if( _breaks.size() != _colors.size() - 1 ) { debugmsg( "Widget property 'breaks' must have one element less than 'colors'" ); }  // :543
for( int i = 0; i < color_max; i++ ) { if( value_offset < _breaks[i] ) {   // :1445
```

**Fix:** Make the mismatch a `jo.throw_error`, loop to `min(color_max, _breaks.size())`, and read `breaks` independently of `colors`.

### UIHUD-B16 · Second, divergent overmap-note symbol/color parser in display.cpp (teleporter preview)
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/display.cpp:1192-1282`, `src/display.cpp:1224`, `src/display.cpp:1244`, `src/display.cpp:1255`, `src/panels.cpp:257`, `src/magic_teleporter_list.cpp:195`, `src/overmap_ui.cpp:505-540`, `src/color.cpp:534-541`

`display::overmap_note_symbol_color` is used only by `draw_overmap_chunk_imgui`, i.e. the teleporter list. It parses notes differently from `overmap_ui::get_note_display_info`:
- **Symbol position.** It uses different position limits: `' X:base'` shows `N` here but `X` on the overmap.
- **Color.** It uses only the first character of the color name (`red;` becomes light red), while the overmap does an exact lookup.
- **Empty color.** For `;foo` or `X:;`, `sym[0]` is read from an empty view (technically UB, cosmetic in practice).

```cpp
std::string_view sym = note_text.substr( colorStart, colorIndex - colorStart );   // :1244
char colorID = sym[0];                                                            // :1255, no empty check
```

**Fix:** Delete `display::overmap_note_symbol_color` and use `get_note_display_info` + `get_note_color` everywhere.

## 3. Performance

### UIHUD-P01 · Surroundings menu redraws the whole game map and sidebar about every 10 ms while idle
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/surroundings_menu.cpp:186`, `src/surroundings_menu.cpp:1195`, `src/surroundings_menu.cpp:1199`, `src/surroundings_menu.cpp:1201`, `src/surroundings_menu.cpp:1207`, `src/input_context.cpp:486`, `src/ui_manager.cpp:482`, `src/game.cpp:3403`, `src/game.cpp:6588`

The V menu sets a 10 ms input timeout and calls `g->invalidate_main_ui_adaptor()` on every loop pass, including TIMEOUT passes where nothing changed. The menu is only `TERMX/3` wide, so each tick runs the full `game::draw()`: `build_map_cache`, `draw_ter` and `draw_panels(true)` (which recomputes every sidebar widget). That is up to about 100 full redraws a second, for as long as the menu is open. Imgui adaptors always redraw, so the menu's own per-frame work (P02) runs at the same rate.

```cpp
ctxt.set_timeout( 10 );   // :186 "arbitrary timeout to fix imgui input weirdness"
while( true ) { update_path_end(); g->invalidate_main_ui_adaptor(); ui_manager::redraw(); ...
    action = get_selected_data()->ctxt.handle_input();   // :1197-1207
```

**Fix:** Invalidate the main UI only when `path_end`, the selected entry or `view_offset` actually changes, and skip that work on `TIMEOUT`. Better still, raise the timeout to `BLINK_SPEED` or remove it, and pump ImGui only on input. The idle menu should then cost almost nothing.

### UIHUD-P02 · Surroundings menu rebuilds item::info / extended_description and every list row each frame
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/surroundings_menu.cpp:853`, `src/surroundings_menu.cpp:939`, `src/surroundings_menu.cpp:944`, `src/surroundings_menu.cpp:1000`, `src/surroundings_menu.cpp:1022`, `src/surroundings_menu.cpp:1078`, `src/item_info.cpp:4516`, `src/item_info.cpp:4250`, `src/item_info.cpp:4284`

`draw_item_tab` submits every filtered entry each frame with no `ImGuiListClipper`. Each row costs a `get_category()` call, a recursive newness check, a trimmed-text measurement and two colored-text draws. The info pane re-runs the full `item::info(true, …)` every frame, which includes a `can_craft_recipe` scan and a scan of all vpart_info entries. The monster tab re-runs `extended_description()` plus a `sees()` line-of-sight check for every row. This cost remains even after P01 is fixed.

```cpp
for( map_entity_stack<item> *it : item_data.filtered_list ) {          // :853, no clipper
selected_it->info( true, selected_info ); ... display_item_info( ... ); // :939-944
bool sees = mon->sees( get_map(), you ) && !inattentive;                // :1022
```

**Fix:** Cache the selected entry's iteminfo vector or description string, keyed on the entity pointer and `calendar::turn`. Wrap the row loops in `ImGuiListClipper`, using a precomputed row index for category separators. Cache `sees()` per turn.

### UIHUD-P03 · Overmap search scans 3.24M tiles with per-tile string copies and re-parses the query each time
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/overmap_ui.cpp:1519`, `src/overmap_ui.cpp:1520`, `src/overmap_ui.cpp:1528`, `src/overmap_ui.cpp:1537`, `src/cata_utility.cpp:123`, `src/cata_utility.cpp:89`, `src/map_iterator.h:171`, `src/overmapbuffer.cpp:327`

SEARCH visits `points_in_radius(curs, OMAPX*5)`, which is 1801² ≈ 3.24M OMTs. Each tile costs an overmap lookup and a linear `std::find` over `overmap_checked` (up to 121 entries). Each seen tile also costs a by-value translated `get_name()` and a `match_include_exclude` call, which copies and re-splits the filter and converts both strings to lowercase UTF-32. In explored worlds, each search freezes noticeably.

```cpp
const int radius = OMAPX * 5; for( const tripoint_abs_omt &p : points_in_radius( curs, radius ) ) {
    ... std::find( overmap_checked.begin(), overmap_checked.end(), om_cache ) ...
    match_include_exclude( om_loc.om->ter( om_relative )->get_name( vision ), term )   // :1520-1537
```

**Fix:** Loop over the at most 11x11 overmaps in range, call `find_notes` once per overmap, then loop over local tiles clipped to the radius. Memoize matches per `(oter_id, vision)`. Split and lowercase the search terms once.

### UIHUD-P04 · Sidebar layout deep-copies every child widget on every redraw, then writes heights back with a linear scan
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/widget.cpp:1062`, `src/widget.cpp:1857`, `src/widget.cpp:1894`, `src/widget.cpp:1197`, `src/widget.cpp:1864`, `src/widget.cpp:1917`, `src/widget.cpp:574`, `src/widget.cpp:615`, `src/panels.h:55`

`custom_draw_func` and `widget::layout` copy each child by value (`widget x = id.obj()`). That copies about 10 strings, translations, vectors, and clauses with heap-copied `std::function` conditions. `ma_buff_legend` alone has 147 clauses. `set_height_for_widget` then scans all ~1247 widgets (cheap interned-id compares). Each column child also builds an unused debug string. This happens on every main redraw: `draw_panels(true)` is forced at `game.cpp:3403`, which makes the per-turn `draw_this_turn` guard dead. The total is a few thousand small allocations per redraw, which is minor next to `draw_ter`.

```cpp
widget row_widget = row_wid.obj();   // :1062
widget cur_child = wid.obj();        // :1857, :1894
debug_widths.append( string_format( "%s: %d,", wid.str(), cur_width ) );   // :1917, used only in a debugmsg
```

**Fix:** Work on the factory instance, as `draw_args::get_widget()` already does: `widget &cur_child = const_cast<widget &>( wid.obj() );`. That turns `set_height_for_widget` into an O(1) write. Build `debug_widths` only inside the `total_width > max_width` branch.

### UIHUD-P05 · Widget clause conditions build a fresh dialogue (two heap talkers and a context map) per clause per body part per redraw
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/widget.cpp:344`, `src/widget.cpp:352`, `src/widget.cpp:1649`, `src/widget.cpp:1552`, `src/widget.cpp:1606`, `src/npctalk.cpp:2748`, `src/avatar.cpp:1812`

`meets_condition` builds a new `dialogue` for every conditional clause and every body part. Each build does a `make_unique<talker_avatar>`, a const_clone and a context-var write. With `bodypart_status` (9 clauses across 6 parts) and `ma_buff_legend` (147 clauses), that is hundreds of constructions per sidebar redraw. Clauses with parse_tags build yet another dialogue.

```cpp
dialogue d( get_talker_for( get_avatar() ), nullptr ); d.reason = opt_var;
write_var_value( var_type::context, "widget", &d, opt_var ); return !has_condition || condition( d );   // :344-349
```

**Fix:** Build one dialogue per `get_clauses()` call (or per body part) and reuse it, overwriting only `reason` and the `widget` var. Optionally cache each widget's evaluated clause list per turn plus a UI-state generation counter.

### UIHUD-P06 · Overmap line-of-sight is computed for every tile in sight range every frame, even with hordes hidden
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/overmap_ui.cpp:2576`, `src/overmap_ui.cpp:2578`, `src/overmap_ui.cpp:2580`, `src/character.cpp:1227`, `src/sdltiles.cpp:3397`, `src/overmapbuffer.cpp:329`

`get_and_assign_los()` runs before the `blink && showhordes` check, so every tile within sight gets a `line_to` trace with `overmap_buffer.ter()` lookups, even though the result matters only on tiles with a visible-size horde. `has_trait(DEBUG_CLAIRVOYANCE)` also runs per tile. The tiles renderer has the same pattern. The cost is a few hundred to ~1700 short traces per frame, which is sub-millisecond.

```cpp
const bool can_see_horde = overmap_ui::get_and_assign_los( args.los, player_character, omp, opts.sight_points ) || debug_horde;
const bool show_hordes = blink && opts.showhordes && can_see_horde;   // :2578-2582
```

**Fix:** Compute the horde size first, under `blink && opts.showhordes`, and run the LOS check only when the size reaches `HORDE_VISIBILITY_SIZE`. Hoist `debug_horde` into `oter_display_options`. Apply the same change in sdltiles.

### UIHUD-P07 · cataimgui::draw_colored_text does about 50 allocations and a full fold per call, for every line of every imgui window each frame
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/cata_imgui.cpp:872`, `src/cata_imgui.cpp:886`, `src/cata_imgui.cpp:889`, `src/cata_imgui.cpp:895`, `src/output.cpp:1035`, `src/output.cpp:120`, `src/help.cpp:261`, `src/help.cpp:317`, `src/help.cpp:346`, `src/ui_manager.cpp:482`

Every call to `draw_colored_text` does avoidable work:
- `replace_colors()` takes the text by value and runs 16 `string_replace` passes, which build tag temporaries.
- `foldstring()` runs even when `wrap_width == 0`.
- Segments are copied by value (`for( auto seg : ... )`).

Help redraws every 50 ms and pushes all paragraphs through this path with no clipping. Each call costs only microseconds, so the per-frame total is small.

```cpp
const std::string &text = replace_colors( original_text );                        // :872
std::vector<std::string> folded_msg = foldstring( text, chars_per_line );          // :886
for( auto seg : color_segments ) {                                                 // :895
```

**Fix:** Skip `replace_colors` when the text has no `<`. Split on `\n` instead of folding when `wrap_width == 0`. Iterate segments by reference. Use a static table of prebuilt tag strings. Cache the folded paragraphs in help_window per (category, width).

### UIHUD-P08 · Surroundings item and terrain lists dedupe by linear vector search: O(items x unique names)
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/surroundings_menu.cpp:248`, `src/surroundings_menu.cpp:250`, `src/surroundings_menu.cpp:263`, `src/surroundings_menu.cpp:592`, `src/surroundings_menu.cpp:594`, `src/surroundings_menu.cpp:731`

When the menu opens, each item's `tname()` is looked up by `std::find` in a vector of all unique names, then looked up again in a `std::map`. Terrain and furniture use the same pattern. The order vector is redundant because the list is sorted afterwards. This runs once per menu open, and `tname()` dominates the cost.

```cpp
if( std::find( item_order.begin(), item_order.end(), name ) == item_order.end() ) { item_order.push_back( name ); items[name] = ...; } else { items[name].add_at_pos(...); }  // :250-256
```

**Fix:** Use `items.try_emplace( name, ... )` (or an `unordered_map`), iterate `items` directly, and hoist `get_map()` out of the loop.

### UIHUD-P09 · uilist copies each visible uilist_entry every frame, so the no-color cache never persists
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/uilist.cpp:124`, `src/uilist.cpp:165`, `src/uilist.cpp:169`, `src/uilist.cpp:64`, `src/uilist.cpp:204`, `src/uilist.cpp:333`, `src/uilist.h:175`

`draw_controls` copies every clipped row by value: txt, desc, ctxt, extratxt and the optional `input_event`. It then calls `_txt_nocolor()` on the copy, so the cache on the real entry is never filled and highlighted rows are re-stripped every frame. Categories (line 64) and the footer description (line 204) are copied too. The cost is small.

```cpp
auto entry = parent.entries[parent.fentries[i]];                                   // :124
if( is_selected || is_hovered ) { str1 = entry._txt_nocolor(); str2 = entry._ctxt_nocolor();   // :165-170
```

**Fix:** Use references (`uilist_entry &entry`, `const auto &cat`, `const std::string &description`) and pointers to the cached strings. Caveat: some callers mutate `entries[].txt`/`ctxt` between queries (`editmap.cpp:1125`, `wish.cpp:301/355`). The no-color cache must be invalidated when the source text changes, for example by storing the source string alongside it, or highlighted rows will show stale text.

### UIHUD-P10 · Overmap rendering does two or three linear scans of all notes for every tile
**Severity:** Low · **Effort:** Medium · **Verified:** plausible
**Where:** `src/overmap_ui.cpp:2599`, `src/overmap_ui.cpp:2617`, `src/overmap_ui.cpp:2620`, `src/overmap_ui.cpp:1108`, `src/sdltiles.cpp:3540`, `src/overmap.cpp:511`, `src/overmap.cpp:525`, `src/overmap.cpp:556`, `src/options.cpp:1774`

For most tiles, `oter_symbol_and_color` calls `is_marked_dangerous`, `has_note` and sometimes `note`. Each call walks the layer's whole `std::vector<om_note>`, so a frame costs O(tiles x notes). The tiles renderer does the same. The cost matters only for heavily noted overmaps: `AUTO_NOTES` defaults to off, so typical note counts are small.

```cpp
} else if( blink && overmap_buffer.is_marked_dangerous( omp ) ) {                                       // :2599
for( const om_note &i : layer[p.z() + OVERMAP_DEPTH].notes ) { if( i.p == p.xy() ) {                    // overmap.cpp:517
```

**Fix:** Precompute, once per frame, a map of notes that intersect the visible window plus a danger mask, the same way `npc_color` is precomputed today. Alternatively, keep a note index and a cached danger mask on `overmap::map_layer`.

### UIHUD-P11 · Every sidebar redraw re-runs the expensive display:: text providers (vehicle fuel/battery scans, weapon tname)
**Severity:** Low · **Effort:** Medium · **Verified:** plausible
**Where:** `src/game.cpp:3403`, `src/game.cpp:3414`, `src/game.cpp:3448`, `src/widget.cpp:1031`, `src/widget.cpp:1063`, `src/widget.cpp:1209`, `src/display.cpp:1105`, `src/display.cpp:1082`, `src/display.cpp:93`, `src/surroundings_menu.cpp:1199`

Widget output is never cached, and `draw_panels(true)` is forced on every `game::draw`. Every text widget therefore recomputes on every redraw, including animation frames and the P01 loop. The recomputed work includes fuel and battery capacity scans, `strain()`, weapon `tname()`, the compass legend and the bodygraph validity check. Most of these are cheap for typical cases, and the sidebar's share of a frame is unmeasured and probably small next to `draw_ter`.

```cpp
const int max_battery = veh->fuel_capacity( here, fuel_type_battery ); const int cur_battery = veh->fuel_left( here, fuel_type_battery );  // display.cpp:1105-1106
draw_panels( true );   // game.cpp:3403
```

**Fix:** Cache each widget's rendered string keyed on `(calendar::turn, moves, ui-dirty counter)`, or restore the `draw_this_turn` logic with a real dirty flag.

## Appendix: considered and rejected

- Overmap imgui sidebar recomputes the tile description (city search and sort) and about 25 keybinding hints at about 33 FPS – the work happens every frame but costs microseconds (a few dozen cities from about 3x3 overmaps, plus small map lookups), well under 1 ms.
- `oter_symbol_and_color` recomputes `line_to(center, mission)` for every tile – this only happens when `mission_inbounds` is false. The main overmap view never sets it, `draw_overmap_chunk` is dead code, and the only live path returns early through `disp_om_cache` unless the center, mission or width changed.
