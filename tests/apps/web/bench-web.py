"""Measure budgets B-W5, B-W6 and B-W7 and the browser half of B-W9 (docs/design/Apps030.md, "Budgets") in
headless browsers: builds tests/apps/web/bench/rows.nra (each row a component; Reverse moves them) and bench/list.nra
(one Std.Ui.Controls list that renders whole), runs each in the browser runtime with bench-web.mjs, and prints the
click-to-commit median and 95th percentile, the reversal figures and the long tasks, with the machine's load
average; then runs bench/growth.nra, 1,000 mount and unmount cycles of ten clicks, and prints neura.ui.stats, the
open roots, the subscribers and the runtime's memory after cycle 100 and after the last. Writes the reports as JSON
with --out. A measurement, not a test: it fails only when a run fails."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / 'tests/wasm'))
sys.path.insert(0, str(HERE))
from importlib import import_module  # noqa: E402

host_check = import_module('check-host')
app_check = import_module('app-check')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--neura', required=True, type=Path)
    parser.add_argument('--wasm', default=ROOT / 'build/wasm-runtime', type=Path)
    parser.add_argument('--browser', action='append', default=[], help='chromium or firefox executable')
    parser.add_argument('--out', type=Path, help='write the reports here as JSON')
    args = parser.parse_args()
    reports = []
    with TemporaryDirectory(prefix='neura-bench-web-') as path:
        work = Path(path)
        for name in ('rows', 'list', 'growth'):
            directory = work / name
            directory.mkdir()
            image = directory / 'app.nvr'
            subprocess.run([args.neura, 'build', HERE / f'bench/{name}.nra', '-o', image], cwd=ROOT, check=True,
                           timeout=120)
            for copied in (args.wasm / 'nvm.wasm', args.wasm / 'nvm.mjs', HERE / 'bench-web.mjs',
                           *(ROOT / 'hosts/web' / file for file in ('nvm.js', 'dom.mjs', 'ui.mjs', 'ui-wire.mjs'))):
                shutil.copy(copied, directory / copied.name)
            app_check.page(directory, image, 'bench-web.mjs')
            if name == 'growth':
                index = (directory / 'index.html').read_text()
                (directory / 'index.html').write_text(index.replace('<title>', '<meta name="neura-bench" content="growth"><title>'))
            for browser in args.browser:
                version = subprocess.check_output([browser, '--version'], text=True, timeout=10).strip()
                load = os.getloadavg()
                report = host_check.run_browser(browser, directory, timeout=900)
                if 'error' in report:
                    raise SystemExit(f'{name}: {version}: {report["error"]}')
                report.update(workload=name, browser=version, load_before=load, load_after=os.getloadavg(),
                              cpus=os.cpu_count())
                reports.append(report)
                if name == 'growth':
                    first, last = report['growth']
                    keys = ('nodes', 'listeners', 'templates', 'queued', 'openRoots', 'subscribers', 'wasmBytes')
                    same = all(first[key] == last[key] for key in keys)
                    print(f'growth: {version}: after cycle {first["cycle"]} and {last["cycle"]} '
                          f'({last["mounts"]} mounts, {last["cycle"] * 10} clicks): '
                          + '; '.join(f'{key} {first[key]} and {last[key]}' for key in (*keys, 'jsHeap'))
                          + f'; {"no growth" if same else "GROWTH"}; load {load[0]:.1f} to {os.getloadavg()[0]:.1f}',
                          flush=True)
                    continue
                w5, w6, w7 = report['w5'], report['w6'], report['w7']
                line = (f'{name}: {version}: B-W5 click to commit median {w5["median"]:.1f} ms, 95th {w5["p95"]:.1f} ms, '
                        f'max {w5["max"]:.1f} ms ({w5["count"]} clicks)')
                if w6:
                    line += f'; B-W6 reverse 95th {w6["p95"]:.1f} ms, median {w6["median"]:.1f} ms ({w6["count"]})'
                line += (f'; B-W7 {"long tasks over 50 ms: " + str(w7["over50"]) + ", longest " + format(w7["longest"], ".0f") + " ms" if w7["supported"] else "no long-task entries in this browser"}'
                         f'; load {load[0]:.1f} to {os.getloadavg()[0]:.1f} on {os.cpu_count()} CPUs')
                print(line, flush=True)
    if args.out:
        args.out.write_text(json.dumps(reports, indent=1))
    return 0


if __name__ == '__main__':
    sys.exit(main())
