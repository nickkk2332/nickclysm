# Proposed execution authoring format

**Draft schema, not currently loadable game JSON.** Examples show the intended interface and can guide implementation/tests. They belong in planning documentation until the loader/runtime exists. Names such as `execution_can_fall`, `start_execution`, and the actions below are proposed additions.

## Definition fields

| Field | Proposed meaning |
| --- | --- |
| `type`, `id`, `name` | `execution`, stable definition ID, translated author/debug name |
| `revision` | Integer compatibility revision for saved instances; incompatible edits invalidate safely |
| `required_roles` | Initial creature roles, normally `actor` and `target`; optional `weapon` binding declared separately |
| `control` | Requested role control: `free`, `held`, or `scripted` |
| `parameters` / `locals` | Declared typed input/default values and private saved instance variables |
| `max_ticks`, `max_step_entries` | Definition limits within engine ceilings |
| `interruptions` | Policies for supported damage/displacement/participant-loss causes |
| `on_start`, `on_tick` | Immediate activation EOCs, once at start and once per subsequent active game tick |
| `on_complete`, `on_interrupt`, `on_end` | Terminal callbacks; `on_end` runs for every terminal path |
| `start`, `steps` | Entry step ID and named step graph |

`copy-from`, extension/deletion, and source tracking should use existing generic-factory conventions. A definition overriding steps must pass graph/revision validation. Definition IDs should be namespaced by mods; instance IDs are allocated by the runtime.

A step has an optional immediate/stateful `action`, an optional bounded `repeat`, and exactly one of `next` or `end`. A pure branching step can omit the action. Multiple actions are expressed as adjacent zero-delay steps, keeping save/cursor semantics small. Lifecycle EOCs can execute normal synchronous effect lists.

Action role requirements are rechecked at execution. A role need not remain alive for a pure aftermath/end step. An expected death produced by the scene's own action is recorded as a result and permits role-free resolution; unexpected death invalidates actions still requiring that participant. Never substitute a new creature for the missing role.

## Transitions and repetition

| Form | Meaning |
| --- | --- |
| `"next": "beat"` | Enter that step immediately, subject to the transition budget |
| `"next": { "step": "beat", "after_ticks": [1, 3] }` | Choose an inclusive delay once, then enter at the saved due tick |
| `"next": { "if": condition, "then": transition, "else": transition }` | Evaluate an ordinary EOC condition once when selecting the transition |
| `"next": { "chance": 0.5, "then": transition, "else": transition }` | One probability roll for that occurrence |
| `"next": { "weighted": [ { "weight": 2, "to": transition } ] }` | Select one eligible weighted destination once |
| `"next": { "result": "name", "cases": { "applied": transition }, "otherwise": transition }` | Select using a saved typed action result's status |
| `"end": "label"` | Complete the graph with an authored label; actual alive/dead/prevented state is reported separately |

Transitions can nest: a chance branch can choose a delayed step, or a condition can choose a weighted list. Validate mutually exclusive forms and limit nesting. Probability is a finite number between 0 and 1. Integers/ranges are inclusive, nonnegative, and validated in the field's domain. Unsupported formulas must fail validation; never silently reinterpret a probability as a weight or “1 in N.”

For deliberate per-tick repetition, a step can have:

```json
{
  "repeat": { "count": 2, "interval_ticks": 1, "first_after_ticks": 1 },
  "action": { "op": "eoc", "eocs": [ "EXAMPLE_EOC_ONE_HELD_BEAT" ] },
  "next": "resolve"
}
```

At entry T, the action occurs at T+1 and T+2, then transitions. Each occurrence may draw a fresh message; loading an occurrence does not repeat its effects. Only immediate actions may use this repeat form initially. A multi-tick motion already has its own saved progress; wrap it in graph transitions if it needs to recur.

## Typed actions and EOC extension

