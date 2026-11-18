"""Run a Neura web application in headless browsers: tests/apps/web/app/app.nra, mounted with Std.Web.mount on
the browser runtime, driven by app-test.mjs, and the portfolio of tests/apps/web/portfolio/portfolio.nra (router,
deep link, contact form with validation over Std.Ui.Controls), driven by portfolio-test.mjs. Each image's native run
must first report that the ui services are not granted. Then bundles the walkthrough's example
(examples/apps/web-counter/app.nra, which docs/apps/WebApp.md shows verbatim) and the test application with
scripts/build-web-bundle.py, and loads both bundles in each browser with bundle-probe.mjs added to the page.

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

SKIP = 77


def page(work, image, module):
    """index.html links the image's stylesheets in the order of its .styles.json, as a bundle's page does."""
    links = ''
    listing = Path(f'{image}.styles.json')
    if listing.exists():
        for path in json.loads(listing.read_text())['stylesheets']:
            links += f'<link rel="stylesheet" href="{html.escape(path)}">'
    (work / 'index.html').write_text('<!doctype html><meta charset="utf-8"><title>Neura web app check</title>'
                                     f'{links}<div id="app"></div><script type="module" src="/{module}"></script>')
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
        journeys = (('app', HERE / 'app/app.nra', 'app-test.mjs', 1),
                    ('portfolio', HERE / 'portfolio/portfolio.nra', 'portfolio-test.mjs', 0))
        for name, source, module, linked in journeys:
            directory = work / name
            directory.mkdir()
            image = directory / 'app.nvr'
            subprocess.run([args.neura, 'build', source, '-o', image], cwd=ROOT, check=True, timeout=120)
            native = subprocess.run([args.neura, 'run', image], capture_output=True, text=True, timeout=60)
            if native.returncode == 0 or 'the ui capability is not granted' not in native.stdout:
                raise SystemExit(f'{name}: native run: expected a refused mount, got {native.returncode}: '
                                 f'{native.stdout}{native.stderr}')
            print(f'{name}: native: {native.stdout.strip().splitlines()[-1]}')
            for copied in (args.wasm / 'nvm.wasm', args.wasm / 'nvm.mjs', HERE / module,
                           *(ROOT / 'hosts/web' / file for file in ('nvm.js', 'dom.mjs', 'ui.mjs', 'ui-wire.mjs'))):
                shutil.copy(copied, directory / copied.name)
            sheets = page(directory, image, module)
            if sheets != linked:
                raise SystemExit(f'{name}: expected {linked} linked stylesheets, found {sheets}')
            for browser in browsers:
                version = subprocess.check_output([browser, '--version'], text=True, timeout=10).strip()
                report = host_check.run_browser(browser, directory)
                if 'error' in report:
                    raise SystemExit(f'{name}: {version}: {report["error"]}')
                steps = '; '.join(f'{step["name"]}' for step in report['steps'])
                print(f'{name}: {version}: {len(report["steps"])} steps passed ({steps})')
        walkthrough(args, work, browsers)
    return 0


def probe(bundle, work, browser):
    """Loads a copy of `bundle` with bundle-probe.mjs added to its page and returns the probe's report."""
    copy = work / f'probe-{bundle.name}'
    shutil.copytree(bundle, copy)
    shutil.copy(HERE / 'bundle-probe.mjs', copy / 'bundle-probe.mjs')
    index = (copy / 'index.html').read_text()
    (copy / 'index.html').write_text(index.replace('</head>', '<script type="module" src="./bundle-probe.mjs"></script>\n</head>'))
    report = host_check.run_browser(browser, copy)
    shutil.rmtree(copy)
    if 'error' in report:
        raise SystemExit(f'bundle {bundle.name}: {report["error"]}')
    return report


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
    parts = site / 'parts'
    subprocess.run([sys.executable, ROOT / 'scripts/build-web-bundle.py', '--source', HERE / 'app/app.nra', '--neura',
                    args.neura, '--wasm', args.wasm, '--id', 'org.example.parts', '--out', parts], check=True,
                   timeout=120)
    manifest = json.loads((parts / 'bundle.json').read_text())
    assets = [entry for entry in manifest['files'] if entry['path'].startswith('assets/')]
    if len(assets) != 2 or any(entry['initial'] or entry['media_type'] != 'image/svg+xml'
                               or not (parts / entry['path']).exists() for entry in assets):
        raise SystemExit(f'expected two bundled SVG assets, not initial: {assets}')
    for browser in browsers:
        version = subprocess.check_output([browser, '--version'], text=True, timeout=10).strip()
        report = probe(bundle, work, browser)
        if report != {'state': 'running', 'sheets': 1, 'color': 'rgb(42, 93, 176)', 'clicked': '1'}:
            raise SystemExit(f'bundle: unexpected walkthrough report {report}')
        report = probe(parts, work, browser)
        if report.get('state') != 'running' or report.get('image') not in {entry['path'] for entry in assets}:
            raise SystemExit(f'bundle: unexpected test application report {report}')
        print(f'bundle: {version}: the walkthrough mounted with {sheets[0]} applied and counted a click; the test '
              f'application showed its bundled image {report["image"]}')


if __name__ == '__main__':
    raise SystemExit(main())
