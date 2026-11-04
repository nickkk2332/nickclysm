# Mod settings

A mod can offer players settings: a switch, a number, a choice from a list, or a line of text. Players pick them when they create a world, on the world creation's "Mod Settings" tab, and the in-game options menu shows them again on its "Mod settings" page. Each world keeps its own values, as it keeps its spawn density.

EOCs read every setting, and change the ones their mod lets them.

- [Declaring a setting](#declaring-a-setting)
- [Overriding another mod's setting](#overriding-another-mods-setting)
- [Where players set them](#where-players-set-them)
- [How a world keeps them](#how-a-world-keeps-them)
- [Reading settings](#reading-settings)
- [Changing settings from EOCs](#changing-settings-from-eocs)
- [Reacting to changes](#reacting-to-changes)
- [World options: `WORLD_OPTION` and `option_slider`](#world-options-world_option-and-option_slider)
- [Replacing an EOC settings menu](#replacing-an-eoc-settings-menu)

## Declaring a setting

```jsonc
{
  "type": "mod_setting",
  "id": "magiclysm_mana_regen",
  "name": "Mana regeneration",
  "description": "Multiplies how fast mana comes back.",
  "kind": "number",
  "default": 1,
  "min": 0.25,
  "max": 4,
  "step": 0.25,
  "changeable_in_game": true,
  "eoc_writable": false,
  "group": "Magic"
}
```

| Field | | Meaning |
| --- | --- | --- |
| `id` | mandatory | Unique across all mods. Start it with your mod's id, so two mods don't pick the same one. |
| `name` | mandatory | The label the menus show; translated. |
| `description` | optional | The tooltip the menus show; translated. |
| `kind` | mandatory | `bool` (an on/off switch), `number`, `choice` or `text`. |
| `default` | optional | The value of a world that never set it: `false`, `min`, the first choice or `""` when left out. |
| `min`, `max` | mandatory for `number` | The range. `max` is more than `min`. |
| `step` | optional for `number` | The values go from `min` in steps of this, up to `max`. Default 1. Numbers keep 6 decimals at most. |
| `choices` | mandatory for `choice` | `[ { "id": "rare", "name": "Rare" }, ... ]`. The ids are unique; the names are translated. |
| `max_length` | optional for `text` | The most characters the text can have, 1 to 200. Default 200. |
| `changeable_in_game` | optional | Default `true`. With `false` the setting is picked when the world is created and fixed after: the in-game menu shows it greyed out, with a tooltip saying why. It doesn't stop EOCs: `eoc_writable` decides that. |
| `eoc_writable` | optional | Default `false`. Whether EOCs can change it ([below](#changing-settings-from-eocs)). |
| `group` | optional | A sub-heading for it inside your mod's part of the menu; translated. |

A setting is checked when it loads, and any of these is a load error:

- a `default` outside `min` to `max`, or not on a step from `min`
- a `step` of 0 or less, or a `max` that isn't more than `min`
- two choices with one id, or a `default` that isn't one of the choices
- text whose `default` is more than one line or longer than `max_length`, or a `max_length` over 200
- the members of another kind: `min` on a switch is an unknown member
- an `id` another mod already defines, without `copy-from` (next section)

Settings belong in mods: world creation reads them from the mods a world uses (`data/core` has none).

## Overriding another mod's setting

As with other types, a mod changes another mod's setting with `copy-from`, naming the setting itself. The setting stays the first mod's: the menus show it in that mod's part.

```jsonc
{ "type": "mod_setting", "id": "magiclysm_mana_regen", "copy-from": "magiclysm_mana_regen", "default": 2 }
```

A definition of an id that's already defined, without `copy-from`, is a load error. Put such overrides in the mod's `mod_interactions/<other mod>/` folder, so they load only when the other mod does.

## Where players set them

- **World creation.** Once mods are picked, world creation reads just the `mod_setting`, `option_slider` and `WORLD_OPTION` objects of those mods, from the files the loader would read for them (their `mod_interactions` folders included), without loading anything else. A file that mentions none of those types isn't even parsed, so this stays quick. The "Mod Settings" tab lists the settings per mod, with each mod's groups as sub-headings; on the first screen of world creation it's behind "[o] - Mod settings". Changing the mod list reads them again: values picked for mods still in the list stay, those of a mod taken out go.
- **In game.** The options menu has a "Mod settings" page, laid out the same way, when a loaded mod has settings. Settings that aren't `changeable_in_game` show greyed out, with a tooltip saying why. Changes apply when the menu's changes are saved, and are saved with the world's options at once, as world options are.

## How a world keeps them

A world keeps its values in its `worldoptions.json`, in an element of their own:

```jsonc
[
  { "info": "...", "default": "...", "name": "SPAWN_DENSITY", "value": "1.00" },
  { "name": "__mod_settings", "value": "", "mod_settings": { "magiclysm_mana_regen": 2.5, "my_mod_difficulty": "hard" } }
]
```

The element has a `name` and a `value` like every option, so a build without mod settings reads it as an option it doesn't know and skips it. Such a build drops it when it saves the world's options, and the settings go back to their defaults.

- **Only values that differ from the default are stored.** A mod that changes a default later changes it for every world that never set the setting.
- **A mod added to an existing world** brings its defaults. Its `changeable_in_game` settings can then be set on the in-game page.
- **A removed mod's values stay** in the file, ignored, so adding the mod back brings them back.
- **A value the setting no longer allows** (the mod narrowed the range, dropped a choice, changed the kind) reads as the setting allows: a number clamped and snapped to the step, anything else as the default.
- Values that EOCs change are saved with the game. Values the in-game menu changes are saved when the menu saves.

## Reading settings

| | Reads | Example |
| --- | --- | --- |
| math `mod_setting(s/v)` | A number. A switch reads as 0 or 1, a choice as its index from 0. Text has no number: using it is an error. | `{ "math": [ "u_mana_mult", "=", "mod_setting('magiclysm_mana_regen')" ] }` |
| condition `mod_setting_on` | Whether a switch is on. | `{ "mod_setting_on": "my_mod_hard_mode" }` |
| condition `mod_setting_is` | Whether a choice or a text is this value: `[ setting, value ]`, each a string or [variable object](EFFECT_ON_CONDITION.md#variable-object). | `{ "mod_setting_is": [ "my_mod_difficulty", "hard" ] }` |
| string mutator `mod_setting` | The value as text: the text, the choice id, `true` or `false`, or the number. Use it anywhere a string or variable is taken, such as `set_string_var`. | `{ "set_string_var": { "mutator": "mod_setting", "id": "my_mod_greeting" }, "target_var": { "global_val": "greeting" } }` |

The setting can be a variable (`mod_setting(my_var)`, `{ "mod_setting_on": { "global_val": "which" } }`). One written out as a string is checked when the game loads: a setting no mod defines, a condition on the wrong kind of setting (`mod_setting_on` on a number), a choice it doesn't have (`mod_setting_is`) or text read by math is a load error. A setting named by a variable that doesn't exist is an error when it runs.

## Changing settings from EOCs

An EOC changes a setting only if the setting is `"eoc_writable": true`.

- math: `mod_setting('id') = v`, and `+=` and the other assignments. A choice is set by its index; text can't be set this way.
- the effect `set_mod_setting`:

| Syntax | Optionality | Value | Info |
| --- | --- | --- | --- |
| "set_mod_setting" | **mandatory** | string or [variable object](EFFECT_ON_CONDITION.md#variable-object) | the setting |
| "value" | **mandatory** | `true`/`false`, number, string, [variable object](EFFECT_ON_CONDITION.md#variable-object) or `{ "math": [...] }` | the new value: `true` or `false` (or a number, 0 being off) for a switch; a number; a choice id (or its index) for a choice; text for text, where a number becomes its text |

```jsonc
{ "set_mod_setting": "my_mod_difficulty", "value": "hard" }
{ "set_mod_setting": "magiclysm_mana_regen", "value": { "math": [ "mod_setting('magiclysm_mana_regen') * 2" ] } }
```

What a setting does with a value:

- a number is clamped to `min` to `max` and snapped to the nearest step
- a choice that isn't one of the setting's is refused: the setting keeps its value, with an error
- text is kept to one line (line breaks become spaces) and cut to `max_length` characters. It's never read as JSON or as an id.

Changing a setting that isn't `eoc_writable` is a load error when the setting is written out in the EOC, and an error when it runs (the setting keeps its value) when a variable names it. So is a value written out that the setting would refuse.

## Reacting to changes

A change of value in game, from the options menu or an EOC, sends the event `mod_setting_changed`, once per change; setting the value a setting already has sends nothing. Its context variables are:

| Variable | |
| --- | --- |
| `id` | the setting |
| `old_value`, `new_value` | the values as text, as the `mod_setting` mutator gives them: the number (`"2.50"`), `true` or `false`, the choice id, the text |

The avatar is `u`. Statistics, scores and achievements don't keep this event.

```jsonc
{
  "type": "effect_on_condition",
  "id": "my_mod_on_setting_changed",
  "eoc_type": "EVENT",
  "required_event": "mod_setting_changed",
  "condition": { "compare_string": [ "my_mod_difficulty", { "context_val": "id" } ] },
  "effect": [ { "u_message": "Difficulty is now <context_val:new_value>." } ]
}
```

## World options: `WORLD_OPTION` and `option_slider`

Mod settings are new values for mods. A mod can also change the built-in world options (spawn density, season length, ...), two ways. World creation reads both the same way it reads mod settings.

- **`WORLD_OPTION`** changes the default of a built-in world option:

  ```jsonc
  { "type": "WORLD_OPTION", "name": "SPAWN_DENSITY", "default": 2 }
  ```

  `name` is a world option ([OPTIONS.md](OPTIONS.md)); `default` is a value it takes: `true` or `false`, a number in its range, or one of its values as a string. Anything else is a load error. A world created with the mod starts with that value, unless the player set the option to something else in the main menu's world defaults or at world creation; taking the mod out of the list there puts the built-in default back. An existing world keeps its value: loading the mod changes only the default its options menu shows. The old shape, `{ "options": [ ... ] }`, which forced options on, is a load error.
- **`option_slider`** with `"context": "WORLDGEN"`: a slider on the first screen of world creation ([JSON_INFO.md](JSON_INFO.md#option-sliders)). A mod's slider is shown after core's, and one with the id of core's replaces it. Its levels set ordinary world options, not mod settings.

## Replacing an EOC settings menu

Some mods build a settings menu out of EOCs and global variables: a dialogue or `run_eoc_selector` that flips `global_val`s, read wherever the mod needs them. To move such a menu over:

1. Declare a `mod_setting` for each global variable, with the variable's default. A variable that holds `0`/`1` becomes a `bool`; one that holds a few strings, a `choice`; a number, a `number` with the range the menu allowed.
2. Read the setting where the mod read the variable: `mod_setting('id')` in math in place of `my_var`, `mod_setting_on` or `mod_setting_is` in place of comparing it, the `mod_setting` mutator where a string was copied.
3. Mark `eoc_writable` only the settings the mod's own EOCs still change; set `changeable_in_game` to `false` for those that only make sense when the world is created.
4. Drop the menu EOCs, or keep one that points players to the options menu. Keep the variables only as long as old saves need them: an `EVENT` EOC on `game_load` can copy an old variable into an `eoc_writable` setting with `set_mod_setting`, then remove the variable.

Players then pick the settings when they create the world, see them all in one place, and keep them per world rather than per character.
