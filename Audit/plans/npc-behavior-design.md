# Design: NPC behavior tree goals from JSON (NPC-J03)

**Status:** decided with the owner on 2026-11-02 and built on 2026-11-03, after the NPC errand and EOC UI batch in the [modding queue](modding-queue.md), whose effects (`u_walk_to`, item pickup, targeted activities, `u_examine`) are what most JSON goals use. The modder documentation is `doc/JSON/NPC_BEHAVIOR.md`. Where the build settled something this design left open, the doc is the reference: for example, a goal with neither `until` nor `timeout` lasts while its node applies, and `npc_goal_changed` is not kept in statistics.

## How NPCs decide today

- **The tree.** Every NPC walks one decision tree each turn, `npc_decision` in `data/json/npcs/npc_behavior.json` (29 nodes). Nodes use the strategies `fallback`, `sequential` and `utility`. In order:
  1. combat (flee or fight)
  2. investigating a sound
  3. a scored list of priorities:
     - needs: temperature, thirst, hunger, sleep
     - guard duty
     - following the player, on foot or in a vehicle
     - player orders
     - camp work and returning to camp
     - free time
- **Conditions and scores.** Node conditions (`"predicate": "npc_has_water"`) and scores (`"score": "npc_thirst_urgency"`) name fixed C++ functions in `character_oracle` and `behavior_oracle`. Their `argument` fields are parsed and then ignored.
- **Goal dispatch.** A leaf's `goal` is a string that `npc::move()` dispatches through a chain of `new_goal == "..."` comparisons (src/npcmove.cpp, around line 1940). An unknown goal falls through to the old `address_needs()` code.
- **Categories and interruption.** `bt_goal_to_category()` (src/npcmove.cpp, around line 289) maps goal names to a fixed category enum (src/npc_decision_category.h):

  `combat, investigate, needs, follow, order, duty, camp_work, camp_travel, free_time, idle, unmodeled`

  An unknown goal is `unmodeled`, the lowest. `should_interrupt()` compares categories by enum order, so a JSON goal can't interrupt anything and anything interrupts it.
- **Moddability.** There is no documentation of the `behavior` type, and npc_class and npc templates can't choose or change the tree.

The result: modders can rearrange the vanilla tree, but they can't add behavior.

## Decisions (2026-11-02)

1. **Ranking: join a category.** A mod goal says which category it belongs to. It competes by score with the other goals in that category, and the category order settles the rest; combat comes first in the default order. Mods may also define new categories, placed before or after a built-in one.
2. **While active: run EOCs until done.** A committed JSON goal runs:
   - EOCs when it starts
   - EOCs each turn
   - until a condition is met, a time limit passes, or a higher category interrupts it
   - then end, timeout or interrupt EOCs
3. **One shared tree, gated by conditions.** Mods add, change or remove branches of the shared tree. A branch can be limited to some NPCs by condition: class, trait, faction or a variable. There are no per-NPC trees.
4. **Every built-in goal is editable,** combat included. Built-in nodes are ordinary nodes that mods can delete, re-condition or re-score. The default tree keeps combat first.

## Design

### 1. Conditions as predicates

```jsonc
"conditions": [ { "predicate": "condition", "condition": { "and": [ { "u_has_trait": "DEVOUT" }, { "math": [ "time_since('sunrise') < time('1 h')" ] } ] } } ]
```

- **Talkers.** The NPC is `u`; there is no beta.
- **Built-in predicates.** They stay, and their `argument` is passed through as documented (today it is parsed and dropped).

### 2. Scores as math

```jsonc
"score": { "math": [ "u_val('devotion') / 100" ] }
```

- **Named scores.** They stay. A node can scale or replace a built-in score: `"score": { "named": "npc_thirst_urgency", "mult": 0.5 }` or a math expression that calls it as `oracle('npc_thirst_urgency')`.
- **Expensive conditions.** `"evaluate_every": "5 turns"` on a node caches its conditions and score for that long.

### 3. Categories

- **Defined in JSON.** Categories become a JSON type. The built-in list above is defined in `data/json/npcs/npc_decision_categories.json`, in today's order, so vanilla behavior is unchanged.
- **Mod categories.** A mod adds one with `"after": "needs"` or `"before": "duty"`.
- **Built-in goals.** Each gets its category in JSON instead of `bt_goal_to_category()`.
- **Interruption:**
  - A goal in a higher category interrupts a lower one.
  - Within a category, the higher score wins, with hysteresis: the committed goal's score gets a bonus, 10% by default and settable per category, so NPCs don't flip between two goals every turn.
  - A goal can't interrupt a higher category by score alone; only reordering categories can do that.

### 4. JSON goals

