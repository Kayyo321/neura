"""Time the bench-wall programs in nvm.wasm under Node and headless browsers against the native runner
(Apps030.md, "Execution in the browser", step 3).

Every program runs at its quick size, natively with native code off (function programs, then regions) and in
nvm.wasm (function programs, then regions), and its output must match. Prints the median wall times, their
geometric means, and the longest idle proof with its block and, under Node, its main-thread CPU time. Timings
taken on a loaded machine are indicative; the ratios are what the A2 decision reads."""

import argparse
from importlib import import_module
import json
import math
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import sys
from tempfile import TemporaryDirectory
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
host_check = import_module('check-host')
SIZES = re.compile(r'^// sizes: quick (\d+) full (\d+) long (\d+)$', re.MULTILINE)


def native(neura, image, size, regions, runs):
    environment = dict(os.environ, NEURA_TIER3='off', NEURA_GPU='off')
    if not regions:
        environment['NEURA_TIER_PROGRAMS'] = '1'
    times = []
    output = None
    for _ in range(runs):
        started = time.perf_counter()
        result = subprocess.run([neura, 'run', '--jit=off', '--max-steps', str(10**12), '--max-heap-cells',
                                 str(2**32), image, '--', str(size)], capture_output=True, text=True,
                                env=environment, timeout=600, check=True)
        times.append(time.perf_counter() - started)
        output = result.stdout
    return statistics.median(times) * 1000, output


def geomean(values):
    return math.exp(sum(math.log(v) for v in values) / len(values))


def report(label, results, natives):
    if isinstance(results, dict) and 'error' in results:
        raise ValueError(f'{label}: {results["error"]}')
    rows = {}
    longest = None
    for entry in results:
        name, regions = entry['name'], entry['regions']
        for sample in entry['samples']:
            if sample['output'] != natives[name]['output'] or sample['kind'] != 'completed':
                raise ValueError(f'{label}: {name} printed {sample["output"]!r}, natively {natives[name]["output"]!r}')
            proof = sample['longest']
            if longest is None or proof['wall'] > longest[1]['wall']:
                longest = (name, proof, sample['longestCpu'])
        rows.setdefault(name, {})['regions' if regions else 'programs'] = statistics.median(
            s['milliseconds'] for s in entry['samples'])
    print(f'{label}:')
    print(f'  {"program":<16}{"native fp":>11}{"native rg":>11}{"wasm fp":>10}{"wasm rg":>10}')
    for name, row in rows.items():
        n = natives[name]
        print(f'  {name:<16}{n["programs"]:>11.1f}{n["regions"]:>11.1f}{row["programs"]:>10.1f}{row["regions"]:>10.1f}')
    columns = [[natives[n]['programs'] for n in rows], [natives[n]['regions'] for n in rows],
               [rows[n]['programs'] for n in rows], [rows[n]['regions'] for n in rows]]
    means = [geomean(column) for column in columns]
    print(f'  {"geomean ms":<16}{means[0]:>11.1f}{means[1]:>11.1f}{means[2]:>10.1f}{means[3]:>10.1f}')
    print(f'  wasm regions / wasm function programs: {means[3] / means[2]:.3f}; '
          f'wasm function programs / native: {means[2] / means[0]:.2f}')
    name, proof, cpu = longest
    print(f'  longest idle proof: {proof["wall"]:.3f} ms wall ({name}, function {proof.get("function")}, '
          f'block {proof.get("block")}); longest main-thread CPU of a proof: {cpu:.3f} ms')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--neura', required=True, type=Path)
    parser.add_argument('--wasm', default=ROOT / 'build/wasm-runtime', type=Path)
    parser.add_argument('--browser', action='append', default=[])
    parser.add_argument('--runs', type=int, default=3)
    parser.add_argument('--only', default='')
    args = parser.parse_args()
    selected = set(filter(None, args.only.split(',')))
    neura = args.neura.resolve()
    with TemporaryDirectory(prefix='neura-wasm-bench-') as path:
        work = Path(path)
        for source in (args.wasm / 'nvm.wasm', args.wasm / 'nvm.mjs', ROOT / 'hosts/web/nvm.js', HERE / 'bench.mjs'):
            shutil.copy(source, work / source.name)
        (work / 'package.json').write_text('{"type":"module"}\n')
        (work / 'index.html').write_text('<!doctype html><meta charset="utf-8"><title>Neura wasm bench</title>'
                                         '<script type="module" src="/bench.mjs"></script>')
        programs, natives = [], {}
        for source in sorted((ROOT / 'tests/bench-wall/neura').glob('*.nra')):
            if selected and source.stem not in selected:
                continue
            size = int(SIZES.search(source.read_text()).group(1))
            image = work / f'{source.stem}.nvr'
            subprocess.run([neura, 'build', source, '-o', image], cwd=ROOT, check=True, timeout=120)
            fp, output = native(neura, image, size, False, args.runs)
            rg, regions_output = native(neura, image, size, True, args.runs)
            if output != regions_output:
                raise ValueError(f'{source.stem}: native tier outputs differ')
            natives[source.stem] = dict(programs=fp, regions=rg, output=output)
            programs.append(dict(name=source.stem, size=size, runs=args.runs))
        (work / 'programs.json').write_text(json.dumps(programs))
        output = subprocess.check_output(['node', work / 'bench.mjs'], text=True, timeout=3600)
        report('Node ' + subprocess.check_output(['node', '--version'], text=True).strip(), json.loads(output), natives)
        for browser in args.browser:
            version = subprocess.check_output([browser, '--version'], text=True, timeout=5).strip()
            report(version, host_check.run_browser(browser, work, timeout=1800), natives)


if __name__ == '__main__':
    main()
