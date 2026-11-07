# Weather Types

Each weather type is a type of weather that occurs, and what causes it. The only required entries are null and clear.

## `weather_type` properties

|      Identifier      |                                           Description                                            |
| -------------------- | ------------------------------------------------------------------------------------------------ |
| `name`               | UI name of weather type.                                                                         |
| `id`                 | Unique string for this weather type.                                                             |
| `color`              | UI color of weather type.                                                                        |
| `map_color`          | Map color of weather type.                                                                       |
| `sym`                | Map glyph of weather type.                                                                       |
| `ranged_penalty`     | Penalty to ranged attacks.                                                                       |
| `sight_penalty`      | Penalty to per-square visibility, applied in transparency map.                                   |
| `light_modifier`     | modification to ambient light.                                                                   |
| `temperature_modifier` | additive modifier to ambient temperature, such as `"-20 C"`; a plain number is degrees Celsius. It applies to every tile above ground (z ≥ 0), indoors as well as outdoors; underground levels keep the region's base temperature. |
| `sound_attn`         | Sound attenuation of a given weather type.                                                       |
| `dangerous`          | If true, our activity gets interrupted.                                                          |
| `precip`             | Amount of associated precipitation. Valid values are: none, very_light, light and heavy          |
| `rains`              | Whether said precipitation falls as rain.                                                        |
| `precip_rate`        | Optional, precipitation as the rainfall it equals, in mm per hour. Funnels and rain collectors, snow accumulation (when `rains` is false) and `weather('precipitation')` use it. Defaults from `precip`: very_light 0.5, light 1.5, heavy 3, none 0. Rain (getting wet, field and scent decay) happens only when `rains` is true and this is above 0. |
| `wetness`            | Optional, how hard the rain soaks every character out in it (above water and not under a roof), every 6 seconds; 0 keeps them dry. Defaults from `precip`: very_light 5, light 30, heavy 60. |
| `field_decay`        | Optional, how much the rain ages outdoor fields and scent each turn, e.g. `"15 s"`. Defaults from `precip`: very_light 5 s, light 15 s, heavy 45 s. |
| `glare`              | Optional, the glare this weather can cause in the eyes of characters (the avatar and NPCs) out in the sun, not asleep, blind, sheltered, wearing `SUN_GLASSES` or with `GLARE_RESIST`. An array of objects; the first whose `min_irradiance` the sun is above (W/m², after `sun_multiplier`) and whose optional `condition` holds (the character is alpha) is applied to every eye: `effect` (effect id), `min_irradiance`, `duration` (default `"20 s"`, when the character doesn't have the effect yet) and `repeat_duration` (default `"10 s"`). `HIGH_GLARE` doubles the duration. An empty array means no glare. Without it: `snow_glare` in winter above 573 W/m², else `glare` above 707 W/m². |
| `light_multiplier`   | Optional, multiplication to ambient light. Multiplication occurs before `light_modifier`.        |
| `tiles_animation`    | Optional, name of the tiles animation to use                                                     |
| `debug_cause_eoc`    | Optional, id of effect_on_condition to be run when the debug menu selects this weather.          |
| `debug_leave_eoc`    | Optional, id of effect_on_condition to be run when the debug menu is used to leave this weather. |
| `sound_category`     | Optional, what sound effect to play. Valid values are: silent, drizzle, rainy, thunder, flurries, snowstorm and snow. |
| `duration_min`       | Optional, the lower bound on the amount of time this weather can last. Defaults to 5 minutes.    |
| `duration_max`       | Optional, the upper bound on the amount of time this weather can last. Defaults to 5 minutes.    |
| `weather_animation`  | Optional, Information controlling weather animations.  Members: factor, color and glyph          |
| `sun_multiplier`     | Optional, multiplier to radiation from sun. Affects energy output of solar panels.               |
| `condition`          | A dialog condition to determine if this weather is happening.  See Dialogue conditions section of [NPCs](NPCs.md) A context variable of `weather_location` contains the location of the current tile checking its weather.  This can be used to have weather be based on location. |
| `priority`           | An integer.  If the condition of multiple weather types are true the one with higher priority wins. |
| `required_weathers`  | A string array of possible weathers, it is at this point in the loop. i.e. rain can only happen if the conditions for clouds light drizzle or drizzle are present.  Required weathers need to have lower load orders to be. |
| `passive_effects`    | Passive effects, applied while this weather occurs to every creature in the reality bubble that is above ground, or underground under open sky, wherever the player is. Creatures under water are not affected. Set `immune_inside` to spare creatures indoors. For detailed syntax, see [field_effect](JSON_INFO.md#field_effect) |
| `tint_color`         | RBG value that sunlight is tinted, during this weather.                                          |
| `tint_strength`      | Value between 0 and 1 that indicates how strong tint_color is, where 0 is no tint at all.        |

#### `weather_type` example

```jsonc
[
  {
    "id": "lightning",
    "type": "weather_type",
    "name": "Lightning Storm",
    "color": "c_yellow",
    "map_color": "h_yellow",
    "sym": "%",
    "sun_sym": "⛈️",
    "ranged_penalty": 4,
    "sight_penalty": 1.25,
    "light_modifier": -45,
    "temperature_modifier": "-30 C",
    "sound_attn": 8,
    "dangerous": false,
    "precip": "heavy",
    "rains": true,
    "required_weathers": [ "thunder" ],
    "priority": 80,
    "condition": { "math": [ "weather('pressure') < 990" ] },
    "passive_effects": [
      {
        "effect_id": "smoke_eyes",
        "body_part": "eyes",
        "intensity": 1,
        "min_duration": "2 seconds",
        "max_duration": "2 seconds",
        "immunity_data": { "body_part_env_resistance": [ [ "sensor", 3 ] ] },
        "immune_inside_vehicle": true
      },
      {
        "effect_id": "smoke_lungs",
        "body_part": "mouth",
        "intensity": 1,
        "min_duration": "2 seconds",
        "max_duration": "2 seconds",
        "immunity_data": { "body_part_env_resistance": [ [ "mouth", 3 ] ], "flags": [ "INHALED_TOXIN_IMMUNE" ] },
        "immune_inside_vehicle": true
      }
    ],
    "tint_color": [110, 0, 0],
    "tint_strength": 1.0
  }
]
