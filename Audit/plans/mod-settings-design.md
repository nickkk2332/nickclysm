# Design: mod settings (DATA-J01)

**Status:** built (all four phases), on the decisions made with the owner on 2026-11-03
(below). The three open points at the end were accepted as proposed. The doc for mod authors is
[doc/JSON/MOD_SETTINGS.md](../../doc/JSON/MOD_SETTINGS.md).

## The problem today

Mods can't offer settings to players (finding DATA-J01 in [04-data-loading-saves.md](../04-data-loading-saves.md)):

- **World creation sees only core data.** Only `data/core` is loaded at the main menu, so world creation never shows an option or `option_slider` that a mod defines.
- **`EXTERNAL_OPTION` is hidden and global.** It is created hidden, on a pseudo-page. It is the same for every world, and changing it means editing the mod's JSON.
- **`WORLD_OPTION` is unusable.** It is undocumented, unused, and can only force an existing option to `"true"`.
- **EOCs can't write options.** `game_option()` in math is read-only.
- **Each mod reinvents a settings menu.** Some build one out of EOCs and global variables (hunvre's `mod_settings`, Aftershock's `options.json`), and such settings aren't visible at world creation.

## Decisions (2026-11-03)

1. **Where.** A per-mod settings tab at world creation, and the same settings in the in-game options menu. Each setting says whether it may change after the world exists (`"changeable_in_game"`). The others are fixed once the world is made, and the in-game menu shows them read-only.
2. **EOC writes.** EOCs can read every setting. They can write a setting only if the mod marks it writable.
3. **Scope.** Settings are per world, stored with the world like spawn density today.
4. **Types:**
   - on/off switch
   - number with min, max and step
   - choice from a list
   - free text

## Design

### Declaring a setting

A new JSON type, `mod_setting`, in the mod's own files:

```jsonc
{
  "type": "mod_setting",
  "id": "magiclysm_mana_regen",          // unique; mods prefix with their own id
  "name": "Mana regeneration",            // translated label
  "description": "Multiplies how fast mana comes back.",
  "kind": "number",                       // "bool", "number", "choice", "text"
  "default": 1.0,
  "min": 0.25, "max": 4, "step": 0.25,    // number only
  "choices": [ { "id": "rare", "name": "Rare" }, { "id": "normal", "name": "Normal" } ], // choice only
  "max_length": 40,                       // text only
  "changeable_in_game": true,
  "eoc_writable": false,
  "group": "Magic"                        // optional sub-heading inside the mod's tab
}
```

- **Why a new type?** `EXTERNAL_OPTION` stays as it is, for core's hidden balance knobs. A separate type keeps "a player sees this" explicit, and avoids changing what existing mods' `EXTERNAL_OPTION`s do.
- **Validation at load:**
  - the default lies in range and on a step
  - choice ids are unique
  - the id is unique across mods
  - a mod overriding another mod's setting uses `copy-from`, like other types

### World creation

- **Reading the settings early.** Once mods are picked on the mod tab, the world creator reads just the `mod_setting` objects of the selected mods. It reuses the loader's JSON walk but loads nothing else, so this stays fast.
- **The settings tab.** A "Mod settings" tab lists them per mod, with each mod's groups as sub-headings.
- **Changing the mod list** refreshes the tab and keeps values already chosen for mods still in the list.
- **Saving.** Values are saved in the world's `worldoptions.json` under a `mod_settings` object, keyed by setting id. Only values that differ from the default are stored, so a mod changing a default later still takes effect for worlds that never touched it.

### In game

- **Where they appear.** The options menu gets a "Mod settings" page with the same layout.
- **Locked settings.** A setting that isn't `changeable_in_game` is shown greyed out, with a tooltip saying why.
- **Hooks.** Changing a setting in game fires a new `mod_setting_changed` event (id, old value, new value), so mods can react. Statistics don't keep this event (see NPC section H's `stats_tracker::records()`).

### Reading and writing from JSON

- **Math.** `mod_setting('magiclysm_mana_regen')` returns a number; a bool reads as 0 or 1, and a choice as its index.
- **Conditions.**
  - `{ "mod_setting_is": [ "id", "rare" ] }` checks a choice or text value
  - `{ "mod_setting_on": "id" }` checks a switch
  - text settings can also be read into a string variable
- **Writing.**
  - `mod_setting('id') = v` in math, or an effect `{ "set_mod_setting": "id", "value": ... }`
  - allowed only for `eoc_writable` settings; anything else is a load error when the id is a literal, and a debugmsg at runtime otherwise
  - values are clamped and snapped to the step; an unknown choice is rejected
- **Unknown ids.** A setting a mod doesn't define is a load error for literal ids.

### Mods added or removed later

- **A mod added to an existing world** gets its defaults. Its `changeable_in_game` settings can then be set from the in-game page.
- **A removed mod's** stored values stay in `worldoptions.json`, ignored, so re-adding the mod restores them.

### Migrating existing mod menus

The doc gets a short "replacing an EOC settings menu" section: declare `mod_setting`s, and read them in place of the global variables. hunvre and Aftershock can move over later; this design doesn't edit them.

## Phases

| Phase | Content | Tests |
|---|---|---|
| 1 | `mod_setting` type, validation, storage in `worldoptions.json`, math and condition reads | load errors; values saved and reloaded; defaults not stored |
| 2 | World creation: pre-reading selected mods' settings, the tab, refreshing on mod-list changes | the pre-read finds exactly the selected mods' settings; values kept across mod-list changes |
| 3 | In-game page, `changeable_in_game`, the event | the event fires once with both values |
| 4 | EOC writes for `eoc_writable` settings, docs (`doc/JSON/MOD_SETTINGS.md`) | clamping, choice rejection, the non-writable load error |

The menus need a display, so tests cover the data behind them, not the screens.

## Open points (proposed defaults, accepted as proposed)

1. **Existing `WORLD_OPTION`.** Proposed: give it the shape `{ "name", "default" }`, so a mod can change the default of a built-in world option, such as spawn density, instead of forcing it on. It is unused today, so nothing breaks.
2. **Core's `option_slider` (world-generation sliders).** Proposed: mods can add their own `option_slider` through the same pre-read. These are ordinary world options, so a mod's slider changes built-in options, not mod settings.
3. **Free text limits.** Proposed: one line, at most 200 characters, never interpreted as JSON or as an id.
