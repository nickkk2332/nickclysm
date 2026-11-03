# Benchmark results

Timings from `tools/benchmark/run_benchmarks.py` (see `tools/benchmark/README.md` for
what each scenario does and how to run it). `baseline.json` is the state before any of
the audit's performance fixes.

## Baseline, before performance fixes

Commit `438e7386`, release build (`TILES=0 RELEASE=1 PCH=0`), 4-vCPU cloud VM
(Intel Xeon @ 2.10 GHz). 3 runs × 5 timed repetitions per scenario, seed 4242.

| Scenario | Median (ms) | Min–max (ms) |
|---|---|---|
| crafting_recompute | 1957 | 1822–2280 |
| horde_pathing | 217 | 185–287 |
| light_vision | 1436 | 1303–1743 |
| monsters_idle | 161 | 136–221 |
| submap_travel_dense | 728 | 663–855 |
| data_load | 25166 | 24184–25522 |

Per unit of work, that is roughly: 2 ms per recipe evaluation (time plus
craftability), 2 ms per horde turn, 7 ms per light and vision cache rebuild, 5 ms per
turn for 200 idle monsters, and 36 ms per submap crossing.

## Noise

A second run on the same code (an A/A check) gave medians within 1–5% of the baseline
for every scenario except `horde_pathing`, which differed by 11%. On this machine, treat
changes under about 10% (15% for `horde_pathing`) as noise unless repeated runs agree;
`--runs 5` narrows it further.

## After the "Fix first" group 4 performance fixes

Commit `49a3b018`, same machine and settings (`after-group4.json`).

| Scenario | Before (ms) | After (ms) | Change | Fixes |
|---|---|---|---|---|
| monsters_idle | 161 | 67 | **-58%** | MONAI-P01 |
| horde_pathing | 217 | 74 | **-66%** | MONAI-P01, SPAT-P01, MONAI-P02 |
| light_vision | 1436 | 803 | **-44%** | SPAT-P02 |
| submap_travel_dense | 728 | 617 | -15% (ranges overlap) | MAP-P01, since reverted (see below) |
| crafting_recompute | 1957 | 1925 | none | CRAFT-P02 |
| data_load | 25166 | 25566 | none | ITEM-P01 |

Measured one at a time, MONAI-P01 alone gave the monster gains (idle -58%, horde -68%);
MONAI-P02 and SPAT-P01 added nothing measurable on top at 100 monsters.

- **No measurable gain:**
  - ITEM-P01 saves too little to see in a 25 s data load.
  - CRAFT-P02 caches the book bonuses that `expected_time_to_craft()` rebuilt on every
    call. Stack sampling shows `crafting_recompute` is dominated instead by
    `can_make_with_inventory()`: the requirement checks walking the crafting inventory
    through `std::function` visitors for every component, tool and quality. That is the
    next target for crafting-menu lag.
- **Not measurable here:** DATA-P01 (loading screen redraws) only affects the tiles
  build with a display.

**MAP-P01 was reverted** (`edc768f7`). Narrowing the reconcile on a map shift to the newly
loaded submaps broke `item_wakeup_shift_reconciles_dropped_entries`: the full walk on
every shift deliberately re-arms wakeup entries that were dropped for items already in
the bubble. A future fix should make that walk cheaper instead (for example a fast path
in `item_wakeup_manager::rebuild_for_item` for items with nothing scheduled).

## Round 2: profile-driven fixes

`before-round2.json` is the code after the group 5 fixes (`917f2c3c`, no round-2
changes); `after-round2.json` is after them (`e43227b3`). Both were run back to back
on the same machine, 2 runs × 5 repetitions, while other work was using the CPU, so
treat the noise floor as about 10%. `zone_sort_queries` and `crafting_recompute_scoped`
were not in the before binary. Their before numbers come from separate builds, measured with
5 repetitions in one run: for `zone_sort_queries`, the final code without the ACT-P01 change;
for `crafting_recompute_scoped`, the code before the requirement-check change. The JSON files
hold only what each binary measured. The `commit` field names the code each binary was built from.

