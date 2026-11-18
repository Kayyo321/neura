"""Compare the production wasm adapter with the native reference runner."""

import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
from queue import Empty, Queue
import re
import secrets
import shutil
import signal
import subprocess
from tempfile import TemporaryDirectory
from threading import Thread


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def native_oracle(neura, image, *, options=(), arguments=(), requests=None):
    command = [neura, 'run', '--tier=0', '--stats', *options, image]
    if arguments:
        command.extend(['--', *arguments])
    result = subprocess.run(command, capture_output=True, text=True, timeout=30)
    counts = re.search(r'stats: steps (\d+), peak heap cells (\d+)(?:, peak stack cells (\d+))?, host requests (\d+)',
                       result.stderr)
    if not counts:
        raise ValueError(result.stderr)
    steps, heap, stack, calls = counts.groups()
    trap = re.search(r'^.*: trap: (.+)$', result.stderr, re.MULTILINE)
    if requests is None:
        lines = result.stdout.splitlines(keepends=True)
        requests = [f'{index}:{line}' for index, line in enumerate(lines)]
    if len(requests) != int(calls):
        raise ValueError(f'{image} reported {calls} requests for {requests!r}')
    return dict(kind='trapped' if trap else 'completed', status=result.returncode, steps=steps,
                heap=heap, stack=stack or '0', diagnostic=trap.group(1) if trap else '', requests=''.join(requests))


def make_fixtures(neura, wasm, work):
    for source in (wasm / 'nvm.wasm', wasm / 'nvm.mjs', ROOT / 'hosts/web/nvm.js', HERE / 'host.mjs'):
        shutil.copy(source, work / source.name)
    (work / 'package.json').write_text('{"type":"module"}\n')
    (work / 'index.html').write_text('<!doctype html><meta charset="utf-8"><script type="module" src="/host.mjs"></script>')
    sources = {
        'hello': ROOT / 'examples/hello-world/hello-world.nra',
        'overflow': ROOT / 'tests/backend/traps/overflow.nra',
        'clock': HERE / 'clock.nra',
        'regions': ROOT / 'tests/backend/programs/array-regions.nra',
        'sha256': ROOT / 'tests/benchmarks/sha256.nra',
        'sorting': ROOT / 'tests/benchmarks/sort.nra',
        'kernelState': ROOT / 'tests/stdlib/sha256/traps/update-after-finish.nra',
    }
    oracles = {}
    for name, source in sources.items():
        image = work / f'{name}.nvr'
        subprocess.run([neura, 'build', source, '-o', image], cwd=ROOT, check=True, timeout=30)
        if name == 'clock':
            oracles[name] = native_oracle(
                neura, image, options=('--replay', HERE / 'clock.trace'), arguments=('héllo',),
                requests=['0:héllo\n', '1:wall', '2:2026-11-09T19:30:15.250Z\n',
                          '3:monotonic', '4:monotonic', '5:PT0.000000001S\n'])
        elif name == 'sha256' or name == 'sorting':
            oracles[name] = native_oracle(neura, image, arguments=('6000' if name == 'sha256' else '600',))
        else:
            oracles[name] = native_oracle(neura, image)
    oracles['stepLimit'] = native_oracle(neura, work / 'regions.nvr', options=('--max-steps', '11'))
    (work / 'oracles.json').write_text(json.dumps(oracles))


class Handler(SimpleHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        # The server rule of history routing: an application path that names no file is answered with index.html.
        if getattr(self.server, 'fallback', False) and not Path(self.translate_path(self.path)).exists():
            self.path = '/index.html'
        super().do_GET()

    def do_POST(self):
        size = int(self.headers.get('Content-Length', '0'))
        if self.path != f'/result?{self.server.token}' or not 0 < size < 4 * 1024 * 1024:
            self.send_error(400)
            return
        self.server.reports.put(json.loads(self.rfile.read(size)))
        self.send_response(204)
        self.end_headers()


def run_browser(browser, work, timeout=45, path='/', fallback=False):
    """Serves `work` and opens `path` in `browser` until the page posts its report. With `fallback`, a path that
    names no file is answered with index.html, as a host serving a history-routed application must."""
    with ThreadingHTTPServer(('127.0.0.1', 0), partial(Handler, directory=work)) as server:
        server.fallback = fallback
        server.reports = Queue()
        server.token = secrets.token_hex(20)
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            # Keep browser profiles and logs outside the served directory.
            with TemporaryDirectory(prefix='neura-wasm-browser-') as tmp:
                profile = Path(tmp) / 'profile'
                profile.mkdir()
                log = Path(tmp) / 'browser.log'
                if 'firefox' in Path(browser).name:
                    flags = ['--headless', '--no-remote', '--profile', str(profile)]
                else:
                    flags = ['--headless', '--disable-gpu', '--disable-background-networking', '--disable-sync',
                             '--no-first-run', f'--user-data-dir={profile}']
                address = f'http://127.0.0.1:{server.server_port}{path}?{server.token}'
                with log.open('wb') as output:
                    process = subprocess.Popen([browser, *flags, address], start_new_session=True,
                                               stdout=output, stderr=subprocess.STDOUT)
                    try:
                        report = server.reports.get(timeout=timeout)
                    except Empty as error:
                        raise RuntimeError(log.read_text(errors='replace')[-4096:]) from error
                    finally:
                        for sig in (signal.SIGTERM, signal.SIGKILL):
                            try:
                                os.killpg(process.pid, sig)
                            except ProcessLookupError:
                                pass
                            try:
                                process.wait(timeout=3)
                                break
                            except subprocess.TimeoutExpired:
                                continue
                return report
        finally:
            server.shutdown()
            thread.join()


def print_report(label, report):
    if 'error' in report:
        raise ValueError(f'{label}: {report["error"]}')
    cases = report['outcomes']
    longest = max(cases, key=lambda outcome: outcome['maxProofMs'])
    proof_ms = longest['maxProofMs']
    print(f'{label}: {len(cases)} oracle comparisons; {report["yields"]} driven yields; '
          f'{report["denied"]} busy idle callbacks skipped; longest explicit idle proof {proof_ms:.3f} ms '
          f'({longest["name"]}, tier {longest["tier"]}, slice {longest["budget"]})')
    if report['browserPauses']:
        print(f'  {len(report["browserPauses"])} requestIdleCallback proofs, '
              f'longest {max(report["browserPauses"]):.3f} ms')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--neura', required=True, type=Path)
    parser.add_argument('--wasm', default=ROOT / 'build/wasm-runtime', type=Path)
    parser.add_argument('--browser', action='append', default=[], help='chromium or firefox executable')
    args = parser.parse_args()
    with TemporaryDirectory(prefix='neura-wasm-host-') as path:
        work = Path(path)
        make_fixtures(args.neura.resolve(), args.wasm.resolve(), work)
        report = subprocess.check_output(['node', work / 'host.mjs'], text=True, timeout=45)
        print_report('Node', json.loads(report))
        for browser in args.browser:
            version = subprocess.check_output([browser, '--version'], text=True, timeout=5).strip()
            print_report(version, run_browser(browser, work))


if __name__ == '__main__':
    main()
