# Engine benchmarks

Repeatable timings for the engine's hot paths, so that a performance change can be
measured before and after instead of guessed at.

## Running

Build the test binary with optimizations, then run the script from the repository root:

```bash
make -j4 TILES=0 RELEASE=1 PCH=0 tests
tools/benchmark/run_benchmarks.py --runs 3 --out /tmp/after.json --compare Audit/benchmarks/baseline.json
```

- `--runs N` starts the test binary N times and combines the results (median of the
  per-run medians). `--reps N` sets the timed repetitions per scenario per run (default 5).
- `--filter bench_light_vision` runs one scenario.
- `--compare FILE` prints the change against a saved run. A change is marked "within
  noise" when the min–max ranges of the two runs overlap.
- Run on an otherwise idle machine. Each scenario is single-threaded, but other load,
  CPU frequency scaling and thermal throttling all add noise. On the 4-vCPU cloud VM the
  baseline was recorded on, two runs of the same code differed by 1–5% (11% for
  `horde_pathing`); see `Audit/benchmarks/README.md`.
- Build with `PCH=0` if you have edited headers: with the precompiled header the build
  does not notice header changes.

The scenarios can also be run directly: `tests/cata_test "[benchmark_scenario]"`
(`BENCH_REPS=1` for a quick check).

## Scenarios

Each scenario builds a fixed, seeded setup without timing it, then times a fixed amount
of work. Scenarios are in `tests/benchmark_scenarios_test.cpp`, except the ones marked
*combat* (`tests/benchmark_scenarios_combat_test.cpp`), *load*
(`tests/benchmark_scenarios_load_test.cpp`), *world*
(`tests/benchmark_scenarios_world_test.cpp`), *npc*
(`tests/benchmark_scenarios_npc_test.cpp`) and *sound*
(`tests/benchmark_scenarios_sound_test.cpp`).