| Scenario | Before (ms) | After (ms) | Change | Fix |
|---|---|---|---|---|
| crafting_recompute | 2006 | 388 | **-81%** | requirement checks share an inventory index |
| overmap_generate | 666 | 255 | **-62%** | OMAP-P01 |
| weather_catchup | 92 | 19 | **-79%** | LOOP-P01 |
| zone_sort_queries | 11.6 | 5.1 | **-56%** | ACT-P01 |
| submap_travel_dense | 778 | 736 | -5% (within noise) | MAP-P01, second attempt |
| crafting_recompute_scoped | 51 | 52 | none | (the menu path, for reference) |
| data_load | 27825 | 26317 | -5% (within noise) | none |

- **Crafting.** The note above was half wrong: the crafting menu opens a
  `query_cache_scope`, inside which inventory queries use a per-type index, so the menu
  was already fast (`crafting_recompute_scoped`: 52 ms for 1500 recipe checks). The slow
  path is every other caller of `can_make_with_inventory()`: zone and NPC activities,
  bionic installation, butchery, vehicle repair checks. Those walked the whole inventory
  once per alternative. The check now opens its own scope, which is a no-op inside the
  menu's.
- **Weather catch-up** samples every 5 minutes instead of every minute (live weather is
  re-rolled every 5 minutes) and looks the terrain and wind power up once.
- **Overmap generation**: the special placement check tests terrain first and no longer
  copies the special's location list for every candidate rotation.
- **Zones** return their tile sets by reference instead of copying them for every lookup.
- **MAP-P01** now keeps the full walk and makes it cheaper: no `map_cursor` for empty
  tiles, and a one-lookup fast path for items with no wakeups. Stack samples put the
  reconcile at about 17% of a map shift in the dense scenario; the saving is within noise.

## Round 3: remaining high-severity performance findings

`before-round3.json` is the round 3 branch with the six measured performance fixes reverted
in the working tree; `after-round3.json` is the branch itself. Both were run back to back,
2 runs × 5 repetitions. Each scenario was committed before its fix.

| Scenario | Before (ms) | After (ms) | Change | Fix |
|---|---|---|---|---|
| blueprint_build_reqs | 228 | 0.6 | **-99.7%** | TERR-P01: index constructions once per data load |
| spellbook_damage_preview | 9.4 | 0.2 | **-97%** | MAGIC-P01: no temporary avatars or level-up events |
| craft_unattended_wait | 186 | 8.0 | **-96%** | CRAFT-P01: run the environment check only when its poll is due |
| vehicle_collision_check | 262 | 39 | **-85%** | VEH-P01: look up the driver only for creature hits |
| temp_map_load | 202 | 75 | **-63%** | MAP-P02: allocate temporary maps' caches lazily |
| farm_scan | 2.0 | 2.1 | none | ACT-P02 (see below) |
| data_load | 23885 | 22475 | -6% (within noise) | none |

- **ACT-P02** is not visible in `farm_scan`. A check-only scan stops at the first tile it
  can act on or report, so it runs the planting check once per scan both before and after.
  The fix pays off in the scans that visit many tiles: tiles to clear, harvest, fertilize
  or till no longer run the check, and the rest share one weather sample per overmap tile.
  Its unit tests count the weather samples directly.
- **UIHUD-P01** (surroundings menu redraw) and **REND-P01** (flying-creature shadows in the
  tiles renderer) need a display, so they have no scenario.
- `run_benchmarks.py` now finds `BENCH` lines that a log line without a newline has run
  into; before, `temp_map_load` (which loads maps, and logs about it) was silently
  missing from results.

## Round 4: twelve more high-severity performance findings

`before-round4.json` is the errands branch before this work (`4d3266c8`) with only the new
scenario files added (and the test spells one of them uses), so it has none of the fixes.
`after-round4.json` is the merged branch (`61727c0c`). Both were run back to back on an
otherwise idle machine, 2 runs × 5 repetitions. Besides the new scenarios, the table has
`temp_map_load` and `overmap_generate`, which run code these fixes touched.