| Action | Important fields and semantics |
| --- | --- |
| `message` | Translated `text` or weighted `pool`, `entry`, optional `append_to`, audience; choose once per occurrence |
| `move` | Role, direction/destination, tile count/range, ticks per tile, first-step timing, support/collision policy, stored result; yields between physical steps |
| `effect` | Source/target role, effect ID, duration/intensity/body part; respect native effect rules |
| `damage` | Source/target, typed damage, body-part policy, ordinary mitigation/prevention, stored result; no generic victim-as-source attribution |
| `incapacitate` | Target, source, supported state, duration, immunity and wake/recovery policy; no death masquerading as knockout |
| `fatal_injury` | Explicit lethal consequence through native supported injury/death rules; prevention remains meaningful |
| `settle_fatal` | Resolve this instance's pending fatal token now, if any; never kill a healthy actor merely because no token exists |
| `eoc` | Immediate activation EOCs with explicit role remapping and declared output locals; world changes use existing effects |

World manipulation such as field placement, item drops, bashing, explosions, and map transforms can initially use `eoc`. Use typed actions when the graph needs exact results or owned effects. A message, sound, highlight, camera hint, or display delay does not itself spend resources, move a creature, or make a kill happen.

Step actions return status plus typed facts. `move` uses statuses such as `moved`, `blocked`, `unsupported`, and `unavailable`; falling/landing/death are additional result facts. A partial stagger records actual distance and stop reason. `incapacitate` returns `applied`, `immune`, `unsupported`, or `unavailable`. `damage` and fatal settlement expose actual result/prevention facts. Do not infer success from whether an EOC was called.

Existing EOCs receive actor/target talkers and copied execution context. New execution-local helper bindings should expose declared locals and read-only action results only while an instance is active. Their implementation must not fall back to global state when called outside one.

## Example A: knife stagger, ledge, and two held beats

This scene is usable on a supported nonfatal hit, and can later be reused by a fatal-candidate rule once fatal resolution is implemented. Initial acquisition requires both creatures. Only the victim is scripted, so the attacker is not granted a free global combat pause.

The message pool is illustrative for a humanoid victim with the corresponding anatomy. Bind it only to appropriate creatures or pass a different pool for robots, eyeless creatures, creatures without blood, or other anatomy. Content matching owns that choice.

```json
{
  "type": "execution",
  "id": "example_knife_stagger",
  "name": "Knife wound and backward stagger",
  "revision": 1,
  "required_roles": [ "actor", "target" ],
  "control": { "actor": "free", "target": "scripted" },
  "max_ticks": 12,
  "max_step_entries": 32,
  "interruptions": { "external_damage": "interrupt", "displacement": "interrupt" },
  "start": "stagger",
  "steps": {
    "stagger": {
      "action": {
        "op": "move",
        "role": "target",
        "direction": "backward",
        "tiles": [ 1, 4 ],
        "ticks_per_tile": 1,
        "first_step_after_ticks": 1,
        "stop_before_unsupported": true,
        "collision": "stop",
        "store_result": "stagger"
      },
      "next": "edge_check"
    },
    "edge_check": {
      "next": {
        "if": { "execution_can_fall": { "role": "target", "direction": "backward" } },
        "then": "edge_roll",
        "else": "wait_start"
      }
    },
    "edge_roll": {
      "next": { "chance": 0.5, "then": "tumble", "else": "wait_start" }
    },
    "tumble": {
      "action": {
        "op": "move",
        "role": "target",
        "direction": "backward",
        "tiles": 1,
        "ticks_per_tile": 1,
        "first_step_after_ticks": 0,
        "stop_before_unsupported": false,
        "collision": "stop",
        "narration": {
          "on_moved": {
            "entry": "tumble",
            "text": "<target_name> tumbles backward into open air!",
            "audience": "visible_action"
          }
        },
        "store_result": "tumble"
      },
      "next": {
        "result": "tumble",
        "cases": { "moved": "resolve", "blocked": "wait_start", "unsupported": "wait_start" },
        "otherwise": "motion_failed"
      }
    },
    "wait_start": {
      "action": {
        "op": "message",
        "entry": "wounded",
        "text": "<target_name> stops, swaying.",
        "audience": "visible"
      },
      "next": "wounded_beats"
    },
    "wounded_beats": {
      "repeat": { "count": 2, "interval_ticks": 1, "first_after_ticks": 1 },
      "action": {
        "op": "message",
        "entry": "wounded_continuation",
        "append_to": "wounded",
        "audience": "visible",
        "pool": {
          "avoid_immediate_repeat": true,
          "entries": [
            { "weight": 1, "text": "<target_name> coughs up blood and grasps weakly at the knife." },
            { "weight": 1, "text": "<target_name> stares at <actor_name>, wide-eyed." },
            { "weight": 1, "text": "<target_name>'s jaw moves silently." }
          ]
        }
      },
      "next": "resolve"
    },
    "resolve": { "end": "wound_sequence_resolved" },
    "motion_failed": { "end": "movement_unavailable" }
  }
}
```