| Scenario | Work timed | Audit findings it covers |
|---|---|---|
| `monsters_idle` | 200 zombies that can't see the player, 30 turns of the monster loop | MONAI-P01, MONAI-P02 |
| `horde_pathing` | 100 zombies pathing around two rings of wall to an invulnerable player, 100 turns | SPAT-P01, MONAI-P02 |
| `light_vision` | the player stepping back and forth among lights at night, 200 light and vision cache rebuilds | SPAT-P02 |
| `submap_travel_dense` | 20 submap crossings over already generated terrain, with a dense base (2,700 items) and a full inventory | MAP-P01 |
| `crafting_recompute` | crafting time and craftability for 300 recipes, 5 times, in a dense base, outside any query cache scope (the path activities and NPCs take) | CRAFT-P02, requirement checks |
| `crafting_recompute_scoped` | the same, inside the query cache scope the crafting menu opens | crafting menu |
| `crafting_panel_redraws_rebuilt` | the crafting menu's info panel for 2 recipes (crossbow: tools, qualities, proficiencies; bat: steps), 100 redraws each, built on every redraw as the menu did before, in a dense base, inside the menu's query cache scope | UIINV-P01 |
| `crafting_panel_redraws_cached` | the same redraws, read from the menu's panel cache, which builds each panel once | UIINV-P01 |
| `weather_catchup` | `sum_conditions` over 3 days at 4 locations, as a returning player's funnels and solar vehicles run it | LOOP-P01 |
| `overmap_generate` | generating one overmap from scratch | OMAP-P01, OMAP-P02 |
| `zone_sort_queries` | 2,000 "which loot zone takes this item" lookups with 9 category zones of 400 tiles | ACT-P01 |
| `spellbook_damage_preview` | the RANDOM_DAMAGE row of the "Study a spell" preview (damage at max level and per level), 200 redraws | MAGIC-P01 |
| `temp_map_load` | loading 100 tinymaps and 20 smallmaps over generated terrain, one wall edit each | MAP-P02 |
| `vehicle_collision_check` | 2000 steps of the collision check a moving vehicle runs per tile | VEH-P01 |
| `craft_unattended_wait` | 300 turns of a crafter waiting on an unattended craft step | CRAFT-P01 |
| `farm_scan` | 5 check-only scans of a 10x10 planted farm plot (a check-only scan stops at the first tile, so it does not show ACT-P02's saving) | ACT-P02 |
| `blueprint_build_reqs` | build requirements for every constructible terrain and furniture, 10 per call, 5 passes | TERR-P01 |
| `shot_flockshot` (*combat*) | 2 rounds of 27.3x110 flockshot (24155 pellets): at an unarmored target 6 tiles away, at an armored one 2 tiles away | CMBT-P01 |
| `shot_birdshot` (*combat*) | 30 rounds of 12 gauge birdshot (100 pellets) at a target 5 tiles away, the common case | CMBT-P01 |
| `shrapnel_armored` (*combat*) | 1 grenade on an armored zombie, with 8 more 1 tile away and 16 more 2 tiles away, all surviving it | CMBT-P02 |
| `npc_spell_eval` (*combat*) | 20 attack choices of an NPC knowing 4 attack spells (blast, cone, line, single target), 6 zombies and the player in range | NPC-P01, MAGIC-P02 |
| `npc_weapon_eval` (*combat*) | 50 attack choices of an NPC with a rifle (2 fire modes), a shotgun, 2 melee weapons and rocks, 6 zombies at 3 to 30 tiles | NPC-P02 |
| `stats_overmap_special_history` (*load*) | the last overmap special entered, over 400 OMTs entered in 4 saved but unloaded overmaps, as a game load rebuilds it | EVT-P01 |
| `mapbuffer_save_uniform` (*load*) | saving 25 explored OMTs in a compressed world: 25 ground quads and 500 uniform quads | MAP-P03 |
| `mapbuffer_lookup_missing` (*load*) | looking up the 84 submaps of 25 OMTs that are not on disk, in a compressed world, as `map::generate` does | MAP-P03 |
| `item_nested_deserialize` (*load*) | reading a 5-level tree of 31 bags holding 186 items, 20 times | INV-P01 |
| `inventory_queries` (*world*) | amount, charges, quality, filter and wielded-item queries on a full inventory with nested containers, 300 rounds | INV-P02 |
| `phase_change_lake_thawed` (*world*) | 40 smallmap loads (all z-levels) over 4 overmap tiles of open lake in summer | MGEN-P01 |
| `phase_change_lake_frozen` (*world*) | 40 smallmap loads over the same lake, frozen, on the winter day that freezes most of it | MGEN-P01 |
| `omt_find_closest_miss` (*world*) | 2 `find_closest` searches that match nothing, within one overmap width on all z-levels, existing overmaps only | OMAP-P02 |
| `omt_find_npc_goals` (*world*) | 10 NPC-style goal searches for any of 6 shop types within 75 tiles on the surface, and 4 `find_all` of fields within 60 tiles | OMAP-P02 |
| `zone_job_missing_tool` (*world*) | 3 farm scans of a 10x10 plot to till with no digging tool anywhere, beside a loot zone of 900 tiles holding 3 items each | ACT-P03 |
| `npc_decision` (*npc*) | 300 turns of the `npc_decision` behavior tree for 20 NPCs (5 followers, 5 guards, 5 thirsty carrying water, 5 idle), as `npc::move()` walks it | NPC-J03 |
| `npc_decision_condition_math` (*npc*) | the same, with 4 condition and math nodes added to the priorities (`data/mods/TEST_DATA/npc_behavior_bench.json`) | NPC-J03 |
| `npc_decision_condition_math_cached` (*npc*) | the same, with those 4 nodes evaluated every 5 turns | NPC-J03 |
| `sound_open_ground` (*sound*) | 100 turns of 20 sounds and 100 footsteps, heard by 300 zombies (`process_sounds`) and the avatar (`process_sound_markers`), on open ground | SPAT-J01 |
| `sound_brick_walls` (*sound*) | the same among a grid of brick walls, which muffle 15 since SPAT-J01-values (it was `sound_vanilla_walls` while vanilla walls muffled nothing and the walk was skipped) | SPAT-J01 |
| `sound_muffling_walls` (*sound*) | the same among walls with a `sound_attenuation` (TEST_DATA), so every hearer in earshot walks the line to each sound cluster | SPAT-J01 |
| `data_load` | test binary start until "Game data loaded" (core and test data) | ITEM-P01 |

The monster loop mirrors `monmove()` in `src/do_turn.cpp` (process turn, then
plan/move until out of moves), plus the vision cache rebuild `do_turn()` does before it,
but without the avatar's input handling and screen updates, which tests can't drive.

## Not covered

- DATA-P01 (a vsync-blocked redraw for every loading step) only exists in the tiles build
  with a real display.
- UI redraw loops (the crafting menu's 100 Hz redraw, the surroundings menu) need a
  display. The `crafting_panel_redraws_*` scenarios call the crafting menu's panel-building
  code directly, without ImGui; `crafting_recompute` times the craftability checks behind
  the recipe list.
- REND-P02 (sprite lookups in the tiles renderer) only exists in the tiles build.

## Changing scenarios

Keep a scenario's work identical across the versions you compare. If you change what a
scenario does, rename it, so new numbers aren't compared with old ones.
