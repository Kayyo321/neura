#!/usr/bin/env python3
"""Black-box Fetch and Http.Client against bounded loopback HTTP/TLS peers."""

import argparse
import concurrent.futures
import contextlib
import http.client
import os
from pathlib import Path
import queue
import select
import socket
import ssl
import subprocess
import tempfile
import threading
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
MAX_HEAD = 128 * 1024
COMMAND_TIMEOUT = 45


def command(arguments, *, env=None, timeout=COMMAND_TIMEOUT):
    result = subprocess.run([str(arg) for arg in arguments], capture_output=True, text=True,
                            env=env, timeout=timeout)
    if result.returncode:
        raise AssertionError(f"exit {result.returncode}: {' '.join(map(str, arguments))}\n"
                             f"{result.stdout}\n{result.stderr}")
    return result.stdout.strip()


def receive_head(connection):
    data = bytearray()
    while not data.endswith(b"\r\n\r\n"):
        part = connection.recv(1)
        if not part or len(data) >= MAX_HEAD:
            raise AssertionError("fixture received an incomplete or oversized request head")
        data.extend(part)
    return bytes(data)


def receive_body(connection, head):
    length = 0
    for line in head.split(b"\r\n")[1:]:
        if line.lower().startswith(b"content-length:"):
            length = int(line.split(b":", 1)[1])
    if length > 8 * 1024 * 1024:
        raise AssertionError("fixture request exceeded the body limit")
    body = bytearray()
    while len(body) < length:
        chunk = connection.recv(min(65536, length - len(body)))
        if not chunk:
            raise AssertionError("fixture received a truncated request body")
        body.extend(chunk)
    return bytes(body)


def fixed(body=b"hello", status=b"200 OK"):
    return b"HTTP/1.1 " + status + b"\r\nContent-Length: " + str(len(body)).encode() + b"\r\nConnection: close\r\n\r\n" + body


def redirect(status, target):
    return b"HTTP/1.1 " + status + b"\r\nLocation: " + target + b"\r\nContent-Length: 0\r\n\r\n"


