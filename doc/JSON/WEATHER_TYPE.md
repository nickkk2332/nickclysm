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
| `snow_rate`          | Optional, mm of snow per hour this weather adds to the ground (see [Snow on the ground](#snow-on-the-ground)). Default: for precipitation that isn't rain (`precip` other than none, `rains` false) the precipitation rate (0.5, 1.5 or 3 mm/h) times the region's snow `water_ratio` (12 by default, so heavy snow adds 36 mm/h); otherwise 0. Setting it makes any weather add snow, rain included. |
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

## Snow on the ground

Snow depth is kept per overmap tile, at ground level, so every level above the ground has the ground's snow. Once a minute, every overmap tile the player has been to gains the current weather's `snow_rate` and, above 0 °C, loses the region's `melt_rate` for each degree; depth stays between 0 and the region's `max_depth`. A tile is caught up for at most 1 hour at a time. A tile the player hasn't been to has the depth of the nearest one they have.

Outdoors and under open sky, deep snow slows characters and monsters by the region's `move_cost` table. By default, characters pay +20 move cost from 100 mm, +50 from 250 mm and +100 from 500 mm; monsters pay 1, 2 and 4 extra terrain cost (25 move points each). Characters with the `LEVITATION` or `SNOWWALKING` flag, and monsters that fly or have `SNOWWALKING`, are not slowed.

The ratio, melt rate, maximum and move-cost table are the region's, in its weather generator's `snow` object (see [REGION_SETTINGS.md](REGION_SETTINGS.md#weather)). EOCs read and set the depth with `weather('snow_depth')` and read the current snowfall with `weather('snow_rate')` (see the `weather` function in [NPCs.md](NPCs.md)).
