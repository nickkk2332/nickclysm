# NPC behavior

## Categories

Every goal an NPC can commit to belongs to a decision category. Categories are ranked: a goal in an earlier category interrupts one in a later category, and within a category the higher score wins. The decision tree picks a goal each turn by its own structure (a `utility` node compares the scores of its children, whatever their categories); the categories then settle whether that pick takes over from the goal the NPC is already committed to.

Categories are the JSON type `npc_decision_category`. The built-in ones are in `data/json/npcs/npc_decision_categories.json`, first to last:

| Category | Built-in goals |
| --- | --- |
| `combat` | `fight`, `flee` |
| `investigate` | `investigate_sound` |
| `needs` | `drink_water`, `eat_food`, `go_to_sleep`, `start_fire`, `regulate_temperature` |
| `follow` | `follow_player`, `follow_embarked` |
| `order` | `goto_ordered_position` |
| `duty` | `return_to_guard_pos`, `hold_position` |
| `camp_work` | `camp_work` |
| `camp_travel` | `return_to_camp` |
| `free_time` | `free_time` |
| `idle` | `idle` |
| `unmodeled` | none: a goal no category names counts as `unmodeled` |

| Field | Optionality | Value | Info |
| --- | --- | --- | --- |
| `"id"` | **mandatory** | string | the category's id |
| `"goals"` | optional | array of strings | built-in goals in this category; supports `extend` and `delete` with `copy-from`. A JSON goal names its category itself (see [JSON goals](#json-goals)) |
| `"before"`, `"after"` | optional | string | the category this one goes right before or right after. Not both. A category with neither keeps its place in load order |
| `"hysteresis"` | optional | number | default 0.1: the share added to the committed goal's score when a goal of the same category challenges it (0.1 is 10%) |

A mod adds a category before or after one that exists:

```jsonc
{ "type": "npc_decision_category", "id": "worship", "after": "needs", "hysteresis": 0.25 }
```

Several categories placed after the same one keep their load order. A category can be placed relative to another mod category too; a loop, or a neighbour that doesn't exist, is a load error.

To move a built-in goal, `delete` it from its category and `extend` another with it. The C++ code still acts on the built-in categories by id (`combat`, `needs` and so on), so they must exist; reorder them rather than deleting them.

### Interruption

Each turn the NPC compares the goal the tree picks (the challenger) with the goal it is committed to:

- **Different categories:** the challenger takes over when its category comes first. A goal never takes over from a goal of an earlier category by score; only reordering the categories can do that.
- **Same category, a JSON goal on either side:** the challenger takes over when its score is above the committed goal's score times 1 + the category's `hysteresis`. With the default 10%, a committed goal scoring 0.5 holds against challengers up to 0.55.
- **Same category, both built-in:** a need takes over from another need when it is more urgent by a margin that grows with the cost of breaking off the first one; other built-in goals stay until their own end.

The committed goal's score is its own node's score this turn, so give a JSON goal's leaf its score. A JSON goal whose node's conditions no longer hold scores 0, so any goal of its category with a score takes over.

The obligations that hold back a built-in need (walking with the player, a guard post, an order to go somewhere) aren't judged for JSON goals: their categories are. A walk a JSON goal sends the NPC on is an order like any [`u_walk_to`](EFFECT_ON_CONDITION.md#u_walk_to-npc_walk_to) walk, though: while the NPC walks it, needs that would take it elsewhere wait until they're urgent.

Reactions to danger are decided before the tree: fighting or fleeing a threat (and escaping an explosion) count as `combat`, investigating a sound as `investigate`, using an inhaler during an asthma attack as `needs`. A JSON goal of a later category ends as interrupted when one comes; a JSON goal of the same category or an earlier one keeps the NPC on it, and the reaction waits. A JSON goal in a category before `combat` is taken up even in a fight, when the tree picks it.

## JSON goals

A behavior leaf can carry its goal as an object instead of a string, and the category it belongs to:

```jsonc
{
  "type": "behavior",
  "id": "npc_pray_at_dawn",
  "category": "worship",
  "conditions": [ { "predicate": "condition", "condition": { "u_has_trait": "DEVOUT" } } ],
  "score": { "math": [ "1 - time_since('sunrise') / time('2 h')" ] },
  "goal": {
    "id": "pray_at_dawn",
    "on_start": [ "EOC_FIND_SHRINE_AND_WALK" ],
    "on_turn": [ "EOC_PRAY_IF_AT_SHRINE" ],
    "until": { "u_has_var": "prayed_today" },
    "timeout": "2 hours",
    "on_interrupt": [ "EOC_PRAYER_INTERRUPTED" ]
  }
}
```

| Field | Optionality | Value | Info |
| --- | --- | --- | --- |
| `"category"` | **mandatory** with a goal object | string | the goal's [category](#categories). On a node with a goal string it states that goal's category, which must agree with the category's `goals` list |
| `"goal"` / `"id"` | **mandatory** | string | the goal's id. It can't be a built-in goal's id, and only one node defines a goal; other nodes may name it as a plain `"goal": "pray_at_dawn"` |
| `"on_start"` | optional | array of EoCs | run when the NPC takes the goal up |
| `"on_turn"` | optional | array of EoCs | run once each turn the goal is active, the first turn included |
| `"until"` | optional | [condition](EFFECT_ON_CONDITION.md#possible-conditions) | the goal ends as `done` once this holds. It's checked at the start of each turn, before `on_turn` |
| `"timeout"` | optional | duration | the goal ends as `timeout` this long after it started |
| `"on_end"` | optional | array of EoCs | run whenever the goal ends, after `on_timeout` or `on_interrupt` |
| `"on_interrupt"` | optional | array of EoCs | run when a goal of an earlier category, a higher score within the category, or a reaction to danger takes over, or when something ends the commitment (a "Follow me" order) |
| `"on_timeout"` | optional | array of EoCs | run when the timeout passed |

EoCs may be ids or inline EoCs. They run with the NPC as `u`, and so does `until`. Their `npc` is an empty talker: an `npc_` condition is false and an `n_` math function gives 0, but neither breaks the game, as they would with no `npc` at all. They get these context values:

| Context value | Value |
| --- | --- |
| `goal_id` | the goal's id |
| `turns_active` | goal-active turns so far: 0 in `on_start`, 1 in the first `on_turn` |
| `reason` | in `on_end`, `on_interrupt` and `on_timeout`: `done`, `timeout` or `interrupted` |

**A goal-active turn.** `on_turn` runs. If the EoCs gave the NPC something to do, it does it through its usual move code:

- a walk sent by the goal's own EoCs (`u_walk_to`, or `u_pickup_item` with `"walk": true`) is walked, a step a turn;
- an activity (`u_assign_activity`) goes on, a task of several steps too;
- what costs moves right away (`u_examine`, `u_attack`, an item effect) is done.

If they gave it nothing and spent no moves, the NPC waits out the turn. A goal never stalls the game. `on_turn` runs once a turn, however often the NPC moves in it.

**The goal's walk.** A walk keeps the context values of the EoC that sent it, so a walk from the goal's EoCs carries its `goal_id`; so does one sent from that walk's own `true_eocs`. Such a walk is the goal at work: the tree sees it as a walk the NPC was ordered on (`goto_ordered_position`), but that doesn't take over from the goal, whatever the goal's category. When the goal ends, its walk is dropped without its EoCs. An activity the goal started goes on unless an end hook stops it (`u_cancel_activity`).

**Saves.** The goal, when it started and its turn count are saved with the NPC. After a load the NPC is on the goal again without `on_start` running; its walk comes back with the save too.

**Load errors.** A goal with a built-in goal's id, a goal object without `category`, a goal defined by two nodes, an unknown category, one goal given two categories (by two nodes, or by a node and a category's `goals`), a `timeout` of 0, an EoC that doesn't exist.

### Worked example: fetching a crowbar

A camp NPC with free time goes for a crowbar lying within 10 tiles, picks it up and comes back to where it stood. It uses the [errand effects](EFFECT_ON_CONDITION.md#u_walk_to-npc_walk_to): `u_map_run_item_eocs` finds the crowbar, `u_pickup_item` with `"walk": true` walks to it and takes it, and `u_walk_to` brings the NPC back.

```jsonc
[
  {
    "type": "behavior",
    "id": "npc_fetch_crowbar",
    "category": "free_time",
    "conditions": [
      { "predicate": "npc_is_camp_idle" },
      {
        "predicate": "condition",
        "condition": { "and": [ { "not": { "u_has_item": "crowbar" } }, { "math": [ "u_fetch_crowbar_failed != 1" ] } ] }
      }
    ],
    "score": "npc_free_time_urgency",
    "goal": {
      "id": "fetch_crowbar",
      "on_start": [ "EOC_FETCH_CROWBAR_START" ],
      "until": {
        "or": [
          { "and": [ { "u_has_item": "crowbar" }, { "math": [ "u_fetch_crowbar_back == 1" ] } ] },
          { "math": [ "u_fetch_crowbar_failed == 1" ] }
        ]
      },
      "timeout": "30 minutes",
      "on_end": [ "EOC_FETCH_CROWBAR_END" ]
    }
  },
  {
    "type": "effect_on_condition",
    "id": "EOC_FETCH_CROWBAR_START",
    "effect": [
      { "math": [ "u_fetch_crowbar_back = 0" ] },
      { "u_location_variable": { "u_val": "fetch_crowbar_home" } },
      {
        "u_map_run_item_eocs": "random",
        "search_data": [ { "id": "crowbar" } ],
        "max_radius": 10,
        "true_eocs": [
          {
            "id": "EOC_FETCH_CROWBAR_GO",
            "effect": [
              {
                "u_pickup_item": "npc",
                "walk": true,
                "true_eocs": [ "EOC_FETCH_CROWBAR_RETURN" ],
                "false_eocs": [ { "id": "EOC_FETCH_CROWBAR_LOST", "effect": [ { "math": [ "u_fetch_crowbar_failed = 1" ] } ] } ]
              }
            ]
          }
        ],
        "false_eocs": [ "EOC_FETCH_CROWBAR_LOST" ]
      }
    ]
  },
  {
    "type": "effect_on_condition",
    "id": "EOC_FETCH_CROWBAR_RETURN",
    "effect": [
      {
        "u_walk_to": { "u_val": "fetch_crowbar_home" },
        "true_eocs": [ { "id": "EOC_FETCH_CROWBAR_BACK", "effect": [ { "math": [ "u_fetch_crowbar_back = 1" ] } ] } ]
      }
    ]
  },
  {
    "type": "effect_on_condition",
    "id": "EOC_FETCH_CROWBAR_END",
    "effect": [
      {
        "if": { "compare_string": [ "done", { "context_val": "reason" } ] },
        "then": { "u_message": "<u_name> is back with a crowbar.", "type": "good" },
        "else": { "u_message": "<u_name> gives up on the crowbar: <context_val:reason>." }
      }
    ]
  }
]
```

How it runs:

1. The node is a child of the free-time part of the tree (add it to `npc_priorities` with `extend`). When the tree picks it and nothing of an earlier category is committed, the NPC takes up `fetch_crowbar` and `on_start` runs: it remembers where the NPC stands and sends it for the nearest crowbar.
2. Each turn the NPC walks its pickup walk. The walk carries `goal_id`, so it is the goal's own: the tree's `goto_ordered_position` doesn't take over.
3. Next to the crowbar, the pickup takes it and its `true_eocs` send the NPC home with `u_walk_to`, again as the goal's walk.
4. Home, the walk's `true_eocs` set `u_fetch_crowbar_back`. Next turn `until` holds, the goal ends as `done` and `on_end` says so. Carrying a crowbar, the NPC no longer meets the node's conditions, so the tree doesn't pick the goal again.
5. Thirst on the way (`needs` comes before `free_time`) or a zombie (`combat`) interrupts the errand: the goal's walk is dropped and `on_end` runs with `reason` `interrupted`. No crowbar within reach, or none it can get to, ends it at once through `u_fetch_crowbar_failed`.

## Reading goals from EOCs

What an NPC is doing can be read in any EoC. The current goal is the one the NPC is committed to, built-in or JSON, or `idle` for none.

| Name | Kind | Gives |
| --- | --- | --- |
| `u_npc_goal`, `npc_npc_goal` | condition: string or [variable object](EFFECT_ON_CONDITION.md#variable-object) | whether this is the NPC's current goal |
| `npc_goal_category` | condition: string or variable object | whether u's current goal is in this category |
| `npc_goal_turns()`, `u_npc_goal_turns()`, `n_npc_goal_turns()` | math | turns since the current goal started; 0 for `idle` or a talker that isn't an NPC |
| `npc_goal_changed` | event | the NPC's goal changed |

```jsonc
{ "u_npc_goal": "pray_at_dawn" }
{ "npc_npc_goal": { "context_val": "expected_goal" } }
{ "npc_goal_category": "needs" }
{ "math": [ "npc_goal_turns() > 100" ] }
```

`npc_goal_changed` fires every time the goal an NPC is committed to changes, built-in goals included, with these fields; its EoCs run with the NPC as `u`:

| Field | Type |
| --- | --- |
| `npc` | `character_id` |
| `old_goal` | `string`, `idle` for none |
| `new_goal` | `string`, `idle` for none |

```jsonc
{
  "type": "effect_on_condition",
  "id": "EOC_SAY_WHEN_SLEEPY",
  "eoc_type": "EVENT",
  "required_event": "npc_goal_changed",
  "condition": { "compare_string": [ "go_to_sleep", { "context_val": "new_goal" } ] },
  "effect": [ { "u_message": "<u_name> yawns.", "type": "neutral" } ]
}
```

## Debugging

The debug menu's **Console…** has an **NPC decision** section in its **Creatures** tab: select an NPC and open it. It's worked out again each turn, so step the game with the console's controls to watch it change. It shows:

- the tree, depth first: each node's conditions, true or false, whether the node is running, succeeded or failed, its score, and for a leaf its goal and category. The path to the goal the tree picks is highlighted;
- the categories, first to last, with their hysteresis;
- the goal the NPC is committed to, its category, score and turns, the goal the tree picks, and why the committed goal stays or what takes over: an earlier category, the scores against the hysteresis bonus, `until` or the timeout.

With the debug log filter for NPC needs on (`DF_NPC_NEEDS`), the log also says when an NPC starts a JSON goal and when and why one ends.

The view's data comes from `npc_goal_runtime::report()` (src/npc_json_goal.h), which changes nothing, so tests can check what it shows.
