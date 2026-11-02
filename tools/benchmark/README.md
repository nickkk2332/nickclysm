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

Each scenario (in `tests/benchmark_scenarios_test.cpp`, and the combat ones in
`tests/benchmark_scenarios_combat_test.cpp`) builds a fixed, seeded setup
without timing it, then times a fixed amount of work.

| Scenario | Work timed | Audit findings it covers |
|---|---|---|
| `monsters_idle` | 200 zombies that can't see the player, 30 turns of the monster loop | MONAI-P01, MONAI-P02 |
| `horde_pathing` | 100 zombies pathing around two rings of wall to an invulnerable player, 100 turns | SPAT-P01, MONAI-P02 |
| `light_vision` | the player stepping back and forth among lights at night, 200 light and vision cache rebuilds | SPAT-P02 |
| `submap_travel_dense` | 20 submap crossings over already generated terrain, with a dense base (2,700 items) and a full inventory | MAP-P01 |
| `crafting_recompute` | crafting time and craftability for 300 recipes, 5 times, in a dense base, outside any query cache scope (the path activities and NPCs take) | CRAFT-P02, requirement checks |
| `crafting_recompute_scoped` | the same, inside the query cache scope the crafting menu opens | crafting menu |
| `weather_catchup` | `sum_conditions` over 3 days at 4 locations, as a returning player's funnels and solar vehicles run it | LOOP-P01 |
| `overmap_generate` | generating one overmap from scratch | OMAP-P01, OMAP-P02 |
| `zone_sort_queries` | 2,000 "which loot zone takes this item" lookups with 9 category zones of 400 tiles | ACT-P01 |
| `spellbook_damage_preview` | the RANDOM_DAMAGE row of the "Study a spell" preview (damage at max level and per level), 200 redraws | MAGIC-P01 |
| `temp_map_load` | loading 100 tinymaps and 20 smallmaps over generated terrain, one wall edit each | MAP-P02 |
| `vehicle_collision_check` | 2000 steps of the collision check a moving vehicle runs per tile | VEH-P01 |
| `craft_unattended_wait` | 300 turns of a crafter waiting on an unattended craft step | CRAFT-P01 |
| `farm_scan` | 5 check-only scans of a 10x10 planted farm plot (a check-only scan stops at the first tile, so it does not show ACT-P02's saving) | ACT-P02 |
| `blueprint_build_reqs` | build requirements for every constructible terrain and furniture, 10 per call, 5 passes | TERR-P01 |
| `shot_flockshot` | 2 rounds of 27.3x110 flockshot (24155 pellets): at an unarmored target 6 tiles away, at an armored one 2 tiles away | CMBT-P01 |
| `data_load` | test binary start until "Game data loaded" (core and test data) | ITEM-P01 |

The monster loop mirrors `monmove()` in `src/do_turn.cpp` (process turn, then
plan/move until out of moves), plus the vision cache rebuild `do_turn()` does before it,
but without the avatar's input handling and screen updates, which tests can't drive.

## Not covered

- DATA-P01 (a vsync-blocked redraw for every loading step) only exists in the tiles build
  with a real display.
- UI redraw loops (the crafting menu's 100 Hz redraw, the surroundings menu) need a
  display; `crafting_recompute` times the work such a redraw repeats.

## Changing scenarios

Keep a scenario's work identical across the versions you compare. If you change what a
scenario does, rename it, so new numbers aren't compared with old ones.
