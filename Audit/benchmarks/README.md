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
