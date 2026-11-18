#!/usr/bin/env python3
"""End-to-end tests of neura-server-host with a Neura server image.

Builds tests/server/fixtures/test-server.nra, runs the host on loopback with
ephemeral ports, and checks requests over plain HTTP and HTTPS, malformed and
oversized input, limits, traps, configuration, record and replay, a
concurrent load of 1,000 requests, and the drain (docs/design/Backend030.md).
"""

import argparse
import http.client
import json
import os
import shutil
import signal
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

FAILURES = []
# "h2c" runs the suite with every host started with --h2c and request()
# speaking HTTP/2 with prior knowledge through the test client
# (tests/server/http2_test_client.cpp); raw sockets and http.client stay
# HTTP/1.1 on the same listeners, and the admin listener is always HTTP/1.1.
PROTOCOL = "http/1.1"
HTTP2_CLIENT = None
ADMIN_PORTS = set()


def check(condition, message):
    if not condition:
        FAILURES.append(message)
        print("FAIL:", message, flush=True)


class Host:
    def __init__(self, args, image, *options, expect_ready=True, env=None):
        self.err_path = Path(tempfile.mkstemp(prefix="host-", suffix=".err", dir=args.work)[1])
        self.err = open(self.err_path, "w")
        h2c = ["--h2c"] if PROTOCOL == "h2c" else []
        command = [args.host, "--listen", "127.0.0.1:0", *h2c, *options, str(image)]
        self.process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=self.err, text=True,
                                        env=env)
        self.ports = {}
        self.ready = False
        deadline = time.time() + 30
        while time.time() < deadline:
            line = self.process.stdout.readline()
            if not line:
                break
            if "listening on" in line:
                url = line.strip().split("listening on ")[1]
                scheme, rest = url.split("://")
                self.ports.setdefault(scheme, int(rest.rsplit(":", 1)[1]))
            if "admin on" in line:
                self.ports["admin"] = int(line.strip().rsplit(":", 1)[1])
                ADMIN_PORTS.add(self.ports["admin"])
            if line.strip().endswith("ready"):
                self.ready = True
                break
        if expect_ready and not self.ready:
            self.stop()
            raise RuntimeError("the host did not become ready:\n" + self.stderr())

    @property
    def port(self):
        return self.ports["http"]

    def stderr(self):
        if not self.err.closed:
            self.err.flush()
        return self.err_path.read_text()

    def children(self, zygote=False):
        # Workers are children of whichever host thread spawned them; a
        # worker forked by the zygote is a child of the host too. The zygote
        # is left out unless asked for.
        pids = []
        for task in Path(f"/proc/{self.process.pid}/task").glob("*"):
            try:
                pids += [int(pid) for pid in (task / "children").read_text().split()]
            except OSError:
                pass
        # Each thread's list is read at a different moment, so a worker
        # reaped after one list was read can appear next to its replacement
        # from a later one. Only the processes that still exist after every
        # list has been read (zombies included) are counted: all of them
        # existed at one moment, the end of the reading.
        return sorted(pid for pid in set(pids) if exists(pid) and (comm(pid) == "neura-zygote") == zygote)

    def stop(self, sig=signal.SIGTERM, timeout=15):
        if self.process.poll() is None:
            self.process.send_signal(sig)
            try:
                self.process.wait(timeout)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        self.err.close()
        return self.process.returncode


def exists(pid):
    return Path(f"/proc/{pid}").exists()


def comm(pid):
    try:
        return Path(f"/proc/{pid}/comm").read_text().strip()
    except OSError:
        return ""


def metric(port, name):
    status, _, body = request(port, "GET", "/metrics")
    for line in body.decode().splitlines():
        if line.startswith(name + " "):
            return int(line.split()[1])
    return None


def h2(port, scenario, *options, body=None, timeout=60):
    """Runs the HTTP/2 test client; its JSON result."""
    command = [HTTP2_CLIENT, "--port", str(port), "--scenario", scenario, *[str(option) for option in options]]
    if body is not None:
        command += ["--body-stdin", "1"]
    result = subprocess.run(command, input=body, capture_output=True, timeout=timeout)
    if result.returncode != 0:
        raise RuntimeError(f"http2 client: {result.returncode} {result.stderr.decode(errors='replace')}")
    return json.loads(result.stdout)


def request(port, method, path, body=None, headers=None, timeout=30):
    if PROTOCOL == "h2c" and port not in ADMIN_PORTS:
        options = ["--method", method, "--path", path, "--timeout-ms", int(timeout * 1000)]
        for name, value in (headers or {}).items():
            options += ["--header", f"{name.lower()}:{value}"]
        if isinstance(body, str):
            body = body.encode()
        result = h2(port, "fetch", *options, body=body, timeout=timeout + 10)
        stream = result["streams"][0]
        if not stream["closed"] or (stream["status"] == 0):
            raise ConnectionError(f"HTTP/2 stream ended without a response: {result}")
        return stream["status"], stream["headers"], stream["body"].encode("latin-1")
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=timeout)
    connection.request(method, path, body=body, headers=headers or {})
    response = connection.getresponse()
    data = response.read()
    connection.close()
    return response.status, dict((k.lower(), v) for k, v in response.getheaders()), data


def raw(port, data, timeout=10):
    """Sends bytes and reads until the peer closes or the timeout."""
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as sock:
        sock.sendall(data)
        chunks = []
        try:
            while True:
                chunk = sock.recv(65536)
                if not chunk:
                    break
                chunks.append(chunk)
        except (socket.timeout, ConnectionResetError):
            pass
    return b"".join(chunks)


def status_of(reply):
    try:
        return int(reply.split(b" ", 2)[1])
    except (IndexError, ValueError):
        return 0


