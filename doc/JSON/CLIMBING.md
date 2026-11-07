# Climbing Aids

## example definition

```jsonc
{
  "type": "climbing_aid",
  "id": "item_stepladder",
  "//": "We can generalize the grappling hook mechanism to portable ladders, so why not?",
  "slip_chance_mod": -20,
    "down": {
    "menu_text": "Place your stepladder below and climb down.",
    "menu_cant": "Can't place your stepladder (needs open space one story below).",
    "menu_hotkey": "l",
    "confirm_text": "Climb down with your stepladder?",
    "msg_after": "You lower the ladder into place and climb down.",
    "//": "This removes a stepladder from inventory and places it on the ground below.",
    "max_height": 1,
    "allow_remaining_height": false,
    "deploy_furn": "f_ladder",
    "easy_climb_back_up": 1
  },
  "condition": { "type": "item", "flag": "stepladder", "uses": 1 }
},
```
### `id`
Mandatory. The id of the climbing aid.

### `type`
Mandatory. Must be `climbing_aid`.

### `//`
A comment.  Disregarded by the parser.  Can be used in any object.  Encouraged, because climbing aids are more complicated than some other types.

#### `slip_chance_mod`
Optional.  Integer.  Defaults to 0.

This value modifies the chance for the character to slip and fall when using this climbing aid.  It is applied after calculating a base chance based on strength, dexterity, stamina, carried weight, wetness and other factors.  The basic slip chance for a beginning character with a light load and most of their stamina is around 10%.  When climbing down multiple stories, the chance to slip occurs once per story.

----

#### `condition`

Mandatory.  Object with the fields listed below.  A single condition governing whether this climbing aid is available.

For the most part, each climbing aid will have a unique condition, with possible exceptions for those that optionally deploy furniture, such as the `VINES2` and `VINES3` mutations.

#### `condition` › `type`

Mandatory.  String.  One of the following.

* `ter_furn`:  One of the tiles on the climbing route has terrain or furniture with the given `flag`.
* `veh`:  The tile just below the ledge has a vehicle part with the given `flag`; an empty `flag` (`""`) means any vehicle.
* `character`:  The one climbing has the given `flag`.
* `trait`:  The one climbing has the trait named by the `flag` field.
* `item`:  The character climbing has the item identified by the `flag` field in their inventory.
* `special`:  Used for special conditions hardcoded into the C++ engine of the game.

Note:  When climbing down, `ter_furn` and `veh` are checked for the space below a ledge and every space below.  The `ter_furn` check stops at the first Z-level with solid ground or any type of furniture

#### `condition` › `flag`

Mandatory.  String.  An identifier, usually a flag, indicating the specific condition within the `type`.

#### `condition` › `uses`

Mandatory if `type` is `item`.  Not allowed for other types.  The number of an item that will be consumed when climbing.  Usually 0 or 1.

#### `condition` › `range`

Optional if `type` is `ter_furn`.  Not allowed otherwise.  Integer.  Default 1.  Defines the maximum distance from the player, in stories, for this aid to be used.

Note:  This is not fully implemented.

-----

#### `down`

