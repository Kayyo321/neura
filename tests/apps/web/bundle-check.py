"""Check a web bundle from a static file host in headless Chromium: the counter image mounts under the bundle's
content-security policy, from a deep path, with application/wasm and with a wrong media type for nvm.wasm, and
a bundle with a changed image refuses to start."""

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
    def __init__(self, *args, wasm_type, **kwargs):
        self.wasm_type = wasm_type
        super().__init__(*args, **kwargs)

    def guess_type(self, path):
        if str(path).endswith('.wasm'):
            return self.wasm_type
        return super().guess_type(path)

    def log_message(self, *_):
        pass


def dump(browser, root, path, wasm_type):
    """The DOM once the boot module has settled. A loaded machine can exhaust the virtual-time budget while
    nvm.wasm compiles, which leaves no state on the page; that load is tried again, at most three times."""
    for _ in range(3):
        dom = dump_once(browser, root, path, wasm_type)
        if 'data-neura-state=' in dom:
            return dom
    return dom


def dump_once(browser, root, path, wasm_type):
    with ThreadingHTTPServer(('127.0.0.1', 0), partial(Handler, directory=root, wasm_type=wasm_type)) as server:
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            with TemporaryDirectory(prefix='neura-bundle-profile-') as profile:
                result = subprocess.run(
                    [browser, '--headless', '--disable-gpu', '--no-first-run', f'--user-data-dir={profile}',
                     '--virtual-time-budget=15000', '--dump-dom',
                     f'http://127.0.0.1:{server.server_port}/{path}index.html'],
                    capture_output=True, text=True, timeout=120)
            if not result.stdout.strip():
                print(f'(no DOM from {browser}: status {result.returncode}; {result.stderr.strip()[-300:]})')
            return result.stdout
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
    parser.add_argument('--browser', default='chromium')
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
        listed = {entry['path'] for entry in manifest['files']} | {'bundle.json'}
        present = {p.name for p in bundle.iterdir()}
        if listed != present:
            raise AssertionError(f'bundle files {sorted(present)} differ from the table {sorted(listed)}')
        version = subprocess.check_output([args.browser, '--version'], text=True, timeout=5).strip()
        for wasm_type in ('application/wasm', 'application/octet-stream'):
            dom = dump(args.browser, site, 'deep/path/', wasm_type)
            expect(dom, r'data-neura-state="running"', wasm_type)
            expect(dom, r'<button id="counter">Clicked 0</button>', wasm_type)
            print(f'{version}: bundle mounted from /deep/path/ with nvm.wasm as {wasm_type}')
        tampered = work / 'tampered'
        shutil.copytree(bundle, tampered)
        image_file = tampered / manifest['image']['path']
        data = bytearray(image_file.read_bytes())
        data[-1] ^= 1
        image_file.write_bytes(bytes(data))
        dom = dump(args.browser, tampered, '', 'application/wasm')
        expect(dom, r'data-neura-state="failed"', 'tampered image')
        if 'id="counter"' in dom:
            raise AssertionError('a tampered image mounted')
        print(f'{version}: a changed image was refused before it ran')


if __name__ == '__main__':
    main()