| Scenario | Before (ms) | After (ms) | Change | Fix |
|---|---|---|---|---|
| stats_overmap_special_history | 513 | 0.4 | **-99.9%** | EVT-P01: record the special when the OMT is entered, instead of loading every visited overmap on game load |
| zone_job_missing_tool | 663 | 24 | **-96%** | ACT-P03: requirement checks share their answers within a scan |
| omt_find_closest_miss | 160 | 7.5 | **-95%** | OMAP-P02: no spiral vector, one terrain match per type, skip overmap levels with nothing to find |
| mapbuffer_lookup_missing | 29 | 1.7 | **-94%** | MAP-P03: list each segment's quad files once |
| npc_spell_eval | 4478 | 354 | **-92%** | NPC-P01, MAGIC-P02: rate spells without recomputing areas and dps per tile |
| shot_flockshot | 112 | 22 | **-80%** | CMBT-P01: the pellets of a shot fly together |
| inventory_queries | 6.8 | 2.1 | **-69%** | INV-P02: one allocation per location, no locations below the top level |
| omt_find_npc_goals | 5.9 | 2.5 | **-58%** | OMAP-P02 |
| npc_weapon_eval | 46 | 22 | **-54%** | NPC-P02: rate weapons only at tiles with creatures |
| mapbuffer_save_uniform | 13.9 | 6.8 | **-51%** | MAP-P03: don't open the save for uniform quads |
| phase_change_lake_frozen | 122 | 98 | -20% (ranges overlap) | MGEN-P01 |
| phase_change_lake_thawed | 110 | 95 | -14% (ranges overlap) | MGEN-P01 |
| temp_map_load | 56 | 48 | -14% (ranges overlap) | MGEN-P01, on dry terrain |
| shot_birdshot | 9.5 | 7.9 | -17% (within noise) | CMBT-P01, the common case |
| item_nested_deserialize | 38 | 37 | -4% (within noise) | INV-P01 |
| shrapnel_armored | 13.0 | 13.0 | none | CMBT-P02, not changed |
| overmap_generate | 391 | 380 | -3% (within noise) | none |
| data_load | 22427 | 22141 | -1% (within noise) | none |

- **MGEN-P01** gains less than the audit expected. The phase pass was a smaller share of a
  submap load than it estimated; the rest is `map::loadn`'s other work. The fix's own
  back-to-back runs agree in direction (frozen lake 152 to 109 ms, thawed 130 to 99 ms), but
  the ranges overlap in both measurements.
- **INV-P01** is mainly a correctness fix: contained items now keep their uids, so saved item
  locations into containers survive a load. JSON parsing dominates `item_nested_deserialize`;
  the copies it removes were the smaller part.
- **CMBT-P02** was measured and left alone: a grenade among 25 armored zombies, about 170
  fragment hits, takes 13 ms.
- `omt_find_closest_miss` depends on which overmaps already exist, and other scenarios
  (`overmap_generate`) clear the overmap buffer, so compare only runs of the same scenario set.

## Section H: the NPC decision tree

Section H (NPC-J03) made the behavior tree editable and added conditions, math scores and
JSON goals. `npc_decision` times 300 turns of the `npc_decision` tree for 20 NPCs (followers,
guards, thirsty and idle). Before is `c9871943` with only the scenario added; after is the
section H branch. Three interleaved rounds of 5 repetitions each, on an otherwise idle machine:

| Scenario | Before, median of rounds (ms) | After, median of rounds (ms) | Fastest repetition, before / after (ms) |
|---|---|---|---|
| npc_decision | 53.1 | 55.0 | 48.5 / 50.2 |
| npc_decision_condition_math | | 59.2 | 56.2 |
| npc_decision_condition_math_cached | | 55.2 | 51.9 |

- The vanilla tree costs the same within noise (the round medians range 52 to 66 ms both
  before and after), about 9 µs per NPC per turn.
- The four condition and math nodes add about 4 ms, under 1 µs per NPC per turn;
  `evaluate_every: 5 turns` takes back most of it.
