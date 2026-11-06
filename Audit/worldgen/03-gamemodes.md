# Game modes, runs, rules, and objectives

Content can already change the central gameplay loop. Scenarios, missions, EOCs, custom UI/content, dimensions, and death prevention are sufficient for substantial conversions. The missing facilities concern reusable orchestration and ownership, especially when a mode has repeated rounds or affects more than the avatar.

## MODE-01 — Custom modes lack a registered, saved session lifecycle

**Priority:** P1 · **Effort:** M–L

**Current evidence.** The native `special_game` interface has initialization, per-turn, pre/post-action, and game-over hooks, but its factory is a compiled switch with default/tutorial implementations. Scenarios separately configure starts and startup EOCs. Engine events already expose game start/load/save, avatar creation, and related lifecycle notifications; adding those notifications again would miss the actual gap. [Native hooks][mode-native], [factory][mode-factory], [scenario fields][scenario-model], [startup EOCs][eoc-init], [engine events][event-types].

**Content limit.** A mod can build an arena through EOCs, but there is no shared mode-session record saying which ruleset is active, which phase/round it is in, who participates, and what should happen on load, abandonment, avatar replacement, or transition to another run. Global variables can represent this, but every conversion invents its own state machine and onboarding.

**Workaround.** Use a dedicated modpack, restricted scenario, startup variables, and game-load EOCs. Sky Island demonstrates this approach successfully. [Existing conversion][sky-info], [start][sky-start].

**Proposal.** Add an optional data-defined mode registry and serialized session instance, using existing EOCs for phase transitions and effects. Distinguish world initialization, character entry, run creation, load/resume, suspension, and terminal completion. Define whether a world allows one active mode or composed rule modules, and make conflicts explicit. Expose a validated entry path so character creation does not silently bypass required setup.

## MODE-02 — Mode rules lack consistent scope, enforcement, and explanations

**Priority:** P1 · **Effort:** M–L

**Current evidence.** World/mod options, scenario flags, item/trait effects, dialogue conditions, and EOCs can change many rules. The native mode can intercept player actions before and after handling, but that interception is a C++ interface. The inspected EOC framework is powerful, yet it is not one policy layer for all costs, permissions, actors, and alternate action paths. [Scenario rules][scenario-model], [native action interception][mode-actions], [EOC mechanisms][eoc-doc], [existing option overrides][sky-info].

**Content limit.** “No crafting during this duel,” “friendly fire is disabled inside the arena,” “the settlement is peaceful until curfew,” or “participants must extract with their issued loadout” involve specific action semantics. Removing a command from one menu does not enforce every NPC, item, activity, or spell path. Applying effects after an action can leave costs or consequences already committed.

**Workaround.** Restrict available content, use existing engine options/flags where suitable, and gate particular scripted interactions. A focused ruleset can be reliable without a universal policy framework.

**Proposal.** Build supported rule adapters over existing semantic operations: permission checks, target selection, resource/cost commitment, and result processing. Scope each rule to a session, participant set, place, or dimension; provide a player-facing reason when it blocks an action. Start with a small list of concrete supported policies. Do not promise an arbitrary JSON switch for every engine behavior or introduce mandatory per-turn scans of every actor.

## MODE-03 — Objectives and run outcomes lack a common orchestration model

**Priority:** P1 · **Effort:** M–L

**Current evidence.** Missions already have deadlines, followups, multiple built-in goal types, a general `goal_condition`, and start/end/fail effects. Achievements and event statistics can record progress; JSON end screens support conditional presentation. Those are valuable objective and reporting pieces. They do not collectively define one mode outcome or multi-stage round lifecycle. [Mission schema][mission-doc], [condition loading][mission-load], [end-screen schema][mode-end], [event notifications][event-types].

**Content limit.** Wave defense, tournaments, rescue campaigns, and territory challenges need rules for simultaneous success/failure, optional objectives, round transitions, scoring attribution, shared participants, timeouts, and abandonment. An end-screen definition can present an outcome; it does not itself guarantee a victory transition. A mission condition also does not automatically coordinate all arena cleanup and rewards.

**Workaround.** Compose missions and EOC counters, with one carefully maintained “resolved” variable guarding rewards. This is suitable for a single authored challenge.

**Proposal.** Add a run objective graph that references existing mission conditions, event/stat queries, and EOCs. Define terminal outcome precedence, one committed resolution per run, and result records for completion, failure, abandonment, and invalidation. Let UI read the same objective state that resolves the game. Rewards, cleanup, and next-run creation should consume a recorded result with independent completion markers, so save/load cannot award twice or strand a resolved run.

## MODE-04 — Repeated runs and progression need explicit world/character/run ownership

**Priority:** P1 · **Effort:** L

**Current evidence.** Character and global variables, saves, dimensions, teleportation, map updates, and death-prevention EOCs can implement repeated expeditions. Sky Island uses those tools for home returns, penalties, counters, and dimension cleanup. That is existing functionality. Native queues and map changes still have their own ownership rules; there is no common run-owned collection of every spawned actor, delayed callback, temporary location, and reward. [Sky Island death loop][sky-death], [travel/cleanup][sky-travel], [queued effect structure][eoc-queue], [timed event structure][timed-model].

**Content limit.** Resetting a dungeon while preserving a home, companions, account-like progression, or selected equipment requires precise scope. Broad map/dimension cleanup can destroy things that should persist; incomplete cleanup can leave old enemies, callbacks, objectives, or references active in a new run. Avatar death/replacement further complicates deciding which state belongs to the run versus the character or world.

**Workaround.** Reserve a dimension and variable namespace for each conversion, explicitly whitelist persistent assets, and maintain bespoke cleanup EOCs. This can work well in a controlled total conversion.

**Proposal.** Introduce run ownership as a reusable scope for spawned entities, timers, objective instances, temporary places, and variables. Specify retention/transfer rules between run, character, settlement, and world. Resume or terminate owned work on load and reset through defined policies. A first implementation can support one active run and a fixed persistence boundary; cross-world progression should be a separate deliberate feature with its own save design.

<!-- Source references -->

[eoc-doc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/EFFECT_ON_CONDITION.md#L2
[eoc-init]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/effect_on_condition.cpp#L164
[eoc-queue]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/character.h#L321
[event-types]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/event.h#L24
[mission-doc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/MISSIONS_JSON.md#L31
[mission-load]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/missiondef.cpp#L127
[mode-actions]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/handle_action.cpp#L3435
[mode-end]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/end_screen.h#L35
[mode-factory]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/gamemode.cpp#L20
[mode-native]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/gamemode.h#L16
[scenario-model]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/scenario.h#L24
[sky-death]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/data/mods/Sky_Island/effectoncondition/death_and_healing.json#L4
[sky-info]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/data/mods/Sky_Island/modinfo.json#L4
[sky-start]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/data/mods/Sky_Island/scenario.json#L4
[sky-travel]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/data/mods/Sky_Island/effectoncondition/teleport.json#L229
[timed-model]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/timed_event.h#L42
