<!-- START doctoc generated TOC please keep comment here to allow auto update -->
<!-- DON'T EDIT THIS SECTION, INSTEAD RE-RUN doctoc TO UPDATE -->
*Contents*

- [Movement Modes](#movement-modes)
  - [definition](#definition)
    - [`type`](#type)
    - [`id`](#id)
    - [`character`](#character)
    - [`panel_char`](#panel_char)
    - [`name`](#name)
    - [`panel_color`](#panel_color)
    - [`symbol_color`](#symbol_color)
    - [`exertion_level`](#exertion_level)
    - [`exertion_level_animal_riding`](#exertion_level_animal_riding)
    - [`change_good_x`](#change_good_x)
    - [`change_bad_x`](#change_bad_x)
    - [`move_type`](#move_type)
    - [`stamina_multiplier`](#stamina_multiplier)
    - [`sound_multiplier`](#sound_multiplier)
    - [`move_speed_multiplier`](#move_speed_multiplier)
    - [`mech_power_use`](#mech_power_use)
    - [`swim_speed_mod`](#swim_speed_mod)
    - [`stop_hauling`](#stop_hauling)
    - [`condition`](#condition)
    - [`switch_cost`](#switch_cost)
  - [Switching cost](#switching-cost)
  - [EOCs and events](#eocs-and-events)

<!-- END doctoc generated TOC please keep comment here to allow auto update -->

# Movement Modes

## definition

```jsonc
{
  "type": "movement_mode",
  "id": "fly",
  "character": "f",
  "panel_char": "F",
  "name": "Fly",
  "panel_color": "light_green",
  "symbol_color": "light_green",
  "exertion_level": "EXTRA_EXERCISE",
  "prepare_none": "You eye the ground and wonder how to miss it",
  "prepare_animal": "Your horse is growing some wings",
  "prepare_mech": "Your mechs jetpack is warming up",
  "change_good_none": "You throw yourself at the ground, and miss.",
  "change_good_animal": "You steer your steed into a... fly?",
  "change_good_mech": "You enable your mech's jetpack unit.",
  "change_bad_none": "You throw yourself at the ground, and hit.",
  "change_bad_animal": "Your steed doesn't know how to fly.",
  "change_bad_mech": "Your mech cannot fly.",
  "move_type": "running",
  "stamina_multiplier": 12.0,
  "sound_multiplier": 4.0,
  "move_speed_multiplier": 2.5,
  "mech_power_use": 5,
  "swim_speed_mod": -80,
  "stop_hauling": true,
  "condition": { "u_has_trait": "WINGS_BIRD" },
  "switch_cost": 200
},
```

### `type`
Mandatory. Must be `"movement_mode"`.

### `id`
Mandatory. The id of the movement mode.

### `character`
Mandatory. The character shown in the move mode menu to select this mode.

### `panel_char`
Mandatory. The character shown on the panels when this mode is selected.

### `name`
Mandatory. The name of the move mode displayed whenever it is displayed.

### `panel_color`
Optional. The color shown on the panels when this mode is selected.

### `symbol_color`
Optional. The color shown in curses when your character is in this move mode.

### `exertion_level`
Mandatory. What exertion level this move mode will put you into. The options are, from least to most exercise:
```
NO_EXERCISE
LIGHT_EXERCISE
MODERATE_EXERCISE
ACTIVE_EXERCISE
EXTRA_EXERCISE
EXPLOSIVE_EXERCISE
```

### `exertion_level_animal_riding`
Optional. The exertion level while riding an animal in this mode, with the same options as `exertion_level`.  Defaults to `NO_EXERCISE`.

### `prepare_x`
Mandatory. The message given when the character switches their desired mode to this move mode, before any action is taken.
There are three values for x: `none`, `animal`, and `mech`.
- `none` is the message given when the character is not mounted.
- `animal` is the message given when riding an animal.
- `mech` is the message given when in a mech.

### `change_good_x`
Mandatory. The message given when the character switches to this move mode.
There are three values for x: `none`, `animal`, and `mech`.
- `none` is the message given when the character is not mounted.
- `animal` is the message given when riding an animal.
- `mech` is the message given when in a mech.

### `change_bad_x`
Optional. The message given when the character fails to switch to this move mode.
You fail to switch to a `running` type mode when you can't run (stamina, legs), to a `prone` or `crouching` one while riding an animal, and to any mode whose [`condition`](#condition) is false.
There are three values for x: `none`, `animal`, and `mech`.
- `none` is the message given when the character is not mounted.
- `animal` is the message given when riding an animal.
- `mech` is the message given when in a mech.

Defaults to `You feel bugs crawl over your skin.`

### `move_type`
Mandatory. The type of move mode - used to determine if the character is in stealth, walking normally, or fleeing.
Valid values are:
- `prone`
- `crouching`
- `walking`
- `running`

### `stamina_multiplier`
Optional. Multiplier on the character's stamina use when in this mode.
All non-negative floating point values are valid.

Defaults to `1.0` (no multiplier).

### `sound_multiplier`
Optional. Multiplier on the sound made when moving in this mode.
All non-negative floating point values are valid.

Defaults to `1.0` (no multiplier).

### `move_speed_multiplier`
Optional. Multiplier on the move speed when moving in this mode.
All non-negative floating point values are valid.

Defaults to `1.0` (no multiplier).

### `mech_power_use`
Optional. Mech power used each step when in this mode.
All integer values are valid.

Defaults to `2`.

### `swim_speed_mod`
Optional. How many additional moves .
All integer values are valid.

Defaults to `0`.

### `stop_hauling`
Optional. Determines whether or not hauling is cancelled switching to this mode.
Valid values are `true` and `false`.

Defaults to false.

### `condition`
Optional. A [dialogue condition](NPCs.md#dialogue-conditions), with the character as `u_`: the character can only switch to this mode while it is true.  A character in the mode when it stops being true (the wings are gone) goes back to walking on its next step.  The built-in rules still apply too.  Defaults to none: anyone can switch to it.

```jsonc
"condition": { "u_has_trait": "WINGS_BIRD" }
```

### `switch_cost`
Optional. Moves added to the cost of switching to this mode, on top of the [posture change](#switching-cost).  Must be zero or more.  Defaults to `0`.

## Switching cost

Changing mode costs moves: 50 to go between standing (`walking`, `running`) and `crouching`, 100 between `crouching` and `prone`, 150 between standing and `prone`, plus the new mode's `switch_cost`.  Carrying more than your capacity multiplies it by the weight ratio, `DEFT` halves it and `CLUMSY` makes it 1.5 times as much; then the enchantment value [`MOVE_MODE_SWITCH_COST`](MAGIC.md) applies.  The avatar pays when its next action starts; an NPC pays when it switches.

## EOCs and events

- The condition `u_has_move_mode` / `npc_has_move_mode` checks the current mode ([EFFECT_ON_CONDITION.md](EFFECT_ON_CONDITION.md#u_has_move_mode-npc_has_move_mode)).
- The effect `u_set_move_mode` / `npc_set_move_mode` switches to a mode ([EFFECT_ON_CONDITION.md](EFFECT_ON_CONDITION.md#u_set_move_mode-npc_set_move_mode)).
- The event `character_changes_move_mode` is sent whenever a character's mode changes.

