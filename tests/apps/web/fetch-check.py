"""Check neura.http.fetch in headless browsers (docs/runtime/Providers.md, "HTTP fetch").

fetch-app.nra runs from a web bundle built with a second origin as its connect origin. The page's server answers
/api/* on the page's origin; the second server answers /cors/* with CORS headers and /plain/* without them. The
program posts its result lines to /api/report, so the browser's own CORS, content-security policy, cookie and
abort behaviour decide what it sees, with no test hook inside the page."""

import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
from queue import Empty, Queue
import subprocess
import sys
from tempfile import TemporaryDirectory
from threading import Thread
import time
from urllib.parse import parse_qs, urlsplit

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]

EXPECTED = """hello: 200 hello
missing: 404 missing
echo: 200 true 256
cors: 200 greet café yes hidden
no cors: failed OutcomeUnknown
timeout: failed TimedOut
too large: failed TooLarge
scheme: failed Unsupported
method: failed InvalidInput
get body: failed InvalidInput
origin: failed CapabilityDenied
omit: 200 cookie no
include: 200 cookie yes
overlap: 200 slow
overlap: 200 slow
overlap: 200 slow
overlapped: true
cancel: failed Cancelled
done"""

BIG = 9 * 1024 * 1024  # over W-3


class Quiet(SimpleHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def reply(self, status, body=b'', headers=()):
        self.send_response(status)
        for name, value in headers:
            self.send_header(name, value)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass


class Site(Quiet):
    """The page's origin: the bundle and /api/*."""

    def do_GET(self):
        path = urlsplit(self.path).path
        if path == '/api/hello':
            self.reply(200, b'hello', [('Content-Type', 'text/plain'),
                                       ('Set-Cookie', 'neura_test=1; Path=/; SameSite=Lax')])
        elif path == '/api/missing':
            self.reply(404, b'missing', [('Content-Type', 'text/plain')])
        elif path == '/api/config':
            self.reply(200, self.server.api.encode(), [('Content-Type', 'text/plain')])
        else:
            super().do_GET()

    def do_PUT(self):
        body = self.rfile.read(int(self.headers.get('Content-Length', '0')))
        if urlsplit(self.path).path != '/api/echo':
            self.reply(404)
            return
        self.reply(200, body, [('Content-Type', 'application/octet-stream'), ('X-Echo-Length', str(len(body)))])

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get('Content-Length', '0')))
        if urlsplit(self.path).path != '/api/report':
            self.reply(404)
            return
        self.server.reports.put(body.decode())
        self.reply(204)


class Api(Quiet):
    """The second origin: /cors/* allows the page's origin, /plain/* sends no CORS headers."""

    def cors(self):
        return [('Access-Control-Allow-Origin', self.server.site), ('Access-Control-Allow-Credentials', 'true'),
                ('Vary', 'Origin')]

    def do_OPTIONS(self):
        if not self.path.startswith('/cors/'):
            self.reply(404)
            return
        self.reply(204, b'', [*self.cors(), ('Access-Control-Allow-Methods', 'GET, POST, PUT'),
                              ('Access-Control-Allow-Headers', 'X-Neura, Content-Type'),
                              ('Access-Control-Max-Age', '0')])

    def do_GET(self):
        url = urlsplit(self.path)
        if url.path == '/plain/hello':
            self.reply(200, b'plain', [('Content-Type', 'text/plain')])
        elif url.path == '/cors/slow':
            time.sleep(int(parse_qs(url.query)['ms'][0]) / 1000)
            self.reply(200, b'slow', [*self.cors(), ('Content-Type', 'text/plain')])
        elif url.path == '/cors/big':
            self.reply(200, b'x' * BIG, [*self.cors(), ('Content-Type', 'application/octet-stream')])
        elif url.path == '/cors/cookie':
            seen = 'neura_test=1' in self.headers.get('Cookie', '')
            self.reply(200, b'cookie yes' if seen else b'cookie no', [*self.cors(), ('Content-Type', 'text/plain')])
        else:
            self.reply(404, b'', self.cors())

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get('Content-Length', '0')))
        if urlsplit(self.path).path != '/cors/greet':
            self.reply(404, b'', self.cors())
            return
        self.reply(200, b'greet ' + body, [*self.cors(), ('Content-Type', 'text/plain; charset=utf-8'),
                                           ('X-Neura-Seen', self.headers.get('X-Neura', 'none')),
                                           ('X-Secret', 'not exposed'),
                                           ('Access-Control-Expose-Headers', 'X-Neura-Seen')])


def serve(handler, **attributes):
    server = ThreadingHTTPServer(('127.0.0.1', 0), handler)
    server.daemon_threads = True
    for name, value in attributes.items():
        setattr(server, name, value)
    Thread(target=server.serve_forever, daemon=True).start()
    return server


FINISHED = ("['completed', 'failed', 'unsupported'].includes(document.documentElement.dataset.neuraState) && "
            "[document.documentElement.dataset.neuraState, document.getElementById('neura-failure').textContent]")


def launch(browser, address, reports, timeout):
    """Loads the page with load-page.mjs until the run has ended, then takes the program's report."""
    result = subprocess.run(['node', HERE / 'load-page.mjs', browser, address, str(timeout), FINISHED],
                            capture_output=True, text=True, timeout=timeout * 4 + 120)
    if result.returncode != 0:
        raise RuntimeError(f'{browser} did not load the page: {result.stderr.strip()[-500:]}')
    state = json.loads(result.stdout)['value']
    try:
        return reports.get(timeout=10)
    except Empty as error:
        raise RuntimeError(f'{browser}: no report; the page ended as {state!r}') from error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--neura', required=True, type=Path)
    parser.add_argument('--wasm', default=ROOT / 'build/wasm-runtime', type=Path)
    parser.add_argument('--browser', action='append', default=[], help='chromium or firefox executable')
    parser.add_argument('--timeout', type=int, default=120, help='seconds to wait for one browser\'s report')
    args = parser.parse_args()
    with TemporaryDirectory(prefix='neura-fetch-') as path:
        work = Path(path)
        image = work / 'fetch.nvr'
        subprocess.run([args.neura.resolve(), 'build', HERE / 'fetch-app.nra', '-o', image], cwd=ROOT, check=True,
                       timeout=120)
        reports = Queue()
        site_dir = work / 'site'
        site_dir.mkdir()
        site = serve(partial(Site, directory=site_dir), reports=reports)
        api = serve(Api)
        site_origin = f'http://127.0.0.1:{site.server_port}'
        api_origin = f'http://127.0.0.1:{api.server_port}'
        site.api, api.site = api_origin, site_origin
        try:
            subprocess.run([sys.executable, ROOT / 'scripts/build-web-bundle.py', '--image', image, '--wasm',
                            args.wasm, '--id', 'org.neura-lang.fetch-check', '--out', site_dir / 'app',
                            '--connect', api_origin], check=True, timeout=60, stdout=subprocess.DEVNULL)
            failures = 0
            for browser in args.browser:
                version = subprocess.check_output([browser, '--version'], text=True, timeout=10).strip()
                report = launch(browser, f'{site_origin}/app/index.html', reports, args.timeout)
                if report.strip() == EXPECTED:
                    print(f'{version}: {len(EXPECTED.splitlines())} fetch cases matched')
                else:
                    failures += 1
                    print(f'{version}: fetch cases differ\n--- expected\n{EXPECTED}\n--- got\n{report}')
            if failures:
                sys.exit(1)
        finally:
            site.shutdown()
            api.shutdown()


if __name__ == '__main__':
    main()
