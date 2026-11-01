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