```jsonc
{ "type": "behavior", "id": "npc_pray_at_dawn", "category": "worship",
  "conditions": [ { "predicate": "condition", "condition": { "u_has_trait": "DEVOUT" } } ],
  "score": { "math": [ "1 - time_since('sunrise') / time('2 h')" ] },
  "goal": {
    "id": "pray_at_dawn",
    "on_start": [ "EOC_FIND_SHRINE_AND_WALK" ],        // e.g. u_location_variable + u_walk_to
    "on_turn": [ "EOC_PRAY_IF_AT_SHRINE" ],
    "until": { "u_has_var": "prayed_today" },
    "timeout": "2 hours",
    "on_end": [ ], "on_interrupt": [ "EOC_PRAYER_INTERRUPTED" ], "on_timeout": [ ]
  } }
```

- **Talkers and context.** EOCs run with the NPC as `u`. Context values: `goal_id`, `turns_active`, and `reason` (`"done"`, `"timeout"` or `"interrupted"`) for the end EOCs.
- **A goal-active turn:**
  1. `on_turn` runs.
  2. If the EOCs gave the NPC something to do (a walk, an activity, an attack, an examine), it does that through its normal move code.
  3. If they gave it nothing and spent no moves, the NPC waits out the turn. A goal never stalls the game.
- **Ending.** The goal ends when `until` is true, `timeout` passes, or a higher category takes over.
- **Saves.** The active goal, its start time and the turn count are saved with the NPC. On load the goal resumes; `on_start` doesn't run again.
- **Name clashes.** Built-in goals stay C++ (`drink_water`, `follow_player`...). A JSON goal with the same id as a built-in one is a load error, so a mod can't silently shadow one.

### 5. Editing the shared tree

- **Inheritance.** `behavior` moves onto `generic_factory`, so nodes support `copy-from`, `extend` and `delete`.
- **Positioned inserts.** For `fallback` and `sequential` nodes, where order matters, an insert can be placed: `"extend": { "children": [ { "id": "npc_pray_at_dawn", "before": "npc_free_time" } ] }`.
- **Built-in nodes** can be deleted, re-conditioned and re-scored like any other, combat included (decision 4).
- **Validation at load:** unknown children, cycles, unknown categories, and a tree whose root has no reachable leaf.

### 6. What EOCs can read (part of NPC-J05)

| Name | Kind | Gives |
|---|---|---|
| `u_npc_goal` | condition: `{ "u_npc_goal": "pray_at_dawn" }` | Whether this is the NPC's current goal |
| `npc_goal_category` | condition | Whether the current goal is in a given category |
| `npc_goal_turns()` | math | Turns since the current goal started |
| `npc_goal_changed` | event | Fires on every change, with the NPC, the old goal and the new goal |

### 7. Debugging

The debug menu gets an "NPC decision" view for a chosen NPC, refreshed each turn. Behavior is hard to debug without it. It shows:
- each node's conditions, true or false, and the reason
- each score
- the category order
- the committed goal and why it won

### 8. Performance

- **Cost per NPC.** Each NPC already walks the tree every turn. Condition and math nodes add the cost of their expressions, and `evaluate_every` lets expensive ones run less often.
- **Benchmarks.** NPC-heavy benchmarks run before and after, as in the performance rounds.

## Phases

| Phase | Content | Tests |
|---|---|---|
| 1 | Categories as JSON; built-in goals mapped in JSON; `generic_factory` for `behavior`. No behavior change. | `tests/npc_arbitration_test.cpp` and the NPC suites pass unchanged; the category order is identical. |
| 2 | Condition predicates, math scores, `argument` passed through, `evaluate_every`. | Each predicate and score form; caching. |
| 3 | JSON goals with start, turn and end hooks, `until`, timeout, interruption and hysteresis; save and load. | An errand goal end to end (walk, pick up, return) using the errand effects; interruption by combat and by needs; resume after load. |
| 4 | Positioned tree edits, deleting and replacing built-ins, load validation. | Insert before or after; delete combat in a test tree; cycle and unknown-child errors. |
| 5 | Reading goals from EOCs, the `npc_goal_changed` event, the debug view, docs (a new NPC_BEHAVIOR.md). | Conditions, math and event fields. |

## Dependencies and related findings

- **Needs the errand effects** from the modding queue: `u_walk_to` (with the NPC-B17 fix), `u_pickup_item` and inventory moves, targeted `u_assign_activity`, `u_examine`.
- **NPC-J03:** this design.
- **NPC-J05:** partly covered (goal state). Attitude, personality and faction-relation reads are left to that finding.
- **NPC-J04:** attitude changes fire no events. It fits next to `npc_goal_changed`.
- **NPC-J10:** need thresholds and urgencies are compile-time constants. Phase 2's math scores let a mod override urgencies per node; the constants themselves stay with that finding.