class Peer:
    """A finite server. The stop event also releases intentionally quiet peers."""
    def __init__(self, case, context=None, *, target=None):
        self.case = case
        self.context = context
        self.target = target
        self.paths = []
        self.stop = threading.Event()
        self.errors = queue.Queue()
        self.listener = socket.socket()
        self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        if case == "non-reading":
            self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(8)
        self.listener.settimeout(0.2)
        self.port = self.listener.getsockname()[1]
        self.thread = threading.Thread(target=self.serve, daemon=True)

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *_):
        self.stop.set()
        self.thread.join(timeout=6)
        self.listener.close()
        if self.thread.is_alive():
            raise AssertionError(f"{self.case}: fixture did not finish within its deadline")
        if not self.errors.empty():
            raise self.errors.get()

    def serve(self):
        deadline = time.monotonic() + 35
        accepted = 0
        while not self.stop.is_set() and time.monotonic() < deadline and accepted < 32:
            try:
                connection, _ = self.listener.accept()
            except socket.timeout:
                continue
            accepted += 1
            with connection:
                connection.settimeout(4)
                try:
                    if self.case == "non-reading":
                        self.stop.wait(timeout=2)
                        continue
                    if self.case == "handshake-timeout":
                        self.stop.wait(timeout=2)
                        continue
                    if self.context:
                        try:
                            connection = self.context.wrap_socket(connection, server_side=True)
                        except (ssl.SSLError, ConnectionError):
                            if self.case in {"untrusted", "wrong-name"}:
                                continue
                            raise
                    with connection:
                        if self.case in {"untrusted", "wrong-name"}:
                            # Some TLS versions finish the server side before
                            # the client reports its verification rejection.
                            try:
                                request = connection.recv(1)
                            except (ssl.SSLError, ConnectionError):
                                continue
                            if request:
                                raise AssertionError("an unverified TLS peer received application bytes")
                            continue
                        self.respond(connection)
                except (BrokenPipeError, ConnectionResetError):
                    # The deadline and malformed-response cases close early.
                    if self.case not in {"timeout", "absolute-deadline", "oversized-header", "oversized-body"}:
                        self.errors.put(AssertionError(f"{self.case}: client closed before the fixture responded"))
                        return
                except Exception as error:
                    self.errors.put(error)
                    return

    def respond(self, connection):
        head = receive_head(connection)
        method, path, _ = head.split(b"\r\n", 1)[0].split(b" ")
        self.paths.append(path)
        if self.case == "closed":
            return
        if self.case == "timeout":
            self.stop.wait(timeout=2)
            return
        if self.case == "absolute-deadline":
            # Each pause is shorter than the 400 ms request budget. A
            # timeout renewed by each read would allow the entire response.
            for piece in (b"HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n", b"he", b"l", b"lo"):
                if self.stop.wait(timeout=0.13):
                    break
                connection.sendall(piece)
            return
        if self.case == "echo":
            body = receive_body(connection, head)
            if body != b"ping":
                raise AssertionError(f"POST body differs: {body!r}")
            connection.sendall(fixed(body))
        elif self.case == "chunked":
            connection.sendall(b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
                               b"2\r\nhe\r\n3\r\nllo\r\n0\r\nX-End: yes\r\n\r\n")
        elif self.case == "many-chunks":
            chunk = b"64\r\n" + b"x" * 100 + b"\r\n"
            connection.sendall(b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n" +
                               chunk * 3000 + b"0\r\nX-End: yes\r\n\r\n")
        elif self.case == "redirect" and head.startswith(b"GET /start "):
            connection.sendall(b"HTTP/1.1 302 Found\r\nLocation: /fixed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
        elif self.case == "redirect-303-same-url":
            body = receive_body(connection, head)
            if head.startswith(b"POST /fixed ") and body == b"ping":
                connection.sendall(redirect(b"303 See Other", b"/fixed"))
            elif head.startswith(b"GET /fixed ") and not body:
                connection.sendall(fixed())
            else:
                raise AssertionError(f"303 fixture received unexpected method, path, or body: {head!r} {body!r}")
        elif self.case in {"redirect-307", "redirect-308"}:
            body = receive_body(connection, head)
            if method != b"POST" or body != b"ping":
                raise AssertionError(f"307/308 fixture received unexpected method or body: {method!r} {body!r}")
            if path == b"/fixed":
                status = b"307 Temporary Redirect" if self.case == "redirect-307" else b"308 Permanent Redirect"
                connection.sendall(redirect(status, self.target or b"/landed"))
            elif path == b"/landed" and self.target is None:
                connection.sendall(fixed(body))
            else:
                raise AssertionError(f"307/308 fixture received unexpected path: {path!r}")
        elif self.case in {"redirect-ten-hops", "redirect-eleven-hops"}:
            hop = int(path[1:])
            if hop == (10 if self.case == "redirect-ten-hops" else 11):
                connection.sendall(fixed())
            else:
                connection.sendall(redirect(b"302 Found", f"/{hop + 1}".encode()))
        elif self.case in {"redirect-headers", "redirected-headers"}:
            fields = dict(line.lower().split(b": ", 1) for line in head.split(b"\r\n")[1:] if line)
            if fields.get(b"accept") != b"text/plain" or fields.get(b"accept-language") != b"en":
                raise AssertionError(f"safe request headers missing: {fields!r}")
            supplied = {b"authorization", b"cookie", b"x-api-key", b"content-type"}
            if self.case == "redirect-headers":
                if not supplied <= fields.keys():
                    raise AssertionError(f"initial request headers missing: {fields!r}")
                connection.sendall(redirect(b"302 Found", self.target))
            else:
                allowed = {b"accept", b"accept-language", b"host", b"connection", b"content-length"}
                if fields.keys() != allowed:
                    raise AssertionError(f"caller headers crossed the origin: {fields!r}")
                connection.sendall(fixed())
        elif self.case == "redirect-downgrade":
            connection.sendall(redirect(b"302 Found", self.target))
        elif self.case == "redirect-loop":
            connection.sendall(b"HTTP/1.1 302 Found\r\nLocation: /loop\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
        elif self.case in {"duplicate-location", "redirect-duplicate-location"}:
            status = b"200 OK" if self.case == "duplicate-location" else b"302 Found"
            connection.sendall(b"HTTP/1.1 " + status + b"\r\nLocation: /one\r\nLocation: /two\r\n"
                               b"Content-Length: 5\r\n\r\nhello")
        elif self.case == "truncated":
            connection.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 10\r\nConnection: close\r\n\r\nabc")
        elif self.case == "bad-chunk":
            connection.sendall(b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
                               b"not-a-size\r\n")
        elif self.case == "oversized-header":
            connection.sendall(b"HTTP/1.1 200 OK\r\nX-Large: " + b"x" * (128 * 1024) + b"\r\n\r\n")
        elif self.case == "oversized-body":
            connection.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 8388609\r\nConnection: close\r\n\r\n")
        elif self.case == "duplicate-length":
            # Ordinary framing-rejection fixtures: no pipelined request or
            # secondary payload is sent through these local peers.
            connection.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 0\r\nContent-Length: 1\r\n\r\n")
        elif self.case == "length-and-transfer":
            connection.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 0\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n")
        elif self.case == "not-found":
            connection.sendall(fixed(b"missing", b"404 Not Found"))
        else:
            connection.sendall(fixed())


class TaskPeer:
    """Barriers require concurrent request heads before releasing responses."""
    def __init__(self, case):
        self.case = case
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(4)
        self.listener.settimeout(0.1)
        self.port = self.listener.getsockname()[1]
        self.stop = threading.Event()
        self.pending = threading.Event()
        self.closed = threading.Event()
        self.probed = threading.Event()
        self.barrier = threading.Barrier(2)
        self.errors = queue.Queue()
        self.lock = threading.Lock()
        self.paths = set()
        self.workers = []
        self.closed_at = self.probed_at = None
        self.thread = threading.Thread(target=self.serve, daemon=True)

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *_):
        self.stop.set()
        self.thread.join(timeout=1)
        self.listener.close()
        for worker in self.workers:
            worker.join(timeout=6)
        if self.thread.is_alive() or any(worker.is_alive() for worker in self.workers):
            raise AssertionError("task fixture exceeded its cleanup deadline")
        if not self.errors.empty():
            raise self.errors.get()

    def serve(self):
        deadline = time.monotonic() + 12
        while not self.stop.is_set() and time.monotonic() < deadline:
            try:
                connection, _ = self.listener.accept()
            except socket.timeout:
                continue
            worker = threading.Thread(target=self.respond, args=(connection,), daemon=True)
            self.workers.append(worker)
            worker.start()

    def respond(self, connection):
        try:
            with connection:
                connection.settimeout(0.2)
                # Head parsing itself also has a finite bound.
                connection.settimeout(3)
                head = receive_head(connection)
                path = head.split(b" ", 2)[1].decode("ascii")
                with self.lock:
                    if path in self.paths:
                        raise AssertionError(f"duplicate task request {path}")
                    self.paths.add(path)
                if self.case == "overlap":
                    if path not in {"/first", "/second"}:
                        raise AssertionError(f"unexpected overlap path {path}")
                    # A serial invoke() cannot send the second request until
                    # the first response arrives; this barrier rejects it.
                    try:
                        self.barrier.wait(timeout=2)
                    except threading.BrokenBarrierError as error:
                        raise AssertionError("two request heads did not overlap before any response") from error
                    connection.sendall(fixed(b"hello"))
                elif path == "/pending":
                    self.pending.set()
                    connection.settimeout(0.1)
                    deadline = time.monotonic() + 5
                    while not self.stop.is_set() and time.monotonic() < deadline:
                        try:
                            data = connection.recv(1)
                        except socket.timeout:
                            continue
                        if data:
                            raise AssertionError("unexpected data after pending GET")
                        self.closed_at = time.monotonic()
                        self.closed.set()
                        return
                    raise AssertionError("pending fetch was not cancelled before the 5-second peer timeout")
                elif path == "/probe":
                    if not self.pending.wait(timeout=2) or self.closed.is_set():
                        raise AssertionError("probe did not observe a live pending request")
                    self.probed_at = time.monotonic()
                    self.probed.set()
                    connection.sendall(fixed(b"pending"))
                elif path == "/cancelled":
                    if not self.probed.is_set() or not self.closed.wait(timeout=1):
                        raise AssertionError("cancelled fetch socket remained open while process awaited cleanup")
                    if self.closed_at - self.probed_at >= 1:
                        raise AssertionError("scope cancellation did not close the pending socket promptly")
                    connection.sendall(fixed(b"closed"))
                else:
                    raise AssertionError(f"unexpected cancellation path {path}")
        except Exception as error:
            self.errors.put(error)


def task_cases(invoke):
    with TaskPeer("overlap") as peer:
        expect(invoke("fetch", f"http://localhost:{peer.port}", "overlap"),
               "Overlap:Ok:200:hello|Ok:200:hello", "fetch-two-task-barrier-overlap")
        if peer.paths != {"/first", "/second"}:
            raise AssertionError(f"overlap paths differ: {peer.paths}")
    with TaskPeer("cancel") as peer:
        expect(invoke("fetch", f"http://localhost:{peer.port}", "cancel-scope"),
               "Cancel:Io:Cancelled|Cleanup:Ok:200:closed", "fetch-scope-cancels-live-pending-request")
        if peer.paths != {"/pending", "/probe", "/cancelled"}:
            raise AssertionError(f"cancellation paths differ: {peer.paths}")


def task_fixture_self_test():
    def get(port, path):
        client = http.client.HTTPConnection("localhost", port, timeout=3)
        try:
            client.request("GET", path)
            response = client.getresponse()
            return response.status, response.read()
        finally:
            client.close()
    with TaskPeer("overlap") as peer:
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as workers:
            first = workers.submit(get, peer.port, "/first")
            second = workers.submit(get, peer.port, "/second")
            if first.result(timeout=4) != (200, b"hello") or second.result(timeout=4) != (200, b"hello"):
                raise AssertionError("overlap fixture did not release both clients")
    print("PASS fixture-two-task-barrier", flush=True)
    try:
        with TaskPeer("overlap") as peer:
            with contextlib.suppress(http.client.RemoteDisconnected):
                get(peer.port, "/first")
    except AssertionError as error:
        if "did not overlap" not in str(error):
            raise
    else:
        raise AssertionError("barrier fixture accepted a serialized request")
    print("PASS fixture-rejects-serialized-request", flush=True)
    with TaskPeer("cancel") as peer:
        with socket.create_connection(("localhost", peer.port), timeout=3) as pending:
            pending.sendall(b"GET /pending HTTP/1.1\r\nHost: localhost\r\n\r\n")
            if get(peer.port, "/probe") != (200, b"pending"):
                raise AssertionError("pending probe failed")
        if get(peer.port, "/cancelled") != (200, b"closed"):
            raise AssertionError("pending cleanup probe failed")
    print("PASS fixture-cancelled-socket-cleanup", flush=True)


def certificate(openssl, directory, stem):
    cert, key = directory / f"{stem}.pem", directory / f"{stem}.key"
    command([openssl, "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2", "-subj", "/CN=localhost",
             "-addext", "subjectAltName=DNS:localhost", "-keyout", key, "-out", cert], timeout=20)
    return cert, key


def server_context(cert, key, alpn=("http/1.1",)):
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.set_alpn_protocols(list(alpn))
    context.load_cert_chain(cert, key)
    return context


def expect(actual, expected, name):
    if actual != expected:
        raise AssertionError(f"{name}: expected {expected!r}, got {actual!r}")
    print(f"PASS {name}", flush=True)


def expect_no_connection(listener, reason):
    try:
        unexpected, _ = listener.accept()
    except BlockingIOError:
        return
    unexpected.close()
    raise AssertionError(f"{reason}: unexpectedly opened a TCP connection")


def redirect_cases(invoke, api, cert, key):
    for case, expected in (("redirect-ten-hops", "Ok:200:hello"),
                           ("redirect-eleven-hops", "Custom:http.OutcomeUnknown")):
        with Peer(case) as peer:
            expect(invoke(api, f"http://127.0.0.1:{peer.port}/0"), expected, f"{api}-{case}")
        if peer.paths != [f"/{hop}".encode() for hop in range(11)]:
            raise AssertionError(f"{case}: unexpected redirect path sequence: {peer.paths!r}")
    with Peer("redirect-307") as peer:
        expect(invoke(api, f"http://127.0.0.1:{peer.port}/fixed", "post"), "Ok:200:ping", f"{api}-307-same-origin")
    with Peer("redirected-headers") as destination:
        target = f"http://127.0.0.1:{destination.port}/landed".encode()
        with Peer("redirect-headers", target=target) as peer:
            expect(invoke(api, f"http://127.0.0.1:{peer.port}/fixed", "redirect-headers"), "Ok:200:hello",
                   f"{api}-redirect-strips-unsafe-fields")
    with socket.socket() as unused:
        unused.bind(("127.0.0.1", 0))
        unused.listen(1)
        unused.setblocking(False)
        target = f"http://127.0.0.1:{unused.getsockname()[1]}/landed".encode()
        for status in (307, 308):
            with Peer(f"redirect-{status}", target=target) as peer:
                url = f"http://127.0.0.1:{peer.port}/fixed"
                expect(invoke(api, url, "post"),
                       f"Custom:http.RedirectDenied:POST {url}: a redirect to another origin would resend the body",
                       f"{api}-{status}-cross-origin-body-refused")
                expect_no_connection(unused, f"HTTP {status} redirect across origins")
        if cert is not None:
            with Peer("redirect-downgrade", server_context(cert, key), target=target) as peer:
                url = f"https://localhost:{peer.port}/fixed"
                expect(invoke(api, url),
                       f"Custom:http.RedirectDenied:GET {url}: a redirect from HTTPS to HTTP was refused",
                       f"{api}-https-to-http-refused")
                expect_no_connection(unused, "HTTPS-to-HTTP redirect")


def origin_cases(invoke):
    """An http_client grant limited to listed origins, end to end through `neura run`."""
    def only(*origins):
        return ("--allow-http-client=" + ",".join(origins),)
    for api in ("fetch", "client"):
        with Peer("fixed") as peer:
            base = f"http://127.0.0.1:{peer.port}"
            expect(invoke(api, base + "/fixed", extra=only(base)), "Ok:200:hello", f"{api}-origin-listed")
        with socket.socket() as unused:
            unused.bind(("127.0.0.1", 0))
            unused.listen(1)
            unused.setblocking(False)
            other = f"http://127.0.0.1:{unused.getsockname()[1]}"
            with Peer("fixed") as peer:
                base = f"http://127.0.0.1:{peer.port}"
                expect(invoke(api, other + "/", extra=only(base)), f"Io:CapabilityDenied:{other}",
                       f"{api}-origin-unlisted-port")
                expect(invoke(api, base + "/", extra=("--allow-http-client=",)), f"Io:CapabilityDenied:{base}",
                       f"{api}-origin-empty-list")
            with Peer("redirect-downgrade", target=(other + "/landed").encode()) as peer:
                base = f"http://127.0.0.1:{peer.port}"
                expect(invoke(api, base + "/fixed", extra=only(base)), f"Io:CapabilityDenied:{other}",
                       f"{api}-origin-redirect-unlisted")
            expect_no_connection(unused, "a request outside the origin list")
            # localhost is a name with loopback answers: they need the numeric origin as well.
            port = unused.getsockname()[1]
            expect(invoke(api, f"http://localhost:{port}/", extra=only(f"http://localhost:{port}")),
                   f"Io:CapabilityDenied:http://localhost:{port}", f"{api}-origin-localhost-loopback")
            expect_no_connection(unused, "a name resolving to an unlisted loopback address")
        with Peer("fixed") as peer:
            expect(invoke(api, f"http://localhost:{peer.port}/fixed",
                          extra=only(f"http://localhost:{peer.port}", f"http://127.0.0.1:{peer.port}")),
                   "Ok:200:hello", f"{api}-origin-localhost-with-numeric")


def native_cases(args, root, cert, key, other_cert, other_key):
    image = root / "native-fetch.nvr"
    command([args.neura, "build", HERE / "loopback/native-fetch.nra", "-o", image])
    env = os.environ.copy()
    if cert is not None:
        env["SSL_CERT_FILE"] = str(cert)
        env["SSL_CERT_DIR"] = str(root / "empty-ca-directory")
        (root / "empty-ca-directory").mkdir()

    def invoke(api, url, mode="get", *, granted=True, extra=()):
        policy = ["--allow-http-client", "--deny-net"] if granted else ["--allow-net"]
        return command([args.neura, "run", image, "--gpu=off", "--max-steps", "500000000",
                        *policy, *extra, "--", api, url, mode], env=env)

    cases = [("fixed", "Ok:200:hello", "get"), ("chunked", "Ok:200:hello", "get"),
             ("many-chunks", "Ok:200:300000:120:120", "body-size"),
             ("redirect", "Ok:200:hello", "get"), ("not-found", "Ok:404:missing", "get"),
             ("redirect-303-same-url", "Ok:200:hello", "post"),
             ("echo", "Ok:200:ping", "post"), ("closed", "Custom:http.OutcomeUnknown", "get"),
             ("truncated", "Custom:http.OutcomeUnknown", "get"), ("bad-chunk", "Custom:http.OutcomeUnknown", "get"),
             ("oversized-header", "Custom:http.TooLarge", "get"),
             ("oversized-body", "Custom:http.TooLarge", "get"),
             ("duplicate-length", "Custom:http.OutcomeUnknown", "get"),
             ("duplicate-location", "Ok:200:hello", "get"),
             ("redirect-duplicate-location", "Custom:http.OutcomeUnknown", "get"),
             ("length-and-transfer", "Custom:http.OutcomeUnknown", "get"),
             ("redirect-loop", "Custom:http.OutcomeUnknown", "get"),
             ("timeout", "Io:TimedOut", "short"), ("absolute-deadline", "Io:TimedOut", "deadline"),
             ("absolute-deadline", "Ok:200:hello", "get"),
             ("non-reading", "Io:TimedOut", "post-blocked")]
    origin_cases(invoke)
    for api in ("fetch", "client"):
        for case, expected, mode in cases:
            with Peer(case) as peer:
                path = "/start" if case == "redirect" else "/loop" if case == "redirect-loop" else "/fixed"
                url = f"http://127.0.0.1:{peer.port}{path}"
                expect(invoke(api, url, mode), expected, f"{api}-{case}")
        redirect_cases(invoke, api, cert, key)
        # Bound but deliberately not listening, so refusal has no port-reuse race.
        with socket.socket() as reserved:
            reserved.bind(("127.0.0.1", 0))
            url = f"http://127.0.0.1:{reserved.getsockname()[1]}/"
            expect(invoke(api, url), "Io:ConnectionRefused", f"{api}-refused")
            expect(invoke(api, url, granted=False), "Io:CapabilityDenied", f"{api}-http-client-default-denied")
        with socket.socket() as unused:
            unused.bind(("127.0.0.1", 0))
            unused.listen(1)
            unused.setblocking(False)
            url = f"http://127.0.0.1:{unused.getsockname()[1]}/"
            for shape in ("duplicate-request-length", "request-length-and-transfer"):
                expect(invoke(api, url, shape), "Io:InvalidInput", f"{api}-{shape}")
            expect_no_connection(unused, "rejected outgoing framing")
            schemes = ("http", "https") if cert is not None else ("http",)
            for scheme in schemes:
                url = f"{scheme}://localhost:{unused.getsockname()[1]}/"
                for field in ("Keep-Alive", "Connection", "Upgrade", "Proxy-Connection", "TE", "HTTP2-Settings"):
                    expect(invoke(api, url, f"header:{field}"), "Io:InvalidInput", f"{api}-{scheme}-reject-{field}")
                expect_no_connection(unused, f"rejected {scheme} connection-specific fields")
        trace = root / f"{api}.trace"
        with Peer("fixed") as peer:
            url = f"http://127.0.0.1:{peer.port}/fixed"
            expect(invoke(api, url, extra=("--record", trace)), "Ok:200:hello", f"{api}-record")
        expect(invoke(api, url, extra=("--replay", trace)), "Ok:200:hello", f"{api}-offline-replay")
        tls_cases = ("tls", "untrusted", "wrong-name", "handshake-timeout") if cert is not None else ()
        for case in tls_cases:
            context = server_context(other_cert, other_key) if case == "untrusted" else server_context(cert, key)
            with Peer(case, context) as peer:
                hostname = "127.0.0.1" if case == "wrong-name" else "localhost"
                url = f"https://{hostname}:{peer.port}/fixed"
                expected = "Io:PermissionDenied" if case in {"untrusted", "wrong-name"} else (
                    "Io:TimedOut" if case == "handshake-timeout" else "Ok:200:hello")
                expect(invoke(api, url, "short" if case == "handshake-timeout" else "get"), expected, f"{api}-{case}")
        if args.without_tls:
            # The compiled-out service must refuse HTTPS before opening TCP.
            with socket.socket() as listener:
                listener.bind(("127.0.0.1", 0))
                listener.listen(1)
                listener.settimeout(0.2)
                url = f"https://127.0.0.1:{listener.getsockname()[1]}/"
                expect(invoke(api, url), "Io:Unsupported", f"{api}-https-build-unsupported")
                try:
                    connection, _ = listener.accept()
                except socket.timeout:
                    pass
                else:
                    connection.close()
                    raise AssertionError("compiled-out HTTPS unexpectedly opened a TCP connection")
    with socket.socket() as reserved:
        reserved.bind(("127.0.0.1", 0))
        expect(invoke("fetch", f"http://127.0.0.1:{reserved.getsockname()[1]}/", "invalid-header"),
               "Io:InvalidInput", "fetch-invalid-request-header")
    task_cases(invoke)
    if cert is not None:
        # Exit while a fetch waits in a TLS handshake: prompt, clean status 0.
        with Peer("handshake-timeout", server_context(cert, key)) as peer:
            started = time.monotonic()
            expect(invoke("fetch", f"https://localhost:{peer.port}/fixed", "exit-pending"), "Exiting",
                   "fetch-exit-joins-pending-worker")
            if time.monotonic() - started > 2.5:
                raise AssertionError("exit waited for the pending fetch's own deadline")
    if args.http2:
        h2_case(args, root, cert, key, invoke)
        h2_peer_cases(cert, key, invoke)


def receive_exact(connection, size):
    data = bytearray()
    while len(data) < size:
        portion = connection.recv(size - len(data))
        if not portion:
            raise AssertionError("HTTP/2 fixture received incomplete input")
        data.extend(portion)
    return bytes(data)


def h2_frame(kind, flags=0, stream=0, payload=b""):
    return len(payload).to_bytes(3, "big") + bytes((kind, flags)) + stream.to_bytes(4, "big") + payload


def hpack_literal(text):
    if len(text) < 127:
        return bytes((len(text),)) + text
    size = len(text) - 127
    prefix = bytearray((127,))
    while size >= 128:
        prefix.append((size & 127) | 128)
        size >>= 7
    prefix.append(size)
    return bytes(prefix) + text


def h2_read_frame(connection):
    header = receive_exact(connection, 9)
    size = int.from_bytes(header[:3], "big")
    if size > 16384:
        raise AssertionError("HTTP/2 fixture received an oversized frame")
    return header[3], header[4], int.from_bytes(header[5:], "big") & 0x7fffffff, receive_exact(connection, size)


class H2Peer(Peer):
    """An authenticated loopback peer for bounded HTTP/2 outcomes."""
    def __init__(self, case, cert, key, compressed_head):
        super().__init__(case, server_context(cert, key, ("h2",)))
        self.compressed_head = compressed_head

    def respond(self, connection):
        # Without this, each flow-control window's last short frame waits for
        # a delayed acknowledgement, and the 8 MiB body takes many seconds.
        connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        if connection.selected_alpn_protocol() != "h2":
            raise AssertionError("native fetch did not negotiate h2 with the fixture")
        if receive_exact(connection, 24) != b"PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n":
            raise AssertionError("HTTP/2 fixture received an invalid preface")
        kind, _, stream, _ = h2_read_frame(connection)
        if (kind, stream) != (4, 0):
            raise AssertionError("HTTP/2 client preface did not start with SETTINGS")
        connection.sendall(h2_frame(4))  # SETTINGS
        for _ in range(16):
            kind, flags, stream, _ = h2_read_frame(connection)
            if (kind, flags, stream) == (1, 5, 1):  # HEADERS, END_HEADERS | END_STREAM
                break
        else:
            raise AssertionError("HTTP/2 fixture did not receive a complete GET on stream 1")
        self.respond_h2(connection)
        if self.case == "h2-goaway-mid-body":
            return
        with contextlib.suppress(ssl.SSLEOFError, ConnectionResetError):
            while connection.recv(16384):
                pass

    def respond_h2(self, connection):
        head = h2_frame(1, 4, 1, b"\x88")  # HEADERS, :status=200
        if self.case == "h2-reset":
            connection.sendall(head + h2_frame(3, stream=1, payload=(8).to_bytes(4, "big")))  # CANCEL
            return
        if self.case == "h2-refused-stream":
            connection.sendall(h2_frame(3, stream=1, payload=(7).to_bytes(4, "big")))  # REFUSED_STREAM
            return
        if self.case == "h2-goaway-mid-body":
            data = h2_frame(0, stream=1, payload=b"partial body")
            goaway = h2_frame(7, payload=(1).to_bytes(4, "big") + bytes(4))
            connection.sendall(head + data + goaway)
            return
        if self.case == "h2-oversized-head":
            fields = b"\x88\x00" + hpack_literal(b"x-test-description") + hpack_literal(b"v" * 20000)
            connection.sendall(h2_frame(1, 1, 1, fields[:16384]) + h2_frame(9, 4, 1, fields[16384:]))
            return
        if self.case == "h2-decoded-head-limit":
            connection.sendall(h2_frame(1, 5, 1, self.compressed_head))
            return
        if self.case == "h2-oversized-body":
            connection.sendall(head)
            credit = {0: 65535, 1: 65535}
            remaining = 8 * 1024 * 1024 + 1
            payload = b"v" * 16384
            while remaining:
                size = min(len(payload), remaining, *credit.values())
                if size == 0:
                    kind, _, stream, update = h2_read_frame(connection)
                    if kind == 8 and stream in credit:  # WINDOW_UPDATE
                        credit[stream] += int.from_bytes(update, "big") & 0x7fffffff
                    continue
                remaining -= size
                connection.sendall(h2_frame(0, int(remaining == 0), 1, payload[:size]))
                for stream in credit:
                    credit[stream] -= size
            return
        early_head = h2_frame(1, 4, 1, b"\x08\x03103")  # HEADERS, :status=103
        goaway = h2_frame(7, payload=bytes(8))  # GOAWAY, last-stream-id=0, NO_ERROR
        connection.sendall((early_head if self.case == "h2-goaway-after-early-head" else b"") + goaway)


def hpack_expansion():
    """A small field section that decodes past the 16 KiB bound: one 4 KB field
    entered in the dynamic table, then one-byte references to it."""
    section = b"\x88" + b"\x40" + hpack_literal(b"x-a") + hpack_literal(b"v" * 4000) + b"\xbe" * 8
    if len(section) > 4200:
        raise AssertionError("the HPACK expansion input must stay small")
    return section


def h2_peer_cases(cert, key, invoke):
    packed = hpack_expansion()
    cases = (("h2-goaway-unprocessed", "Custom:http.NotProcessed:NotProcessed"),
             ("h2-refused-stream", "Custom:http.NotProcessed:NotProcessed"),
             ("h2-goaway-after-early-head", "Custom:http.OutcomeUnknown"),
             ("h2-goaway-mid-body", "Custom:http.OutcomeUnknown"),
             ("h2-reset", "Custom:http.OutcomeUnknown"),
             ("h2-oversized-head", "Custom:http.TooLarge"),
             ("h2-decoded-head-limit", "Custom:http.TooLarge"),
             ("h2-oversized-body", "Custom:http.TooLarge"))
    for api in ("fetch", "client"):
        for case, expected in cases:
            with H2Peer(case, cert, key, packed) as peer:
                expect(invoke(api, f"https://localhost:{peer.port}/"), expected, f"{api}-{case}")


class H2Relay:
    """Observe real ALPN and bridge frames to the existing Neura HTTPS server."""
    def __init__(self, cert, key, backend_port):
        self.front = server_context(cert, key, ("h2",))
        self.backend = ssl.create_default_context(cafile=str(cert))
        self.backend.set_alpn_protocols(["h2"])
        self.backend_port = backend_port
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(1)
        self.listener.settimeout(5)
        self.port = self.listener.getsockname()[1]
        self.stop = threading.Event()
        self.errors = queue.Queue()
        self.negotiated = False
        self.thread = threading.Thread(target=self.serve, daemon=True)

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *_):
        self.stop.set()
        self.thread.join(timeout=6)
        self.listener.close()
        if self.thread.is_alive():
            raise AssertionError("HTTP/2 relay did not finish within its deadline")
        if not self.errors.empty():
            raise self.errors.get()
        if not self.negotiated:
            raise AssertionError("the native client did not negotiate h2")

    def pump(self, front, backend):
        # One thread owns BOTH SSL objects. Concurrent recv/send on one
        # SSLSocket can corrupt TLS state even when the directions differ.
        peers = (front, backend)
        outgoing = {front: bytearray(), backend: bytearray()}
        writing = {front: b"", backend: b""}
        send_needs_read = {front: False, backend: False}
        read_needs_write = {front: False, backend: False}
        response_frames = bytearray()
        response_complete = False
        deadline = time.monotonic() + 6
        for peer in peers:
            peer.setblocking(False)
        while not self.stop.is_set() and time.monotonic() < deadline:
            readers = list(peers)
            writers = [peer for peer in peers if read_needs_write[peer] or
                       ((outgoing[peer] or writing[peer]) and not send_needs_read[peer])]
            readable, writable, _ = select.select(readers, writers, [], 0.1)
            for peer in peers:
                if peer.pending() and peer not in readable:
                    readable.append(peer)
            # Retain the exact write bytes across SSLWantRead/SSLWantWrite.
            for peer in peers:
                if not writing[peer] and outgoing[peer]:
                    writing[peer] = bytes(outgoing[peer][:65536])
                    del outgoing[peer][:len(writing[peer])]
                ready = peer in readable if send_needs_read[peer] else peer in writable
                if not writing[peer] or not ready:
                    continue
                try:
                    sent = peer.send(writing[peer])
                    if sent == 0:
                        raise AssertionError("TLS relay write made no progress")
                    writing[peer] = writing[peer][sent:]
                    send_needs_read[peer] = False
                except ssl.SSLWantReadError:
                    send_needs_read[peer] = True
                except ssl.SSLWantWriteError:
                    send_needs_read[peer] = False
            if response_complete and not outgoing[front] and not writing[front]:
                # Let the client consume the final frame before closing the
                # transport, even if its last control frame is still in flight.
                self.stop.wait(timeout=max(0, deadline - time.monotonic()))
                return
            for peer in peers:
                if send_needs_read[peer]:
                    continue
                if peer not in readable and not (read_needs_write[peer] and peer in writable):
                    continue
                try:
                    data = peer.recv(65536)
                    read_needs_write[peer] = False
                except ssl.SSLWantReadError:
                    read_needs_write[peer] = False
                    continue
                except ssl.SSLWantWriteError:
                    read_needs_write[peer] = True
                    continue
                if not data:
                    raise AssertionError("TLS relay peer closed before a complete HTTP/2 response was forwarded")
                target = backend if peer is front else front
                outgoing[target].extend(data)
                if len(outgoing[target]) > 256 * 1024:
                    raise AssertionError("TLS relay buffer exceeded its bound")
                if peer is backend:
                    response_frames.extend(data)
                    while len(response_frames) >= 9:
                        size = int.from_bytes(response_frames[:3], "big")
                        if size > 65536:
                            raise AssertionError("unexpectedly large HTTP/2 fixture frame")
                        if len(response_frames) < 9 + size:
                            break
                        frame_type, flags = response_frames[3], response_frames[4]
                        stream = int.from_bytes(response_frames[5:9], "big") & 0x7fffffff
                        if stream == 1 and frame_type in (0, 1) and flags & 1:
                            response_complete = True
                        del response_frames[:9 + size]
        raise AssertionError("TLS relay stopped before forwarding the complete HTTP/2 response")

    def serve(self):
        try:
            raw, _ = self.listener.accept()
            with raw:
                raw.settimeout(4)
                with self.front.wrap_socket(raw, server_side=True) as front:
                    if front.selected_alpn_protocol() != "h2":
                        raise AssertionError("native fetch did not offer HTTP/2 by ALPN")
                    with socket.create_connection(("127.0.0.1", self.backend_port), timeout=4) as backend_raw:
                        with self.backend.wrap_socket(backend_raw, server_hostname="localhost") as backend:
                            if backend.selected_alpn_protocol() != "h2":
                                raise AssertionError("Neura HTTPS server did not select h2")
                            self.negotiated = True
                            self.pump(front, backend)
        except Exception as error:
            self.errors.put(error)