`execution_can_fall` is a proposed pure live-world predicate, using the runtime's captured backward direction and the creature's support/flight state. It performs no roll or movement. The `tumble` movement rechecks those facts at commit. `on_moved` narration is emitted only after the destination is accepted and before gravity can finalize death; it uses the observation captured for that physical action. A blocked movement cannot print a successful tumble.

At `resolve`, terminal cleanup settles any pending fatal token. For a nonfatal scene, a survivor is simply released. If a living target dies from the owned fall, that expected result permits the role-free resolution step; terminal callbacks receive outcome facts and optional missing talkers, not a new creature at the landing tile.

This definition's “grasp the knife” text assumes the weapon remains relevant to the wound. A content pack that depicts an embedded item should actually transfer/bind that item and require it; wording alone does not embed the attacker's currently wielded knife.

### Example automatic rule for the nonfatal version

```json
{
  "type": "execution_trigger",
  "id": "example_knife_stagger_on_hit",
  "event": "attack_resolved",
  "execution": "example_knife_stagger",
  "priority": 10,
  "weight": 1,
  "chance": 0.03,
  "cooldown": { "ticks": 30, "scope": "actor_target_pair" },
  "match": {
    "source_kind": [ "avatar", "npc" ],
    "target_kind": [ "npc" ],
    "channels": [ "melee" ],
    "weapon_skills": [ "stabbing" ],
    "body_parts": [ "torso" ],
    "applied_damage": [ { "type": "stab", "minimum": 1 } ],
    "fatal_state": "alive"
  }
}
```

The 3% rate is an example content tuning choice. It is not the system default. A fatal version uses `fatal_candidate`, a supported channel, and `fatal_state: "pending_fatal"`; simply removing the living check from the above post-hit rule is unsafe.

## Example B: lucky critical knockout

An attack that remains nonfatal can choose this scene. The trigger should require an eligible head hit and an incapacity-capable target; it should not assume every monster has a human head or can become unconscious. `incapacitate` still checks immunity/state when it commits.

```json
{
  "type": "execution",
  "id": "example_lucky_knockout",
  "name": "Lucky knockout",
  "revision": 1,
  "required_roles": [ "actor", "target" ],
  "control": { "actor": "free", "target": "held" },
  "max_ticks": 4,
  "start": "try_knockout",
  "steps": {
    "try_knockout": {
      "action": {
        "op": "incapacitate",
        "role": "target",
        "source": "actor",
        "state": "unconscious",
        "duration_ticks": [ 10, 30 ],
        "respect_immunity": true,
        "wake_on_damage": true,
        "store_result": "knockout"
      },
      "next": {
        "result": "knockout",
        "cases": { "applied": "announce", "immune": "resisted", "unsupported": "resisted" },
        "otherwise": "unavailable"
      }
    },
    "announce": {
      "action": {
        "op": "message",
        "entry": "knockout",
        "text": "<target_name> crumples, unconscious.",
        "audience": "visible"
      },
      "next": { "step": "done", "after_ticks": 1 }
    },
    "done": { "end": "knockout_applied" },
    "resisted": { "end": "knockout_resisted" },
    "unavailable": { "end": "target_unavailable" }
  }
}
```

Execution control ends with the scene; the actual unconscious effect remains for its own duration/recovery rules. There is no corpse, kill XP, or on-kill callback. This is gameplay incapacity, not a promise that a severe head injury is harmless or that all targets recover identically.

## Other content patterns

