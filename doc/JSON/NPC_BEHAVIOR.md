# NPC behavior

How NPCs decide what to do each turn, and how mods change it.

## The decision tree

Every turn, each NPC walks one behavior tree, `npc_decision`, defined in
`data/json/npcs/npc_behavior.json`. The walk ends at a leaf node whose `goal` the NPC then
pursues (`"drink_water"`, `"follow_player"`...). When the walk finds no leaf that can make
progress, the goal is `"idle"`. Monsters pick their goals with nodes of the same type
(`data/json/monsters/monster_goals.json`).

The vanilla tree, in the order it is walked:

```
npc_decision                      fallback
├─ npc_combat                     fallback, when npc_in_danger
│  ├─ npc_flee                    goal flee
│  └─ npc_fight                   goal fight
├─ npc_investigate                goal investigate_sound
└─ npc_priorities                 utility: the highest score wins
   ├─ npc_needs                   utility
   │  ├─ npc_homeostasis          temperature
   │  ├─ npc_thirst
   │  ├─ npc_hunger
   │  └─ npc_sleepiness
   ├─ npc_duty                    guard post
   ├─ npc_follow_embarked
   ├─ npc_follow
   ├─ npc_player_order
   ├─ npc_camp_work
   ├─ npc_return_to_camp
   └─ npc_free_time
```

### Node fields

```jsonc
{
  "type": "behavior",
  "id": "npc_thirst",
  "strategy": "sequential",
  "conditions": [ { "predicate": "npc_needs_water_badly" } ],
  "children": [ "npc_obtain_water" ],
  "score": "npc_thirst_urgency"
}
```

