# Current capabilities and practical boundaries

## Generation is already more than static templates

The local generator supports ASCII maps, palettes, weighted variants, nested chunks, predecessor maps, terrain/furniture transforms, items, fields, vehicles, zones, NPC templates, ownership, and location variables. Parameters can be shared at special, OMT, nested, or vertical OMT-stack scope. Nested selection can inspect neighbors, joins, and predecessors. These can produce coherent multi-part locations with significant variation. [Mapgen documentation][mapgen-doc], [parameters][params-doc], [conditional nesting][nested-doc].

At the world scale, overmap terrain, connections, fixed and mutable specials, regional terrain settings, and dimension region layouts provide substantial control. Layout modes include uniform, weighted random, angular sectors, and a bounded Voronoi layout with a fallback outside its bounds. Cities have regional building tables and road generation; mutable specials assemble layouts through joins and phases. These are existing procedural systems, not proposed additions. [Overmap concepts][overmap-doc], [mutable generation][mutable-doc], [region layouts][layout-doc].

The generation pipeline still orchestrates major geographical passes in C++: rivers, lakes, oceans, forests, highways, cities, roads/railroads, specials, and vertical generation. Regional configuration selects and tunes many of these; it does not register arbitrary new algorithmic passes from JSON. Post-processing also offers useful configurable effects, including transforms and item modification, through a fixed set of executor types. [Pipeline][worldgen-core], [post-processing][post-doc], [executor types][post-types].

## Existing hooks and runtime changes

| Mechanism | Supported behavior | Important boundary |
| --- | --- | --- |
| Special `eoc` | Conditions gate placement; true effects run when placement is attempted through the special's placement implementation | Dialogue uses the avatar, with no shared generation-location/instance payload; effects run before the placement implementation returns |
| OMT `entry_eoc` / `exit_eoc` | Avatar boundary hooks are defined and dispatched | Exit dispatch has OBS-01; these are avatar OMT hooks, not a general resident/settlement membership lifecycle |
| Variable mapgen weights | Evaluate `dbl_or_var` weights during selection | Formula dialogue is avatar-based; it does not automatically contain the map being generated |
| `mapgen_update` | Apply update definitions at selected locations, immediately or later | EOC wrapper passes empty explicit arguments, ignores returned result, and exposes no general conflict/commit report |
| `set_overmap_terrain` | Change an OMT label at a chosen point | This does not itself rebuild the existing detailed map |
| `place_overmap_special` | Attempt runtime placement with rotation, force/unexplored options, and success/failure EOCs | Success is not a general atomic multi-map rebuild; mutable expansion has OBS-02 |
| `place_variables` | Export named absolute locations for scripts | Global names require manual instance isolation; they are not an automatic per-building place registry |
| Map extras | Weighted generation-time additions using native, mapgen, or update-mapgen executors; record applied extras | Z/region-flag eligibility is narrower than a general event condition/director |

Evidence: [special check][special-check], [special effects][special-eoc], [movement hooks][omt-cross], [weights][mapgen-weight], [update binding][update-eoc], [update engine][update-core], [location variables][place-vars], [extra model][extra-model], [extra eligibility][extra-check]. Runtime overmap editing is documented in the [EOC reference][eoc-doc].

There are also smaller binding gaps. `closest_city` selects known/all-city search and exports name/size, but has no configurable size/service filter or structured failure result. Native overmap APIs can query hordes and spawn monster groups; the inspected EOC/math registries do not expose direct equivalents for those operations. Local creature spawning and placing a special with its configured spawns remain available workarounds, with different semantics. These are candidates for adapters, not evidence that the underlying overmap simulation needs rebuilding. [City binding][city-query], [horde queries][horde-query], [group spawning][group-spawn].

## Game modes already possible through content

Scenarios configure starts, allowed character options, dates, locations, missions, vehicles, map extras, flags, and startup EOCs. Professions and recurring/event EOCs add further behavior. Game lifecycle notifications already exist, including start, load, save, avatar creation, and game-over events. [Scenario model][scenario-model], [startup dispatch][eoc-init], [event types][event-types].

Sky Island demonstrates an extraction-style mode through existing content: a restricted start, persistent home, expeditions, rewards, dimension travel/cleanup, and death prevention with penalties. This is evidence that major gameplay changes do not require a native `special_game` subclass. Its mod metadata also states a character-creation path restriction, showing why reusable mode onboarding remains useful. [Scenario][sky-start], [mod description][sky-info], [death handling][sky-death], [dimension cleanup][sky-travel].

