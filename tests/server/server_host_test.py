#!/usr/bin/env python3
"""End-to-end tests of neura-server-host with a Neura server image.

Builds tests/server/fixtures/test-server.nra, runs the host on loopback with
ephemeral ports, and checks requests over plain HTTP and HTTPS, malformed and
oversized input, limits, traps, configuration, record and replay, a
concurrent load of 1,000 requests, and the drain (docs/design/Backend030.md).
"""

import argparse
import http.client
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


def check(condition, message):
    if not condition:
        FAILURES.append(message)
        print("FAIL:", message, flush=True)


class Host:
    def __init__(self, args, image, *options, expect_ready=True, env=None):
        self.err_path = Path(tempfile.mkstemp(prefix="host-", suffix=".err", dir=args.work)[1])
        self.err = open(self.err_path, "w")
        command = [args.host, "--listen", "127.0.0.1:0", *options, str(image)]
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

    def children(self):
        try:
            text = Path(f"/proc/{self.process.pid}/task/{self.process.pid}/children").read_text()
            return [int(pid) for pid in text.split()]
        except OSError:
            return []

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


def request(port, method, path, body=None, headers=None, timeout=30):
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
    host = Host(args, image, "--max-workers", "1", "--max-queue", "0", "--ready-workers", "0",
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

        started = time.time()
        thread = threading.Thread(target=slow)
        thread.start()
        time.sleep(0.5)
        status, headers, _ = request(port, "GET", "/hello")
        check(status == 503 and headers.get("retry-after") == "1", f"saturated: {status} {headers}")
        thread.join()
        elapsed = time.time() - started
        check(results.get("slow") == 504 and elapsed < 6, f"deadline: {results} after {elapsed:.1f} s")
        time.sleep(0.5)
        status, _, _ = request(port, "GET", "/hello")
        check(status == 200, f"after the deadline: {status}")
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
        # A plain HTTP request on the TLS port fails the handshake and leaves the host serving.
        raw(host.ports["https"], b"GET / HTTP/1.1\r\nHost: t\r\n\r\n", timeout=3)
        status, _, _ = request(host.port, "GET", "/hello")
        check(status == 200, f"after a failed handshake: {status}")
    finally:
        host.stop()
    missing = subprocess.run([args.host, "--tls-listen", "127.0.0.1:0", "--cert", str(directory / "none.pem"),
                              "--key", str(key), str(image)], capture_output=True, text=True, timeout=60)
    check(missing.returncode == 1, f"missing certificate: {missing.returncode} {missing.stderr}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--neura", required=True)
    parser.add_argument("--host", required=True)
    parser.add_argument("--source", required=True)
    parser.add_argument("--work", required=True)
    parser.add_argument("--openssl")
    parser.add_argument("--curl")
    parser.add_argument("--tls", action="store_true")
    parser.add_argument("--only")
    args = parser.parse_args()
    os.makedirs(args.work, exist_ok=True)
    image = Path(args.work) / "test-server.nvr"
    build = subprocess.run([args.neura, "build", str(Path(args.source) / "tests/server/fixtures/test-server.nra"),
                            "-o", str(image)], capture_output=True, text=True)
    if build.returncode != 0:
        print(build.stdout + build.stderr)
        return 1
    tests = [test_basic, test_limits, test_startup_check, test_record_replay, test_without_host, test_tls]
    for test in tests:
        if args.only and args.only != test.__name__:
            continue
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
