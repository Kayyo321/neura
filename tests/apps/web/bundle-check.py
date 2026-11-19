"""Check a web bundle from a static file host in headless Chromium: the counter image mounts under the bundle's
content-security policy, from a deep path, with application/wasm and with a wrong media type for nvm.wasm; a
bundle with a changed image, or a changed module that another module imports, refuses to start; a bundle built
for history routing under /app/ mounts when opened at the application path /app/parts/7, and refuses to start
there when an imported module changed (its import map's integrity keys resolve from the base); and an image that
imports a service a web target cannot have is refused when the bundle is built."""

import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from importlib import import_module
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory
from threading import Thread

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE))
service_check = import_module('check')


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, wasm_type, fallback=None, **kwargs):
        self.wasm_type = wasm_type
        self.fallback = fallback
        super().__init__(*args, **kwargs)

    def do_GET(self):
        # The server rule of history routing: a path that names no file is answered with the bundle's index.html.
        if self.fallback and not Path(self.translate_path(self.path)).exists():
            self.path = self.fallback
        super().do_GET()

    def guess_type(self, path):
        if str(path).endswith('.wasm'):
            return self.wasm_type
        return super().guess_type(path)

    def log_message(self, *_):
        pass


COUNTER_UP = "document.getElementById('counter') && document.documentElement.dataset.neuraState"
STATE = 'document.documentElement.dataset.neuraState'


def dump(browser, root, path, wasm_type, until=COUNTER_UP, seconds=60, page='index.html', fallback=None):
    """The DOM once `until` holds in the page, or at the deadline, from load-page.mjs: real time with a deadline
    rather than a virtual-time budget, so a loaded machine slows the check instead of failing it. With `fallback`,
    a path that names no file is answered with that file, as the host of a history-routed bundle must."""
    with ThreadingHTTPServer(('127.0.0.1', 0), partial(Handler, directory=root, wasm_type=wasm_type,
                                                       fallback=fallback)) as server:
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            result = subprocess.run(['node', HERE / 'load-page.mjs', browser,
                                     f'http://127.0.0.1:{server.server_port}/{path}{page}', str(seconds), until],
                                    capture_output=True, text=True, timeout=seconds * 4 + 120)
            if result.returncode != 0:
                raise RuntimeError(f'{browser} did not load the page: {result.stderr.strip()[-500:]}')
            return json.loads(result.stdout)['html']
        finally:
            server.shutdown()
            thread.join()


def expect(dom, pattern, label):
    if not re.search(pattern, dom):
        raise AssertionError(f'{label}: {pattern!r} not found in\n{dom[-2000:]}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--neura', required=True, type=Path)
    parser.add_argument('--wasm', default=ROOT / 'build/wasm-runtime', type=Path)
    parser.add_argument('--browser', action='append', default=[], help='chromium or firefox; chromium by default')
    args = parser.parse_args()
    with TemporaryDirectory(prefix='neura-web-bundle-') as path:
        work = Path(path)
        trusted = service_check.trusted_tree(args.neura.resolve(), work)
        image = work / 'counter.nvr'
        subprocess.run([trusted, 'build', HERE / 'counter-app.nra', '-o', image], cwd=ROOT, check=True, timeout=60)
        site = work / 'site'
        bundle = site / 'deep/path'
        subprocess.run([sys.executable, ROOT / 'scripts/build-web-bundle.py', '--image', image, '--wasm', args.wasm,
                        '--id', 'org.neura-lang.counter', '--out', bundle], check=True, timeout=60)
        manifest = json.loads((bundle / 'bundle.json').read_text())
        routed_site = work / 'routed'
        routed = routed_site / 'app'
        subprocess.run([sys.executable, ROOT / 'scripts/build-web-bundle.py', '--image', image, '--wasm', args.wasm,
                        '--id', 'org.neura-lang.counter', '--routing', 'history', '--base', '/app/', '--out', routed],
                       check=True, timeout=60, stdout=subprocess.DEVNULL)
        routed_changed = work / 'routed-changed'
        shutil.copytree(routed_site, routed_changed)
        ui_module = next((routed_changed / 'app').glob('ui.*.mjs'))
        ui_module.write_text(ui_module.read_text() + '\n// changed\n')
        listed = {entry['path'] for entry in manifest['files']} | {'bundle.json'}
        present = {p.name for p in bundle.iterdir()}
        if listed != present:
            raise AssertionError(f'bundle files {sorted(present)} differ from the table {sorted(listed)}')
        for browser in args.browser or ['chromium']:
            version = subprocess.check_output([browser, '--version'], text=True, timeout=10).strip()
            for wasm_type in ('application/wasm', 'application/octet-stream'):
                dom = dump(browser, site, 'deep/path/', wasm_type)
                expect(dom, r'data-neura-state="running"', wasm_type)
                expect(dom, r'<button id="counter">Clicked 0</button>', wasm_type)
                print(f'{version}: bundle mounted from /deep/path/ with nvm.wasm as {wasm_type}')
            tampered = work / 'tampered'
            if not tampered.exists():
                shutil.copytree(bundle, tampered)
                image_file = tampered / manifest['image']['path']
                data = bytearray(image_file.read_bytes())
                data[-1] ^= 1
                image_file.write_bytes(bytes(data))
            dom = dump(browser, tampered, '', 'application/wasm', until=STATE)
            expect(dom, r'data-neura-state="failed"', 'tampered image')
            if 'id="counter"' in dom:
                raise AssertionError('a tampered image mounted')
            print(f'{version}: a changed image was refused before it ran')
            changed = work / 'changed-module'
            if not changed.exists():
                shutil.copytree(bundle, changed)
                ui_module = next(changed.glob('ui.*.mjs'))
                ui_module.write_text(ui_module.read_text() + '\n// changed\n')
            # Nothing runs, so nothing sets a state: the wait is a short deadline.
            dom = dump(browser, changed, '', 'application/wasm', until=STATE, seconds=10)
            if 'data-neura-state=' in dom or 'id="counter"' in dom:
                raise AssertionError('a bundle whose ui module changed started')
            print(f'{version}: a changed imported module was refused before it ran')
            dom = dump(browser, routed_site, 'app/parts/', 'application/wasm', page='7', fallback='/app/index.html')
            expect(dom, r'data-neura-state="running"', 'history routing')
            expect(dom, r'<button id="counter">Clicked 0</button>', 'history routing')
            dom = dump(browser, routed_changed, 'app/parts/', 'application/wasm', until=STATE, seconds=10, page='7',
                       fallback='/app/index.html')
            if 'data-neura-state=' in dom or 'id="counter"' in dom:
                raise AssertionError('a history-routed bundle whose ui module changed started at a deep path')
            print(f'{version}: a history-routed bundle under /app/ mounted at /app/parts/7, and refused to start there '
                  'when an imported module changed')
        env_image = work / 'env.nvr'
        subprocess.run([trusted, 'build', HERE / 'env-app.nra', '-o', env_image], cwd=ROOT, check=True, timeout=60)
        refused = subprocess.run([sys.executable, ROOT / 'scripts/build-web-bundle.py', '--image', env_image,
                                  '--wasm', args.wasm, '--id', 'org.neura-lang.env', '--out', work / 'env'],
                                 capture_output=True, text=True, timeout=60)
        if refused.returncode == 0 or 'neura.env.get/1' not in refused.stderr:
            raise AssertionError(f'an image importing neura.env.get was bundled: {refused.stderr}')
        print('an image importing neura.env.get was refused at bundle time')


if __name__ == '__main__':
    main()
