#!/usr/bin/env python3
"""Portable capped HSP experiments; Python standard library only."""
import argparse
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent
# Historical default ROI lengths below 1B. Preserve the compared instruction
# ranges rather than padding these inputs to 1B by repeating their traces.
SHORT_ROIS = {
    ('bfs', 'kronecker'): 200000000,
    ('bfs', 'liveJournal'): 731130739,
    ('bfs', 'orkut'): 737247755,
    ('bfs', 'wiki'): 133989777,
    ('bfs', 'road'): 140160397,
    ('bfs', 'u22'): 400030742,
    ('bc', 'road'): 344165792,
    ('cc', 'kronecker'): 400000000,
    ('cc', 'liveJournal'): 422496632,
    ('cc', 'orkut'): 256297330,
    ('cc', 'wiki'): 217016817,
    ('cc', 'road'): 191051857,
    ('cc', 'u22'): 707903732,
    ('sssp', 'wiki'): 924096728,
    ('sssp', 'road'): 281298430,
}
JOBS = {}
for app in ['bfs', 'bc', 'cc', 'pr', 'sssp']:
    graphs = ['kronecker', 'liveJournal', 'orkut', 'wiki', 'road', 'u22']
    for graph in graphs:
        suffix = 'champsimtrace' if app == 'bfs' and graph == 'kronecker' else 'champsim'
        trace = f'{app}.{suffix}.xz' if graph == 'kronecker' else f'{app}_{graph}.{suffix}.xz'
        if graph == 'u22':
            trace = f'u22_{app}.champsim.xz'
        JOBS[f'{app}_{graph}'] = dict(trace=trace, graph=graph, instructions=SHORT_ROIS.get((app, graph), 1000000000), internal=True)
WIDTHS = {
    'hsp4_bf8': dict(hsp_bits=4, bf_bits=8, hotness_max=15),
    'hsp10_bf4': dict(hsp_bits=10, bf_bits=4, hotness_max=512),
    'hsp4_bf4': dict(hsp_bits=4, bf_bits=4, hotness_max=15),
}

