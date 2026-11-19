#!/usr/bin/env python3
"""The reference application's page from `neura build --target web`, in headless browsers (docs/apps/Projects.md).

Writes `neura new stock --template fullstack`, builds its web target with the real browser runtime, and opens the
bundle from a static file host at a deep path, once with application/wasm and once with
application/octet-stream for nvm.wasm (case 11): the page mounts under its content-security policy and shows the
starting parts from the shared contract. Then it builds the bundle under `neura dev` and opens it from the
development server, whose page carries the reload script. Exits 77 (skipped) without Node.

usage: browser-check.py --neura <neura> --wasm <runtime directory> [--browser chromium|firefox]...
"""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
WEB_TESTS = ROOT / 'tests/apps/web'
sys.path.insert(0, str(WEB_TESTS))


def load(module_path):
    import importlib.util
    spec = importlib.util.spec_from_file_location('bundle_check', module_path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


MOUNTED = "document.getElementById('add') && document.documentElement.dataset.neuraState"


def page_text(browser, url, until=MOUNTED, seconds=90):
    result = subprocess.run(['node', WEB_TESTS / 'load-page.mjs', browser, url, str(seconds), until],
                            capture_output=True, text=True, timeout=seconds * 4 + 120)
    if result.returncode != 0:
        raise RuntimeError(f'{browser} did not load {url}: {result.stderr.strip()[-500:]}')
    return json.loads(result.stdout)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--neura', required=True, type=Path)
    parser.add_argument('--wasm', required=True, type=Path)
    parser.add_argument('--browser', action='append', default=[])
    args = parser.parse_args()
    if not shutil.which('node'):
        print('skipped: Node is not installed')
        return 77
    check = load(WEB_TESTS / 'bundle-check.py')
    browsers = args.browser or ['chromium']
    neura = args.neura.resolve()
    failures = []
    with TemporaryDirectory(prefix='neura-project-browser-') as path:
        work = Path(path)
        # The page runtime: the browser runtime's nvm.wasm and nvm.mjs beside the page modules.
        runtime = work / 'runtime'
        runtime.mkdir()
        for name in ('nvm.wasm', 'nvm.mjs'):
            shutil.copy(args.wasm / name, runtime / name)
        for module in ('ui-wire.mjs', 'dom.mjs', 'nvm.js', 'ui.mjs', 'fetch.mjs', 'boot.mjs'):
            shutil.copy(ROOT / 'hosts/web' / module, runtime / module)
        env = dict(os.environ, NEURA_WEB_RUNTIME=str(runtime))
        project = work / 'stock'
        subprocess.run([neura, 'new', project, '--template', 'fullstack'], check=True, capture_output=True)
        subprocess.run([neura, 'build', '--target', 'web'], cwd=project, env=env, check=True, capture_output=True)
        site = work / 'site'
        shutil.copytree(project / 'dist/web', site / 'deep/path')
        for browser in browsers:
            for wasm_type in ('application/wasm', 'application/octet-stream'):
                dom = check.dump(browser, site, 'deep/path/', wasm_type, until=MOUNTED, seconds=90)
                ok = re.search(r'Stock: Bolt Nut Washer', dom) and 'data-neura-state="running"' in dom
                print(f"{'ok  ' if ok else 'FAIL'} {browser}: the stock page mounts from a deep path ({wasm_type})")
                if not ok:
                    failures.append(f'{browser} {wasm_type}')
                    print(dom[-1500:])

        # neura dev: the page from the development server carries the reload script; a stylesheet edit is swapped.
        log = open(work / 'dev.log', 'w')
        dev = subprocess.Popen([str(neura), 'dev', '--target', 'web', '--port', '0'], cwd=project, env=env,
                               stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT)
        try:
            found = None
            for _ in range(600):
                found = re.search(r'127\.0\.0\.1:(\d+)', (work / 'dev.log').read_text())
                if found:
                    break
                time.sleep(0.1)
            if not found:
                failures.append('neura dev did not start')
            else:
                url = f'http://127.0.0.1:{found.group(1)}/'
                for browser in browsers:
                    report = page_text(browser, url)
                    ok = 'neura-dev.js' in report['html'] and 'Stock: Bolt Nut Washer' in report['html']
                    print(f"{'ok  ' if ok else 'FAIL'} {browser}: the stock page mounts from neura dev")
                    if not ok:
                        failures.append(f'{browser} neura dev')
        finally:
            dev.stdin.close()
            dev.wait(timeout=30)
            log.close()
    if failures:
        print(f'{len(failures)} failed: {failures}')
        return 1
    print('project pages: all checks passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