| Field | Description
|---|---
| `type` | `"behavior"`.
| `id` | The node's id.
| `children` | (_optional_) The ids of the child nodes, in order. A node has either children or a `goal`.
| `strategy` | (_mandatory with children_) How the children are walked; see below.
| `goal` | (_mandatory without children_) The goal an NPC takes when the walk ends at this node.
| `conditions` | (_optional_) Tests the node must pass before its goal or children are considered; see [Conditions](#conditions).
| `score` | (_optional_) How much the NPC wants this node, for a `utility` parent; see [Scores](#scores).
| `evaluate_every` | (_optional_) A [time duration](JSON_INFO.md#time-duration), such as `"5 turns"`. The node's conditions and score are reused for that long, separately for each NPC, instead of being worked out on every walk. Default `"0 turns"`: every walk works them out.
| `copy-from`, `extend`, `delete` | See [Editing the tree](#editing-the-tree).

### How a node is walked

A node answers with one of three statuses:

- **running**: it can make progress. A leaf that is running is a candidate goal.
- **failure**: it can't make progress (no water within reach, no target).
- **success**: there is nothing to do here (not thirsty).

A node first tests its conditions in order. The first one that is not running gives the node's
status, and the node is done. When all are running (or there are none), a leaf is running and
offers its goal; a node with children walks them with its strategy:

| Strategy | Walk
|---|---
| `fallback` | The children in order. The first that is running or succeeds gives the answer; when all fail, the node fails. Use it for alternatives in priority order: `npc_decision` tries combat, then investigating, then the priorities.
| `sequential` | The children in order. The first that is running or fails gives the answer; when all succeed, the node succeeds.
| `sequential_until_done` | The children in order. The first that is running gives the answer; otherwise the node succeeds.
| `utility` | Every child. Of those that are running, the one with the highest score gives the answer; on a tie, the earlier child. When none is running, the node succeeds if a child succeeded, and fails otherwise.

A `utility` node compares its children's scores. A child's score is its own `score` when it has
one, otherwise the score of the leaf it chose, otherwise 0. So `npc_needs` hands up the score of
the need that won inside it, while `npc_thirst` replaces the score of its leaves with
`npc_thirst_urgency`.

## Conditions

`conditions` is a list of tests, run in order:

```jsonc
"conditions": [
  { "predicate": "npc_on_shift" },
  { "predicate": "npc_displaced_from_post" },
  { "predicate": "condition", "condition": { "u_has_trait": "DEVOUT" }, "invert_result": true }
]
```

| Field | Description
|---|---
| `predicate` | `"condition"` for a dialogue condition, or the name of a built-in predicate (below).
| `condition` | (_mandatory for `"condition"`_) A [dialogue condition](NPCs.md#dialogue-conditions), with the NPC as `u`.
| `argument` | (_optional_) A string for the few built-in predicates that read one. Giving one to any other predicate is a load error, so an argument is never silently ignored.
| `invert_result` | (_optional_) `true` swaps running and failure. Success stays success.

### Dialogue conditions

`"predicate": "condition"` runs any dialogue condition, math included. It is running when the
condition is true and fails when it is false, so a `fallback` parent moves on to its next child.

- The NPC is `u`. The beta talker is empty: `npc_` conditions are false and `n_` math reads 0.
- A condition can limit a branch to some NPCs: by class (`u_has_class`), trait
  (`u_has_trait`), faction (`u_is_in_faction`), or a variable that an EOC sets.
- Monster goal nodes take them too, with the monster as `u`.

```jsonc
{
  "type": "behavior",
  "id": "mymod_pray_at_dawn",
  "conditions": [
    {
      "predicate": "condition",
      "condition": {
        "and": [
          { "u_is_in_faction": "mymod_monks" },
          { "u_has_trait": "DEVOUT" },
          { "math": [ "time_since('sunrise') < time('1 h')" ] }
        ]
      }
    }
  ],
  "goal": "mymod_pray"
}
```

Conditions run every time an NPC walks the node, which is every turn for the nodes on its way.
Put cheap tests first, since the first test that fails ends the node, and use
`evaluate_every` for expensive ones.

### Built-in predicates

They read the NPC through C++. None of the NPC predicates reads an `argument`. An NPC
predicate in a monster's tree, or a monster predicate in an NPC's, fails (and an NPC score
gives 0), with an error; a tree that uses both kinds is reported at load.

| Predicate | Running when | Otherwise
|---|---|---
| `npc_in_danger` | the NPC's AI rates the danger around it above 0 | failure
| `npc_should_flee` | it is running away (the `npc_run_away` effect or a temporary flee attitude) | failure
| `npc_has_target` | it has a target | failure
| `npc_has_sound_alerts` | it heard a sound worth investigating; never for companions walking with the player, immobile NPCs or `IGNORE_SOUND` | failure
| `npc_needs_temperature_regulation` | a body part is very cold | success
| `npc_needs_water_badly` | thirst is above 520 | success (also for NPCs that don't need food)
| `npc_needs_food_badly` | it is starving, or short of calories | success (also for NPCs that don't need food)
| `npc_needs_sleep_badly` | sleepiness is at least tired | success
| `npc_has_water`, `npc_has_food` | it carries something it may drink or eat for the need, and may stop to consume it | failure
| `npc_can_obtain_water`, `npc_can_obtain_food` | there is a source it may go to | failure
| `npc_can_regulate_temperature` | it can put on warmer clothes, reach a warmth source, or wait indoors (for up to 60 turns without getting warmer) | failure
| `npc_can_wear_warmer_clothes` | it carries warm clothes it can put on | failure
| `npc_can_make_fire` | it has a spot and the means to make a fire | failure
| `npc_can_take_shelter` | there is shelter nearby | failure
| `npc_can_sleep` | it is tired, can sleep, and has somewhere it may sleep | failure
| `npc_displaced_from_post` | it has a guard post and is not on it | failure
| `npc_on_shift` | it has a guard post and its class's work hours cover now | failure
| `npc_is_following` | it follows the player and is outside its follow radius or on another level | success when close; failure when not following, or while the player's vehicle moves
| `npc_should_embark` | it follows the player, who is in a parked vehicle | failure
| `npc_has_goto_order` | it has a walk order (the "Move there!" order, or `u_walk_to`) and hasn't arrived | success when arrived
| `npc_has_camp_job` | a camp resident in its camp has a job, last searched for over 10 minutes ago | failure
| `npc_is_away_from_camp` | a camp resident is outside its camp | failure
| `npc_is_camp_idle` | a camp resident in its camp is not busy with an activity | failure

Monster goal trees use these:

| Predicate | `argument` | Running when
|---|---|---
| `monster_not_hallucination` | none | the monster is real
| `monster_items_available` | none | it stands on items it can absorb
| `monster_split_possible` | none | it has `SPLIT` and enough hit points to split
| `monster_adjacent_plants` | none | a plant is next to it
| `monster_special_available` | a special attack's name, required | `"NAME"`: that special attack is ready. `"!NAME"`: it is ready, or the monster doesn't have it.

## Scores

A `utility` node runs the child with the highest score. Scores are about 0 to 1, where 1 is
urgent. There are three ways to write one:

```jsonc
"score": "npc_thirst_urgency"                                  // a built-in score
"score": { "named": "npc_thirst_urgency", "mult": 0.5 }        // a built-in score, scaled
"score": { "math": [ "u_devotion / 100" ] }                    // math, with the NPC as u
```

| Form | Fields
|---|---
| a string | The name of a built-in score. `score_argument` would be its argument, but no built-in score reads one, so giving one is a load error.
| `named` | The name of a built-in score. `mult` (_optional_, default 1) multiplies it; `argument` as `score_argument`.
| `math` | A [math expression](NPCs.md#math), with the NPC as `u` and an empty beta (`n_` math reads 0). The math function `oracle('name')` gives a built-in score, so a node can combine one with anything else: `"math": [ "max( oracle('npc_thirst_urgency'), u_devotion / 100 )" ]`. A name written in quotes is checked at load.

A score is only worked out when its node is running. A node without a score scores 0, unless
it has children (see [How a node is walked](#how-a-node-is-walked)).

### Built-in scores

| Score | Value
|---|---
| `npc_thirst_urgency` | thirst / 1200 (1 is dying of thirst); 0 for NPCs that don't need food
| `npc_hunger_urgency` | 1 - stored calories / healthy calories; 0 for NPCs that don't need food
| `npc_temperature_urgency` | how far the coldest body part is from 37 C toward freezing (28 C)
| `npc_sleepiness_urgency` | sleepiness / 1000 (1 is passing out)
| `npc_duty_urgency` | 0.45 on shift at the post; 0.45 to 0.5 on shift away from it, growing with distance; 0 off shift or without a post
| `npc_following_urgency` | 0.3 to 0.6, growing with distance outside the follow radius; 0.6 on another level; 0 within the radius
| `npc_embark_urgency` | 0.6 while it should board the player's vehicle; 0.15 once on a belted seat or bed
| `npc_goto_order_urgency` | 0.65 until it arrives
| `npc_camp_work_urgency` | 0.4 for a camp resident with a job, in its camp
| `npc_return_to_camp_urgency` | 0.5 for a camp resident outside its camp
| `npc_free_time_urgency` | 0.35 for an idle camp resident in its camp

### `evaluate_every`

Conditions and math run on every walk. When they are expensive (searching around the NPC, many
math functions), `evaluate_every` reuses a node's results for a while:

```jsonc
{
  "type": "behavior",
  "id": "mymod_scavenge",
  "evaluate_every": "10 turns",
  "conditions": [ { "predicate": "condition", "condition": { "math": [ "u_monsters_nearby('mon_zombie', 'radius': 30) == 0" ] } } ],
  "score": { "math": [ "0.3 + u_greed / 200" ] },
  "goal": "mymod_scavenge"
}
```

- What is reused: the node's conditions and its own score. The children are still walked every
  time.
- Each NPC keeps its own results, from the walk that worked them out until the given time has
  passed.
- The cost: the NPC acts on old results for up to that long. A node that may start or stop a
  fight wants a short interval, or none.
- Only NPCs (and the avatar) keep results; monster goal nodes are worked out every time.

## Editing the tree

There is one shared tree. Mods add, change and remove its nodes with `copy-from`, `extend` and
`delete` (see [JSON inheritance](JSON_INHERITANCE.md)); every vanilla node can be changed this
way, combat included. To change a node in place, a mod defines it again with its own id and
`"copy-from"` it (a second definition in the same mod is an error). To make a variant, copy it
to a new id.

```jsonc
// add a branch to the priorities
{
  "type": "behavior",
  "id": "npc_priorities",
  "copy-from": "npc_priorities",
  "extend": { "children": [ "mymod_pray_at_dawn" ] }
}
```

When a field is given, it replaces the inherited one: `children`, `conditions`, `strategy`,
`goal`, `score` and `evaluate_every`.

### Children

- `"extend": { "children": [ ... ] }` adds children. An id is added at the end; an object places
  it next to another child, which matters in `fallback` and `sequential` nodes:

  ```jsonc
  "extend": { "children": [
    { "id": "mymod_take_cover", "before": "npc_fight" },
    { "id": "mymod_call_for_help", "after": "npc_flee" },
    "mymod_last_resort"
  ] }
  ```

  The anchor (`before` or `after`) is a child of the inherited list, or one inserted earlier in
  the same `extend`.
- `"delete": { "children": [ ... ] }` removes children by id.
- Deletions apply to the inherited list before the inserts. An anchor names a child in the list,
  or else one that was deleted. So a child can be replaced in place, or moved:

  ```jsonc
  // replace combat with a mod's own
  {
    "type": "behavior",
    "id": "npc_decision",
    "copy-from": "npc_decision",
    "extend": { "children": [ { "id": "mymod_combat", "before": "npc_combat" } ] },
    "delete": { "children": [ "npc_combat" ] }
  }
  // NPCs that never fight
  {
    "type": "behavior",
    "id": "npc_decision",
    "copy-from": "npc_decision",
    "delete": { "children": [ "npc_combat" ] }
  }
  // move a child: delete it, and insert it again elsewhere
  "extend": { "children": [ { "id": "npc_investigate", "before": "npc_combat" } ] },
  "delete": { "children": [ "npc_investigate" ] }
  ```

### Conditions and scores

- `"extend": { "conditions": [ ... ] }` appends conditions.
- `"delete": { "conditions": [ ... ] }` removes every condition that uses a predicate, given by
  name (`"npc_has_target"`, or `"condition"` for all dialogue conditions) or as
  `{ "predicate": ..., "argument": ... }` to match the argument too.
- `score` replaces the score; scale a built-in one with `mult`, or write math around it.

```jsonc
// followers only follow when they aren't devout
{
  "type": "behavior",
  "id": "npc_follow",
  "copy-from": "npc_follow",
  "extend": { "conditions": [ { "predicate": "condition", "condition": { "not": { "u_has_trait": "DEVOUT" } } } ] }
}
// fight without needing a target, half as eager to drink
{ "type": "behavior", "id": "npc_fight", "copy-from": "npc_fight", "delete": { "conditions": [ "npc_has_target" ] } },
{ "type": "behavior", "id": "npc_thirst", "copy-from": "npc_thirst", "score": { "named": "npc_thirst_urgency", "mult": 0.5 } }
```

### Checks at load

These stop loading with an error that points at the JSON:

- an unknown predicate or built-in score, an `argument` for one that reads none, or a missing
  one where it is needed
- an unknown name written in quotes in `oracle('...')`
- both `before` and `after` in one insert
- `extend` or `delete` without `copy-from`

Edits that two mods may well both make are reported, with where they are in the JSON, and
loading goes on:

- deleting a child or condition the node doesn't have (skipped)
- inserting a child it already has (skipped)
- `before` or `after` naming something that is not a child (the child is added at the end)
- a member of `extend` or `delete` that nothing reads, such as a misspelt `"childs"`

These are reported once everything is loaded:

- a child that is not a behavior (it is left out)
- a child listed twice
- a cycle, with its path (the link that closes it is left out, so a walk always ends)
- a node whose children were all left out: it fails, so its parent moves on
- a tree whose root reaches no leaf with a goal (it can only ever give `"idle"`); a root is a
  node no other node lists as a child
- a tree that uses both NPC and monster predicates or scores
- a node with neither children nor a goal, with both, or with children but no strategy