def h2_case(args, root, cert, key, invoke):
    server_image = root / "test-server.nvr"
    command([args.neura, "build", ROOT / "tests/server/fixtures/test-server.nra", "-o", server_image])
    host = args.host or args.neura.with_name("neura-server-host" + (".exe" if os.name == "nt" else ""))
    if not host.is_file():
        raise AssertionError(f"HTTP/2 test requires the configured server host: {host}")
    errors_path = root / "http2-server.err"
    with errors_path.open("w") as errors:
        process = subprocess.Popen([str(host), "--listen", "127.0.0.1:0", "--tls-listen", "127.0.0.1:0",
                                    "--cert", str(cert), "--key", str(key), "--max-workers", "2", str(server_image)],
                                   stdout=subprocess.PIPE, stderr=errors, text=True)
        lines = queue.Queue()
        reader = threading.Thread(target=lambda: [lines.put(line) for line in process.stdout], daemon=True)
        reader.start()
        try:
            deadline = time.monotonic() + 12
            port = None
            ready = False
            while time.monotonic() < deadline:
                try:
                    line = lines.get(timeout=0.2)
                except queue.Empty:
                    if process.poll() is not None:
                        break
                    continue
                if "listening on https://" in line:
                    port = int(line.strip().rsplit(":", 1)[1])
                if line.strip().endswith("ready"):
                    ready = True
                    break
            if not ready or port is None:
                raise AssertionError("Neura HTTPS server did not become ready: " + errors_path.read_text())
            for api in ("fetch", "client"):
                with H2Relay(cert, key, port) as relay:
                    expect(invoke(api, f"https://localhost:{relay.port}/hello"), "Ok:200:hello from https",
                           f"{api}-http2-neura-https-server")
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
            process.stdout.close()
            reader.join(timeout=1)