The native special-game factory, however, switches between the default implementation and the tutorial. Its virtual initialization, per-turn, pre/post-action, and game-over hooks are C++ interfaces. They are not a JSON registry for independently saved custom mode sessions. [Native interface][mode-native], [factory][mode-factory].

## Populated places and NPC work

| Existing foundation | What authors can do now | What it does not automatically provide |
| --- | --- | --- |
| Mapgen NPC templates and unique IDs | Place named merchants, guards, quest givers, and other authored residents | Procedural household/resident generation, settlement membership, or assignment of homes/jobs |
| Dialogue, missions, factions, shopkeeper classes | Build hubs with trading, quests, reputation, and scripted services | A shared service registry with staffing, queues, opening rules, and settlement-wide supply accounting |
| Camps and companion missions | Assign residents, build expansions, consume camp food/resources, and send followers on work | A universal town simulation or arbitrary executable job from a new `faction_mission` JSON record |
| JSON behavior goals | Add conditions, math scores, categories, lifecycle EOCs, walks, pickups, activities, and interruption handling | A calendar agenda, persistent shared work-order board, or automatic offscreen execution |
| Class `work_hours` | Define a daily shift, including one crossing midnight; reconcile wake/sleep and post duty | Multiple daily appointments, weekly patterns, household calendars, and automatic service closure |
| Craft reservations | Protect specific craft requirements/providers from conflicting automation | A general reservation for a work order, appointment, bed, service slot, convoy, or entire shared task |
| NPC catch-up and overmap travel | Update selected body/effect state on load and move traveling NPCs on overmap routes | General offscreen replay of the behavior tree, settlement production, or individual daily agendas |

Evidence: [NPC placement][npc-spawn], [authored refuge][refugee], [factions][faction-model], [camp interfaces][camp-model], [JSON goals][npc-goals], [shift loading][shift-load], [duty semantics][shift-duty], [schedule reconciliation][shift-reconcile], [craft reservations][reservations], [load catch-up][npc-catchup], [overmap travel][npc-travel].

The current NPC behavior implementation is an especially important improvement: goals and their start time/active-turn count are saved, and a restored goal resumes without repeating `on_start`. EOC errands and activities give these goals real actions. The proposed schedule and job layers should use these mechanisms. [Behavior documentation][npc-behavior], [saved goal state][npc-goal-state].

The `city.population` field is explicitly **original population**. It is not a count of living NPC residents, nor evidence that a generated city contains a simulated population. [City structure][city-model].

## “Random events” currently means several different systems

| System | Existing purpose | Boundary |
| --- | --- | --- |
| Map extras | Add varied scenes during map generation | Not an ongoing regional event lifecycle |
| Native timed events | Delayed explosions, transforms, updates, and selected special behaviors | Fixed event types; some location events wait for the current dimension to match |
| Recurring EOCs | Periodic, conditional, random-interval behavior on character or global queues | Authors coordinate cooldowns, competing events, instance ownership, and missed-time behavior themselves |
| EVENT EOCs | React to typed engine notifications | Subscriptions use a compiled event enum; participants depend on the emitter and dispatch path |
| Missions and dialogue | Objectives, deadlines, conditions, followups, and start/end/fail effects | No common relationship to a multi-stage regional incident or service/work order |
| Weather and portal-storm content | Persistent-feeling world phenomena composed from weather and EOCs | Specialized content is not a general event-director schema |

Evidence: [extras][extras-settings], [timed events][timed-model], [timed processing][timed-process], [EOC model][eoc-model], [EOC processing][eoc-process], [event dispatch][event-dispatch], [missions][mission-doc], [portal-storm content][portal].

Here, `global` means the EOC uses the game's global queue and is processed on the avatar's turn. It does **not** mean its effects run independently in every settlement, all dimensions are advanced, or every NPC is continuously simulated. `run_for_npcs` applies an additional pass over `g->all_npcs()`; it is not an unloaded population simulator. [Queue processing][eoc-process], [activation implementation][eoc-activate].

## Concrete content feasibility

