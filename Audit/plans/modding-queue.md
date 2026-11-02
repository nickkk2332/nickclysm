# Modding queue: NPC errands and EOC UI

The owner asked for these on 2026-11-02, after the grappling port. They are queued behind the PR #3 review comments. Each item lists the planned JSON with an example, what it fixes, and what to decide before building. The syntax is a proposal; it gets settled when the item is built.

## Order

1. **PR #3 review comments.** Not part of this queue, but they come first.
2. **NPC errands:** `u_walk_to`, `u_pickup_item`, `u_wield_item` / `u_wear_item` / `u_drop_item`, targeted `u_assign_activity` and `u_examine` (sections A–E). Together they let a mod script an errand from start to end: walk to the shed, pick up the crowbar, wield it, pry the door, come back.
3. **EOC UI:** a text window and a checklist menu (sections F–G).
4. **NPC behavior tree EOC leaves (NPC-J03).** This needs a design discussion with the owner first; see section H.

Related findings that fit these batches (fold them in if they touch the same code):

| Finding | What it is |
|---|---|
| NPC-B17 | `npc::move_to` teleports when the next path step is not adjacent. `u_walk_to` needs this fixed. |
| EOC-J15 | `u_var_changed` does not fire on math changes. Widgets and EOCs watching a variable miss updates. |
| EOC-J06 | Mods can't define their own events. |
| EOC-J08, EOC-J13 | Monster, furniture and vehicle talkers silently ignore most effects. |

## A. `u_walk_to` / `npc_walk_to`

The NPC walks to a tile on the local map over the following turns, using its own AI movement (doors, vehicles, traps and pathfinding). This is not `npc::move()`: that function is the NPC's whole AI turn, and calling it from an EOC could re-enter itself and run outside the NPC's own turn slot.

```jsonc
{ "npc_walk_to": { "context_val": "shed_door" },   // a location variable
  "range": 1,                                       // optional: close enough (default 0, the tile itself)
  "give_up_after": 300,                             // optional: turns (default: when the path fails)
  "run": false,                                     // optional
  "true_eocs": [ "EOC_ARRIVED" ], "false_eocs": [ "EOC_GAVE_UP" ] }
```

- **Danger first.** A threat interrupts the walk the way it interrupts other goals. Whether the walk resumes afterwards is to be decided; the proposal is that it resumes unless `"interruptible": false`.
- **Avatar.** For the avatar it is auto-move.
- **Saves.** The goal is saved with the NPC, unlike the follower "go to" order (NPC-B14).
- **Condition.** `u_is_walking_to` reports whether a walk is in progress.

## B. `u_pickup_item` and inventory moves

| Effect | What it does |
|---|---|
| `u_pickup_item` | Takes a specific item from the map into the inventory, keeping its charges, damage, contents and variables. It works for NPCs and opens no menu. |
| `u_wield_item`, `u_wear_item`, `u_drop_item` | The same for an item the talker carries. |

```jsonc
{ "npc_map_run_item_eocs": "random", "search_data": [ { "id": "crowbar" } ], "max_radius": 10,
  "true_eocs": [ { "id": "EOC_TAKE_IT", "effect": [
    { "u_pickup_item": "npc", "walk": true,        // the item talker found above; walk over if needed
      "true_eocs": [ "EOC_GOT_IT" ], "false_eocs": [ "EOC_COULD_NOT" ] },
    { "u_wield_item": "npc" } ] } ] }
```

- **Item handles.** The item comes from an item talker (`u_map_run_item_eocs`, `u_run_inv_eocs`) or from `search_data` plus a location. EOC-J01 (no handle to an item in a variable) limits how far one EOC can pass the item to another; this batch may need a minimal item handle.
- **Costs and refusals.** Pickups cost moves as a normal pickup does. Refusals give a reason: too heavy, no room, owned by a faction (it counts as theft, with the usual reaction), out of reach.

## C. Targeted activities (the rest of ACT-J02)

`u_assign_activity` gains a target, so activities that need one work from an EOC.