def self_test(cert, key, other_cert, other_key):
    task_fixture_self_test()
    # Validate the fixtures independently while the native provider is built.
    for case, path, expected in (("fixed", "/fixed", (200, b"hello")),
                                 ("chunked", "/fixed", (200, b"hello")),
                                 ("not-found", "/fixed", (404, b"missing")),
                                 ("redirect", "/start", (302, b"")),
                                 ("echo", "/fixed", (200, b"ping"))):
        with Peer(case) as peer:
            client = http.client.HTTPConnection("127.0.0.1", peer.port, timeout=3)
            try:
                client.request("POST" if case == "echo" else "GET", path, body=b"ping" if case == "echo" else None)
                response = client.getresponse()
                actual = response.status, response.read()
                if actual != expected:
                    raise AssertionError(f"fixture {case}: {actual!r}")
            finally:
                client.close()
        print(f"PASS fixture-{case}", flush=True)
    for case in (("tls", "wrong-name", "untrusted") if cert is not None else ()):
        context = server_context(other_cert, other_key) if case == "untrusted" else server_context(cert, key)
        with Peer(case, context) as peer:
            hostname = "127.0.0.1" if case == "wrong-name" else "localhost"
            client = http.client.HTTPSConnection(hostname, peer.port, timeout=3,
                                                context=ssl.create_default_context(cafile=str(cert)))
            try:
                try:
                    client.request("GET", "/fixed")
                    response = client.getresponse()
                    if case != "tls" or (response.status, response.read()) != (200, b"hello"):
                        raise AssertionError(f"fixture {case} accepted an unverified peer")
                except ssl.SSLCertVerificationError:
                    if case == "tls":
                        raise
            finally:
                client.close()
        print(f"PASS fixture-{case}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--neura", type=Path)
    parser.add_argument("--openssl", type=Path, help="enable TLS fixtures using this local certificate generator")
    parser.add_argument("--without-tls", action="store_true", help="assert compiled-out HTTPS returns Unsupported before TCP")
    parser.add_argument("--host", type=Path)
    parser.add_argument("--http2", action="store_true", help="require ALPN h2 through the existing Neura HTTPS server")
    parser.add_argument("--self-test", action="store_true", help="check the loopback fixtures without a Neura provider")
    args = parser.parse_args()
    if not args.self_test and args.neura is None:
        parser.error("--neura is required unless --self-test is given")
    if args.http2 and args.openssl is None:
        parser.error("--http2 needs --openssl for its local TLS certificates")
    if args.without_tls and args.openssl is not None:
        parser.error("--without-tls cannot be combined with TLS fixtures")
    if args.neura:
        args.neura = args.neura.resolve()
    if args.host:
        args.host = args.host.resolve()
    if args.openssl:
        args.openssl = args.openssl.resolve()
    with tempfile.TemporaryDirectory(prefix="neura-native-fetch-") as directory:
        root = Path(directory)
        cert = key = other_cert = other_key = None
        if args.openssl:
            cert, key = certificate(args.openssl, root, "localhost")
            other_cert, other_key = certificate(args.openssl, root, "untrusted")
        if args.self_test:
            self_test(cert, key, other_cert, other_key)
        else:
            native_cases(args, root, cert, key, other_cert, other_key)


if __name__ == "__main__":
    main()
