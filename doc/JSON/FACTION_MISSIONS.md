<!-- START doctoc generated TOC please keep comment here to allow auto update -->
<!-- DON'T EDIT THIS SECTION, INSTEAD RE-RUN doctoc TO UPDATE -->
- [Overview](#overview)
- [Faction Mission JSON Object](#faction-mission-json-object)
  - [Behaviors](#behaviors)
  - [Results](#results)
  - [Encounter](#encounter)
  - [EOCs](#eocs)
  - [Saves](#saves)
  - [Examples](#examples)
<!-- END doctoc generated TOC please keep comment here to allow auto update -->

# Overview

Faction Camps (aka basecamps) can send NPC followers on missions to perform some actions.

Every camp mission that sends a companion out from a camp expansion is a `faction_mission`
object.  The camp board offers it at each expansion (or the camp's own tile) that has all its
`required_provides`, sends a companion for its `duration`, and brings the companion back with its
`results`.  A mission's `behavior` names the code that runs it: `generic` (the default) runs the
mission entirely from its JSON, the other behaviors are the bespoke missions (hide sites, farming,
recruiting, ...), which take what they can from the JSON (gating, texts, companion skill,
exertion, training, EOCs) and do the rest themselves.

Not faction missions, and still offered by the code: camp upgrades, crafting, map fortifications,
the camp's own actions (distribute food, assign jobs, ...) and emergency recall.

# Faction Mission JSON Object

| Field | Required? | Description |
| ----- | --------- | ----------- |
| `"type"` | yes | Must be `"faction_mission"` |
| `"id"` | yes | Unique identifier for the mission, often `"camp_something"` |
| `"name"` | yes | Short name for identifying the mission in a list or header |
| `"desc"` | yes | Longer description of the mission |
| `"behavior"` | no | What runs the mission, see [Behaviors](#behaviors).  Default `"generic"` |
| `"required_provides"` | no | Array of camp features the expansion must have for the mission to be offered there: a string (a blueprint `"provides"` id, or an upgrade's recipe id, which every upgrade provides), or `{ "id": "...", "amount": 2 }` for at least that much of it (amounts of the same id add up across an expansion's upgrades, see [BASECAMP.md](BASECAMP.md#blueprint-requires-provides-and-excludes)).  Without it the mission is offered at every expansion |
| `"skill"` | no | The skill companions are ranked by in the menu that chooses one, shown as "Skill used" |
| `"skill_level"` | no | A companion needs at least this level of `"skill"` to be sent.  Default 0 |
| `"activity"` | no | The exertion the companion is fed for (and that is shown), see [Player Activity](../PLAYER_ACTIVITY.md#json-properties).  Without it the companion is fed as `NO_EXERCISE` and no activity level is shown |
| `"duration"` | for some behaviors | How long the companion is out: e.g. `"5 h"`.  Required by `generic`, `menial`, `survey_field`, `survey_expansion` and `recruiting`; the other behaviors work out their own time, and giving one is a load error |
| `"repeatable"` | no | `generic` only.  If true the companion keeps working in cycles of `"duration"` until it is called back, brings back the results of every whole cycle, and pays at the end for the food of the time past the first cycle; the return entry is available once one cycle is done.  If false (default) the companion is back after `"duration"` with one cycle's results |
| `"positions"` | no | How many companions can be out on the mission at once at one expansion.  Default 1 |
| `"train"` | no | `{ "skill": "survival", "difficulty": 2 }`: what a companion back from the mission practices, for as long as it was out.  `"skill"` is a skill id, a `companion_skill_practice` group of the skills (`"gathering"`, `"hunting"`, `"trapping"`, `"menial"`, `"construction"`, `"recruiting"`, see `skills.json`) or `"combat"` (the companion's best combat skill and the `combat` group); `"difficulty"` defaults to 1.  Without it nothing is trained |
| `"results"` | no | `generic` only.  What the companion brings back, see [Results](#results) |
| `"encounter"` | no | `generic` only.  A chance of a random encounter when the companion comes back, see [Encounter](#encounter) |
| `"start_eocs"` | no | EOCs (ids or inline) run when a companion sets off, see [EOCs](#eocs) |
| `"return_eocs"` | no | EOCs run when a companion comes back from the mission (not when it is called back by emergency recall, or dies), after what it brought back is in camp storage |
| `"action_name"` | no | Menu entry that sends a companion, after the expansion's direction (e.g. `[N] Go Fishing`).  Default `"name"` |
| `"return_name"` | no | Menu entry that brings companions back.  Default `Recover Ally from <name>` |
| `"progress"` | no | Header of the list of the companions out on the mission, in the return entry.  Default `"name"` |
| `"depart_message"` | no | Popup after the companion's name when it sets off: `"departs to search for edible plants…"`.  Default `departs on a mission…` |
| `"return_message"` | no | Popup after the companion's name when it comes back.  Default `returns from the mission…` |
| `"items_label"` | no | First word in "Foo possibilities:" header for list of possible items.  For a mission with `"results"` and no `"items_possibilities"`, the list is a sample of the results' item group for the current season |
| `"items_possibilities"` | no | Array of names of possible items returned from the mission |
| `"effects"` | no | Array of descriptions of what the mission accomplishes |
| `"risk"` | no | Display only.  One of `"NONE"`, `"VERY_LOW"`, `"LOW"`, `"MEDIUM"`, `"HIGH"`, `"VERY_HIGH"` |
| `"difficulty"` | no | Display only.  One of `"NONE"`, `"VERY_LOW"`, `"LOW"`, `"MEDIUM"`, `"HIGH"`, `"VERY_HIGH"` |
| `"time"` | no | Display only: description of how long the mission takes.  Without it a mission with a `"duration"` shows that |
| `"footer"` | no | Text to display at the bottom of the entire description |

The camp board lists a direction's missions in this order: its upgrades, then the faction
missions offered there in the order they were loaded, then map fortifications, crafting and
hiding missions.  The `survey_field` and `survey_expansion` missions are listed first, with the
camp's own actions.

## Behaviors

| Behavior | What it does | Uses |
| -------- | ------------ | ---- |
| `generic` | Sends a companion for `"duration"`, then brings it back with its `"results"`.  Vanilla gathering, firewood, foraging, trapping and hunting are generic missions | everything above |
| `menial` | Not a mission: the companion sorts the camp's unsorted loot zone on the spot (camp_menial).  `"duration"` is the time whose food it needs in the larder; a companion saved out on the old menial mission comes back after it | `"duration"`, the texts, `"train"` (for those old companions), `"start_eocs"` |
| `cut_logs`, `clearcut` | Logging and clear-cutting a forest tile the player picks (6 hours plus the work and the trips) | gating, texts, `"skill"`, `"activity"`, `"train"`, EOCs |
| `setup_hide_site`, `relay_hide_site` | Building a hide site, or moving gear to or from one | as above |
| `scouting`, `combat_patrol` | Patrolling a path of checkpoints the player picks (`combat_patrol` fights what it meets) | as above |
| `plow`, `plant`, `harvest` | Farming the expansion's plots; only offered while there is room for another companion and there are plots to work (`plant` also needs it warm enough to plant) | as above |
| `recruiting` | Searching for a recruit for `"duration"`.  Its description is the camp's recruiting evaluation instead of `"desc"` | `"duration"` and as above |
| `survey_field`, `survey_expansion` | Offered only at the camp's own tile, with no direction in their menu entries: converting a neighbouring tile to field, and founding an expansion.  `survey_field` is only offered while a neighbouring tile isn't a field, `survey_expansion` only startable when some neighbour can take an expansion | `"duration"` and as above.  `survey_expansion`'s `"return_message"` and `"train"` are for a built expansion; giving up keeps the code's message and trains construction at 0 |

Several missions can share a behavior: a mod can define its own logging or farming mission with
other texts, gating or training.

## Results

For a `generic` mission, rolled when the companion comes back:

```json
"results": {
  "item_group": "test_camp_catch_default",
  "seasonal_item_groups": { "summer": "test_camp_catch_summer" },
  "monster_groups": [ "GROUP_CAMP_HUNTING", "GROUP_CAMP_TRAPPING" ],
  "attempts": 3,
  "difficulty": 15,
  "skill_score": { "skills": { "survival": 2 }, "stats": { "perception": 1 }, "constant": 0 }
}
```

| Field | Description |
| ----- | ----------- |
| `"attempts"` | Attempts per cycle (default 1).  An attempt succeeds when the companion's skill score, rounded, is greater than a roll from 0 to `"difficulty"` (default 0) |
| `"item_group"` | Each successful attempt rolls one item from this group |
| `"seasonal_item_groups"` | Object of `"spring"`, `"summer"`, `"autumn"`, `"winter"` to an item group used instead of `"item_group"` in that season |
| `"monster_groups"` | Each successful attempt brings back a corpse (with its pack) from one of these groups, picked weighted by each group's total frequency.  With an item group too, both are rolled |
| `"skill_score"` | The companion's score, worked out before the mission trains it: each of `"skills"` (with the progress into the next level) times its weight, plus each of `"stats"` (`"strength"`, `"dexterity"`, `"intelligence"`, `"perception"`, current values) times its weight, plus `"constant"` (default 0).  Without `"skill_score"` the score is 1, so with `"difficulty"` 0 every attempt succeeds |

What the companion brings back goes to camp storage, like the results of every camp mission.
Results can also come from `"return_eocs"`.

## Encounter

`"encounter": { "one_in": 15, "favor": 2, "threat": 10, "task": "foraging for edible plants" }`:
one in `"one_in"` returns from a `generic` mission start with a random encounter ("While
foraging for edible plants, a silent specter approaches..."), the same one vanilla gathering
missions have.  The companion's survival and speech plus `"favor"` help it get away or talk its
way out; otherwise it fights up to `"threat"` monsters with its combat skills, and may die (its
death EOCs run, see the `companion_mission_finished` event).  A companion that dies brings
nothing back.

## EOCs

`"start_eocs"` and `"return_eocs"` run with the companion as alpha and the avatar as beta, and
these context variables:

| Variable | Value |
| -------- | ----- |
| `camp_mission` | The faction_mission id |
| `pos` | The camp's location (its overmap tile's north-west corner) |

The `companion_mission_started` and `companion_mission_finished` events (see
[EFFECT_ON_CONDITION.md](EFFECT_ON_CONDITION.md)) have a `camp_mission` field with the same id.

## Saves

A companion out on a mission saves the mission's id and behavior.  When it loads, the mission's
JSON says what runs it, so a mod that changes a mission's behavior also changes how companions
already out on it come back.  A companion saved out on a vanilla mission before missions had ids
comes back as that vanilla mission.  A companion out on a mission whose JSON is gone (its mod was
removed) can still be brought back by emergency recall.

## Examples

A fishing mission for a mod whose dock upgrade provides `fishing_dock`: out for five hours, it
brings back three fish in summer, otherwise driftwood.

```json
  {
    "type": "faction_mission",
    "id": "camp_fishing",
    "name": "Fishing",
    "desc": "Send a companion to fish from the dock.",
    "skill": "survival",
    "activity": "LIGHT_EXERCISE",
    "positions": 2,
    "required_provides": [ "fishing_dock" ],
    "duration": "5 h",
    "action_name": "Go Fishing",
    "return_name": "Recover Fisher",
    "progress": "Fishing off the dock.",
    "depart_message": "heads to the dock with a rod…",
    "return_message": "comes back from the dock smelling of fish…",
    "train": { "skill": "survival", "difficulty": 2 },
    "items_label": "Fishing",
    "results": {
      "item_group": "fishing_driftwood",
      "seasonal_item_groups": { "summer": "fishing_summer_catch" },
      "attempts": 3
    },
    "return_eocs": [ "EOC_FISHING_TALL_TALES" ]
  }
```

Vanilla foraging (from `data/json/faction_missions.json`), a repeatable generic mission:

```json
  {
    "type": "faction_mission",
    "id": "camp_foraging",
    "name": "Foraging",
    "desc": "Send a companion to forage for edible plants.",
    "skill": "survival",
    "items_label": "Foraging",
    "items_possibilities": [ "wild vegetables", "fruits and nuts depending on season" ],
    "risk": "VERY_LOW",
    "activity": "LIGHT_EXERCISE",
    "time": "4 Hours, Repeated",
    "positions": 3,
    "required_provides": [ "foraging" ],
    "duration": "4 h",
    "repeatable": true,
    "action_name": "Forage for plants",
    "return_name": "Recover Foragers",
    "progress": "Foraging for edible plants.",
    "depart_message": "departs to search for edible plants…",
    "return_message": "returns from foraging for edible plants carrying supplies and has a bit more experience…",
    "train": { "skill": "gathering", "difficulty": 1 },
    "encounter": { "one_in": 15, "favor": 2, "threat": 10, "task": "foraging for edible plants" },
    "results": {
      "seasonal_item_groups": {
        "spring": "foraging_faction_camp_spring",
        "summer": "foraging_faction_camp_summer",
        "autumn": "foraging_faction_camp_autumn",
        "winter": "foraging_faction_camp_winter"
      },
      "attempts": 12,
      "difficulty": 15,
      "skill_score": { "skills": { "survival": 2 }, "stats": { "perception": 1 } }
    }
  }
```

A bespoke behavior with its texts and training in JSON:

```json
  {
    "type": "faction_mission",
    "id": "camp_cut_logs",
    "name": "Cut Logs",
    "desc": "Send a companion to a nearby forest to cut logs.",
    "behavior": "cut_logs",
    "skill": "fabrication",
    "skill_level": 2,
    "activity": "ACTIVE_EXERCISE",
    "positions": 1,
    "required_provides": [ "logging" ],
    "action_name": "Cut Logs",
    "return_name": "Recover Log Cutter",
    "depart_message": "departs to cut logs…",
    "return_message": "returns from working in the woods…",
    "train": { "skill": "construction", "difficulty": 2 }
  }
```
