"""Run the decoded UI operation corpus in real headless Chromium and Firefox."""

import argparse
from functools import partial
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
from queue import Empty, Queue
import secrets
import shutil
import signal
import subprocess
from tempfile import TemporaryDirectory
from threading import Thread
from urllib.parse import parse_qs, urlsplit


HERE = Path(__file__).resolve().parent
ADAPTER = HERE.parents[2] / "hosts/web/dom.mjs"
PAGE = b"""<!doctype html>
<html lang="en">
<meta charset="utf-8">
<title>Neura DOM adapter tests</title>
<script type="module" src="/dom-test.mjs"></script>
</html>
"""


class Handler(BaseHTTPRequestHandler):
    def __init__(self, *args, results, token, **kwargs):
        self.results = results
        self.token = token
        super().__init__(*args, **kwargs)

    def log_message(self, *_):
        pass

    def do_GET(self):
        path = urlsplit(self.path).path
        files = {
            "/dom.mjs": ("text/javascript", ADAPTER),
            "/dom-test.mjs": ("text/javascript", HERE / "dom-test.mjs"),
            "/batches.json": ("application/json", HERE / "batches.json"),
        }
        if path == "/":
            content_type, content = "text/html; charset=utf-8", PAGE
        elif path == "/favicon.ico":
            self.send_response(204)
            self.end_headers()
            return
        elif path in files:
            content_type, filename = files[path]
            content = filename.read_bytes()
        else:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(content)))
        self.send_header("Content-Security-Policy", "default-src 'self'; object-src 'none'; base-uri 'none'")
        self.end_headers()
        self.wfile.write(content)

    def do_POST(self):
        route = urlsplit(self.path)
        size = int(self.headers.get("Content-Length", "0"))
        if route.path != "/result" or parse_qs(route.query).get("token") != [self.token] or not 0 < size <= 65536:
            self.send_error(400)
            return
        report = json.loads(self.rfile.read(size))
        self.send_response(204)
        self.end_headers()
        self.results.put(report)


def stop(process):
    if process.poll() is None:
        if os.name == "posix":
            os.killpg(process.pid, signal.SIGTERM)
        else:
            process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            if os.name == "posix":
                os.killpg(process.pid, signal.SIGKILL)
            else:
                process.kill()
            process.wait(timeout=3)


def run_browser(engine, executable):
    if not shutil.which(executable):
        raise SystemExit(f"{engine} executable is unavailable: {executable}")
    version = subprocess.check_output([executable, "--version"], text=True, timeout=5).strip()
    token = secrets.token_hex(12)
    results = Queue()
    server = ThreadingHTTPServer(("127.0.0.1", 0), partial(Handler, results=results, token=token))
    Thread(target=server.serve_forever, daemon=True).start()
    try:
        with TemporaryDirectory(prefix=f"neura-dom-{engine}-") as work:
            profile = Path(work) / "profile"
            profile.mkdir()
            log_path = Path(work) / "browser.log"
            url = f"http://127.0.0.1:{server.server_port}/?token={token}"
            if engine == "chromium":
                flags = ["--headless", "--disable-gpu", "--disable-dev-shm-usage", "--no-first-run",
                         "--disable-background-networking", f"--user-data-dir={profile}"]
                if hasattr(os, "geteuid") and os.geteuid() == 0:
                    flags.append("--no-sandbox")
            else:
                flags = ["--headless", "--no-remote", "--profile", str(profile)]
            with log_path.open("wb") as log:
                process = subprocess.Popen([executable, *flags, url], stdout=log, stderr=subprocess.STDOUT,
                                           start_new_session=os.name == "posix")
                try:
                    report = results.get(timeout=18)
                except Empty:
                    report = {"error": "the browser did not send a result"}
                finally:
                    stop(process)
            if "error" in report:
                tail = "\n".join(log_path.read_text(errors="replace").splitlines()[-10:])
                raise SystemExit(f"{version}: {report['error']}\n{tail}")
            print(f"{version}: {len(report['passed'])} tests passed "
                  f"(native state-preserving move: {report['preservingMove']})")
    finally:
        server.shutdown()
        server.server_close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chromium", default="chromium")
    parser.add_argument("--firefox", default="firefox")
    args = parser.parse_args()
    run_browser("chromium", args.chromium)
    run_browser("firefox", args.firefox)


if __name__ == "__main__":
    main()