```jsonc
{ "npc_assign_activity": "ACT_READ", "target_item": "npc" }          // read this book
{ "npc_assign_activity": "ACT_BUTCHER", "target_item": "npc", "butcher_type": "full" }
{ "u_assign_activity": "ACT_CRAFT", "recipe": "makeshift_crowbar", "target_loc": { "context_val": "bench" } }
```

- **Which activities first.** Each activity actor takes its targets differently, so they are ported one at a time. Proposed first set: read, butcher and disassemble, craft at a workbench, wear, unload and reload. The rest are refused with a load error that names the activity.

## D. `u_examine` / `npc_examine`

The character operates a tile as if it examined it.

```jsonc
{ "npc_examine": { "context_val": "shed_door" }, "true_eocs": [ "EOC_OPENED" ] }
```

- **The catch.** Most built-in examine actions are written for the avatar, with menus and prompts; `iexamine.cpp` has about 460 messages, menus and prompts.
- **Scope.** The proposal allows furniture and terrain whose examine action is an EOC (`"examine_action": { "type": "effect_on_condition" }`), plus a list of built-in actions known to be safe without a menu: doors and gates, switches and controls with no prompt, and sitting or lying down. Anything else is refused with a reason, rather than letting an NPC trigger a player menu.
- **Open question.** Should examine actions get an NPC-aware variant over time, starting with the most useful ones (crafting stations, vending, terminals)?

## E. Small conditions to go with A–D

| Condition | Checks |
|---|---|
| `u_has_item_in_hand` | The talker is holding a given item. |
| `u_can_reach` | A location is reachable by path. |
| `u_is_doing_activity` | Gives the activity id, not only whether any activity is running. |

## F. Text window: `u_show_text`

A scrollable window with a title, text (with dialogue tags and snippets), and optional buttons that each run EOCs. Use it for journals, notes, quest logs, tutorials and terminal-like screens.

```jsonc
{ "u_show_text": { "title": "Field journal", "text": { "global_val": "journal_text" },
  "buttons": [ { "name": "Close" },
               { "name": "Tear out the page", "effect": [ "EOC_TEAR_PAGE" ], "condition": { "u_has_item": "pencil" } } ] } }
```

- **Who sees it.** The avatar only. For an NPC it does nothing and runs `false_eocs`.
- **Tests.** In tests it answers through a test hook, as the technique menu does.

## G. Checklist menu: `run_eoc_checklist`

A multi-select menu: each option can be ticked, confirming runs the EOCs of the ticked options (or sets one variable per option), and cancelling runs nothing. `run_eoc_selector` already picks one option; this picks several.

```jsonc
{ "run_eoc_checklist": [ "EOC_PACK_FOOD", "EOC_PACK_WATER", "EOC_PACK_TOOLS" ],
  "names": [ "Food", "Water", "Tools" ], "checked": [ true, true, false ],
  "min": 1, "max": 2, "title": "What do you bring?" }
```

- **Parity.** It takes the same `hide_failing` and `variables` options as `run_eoc_selector`.

## H. NPC behavior tree EOC leaves (NPC-J03): needs a design discussion

The NPC behavior tree in `npc_behavior.json` can't express anything new today:
- goals are dispatched by name in C++;
- a JSON goal is always the lowest priority;
- predicates and scores are fixed C++ functions.

The audit's proposed fix:
- a `{ "predicate": "condition", ... }` predicate;
- a `{ "score": "math", ... }` score;
- leaves that declare a priority category and run EOCs while the NPC is committed to them, until a condition is met.

With A–D in place, a JSON goal could be "fetch water when thirsty and the jug is empty". It changes how NPCs decide what to do, so it gets its own design talk before any code.

## Not queued: designs waiting in the backlog

- [Grid resources, machine EOC hooks, tile variables and catch-up](grids-and-stateful-machines-design.md): custom resources besides electricity, EOC hooks and variables on appliance parts and furniture, and one catch-up rule for unloaded time. The owner filed it as a strong modding addition, to be built when the related findings (EOC-J13, MAP-J06, VINT-J06 and others) come up.
