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
- Monster goal nodes take them too, with the monster as `u` and its attack target (the creature
  at its destination it can see, if it isn't friendly to it) as `npc`; without one, `npc` is
  empty as for NPCs.

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
          { "math": [ "time_since('midnight') < time('7 h')" ] }
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
| `npc_needs_temperature_regulation` | a body part is at or below its class's `cold_body_temperature` ([Tuning](#tuning)), 31 C by default | success
| `npc_needs_water_badly` | thirst is above its class's `drink_water` `act` level ([Tuning](#tuning)), 520 by default | success (also for NPCs that don't need food)
| `npc_needs_food_badly` | it is starving, or short of calories (by default 20% of a healthy store) | success (also for NPCs that don't need food)
| `npc_needs_sleep_badly` | sleepiness is at least tired (191, by default) | success
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

### Monster goals

A monster's goals are the nodes its type's `"behavior"` lists (a node id or an array of them, see
[MONSTERS.md](MONSTERS.md#behavior)), then the nodes whose ids are the names of its special
attacks (`SPLIT`, `ABSORB_ITEMS`, `EAT_CROP` in vanilla), tried in that order until one is running
(`sequential_until_done`). The goal a monster's walk ends at, each turn before its special
attacks, is:

- `"eoc:ID"`: runs that effect_on_condition, with the monster as `u` and its attack target (if
  any) as `npc`. A load error names an unknown EOC in the tree of a monster with a `behavior`.
- `"special:NAME"` or `"NAME"`: uses the monster's special attack `NAME`, if it has it. The goal
  doesn't wait for the attack's cooldown (check it with `monster_special_available`), which then
  restarts. Any other goal does nothing.

Conditions and math scores read the monster as `u` and its target as `npc`; math scores rank a
`utility` node's children, as for NPCs. The goal can't yet make a monster flee, wander or follow.

```jsonc
{ "type": "behavior", "id": "mymod_hive_call", "conditions": [ { "predicate": "condition", "condition": { "math": [ "u_hp('torso') < 20" ] } } ], "goal": "eoc:EOC_MYMOD_CALL_HIVE" }
// and in the monster: "behavior": "mymod_hive_call"
```

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
| `npc_embark_urgency` | 0.6 while it should board the player's vehicle; 0.15 once on a belted seat or bed (its class's `embark` and `embark_seated`)
| `npc_goto_order_urgency` | 0.65 until it arrives
| `npc_camp_work_urgency` | 0.4 for a camp resident with a job, in its camp
| `npc_return_to_camp_urgency` | 0.5 for a camp resident outside its camp
| `npc_free_time_urgency` | 0.35 for an idle camp resident in its camp

These are the defaults. An NPC class can change the levels the needs act at and the obligations' scores, for its NPCs alone; see [Tuning](#tuning).

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

To move a built-in goal, `delete` it from its category and `extend` another with it. Every built-in goal needs a category: one that no category lists is a load error (a mod that redefines `needs` without `copy-from` and without its `goals` would leave the needs out). The C++ rules of a built-in goal follow the goal, not its category: `fight` and `flee` last only while the tree picks them, two needs weigh their urgencies with margins, `start_fire` doesn't displace a need that has an executor, wherever they are. Moving one changes only its rank. The C++ code also acts on the built-in categories by id (`combat`, `needs` and so on), so they must exist; reorder them rather than deleting them.

### Interruption

Each turn the NPC compares the goal the tree picks (the challenger) with the goal it is committed to:

- **Different categories:** the challenger takes over when its category comes first. A goal never takes over from a goal of an earlier category by score; only reordering the categories can do that.
- **Same category, a JSON goal on either side:** the challenger takes over when its score is above the committed goal's score times 1 + the category's `hysteresis`. With the default 10%, a committed goal scoring 0.5 holds against challengers up to 0.55.
- **Same category, both built-in:** a need takes over from another need when it is more urgent by a margin that grows with the cost of breaking off the first one; other built-in goals stay until their own end.

The committed goal's score is its own node's score this turn, so give a JSON goal's leaf its score. A JSON goal whose node's conditions no longer hold scores 0, so any goal of its category with a score takes over.

A JSON goal with an `until` or a `timeout` runs until one of them ends it, or something takes over: its node's conditions only decide when it's taken up. A goal with neither lasts while its node applies: once its node's conditions no longer hold and the tree picks something else, it ends as `interrupted`.

The obligations that hold back a built-in need (walking with the player, a guard post, an order to go somewhere) aren't judged for JSON goals: their categories are. A walk a JSON goal sends the NPC on is an order like any [`u_walk_to`](EFFECT_ON_CONDITION.md#u_walk_to-npc_walk_to) walk, though: while the NPC walks it, needs that would take it elsewhere wait until they're urgent.

Reactions to danger are decided before the tree: fighting or fleeing a threat (and escaping an explosion) count as `combat`, investigating a sound as `investigate`, using an inhaler during an asthma attack as `needs`. A JSON goal of a later category ends as interrupted when one comes; a JSON goal of the same category or an earlier one keeps the NPC on it, and the reaction waits, with none of its side effects (combat CBMs and items stay off, a sound's spot isn't remembered). A JSON goal in a category before `combat` is taken up even in a fight, when the tree picks it and it would win against what the NPC is committed to.

Activities that skip the decision tree (eating, harvesting, foraging, lighting a fire) pause a JSON goal: neither `on_turn` nor `until` nor `timeout` is looked at while they last. The timeout counts from the goal's start all the same, so a goal past its time ends as soon as the activity does.

## JSON goals

A behavior leaf can carry its goal as an object instead of a string, and the category it belongs to:

```jsonc
{
  "type": "behavior",
  "id": "npc_pray_at_dawn",
  "category": "worship",
  "conditions": [ { "predicate": "condition", "condition": { "u_has_trait": "DEVOUT" } } ],
  "score": { "math": [ "u_devotion / 100" ] },
  "goal": {
    "id": "pray_at_dawn",
    "on_start": [ "EOC_FIND_SHRINE_AND_WALK" ],
    "on_turn": [ "EOC_PRAY_IF_AT_SHRINE" ],
    "until": { "math": [ "u_prayed_today == 1" ] },
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

**The goal's walk.** A walk keeps the context values of the EoC that sent it, so a walk from the goal's EoCs carries its `goal_id`; so does one sent from that walk's own `true_eocs`. Such a walk is the goal at work: the tree sees it as a walk the NPC was ordered on (`goto_ordered_position`), but that doesn't take over from the goal, whatever the goal's category. When the goal ends, its walk is dropped without its EoCs; a goal that ends during its walk (`until`, `timeout`) isn't taken up again in that turn, and the next turn only when the tree picks it. An activity the goal started goes on unless an end hook stops it (`u_cancel_activity`).

**Saves.** The goal, when it started and its turn count are saved with the NPC. After a load the NPC is on the goal again without `on_start` running; its walk comes back with the save too.

**Overriding a goal's node.** A mod that redefines the node with `copy-from` and gives neither `goal` nor `category` keeps its goal and changes only what it gives, such as its conditions or score. Giving `goal` again replaces the goal.

**Load errors.** A goal with a built-in goal's id, a goal object without `category`, a goal defined by two nodes, an unknown category, one goal given two categories (by two nodes, or by a node and a category's `goals`), a `timeout` of 0, an EoC that doesn't exist.

### Worked example: fetching a crowbar

An NPC with nothing better to do goes for a crowbar lying within 10 tiles, picks it up and comes back to where it stood. It uses the [errand effects](EFFECT_ON_CONDITION.md#u_walk_to-npc_walk_to): `u_map_run_item_eocs` finds the crowbar, `u_pickup_item` with `"walk": true` walks to it and takes it, and `u_walk_to` brings the NPC back. If there's no crowbar to get, or it takes too long, the NPC gives up for a day.

```jsonc
[
  {
    "type": "behavior",
    "id": "npc_fetch_crowbar",
    "category": "free_time",
    "conditions": [
      {
        "predicate": "condition",
        "condition": {
          "and": [
            { "not": { "u_has_item": "crowbar" } },
            {
              "or": [
                { "math": [ "u_fetch_crowbar_failed_at == 0" ] },
                { "math": [ "time('now') - u_fetch_crowbar_failed_at > time('1 d')" ] }
              ]
            }
          ]
        }
      }
    ],
    "score": { "math": [ "0.3" ] },
    "goal": {
      "id": "fetch_crowbar",
      "on_start": [ "EOC_FETCH_CROWBAR_START" ],
      "until": {
        "or": [
          { "and": [ { "u_has_item": "crowbar" }, { "math": [ "u_fetch_crowbar_back == 1" ] } ] },
          { "math": [ "u_fetch_crowbar_lost == 1" ] }
        ]
      },
      "timeout": "30 minutes",
      "on_timeout": [ "EOC_FETCH_CROWBAR_LOST" ],
      "on_end": [ "EOC_FETCH_CROWBAR_END" ]
    }
  },
  {
    "type": "effect_on_condition",
    "id": "EOC_FETCH_CROWBAR_START",
    "effect": [
      { "math": [ "u_fetch_crowbar_back = 0" ] },
      { "math": [ "u_fetch_crowbar_lost = 0" ] },
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
                "false_eocs": [ "EOC_FETCH_CROWBAR_LOST" ]
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
    "id": "EOC_FETCH_CROWBAR_LOST",
    "effect": [ { "math": [ "u_fetch_crowbar_lost = 1" ] }, { "math": [ "u_fetch_crowbar_failed_at = time('now')" ] } ]
  },
  {
    "type": "effect_on_condition",
    "id": "EOC_FETCH_CROWBAR_END",
    "effect": [
      {
        "if": { "u_has_item": "crowbar" },
        "then": { "u_message": "<u_name> is back with a crowbar.", "type": "good" },
        "else": { "u_message": "<u_name> gives up on the crowbar (<context_val:reason>)." }
      }
    ]
  }
]
```

The node goes into the tree with `copy-from` and `extend` (see [Editing the tree](#editing-the-tree)):

```jsonc
{
  "type": "behavior",
  "id": "npc_priorities",
  "copy-from": "npc_priorities",
  "extend": { "children": [ "npc_fetch_crowbar" ] }
}
```

How it runs:

1. In `npc_priorities` the node competes by score with the other priorities, here at 0.3. When the tree picks it and nothing of an earlier category is committed, the NPC takes up `fetch_crowbar` and `on_start` runs: it remembers where the NPC stands and sends it for the nearest crowbar.
2. Each turn the NPC walks its pickup walk. The walk carries `goal_id`, so it is the goal's own: the tree's `goto_ordered_position` doesn't take over.
3. Next to the crowbar, the pickup takes it and its `true_eocs` send the NPC home with `u_walk_to`, again as the goal's walk.
4. Home, the walk's `true_eocs` set `u_fetch_crowbar_back`. Next turn `until` holds, the goal ends as `done` and `on_end` says so. Carrying a crowbar, the NPC no longer meets the node's conditions, so the tree doesn't pick the goal again.
5. No crowbar within reach, one it can't get to, or 30 minutes gone: `EOC_FETCH_CROWBAR_LOST` sets `u_fetch_crowbar_failed_at`, the goal ends, and the node's conditions keep the NPC from trying again for a day.
6. Thirst on the way (`needs` comes before `free_time`) or a zombie (`combat`) interrupts the errand: the goal's walk is dropped and `on_end` runs with `reason` `interrupted`.

The example is in `data/mods/TEST_DATA/npc_goals_fetch_crowbar.json` (without the `npc_priorities` extension), and `tests/npc_json_goal_test.cpp` runs it.

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

`npc_goal_changed` fires every time the goal an NPC is committed to changes, built-in goals included, with these fields; its EoCs run with the NPC as `u`. It isn't kept for statistics, scores or achievements, which would grow the save with every NPC's changes:

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

## Reading and changing an NPC's AI

Besides its goal, EOCs can read and change these of an NPC; each is described where it's linked. They give false or 0 for a talker that isn't an NPC, or a missing one, and change nothing then.

| Name | Kind | Gives |
| --- | --- | --- |
| [`u_attitude`, `npc_attitude`](EFFECT_ON_CONDITION.md#u_attitude-npc_attitude) | condition | its attitude toward the avatar (`NPCATT_KILL`...) |
| [`u_mission`, `npc_mission`](EFFECT_ON_CONDITION.md#u_mission-npc_mission) | condition | its standing mission (`GUARD`...) |
| [`u_on_shift`, `npc_on_shift`](EFFECT_ON_CONDITION.md#u_on_shift-npc_on_shift) | condition | it has a guard post and its work hours cover now, as the predicate `npc_on_shift` |
| [`u_at_post`, `npc_at_post`](EFFECT_ON_CONDITION.md#u_at_post-npc_at_post) | condition | it stands on its guard post |
| [`npc_danger()`](NPCs.md#math) | math | the danger its AI rates around it, as `npc_in_danger` reads it |
| [`npc_panic()`](NPCs.md#math) | math | how panicked it is in a fight |
| [`npc_need_urgency('drink_water')`](NPCs.md#math) | math | a need's urgency, as the built-in scores give it, or a [custom need](#custom-needs)'s |
| [`npc_personality('aggression')`](NPCs.md#math) | math, assignable | a personality trait, -10 to 10 |
| [`u_set_attitude`, `npc_set_attitude`](EFFECT_ON_CONDITION.md#u_set_attitude-npc_set_attitude) | effect | its attitude, through the dialogue effect for it |
| [`u_set_mission`, `npc_set_mission`](EFFECT_ON_CONDITION.md#u_set_mission-npc_set_mission) | effect | its mission, through the dialogue effect for it |
| [`npc_attitude_changed`, `npc_becomes_hostile`](EFFECT_ON_CONDITION.md#npc-attitude-events) | events | its attitude changed, and why |
| `npc_recruited`, `npc_dismissed`, `npc_mutinies` | [events](EFFECT_ON_CONDITION.md#event-eoc-types) | it joined or left the avatar's followers, or mutinied |

A faction's relations to other factions are read with [`faction_has_relation`](EFFECT_ON_CONDITION.md#faction_has_relation) and set with [`set_faction_relation`](EFFECT_ON_CONDITION.md#set_faction_relation).

## Tuning

An NPC class's `ai_tuning` ([NPCs.md](NPCs.md#ai-tuning)) changes the numbers the built-in needs, the weighing of needs against obligations and sleep run on, for that class's NPCs. Needs a mod adds are [custom needs](#custom-needs). A class that gives none, every vanilla one, has the defaults below: the game's behavior without it. With `copy-from` a class keeps what the class it copies gives and changes what it gives itself. To weigh a score for one branch of the tree instead, edit that node's `score` ([Scores](#scores)).

### Needs

Each built-in need has an urgency from 0 to 1, its built-in score: thirst / 1200, the share of a healthy calorie store missing, sleepiness / 1000, how cold the coldest body part is. `needs` gives three levels on that scale for each:

| Level | What waits for it | `drink_water` | `eat_food` | `go_to_sleep` | `regulate_temperature` |
| --- | --- | --- | --- | --- | --- |
| `act` | the need's predicate: `npc_needs_water_badly` (thirst above it), `npc_needs_food_badly` (calories short by more than it), `npc_needs_sleep_badly` and `npc_can_sleep` (sleepiness at it); also what a discouraged option's level relaxes to while the avatar stays put | 0.433 (thirst 520) | 0.2 | 0.191 (tired) | 0.75 |
| `insist` | an option the NPC's obligations discourage, such as a trip out of the follow radius, or sleep while the avatar is on the move | 0.667 (thirst 800) | 0.4 | 0.383 (dead tired) | 0.75 |
| `emergency` | an option they allow only in an emergency, such as leaving a guard post or a walk it was ordered on | 0.75 | 0.6 | 0.575 (exhausted) | 0.75 |

Each level is 0 to 1, and `act` <= `insist` <= `emergency`. The temperature predicate doesn't read `regulate_temperature`'s levels: it fires on a body part at or below `cold_body_temperature`, and those levels only decide when the NPC may leave an obligation to get warm.

| Member | Default | What it does |
| --- | --- | --- |
| `cold_body_temperature` | 31 C | `npc_needs_temperature_regulation` fires once a body part's temperature is this or lower |
| `need_critical_urgency` | 0.75 | a need more urgent than this takes over from another need the NPC is on, whether or not it beats it by the margin it otherwise needs (0.15 to 0.45, by what breaking off the first one costs) |
| `starvation_hunger` | 300 | an NPC burning its stored calories counts as starving, and looks for food even when not short of calories, from this hunger |
| `camp_meal_kcal` | 500 | the least a camp's larder serves an NPC that is hungry but not short of calories |

### Obligations

`urgency` sets the built-in scores of the obligations, 0 to 1, against the needs' urgencies:

| Member | Default | Score |
| --- | --- | --- |
| `duty` | 0.45 | `npc_duty_urgency` of a guard on shift at its post, and the least while it's away from it |
| `duty_max` | 0.5 | `npc_duty_urgency` away from its post at the most (0.05 a tile); at least `duty` |
| `follow_max` | 0.6 | `npc_following_urgency` at the most, and on another level: from 0.3 just outside the follow radius, 0.015 more a tile |
| `goto` | 0.65 | `npc_goto_order_urgency` |
| `camp_work` | 0.4 | `npc_camp_work_urgency` |
| `return_to_camp` | 0.5 | `npc_return_to_camp_urgency` |
| `free_time` | 0.35 | `npc_free_time_urgency` |
| `embark` | 0.6 | `npc_embark_urgency` while it should board the avatar's parked vehicle |
| `embark_seated` | 0.15 | `npc_embark_urgency` once it is on a belted seat or bed in it: low enough that a tired passenger sleeps |

### Sleep

| `sleep` member | Default | What it does |
| --- | --- | --- |
| `settle_interval` | 3 minutes | an NPC lying down tries to fall asleep this often, each try easier than the last (up to the tenth) |
| `settle_timeout` | 30 minutes | then it gives the spot up and looks for another |
| `stall_wakes`, `stall_window` | 3, 30 minutes | woken this many times within this long, its sleep stalls |
| `stall_duration` | 30 minutes | a stalled sleep gives way to any other work this long |
| `restless_window` | 30 minutes | noise wakes are counted while each comes within this long of the last; from the second, sleep waits a minute, doubling with each further wake up to 16 minutes, while the noise goes on |

A woken NPC lies back down while it is still half as tired as `go_to_sleep`'s `act`.

## Custom needs

A mod adds a need of its own (blood, mana, a battery to charge) with the type `npc_need`. It is weighed with the built-in needs and leaves them as they are: each one becomes a leaf of the decision tree, by default the last child of `npc_needs`, whose score is the need's urgency and whose goal is a [JSON goal](#json-goals) of the `needs` category with the need's hooks.

```jsonc
{
  "type": "npc_need",
  "id": "mymod_blood",                                     // the need's id, and its goal's
  "urgency": { "math": [ "u_mymod_blood_hunger / 100" ] }, // with the NPC as u
  "act": 0.4,
  "condition": { "u_has_trait": "MYMOD_VAMPIRE" },
  "on_start": [ "EOC_MYMOD_FIND_BLOOD" ],
  "on_turn": [ "EOC_MYMOD_DRINK_IF_NEAR" ],
  "timeout": "2 hours",
  "on_end": [ "EOC_MYMOD_BLOOD_DONE" ]
}
```

| Field | Optionality | Value | Info |
| --- | --- | --- | --- |
| `"id"` | **mandatory** | string | the need's id, which is also its goal's: it can't be a built-in goal's or another goal's |
| `"urgency"` | **mandatory** | number, [variable object](NPCs.md#variable-object) or math | how urgent the need is, with the NPC as `u` and an empty `npc`; clamped to 0 to 1. It is the leaf's score, and what [`npc_need_urgency('id')`](NPCs.md#math) gives |
| `"act"` | optional | number, 0 to 1 | default 0.5: the NPC feels the need, and may act on it, from this urgency |
| `"condition"` | optional | [condition](EFFECT_ON_CONDITION.md#possible-conditions) | only NPCs it holds for, as `u`, feel the need, by class (`u_has_class`), trait or anything else. Without one, every NPC does |
| `"category"` | optional | string | default `needs`: the goal's [category](#categories) |
| `"parent"` | optional | string | default `npc_needs`: the behavior node the leaf is added under, after its own children. It needs children and a strategy |
| `"on_start"`, `"on_turn"`, `"on_end"`, `"on_interrupt"`, `"on_timeout"`, `"timeout"` | optional | | the goal's hooks and timeout, as a [JSON goal](#json-goals)'s |
| `"until"` | optional | [condition](EFFECT_ON_CONDITION.md#possible-conditions) | the goal ends as `done` once this holds. Without one, it ends as `done` once the need isn't felt: its urgency is below `act`, or its condition stopped holding |

The leaf is `npc_need_` and the need's id. It runs while the need is felt and succeeds (nothing to do) otherwise, as the built-in needs' predicates do; the debug view shows its condition as `npc_need( id )`. A utility parent weighs it against the built-in needs by score, so the more urgent need wins, and a need already pursued holds against another one of its category by the category's hysteresis ([Interruption](#interruption)). The obligations that hold back a built-in need aren't judged for it: its category is.

`copy-from` works as for other types; what the copy doesn't give it keeps. Load errors: no `urgency`, an `act` outside 0 to 1, a `timeout` of 0. Reported once everything is loaded: an unknown category or EoC, and, with the need left out, a `parent` that isn't a behavior, a goal id that is taken, or a behavior already named `npc_need_` and the id.

The per-class `ai_tuning` doesn't name custom needs; put what differs between NPCs in the need's `urgency` math and `condition`.

## Debugging

The debug menu's **Console…** has an **NPC decision** section in its **Creatures** tab: select an NPC and open it. It's worked out again each turn, so step the game with the console's controls to watch it change. It shows:

- the tree, depth first: each node's conditions, true or false, whether the node is running, succeeded or failed, its score, and for a leaf its goal and category. The path to the goal the tree picks is highlighted;
- the categories, first to last, with their hysteresis;
- the goal the NPC is committed to, its category, score and turns, the goal the tree picks, and why the committed goal stays or what takes over: an earlier category, the scores against the hysteresis bonus, `until`, the timeout, its node no longer applying, its own walk. It's the decision `npc::move()` makes, worked out by the same code.

With the debug log filter for NPC needs on (`DF_NPC_NEEDS`), the log also says when an NPC starts a JSON goal and when and why one ends.

The view's data comes from `npc_goal_runtime::report()` (src/npc_json_goal.h), which changes nothing, so tests can check what it shows.
