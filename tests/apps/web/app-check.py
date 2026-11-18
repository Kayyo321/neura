"""Run a Neura web application in headless browsers: tests/apps/web/app/app.nra, mounted with Std.Web.mount on
the browser runtime, driven by app-test.mjs. Checks the native refusal first: without a browser page the image
reports that the ui services are not granted. Then bundles the walkthrough's example
(examples/apps/web-counter/app.nra, which docs/apps/WebApp.md shows verbatim) with scripts/build-web-bundle.py
and checks that the bundle mounts it, with its stylesheet, in headless Chromium.

Exits 77 (skipped) when the browser runtime was not built or no browser is given, as the other web checks do."""

import argparse
import html
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / 'tests/wasm'))
from importlib import import_module  # noqa: E402

host_check = import_module('check-host')
sys.path.insert(0, str(HERE))
bundle_check = import_module('bundle-check')

SKIP = 77


def page(work, image):
    """index.html links the image's stylesheets in the order of its .styles.json, as a bundle's page does."""
    links = ''
    listing = Path(f'{image}.styles.json')
    if listing.exists():
        for path in json.loads(listing.read_text())['stylesheets']:
            links += f'<link rel="stylesheet" href="{html.escape(path)}">'
    (work / 'index.html').write_text('<!doctype html><meta charset="utf-8"><title>Neura web app check</title>'
                                     f'{links}<div id="app"></div><script type="module" src="/app-test.mjs"></script>')
    return links.count('<link')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--neura', required=True, type=Path)
    parser.add_argument('--wasm', default=ROOT / 'build/wasm-runtime', type=Path)
    parser.add_argument('--browser', action='append', default=[], help='chromium or firefox executable')
    args = parser.parse_args()
    browsers = [browser for browser in args.browser if shutil.which(browser)]
    if not (args.wasm / 'nvm.wasm').exists() or not browsers or not shutil.which('node'):
        print(f'skipped: needs {args.wasm}/nvm.wasm (the wasm-runtime preset) and a browser', file=sys.stderr)
        return SKIP
    with TemporaryDirectory(prefix='neura-web-app-') as path:
        work = Path(path)
        image = work / 'app.nvr'
        subprocess.run([args.neura, 'build', HERE / 'app/app.nra', '-o', image], cwd=ROOT, check=True, timeout=120)
        native = subprocess.run([args.neura, 'run', image], capture_output=True, text=True, timeout=60)
        if native.returncode == 0 or 'the ui capability is not granted' not in native.stdout:
            raise SystemExit(f'native run: expected a refused mount, got {native.returncode}: {native.stdout}'
                             f'{native.stderr}')
        print(f'native: {native.stdout.strip().splitlines()[-1]}')
        for source in (args.wasm / 'nvm.wasm', args.wasm / 'nvm.mjs', HERE / 'app-test.mjs',
                       *(ROOT / 'hosts/web' / name for name in ('nvm.js', 'dom.mjs', 'ui.mjs', 'ui-wire.mjs'))):
            shutil.copy(source, work / source.name)
        sheets = page(work, image)
        if sheets != 1:
            raise SystemExit(f'expected one linked stylesheet, found {sheets}')
        for browser in browsers:
            version = subprocess.check_output([browser, '--version'], text=True, timeout=10).strip()
            report = host_check.run_browser(browser, work)
            if 'error' in report:
                raise SystemExit(f'{version}: {report["error"]}')
            steps = '; '.join(f'{step["name"]}' for step in report['steps'])
            print(f'{version}: {len(report["steps"])} steps passed ({steps}); {report["commits"]} commits')
        walkthrough(args, work, browsers)
    return 0


def load(browser, site, path, wasm_type):
    """The bundled page's DOM once its boot module settled. bundle-check.dump tries a load three times, since a
    loaded machine can exhaust the virtual-time budget while nvm.wasm compiles; this tries that three times more."""
    for _ in range(3):
        dom = bundle_check.dump(browser, site, path, wasm_type)
        if 'data-neura-state=' in dom:
            break
    return dom


def walkthrough(args, work, browsers):
    example = ROOT / 'examples/apps/web-counter/app.nra'
    document = (ROOT / 'docs/apps/WebApp.md').read_text()
    if f'```neura\n{example.read_text()}```' not in document:
        raise SystemExit('docs/apps/WebApp.md does not show examples/apps/web-counter/app.nra verbatim')
    site = work / 'site'
    bundle = site / 'portfolio'
    subprocess.run([sys.executable, ROOT / 'scripts/build-web-bundle.py', '--source', example, '--neura', args.neura,
                    '--wasm', args.wasm, '--id', 'org.example.counter', '--out', bundle], check=True, timeout=120)
    manifest = json.loads((bundle / 'bundle.json').read_text())
    sheets = [entry['path'] for entry in manifest['files'] if entry['path'].startswith('styles/')]
    if len(sheets) != 1 or not (bundle / sheets[0]).exists():
        raise SystemExit(f'expected one bundled stylesheet, found {sheets}')
    chromium = next((browser for browser in browsers if 'firefox' not in Path(browser).name), None)
    if chromium is None:
        print('bundle: no Chromium to load it')
        return
    dom = load(chromium, site, 'portfolio/', 'application/wasm')
    bundle_check.expect(dom, f'<link rel="stylesheet" href="./{re.escape(sheets[0])}">', 'stylesheet link')
    bundle_check.expect(dom, r'data-neura-state="running"', 'bundle state')
    bundle_check.expect(dom, r'<h1>My first Neura app</h1>', 'heading')
    bundle_check.expect(dom, r'<span class="count" id="count">0</span>', 'counter')
    print(f'bundle: the walkthrough example mounted from /portfolio/ with {sheets[0]} linked')
    # The test application's bundle carries its two assets, fetched when used rather than before the first commit.
    parts = site / 'parts'
    subprocess.run([sys.executable, ROOT / 'scripts/build-web-bundle.py', '--source', HERE / 'app/app.nra', '--neura',
                    args.neura, '--wasm', args.wasm, '--id', 'org.example.parts', '--out', parts], check=True,
                   timeout=120)
    manifest = json.loads((parts / 'bundle.json').read_text())
    assets = [entry for entry in manifest['files'] if entry['path'].startswith('assets/')]
    if len(assets) != 2 or any(entry['initial'] or entry['media_type'] != 'image/svg+xml'
                               or not (parts / entry['path']).exists() for entry in assets):
        raise SystemExit(f'expected two bundled SVG assets, not initial: {assets}')
    dom = load(chromium, site, 'parts/', 'application/wasm')
    bundle_check.expect(dom, r'data-neura-state="running"', 'parts bundle state')
    badge = re.search(r'<img id="badge" src="(assets/[0-9a-f]{64}\.svg)"', dom)
    if not badge or badge.group(1) not in {entry['path'] for entry in assets}:
        raise SystemExit(f'the bundled page does not show a bundled image: {dom[-1500:]}')
    print(f'bundle: the test application mounted from /parts/ with {len(assets)} assets')


if __name__ == '__main__':
    raise SystemExit(main())