Mandatory unless the aid has an [`up`](#up) block.  Object with the fields listed below.  Rules for climbing down with this aid.  Does not apply to stairs or stair-like objects (eg, permanent ladders) at this time.

Note:  The player can climb down by walking into a ledge or using `>` near a ledge.  This will first present a menu with any climbing aids that can deploy furniture, plus the safest climbing aid that does *not* deploy furniture based on `slip_chance_mod`.  eg:  A stepladder takes priority over a downspout.

#### `down` › `max_height`
Optional.  Integer.  Default 1.  The maximum number of stories that a creature can descend using this climbing aid.

Set this to 0 to disable using this climbing aid to climb down: it isn't offered when climbing down, and the rest of the `down` block can be left out.  An aid without `down` has this.

#### `down` › `allow_remaining_height`

Optional.  Boolean.  Default true.  Whether this climbing aid may be used to climb only part of the distance down to the ground.  Typically, the player will fall after a partial climb down.  eg, this is set to false for the climbing aid 'place my stepladder below'.

Note:  Due to limited implementation, this should only be set to `false` for climbing aids that use `deploy_furn`, otherwise the player may be prevented from climbing.  A grayed-out menu option with `menu_cant` text will be displayed when an aid is disabled by this rule.

#### `down` › `sheer_drop`

Optional.  Boolean.  Default false.  Whether this climbing aid is also offered when the player takes stairs down (`>`) and the stairs have no counterpart below, ending in a sheer drop.  Only aids whose condition is of type `character`, `trait` or `item` can be used there.  The game first offers its built-in ways down (`WEB_RAPPEL`, `VINES2`/`VINES3`, a grappling hook, a long rope); when none applies, the safest aid with `sheer_drop` (lowest `slip_chance_mod`) is offered instead of jumping.  The prompt is "There is a sheer drop halfway down." followed by `confirm_text` (don't use `%s` in it).  On yes: `msg_before`, the item is used up (for `item` aids with `uses`), `cost` is paid once, then `msg_after`.  With `easy_climb_back_up` above 0 the aid leaves a way back up: its `deploy_furn` (on the tile you land on), or a rope (`f_rope_up`) when it has none.  There is no slip check.

#### `down` › `easy_climb_back_up`

Optional.  Integer.  Default 0.  When climbing down this many stories or fewer, the player will be told it should be easy to climb back up.  This bypasses the normal estimation of climbing difficulty.  Typically set to the same value as `max_height` for climbing aids that leave climbable furniture behind.

#### `down` › `confirm_text`

Mandatory.  String.  A sentence in the form of a question, shown at the end of a prompt describing the dangers of the climb.  This prompt appears *after* choosing this climbing aid in a menu (see below).

This string may use `%s` to refer to the furniture one Z-level below the player on the climbing path.  `%s` will be expanded into a string of the form `"the (furniture)"`.

#### `down` › `menu_text`

Mandatory.  String.  Text of the menu option for using this climbing aid.

This string may use `%s` to refer to the furniture one Z-level below the player on the climbing path.  `%s` will be expanded into a string of the form `"the (furniture)"`.

#### `down` › `menu_cant`

Mandatory if `deploy_furn` is set.  Optional otherwise.  String.  This is the greyed-out version of the menu option displayed when a climbing aid's condition is satisfied but `allow_remaining_height` or existing furniture prevents it from being used.

This string may use `%s` similar to `menu_text`.

#### `down` › `menu_hotkey`

Mandatory is `deploy_furn` is set.  Optional otherwise.  A string containing a single ASCII character.  This is the default English-language hotkey for selecting this climbing aid.  Avoid using the same letter as other climbing aids, and avoid `j`, `p` and `f` which are used for additional options in the ledge examination menu.

Note:  Each climbing option that can deploy furniture should get its own unique hotkey.  Non-deploying options default to `c`.

#### `down` › `msg_before`

Optional.  String.  Message added to the log before climbing down.  This message will be logged even if the player slips and falls.

#### `down` › `msg_after`

Optional.  String.  Message added to the log after climbing down.  This message will only be logged if the player successfully descends with the climbing aid (although this does not necessarily mean the player climbed all the way to the ground).

#### `down` › `deploy_furn`

Optional.  Furniture ID string.  Identifies a type of furniture which will be created at every Z-level beneath the character until they reach the end of their climb.  At most `max_height` copies of the furniture will be created.

Note:  A climbing aid will be disabled if there is any furniture in spaces where new furniture would be deployed.  It will still appear in the menu, grayed out, with its `menu_cant` text.

Note:  Pay special attention to whether deployed furniture has the `ALLOW_ON_OPEN_AIR` flag.  eg: webs and grappling hooks do, stepladders don't.  `allow_remaining_height: false` can be used to avoid placing furniture without this flag on open air.

#### `down` › `cost`

Optional.  Object containing the fields below.  This penalty is applied to the player once, after a climb down that didn't slip.

#### `down` › `cost` › `kcal`

Optional.  Integer.  Food calories used up per story.

#### `down` › `cost` › `thirst`

Optional.  Integer.  Thirst increase per story.

#### `down` › `cost` › `pain`

Optional.  Integer.  Pain increase per story.

#### `down` › `cost` › `damage`

Optional.  Integer.  Damage taken to the torso per story.

-----

#### `up`

Optional.  Object with the fields listed below.  Rules for climbing up a Z-level with this aid (`<` where there are no stairs or ladder: up a wall, furniture or a ledge above).  An aid without `up` isn't used to climb up.

When climbing up, the conditions are checked at the climber's own tile (its terrain, furniture and vehicle) and on the climber (flags, traits, items).  The safest matching aid with an `up` block (lowest `slip_chance_mod`) is used for the slip chance, the move cost and the cost below.  If none matches, `furn_CLIMBABLE` is used, as vanilla does.  Characters with the `CLIMB_FLYING` flag fly up instead and use no aid.  Items aren't used up when climbing up.

```jsonc
{
  "type": "climbing_aid",
  "id": "item_climbing_harness",
  "slip_chance_mod": -30,
  "up": { "move_cost": 300, "needs_support": false, "cost": { "kcal": 20 }, "msg_after": "Your harness takes the strain." },
  "condition": { "type": "item", "flag": "climbing_harness", "uses": 0 }
}
```

#### `up` › `move_cost`

Optional.  Integer.  Default 500.  Moves added to the climbing cost of the terrain and furniture braced against; twice this when nothing braces the climber (`CLIMB_NO_LADDER`, `WALL_CLING`, a nearby `CLIMB_ADJACENT` tile, or an aid with `needs_support: false`).

#### `up` › `needs_support`

Optional.  Boolean.  Default true.  If false, the aid alone lets the character climb up, with no walls or furniture to brace against (a grappling hook, climbing gear).

#### `up` › `cost`

Optional.  The same object as [`down` › `cost`](#down--cost), applied once after a climb up that didn't slip.

#### `up` › `msg_after`

Optional.  String.  Message added to the log after a successful climb up.

-----

#### Events

The event `character_climbs` (see [EFFECT_ON_CONDITION.md](EFFECT_ON_CONDITION.md)) is sent after the avatar climbs up or down without slipping, with the aid's id, the direction and the Z-levels climbed: an EOC on it can follow up on a climb.  `character_falls` is sent when a character falls.
