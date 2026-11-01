#!/usr/bin/env python3
"""Run the engine benchmark scenarios and record or compare the results.

The scenarios live in tests/benchmark_scenarios_test.cpp (hidden tag
[benchmark_scenario]). Each prints a line like
    BENCH monsters_idle median_ms=217.49 min_ms=210.10 max_ms=230.02 reps=5 work="..."
This script also measures how long the test binary takes to load the game data,
from its log timestamps.

Examples:
    # record a baseline
    tools/benchmark/run_benchmarks.py --out Audit/benchmarks/baseline.json
    # after a change, compare with it
    tools/benchmark/run_benchmarks.py --compare Audit/benchmarks/baseline.json

Build the tests first (make ... tests; RELEASE=1 for meaningful numbers) and run on an
otherwise idle machine: the scenarios are single-threaded, but other load still adds noise.
"""

import argparse
import datetime
import json
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
import tempfile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
# Not anchored at the start: a log line printed without a newline (map loading does this)
# can end up in front of it.
BENCH_RE = re.compile(
    r'BENCH (\S+) median_ms=([\d.]+) min_ms=([\d.]+) max_ms=([\d.]+) reps=(\d+) work="(.*)"$')
TIME_RE = re.compile(r'^(\d\d):(\d\d):(\d\d)\.(\d+) ')


def log_time_ms(line):
    m = TIME_RE.match(line)
    if not m:
        return None
    h, mi, s, frac = m.groups()
    return ((int(h) * 60 + int(mi)) * 60 + int(s)) * 1000 + int(frac[:3].ljust(3, '0'))


def run_once(binary, seed, reps, filter_tag):
    """Runs the scenarios once; returns (scenarios dict, data load ms or None)."""
    user_dir = tempfile.mkdtemp(prefix='cdda-bench-')
    env = dict(os.environ, BENCH_REPS=str(reps))
    try:
        proc = subprocess.run(
            [binary, f'--user-dir={user_dir}/', '--rng-seed', str(seed), filter_tag],
            cwd=REPO, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, errors='replace', check=False)
    finally:
        shutil.rmtree(user_dir, ignore_errors=True)
    scenarios = {}
    start = loaded = None
    for line in proc.stdout.splitlines():
        m = BENCH_RE.search(line)
        if m:
            name, median, lo, hi, n, work = m.groups()
            scenarios[name] = {'median_ms': float(median), 'min_ms': float(lo),
                               'max_ms': float(hi), 'reps': int(n), 'work': work}
        elif 'Starting log.' in line and start is None:
            start = log_time_ms(line)
        elif 'Game data loaded' in line and loaded is None:
            loaded = log_time_ms(line)
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout[-4000:])
        raise SystemExit(f'benchmark run failed with exit code {proc.returncode}')
    if not scenarios:
        sys.stderr.write(proc.stdout[-4000:])
        raise SystemExit(f'no scenario matched {filter_tag!r} (no BENCH lines in the output)')
    load_ms = None
    if start is not None and loaded is not None:
        load_ms = (loaded - start) % (24 * 3600 * 1000)
    return scenarios, load_ms


def git_commit():
    try:
        return subprocess.run(['git', 'rev-parse', '--short', 'HEAD'], cwd=REPO,
                              capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return 'unknown'


def cpu_model():
    try:
        with open('/proc/cpuinfo', encoding='utf-8') as f:
            for line in f:
                if line.startswith('model name'):
                    return line.split(':', 1)[1].strip()
    except OSError:
        pass
    return platform.processor() or 'unknown'


def collect(args):
    runs = []
    loads = []
    for i in range(args.runs):
        print(f'run {i + 1}/{args.runs}...', file=sys.stderr, flush=True)
        scenarios, load_ms = run_once(args.binary, args.seed, args.reps, args.filter)
        runs.append(scenarios)
        if load_ms is not None:
            loads.append(load_ms)
    # Combine runs: median of the per-run medians, overall min and max.
    names = sorted({name for run in runs for name in run})
    combined = {}
    for name in names:
        per_run = [run[name] for run in runs if name in run]
        combined[name] = {
            'median_ms': statistics.median(r['median_ms'] for r in per_run),
            'min_ms': min(r['min_ms'] for r in per_run),
            'max_ms': max(r['max_ms'] for r in per_run),
            'samples': sum(r['reps'] for r in per_run),
            'work': per_run[0]['work'],
        }
    if loads:
        combined['data_load'] = {
            'median_ms': statistics.median(loads), 'min_ms': min(loads), 'max_ms': max(loads),
            'samples': len(loads), 'work': 'test binary start to "Game data loaded"',
        }
    return {
        'label': args.label,
        'commit': args.commit or git_commit(),
        'date': datetime.datetime.now().isoformat(timespec='seconds'),
        'machine': {'cpu': cpu_model(), 'cpus': os.cpu_count(), 'system': platform.platform()},
        'binary': os.path.relpath(os.path.abspath(args.binary), REPO),
        'seed': args.seed,
        'scenarios': combined,
    }


def print_table(result, baseline=None):
    header = '| Scenario | Median (ms) | Min–max (ms) |'
    sep = '|---|---|---|'
    if baseline:
        header += ' Baseline median (ms) | Change |'
        sep += '---|---|'
    print(header)
    print(sep)
    for name, cur in result['scenarios'].items():
        row = f"| {name} | {cur['median_ms']:.1f} | {cur['min_ms']:.1f}–{cur['max_ms']:.1f} |"
        if baseline:
            base = baseline['scenarios'].get(name)
            if base:
                change = (cur['median_ms'] - base['median_ms']) / base['median_ms'] * 100
                # Call a change real only when the ranges don't overlap.
                overlap = cur['min_ms'] <= base['max_ms'] and base['min_ms'] <= cur['max_ms']
                note = '' if not overlap else ' (within noise)'
                row += f" {base['median_ms']:.1f} | {change:+.1f}%{note} |"
            else:
                row += ' – | new |'
        print(row)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--binary', default=os.path.join(REPO, 'tests', 'cata_test'))
    parser.add_argument('--runs', type=int, default=1,
                        help='separate process runs to combine (default 1)')
    parser.add_argument('--reps', type=int, default=5,
                        help='timed repetitions per scenario per run (default 5)')
    parser.add_argument('--seed', type=int, default=4242)
    parser.add_argument('--filter', default='[benchmark_scenario]',
                        help='Catch2 test spec, e.g. bench_light_vision')
    parser.add_argument('--label', default='')
    parser.add_argument('--commit', default='',
                        help='commit the binary was built from (default: the checkout\'s HEAD)')
    parser.add_argument('--out', help='write the results as JSON to this file')
    parser.add_argument('--compare', help='baseline JSON to compare against')
    args = parser.parse_args()

    result = collect(args)
    baseline = None
    if args.compare:
        with open(args.compare, encoding='utf-8') as f:
            baseline = json.load(f)
        print(f"Baseline: {baseline.get('label') or '-'} at {baseline['commit']} "
              f"({baseline['date']})")
    print(f"Current: {result['label'] or '-'} at {result['commit']} ({result['date']}), "
          f"{result['machine']['cpu']}")
    print_table(result, baseline)
    if args.out:
        os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
        with open(args.out, 'w', encoding='utf-8') as f:
            json.dump(result, f, indent=2)
            f.write('\n')
        print(f'wrote {args.out}', file=sys.stderr)


if __name__ == '__main__':
    main()
