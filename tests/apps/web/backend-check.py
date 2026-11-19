"""Run a Neura front end against a Neura backend in headless browsers (docs/apps/WebApp.md, "Call a Neura backend").

Builds the server image of examples/server/hello-server.nra and serves it with neura-server-host, bundles the front
end of examples/apps/web-backend/app.nra with --http-client, and serves the bundle as a reverse proxy does: the
bundle's files at /, and every /api/ path passed to the server host with the prefix removed, so the page and the
server share one origin. backend-probe.mjs, added to a copy of the bundle, greets twice, sends a message the server
echoes, and calls once more after the proxy has started answering 502, as it does when the server is down.

neura-server-host is found beside --server-neura (by default --neura), as `neura serve` finds it. Exits 77 (skipped)
when the browser runtime, a browser, Node, the server host or the server example is missing."""

import argparse
import http.client
from pathlib import Path
import shutil
import socket
import subprocess
import sys
from tempfile import TemporaryDirectory
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / 'tests/wasm'))
from importlib import import_module  # noqa: E402

host_check = import_module('check-host')

SKIP = 77
EXPECTED = ['hello, Ada', 'hello, Grace', 'ping é ✓', 'Failed: the server answered 502']
# The request headers the proxy passes on, and the response headers it passes back.
REQUEST_HEADERS = ('Content-Type', 'Content-Length', 'Accept')
RESPONSE_HEADERS = ('Content-Type', 'X-Request-Id')


class Proxy(host_check.Handler):
    """The bundle's files, the check's report, and /api/ passed to the backend at server.backend."""

    def forward(self, method):
        if self.server.down:
            self.send_error(502)
            return
        size = int(self.headers.get('Content-Length', '0'))
        body = self.rfile.read(size) if size else None
        target = self.path[len('/api'):]
        connection = http.client.HTTPConnection('127.0.0.1', self.server.backend, timeout=20)
        try:
            connection.request(method, target, body=body,
                               headers={name: self.headers[name] for name in REQUEST_HEADERS if name in self.headers})
            answer = connection.getresponse()
            data = answer.read()
        finally:
            connection.close()
        self.server.forwarded.append(f'{method} {target} {answer.status}')
        self.send_response(answer.status)
        for name in RESPONSE_HEADERS:
            if answer.getheader(name) is not None:
                self.send_header(name, answer.getheader(name))
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path.startswith('/api/'):
            self.forward('GET')
        else:
            super().do_GET()

    def do_POST(self):
        if self.path.startswith('/api/'):
            self.forward('POST')
        elif self.path == f'/backend-down?{self.server.token}':
            self.server.down = True
            self.send_response(204)
            self.end_headers()
        else:
            super().do_POST()


def proxy_for(port, forwarded):
    """The Proxy handler class for a backend on `port`, recording each forwarded request in `forwarded`."""

    class Bound(Proxy):
        def setup(self):
            self.server.backend = port
            self.server.forwarded = forwarded
            if not hasattr(self.server, 'down'):
                self.server.down = False
            super().setup()

    return Bound


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--neura', required=True, type=Path, help='builds the front end')
    parser.add_argument('--server-neura', type=Path, help='builds the server image (default: --neura)')
    parser.add_argument('--server-source', type=Path, default=ROOT / 'examples/server/hello-server.nra')
    parser.add_argument('--wasm', default=ROOT / 'build/wasm-runtime', type=Path)
    parser.add_argument('--browser', action='append', default=[], help='chromium or firefox executable')
    args = parser.parse_args()
    server_neura = (args.server_neura or args.neura).resolve()
    server_host = server_neura.parent / 'neura-server-host'
    browsers = [browser for browser in args.browser if shutil.which(browser)]
    missing = [what for what, present in (
        (f'{args.wasm}/nvm.wasm (the wasm-runtime preset)', (args.wasm / 'nvm.wasm').exists()),
        ('a browser', bool(browsers)), ('node', bool(shutil.which('node'))),
        (f'{server_host}', server_host.exists()), (f'{args.server_source}', args.server_source.exists())) if not present]
    if missing:
        print(f'skipped: needs {", ".join(missing)}', file=sys.stderr)
        return SKIP
    # docs/apps/WebApp.md quotes the command's work; the quoted lines must be the example's.
    walkthrough = (ROOT / 'docs/apps/WebApp.md').read_text()
    quoted = walkthrough.split('let request = http.request(call.method, call.path);', 1)[1].split('match (request', 1)[0]
    if 'let request = http.request(call.method, call.path);' + quoted not in (
            ROOT / 'examples/apps/web-backend/app.nra').read_text().replace('\n    ', '\n'):
        raise SystemExit('docs/apps/WebApp.md does not quote examples/apps/web-backend/app.nra')
    with TemporaryDirectory(prefix='neura-web-backend-') as path:
        work = Path(path)
        image = work / 'hello-server.nvr'
        subprocess.run([server_neura, 'build', args.server_source, '-o', image], check=True, timeout=120)
        bundle = work / 'site'
        subprocess.run([sys.executable, ROOT / 'scripts/build-web-bundle.py', '--source',
                        ROOT / 'examples/apps/web-backend/app.nra', '--neura', args.neura, '--wasm', args.wasm,
                        '--id', 'org.example.backend', '--title', 'Ask the server', '--http-client', '--out', bundle],
                       check=True, timeout=120, stdout=subprocess.DEVNULL)
        shutil.copy(HERE / 'backend-probe.mjs', bundle / 'backend-probe.mjs')
        index = (bundle / 'index.html').read_text()
        (bundle / 'index.html').write_text(
            index.replace('</head>', '<script type="module" src="./backend-probe.mjs"></script>\n</head>'))
        # The host's standard output is a pipe here, which it may buffer, so readiness is a port that accepts.
        with socket.socket() as probe:
            probe.bind(('127.0.0.1', 0))
            port = probe.getsockname()[1]
        server = subprocess.Popen([server_host, '--listen', f'127.0.0.1:{port}', '--config', 'greeting=hello', image],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            deadline = time.monotonic() + 60
            while True:
                if server.poll() is not None:
                    raise SystemExit(f'{server_host} exited with status {server.returncode}')
                try:
                    socket.create_connection(('127.0.0.1', port), timeout=1).close()
                    break
                except OSError:
                    if time.monotonic() > deadline:
                        raise SystemExit(f'{server_host} did not listen on {port}')
                    time.sleep(0.1)
            for browser in browsers:
                version = subprocess.check_output([browser, '--version'], text=True, timeout=10).strip()
                forwarded = []
                report = host_check.run_browser(browser, bundle, timeout=120, handler=proxy_for(port, forwarded))
                if 'error' in report:
                    raise SystemExit(f'{version}: {report["error"]}')
                if report != {'state': 'running', 'steps': EXPECTED}:
                    raise SystemExit(f'{version}: unexpected report {report}')
                if forwarded != ['GET /hello?name=Ada 200', 'GET /hello?name=Grace 200', 'POST /echo 200']:
                    raise SystemExit(f'{version}: the server saw {forwarded}')
                print(f'{version}: the front end called the Neura server through /api/: {"; ".join(forwarded)}; '
                      f'the status line showed {"; ".join(report["steps"])}')
        finally:
            server.terminate()
            try:
                server.wait(timeout=10)
            except subprocess.TimeoutExpired:
                server.kill()
    return 0


if __name__ == '__main__':
    sys.exit(main())