| Content idea | Current feasibility | What a reusable, fuller version needs |
| --- | --- | --- |
| An immense academy or dungeon with randomized wings | Native mapgen/mutable-special content | Semantic entrance, route, and objective constraints across pieces |
| A desert, island, or alien dimension | Native regional settings/layouts and custom mapgen | Better transition planning for mixed-region borders and new geography algorithms |
| A town with a blacksmith, clinic, inn, and guards | Scriptable authored hub | Resident/place/service instances and staffing tied to schedules |
| An NPC who works at a post by day and sleeps at night | Existing shift/duty/body behavior, with content setup | An agenda for distinct home, work, leisure, and exceptional duties |
| Citizens repairing raid damage and sharing supplies | Individual activities/scripts can approximate it | Work orders, permissions, reservations, and resource accounting |
| A caravan traveling between towns | Existing travel/mission primitives plus scripts | Route/manifest identity, cargo conservation, staffing, and loaded/offscreen handoff |
| A survival arena or timed wave defense | Scriptable EOC counters, spawning, locations, and timers | Reusable mode sessions, outcomes, restrictions, reset, and UI |
| An extraction roguelite | Already demonstrated by Sky Island | Shared run/progression ownership and cleanup contracts |
| A seasonal festival, epidemic, or invasion across several towns | Scriptable selected effects and state variables | Scoped event instances, persistent affected places, causal history, and catch-up |
| A town that expands or collapses while the player is away | Bespoke camp/mission/EOC approximation | Settlement model, coarse simulation, and conflict-aware local projection |

The useful target is an opt-in framework for bounded content, not per-turn physical simulation of every person and tile in an infinite world.

<!-- Source references -->

[camp-model]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/basecamp.h#L162
[city-model]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/city.h#L16
[city-query]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npctalk.cpp#L9932
[eoc-activate]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/effect_on_condition.cpp#L458
[eoc-doc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/EFFECT_ON_CONDITION.md#L2
[eoc-init]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/effect_on_condition.cpp#L164
[eoc-model]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/effect_on_condition.h#L49
[eoc-process]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/effect_on_condition.cpp#L393
[event-dispatch]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/effect_on_condition.cpp#L698
[event-types]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/event.h#L24
[extra-check]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/map_extras.cpp#L1300
[extra-model]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/map_extras.h#L60
[extras-settings]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/REGION_SETTINGS.md#L498
[faction-model]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/faction.h#L189
[group-spawn]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/overmapbuffer.h#L586
[horde-query]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/overmapbuffer.h#L274
[layout-doc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/REGION_LAYOUT.md#L1
[mapgen-doc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/MAPGEN.md#L143
[mapgen-weight]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/mapgen.cpp#L513
[mission-doc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/MISSIONS_JSON.md#L31
[mode-factory]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/gamemode.cpp#L20
[mode-native]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/gamemode.h#L16
[mutable-doc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/OVERMAP.md#L833
[nested-doc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/MAPGEN.md#L1207
[npc-behavior]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/NPC_BEHAVIOR.md#L1
[npc-catchup]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npc.cpp#L3697
[npc-goal-state]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npc_goal_state.h#L27
[npc-goals]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/NPC_BEHAVIOR.md#L408
[npc-spawn]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/mapgen.cpp#L2059
[npc-travel]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/do_turn.cpp#L452
[omt-cross]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/game.cpp#L11805
[overmap-doc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/OVERMAP.md#L71
[params-doc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/MAPGEN.md#L1317
[place-vars]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/mapgen.cpp#L3519
[portal]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/data/json/effects_on_condition/nether_eocs/portal_storm_effect_on_condition.json#L19
[post-doc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/doc/JSON/OVERMAP.md#L390
[post-types]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/mapgen_post_process.h#L22
[refugee]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/data/json/mapgen/refugee_center/refugee_center.json#L143
[reservations]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/craft_reservation.h#L25
[scenario-model]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/scenario.h#L24
[shift-duty]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npc_arbitration.cpp#L164
[shift-load]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npc_class.cpp#L274
[shift-reconcile]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npc.cpp#L2515
[sky-death]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/data/mods/Sky_Island/effectoncondition/death_and_healing.json#L4
[sky-info]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/data/mods/Sky_Island/modinfo.json#L4
[sky-start]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/data/mods/Sky_Island/scenario.json#L4
[sky-travel]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/data/mods/Sky_Island/effectoncondition/teleport.json#L229
[special-check]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/overmap.cpp#L2961
[special-eoc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/overmap_special.cpp#L276
[timed-model]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/timed_event.h#L42
[timed-process]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/timed_event.cpp#L422
[update-core]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/mapgen.cpp#L7886
[update-eoc]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/npctalk.cpp#L5465
[worldgen-core]: https://github.com/nickkk2332/nickclysm/blob/b32c9baea25f62478228c6a91cbbf9f356e4e8a3/src/overmap.cpp#L925