| Pattern | Rule/graph shape |
| --- | --- |
| Strength-based takedown | Require a supported attribute difference, choose a short hold/throw/damage graph; evaluate real strength only where both actors expose it |
| Monster→NPC execution | Match monster source ID/species/attack and NPC target class/faction; bind monster as actor and NPC as target |
| NPC→monster execution | Reverse the role filters explicitly; match style/weapon facts only if the NPC attack adapter reports them |
| Martial-arts-specific finish | Match selected style plus technique and resolved result, or invoke `start_execution` from an existing style/technique EOC |
| Damage at a precise location | Match resolved damage type and body part/weakpoint; world geometry is a separate condition on a declared participant/location |
| Rare clash | Trigger a low-probability supported exchange, script both roles, then branch into separation, counterattack, or interruption |
| Uncertain delayed beat | Use a transition with `after_ticks: [1, 3]`; it keeps the sampled deadline through save/load |
| A scene changes terrain/items | An immediate EOC action calls existing world effects; use result-bearing adapters if subsequent branches depend on success |
| An execution makes another attack | Attribute it to the bound actor, give it the parent causal ID, and suppress automatic recursive execution selection by default |

Monster “strength” must not be fabricated from melee dice or the avatar's stat. Use declared size/mass/power criteria where appropriate, or an explicit content-defined comparison. Weakpoint selection should use stable IDs added to combat context, not translated display names.

For a clash, both roles can be `scripted`, but acquisition must be atomic. For example: opening weapon-contact message → weighted choice between a real shove, an attributed counterattack after 1–3 ticks, or separation → resolution. Two competing attacks need a deduplicated encounter/contact key; otherwise both combat events can start the same clash. This is a short combat sequence, not a new persistent beam-clash physics system.

## Manual EOC invocation

Proposed effect:

```json
{
  "start_execution": "example_knife_stagger",
  "roles": { "actor": "u", "target": "npc" },
  "bind_weapon": "actor_wielded",
  "result_var": { "context_val": "started_execution" },
  "true_eocs": [ "EXAMPLE_EOC_SEQUENCE_ACCEPTED" ],
  "false_eocs": [ "EXAMPLE_EOC_SEQUENCE_REFUSED" ]
}
```

`true_eocs` means the instance was accepted, not that its target died or the graph completed. Completion is reported by lifecycle callbacks/querying the instance result. Refusal returns a reason such as unsupported actor, busy participant, missing weapon, absent context, invalid dimension, or unsupported fatal policy. A manual start cannot retrospectively reserve death after native finalization.

An extension should also expose `cancel_execution`, active-instance queries by role/ID, current step/elapsed time, and terminal result inspection. Cancellation requests are deferred safely if made inside a callback. APIs must distinguish a definition ID from an instance handle.

## Message pools, substitutions, and append behavior

`text` and `pool` are mutually exclusive. A pool may reference an existing snippet category or provide weighted translated entries. Selection and no-repeat memory belong to the instance. Validate positive candidate weights and handle a one-entry pool without an impossible no-repeat loop. Presentation inspection must not reroll the selected line.

Role substitutions such as `<actor_name>`, `<target_name>`, possessives, and pronouns need translation-aware formatting and visibility-aware names. Existing snippet/dialogue expansion can supply useful machinery, but these exact execution tags are proposed. Use complete sentences or translated templates rather than requiring English grammar from the runtime.

`append_to` names an entry/group in the same instance. Each repeated `wounded_beats` occurrence appends one segment to `wounded`, preserving segment timestamps. `entry` identifies that action's own contribution; repeated occurrences also carry their entry serial. If the anchor was never observed, do not reveal its hidden text when later appending. Create a new visible entry for the new segment instead.

The ordinary log should retain chronological ordering around other combat messages. A compact execution view may display a growing combined passage; no code may blindly mutate the global last message. These semantics require new message-record/renderer support, not only a new EOC flag.

## Validation required before content can load

- Valid entry/destination IDs, one transition form, bounded nesting/repeats, reachable terminal paths, and limits for cycles.
- Declared roles/locals/parameters; action compatibility with role kind/control; optional-role handling.
- Known weapon/style/technique/damage/body-part/monster/effect IDs and supported event-channel fields.
- Proper probability/range/weight types, finite values, meaningful durations, and no all-zero choice pools.
- Distinction between actual terminal outcomes and descriptive end labels; fatal actions require an explicit supported policy.
- Message extraction, valid snippet references, append targets, audience rules, and local-output names.
- Revision compatibility for saved step/action state, with safe invalidation when definitions are removed or changed.

Graph validation cannot prove arbitrary EOCs are pure, reversible, terminating, or fully role-safe. Use existing activation recursion limits, new execution budgets, targeted warnings for detached timers, and runtime revalidation rather than claiming full static proof.