def bloom_filter_environment():
    """Preserve only the supported BF overrides; isolate other HSP settings."""
    values = {}
    for suffix, default, minimum in [('HASHES', '7', 1), ('COUNTERS', '4912', 1), ('LATENCY', '1', 0)]:
        key = 'CHAMPSIM_STLB_HSP_BF_' + suffix
        try:
            value = int(os.environ.get(key, default))
        except ValueError:
            raise ValueError(f'{key} must be an integer') from None
        if value < minimum:
            raise ValueError(f'{key} must be at least {minimum}')
        values[key] = str(value)
    key = 'CHAMPSIM_STLB_HSP_BF_FALSE_POSITIVE_RATE'
    try:
        value = float(os.environ.get(key, '0.01'))
    except ValueError:
        raise ValueError(f'{key} must be a number between 0 and 1') from None
    if not 0 < value < 1:
        raise ValueError(f'{key} must be strictly between 0 and 1')
    values[key] = str(value)
    return values

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--variant', required=True, choices=['hsp4_bf8', 'hsp10_bf4', 'hsp4_bf4'])
    parser.add_argument('--trace-root', type=Path, default=Path('/tank/traces'))
    parser.add_argument('--pr-trace-root', type=Path, help='PR trace directory; overrides --trace-root')
    parser.add_argument('--sssp-trace-root', type=Path, help='SSSP trace directory; overrides --trace-root')
    parser.add_argument('--u22-trace', type=Path, help='Override U22 PR trace; raw or xz')
    parser.add_argument('--parallel', type=int, default=4)
    parser.add_argument('--jobs', nargs='+', default=['all'], help='all, bfs, bc, cc, pr, sssp, or individual job names')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    widths = WIDTHS[args.variant]
    if args.parallel < 1:
        parser.error('--parallel must be positive')
    try:
        bf_environment = bloom_filter_environment()
    except ValueError as error:
        parser.error(str(error))
    selected = []
    for value in args.jobs:
        names = list(JOBS) if value == 'all' else [j for j in JOBS if j.startswith(value + '_')] if value in ['bfs', 'bc', 'cc', 'pr', 'sssp'] else [value]
        for name in names:
            if name not in JOBS:
                parser.error('Unknown job: ' + name)
            if name not in selected:
                selected.append(name)
    output = (args.output or ROOT / 'results' / args.variant / datetime.now().strftime('%Y%m%d_%H%M%S')).resolve()
    commands = {}
    skipped = {}
    for name in selected:
        job = JOBS[name]
        trace_root = (args.pr_trace_root if name.startswith('pr_') else args.sssp_trace_root) or args.trace_root
        if name.startswith('pr_u22') and args.u22_trace:
            candidates = [args.u22_trace]
        elif job['graph'] == 'u22':
            filename = job['trace']
            candidates = [trace_root / filename, trace_root / filename.removesuffix('.xz'),
                          trace_root / 'u22_uniform_trace' / filename,
                          trace_root / 'u22_uniform_trace' / filename.removesuffix('.xz')]
        else:
            candidates = [trace_root / job['trace'], trace_root / job['trace'].removesuffix('.xz')]
        trace = next((p for p in candidates if p.is_file()), None)
        if trace is None:
            skipped[name] = [str(p) for p in candidates]
            print(f'[skip] {name}: trace not found; checked ' + ', '.join(skipped[name]), flush=True)
            continue
        binary = ROOT / 'bin' / ('champsim_' + args.variant)
        if not args.dry_run and not binary.is_file():
            parser.error(f'Missing binary for {name}: {binary}')
        commands[name] = ['nice', '-n', '10', 'stdbuf', '-oL', '-eL', str(binary), '--warmup-instructions', '50000000', '--simulation-instructions', str(job['instructions']), '--json', str(output / f'{name}.json.tmp'), str(trace)]
    if args.dry_run:
        print(json.dumps(dict(output=str(output), parallel=args.parallel, bf_environment=bf_environment, commands=commands, skipped=skipped), indent=2))
        return 0
    selected = list(commands)
    if not selected:
        print('No available traces; no simulations started.', flush=True)
        return 0
    output.mkdir(parents=True, exist_ok=False)
    env = {k:v for k,v in os.environ.items() if not k.startswith('CHAMPSIM_STLB_HSP_')}
    env.update(CHAMPSIM_STLB_HSP_BUFFER_CAPACITY='512', CHAMPSIM_STLB_HSP_NUM_BUFFERS='8',
               CHAMPSIM_STLB_HSP_PROMOTE_THRESHOLD='1', CHAMPSIM_STLB_HSP_HOTNESS_SAT=str(widths['hotness_max']),
               CHAMPSIM_STLB_HSP_INTERNAL_LATENCY='1', CHAMPSIM_STLB_HSP_SEARCH_LATENCY='9')
    env.update(bf_environment)
    manifest = dict(variant=args.variant, counter_widths=widths, bf_environment=bf_environment, warmup=50000000, shared_mshr_credits=16, jobs={n:JOBS[n] for n in selected}, commands=commands, skipped=skipped,
                    binary_sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (ROOT/'bin').iterdir()})
    (output/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    def run(name):
        print('Starting ' + name, flush=True)
        rc = 1
        summary = None
        with (output/f'{name}.log').open('w') as log:
            try:
                log.write(json.dumps(commands[name])+'\n'); log.flush()
                log.write('BF environment: ' + json.dumps(bf_environment) + '\n'); log.flush()
                rc = subprocess.run(commands[name], env=env, stdout=log, stderr=subprocess.STDOUT).returncode
                if rc == 0:
                    temporary = output/f'{name}.json.tmp'
                    with temporary.open() as source:
                        result = json.load(source)
                    cores = [c for p in result['phases'] for c in p['roi']['cores']]
                    instructions = sum(c['instructions'] for c in cores)
                    cycles = sum(c['cycles'] for c in cores)
                    assert JOBS[name]['instructions'] <= instructions <= JOBS[name]['instructions']+16
                    assert cycles > 0
                    if JOBS[name]['internal']:
                        assert result['bf_counter_bits'] == widths['bf_bits']
                        assert result['hsp_hotness_counter_bits'] == widths['hsp_bits']
                        assert result['hsp_hotness_saturation'] == widths['hotness_max']
                        assert result['bf_counters_per_filter'] == int(bf_environment['CHAMPSIM_STLB_HSP_BF_COUNTERS'])
                        assert result['bf_lookup_latency_cycles'] == int(bf_environment['CHAMPSIM_STLB_HSP_BF_LATENCY'])
                        assert 'bf_counter_saturations' in result and 'bf_false_negatives' in result
                        assert result['hsp_latency_model'] == 'internal'
                        assert result['hsp_lookup_extra_cycles'] == 0
                        assert result['roi_cycles_with_hsp'] == cycles
                    temporary.rename(output/f'{name}.json')
                    summary = dict(job=name, variant=args.variant, instructions=instructions, roi_cycles=cycles)
                    if JOBS[name]['internal']:
                        summary.update(bf_counter_saturations=result['bf_counter_saturations'], bf_false_negatives=result['bf_false_negatives'])
                    (output/f'{name}.summary.json').write_text(json.dumps(summary, indent=2)+'\n')
            except Exception as error:
                rc = 1
                log.write(f'\nValidation/launch error: {error!r}\n')
        (output/f'{name}.exit_status').write_text(str(rc)+'\n')
        print(f'Finished {name}: exit={rc}', flush=True)
        return name, rc, summary
    with ThreadPoolExecutor(max_workers=args.parallel) as pool:
        results = list(pool.map(run, selected))
    rc = int(any(rc != 0 for _,rc,_ in results))
    (output/'supervisor.exit_status').write_text(str(rc)+'\n')
    return rc

if __name__ == '__main__':
    raise SystemExit(main())