def test_basic(args, image):
    host = Host(args, image, "--config", "greeting=hi", "--config-env-prefix", "NEURA_TEST_",
                env={**os.environ, "NEURA_TEST_from_env": "env-value"})
    try:
        port = host.port
        status, headers, body = request(port, "GET", "/hello")
        check(status == 200 and body == b"hello from http\n", f"GET /hello: {status} {body!r}")
        check(headers.get("content-type") == "text/plain; charset=utf-8", f"content type: {headers}")

        status, headers, body = request(port, "HEAD", "/hello")
        check(status == 200 and body == b"" and headers.get("content-length") == "16", f"HEAD: {status} {headers}")

        payload = bytes(range(256)) * 40
        status, headers, body = request(port, "POST", "/echo", body=payload,
                                        headers={"Content-Type": "application/x-test"})
        check(status == 201 and body == payload, f"POST /echo: {status} {len(body)}")
        check(headers.get("content-type") == "application/x-test", f"echo content type: {headers}")
        check(headers.get("x-body-length") == str(len(payload)), f"echo length: {headers}")

        chunked = (b"POST /echo HTTP/1.1\r\nHost: t\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
                   b"5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n")
        reply = raw(port, chunked)
        check(status_of(reply) == 201 and reply.endswith(b"hello world"), f"chunked POST: {reply[:200]!r}")

        two = (b"GET /info?a=1 HTTP/1.1\r\nHost: t\r\n\r\n"
               b"GET /info?b=2 HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n")
        reply = raw(port, two)
        first = reply.find(b'"query":"a=1"')
        second = reply.find(b'"query":"b=2"')
        check(reply.count(b"HTTP/1.1 200") == 2 and 0 <= first < second, f"pipelined: {reply!r}")

        expect = (b"POST /echo HTTP/1.1\r\nHost: t\r\nContent-Length: 3\r\nExpect: 100-continue\r\n"
                  b"Connection: close\r\n\r\n")
        with socket.create_connection(("127.0.0.1", port), timeout=10) as sock:
            sock.sendall(expect)
            interim = sock.recv(4096)
            check(interim.startswith(b"HTTP/1.1 100 Continue"), f"100-continue: {interim!r}")
            sock.sendall(b"abc")
            rest = b""
            while True:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                rest += chunk
            check(status_of(rest) == 201 and rest.endswith(b"abc"), f"after 100: {rest!r}")

        status, _, body = request(port, "GET", "/config?greeting")
        check(status == 200 and body == b"hi", f"config: {status} {body!r}")
        status, _, body = request(port, "GET", "/config?from_env")
        check(status == 200 and body == b"env-value", f"config from env: {status} {body!r}")
        status, _, body = request(port, "GET", "/config?missing")
        check(status == 404, f"config missing: {status}")

        status, headers, _ = request(port, "GET", "/status")
        check(status == 204 and headers.get("x-test") == "yes", f"204: {status} {headers}")
        status, _, _ = request(port, "GET", "/nothing")
        check(status == 404, f"404: {status}")

        status, headers, body = request(port, "GET", "/fail")
        check(status == 500 and body == b'{"error":"internal"}', f"handler Err: {status} {body!r}")

        status, _, body = request(port, "GET", "/trap")
        check(status == 500 and body == b'{"error":"internal"}', f"trap: {status} {body!r}")
        status, _, _ = request(port, "GET", "/hello")
        check(status == 200, f"after a trap: {status}")
        time.sleep(1.0)
        check(len(host.children()) <= 3, f"worker processes after the trap: {host.children()}")

        # Malformed and rejected input, before any worker.
        cases = [
            (b"GARBAGE\r\n\r\n", 400, "bad request line"),
            (b"GET / HTTP/1.1\r\n\r\n", 400, "missing Host"),
            (b"GET / HTTP/1.1\r\nHost: a\r\nHost: b\r\n\r\n", 400, "repeated Host"),
            (b"POST /echo HTTP/1.1\r\nHost: t\r\nContent-Length: 3\r\nTransfer-Encoding: chunked\r\n\r\n", 400,
             "Content-Length and Transfer-Encoding"),
            (b"POST /echo HTTP/1.1\r\nHost: t\r\nTransfer-Encoding: gzip\r\n\r\n", 501, "unknown coding"),
            (b"BREW /pot HTTP/1.1\r\nHost: t\r\n\r\n", 501, "unknown method"),
            (b"GET /chat HTTP/1.1\r\nHost: t\r\nConnection: Upgrade\r\nUpgrade: websocket\r\n\r\n", 501,
             "WebSocket upgrade"),
            (b"GET / HTTP/1.1\r\nHost: t\r\nX-Folded: a\r\n b\r\n\r\n", 400, "folded header"),
            (b"GET / HTTP/1.1\r\nHost: t\r\n" + b"X-Big: " + b"a" * 20000 + b"\r\n\r\n", 431, "head over S-5"),
            (b"POST /echo HTTP/1.1\r\nHost: t\r\nContent-Length: 2000000\r\n\r\n", 413, "body over S-6"),
        ]
        for data, expected, name in cases:
            reply = raw(port, data)
            check(status_of(reply) == expected, f"{name}: expected {expected}, got {reply[:120]!r}")
        reply = raw(port, b"\x16\x03\x01\x02\x00\x01\x00\x01\xfc\x03\x03" + os.urandom(64))
        check(status_of(reply) in (0, 400), f"binary garbage: {reply[:80]!r}")
        status, _, _ = request(port, "GET", "/hello")
        check(status == 200, f"after malformed input: {status}")

        # Load: 1,000 requests over 8 keep-alive connections.
        counts = {"ok": 0, "bad": []}
        lock = threading.Lock()

        def client(index):
            connection = http.client.HTTPConnection("127.0.0.1", port, timeout=60)
            for n in range(125):
                try:
                    connection.request("GET", f"/info?c={index}&n={n}")
                    response = connection.getresponse()
                    data = response.read()
                    good = response.status == 200 and f"c={index}&n={n}".encode() in data
                except Exception as error:  # noqa: BLE001 - recorded as a failure
                    good = False
                    data = repr(error).encode()
                    connection.close()
                    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=60)
                with lock:
                    if good:
                        counts["ok"] += 1
                    elif len(counts["bad"]) < 5:
                        counts["bad"].append(data[:200])
            connection.close()

        started = time.time()
        threads = [threading.Thread(target=client, args=(i,)) for i in range(8)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        elapsed = time.time() - started
        print(f"load: {counts['ok']}/1000 in {elapsed:.1f} s", flush=True)
        check(counts["ok"] == 1000, f"load: {counts['ok']} of 1000 succeeded: {counts['bad']}")
        time.sleep(1.0)
        check(len(host.children()) <= 3, f"worker processes after the load: {host.children()}")

        started = time.time()
        code = host.stop()
        check(code == 0 and time.time() - started < 5, f"drain: status {code} after {time.time() - started:.1f} s")
        check('"message":"stopped"' in host.stderr(), "drain log")
        check("the handler trapped on purpose" in host.stderr(), "trap message in the log")
    finally:
        host.stop()


def test_limits(args, image):
    # One slot and one place in the queue: a worker holds its slot until it
    # has been reaped, so a request that follows a response at once waits
    # for it instead of being refused.
    host = Host(args, image, "--max-workers", "1", "--max-queue", "1", "--ready-workers", "0",
                "--request-timeout", "2", "--max-response-bytes", "2000", "--max-body-bytes", "1024")
    try:
        port = host.port
        status, _, body = request(port, "GET", "/big?2000")
        check(status == 200 and body == b"x" * 2000, f"response within S-7: {status} {len(body)}")
        status, _, _ = request(port, "GET", "/big?2001")
        check(status == 500, f"response over S-7: {status}")
        status, _, body = request(port, "POST", "/echo", body=b"y" * 1024)
        check(status == 201 and len(body) == 1024, f"body at S-6: {status}")
        status, _, _ = request(port, "POST", "/echo", body=b"y" * 1025)
        check(status == 413, f"body over S-6: {status}")

        results = {}

        def slow():
            results["slow"] = request(port, "GET", "/sleep")[0]

        def queued():
            results["queued"] = request(port, "GET", "/hello")[0]

        started = time.time()
        thread = threading.Thread(target=slow)
        thread.start()
        time.sleep(0.5)
        waiting = threading.Thread(target=queued)
        waiting.start()
        time.sleep(0.3)
        status, headers, _ = request(port, "GET", "/hello")
        check(status == 503 and headers.get("retry-after") == "1", f"saturated: {status} {headers}")
        thread.join()
        waiting.join()
        check(results.get("queued") in (200, 503), f"queued behind the slow request: {results}")
        elapsed = time.time() - started
        check(results.get("slow") == 504 and elapsed < 6, f"deadline: {results} after {elapsed:.1f} s")
        time.sleep(0.5)
        status, _, _ = request(port, "GET", "/hello")
        check(status == 200, f"after the deadline: {status}")
    finally:
        host.stop()


def read_until_close(sock):
    chunks = []
    try:
        while True:
            chunk = sock.recv(65536)
            if not chunk:
                break
            chunks.append(chunk)
    except (socket.timeout, ConnectionResetError):
        pass
    return b"".join(chunks)


def test_streaming(args, image):
    host = Host(args, image)
    try:
        port = host.port
        # A streamed response arrives as it is written: the first chunk well
        # before the producer's 300 ms pause ends.
        connection = http.client.HTTPConnection("127.0.0.1", port, timeout=30)
        started = time.time()
        connection.request("GET", "/stream?50")
        response = connection.getresponse()
        first = response.read(1000)
        first_at = time.time() - started
        rest = response.read()
        total_at = time.time() - started
        check(response.status == 200 and response.getheader("transfer-encoding") == "chunked"
              and response.getheader("content-length") is None and response.getheader("x-streamed") == "yes",
              f"streamed head: {response.status} {response.getheaders()}")
        check(first == b"a" * 1000 and len(rest) == 49000, f"streamed body: {len(first)} + {len(rest)}")
        check(total_at - first_at > 0.25, f"the first chunk came before the pause: {first_at:.3f} of {total_at:.3f} s")
        # Keep-alive continues after a streamed response.
        connection.request("GET", "/hello")
        response = connection.getresponse()
        check(response.status == 200 and response.read() == b"hello from http\n", "keep-alive after a stream")
        connection.close()

        # A streamed download past S-7 (8 MiB): not buffered, so not limited.
        status, headers, body = request(port, "GET", "/stream?12000", timeout=120)
        check(status == 200 and len(body) == 12_000_000 and body.count(b"a") == 12_000_000,
              f"12 MB streamed download: {status} {len(body)}")

        status, headers, body = request(port, "HEAD", "/stream?3")
        # HTTP/2 has no transfer codings (RFC 9113, 8.2.2).
        framing = "chunked" if PROTOCOL == "http/1.1" else None
        check(status == 200 and body == b"" and headers.get("transfer-encoding") == framing,
              f"HEAD of a stream: {status} {headers}")

        # A producer that fails after sending bytes: the body ends without its last chunk.
        with socket.create_connection(("127.0.0.1", port), timeout=10) as sock:
            sock.sendall(b"GET /stream-fail HTTP/1.1\r\nHost: t\r\n\r\n")
            reply = read_until_close(sock)
        check(status_of(reply) == 200 and b"7\r\npartial\r\n" in reply and not reply.endswith(b"0\r\n\r\n"),
              f"truncated stream: {reply[-80:]!r}")

        # S-6 is checked as a chunked body arrives, after the worker started.
        big = b"%x\r\n%s\r\n" % (600_000, b"z" * 600_000)
        reply = raw(port, b"POST /echo HTTP/1.1\r\nHost: t\r\nTransfer-Encoding: chunked\r\n\r\n" + big + big
                    + b"0\r\n\r\n")
        check(status_of(reply) == 413, f"chunked body over S-6: {reply[:80]!r}")
        status, _, body = request(port, "POST", "/echo", body=b"q" * 1_000_000)
        check(status == 201 and len(body) == 1_000_000, f"1 MB echo: {status} {len(body)}")
    finally:
        host.stop()
    check("the producer failed on purpose" in host.stderr(), "the failed producer is logged")


def test_streamed_request_body(args, image):
    host = Host(args, image, "--config", "streamed_body=yes", "--max-body-bytes", "64000000")
    try:
        port = host.port

        def pieces():
            for _ in range(200):
                yield b"u" * 100_000

        connection = http.client.HTTPConnection("127.0.0.1", port, timeout=60)
        connection.request("POST", "/upload", body=pieces(), encode_chunked=True)
        response = connection.getresponse()
        body = response.read()
        check(response.status == 200 and body.startswith(b"received 20000000 in "),
              f"20 MB streamed upload: {response.status} {body!r}")
        # The handler answers before reading; the rest of the body is
        # discarded and the connection stays usable.
        connection.request("POST", "/early", body=b"v" * 3_000_000)
        response = connection.getresponse()
        check(response.status == 200 and response.read() == b"early\n", "an answer before the body")
        connection.request("POST", "/upload", body=b"w" * 10)
        response = connection.getresponse()
        check(response.status == 200 and response.read() == b"received 10 in 1 reads\n", "after the early answer")
        connection.close()
        # The worker streams 4 MB back before it reads a 4 MB body: neither
        # side may block the other.
        connection = http.client.HTTPConnection("127.0.0.1", port, timeout=60)
        connection.request("POST", "/stream-first", body=b"x" * 4_000_000)
        response = connection.getresponse()
        data = response.read()
        check(response.status == 200 and len(data) == 4_000_000 + len(b"read 4000000")
              and data.endswith(b"read 4000000"), f"stream before the body: {response.status} {len(data)} {data[-20:]!r}")
        connection.close()
        # A slow client: the handler reads the body as it comes.
        with socket.create_connection(("127.0.0.1", port), timeout=10) as sock:
            sock.sendall(b"POST /upload HTTP/1.1\r\nHost: t\r\nContent-Length: 6\r\nConnection: close\r\n\r\nabc")
            time.sleep(0.5)
            sock.sendall(b"def")
            reply = read_until_close(sock)
        check(status_of(reply) == 200 and reply.endswith(b"received 6 in 2 reads\n"), f"slow body: {reply!r}")
    finally:
        host.stop()


def test_deadline_from_head(args, image):
    # S-8 counts from the end of the head: a slow body plus the handler's
    # one-second nap pass a two-second deadline.
    host = Host(args, image, "--request-timeout", "2")
    try:
        started = time.time()
        with socket.create_connection(("127.0.0.1", host.port), timeout=10) as sock:
            sock.sendall(b"POST /nap HTTP/1.1\r\nHost: t\r\nContent-Length: 2\r\nConnection: close\r\n\r\na")
            time.sleep(1.5)
            sock.sendall(b"b")
            reply = read_until_close(sock)
        check(status_of(reply) == 504 and time.time() - started < 4, f"deadline from the head: {reply[:60]!r}")
    finally:
        host.stop()


def test_keep_alive_latency(args, image):
    # A response leaves without waiting for the client's delayed
    # acknowledgement (TCP_NODELAY, and the head sent with a small body):
    # with Nagle's algorithm each keep-alive response took about 40 ms.
    host = Host(args, image)
    try:
        connection = http.client.HTTPConnection("127.0.0.1", host.port, timeout=30)
        times = []
        for n in range(40):
            started = time.perf_counter()
            connection.request("GET", f"/info?n={n}")
            response = connection.getresponse()
            response.read()
            times.append(time.perf_counter() - started)
            time.sleep(0.01)
        connection.close()
        times.sort()
        median = times[len(times) // 2]
        check(median < 0.03, f"keep-alive median latency {median * 1000:.1f} ms")
    finally:
        host.stop()


def test_admin(args, image):
    host = Host(args, image, "--admin-listen", "127.0.0.1:0")
    try:
        admin = host.ports.get("admin")
        check(admin is not None, f"admin listener: {host.ports}")
        if admin is None:
            return
        status, _, body = request(admin, "GET", "/livez")
        check(status == 200 and body == b"ok\n", f"/livez: {status} {body!r}")
        status, _, body = request(admin, "GET", "/readyz")
        check(status == 200 and body == b"ready\n", f"/readyz: {status} {body!r}")
        status, _, _ = request(admin, "POST", "/livez", body=b"")
        check(status == 405, f"POST /livez: {status}")
        status, _, _ = request(admin, "GET", "/nothing")
        check(status == 404, f"admin 404: {status}")
        for _ in range(2):
            status, _, _ = request(host.port, "GET", "/metric")
            check(status == 200, f"/metric: {status}")
        status, headers, body = request(admin, "GET", "/metrics")
        text = body.decode()
        check(status == 200 and headers.get("content-type", "").startswith("text/plain; version=0.0.4"),
              f"/metrics: {status} {headers}")
        check("test_requests_total 4" in text and "test_depth -5" in text, f"application metrics: {text[-300:]}")
        check('neura_http_responses_total{class="2xx"}' in text and "neura_ready 1" in text, f"host metrics: {text[:400]}")
        check("# TYPE test_requests_total counter" in text and "# TYPE test_depth gauge" in text, "metric types")
    finally:
        host.stop()


def test_dead_ready_worker(args, image):
    # A ready worker that has ended is never given a request: the host
    # discards it and the request runs once, in another worker.
    host = Host(args, image, "--ready-workers", "2")
    try:
        deadline = time.time() + 10
        while len(host.children()) < 2 and time.time() < deadline:
            time.sleep(0.05)
        ready = host.children()
        check(len(ready) == 2, f"two ready workers: {ready}")
        for pid in ready:
            os.kill(pid, signal.SIGKILL)
        time.sleep(0.2)
        status, _, body = request(host.port, "GET", "/hello")
        check(status == 200 and body == b"hello from http\n", f"after the ready workers died: {status} {body!r}")
    finally:
        host.stop()
    check("a ready worker had ended" in host.stderr(), "the discarded worker is logged")


def test_refused_assignment(args, image):
    # A worker whose channel refuses the Assign has run nothing: the request
    # goes to a new worker, once, and runs exactly once. The test hook
    # NEURA_SERVER_TEST_REFUSE_ASSIGN=N makes the first N workers refuse.
    for start in ("zygote", "exec"):
        for ready in ("0", "1"):
            env = dict(os.environ, NEURA_SERVER_TEST_REFUSE_ASSIGN="1")
            host = Host(args, image, "--worker-start", start, "--ready-workers", ready, "--no-startup-check",
                        "--admin-listen", "127.0.0.1:0", env=env)
            label = f"{start}, {ready} ready"
            try:
                if ready == "1":
                    # The ready worker took the refusal; it must be in the
                    # pool before the request, or the request starts its own.
                    deadline = time.time() + 10
                    while metric(host.ports["admin"], "neura_workers_ready") != 1 and time.time() < deadline:
                        time.sleep(0.01)
                status, _, _ = request(host.port, "GET", "/metric")
                check(status == 200, f"refused assignment ({label}): {status}")
                admin = host.ports["admin"]
                check(metric(admin, "test_requests_total") == 2, f"the request ran once ({label})")
                check(metric(admin, "neura_worker_retries_total") == 1, f"one retry ({label})")
                # A worker that took its Assign is never replaced.
                status, _, _ = request(host.port, "GET", "/trap")
                check(status == 500, f"trap after a retry ({label}): {status}")
                check(metric(admin, "neura_worker_retries_total") == 1, f"a trap is not retried ({label})")
            finally:
                host.stop()
            check("a worker refused its assignment; using a new one" in host.stderr(), f"retry logged ({label})")
    # The new worker refuses too: no third worker, the client gets 500.
    env = dict(os.environ, NEURA_SERVER_TEST_REFUSE_ASSIGN="2")
    host = Host(args, image, "--ready-workers", "0", "--no-startup-check", "--admin-listen", "127.0.0.1:0", env=env)
    try:
        status, _, body = request(host.port, "GET", "/metric")
        check(status == 500 and body == b'{"error":"internal"}', f"two refusals: {status} {body!r}")
        check(metric(host.ports["admin"], "test_requests_total") is None, "nothing ran after two refusals")
        check(metric(host.ports["admin"], "neura_worker_retries_total") == 1, "two refusals are one retry")
        status, _, _ = request(host.port, "GET", "/hello")
        check(status == 200, f"after two refusals: {status}")
    finally:
        host.stop()
    check("the new worker refused its assignment too" in host.stderr(), "second refusal logged")


def test_zygote_restart(args, image):
    # A zygote that ends is replaced; requests are still served.
    host = Host(args, image, "--ready-workers", "0")
    try:
        zygotes = host.children(zygote=True)
        check(len(zygotes) == 1, f"one zygote: {zygotes}")
        for pid in zygotes:
            os.kill(pid, signal.SIGKILL)
        time.sleep(0.2)
        status, _, body = request(host.port, "GET", "/hello")
        check(status == 200 and body == b"hello from http\n", f"after the zygote died: {status} {body!r}")
        replaced = host.children(zygote=True)
        check(len(replaced) == 1 and replaced != zygotes, f"a new zygote: {zygotes} then {replaced}")
    finally:
        host.stop()
    check("the worker zygote ended; starting a new one" in host.stderr(), "the zygote restart is logged")


def test_process_bound(args, image):
    # A worker holds its S-1 slot until it has been reaped, so the workers,
    # exited or not, never outnumber S-1 plus S-2 (B-S4).
    for start in ("zygote", "exec"):
        host = Host(args, image, "--worker-start", start, "--max-workers", "2", "--ready-workers", "1",
                    "--max-queue", "256")
        peak = [0]
        stop = threading.Event()

        def sample():
            while not stop.is_set():
                peak[0] = max(peak[0], len(host.children()))
                time.sleep(0.002)

        def load(statuses):
            connection = http.client.HTTPConnection("127.0.0.1", host.port, timeout=30)
            for _ in range(25):
                connection.request("GET", "/info")
                response = connection.getresponse()
                response.read()
                statuses.append(response.status)
            connection.close()

        try:
            sampler = threading.Thread(target=sample)
            sampler.start()
            statuses = []
            threads = [threading.Thread(target=load, args=(statuses,)) for _ in range(12)]
            for thread in threads:
                thread.start()
            for thread in threads:
                thread.join()
            stop.set()
            sampler.join()
            check(statuses.count(200) == 300, f"bounded load ({start}): {sorted(set(statuses))} {len(statuses)}")
            check(0 < peak[0] <= 3, f"workers at the peak ({start}): {peak[0]}, the bound is 3")
            check(len(host.children(zygote=True)) == (1 if start == "zygote" else 0), f"zygote count ({start})")
        finally:
            stop.set()
            host.stop()


def test_step_limit(args, image):
    # --max-steps bounds each request's execution; past it the execution
    # traps, the client gets 500, and the next request runs in a new worker.
    host = Host(args, image, "--max-steps", "2000000")
    try:
        status, _, body = request(host.port, "GET", "/spin?1000")
        check(status == 200 and body.startswith(b"spun "), f"within the step limit: {status} {body!r}")
        status, _, body = request(host.port, "GET", "/spin?5000000")
        check(status == 500 and body == b'{"error":"internal"}', f"past the step limit: {status} {body!r}")
        status, _, _ = request(host.port, "GET", "/hello")
        check(status == 200, f"after the step limit: {status}")
    finally:
        host.stop()
    check("2000000 steps" in host.stderr(), "the step-limit trap is logged")


def test_drain_completes_responses(args, image):
    host = Host(args, image, "--drain-timeout", "10")
    try:
        results = {}

        def nap():
            results["nap"] = request(host.port, "GET", "/nap")

        thread = threading.Thread(target=nap)
        thread.start()
        time.sleep(0.4)
        host.process.send_signal(signal.SIGTERM)
        thread.join()
        code = host.process.wait(15)
        status, _, body = results.get("nap", (0, {}, b""))
        check(status == 200 and body == b"rested\n", f"a request in flight during the drain: {status} {body!r}")
        check(code == 0, f"drain status: {code}")
    finally:
        host.stop()


def test_startup_check(args, image):
    host = Host(args, image, "--config", "refuse_startup=yes", expect_ready=False)
    code = host.process.wait(30)
    check(code == 1 and not host.ports, f"refused startup: status {code}, ports {host.ports}")
    check("startup check failed" in host.stderr(), "startup failure is reported")
    host.stop()


def test_record_replay(args, image):
    directory = Path(args.work) / "traces"
    shutil.rmtree(directory, ignore_errors=True)
    directory.mkdir()
    host = Host(args, image, "--record-dir", str(directory), "--no-startup-check", "--ready-workers", "0")
    try:
        status, _, _ = request(host.port, "POST", "/echo", body=b"recorded")
        check(status == 201, f"recorded request: {status}")
    finally:
        host.stop()
    traces = sorted(directory.glob("*.trace"))
    check(len(traces) == 1, f"one trace per request: {traces}")
    if traces:
        result = subprocess.run([args.neura, "run", "--replay", str(traces[0]), str(image)],
                                capture_output=True, text=True, timeout=60)
        check(result.returncode == 0, f"replay without a host: {result.returncode} {result.stderr}")


def test_without_host(args, image):
    result = subprocess.run([args.neura, "run", str(image)], capture_output=True, text=True, timeout=60)
    check(result.returncode == 1 and "run this image with neura-server-host" in result.stderr,
          f"neura run of a server image: {result.returncode} {result.stderr}")


def test_tls(args, image):
    if not args.tls:
        result = subprocess.run([args.host, "--tls-listen", "127.0.0.1:0", "--cert", "c.pem", "--key", "k.pem",
                                 str(image)], capture_output=True, text=True, timeout=60)
        check(result.returncode == 1 and "TLS" in result.stderr, f"no TLS build: {result.stderr}")
        return
    directory = Path(args.work) / "tls"
    directory.mkdir(exist_ok=True)
    certificate = directory / "cert.pem"
    key = directory / "key.pem"
    subprocess.run([args.openssl, "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", str(key), "-out",
                    str(certificate), "-days", "2", "-subj", "/CN=localhost", "-addext",
                    "subjectAltName=IP:127.0.0.1,DNS:localhost"], check=True, capture_output=True)
    host = Host(args, image, "--tls-listen", "127.0.0.1:0", "--cert", str(certificate), "--key", str(key))
    try:
        check("https" in host.ports and "http" in host.ports, f"both listeners: {host.ports}")
        context = ssl.create_default_context(cafile=str(certificate))
        connection = http.client.HTTPSConnection("127.0.0.1", host.ports["https"], context=context, timeout=30)
        connection.request("GET", "/hello")
        response = connection.getresponse()
        body = response.read()
        check(response.status == 200 and body == b"hello from https\n", f"HTTPS GET: {response.status} {body!r}")
        connection.request("POST", "/echo", body=b"over tls", headers={"Content-Type": "text/plain"})
        response = connection.getresponse()
        body = response.read()
        check(response.status == 201 and body == b"over tls", f"HTTPS POST: {response.status} {body!r}")
        connection.close()
        status, _, body = request(host.port, "GET", "/hello")
        check(status == 200 and body == b"hello from http\n", f"plain beside TLS: {status} {body!r}")
        if args.curl:
            for url, expected in ((f"https://127.0.0.1:{host.ports['https']}/hello", "hello from https"),
                                  (f"http://127.0.0.1:{host.port}/hello", "hello from http")):
                result = subprocess.run([args.curl, "-sS", "--cacert", str(certificate), url], capture_output=True,
                                        text=True, timeout=30)
                check(result.returncode == 0 and result.stdout.strip() == expected,
                      f"curl {url}: {result.returncode} {result.stdout!r} {result.stderr}")
            result = subprocess.run([args.curl, "-sS", "--cacert", str(certificate), "-X", "POST", "--data-binary",
                                     "curl body", f"https://127.0.0.1:{host.ports['https']}/echo"],
                                    capture_output=True, text=True, timeout=30)
            check(result.returncode == 0 and result.stdout == "curl body", f"curl POST: {result.stdout!r}")
            # curl --http2 offers h2 and http/1.1 by ALPN. A build with
            # HTTP/2 selects h2 (decision 9 as amended), one without it
            # http/1.1.
            expected = "2" if args.http2_client else "1.1"
            result = subprocess.run([args.curl, "-sS", "--http2", "--cacert", str(certificate), "-o", "/dev/null",
                                     "-w", "%{http_version}", f"https://127.0.0.1:{host.ports['https']}/hello"],
                                    capture_output=True, text=True, timeout=30)
            check(result.returncode == 0 and result.stdout == expected,
                  f"curl --http2 negotiates {expected}: {result.stdout!r} {result.stderr}")
        # A plain HTTP request on the TLS port fails the handshake and leaves the host serving.
        raw(host.ports["https"], b"GET / HTTP/1.1\r\nHost: t\r\n\r\n", timeout=3)
        status, _, _ = request(host.port, "GET", "/hello")
        check(status == 200, f"after a failed handshake: {status}")
    finally:
        host.stop()
    missing = subprocess.run([args.host, "--tls-listen", "127.0.0.1:0", "--cert", str(directory / "none.pem"),
                              "--key", str(key), str(image)], capture_output=True, text=True, timeout=60)
    check(missing.returncode == 1, f"missing certificate: {missing.returncode} {missing.stderr}")


def tls_material(args, name="tls", subject="localhost"):
    directory = Path(args.work) / name
    directory.mkdir(exist_ok=True)
    certificate = directory / "cert.pem"
    key = directory / "key.pem"
    subprocess.run([args.openssl, "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", str(key), "-out",
                    str(certificate), "-days", "2", "-subj", f"/CN={subject}", "-addext",
                    "subjectAltName=IP:127.0.0.1,DNS:localhost"], check=True, capture_output=True)
    return certificate, key


def curl_version(args, url, *options, cafile=None):
    command = [args.curl, "-sS", "-o", "-", "-w", "\n%{http_version}", *options]
    if cafile:
        command += ["--cacert", str(cafile)]
    result = subprocess.run(command + [url], capture_output=True, text=True, timeout=30)
    body, _, version = result.stdout.rpartition("\n")
    return result.returncode, version, body


def test_http2_curl(args, image):
    # HTTP/2 by ALPN on TLS listeners (on by default, off with --no-http2),
    # and h2c with prior knowledge only with --h2c (decision 9 as amended).
    if not args.http2_client:
        result = subprocess.run([args.host, "--listen", "127.0.0.1:0", "--h2c", str(image)], capture_output=True,
                                text=True, timeout=60)
        check(result.returncode == 1 and "no HTTP/2" in result.stderr, f"--h2c without HTTP/2: {result.stderr}")
        return
    if not args.curl:
        print("skipped: no curl")
        return
    if args.tls:
        certificate, key = tls_material(args, "h2-tls")
        host = Host(args, image, "--tls-listen", "127.0.0.1:0", "--cert", str(certificate), "--key", str(key))
        try:
            url = f"https://127.0.0.1:{host.ports['https']}"
            code, version, body = curl_version(args, url + "/hello", "--http2", cafile=certificate)
            check(code == 0 and version == "2" and body == "hello from https\n", f"h2 by ALPN: {code} {version} {body!r}")
            code, version, _ = curl_version(args, url + "/hello", "--http1.1", cafile=certificate)
            check(code == 0 and version == "1.1", f"http/1.1 by ALPN beside h2: {code} {version}")
            code, version, body = curl_version(args, url + "/echo", "--http2", "-X", "POST", "--data-binary",
                                               "h2 body", cafile=certificate)
            check(code == 0 and version == "2" and body == "h2 body", f"h2 POST: {code} {version} {body!r}")
            code, version, body = curl_version(args, url + "/stream?3", "--http2", cafile=certificate)
            check(code == 0 and version == "2" and body == "a" * 3000, f"h2 streamed response: {code} {len(body)}")
            # The plain listener is not h2c without --h2c.
            code, _, _ = curl_version(args, f"http://127.0.0.1:{host.port}/hello", "--http2-prior-knowledge")
            check(code != 0, f"h2c refused without --h2c: {code}")
        finally:
            host.stop()
        host = Host(args, image, "--tls-listen", "127.0.0.1:0", "--cert", str(certificate), "--key", str(key),
                    "--no-http2")
        try:
            code, version, _ = curl_version(args, f"https://127.0.0.1:{host.ports['https']}/hello", "--http2",
                                            cafile=certificate)
            check(code == 0 and version == "1.1", f"--no-http2 negotiates 1.1: {code} {version}")
        finally:
            host.stop()
    host = Host(args, image, "--h2c")
    try:
        url = f"http://127.0.0.1:{host.port}/hello"
        code, version, body = curl_version(args, url, "--http2-prior-knowledge")
        check(code == 0 and version == "2" and body == "hello from http\n", f"h2c: {code} {version} {body!r}")
        code, version, body = curl_version(args, url, "--http1.1")
        check(code == 0 and version == "1.1" and body == "hello from http\n", f"1.1 beside h2c: {code} {version}")
        # No Upgrade: h2c (RFC 9113 deprecates it): served as HTTP/1.1.
        code, version, _ = curl_version(args, url, "--http2")
        check(code == 0 and version == "1.1", f"Upgrade: h2c is not taken: {code} {version}")
    finally:
        host.stop()


def test_http2_streams(args, image):
    # The HTTP/2 framing behaviors, with the scripted client over h2c.
    if not args.http2_client:
        return
    host = Host(args, image, "--h2c", "--admin-listen", "127.0.0.1:0", "--max-workers", "8")
    try:
        port = host.port
        # Interleaving: four 1 s requests on one connection run at once.
        result = h2(port, "interleave", "--path", "/nap", "--count", 4)
        streams = result["streams"]
        last = max(stream["closed_ms"] for stream in streams)
        check([stream["status"] for stream in streams] == [200] * 4 and all(s["body"] == "rested\n" for s in streams),
              f"interleaved streams: {result}")
        check(last < 2500, f"four 1 s streams on one connection took {last:.0f} ms")

        # Flow control is the backpressure: with a 16 KiB window and no
        # window update the response stops at the window, and another stream
        # on the connection is still answered; once the window opens the
        # whole body arrives.
        result = h2(port, "stall", "--path", "/big?300000", "--window", 16384, "--hold-ms", 1500)
        big = result["streams"][0]
        check(result["bytes_at_hold_end"] == 16384, f"stalled at the window: {result['bytes_at_hold_end']}")
        check(result.get("hello_during_stall") is True, f"another stream during the stall: {result}")
        check(big["status"] == 200 and big["bytes"] == 300000 and big["closed"], f"after the stall: {big}")

        # RST_STREAM cancels the stream's worker at once, and the
        # connection goes on.
        result = h2(port, "reset", "--path", "/sleep", "--after-ms", 500)
        first, hello = result["streams"]
        check(first["error"] == 8 and hello["status"] == 200, f"reset then hello: {result}")
        deadline = time.time() + 5
        while metric(host.ports["admin"], "neura_workers_running") != 0 and time.time() < deadline:
            time.sleep(0.05)
        check(metric(host.ports["admin"], "neura_workers_running") == 0, "the reset stream's worker is gone")
        check("the client cancelled the request; worker killed" in host.stderr(), "the cancel is logged")

        # S-5 applies to the decoded header list: 431.
        result = h2(port, "headers", "--path", "/hello", "--header-bytes", 20000)
        check(result["streams"][0]["status"] == 431, f"oversized header list: {result}")
        result = h2(port, "headers", "--path", "/hello", "--header-bytes", 8000)
        check(result["streams"][0]["status"] == 200, f"header list within S-5: {result}")
    finally:
        host.stop()

    # S-14: streams beyond --max-streams are refused (REFUSED_STREAM), even
    # from a client that ignores the server's setting.
    host = Host(args, image, "--h2c", "--max-streams", "2")
    try:
        result = h2(host.port, "streams", "--path", "/nap", "--count", 5)
        statuses = [stream["status"] for stream in result["streams"]]
        refused = [stream for stream in result["streams"] if stream["error"] == 7 and stream["status"] == 0]
        check(statuses.count(200) == 2 and len(refused) == 3, f"too many streams: {result}")
        status, _, _ = request(host.port, "GET", "/hello")
        check(status == 200, f"after refused streams: {status}")
    finally:
        host.stop()

    # Drain: SIGTERM sends GOAWAY; the streams in flight finish and no new
    # stream is taken.
    host = Host(args, image, "--h2c")
    try:
        result = h2(host.port, "drain", "--path", "/nap", "--count", 2, "--after-ms", 300,
                    "--pid", host.process.pid)
        statuses = [stream["status"] for stream in result["streams"][:2]]
        check(result["goaway"] and result["goaway_error"] == 0 and statuses == [200, 200],
              f"drain with streams in flight: {result}")
        late = result["streams"][2:] if len(result["streams"]) > 2 else []
        check(all(stream["status"] == 0 for stream in late), f"no stream after GOAWAY: {result}")
        check(host.process.wait(30) == 0, "the host exits 0 after the drain")
    finally:
        host.stop()


def test_http2_reload(args, image):
    # SIGHUP rereads the certificate; h2 is still offered by the new context.
    if not (args.http2_client and args.tls and args.openssl):
        return
    certificate, key = tls_material(args, "h2-reload", "first")
    host = Host(args, image, "--tls-listen", "127.0.0.1:0", "--cert", str(certificate), "--key", str(key))
    try:
        port = host.ports["https"]

        def handshake():
            result = subprocess.run([args.openssl, "s_client", "-connect", f"127.0.0.1:{port}", "-alpn", "h2,http/1.1"],
                                    input="", capture_output=True, text=True, timeout=30)
            return result.stdout

        before = handshake()
        check("ALPN protocol: h2" in before and "CN=first" in before, "h2 with the first certificate")
        fresh, fresh_key = tls_material(args, "h2-reload-next", "second")
        shutil.copy(fresh, certificate)
        shutil.copy(fresh_key, key)
        host.process.send_signal(signal.SIGHUP)
        deadline = time.time() + 10
        after = ""
        while time.time() < deadline:
            after = handshake()
            if "CN=second" in after:
                break
            time.sleep(0.2)
        check("ALPN protocol: h2" in after and "CN=second" in after, "h2 with the reloaded certificate")
        if args.curl:
            code, version, body = curl_version(args, f"https://127.0.0.1:{port}/hello", "--http2", cafile=fresh)
            check(code == 0 and version == "2" and body == "hello from https\n", f"h2 after SIGHUP: {code} {version}")
    finally:
        host.stop()


def main():
    global PROTOCOL, HTTP2_CLIENT  # noqa: PLW0603 - set once from the command line
    parser = argparse.ArgumentParser()
    parser.add_argument("--neura", required=True)
    parser.add_argument("--host", required=True)
    parser.add_argument("--source", required=True)
    parser.add_argument("--work", required=True)
    parser.add_argument("--openssl")
    parser.add_argument("--curl")
    parser.add_argument("--tls", action="store_true")
    parser.add_argument("--only")
    parser.add_argument("--http2-client", help="tests/server/http2_test_client.cpp, in builds with HTTP/2")
    parser.add_argument("--protocol", choices=["http/1.1", "h2c"], default="http/1.1")
    args = parser.parse_args()
    PROTOCOL = args.protocol
    HTTP2_CLIENT = args.http2_client
    if PROTOCOL == "h2c" and not HTTP2_CLIENT:
        print("--protocol h2c needs --http2-client")
        return 1
    os.makedirs(args.work, exist_ok=True)
    image = Path(args.work) / "test-server.nvr"
    build = subprocess.run([args.neura, "build", str(Path(args.source) / "tests/server/fixtures/test-server.nra"),
                            "-o", str(image)], capture_output=True, text=True)
    if build.returncode != 0:
        print(build.stdout + build.stderr)
        return 1
    tests = [test_basic, test_limits, test_step_limit, test_streaming, test_streamed_request_body,
             test_deadline_from_head, test_keep_alive_latency, test_admin, test_dead_ready_worker, test_refused_assignment, test_zygote_restart,
             test_process_bound, test_drain_completes_responses, test_startup_check, test_record_replay, test_without_host, test_tls,
             test_http2_curl, test_http2_streams, test_http2_reload]
    for test in tests:
        if args.only and args.only != test.__name__:
            continue
        if PROTOCOL == "h2c" and test.__name__.startswith("test_http2"):
            continue  # They choose their protocols themselves.
        print("--", test.__name__, flush=True)
        try:
            test(args, image)
        except Exception as error:  # noqa: BLE001 - reported as a failure
            check(False, f"{test.__name__}: {error!r}")
    if FAILURES:
        print(f"{len(FAILURES)} failure(s)")
        return 1
    print("all server host checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
