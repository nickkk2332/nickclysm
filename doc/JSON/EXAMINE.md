<!-- START doctoc generated TOC please keep comment here to allow auto update -->
<!-- DON'T EDIT THIS SECTION, INSTEAD RE-RUN doctoc TO UPDATE -->
*Contents*

- [Terrain/Furniture Examination Actions](#terrainfurniture-examination-actions)
  - [Hardcoded Examine Actions](#hardcoded-examine-actions)
  - [Examine Actors](#examine-actors)
    - [`appliance_convert`](#appliance_convert)
      - [`furn_set`](#furn_set)
      - [`ter_set`](#ter_set)
      - [`item`](#item)
      - [Example](#example)
    - [`cardreader`](#cardreader)
      - [`flags`](#flags)
      - [`consume_card`](#consume_card)
      - [`allow_hacking`](#allow_hacking)
      - [`hacked_ter`](#hacked_ter)
      - [`hacked_furn`](#hacked_furn)
      - [`despawn_monsters`](#despawn_monsters)
      - [`omt_allowed_radius`](#omt_allowed_radius)
      - [`mapgen_id`](#mapgen_id)
      - [`radius`](#radius)
      - [`terrain_changes`](#terrain_changes)
      - [`furn_changes`](#furn_changes)
      - [`query`](#query)
      - [`query_msg`](#query_msg)
      - [`success_msg`](#success_msg)
      - [`redundant_msg`](#redundant_msg)
      - [Example](#example-1)
    - [`effect_on_condition`](#effect_on_condition)
      - [`effect_on_conditions`](#effect_on_conditions)
    - [`mortar`](#mortar)
      - [`ammo`](#ammo)
      - [`range`](#range)
      - [`condition`](#condition)
      - [`condition_fail_msg`](#condition_fail_msg)
      - [`aim_deviation`](#aim_deviation)
      - [`aim_duration`](#aim_duration)
      - [`flight_time`](#flight_time)
      - [`effect_on_conditions`](#effect_on_conditions-1)
      - [Example](#example-2)
    - [`process_start`](#process_start)
      - [Messages](#messages)
      - [Example](#example-3)
    - [`process_finish`](#process_finish)
      - [Example](#example-4)

<!-- END doctoc generated TOC please keep comment here to allow auto update -->

# Terrain/Furniture Examination Actions

These are actions that will be performed when a terrain/furniture is examined.
The hardcoded examine actions specified as a `"examine_action": "ACTION"`, where `ACTION` is replaced with one of the strings from the list below.
The examine actors are specified as JSON objects with a `type` corresponding to a certain type of action, and other members filling in the data for how the action functions.
Examine actions and examine actors can be mixed within single key, like `"examine_action": [ "locked_object_pickable", "chainfence", { "type": "effect_on_conditions", ... } ]`

## Hardcoded Examine Actions

- ```aggie_plant``` Harvest plants.
- ```autodoc``` Brings the Autodoc consoles menu. Needs the ```AUTODOC``` flag to function properly and an adjacent furniture with the ```AUTODOC_COUCH``` flag.
- ```arcfurnace_empty```, ```arcfurnace_full```, ```autoclave_empty```, ```autoclave_full```, ```kiln_empty```, ```kiln_full```, ```stook_empty```, ```stook_full``` Old batch processes, kept for mods that name them. They only work on the vanilla furniture ids (`f_arcfurnace_empty`/`_full`, `f_autoclave`/`_full`, `f_kiln_empty`/`_full`, `f_kiln_metal_empty`/`_full`, `f_kiln_portable_empty`/`_full`, `f_stook_empty`/`_full`), which use the [`process_start`](#process_start) and [`process_finish`](#process_finish) actors now; use those for new furniture. `arcfurnace_empty` makes `unfinished_cac2`, an item that doesn't exist.
- ```bars``` Take advantage of AMORPHOUS and slip through the bars.
- ```bulletin_board``` Use this to arrange tasks for your faction camp.
- ```chainfence``` Hop over the chain fence.
- ```controls_gate``` Controls the attached gate.
- ```dirtmound``` Plant seeds and plants.
- ```elevator``` Use the elevator to change floors.
- ```finite_water_source``` Drink or get liquid from this terrain/furniture. Unlike ordinary `water_source`, terrain with this examine action will get liquid from a finite source (liquid is placed on that tile as an item during the mapgen) and will stop functioning if said liquid if exhausted on that tile. Should be used in pair with `liquid_source`
- ```flower_poppy``` Pick the mutated poppy.
- ```fswitch``` Flip the switch and the rocks will shift.
- ```fungus``` Release spores as the terrain crumbles away.
- ```gaspump``` Use the gas-pump.
- ```harvest_plant_ex``` Harvest a harvestable plant. Works on field crops and planters. (corn, wheat, etc.)
- ```gunsafe_el``` An electronic safe: hacking it open turns it into its `hack_result` furniture (default `f_safe_o`), see [JSON_INFO.md](JSON_INFO.md#hack_result).
- ```locked_object``` Locked, but can be pried open. Adding 'PICKABLE' flag allows opening with a lockpick as well. Lockpicking uses the furniture/terrain's `lockpick_result`, `lockpick_message`, `lockpick_difficulty` and `lockpick_jam_result` (see [JSON_INFO.md](JSON_INFO.md#lockpick_result)); an `ALARMED` lock may set off an alarm.
- ```locked_object_pickable``` Locked, but can be opened with a lockpick. Requires 'PICKABLE' flag; lockpicking works as for `locked_object`.
- ```none``` None
- ```pedestal_temple``` Opens the temple if you have a petrified eye.
- ```pedestal_wyrm``` Spawn wyrms.
- ```pit_covered``` Uncover the pit.
- ```pit``` Cover the pit if you have some planks of wood.
- ```portable_structure``` Take down a tent or similar portable structure.
- ```recycle_compactor``` Compress pure metal objects into basic shapes.
- ```rubble``` Clear up the rubble if you have a shovel.
- ```safe``` Attempt to crack the safe.
- ```shelter``` Take down the shelter.
- ```shrub_marloss``` Pick a Marloss bush.
- ```shrub_wildveggies``` Pick a wild veggies shrub.
- ```slot_machine``` Gamble.
- ```water_source``` Drink or get liquid from a furniture/terrain. Should be used in pair with `liquid_source`
- ```mortar``` Shoot a projectile.

## Examine Actors

All examine actions share the keys `"type"` and `"name"`

```jsonc
  {
    "type": "appliance_convert",
    "name": "Take down"
    ...
  }
```

### `appliance_convert`

#### `furn_set`
Optional, defaults to no change.
String.
Furniture id that this tile will be set to after placing the appliance.

#### `ter_set`
Optional, defaults to no change.
String.
Terrain id that this tile will be set to after placing the appliance.

#### `item`
Mandatory.
String.
Item id of the base item of this appliance.

#### Example
```jsonc
  {
    "type": "appliance_convert",
    "furn_set": "f_null",
    "ter_set": "t_floor",
    "item": "fridge"
  }
```

### `cardreader`

#### `flags`
Mandatory.
Array of strings.
List of item flags that, when on an item, mean that the item can be used as card.

#### `consume_card`
Optional, defaults to true.
Boolean (true/false).
Whether or not to consume the item used to activate this cardreader.

#### `allow_hacking`
Optional, defaults to true.
Boolean (true/false).
Whether or not to allow hacking this door with an electrohack.
A successful hack makes the same changes as a card (`mapgen_id`, or `terrain_changes` and `furn_changes` within `radius`), without despawning monsters or using up a card, then turns the reader into `hacked_ter` / `hacked_furn`.

#### `hacked_ter`
Optional.
String.
The terrain the reader's tile becomes when it is hacked open.
Defaults to `t_card_reader_broken` for a terrain reader; the terrain under a furniture reader stays as it is.

#### `hacked_furn`
Optional.
String.
The furniture the reader's tile becomes when it is hacked open.
Without it the furniture stays as it is.

#### `hack_difficulty`
Optional, defaults to 6.
Integer (0 or greater).
The hack roll needed to open it. The roll is random around the hacker's hacking level (computer skill, tool quality and intelligence; standard deviation 5): below 0 it sets off an alarm, from 0 to below `hack_difficulty` nothing happens.

#### `hack_eocs`
Optional.
Array of strings and or effect_on_condition objects.
Run once the reader is hacked open (after its changes), with the hacker as alpha and the context value `pos`, the reader's location.

#### `despawn_monsters`
Optional, defaults to true.
Boolean (true/false).
Whether or not to remove hostile monsters with the `ID_CARD_DESPAWN` flag.

#### `omt_allowed_radius`
Optional, defaults to infinity.
Integer (0 or greater).
For cards with the `PRESERVE_SPAWN_OMT` flag, how many overmap tiles away a card can spawn and be accepted for this cardreader.
For cards without the flag, this field is ignored.

#### `mapgen_id`
Optional.
String.
Update mapgen ID to apply on opening the door.
Conflicts with `radius`, `terrain_changes`, and `furn_changes`.

#### `radius`
Optional, defaults to 3.
Integer.
What area around the cardreader to apply changes to.
With a radius of three, it will go 3 tiles out from the reader in every direction.

#### `terrain_changes`
Optional, defaults to nothing.
JSON Object.
Each key in this JSON Object corresponds to a terrain, and the value is a terrain that that terrain will be transformed into.
Multiple items can be specified by adding more keys, e.g. `{ "a": "b", "x": "y" }`

#### `furn_changes`
Optional, defaults to nothing.
JSON Object.
Each key in this JSON Object corresponds to a furniture, and the value is a furniture that that furniture will be transformed into.
Multiple items can be specified by adding more keys, e.g. `{ "a": "b", "x": "y" }`

#### `query`
Optional, defaults to true.
Boolean (true/false).
Whether or not to query the player before activating and potentially consuming a card.
With `false`, examining the reader with a card uses it at once.

#### `query_msg`
Optional, defaults to nothing.
String.
What message to display when querying the player on whether or not to activate the cardreader.

#### `success_msg`
Mandatory.
String.
What message to print to the log when this is successfully activated.

#### `redundant_msg`
Mandatory.
String.
What message to print when attempting to activate the cardreader after it has already been activated.

#### Example
```jsonc
  {
    "type": "cardreader",
    "flags": [ "SCIENCE_CARD" ],
    "consume_card": true,
    "allow_hacking": true,
    "despawn_monsters": true,
    "omt_allowed_radius": 3,
    "radius": 3,
    "terrain_changes": { "t_door_metal_locked": "t_door_metal_c" },
    "furn_changes": { "f_crate_c": "f_crate_o" },
    "query": true,
    "query_msg": "Are you sure you want to open this door?",
    "success_msg": "You opened the door!",
    "redundant_msg": "The door is already open."
  }
```
### `effect_on_condition`

#### `effect_on_conditions`
Mandatory.
Array of strings and or effect_on_condition objects.
Run all of the eocs upon being examined with u as the examiner and npc as null. See [EFFECT_ON_CONDITION.md](EFFECT_ON_CONDITION.md)
They get the context values `this` (the id of the terrain or furniture the action belongs to), `terrain`, `furniture`, `examined_layer` (`terrain` or `furniture`) and `pos` (the tile).

#### `condition`
Optional.
A [dialogue condition](EFFECT_ON_CONDITION.md#dialogue-conditions), with the examiner as alpha and the same context values.
When it isn't met, the EOCs don't run: examining shows `condition_fail_msg` if there is one; without one, the action is greyed out in the examine menu, and a tile whose only action it is can't be examined.

#### `condition_fail_msg`
Optional.
String.
Shown to the examiner when `condition` isn't met.

#### `npc_usable`
Optional, defaults to true.
Boolean (true/false).
Whether an NPC (for example one told to examine the tile from an EOC, `npc_examine`) may use this action.
An NPC uses the first examine actor it may use: one whose `condition` it meets and that is `npc_usable` (other actor types always count as usable), else the tile's first built-in examine action.

#### Example
```jsonc
  "examine_action": {
    "type": "effect_on_condition",
    "name": "Pray",
    "condition": { "u_has_trait": "SPIRITUAL" },
    "condition_fail_msg": "You feel nothing.",
    "npc_usable": false,
    "effect_on_conditions": [ "EOC_ALTAR" ]
  }
```

### `mortar`

#### `ammo`
Mandatory.
Array of strings.
List of ammo types, that can be used in this mortar.

#### `range`
Mandatory
Integer
How far this mortar can shoot, in meters.

#### `condition`
Optional, defaults to true.
Condition
Can be used to add custom conditions for a turret to be used; character must pass the condition, otherwise they won't be able to use a turret.

#### `condition_fail_msg`
Optional, defaults to `You cannot use this mortar.`
String.
If `condition` is failed, the game print this message

#### `aim_deviation`
Optional, defaults to 0.
Integer or variable object
Percent of the distance between you and aiming point, that you may miss; aim_deviation of `0.02` when you aim 1000 meters afar would mean you can miss for 20 meters, 10 meters in either coordinate.

#### `aim_duration`
Optional, defaults to 0 seconds.
Duration or variable object
How long you gonna aim before actually shooting a projectile.

#### `flight_time`
Optional, defaults to 0 seconds.
Duration or variable object
How long the projectile gonna fly before explosion occur.

#### `effect_on_conditions`
Optional
JSON Object.
Effect on condition, that would be fired in the end of shooting. Additional context variables sent: 
`this`, string, furniture id; 
`pos`; string, coordinates of the furniture; 
`target`, string, coordinates of picked tile

#### Example
```jsonc
    "examine_action": {
      "type": "mortar",
      "ammo": [ "mortar_60mm" ],
      "condition": { "math": [ "u_skill('launcher') >= 1" ] },
      "condition_fail_msg": "You have absolutely no idea how to use this mortar.",
      "range": 3490,
      "aim_duration": {
        "math": [
          "u_effect_duration('aimed_60mm') > 1",
          " ? max(5, 16 / u_skill('launcher'))",
          " : max(60, 190 / u_skill('launcher'))"
        ]
      },
      "aim_deviation": { "math": [ "max(0.02, 0.1 / u_skill('launcher'))" ] },
      "flight_time": "15 seconds",
      "effect_on_conditions": [
        {
          "id": "EOC_60MM_SHOT",
          "effect": [
            { "u_add_effect": "aimed_60mm", "duration": "3 minutes" },
            { "math": [ "u_skill_exp('launcher', 'format': 'percentage')++" ] },
            { "if": { "math": [ "distance('u', _target) <= 400 " ] }, "then": { "u_spawn_item": "60mm_propellant" } },
            { "if": { "math": [ "distance('u', _target) <= 1340" ] }, "then": { "u_spawn_item": "60mm_propellant" } },
            { "if": { "math": [ "distance('u', _target) <= 2150" ] }, "then": { "u_spawn_item": "60mm_propellant" } },
            { "if": { "math": [ "distance('u', _target) <= 2890" ] }, "then": { "u_spawn_item": "60mm_propellant" } }
          ]
        }
      ]
    }
```
### `process_start`

Starts a batch process on furniture, like firing a kiln: checks what lies on the tile, turns it into an intermediate item (or leaves it there to age) and switches the furniture to its working state, which has a [`process_finish`](#process_finish) action. The time of the process counts from the birthday of the first item on the tile.

| Field | Default | Description
|---    |---      |---
| `accepts` | any item | Item filter (see below): every item on the tile must match it.
| `reject_if_contains` | none | Item filter (needs `materials`, `flags` or `items`): an item that matches it stops the process with `contains_message`. Checked for each item before `accepts`.
| `requires_outside` | false | The furniture must be outside.
| `intermediate` | none | Item id, counted by charges. If given, all the items on the tile are replaced by this item, made now, with as many charges as fit in `yield_percent` of their volume.
| `yield_percent` | | Number or [variable object](EFFECT_ON_CONDITION.md), mandatory with `intermediate` (and only with it). The percent of the volume that becomes `intermediate`, rounded down to a whole percent.
| `skill` | none | Skill id. Sets the context value `skill_level` to the character's level in this skill, with the progress towards the next level and enchantments (`u_skill()` gives only the whole level).
| `energy` | none | Number or variable object: kJ of UPS power the character needs, used up when the process starts.
| `requirements` | none | Requirement id: tools and components the character needs nearby, used up when the process starts. What's missing shows in a popup.
| `firestarter` | false | The process needs fire: examining the furniture asks for a fire source and lights it with one of the character's firestarters (or a lighter CBM), and a firestarter used on the furniture starts it too. The yield, `requirements` and `energy` are checked once the fire is lit. Furniture only.
| `query` | none | Question asked before starting (not with `firestarter`).
| `reset_age` | false | Without `intermediate`: the items on the tile are made new, so the process time counts from now. Otherwise it counts from when the first item was made (a stook dries grain from its harvest).
| `furn_set` | | Mandatory. Furniture the tile becomes.
| `effect_on_conditions` | none | Run once the process has started, with the character as `u`.

An **item filter** is an object with `materials` (material ids), `flags` (item flag ids) and `items` (item ids): an item matches when it is made of one of the materials, has one of the flags or is one of the items. When none of those lists is given, every item matches. `"fresh": true` also asks for the item not to be rotten, and `"bionic": true` for it to be a CBM.

The math of `yield_percent` and `energy`, and the `effect_on_conditions`, get the context values `this` (furniture id), `pos` (location), `volume` (mL of the items on the tile) and, with `skill`, `skill_level`; in math they are `_volume`, `_skill_level` and so on.

The checks run in this order, and the first that fails says why and stops: each item (`reject_if_contains`, then `accepts`), an empty tile, `requires_outside`, the yield, `requirements`, `energy`, then `ready_message` and `query`.

#### Messages

Each message is a string, or `{ "text": "...", "type": "bad" }` with a message type (`good`, `bad`, `mixed`, `warning`, `info`, `neutral`; default `neutral`, `bad` for `reject_message`). All are optional.

| Field | When
|---    |---
| `contains_message` | An item matches `reject_if_contains`. A message or an array of them; `%s` is the item.
| `reject_message` | An item doesn't match `accepts`; `%s` is the item.
| `empty_message` | Nothing on the tile matches.
| `outside_message` | The furniture isn't outside.
| `too_small_message` | The yield is less than one charge of `intermediate`.
| `no_energy_message` | The character lacks the UPS power.
| `ready_message` | Before `query`; `%1$s` is the volume of the items and `%2$s` its unit.
| `success_message` | The process has started.

#### Example
The vanilla rock kiln:
```jsonc
"examine_action": {
  "type": "process_start",
  "accepts": { "materials": [ "wood", "bone" ] },
  "reject_if_contains": { "items": [ "charcoal" ] },
  "contains_message": [ "This kiln already contains charcoal.", "Remove it before firing the kiln again." ],
  "reject_message": "This kiln contains %s, which can't be made into charcoal!",
  "empty_message": "This kiln is empty.  Fill it with wood or bone and try again.",
  "skill": "fabrication",
  "yield_percent": { "math": [ "100 - trunc(60 - 2 * _skill_level)" ] },
  "intermediate": "unfinished_charcoal",
  "too_small_message": "The batch in this kiln is too small to yield any charcoal.",
  "firestarter": true,
  "furn_set": "f_kiln_full"
}
```

A furnace that runs on a UPS:
```jsonc
"examine_action": {
  "type": "process_start",
  "accepts": { "materials": [ "cac2powder" ] },
  "reject_if_contains": { "items": [ "chem_carbide" ] },
  "contains_message": [ "This furnace already contains calcium carbide.", "Remove it before activating the arc furnace again." ],
  "yield_percent": { "math": [ "40 + 2 * u_skill('fabrication')" ] },
  "intermediate": "chem_carbide",
  "energy": 1250,
  "no_energy_message": "This furnace is ready to be turned on, but you lack a UPS with sufficient power.",
  "ready_message": "This furnace contains %1$s %2$s of material, and is ready to be turned on.",
  "query": "Turn on the furnace?",
  "success_message": "You turn on the furnace.",
  "furn_set": "f_arcfurnace_full"
}
```

### `process_finish`

Examines furniture whose process is running: says how long it still needs, and once its time is up turns the contents into the output and switches the furniture back.

| Field | Default | Description
|---    |---      |---
| `duration` | | Mandatory. Duration or variable object: how long the process takes, from the birthday of the first item on the tile.
| `furn_set` | | Mandatory. Furniture the tile becomes when the process is done, or at once when the tile is empty.
| `accepts` | any item | Item filter, as for `process_start`: an item that doesn't match stops the examination with `reject_message` (`%s` is the item).
| `output` | none | `{ "item": id, "from": [ ids ] }`: the items of the `from` types become `item`, with as many charges as fit in their volume (`%d` in `done_message`).
| `convert_to_smoking_result` | false | Each item with a `smoking_result` becomes it, as old as the item was (the stook's drying).
| `sterilize_cbms` | false | Items without the `NO_PACKED` flag lose `NO_STERILE`; when every item is an unpacked CBM, the log warns that they will be contaminated at once.
| `effect_on_conditions` | none | Run when the process is done, with the character as `u` and the context values `this` and `pos`.

Messages, as for `process_start`: `empty_message` (the tile is empty), `reject_message`, `examine_message` (shown before the time check), `done_message` (the process is done; with `convert_to_smoking_result` only if something was dried) and `time_left_message`. `time_left_message` is either a string with `%s` for the time left, or an object with `hours` (plural forms, `%d` hours), `less_than_an_hour` and `minutes` (plural forms, `%d` minutes): the first with more than an hour left, the second with more than half an hour, the third otherwise.

The kilns, the arc furnace, the stook and the autoclave are made of these two actors; see `data/json/furniture_and_terrain/furniture-tools.json`, `furniture-domestic_plants.json` and `furniture-medical.json`. The smoking racks and mills keep their hardcoded `smoker_options` and `quern_examine` actions: they load food from a menu, burn charcoal in proportion to the food, run on a timer item that finishes the process by itself, stop the food rotting while they run, and the mills work in recipe batches.

#### Example
```jsonc
"examine_action": {
  "type": "process_finish",
  "duration": "24 h",
  "empty_message": "This kiln is empty…",
  "examine_message": "There's a charcoal kiln there.",
  "time_left_message": {
    "hours": { "str": "It will finish burning in about %d hour.", "str_pl": "It will finish burning in about %d hours." },
    "less_than_an_hour": "It will finish burning in less than an hour.",
    "minutes": { "str": "It should take about %d minute to finish burning.", "str_pl": "It should take about %d minutes to finish burning." }
  },
  "output": { "item": "charcoal", "from": [ "unfinished_charcoal", "charcoal" ] },
  "done_message": "It has finished burning, yielding %d charcoal.",
  "furn_set": "f_kiln_empty"
}
```
