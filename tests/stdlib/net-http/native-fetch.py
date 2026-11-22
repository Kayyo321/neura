#!/usr/bin/env python3
"""Black-box Fetch and Http.Client against bounded loopback HTTP/TLS peers."""

import argparse
import concurrent.futures
import contextlib
import http.client
import os
from pathlib import Path
import queue
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


class Peer:
    """A finite server. The stop event also releases intentionally quiet peers."""
    def __init__(self, case, context=None):
        self.case = case
        self.context = context
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
        if self.case == "closed":
            return
        if self.case == "timeout":
            self.stop.wait(timeout=2)
            return
        if self.case == "absolute-deadline":
            time.sleep(0.12)
            connection.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n")
            if not self.stop.wait(timeout=0.18):
                connection.sendall(b"hello")
            return
        if self.case == "echo":
            body = receive_body(connection, head)
            if body != b"ping":
                raise AssertionError(f"POST body differs: {body!r}")
            connection.sendall(fixed(body))
        elif self.case == "chunked":
            connection.sendall(b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
                               b"2\r\nhe\r\n3\r\nllo\r\n0\r\nX-End: yes\r\n\r\n")
        elif self.case == "redirect" and head.startswith(b"GET /start "):
            connection.sendall(b"HTTP/1.1 302 Found\r\nLocation: /fixed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
        elif self.case == "redirect-loop":
            connection.sendall(b"HTTP/1.1 302 Found\r\nLocation: /loop\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
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
             ("redirect", "Ok:200:hello", "get"), ("not-found", "Ok:404:missing", "get"),
             ("echo", "Ok:200:ping", "post"), ("closed", "Custom:http.OutcomeUnknown", "get"),
             ("truncated", "Custom:http.OutcomeUnknown", "get"), ("bad-chunk", "Custom:http.OutcomeUnknown", "get"),
             ("oversized-header", "Custom:http.TooLarge", "get"),
             ("oversized-body", "Custom:http.TooLarge", "get"),
             ("duplicate-length", "Custom:http.OutcomeUnknown", "get"),
             ("length-and-transfer", "Custom:http.OutcomeUnknown", "get"),
             ("redirect-loop", "Custom:http.OutcomeUnknown", "get"),
             ("timeout", "Io:TimedOut", "short"), ("absolute-deadline", "Io:TimedOut", "short"),
             ("non-reading", "Io:TimedOut", "post-blocked")]
    for api in ("fetch", "client"):
        for case, expected, mode in cases:
            with Peer(case) as peer:
                path = "/start" if case == "redirect" else "/loop" if case == "redirect-loop" else "/fixed"
                url = f"http://127.0.0.1:{peer.port}{path}"
                expect(invoke(api, url, mode), expected, f"{api}-{case}")
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
            try:
                unexpected, _ = unused.accept()
            except BlockingIOError:
                pass
            else:
                unexpected.close()
                raise AssertionError("rejected outgoing framing opened a TCP connection")
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
    if args.http2:
        h2_case(args, root, cert, key, invoke)


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

    def copy(self, source, target):
        deadline = time.monotonic() + 6
        try:
            while not self.stop.is_set() and time.monotonic() < deadline:
                try:
                    data = source.recv(65536)
                except socket.timeout:
                    continue
                if not data:
                    return
                target.sendall(data)
        except (ConnectionError, ssl.SSLError):
            # Closing a completed one-request fetch tears down its TLS session.
            pass
        finally:
            self.stop.set()
            with contextlib.suppress(OSError):
                target.shutdown(socket.SHUT_RDWR)

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
                            front.settimeout(0.25)
                            backend.settimeout(0.25)
                            reverse = threading.Thread(target=self.copy, args=(backend, front), daemon=True)
                            reverse.start()
                            self.copy(front, backend)
                            reverse.join(timeout=5)
                            if reverse.is_alive():
                                raise AssertionError("HTTP/2 reverse relay exceeded its deadline")
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
