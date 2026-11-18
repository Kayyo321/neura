"""Check the browser runtime's host services: the commit and event encodings, waits and timers, and a counter
image that mounts, handles injected clicks and re-renders, under Node and in headless browsers."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / 'tests/wasm'))
from importlib import import_module  # noqa: E402

host_check = import_module('check-host')

COUNTER = """template: ok
unknown encoding: error 4
mount: ok
unknown commit encoding: error 4
label 1: ok
label 2: ok
label 3: ok
nodes 3, listeners 1, templates 1, queued 0
pushed: /parts/7
current: /parts/7
invalid path: error 4
close: ok
closed root: error 4
"""

# The browser grants console, clocks and entropy; files, sockets and the environment answer error 14.
BROWSER_CAPABILITIES = ('--deny-fs', '--deny-net', '--deny-env')


def native(neura, image):
    result = subprocess.run([neura, 'run', '--tier=0', *BROWSER_CAPABILITIES, image], capture_output=True,
                            text=True, timeout=60)
    return dict(kind='completed' if result.returncode == 0 else 'trapped', status=result.returncode,
                stdout=result.stdout)


def trusted_tree(neura, work):
    """A copy of the SDK layout whose library also holds the counter probe: only library code may call host
    services. The executable finds its library next to its real path, so it is copied, not linked."""
    prefix = work / 'sdk'
    (prefix / 'bin').mkdir(parents=True)
    # `neura` starts its sibling tools, which load lib/neura beside them.
    source = Path(neura).resolve().parent
    for tool in [Path(neura).resolve(), *sorted(source.glob('neura-*')), source / 'neurac']:
        if tool.is_file():
            shutil.copy2(tool, prefix / 'bin' / tool.name)
    if (source / 'lib/neura').is_dir():
        shutil.copytree(source / 'lib/neura', prefix / 'bin/lib/neura')
    shutil.copytree(ROOT / 'stdlib', prefix / 'stdlib')
    shutil.copytree(HERE / 'probe', prefix / 'stdlib/std/probe')
    return prefix / 'bin/neura'


def make_fixtures(neura, wasm, work):
    for source in (wasm / 'nvm.wasm', wasm / 'nvm.mjs', HERE / 'web-test.mjs', ROOT / 'tests/apps/ui-ops/batches.json',
                   *(ROOT / 'hosts/web' / name for name in ('nvm.js', 'dom.mjs', 'ui.mjs', 'ui-wire.mjs'))):
        shutil.copy(source, work / source.name)
    (work / 'package.json').write_text('{"type":"module"}\n')
    (work / 'index.html').write_text('<!doctype html><meta charset="utf-8"><title>Neura web host checks</title>'
                                     '<script type="module" src="/web-test.mjs"></script>')
    oracles = {}
    for name, source in (('timers', ROOT / 'tests/wasm/timers.nra'), ('serialSleep', ROOT / 'tests/wasm/serial-sleep.nra'),
                         ('print', ROOT / 'tests/wasm/print.nra')):
        image = work / f'{name}.nvr'
        subprocess.run([neura, 'build', source, '-o', image], cwd=ROOT, check=True, timeout=60)
        oracles[name] = native(neura, image)
    trusted = trusted_tree(neura, work)
    subprocess.run([trusted, 'build', HERE / 'counter-app.nra', '-o', work / 'counter.nvr'], cwd=ROOT, check=True,
                   timeout=60)
    shutil.rmtree(work / 'sdk')
    oracles['counter'] = COUNTER
    (work / 'oracles.json').write_text(json.dumps(oracles))
    return oracles


def summary(label, report):
    if 'error' in report:
        raise ValueError(f'{label}: {report["error"]}')
    waits = ', '.join(f'{w["label"]} {w["slices"]} slices {w["milliseconds"]:.0f} ms' for w in report['waits'])
    line = f'{label}: {report["wire"]["commits"]} corpus commits round-tripped; waits matched native ({waits})'
    if 'counter' in report:
        counter = report['counter']
        line += (f'; {report["corpus"]} corpus commits applied through neura.ui.commit; counter matched '
                 f'in {counter["slices"]} slices')
    print(line)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--neura', required=True, type=Path)
    parser.add_argument('--wasm', default=ROOT / 'build/wasm-runtime', type=Path)
    parser.add_argument('--browser', action='append', default=[], help='chromium or firefox executable')
    args = parser.parse_args()
    with TemporaryDirectory(prefix='neura-web-host-') as path:
        work = Path(path)
        make_fixtures(args.neura.resolve(), args.wasm.resolve(), work)
        node = subprocess.run(['node', work / 'web-test.mjs'], capture_output=True, text=True, timeout=60)
        if node.returncode != 0:
            raise ValueError(f'Node: {node.stderr}')
        summary('Node', json.loads(node.stdout))
        for browser in args.browser:
            version = subprocess.check_output([browser, '--version'], text=True, timeout=5).strip()
            summary(version, host_check.run_browser(browser, work))


if __name__ == '__main__':
    main()
